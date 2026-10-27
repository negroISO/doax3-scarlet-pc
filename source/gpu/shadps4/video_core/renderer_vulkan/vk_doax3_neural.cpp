// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_doax3_neural.h"
#include "video_core/renderer_vulkan/vk_camera_motion.h"
#include "video_core/renderer_vulkan/vk_dlss.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_gpu_profiler.h"
#include "video_core/renderer_vulkan/vk_scene_resolution.h"
#include "video_core/renderer_vulkan/vk_temporal_upscaler.h"
#include "port_settings.h"
#include "common/scope_exit.h"
#include "video_core/texture_cache/texture_cache.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vk_mem_alloc.h>

namespace Vulkan {
namespace {
void Dump(const Instance &instance, Scheduler &scheduler, vk::CommandBuffer cmd,
          vk::Image image, vk::ImageAspectFlags aspect, u32 width, u32 height,
          u32 index, const char *label) {
  const VkBufferCreateInfo ci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                              .size = VkDeviceSize(width) * height * 4,
                              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
  const VmaAllocationCreateInfo ac{
      .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
               VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST};
  VkBuffer buffer{};
  VmaAllocation allocation{};
  VmaAllocationInfo info{};
  if (vmaCreateBuffer(instance.GetAllocator(), &ci, &ac, &buffer, &allocation,
                      &info) != VK_SUCCESS)
    return;
  const vk::MemoryBarrier2 barrier{
      .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
      .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
      .dstAccessMask = vk::AccessFlagBits2::eTransferRead};
  cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});
  cmd.copyImageToBuffer(
      image, vk::ImageLayout::eGeneral, buffer,
      vk::BufferImageCopy{.imageSubresource = {aspect, 0, 0, 1},
                          .imageExtent = {width, height, 1}});
  const char *directory = std::getenv("THEORDER_DUMP_DIR");
  char path[1024];
  std::snprintf(path, sizeof(path), "%s/nr_%03u_%s_%ux%u.raw",
                directory ? directory : ".", index, label, width, height);
  scheduler.DeferPriorityOperation([allocator = instance.GetAllocator(), buffer,
                                    allocation, info, size = ci.size,
                                    path = std::string{path}] {
    vmaInvalidateAllocation(allocator, allocation, 0, VK_WHOLE_SIZE);
    if (FILE *file = std::fopen(path.c_str(), "wb")) {
      std::fwrite(info.pMappedData, 1, size, file);
      std::fclose(file);
    }
    vmaDestroyBuffer(allocator, buffer, allocation);
  });
}
} // namespace

Doax3Neural::Doax3Neural(const Instance &i, Scheduler &s, Runtime &r,
                         VideoCore::TextureCache &t, CameraMotion &c, SceneTargets &targets)
    : instance{i}, scheduler{s}, runtime{r}, textures{t}, camera{c},
      scenes{targets}, motion{i.GetDevice(), i.GetAllocator()},
      dlss_input{i.GetDevice(), i.GetAllocator()}, dlss_output{i.GetDevice(), i.GetAllocator()} {
  enabled = camera.UsesDoax3Constants();
  PortSettings::Get().doax3_scene = enabled;
  if (enabled) {
    auto& settings = PortSettings::Get();
    settings.fps_limit = 30;
    settings.aspect = settings.startup_aspect = 1; // DOAX3 fills its native 16:9 frame.
    settings.engine_fps = settings.startup_engine_fps = 60;
    settings.applied_engine_fps = 60;
  }
  const char *guides = std::getenv("THEORDER_NR_GUIDES_ONLY");
  guides_only = guides && guides[0] == '1';
}
Doax3Neural::~Doax3Neural() {
  if (initialized)
    scheduler.Finish();
}

void Doax3Neural::Record(VideoCore::ImageId color_id, TemporalUpscaler &upscaler) {
  if (!enabled || !camera.Ready() || !color_id)
    return;
  auto &color = textures.GetImage(color_id);
  auto &depth = textures.GetImage(camera.Depth());
  const auto &settings = PortSettings::Get();
  const bool neural_on = settings.neural;
  const bool dlss_on = settings.upscaler == PortSettings::UpscalerDlss && settings.dlss_supported;
  const int output = std::clamp(settings.output_res.load(), 0, PortSettings::OutputCount - 1);
  const u32 ow = PortSettings::OutputWidths[output], oh = PortSettings::OutputHeights[output];
  if (!neural_on && !dlss_on && ow == 1920 && oh == 1080 && !guides_only) return;
  const auto size = scenes.Reduced() ? scenes.Size() : SceneResolution::Size{};
  const u32 w = size.width, h = size.height;
  if (color.info.size.width != 1920 || color.info.size.height != 1080 ||
      depth.info.size.width != 1920 || depth.info.size.height != 1080 ||
      color.info.pixel_format != vk::Format::eR8G8B8A8Srgb ||
      depth.info.pixel_format != vk::Format::eD32SfloatS8Uint)
    return;
  Dlss *dlss = guides_only ? nullptr : Dlss::Get();
  const bool nr_available = neural_on && dlss && dlss->NeuralAvailable();
  if (neural_on && !guides_only && !nr_available) {
    PortSettings::Get().neural_problem = "NR unavailable; scene preserved";
    if (!noted) {
      std::printf("DOAX3 NR: native frame preserved (%s)\n",
                  dlss ? dlss->NeuralProblem().c_str()
                       : "bridge/runtime missing");
      noted = true;
    }
  }
  const bool resized = width != w || height != h || output_width != ow || output_height != oh;
  const bool nr_changed = applied_nr_preset != settings.nr_preset || applied_nr_passes != settings.nr_passes;
  if (resized || nr_changed) {
    scheduler.Finish();
    if (dlss) dlss->ReleaseNeural();
    if (resized && dlss) dlss->ReleaseFeature();
    if (resized) {
      motion_view.reset();
      motion = VideoCore::UniqueImage(instance.GetDevice(), instance.GetAllocator());
      dlss_input_view.reset();
      dlss_output_view.reset();
      dlss_input = VideoCore::UniqueImage(instance.GetDevice(), instance.GetAllocator());
      dlss_output = VideoCore::UniqueImage(instance.GetDevice(), instance.GetAllocator());
      initialized = false;
    }
    width = w; height = h; output_width = ow; output_height = oh;
    applied_nr_preset = settings.nr_preset;
    applied_nr_passes = settings.nr_passes;
    frames = 0;
  }
  const Dlss::FeatureDesc desc{w, h, ow, oh, settings.preset.load(), false,
                              settings.dlss_model.load(), camera.DepthInverted()};
  bool dlss_ready = dlss_on && dlss && dlss->Available() && !dlss->RejectedFeature(desc);
  if (dlss_ready && !dlss->HasFeature(desc)) {
    scheduler.Finish();
    dlss->ReleaseFeature();
    dlss_ready = dlss->CreateFeature(scheduler.CommandBuffer(), desc);
    frames = 0;
  }
  auto* profiler = GpuProfiler::Get();
  const bool timed = profiler && profiler->Records(&scheduler);
  const u64 previous_label = timed ? profiler->Current() : 0;
  if (timed)
    profiler->Mark(0xD0A30018ull, [] { return std::string{"DOAX3 native NR + motion/depth guides"}; });
  SCOPE_EXIT {
    if (timed) profiler->Resume(previous_label);
  };
  const auto device = instance.GetDevice();
  if (!motion_view) {
    width = w;
    height = h;
    motion.Create({.imageType = vk::ImageType::e2D,
                   .format = vk::Format::eR16G16Sfloat,
                   .extent = {w, h, 1},
                   .mipLevels = 1,
                   .arrayLayers = 1,
                   .samples = vk::SampleCountFlagBits::e1,
                   .tiling = vk::ImageTiling::eOptimal,
                   .usage = vk::ImageUsageFlagBits::eStorage |
                            vk::ImageUsageFlagBits::eSampled |
                            vk::ImageUsageFlagBits::eTransferSrc});
    motion_view = Check(device.createImageViewUnique(
        {.image = vk::Image(motion),
         .viewType = vk::ImageViewType::e2D,
         .format = vk::Format::eR16G16Sfloat,
         .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}));
  }
  vk::Image color_image = color.backing->image, depth_image = depth.backing->image;
  vk::ImageView depth_view, color_view;
  const bool proxy = scenes.Reduced();
  if (proxy) {
    VideoCore::ImageViewInfo color_info{}, depth_info{};
    color_info.format = color.info.pixel_format;
    depth_info.format = depth.info.pixel_format;
    const auto c = scenes.Read(color_id, color_info, vk::PipelineStageFlagBits2::eAllCommands,
                              vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
    const auto d = scenes.Read(camera.Depth(), depth_info, vk::PipelineStageFlagBits2::eAllCommands,
                              vk::AccessFlagBits2::eMemoryRead);
    color_image = c.image; color_view = c.view;
    depth_image = d.image; depth_view = d.view;
  } else {
    const vk::ImageViewUsageCreateInfo sampled_usage{.usage = vk::ImageUsageFlagBits::eSampled};
    depth_view = Check(device.createImageView(
      {.pNext = &sampled_usage, .image = vk::Image(depth.backing->image),
       .viewType = vk::ImageViewType::e2D,
       .format = depth.info.pixel_format,
       .subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}}));
    color_view = Check(device.createImageView(
      {.pNext = &sampled_usage, .image = vk::Image(color.backing->image),
       .viewType = vk::ImageViewType::e2D,
       .format = color.info.pixel_format,
       .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}));
  }
  scheduler.EndRendering();
  if (!proxy) {
    runtime.Transit(&depth, vk::ImageLayout::eGeneral,
                  vk::PipelineStageFlagBits2::eAllCommands,
                  vk::AccessFlagBits2::eMemoryRead);
  runtime.Transit(&color, vk::ImageLayout::eGeneral,
                  vk::PipelineStageFlagBits2::eAllCommands,
                  vk::AccessFlagBits2::eMemoryRead |
                      vk::AccessFlagBits2::eMemoryWrite);
  runtime.FlushBarriers();
  }
  const auto cmd = scheduler.CommandBuffer(); // synchronize deferred recording
                                              // before reading camera
  const vk::ImageMemoryBarrier2 to_write{
      .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
      .srcAccessMask =
          initialized ? vk::AccessFlagBits2::eMemoryRead : vk::AccessFlags2{},
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .oldLayout =
          initialized ? vk::ImageLayout::eGeneral : vk::ImageLayout::eUndefined,
      .newLayout = vk::ImageLayout::eGeneral,
      .image = vk::Image(motion),
      .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
  cmd.pipelineBarrier2(
      {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_write});
  camera.RecordMotion(cmd, depth_view, *motion_view, w, h);
  const vk::MemoryBarrier2 to_read{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
      .dstAccessMask = vk::AccessFlagBits2::eMemoryRead};
  cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &to_read});
  initialized = true;
  const char *trigger = std::getenv("THEORDER_NR_DUMP_TRIGGER");
  const bool dump = trigger && std::remove(trigger) == 0;
  if (dump) {
    Dump(instance, scheduler, cmd, color.backing->image,
         vk::ImageAspectFlagBits::eColor, w, h, dump_index, "before_rgba8");
    Dump(instance, scheduler, cmd, depth.backing->image,
         vk::ImageAspectFlagBits::eDepth, w, h, dump_index, "depth_f32");
    Dump(instance, scheduler, cmd, motion, vk::ImageAspectFlagBits::eColor, w,
         h, dump_index, "motion_rg16f");
  }
  bool evaluated = false;
  if (nr_available) {
    const Dlss::Resource c{color_image,
                           color_view,
                           color.info.pixel_format,
                           vk::ImageAspectFlagBits::eColor,
                           w,
                           h};
    Dlss::NeuralOptions options;
    options.depth_inverted = camera.DepthInverted();
    options.color_is_bgra =
        true; // matches DOAX3's VideoOut BGRA display interpretation
    options.intensity = settings.nr_strength / 100.f;
    options.style = settings.nr_style;
    options.preset = settings.nr_preset;
    options.passes = settings.nr_passes;
    evaluated = dlss->EvaluateNeural(
        cmd,
        {.color = c,
         .depth = {depth_image, depth_view, depth.info.pixel_format,
                   vk::ImageAspectFlagBits::eDepth, w, h},
         .motion = {motion, *motion_view, vk::Format::eR16G16Sfloat,
                    vk::ImageAspectFlagBits::eColor, w, h},
         .output = c,
         .jitter_x = upscaler.Jitter()[0],
         .jitter_y = upscaler.Jitter()[1],
         .reset = frames == 0 || camera.HistoryReset(),
         .frame_ms = 1000.f / 30.f,
         .sharpness = 0},
        options);
    PortSettings::Get().neural_problem = evaluated ? "Active" : "NR evaluation failed; scene preserved";
  }
  vk::Image adopted{};
  if (dlss_ready) {
    const auto allocate_color = [&](VideoCore::UniqueImage& image, vk::UniqueImageView& view, u32 iw, u32 ih) {
      if (view) return;
      image.Create({.imageType = vk::ImageType::e2D, .format = vk::Format::eB8G8R8A8Unorm,
        .extent = {iw, ih, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1, .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                 vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst});
      view = Check(device.createImageViewUnique({.image = vk::Image(image),
        .viewType = vk::ImageViewType::e2D, .format = vk::Format::eB8G8R8A8Unorm,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}));
    };
    const bool fresh = !dlss_input_view;
    allocate_color(dlss_input, dlss_input_view, w, h);
    allocate_color(dlss_output, dlss_output_view, ow, oh);
    std::array<vk::ImageMemoryBarrier2, 2> barriers;
    for (u32 i = 0; i < 2; ++i) barriers[i] = {
      .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
      .srcAccessMask = fresh ? vk::AccessFlags2{} : vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
      .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
      .oldLayout = fresh ? vk::ImageLayout::eUndefined : vk::ImageLayout::eGeneral,
      .newLayout = vk::ImageLayout::eGeneral,
      .image = i == 0 ? vk::Image(dlss_input) : vk::Image(dlss_output),
      .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = barriers.data()});
    const vk::MemoryBarrier2 copy_ready{.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
      .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite, .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
      .dstAccessMask = vk::AccessFlagBits2::eTransferRead};
    cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &copy_ready});
    cmd.copyImage(color_image, vk::ImageLayout::eGeneral, dlss_input, vk::ImageLayout::eGeneral,
      vk::ImageCopy{.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, .extent = {w, h, 1}});
    const vk::MemoryBarrier2 evaluate_ready{.srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
      .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands, .dstAccessMask = vk::AccessFlagBits2::eMemoryRead};
    cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &evaluate_ready});
    const bool ok = dlss->Evaluate(cmd, {
      .color = {dlss_input, *dlss_input_view, vk::Format::eB8G8R8A8Unorm, vk::ImageAspectFlagBits::eColor, w, h},
      .depth = {depth_image, depth_view, depth.info.pixel_format, vk::ImageAspectFlagBits::eDepth, w, h},
      .motion = {motion, *motion_view, vk::Format::eR16G16Sfloat, vk::ImageAspectFlagBits::eColor, w, h},
      .output = {dlss_output, *dlss_output_view, vk::Format::eB8G8R8A8Unorm, vk::ImageAspectFlagBits::eColor, ow, oh},
      .jitter_x = upscaler.Jitter()[0], .jitter_y = upscaler.Jitter()[1],
      .reset = frames == 0 || camera.HistoryReset(), .frame_ms = 1000.f / 30.f, .sharpness = 0});
    if (ok) adopted = dlss_output;
  } else if (w == ow && h == oh && (ow != 1920 || oh != 1080)) {
    adopted = color_image;
  }
  if (dump) {
    Dump(instance, scheduler, cmd, color.backing->image,
         vk::ImageAspectFlagBits::eColor, w, h, dump_index, "after_rgba8");
    ++dump_index;
  }
  if (++frames == 1 || frames % 300 == 0)
    std::printf(
        "DOAX3 NR: %llu guided native frames, %llu neural evaluations, %s\n",
        static_cast<unsigned long long>(frames),
        static_cast<unsigned long long>(dlss ? dlss->NeuralFrames() : 0),
        guides_only ? "guide validation only"
        : evaluated ? "evaluated"
                    : "native fallback");
  if (adopted) upscaler.AdoptDoax3Scene(color_id, camera.Depth(), adopted, ow, oh);
  if (!proxy) scheduler.DeferPriorityOperation([device, depth_view, color_view] {
    device.destroyImageView(depth_view);
    device.destroyImageView(color_view);
  });
}
} // namespace Vulkan
