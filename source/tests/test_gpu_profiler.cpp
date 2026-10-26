// SPDX-License-Identifier: GPL-2.0-or-later
// Display boundaries can outpace actual GPU submits during loading/cutscenes.
#include <cstdio>
#include <cstdlib>
#include "video_core/renderer_vulkan/vk_gpu_profiler.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

int main() {
#ifdef _WIN32
    _putenv_s("THEORDER_GPU_PROFILE", "1");
#else
    setenv("THEORDER_GPU_PROFILE", "1", 1);
#endif
    Vulkan::Instance instance(0, false);
    Vulkan::Scheduler scheduler(instance);
    Vulkan::GpuProfiler::Init(instance, scheduler);
    auto* profiler = Vulkan::GpuProfiler::Get();
    if (!profiler) return 1;
    // The old profiler blocks on the fourth boundary: none of these timestamps
    // have been submitted, and the blocked thread itself must reach Flush.
    for (unsigned frame = 0; frame < 64; ++frame) {
        profiler->Mark(1, [] { return "unsubmitted frame"; });
        profiler->BeginFrame();
    }
    scheduler.Finish();
    // Recover all query slices, reuse them, and execute further GPU work.
    for (unsigned frame = 0; frame < 16; ++frame) {
        profiler->Mark(2, [] { return "submitted frame"; });
        profiler->BeginFrame();
        scheduler.Finish();
    }
    std::puts("PASS: GPU profiler skips unsubmitted queries and recovers after submission");
}
