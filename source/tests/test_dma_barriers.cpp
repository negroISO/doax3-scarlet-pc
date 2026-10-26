// SPDX-License-Identifier: GPL-2.0-or-later
// Headless Vulkan regression: real sparse buffer cache, simulated guest memory.
#include <cstring>
#include <cstdlib>
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/page_manager.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"
#include "video_core/texture_cache/blit_helper.h"
#undef NDEBUG
#include <cassert>
int main() {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
#endif
    using namespace Vulkan;
    using namespace VideoCore;
    runtime_disabled_optimizations = PortToggle::DeferredUploads;
    Instance instance(s32{-1}, false);
    Scheduler scheduler(instance);
    Runtime runtime(instance, scheduler);
    PageManager page_manager(nullptr);
    alignas(TextureCache) std::byte texture_storage[sizeof(TextureCache)];
    auto* textures = reinterpret_cast<TextureCache*>(texture_storage);
    // The caches hold references to each other, as they do in Rasterizer's construction order.
    BufferCache buffers(instance, scheduler, runtime, nullptr, *textures, page_manager);
    std::construct_at(textures, instance, scheduler, runtime, nullptr, buffers, page_manager);
    scheduler.SetSubmitCallback([&](SubmitInfo& info) {
        runtime.FlushBarriers();
        buffers.SubmitPendingArenaBinds(info);
    });
    constexpr VAddr address = 0x1000000;
    constexpr u32 size = 64_KB;
    page_manager.OnGpuMap(address, size);
    const auto [arena, offset] = buffers.ObtainBuffer(address, size, ObtainBufferFlags::IgnoreStreamBuffer);
    runtime.FlushBarriers();
    assert(!buffers.SynchronizeDmaBuffers()); // unchanged resident data needs no new barrier
    buffers.InvalidateMemory(address, size);
    assert(buffers.SynchronizeDmaBuffers()); // CPU-dirty uploads must precede BDA shader reads
    runtime.FlushBarriers();
    buffers.MarkDmaBuffersAccessed(vk::PipelineStageFlagBits2::eComputeShader);
    assert(runtime.IsBufferAccessed(arena, offset, size, true)); // later uploads wait for reads
    assert(!runtime.IsBufferAccessed(arena, offset, size));
    buffers.InvalidateMemory(address, size);
    assert(buffers.SynchronizeDmaBuffers());
    runtime.FlushBarriers();
    buffers.MarkDmaBuffersAccessed(vk::PipelineStageFlagBits2::eAllGraphics);
    assert(runtime.IsBufferAccessed(arena, offset, size, true));
    scheduler.Finish();
    std::destroy_at(textures);
    std::puts("DMA barriers: resident dirty uploads and compute/graphics BDA reads tracked");
}
