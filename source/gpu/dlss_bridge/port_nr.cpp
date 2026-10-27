// Native Vulkan NR adapter. Runtime files are supplied by the local user.
// Uses the NGX string-keyed Feature 18 interface; does not modify the driver or
// runtime.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "port_nr.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <initializer_list>
#include <nvsdk_ngx_helpers_vk.h>
#include <windows.h>

namespace {
using InitFn = uint32_t (*)(unsigned long long, const wchar_t *, VkInstance,
                            VkPhysicalDevice, VkDevice, uint32_t, const void *);
using CreateFn = uint32_t (*)(VkCommandBuffer, int, const NVSDK_NGX_Parameter *,
                              NVSDK_NGX_Handle **);
using EvalFn = uint32_t (*)(VkCommandBuffer, const NVSDK_NGX_Handle *,
                            const NVSDK_NGX_Parameter *, void *);
using ReleaseFn = uint32_t (*)(NVSDK_NGX_Handle *);
using StopFn = uint32_t (*)(VkDevice);
VkDevice device{};
VkPhysicalDevice physical{};
HMODULE module{};
InitFn init{};
CreateFn create{};
EvalFn evaluate{};
ReleaseFn release{};
StopFn stop{};
PortNrAllocate allocate{};
PortNrDestroy destroy{};
PortDlssLogFn logfn{};
NVSDK_NGX_Parameter *parameters{};
NVSDK_NGX_Handle *feature{};
struct Image {
  VkImage image{};
  VkImageView view{};
  VkDeviceMemory memory{};
};
struct PassState {
  NVSDK_NGX_Parameter* parameters{};
  NVSDK_NGX_Handle* feature{};
  Image input, output;
  bool images_general{};
};
std::array<PassState, 3> passes;
bool initialized{}, images_general{}, failed{};
uint32_t width{}, height{};
VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
uint64_t frames{};
const char *problem = "Neural runtime not initialized";
Image input, output;
#define VK_FUNCTIONS(X)                                                        \
  X(vkGetPhysicalDeviceMemoryProperties)                                       \
  X(vkCreateImage) X(vkGetImageMemoryRequirements) X(vkAllocateMemory)         \
      X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView)          \
          X(vkDestroyImage) X(vkFreeMemory) X(vkCmdPipelineBarrier)            \
              X(vkCmdBlitImage) X(vkCmdCopyImage)
#define DECLARE(name) PFN_##name name{};
VK_FUNCTIONS(DECLARE)
#undef DECLARE
void Log(const char *text, uint32_t code = 1) {
  char buffer[384];
  std::snprintf(buffer, sizeof(buffer), "NR: %s (0x%08x)", text, code);
  if (logfn)
    logfn(code != 1, buffer);
}
bool Fail(const char *text, uint32_t code) {
  problem = text;
  failed = true;
  Log(text, code);
  return false;
}
void Drop(Image &i) {
  if (i.view)
    vkDestroyImageView(device, i.view, nullptr);
  if (i.image)
    vkDestroyImage(device, i.image, nullptr);
  if (i.memory)
    vkFreeMemory(device, i.memory, nullptr);
  i = {};
}
bool Make(Image &i) {
  VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ci.imageType = VK_IMAGE_TYPE_2D;
  ci.format = color_format;
  ci.extent = {width, height, 1};
  ci.mipLevels = ci.arrayLayers = 1;
  ci.samples = VK_SAMPLE_COUNT_1_BIT;
  ci.tiling = VK_IMAGE_TILING_OPTIMAL;
  ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
  if (vkCreateImage(device, &ci, nullptr, &i.image) != VK_SUCCESS)
    return false;
  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(device, i.image, &requirements);
  VkPhysicalDeviceMemoryProperties memory{};
  vkGetPhysicalDeviceMemoryProperties(physical, &memory);
  uint32_t type = 0;
  while (type < memory.memoryTypeCount &&
         (!(requirements.memoryTypeBits & (1u << type)) ||
          !(memory.memoryTypes[type].propertyFlags &
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))
    ++type;
  if (type == memory.memoryTypeCount)
    return false;
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = requirements.size;
  ai.memoryTypeIndex = type;
  if (vkAllocateMemory(device, &ai, nullptr, &i.memory) != VK_SUCCESS ||
      vkBindImageMemory(device, i.image, i.memory, 0) != VK_SUCCESS)
    return false;
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = i.image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = ci.format;
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  return vkCreateImageView(device, &vi, nullptr, &i.view) == VK_SUCCESS;
}
void Barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout old,
             VkImageLayout next) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.srcAccessMask =
      old == VK_IMAGE_LAYOUT_UNDEFINED
          ? 0
          : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  b.oldLayout = old;
  b.newLayout = next;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &b);
}
void Blit(VkCommandBuffer cmd, VkImage source, VkImage dest) {
  if (color_format == VK_FORMAT_R8G8B8A8_UNORM ||
      color_format == VK_FORMAT_B8G8R8A8_UNORM) {
    // SDR is display encoded. A copy preserves sRGB bytes; a blit decodes them.
    VkImageCopy region{};
    region.srcSubresource =
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {width, height, 1};
    vkCmdCopyImage(cmd, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    return;
  }
  VkImageBlit region{};
  region.srcSubresource =
      region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.srcOffsets[1] =
      region.dstOffsets[1] = {int32_t(width), int32_t(height), 1};
  vkCmdBlitImage(cmd, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                 VK_FILTER_NEAREST);
}
void Release() {
  for (auto& pass : passes) {
    if (pass.feature && release) {
      __try {
        release(pass.feature);
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("release raised exception", GetExceptionCode());
      }
    }
    if (pass.parameters && destroy) destroy(pass.parameters);
    Drop(pass.input);
    Drop(pass.output);
    pass = {};
  }
  feature = nullptr;
  parameters = nullptr;
  input = output = {};
  width = height = 0;
  images_general = false;
  failed = false;
  problem = initialized ? nullptr : "Neural runtime not initialized";
}
void Shutdown() {
  Release();
  if (initialized && stop) {
    __try {
      stop(device);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      Log("shutdown raised exception", GetExceptionCode());
    }
  }
  initialized = false;
}
int Initialize(VkInstance instance, VkPhysicalDevice pd, VkDevice dev,
               const wchar_t *runtime, const wchar_t *data,
               PortNrAllocate alloc, PortNrDestroy free_params,
               PortDlssLogFn logger) {
  device = dev;
  physical = pd;
  allocate = alloc;
  destroy = free_params;
  logfn = logger;
  HMODULE vk = GetModuleHandleW(L"vulkan-1.dll");
#define LOAD(name)                                                             \
  name = reinterpret_cast<PFN_##name>(GetProcAddress(vk, #name));              \
  if (!name)                                                                   \
    return Fail("Vulkan loader entry missing", 0);
  VK_FUNCTIONS(LOAD)
#undef LOAD
  module = LoadLibraryExW(runtime, nullptr,
                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                              LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (!module)
    return Fail("NR runtime could not load", GetLastError());
  init = reinterpret_cast<InitFn>(
      GetProcAddress(module, "NVSDK_NGX_VULKAN_Init_Ext"));
  create = reinterpret_cast<CreateFn>(
      GetProcAddress(module, "NVSDK_NGX_VULKAN_CreateFeature"));
  evaluate = reinterpret_cast<EvalFn>(
      GetProcAddress(module, "NVSDK_NGX_VULKAN_EvaluateFeature"));
  release = reinterpret_cast<ReleaseFn>(
      GetProcAddress(module, "NVSDK_NGX_VULKAN_ReleaseFeature"));
  stop = reinterpret_cast<StopFn>(
      GetProcAddress(module, "NVSDK_NGX_VULKAN_Shutdown1"));
  if (!init || !create || !evaluate || !release || !stop)
    return Fail("NR Vulkan exports missing", 0);
  uint32_t result{};
  __try {
    result = init(0, data, instance, pd, dev, NVSDK_NGX_Version_API, nullptr);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    result = GetExceptionCode();
  }
  if (result != 1)
    return Fail("NR Vulkan initialization failed", result);
  initialized = true;
  problem = nullptr;
  Log("Vulkan runtime ready; awaiting scene inputs");
  return 1;
}
uint32_t Create(VkCommandBuffer cmd, const PortNrOptions &options) {
  if (allocate(&parameters) != NVSDK_NGX_Result_Success)
    return 0xBAD00000;
  uint32_t result{};
  __try {
    for (const char *k : {"Width", "DLSSNR.Width", "DLSSNR.InputWidth",
                          "DLSSNR.OutputWidth", "DLSSNR.Output.Width"})
      parameters->Set(k, width);
    for (const char *k : {"Height", "DLSSNR.Height", "DLSSNR.InputHeight",
                          "DLSSNR.OutputHeight", "DLSSNR.Output.Height"})
      parameters->Set(k, height);
    parameters->Set("DLSSNR.Upscaling", 0u);
    parameters->Set("DLSSNR.Scale", 1.f);
    parameters->Set("DLSSNR.ScalingRatio", 1.f);
    parameters->Set("Feature_Flags", 64u | (options.hdr ? 1u : 0u));
    parameters->Set("DLSSNR.Hdr", options.hdr ? 1u : 0u);
    parameters->Set("DLSSNR.SDR", options.hdr ? 0u : 1u);
    parameters->Set("DLSSNR.AutoExposure", 1u);
    parameters->Set("InPreExposure", 1.f);
    parameters->Set("InExposureScale", 1.f);
    parameters->Set("CreationNodeMask", 1u);
    parameters->Set("VisibilityNodeMask", 1u);
    parameters->Set("DLSSNR.Hint.Render.Preset", unsigned(std::clamp(options.preset, 0, 3)));
    parameters->Set("DLSSNR.Style", unsigned(options.style));
    parameters->Set("DLSSNR.UseAutoMask", 1u);
    parameters->Set("DLSSNR.Intensity", options.intensity);
    parameters->Set("DLSSNR.LocalToneStrength", options.tone);
    parameters->Set("DLSSNR.LocalStructureStrength", options.structure);
    parameters->Set("DLSSNR.SkinStructureStrength", options.skin);
    result = create(cmd, 18, parameters, &feature);
    if (result != 1 && options.preset != 0) {
      Log("requested NR model failed; retrying runtime default", result);
      if (feature) release(feature);
      feature = nullptr;
      parameters->Set("DLSSNR.Hint.Render.Preset", 0u);
      result = create(cmd, 18, parameters, &feature);
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    result = GetExceptionCode();
  }
  return result;
}
int EvaluatePass(VkCommandBuffer cmd, const PortDlssEvaluate *frame,
             const PortNrOptions *options) {
  if (!initialized || failed || !frame || !options)
    return 0;
  // A successful NGX return does not validate the game's HDR color contract.
  // The captured HDR trial flattened the scene; preserve the known-good DLAA
  // image.
  if (options->hdr)
    return Fail("NR HDR color mapping not validated; DLAA preserved", 0);
  if (frame->output.width != frame->motion.width ||
      frame->output.height != frame->motion.height)
    return Fail("NR requires native-size motion guides", 0);
  if (feature &&
      (width != frame->output.width || height != frame->output.height))
    return Fail("NR input size changed; restart required", 0);
  if (!feature) {
    width = frame->output.width;
    height = frame->output.height;
    color_format = options->hdr             ? VK_FORMAT_R16G16B16A16_SFLOAT
                   : options->color_is_bgra ? VK_FORMAT_B8G8R8A8_UNORM
                                            : VK_FORMAT_R8G8B8A8_UNORM;
    if (width < 320 || height < 192)
      return Fail("NR scene below minimum size", 0);
    if ((!input.image && !Make(input)) || (!output.image && !Make(output)))
      return Fail("NR image allocation failed", 0);
    const auto result = Create(cmd, *options);
    if (result != 1 || !feature)
      return Fail("NR feature creation failed; DLAA preserved", result);
    Log("Feature 18 created; actual frame evaluation pending");
  }
  Barrier(cmd, frame->output.image, VK_IMAGE_LAYOUT_GENERAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  Barrier(cmd, input.image,
          images_general ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  Barrier(cmd, output.image,
          images_general ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_GENERAL);
  Blit(cmd, frame->output.image, input.image);
  Barrier(cmd, input.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_GENERAL);
  Barrier(cmd, frame->output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_IMAGE_LAYOUT_GENERAL);
  images_general = true;
  const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  auto color = NVSDK_NGX_Create_ImageView_Resource_VK(
      input.view, input.image, range, color_format, width, height, false);
  auto out = NVSDK_NGX_Create_ImageView_Resource_VK(
      output.view, output.image, range, color_format, width, height, true);
  auto depth = NVSDK_NGX_Create_ImageView_Resource_VK(
      frame->depth.view, frame->depth.image, frame->depth.range,
      frame->depth.format, frame->depth.width, frame->depth.height, false);
  auto motion = NVSDK_NGX_Create_ImageView_Resource_VK(
      frame->motion.view, frame->motion.image, frame->motion.range,
      frame->motion.format, frame->motion.width, frame->motion.height, false);
  uint32_t result{};
  __try {
    parameters->Set("DLSSNR.Style", unsigned(options->style));
    parameters->Set("DLSSNR.Intensity", options->intensity);
    parameters->Set("DLSSNR.LocalToneStrength", options->tone);
    parameters->Set("DLSSNR.LocalStructureStrength", options->structure);
    parameters->Set("DLSSNR.SkinStructureStrength", options->skin);
    parameters->Set("DLSSNR.Color", &color);
    parameters->Set("DLSSNR.Output", &out);
    parameters->Set("DLSSNR.Depth", &depth);
    parameters->Set("DLSSNR.MVec", &motion);
    parameters->Set("Color", &color);
    parameters->Set("Output", &out);
    parameters->Set("Depth", &depth);
    parameters->Set("MotionVectors", &motion);
    for (const char *k :
         {"DLSSNR.ControlMask", "DLSSNR.UI", "DLSSNR.UIAlpha",
          "DLSSNR.Backbuffer", "DLSSNR.BidirectionalDistortionField"})
      parameters->Set(k, static_cast<void *>(nullptr));
    for (const char *k :
         {"DLSSNR.ColorSubrectWidth", "DLSSNR.OutputSubrectWidth",
          "DLSSNR.MVecSubrectWidth", "DLSSNR.DepthSubrectWidth"})
      parameters->Set(k, width);
    for (const char *k :
         {"DLSSNR.ColorSubrectHeight", "DLSSNR.OutputSubrectHeight",
          "DLSSNR.MVecSubrectHeight", "DLSSNR.DepthSubrectHeight"})
      parameters->Set(k, height);
    for (const char *k :
         {"DLSSNR.ColorSubrectBaseX", "DLSSNR.ColorSubrectBaseY",
          "DLSSNR.OutputSubrectBaseX", "DLSSNR.OutputSubrectBaseY",
          "DLSSNR.MVecSubrectBaseX", "DLSSNR.MVecSubrectBaseY",
          "DLSSNR.DepthSubrectBaseX", "DLSSNR.DepthSubrectBaseY"})
      parameters->Set(k, 0u);
    parameters->Set("DLSSNR.MVecScaleX", 1.f);
    parameters->Set("DLSSNR.MVecScaleY", 1.f);
    parameters->Set("DLSSNR.DepthInverted", options->depth_inverted ? 1u : 0u);
    parameters->Set("DLSSNR.Reset", frame->reset ? 1u : 0u);
    parameters->Set("Reset", frame->reset ? 1u : 0u);
    parameters->Set("Sharpness", 0.f);
    result = evaluate(cmd, feature, parameters, nullptr);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    result = GetExceptionCode();
  }
  if (result != 1)
    return Fail("NR evaluation failed; DLAA preserved", result);
  static int last_style = -1;
  static float last_intensity = -1.f;
  if (last_style != options->style || last_intensity != options->intensity) {
    char message[160];
    std::snprintf(message, sizeof(message), "SDR style=%d intensity=%.3f",
                  options->style, options->intensity);
    Log(message);
    last_style = options->style;
    last_intensity = options->intensity;
  }
  Barrier(cmd, output.image, VK_IMAGE_LAYOUT_GENERAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  Barrier(cmd, frame->output.image, VK_IMAGE_LAYOUT_GENERAL,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  Blit(cmd, output.image, frame->output.image);
  Barrier(cmd, output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_IMAGE_LAYOUT_GENERAL);
  Barrier(cmd, frame->output.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_GENERAL);
  if (++frames == 1 || frames % 300 == 0) {
    Log("Neural frame evaluated successfully");
  }
  problem = nullptr;
  return 1;
}
const char *Problem() { return problem; }
int Evaluate(VkCommandBuffer cmd, const PortDlssEvaluate* frame,
             const PortNrOptions* options) {
  if (!options || !frame) return 0;
  const int count = std::clamp(options->passes, 1, int(passes.size()));
  // Each evaluation consumes the previous pass's result. Distinct feature
  // handles keep each pass's history on the game's frame clock.
  for (int i = 0; i < count; ++i) {
    parameters = passes[i].parameters;
    feature = passes[i].feature;
    input = passes[i].input;
    output = passes[i].output;
    images_general = passes[i].images_general;
    const int result = EvaluatePass(cmd, frame, options);
    passes[i] = {parameters, feature, input, output, images_general};
    if (!result) return 0;
  }
  return 1;
}
const PortNrApi api{Initialize, Evaluate, Release, Shutdown, Problem};
} // namespace
extern "C" __declspec(dllexport) const PortNrApi *PortNrGetApiV3() {
  return &api;
}
