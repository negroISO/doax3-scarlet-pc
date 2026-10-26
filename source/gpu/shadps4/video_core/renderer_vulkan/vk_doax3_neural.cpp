// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_doax3_neural.h"
#include "video_core/renderer_vulkan/vk_camera_motion.h"
#include "video_core/renderer_vulkan/vk_dlss.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_gpu_profiler.h"
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
                         VideoCore::TextureCache &t, CameraMotion &c)
    : instance{i}, scheduler{s}, runtime{r}, textures{t}, camera{c},
      motion{i.GetDevice(), i.GetAllocator()} {
  const char *setting = std::getenv("THEORDER_NEURAL");
  enabled = camera.UsesDoax3Constants() && setting && setting[0] == '1';
  const char *guides = std::getenv("THEORDER_NR_GUIDES_ONLY");
  guides_only = guides && guides[0] == '1';
}
Doax3Neural::~Doax3Neural() {
  if (initialized)
    scheduler.Finish();
}

void Doax3Neural::Record(VideoCore::ImageId color_id) {
  if (!enabled || !camera.Ready() || !color_id)
    return;
  auto &color = textures.GetImage(color_id);
  auto &depth = textures.GetImage(camera.Depth());
  const u32 w = color.info.size.width, h = color.info.size.height;
  if (w != 1920 || h != 1080 || depth.info.size.width != w ||
      depth.info.size.height != h ||
      color.info.pixel_format != vk::Format::eR8G8B8A8Srgb ||
      depth.info.pixel_format != vk::Format::eD32SfloatS8Uint)
    return;
  Dlss *dlss = guides_only ? nullptr : Dlss::Get();
  if (!guides_only && (!dlss || !dlss->NeuralAvailable())) {
    if (!noted) {
      std::printf("DOAX3 NR: native frame preserved (%s)\n",
                  dlss ? dlss->NeuralProblem().c_str()
                       : "bridge/runtime missing");
      noted = true;
    }
    return;
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
  const auto depth_view = Check(device.createImageView(
      {.image = vk::Image(depth.backing->image),
       .viewType = vk::ImageViewType::e2D,
       .format = depth.info.pixel_format,
       .subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}}));
  const auto color_view = Check(device.createImageView(
      {.image = vk::Image(color.backing->image),
       .viewType = vk::ImageViewType::e2D,
       .format = color.info.pixel_format,
       .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}));
  scheduler.EndRendering();
  runtime.Transit(&depth, vk::ImageLayout::eGeneral,
                  vk::PipelineStageFlagBits2::eAllCommands,
                  vk::AccessFlagBits2::eMemoryRead);
  runtime.Transit(&color, vk::ImageLayout::eGeneral,
                  vk::PipelineStageFlagBits2::eAllCommands,
                  vk::AccessFlagBits2::eMemoryRead |
                      vk::AccessFlagBits2::eMemoryWrite);
  runtime.FlushBarriers();
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
  if (dlss) {
    const Dlss::Resource c{color.backing->image,
                           color_view,
                           color.info.pixel_format,
                           vk::ImageAspectFlagBits::eColor,
                           w,
                           h};
    Dlss::NeuralOptions options;
    options.depth_inverted = camera.DepthInverted();
    options.color_is_bgra =
        true; // matches DOAX3's VideoOut BGRA display interpretation
    if (const char *strength = std::getenv("THEORDER_NR_STRENGTH")) {
      const float value = std::strtof(strength, nullptr);
      if (std::isfinite(value))
        options.intensity = std::clamp(value, 0.f, 1.f);
    }
    evaluated = dlss->EvaluateNeural(
        cmd,
        {.color = c,
         .depth = {depth.backing->image, depth_view, depth.info.pixel_format,
                   vk::ImageAspectFlagBits::eDepth, w, h},
         .motion = {motion, *motion_view, vk::Format::eR16G16Sfloat,
                    vk::ImageAspectFlagBits::eColor, w, h},
         .output = c,
         .jitter_x = 0,
         .jitter_y = 0,
         .reset = frames == 0 || camera.HistoryReset(),
         .frame_ms = 1000.f / 30.f,
         .sharpness = 0},
        options);
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
  scheduler.DeferPriorityOperation([device, depth_view, color_view] {
    device.destroyImageView(depth_view);
    device.destroyImageView(color_view);
  });
}
} // namespace Vulkan
