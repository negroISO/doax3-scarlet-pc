// Local Vulkan adapter for the user-supplied DLSS Neural Rendering runtime.
#pragma once
#include "port_dlss_bridge.h"
#include <nvsdk_ngx_params.h>

using PortNrAllocate = NVSDK_NGX_Result (*)(NVSDK_NGX_Parameter **);
using PortNrDestroy = NVSDK_NGX_Result (*)(NVSDK_NGX_Parameter *);
struct PortNrApi {
  int (*Initialize)(VkInstance, VkPhysicalDevice, VkDevice, const wchar_t *,
                    const wchar_t *, PortNrAllocate, PortNrDestroy,
                    PortDlssLogFn);
  int (*Evaluate)(VkCommandBuffer, const PortDlssEvaluate *,
                  const PortNrOptions *);
  // Caller waits for all previously submitted work before release.
  void (*Release)();
  void (*Shutdown)();
  const char *(*Problem)();
};
using PortNrGetApiFn = const PortNrApi *(*)();
