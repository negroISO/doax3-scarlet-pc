// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include "video_core/texture_cache/readback_queue.h"
#include "video_core/texture_cache/readback_sources.h"
#include <chrono>
#include "port_toggles.h"
#include <optional>
#include <cstdlib>
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>
#include <unordered_map>
#include <unordered_set>
#include <boost/container/small_vector.hpp>
#include <queue>
#include <tsl/robin_map.h>

#include "common/lru_cache.h"
#include "common/multi_level_page_table.h"
#include "common/slot_vector.h"
#include "shader_recompiler/resource.h"
#include "video_core/texture_cache/blit_helper.h"
#include "video_core/texture_cache/image.h"
#include "video_core/texture_cache/image_view.h"
#include "video_core/texture_cache/sampler.h"
#include "video_core/texture_cache/tile_manager.h"

namespace AmdGpu {
struct Liverpool;
}

namespace Vulkan {
class Runtime;
}

namespace VideoCore {

class BufferCache;
class PageManager;

class TextureCache {
    static constexpr s64 DEFAULT_PRESSURE_GC_MEMORY = 1_GB + 512_MB;
    static constexpr s64 DEFAULT_CRITICAL_GC_MEMORY = 3_GB;
    static constexpr s64 TARGET_GC_THRESHOLD = 8_GB;

    struct BucketEntry {
        u32 key;
        u32 size;
        ImageId id;

        bool Overlaps(VAddr addr, size_t size) const noexcept {
            const VAddr base = Address();
            return base < (addr + size) && addr < (base + this->size);
        }

        VAddr Address() const noexcept {
            return VAddr(key) << 8;
        }
    };

    struct alignas(64) Bucket {
        SmallVector<BucketEntry, 4, u32> entries;
    };
    static_assert(sizeof(Bucket) == 64);

    struct Traits {
        using Entry = Bucket;
        static constexpr size_t ADDRESS_SPACE_BITS = 40;
        static constexpr size_t L1_BITS = 10;
        static constexpr size_t PAGE_BITS = 18;
        static constexpr bool NULL_CHECK = true;
    };
    using PageTable = Common::MultiLevelPageTable<Traits>;

public:
    enum class BindingType : u32 {
        Texture,
        Storage,
        RenderTarget,
        DepthTarget,
        VideoOut,
    };

    enum class DownloadMemoryFlags : u32 {
        None = 0,
        Sync = 1 << 0,
        Priority = 1 << 1,
        DiscardBufferCache = 1 << 2,
    };

    struct ImageDesc {
        ImageInfo info;
        ImageViewInfo view_info;
        BindingType type{BindingType::Texture};
        bool needs_native{}; ///< port: integer-coordinate reads (ResolveOverlap partial-height views)

        ImageDesc() = default;
        ImageDesc(const AmdGpu::Image& image, const Shader::ImageResource& desc)
            : info{image, desc}, view_info{image, desc},
              type{desc.is_written ? BindingType::Storage : BindingType::Texture},
              needs_native{desc.needs_native} {}
        ImageDesc(const AmdGpu::ColorBuffer& buffer, AmdGpu::CbDbExtent hint)
            : info{buffer, hint}, view_info{buffer}, type{BindingType::RenderTarget} {}
        ImageDesc(const AmdGpu::DepthBuffer& buffer, AmdGpu::DepthView view,
                  AmdGpu::DepthControl ctl, VAddr htile_address, AmdGpu::CbDbExtent hint,
                  bool write_buffer = false)
            : info{buffer, view.NumSlices(), htile_address, hint, write_buffer},
              view_info{buffer, view, ctl}, type{BindingType::DepthTarget} {}
        ImageDesc(const Libraries::VideoOut::BufferAttributeGroup& group, VAddr cpu_address)
            : info{group, cpu_address}, type{BindingType::VideoOut} {}
    };
    static_assert(std::is_trivially_destructible_v<ImageDesc>);

public:
    TextureCache(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler,
                 Vulkan::Runtime& runtime, AmdGpu::Liverpool* liverpool, BufferCache& buffer_cache,
                 PageManager& page_manager);
    ~TextureCache();

    /// port: changes whenever an image is registered or unregistered.
    [[nodiscard]] u64 RegistryGeneration() const noexcept {
        return registry_generation.load(std::memory_order_acquire);
    }

    /// port: FindImage's access tick for an image found by a memoized lookup. The LRU touch
    /// FindImage also does happens in UpdateImage (FindTexture), which every binding reaches.
    void MarkFound(ImageId image_id) {
        slot_images[image_id].tick_accessed_last = scheduler.CurrentTick();
    }

    TileManager& GetTileManager() noexcept {
        return tile_manager;
    }

    /// Invalidates any image in the logical page range.
    void InvalidateMemory(VAddr addr, size_t size);

    /// port: a recorded buffer-to-staging copy joins the deferred image readbacks: written to
    /// guest memory once the GPU is done (BufferCache::QueueReadbacks).
    void QueueBufferReadback(VAddr address, u64 size, Vulkan::StagingBufferRef staging,
                             u64 token = 0);
    [[nodiscard]] bool DeferredReadbacks() const noexcept {
        return deferred_readbacks;
    }

    /// port (gow3_pc a101edb): guest memory written through the backing view (readbacks, buffer
    /// downloads) skips page tracking. Any lock may be held: images over the range are marked
    /// CPU-dirty at the next UpdateImage or GC tick (ApplyBackingWrites).
    void NoteBackingWrite(VAddr address, u64 size);

    /// Marks an image as dirty if it exists at the provided address.
    void InvalidateMemoryFromGPU(VAddr address, size_t max_size);

    /// Marks all images as maybe GPU dirty within the provided range.
    void MarkAsMaybeGpuDirty(VAddr addr, size_t size);

    /// Evicts any images that overlap the unmapped range.
    void UnmapMemory(VAddr cpu_addr, size_t size);

    /// Schedules a copy of pending images for download back to CPU memory.
    void ProcessDownloadImages();
    bool ResolveReadbacks(VAddr address, u64 size, bool assume_locks = true,
                          ReadbackSource source = ReadbackSource::Unknown);
    void CancelReadbacks(VAddr address, u64 size);

    /// Retrieves the image handle of the image with the provided attributes.
    [[nodiscard]] ImageId FindImage(ImageDesc& desc, bool exact_fmt = false);

    /// Retrieves image whose address matches provided
    [[nodiscard]] ImageId FindImageFromRange(VAddr address, size_t size, bool ensure_valid = true);

    /// port: a FindView result remembered by the caller for the same image and view info.
    struct ViewMemo {
        ImageId image_id{};
        const void* backing = nullptr;
        ImageViewId view_id{};
    };

    /// Retrieves an image view with the properties of the specified image id.
    /// `refresh` false: an image found out of date is used as is (the texture binding helper
    /// checked it beforehand; only a guest write racing with the draw can make it so).
    [[nodiscard]] ImageView& FindTexture(ImageId image_id, const ImageDesc& desc,
                                         ViewMemo* memo = nullptr, bool refresh = true);

    /// Retrieves the render target with specified properties
    [[nodiscard]] ImageView& FindRenderTarget(ImageId image_id, const ImageDesc& desc);

    /// Retrieves the depth target with specified properties
    [[nodiscard]] ImageView& FindDepthTarget(ImageId image_id, const ImageDesc& desc);

    /// port: whether UpdateImage has nothing to do for this image (its fast path).
    [[nodiscard]] bool IsUpToDate(ImageId image_id) const {
        const Image& image = slot_images[image_id];
        const u32 flags = std::atomic_ref<u32>(const_cast<u32&>(reinterpret_cast<const u32&>(image.flags)))
                              .load(std::memory_order_acquire);
        constexpr u32 Dirty = static_cast<u32>(ImageFlagBits::Dirty);
        constexpr u32 Registered = static_cast<u32>(ImageFlagBits::Registered);
        constexpr u32 Aliased = static_cast<u32>(ImageFlagBits::Aliased);
        return (flags & (Dirty | Registered | Aliased)) == Registered &&
               !has_backing_writes.load(std::memory_order_acquire) &&
               image.track_addr == image.guest_begin && image.track_addr_end == image.guest_end &&
               image.lru_touched_tick == gc_tick;
    }
    /// Links the stencil plane of a depth surface to its depth image.
    void AssociateStencil(ImageId depth_id, const ImageInfo& depth_info);

    /// Updates image contents if it was modified by CPU.
    void UpdateImage(ImageId image_id) {
        // port: a clean image already tracked and touched in this GC period needs nothing.
        // Every texture binding comes here; the mutex (shared with the fault handlers of the
        // guest threads) was ~3% of the GPU thread. Flags are read atomically: an invalidation
        // racing with this check races the same way with the locked path.
        if (!PortToggle::Disabled(PortToggle::UpdateImageFastPath)) {
            const Image& image = slot_images[image_id];
            const u32 flags = std::atomic_ref<u32>(const_cast<u32&>(reinterpret_cast<const u32&>(image.flags)))
                                  .load(std::memory_order_acquire);
            constexpr u32 Dirty = static_cast<u32>(ImageFlagBits::Dirty);
            constexpr u32 Registered = static_cast<u32>(ImageFlagBits::Registered);
            // Images sharing memory with another take the locked path: SynchronizeAlias.
            constexpr u32 Aliased = static_cast<u32>(ImageFlagBits::Aliased);
            if ((flags & (Dirty | Registered | Aliased)) == Registered &&
                !has_backing_writes.load(std::memory_order_acquire) &&
                image.track_addr == image.guest_begin && image.track_addr_end == image.guest_end &&
                image.lru_touched_tick == gc_tick) {
                return;
            }
        }
        std::scoped_lock lock{mutex};
        ApplyBackingWrites();
        Image& image = slot_images[image_id];
        TouchImage(image);
        TrackImage(image_id);
        RefreshImage(image);
        SynchronizeAlias(image_id);
    }

    /// port: a GPU write to the image (render target, storage): images sharing its memory
    /// copy from it before their next read. From cuesta4/shadPS4 (eltutz), which fixed God of
    /// War III's exposure: it is measured into a small image and read through an alias.
    void PublishAliasWrite(ImageId image_id);

    /// Resolves overlap between existing cache image and pending merged image
    [[nodiscard]] std::tuple<ImageId, int, int> ResolveOverlap(const ImageInfo& info,
                                                               BindingType binding,
                                                               ImageId cache_img_id,
                                                               ImageId merged_image_id,
                                                               bool native_reads = false);

    /// Resolves depth overlap and either re-creates the image or returns existing one
    [[nodiscard]] ImageId ResolveDepthOverlap(const ImageInfo& requested_info, BindingType binding,
                                              ImageId cache_img_id);

    /// Creates a new image with provided image info and copies subresources from image_id
    [[nodiscard]] ImageId ExpandImage(const ImageInfo& info, ImageId image_id);

    /// Reuploads image contents.
    void RefreshImage(Image& image);

    /// Retrieves the sampler that matches the provided S# descriptor.
    /// extra_lod_bias: port, added to the S#'s bias (reduced scene rendering).
    [[nodiscard]] vk::Sampler GetSampler(const AmdGpu::Sampler& sampler,
                                         AmdGpu::BorderColorBuffer border_color_base,
                                         bool is_depth, float extra_lod_bias = 0.0f);

    /// Retrieves the image with the specified id.
    Image* TryGetImage(ImageId id, u64 uid) {
        if (!slot_images.IsAllocated(id)) return nullptr;
        auto& image = slot_images[id];
        return !uid || image.image_uid == uid ? &image : nullptr;
    }
    /// port diagnostics: writes level 0 of a color image to THEORDER_DIAG_DIR as raw pixels
    /// (<seq>_<tag>_<address>_<w>x<h>_p<pitch>_b<bytes per pixel>_f<vk format>.raw). Waits for
    /// the GPU.
    void DiagDumpImage(ImageId image_id, const char* tag);

    [[nodiscard]] Image& GetImage(ImageId id) {
        auto& image = slot_images[id];
        TouchImage(image);
        return image;
    }

    /// Retrieves the image view with the specified id.
    [[nodiscard]] ImageView& GetImageView(ImageId id) {
        return slot_image_views[id];
    }

    /// Get the associated depth stencil image if it is still valid.
    ImageId GetAssociatedDepth(Image& image) {
        if (!image.depth_id) {
            return {};
        }
        if (slot_images.IsAllocated(image.depth_id)) {
            auto& depth_image = slot_images[image.depth_id];
            if (depth_image.image_uid == image.depth_uid &&
                depth_image.flags & ImageFlagBits::Registered) {
                return image.depth_id;
            }
        }
        // The linked depth image is no longer valid, disassociate it.
        image.DisassociateDepth();
        return {};
    }

    enum class MetaType {
        CMask,
        FMask,
        HTile,
    };

    struct MetaDataInfo {
        MetaType type;
        ImageId owner_id{};
        u64 owner_uid{};
        u32 num_samples = 1;
        s32 clear_mask = -1;
        bool htile_stencil{};  ///< port: HTILE words carry stencil state (HtileReadValue)
        bool shader_written{}; ///< port: NoteMetaShaderWrite since the last clear
    };

    /// Returns metadata information if the specified address belongs to a live image.
    std::optional<MetaDataInfo> GetMetaInfo(VAddr address) const {
        const auto it = surface_metas.find(address);
        if (it == surface_metas.end()) {
            return std::nullopt;
        }
        const auto& meta = it->second;
        if (!meta.owner_id || !slot_images.IsAllocated(meta.owner_id)) {
            return std::nullopt;
        }
        const auto& owner = slot_images[meta.owner_id];
        if (owner.image_uid != meta.owner_uid || False(owner.flags & ImageFlagBits::Registered)) {
            return std::nullopt;
        }
        return meta;
    }

    /// Returns meta type if the specified address is a metadata surface.
    std::optional<MetaType> IsMeta(VAddr address) const {
        auto it = surface_metas.find(address);
        if (it != surface_metas.end()) {
            return it->second.type;
        }
        return std::nullopt;
    }

    /// port: the HTILE word a shader reading the HTILE surface at `address` sees, or nothing.
    /// Depth is never compressed here, so tiles read as expanded (ZMask 0xF: read the depth
    /// image) once the slice was drawn after its clear, and as cleared (ZMask 0) before that.
    /// Min/max depth stay the full range (radv's initial values). The Order's shadow map
    /// conversion (cs 0x8ea0ef5b) takes ZMask 0 tiles as empty: no shadows without this.
    std::optional<u32> HtileReadValue(VAddr address) const {
        const auto it = surface_metas.find(address);
        if (it == surface_metas.end() || it->second.type != MetaType::HTile ||
            it->second.shader_written) {
            return std::nullopt;
        }
        const bool cleared = it->second.clear_mask & 1;
        const u32 expanded = it->second.htile_stencil ? 0xfffff3ffu : 0xfffc000fu;
        return cleared ? expanded & ~0xfu : expanded;
    }

    /// Returns true if a slice of the specified metadata surface has been cleared.
    bool IsMetaCleared(VAddr address, u32 slice) const {
        const auto& it = surface_metas.find(address);
        if (it != surface_metas.end()) {
            return it->second.clear_mask & (1u << slice);
        }
        return false;
    }

    /// Clears all slices of the specified metadata surface.
    bool ClearMeta(VAddr address) {
        auto it = surface_metas.find(address);
        if (it != surface_metas.end()) {
            it->second.clear_mask = u32(-1);
            it->second.shader_written = false;
            return true;
        }
        return false;
    }

    /// port: a shader wrote a metadata surface with more than a clear (it encodes it): readers
    /// see those words, not HtileReadValue's, until the next clear.
    void NoteMetaShaderWrite(VAddr address) {
        auto it = surface_metas.find(address);
        if (it != surface_metas.end()) {
            it->second.shader_written = true;
        }
    }

    /// Updates the state of a slice of the specified metadata surface.
    bool TouchMeta(VAddr address, u32 slice, bool is_clear) {
        auto it = surface_metas.find(address);
        if (it != surface_metas.end()) {
            if (is_clear) {
                it->second.clear_mask |= 1u << slice;
            } else {
                it->second.clear_mask &= ~(1u << slice);
            }
            return true;
        }
        return false;
    }

    /// Runs the garbage collector.
    void RunGarbageCollector();

    template <typename Func>
    void ForEachImageInRegion(VAddr cpu_addr, size_t size, Func&& func) {
        using FuncReturn = typename std::invoke_result<Func, ImageId, Image&>::type;
        static constexpr bool BOOL_BREAK = std::is_same_v<FuncReturn, bool>;
        const u64 first_page = cpu_addr >> Traits::PAGE_BITS;
        ForEachPage(cpu_addr, size, [this, first_page, cpu_addr, size, func](u64 page) {
            const auto it = page_table.find(page);
            if (it == nullptr) {
                if constexpr (BOOL_BREAK) {
                    return false;
                } else {
                    return;
                }
            }
            for (const auto& entry : it->entries) {
                const u64 base_page = entry.Address() >> Traits::PAGE_BITS;
                if (page != std::max(first_page, base_page)) {
                    continue;
                }
                if (!entry.Overlaps(cpu_addr, size)) {
                    continue;
                }
                Image& image = slot_images[entry.id];
                if constexpr (BOOL_BREAK) {
                    if (func(entry.id, image)) {
                        return true;
                    }
                } else {
                    func(entry.id, image);
                }
            }
            if constexpr (BOOL_BREAK) {
                return false;
            }
        });
    }

    template <typename Func>
    void ForEachImageWithAddress(VAddr cpu_addr, Func&& func) {
        const auto it = page_table.find(cpu_addr >> Traits::PAGE_BITS);
        if (it == nullptr) {
            return;
        }
        const u32 key = u32(cpu_addr >> 8);
        for (const auto& entry : it->entries) {
            if (entry.key == key) {
                func(entry.id, slot_images[entry.id]);
            }
        }
    }

private:
    /// Iterate over all page indices in a range
    template <typename Func>
    static void ForEachPage(PAddr addr, size_t size, Func&& func) {
        static constexpr bool RETURNS_BOOL = std::is_same_v<std::invoke_result<Func, u64>, bool>;
        const u64 page_end = (addr + size - 1) >> Traits::PAGE_BITS;
        for (u64 page = addr >> Traits::PAGE_BITS; page <= page_end; ++page) {
            if constexpr (RETURNS_BOOL) {
                if (func(page)) {
                    break;
                }
            } else {
                func(page);
            }
        }
    }

    /// Copies image memory back to CPU.
    void DownloadImageMemory(ImageId image_id,
                             DownloadMemoryFlags flags = DownloadMemoryFlags::None);

    /// port: a recorded image-to-staging copy, written to guest memory after a GPU wait.
    struct PendingDownload {
        VAddr guest_address;
        u32 size;
        Vulkan::StagingBufferRef staging;
    };
    /// Records the copy of a GPU-modified image into a deferred staging allocation.
    std::optional<PendingDownload> RecordImageDownload(ImageId image_id);

    /// Register image in the page table
    void RegisterImage(ImageId image);

    /// Unregister image from the page table
    void UnregisterImage(ImageId image);

    /// Track CPU reads and writes for image
    void TrackImage(ImageId image_id);

    /// Stop tracking CPU reads and writes for image
    void UntrackImage(ImageId image_id);
    void UntrackImageHead(ImageId image_id);
    void UntrackImageTail(ImageId image_id);

    void MarkAsMaybeDirty(ImageId image_id, Image& image);

    void RegisterMeta(VAddr address, MetaDataInfo info);
    void UnregisterMeta(VAddr address, u64 owner_uid);

    /// Removes the image and any views/surface metas that reference it.
    void DeleteImage(ImageId image_id);

    /// Touch the image in the LRU cache.
    void TouchImage(Image& image);

    /// Removes image from the cache and schedules it for deletion.
    void FreeImage(ImageId image_id) {
        const auto& image = slot_images[image_id];
        ResolveReadbacks(image.info.guest_address, image.info.guest_size, true,
                         ReadbackSource::ImageFree);
        {
            std::scoped_lock lk{slot_images[image_id].mutex};
            UntrackImage(image_id);
        }
        UnregisterImage(image_id);
        DeleteImage(image_id);
    }

    void GarbageCollectImages();
    void GarbageCollectSamplers();

private:
    const Vulkan::Instance& instance;
    Vulkan::Scheduler& scheduler;
    Vulkan::Runtime& runtime;
    AmdGpu::Liverpool* liverpool;
    BufferCache& buffer_cache;
    PageManager& page_manager;
    PageTable page_table;
    Common::SlotVector<Image> slot_images;
    Common::SlotVector<ImageView> slot_image_views;
    Common::SlotVector<Sampler> slot_samplers;
    BlitHelper blit_helper;
    TileManager tile_manager;
    std::unordered_map<u64, SamplerId> samplers;
    std::unordered_set<ImageId> download_images;
    u64 total_used_memory = 0;
    u64 gc_evictions = 0, gc_downloads = 0, gc_dropped = 0; ///< port: pressure report
    u64 gc_kept = 0, gc_kept_bytes = 0; ///< port: old images the latest pass could not evict
    std::chrono::steady_clock::time_point gc_report_time{};
    /// port: registered images and their guest size, for the once-a-minute memory line.
    u64 image_count = 0, image_bytes = 0;
    std::chrono::steady_clock::time_point memory_report_time{};
    void ReportMemory();
    /// port: gc_tick at each of the last 64 seconds (GarbageCollectImages).
    std::array<u64, 64> gc_tick_at_second{};
    u64 gc_second = 0;
    u64 trigger_gc_memory = 0;
    u64 pressure_gc_memory = 0;
    u64 critical_gc_memory = 0;
    u64 total_used_samplers = 0;
    u64 trigger_gc_samplers = 0;
    u64 pressure_gc_samplers = 0;
    u64 critical_gc_samplers = 0;
    u64 gc_tick = 0;
    Common::LRUCache<Image> image_lru_cache;
    Common::LRUCache<Sampler> sampler_lru_cache;
    const bool readback_linear_images;
    const bool deferred_readbacks = [] {
        const char* value = std::getenv("THEORDER_DEFERRED_READBACK");
        return value && value[0] == '1';
    }();
    /// port: THEORDER_STALE_READBACK=1 (needs deferred readbacks). A pending copy only blocks CPU
    /// writes: CPU reads see the version the GPU published last (one frame late) instead of
    /// waiting for the GPU. Writes still wait, so no byte is lost.
    const bool stale_readbacks = deferred_readbacks && [] {
        const char* value = std::getenv("THEORDER_STALE_READBACK");
        return value && value[0] == '1';
    }();
    template <bool track>
    void WatchReadback(VAddr address, u64 size);
    struct ReadbackImageStat {
        VAddr address;
        u32 width, height, bits, copies, cpu_accesses;
        vk::Format format;
        u64 Bytes() const { return u64{width} * height * bits / 8; }
    };
    std::array<ReadbackImageStat, 16> readback_images{};
    u64 readbacks_released = 0;
    ReadbackQueue<Vulkan::StagingBufferRef> pending_readbacks;
    std::mutex readback_mutex;
    std::atomic<bool> has_pending_readbacks{false};
    u64 readbacks_queued = 0, readbacks_completed = 0, readbacks_waited = 0;
    u64 readbacks_canceled = 0, readbacks_peak_bytes = 0;
    std::chrono::steady_clock::time_point readback_report = std::chrono::steady_clock::now();
    ReadbackSourceStats readback_sources;
    void RetireReadbacks();
    std::mutex mutex;
    /// port: under `mutex`. GPU-modified images hold the written data themselves.
    void ApplyBackingWrites();
    std::mutex backing_writes_mutex;
    std::vector<std::pair<VAddr, u64>> backing_writes;
    std::atomic<bool> has_backing_writes{false};
    /// port: images with the same guest address (guarded by `mutex`). `backing` holds the
    /// complete contents; `writer` is a newer write not yet copied into it.
    struct AliasState {
        u64 backing_uid{};
        u64 writer_uid{};
        ImageId backing{};
        ImageId writer{};
        u32 members{};
        void ResetAuthority() {
            const u32 count = members;
            *this = {};
            members = count;
        }
    };
    tsl::robin_map<VAddr, AliasState> alias_states;
    std::atomic<u64> alias_generation{};
    [[nodiscard]] bool IsLiveImage(ImageId image_id, u64 image_uid) const {
        return image_id && slot_images.IsAllocated(image_id) &&
               slot_images[image_id].image_uid == image_uid;
    }
    template <typename Func>
    void ForEachAlias(const Image& image, Func&& func);
    void SynchronizeAlias(ImageId image_id);
    [[nodiscard]] bool CommitAliasWriter(AliasState& state);
    void CopyAlias(ImageId src_id, ImageId dst_id, const Extent3D& extent);
    void InvalidateAlias(Image& image);
    // port: FindImage results for unchanged image registrations (guarded by `mutex`).
    struct FindImageCacheEntry {
        VAddr address = 0;
        u64 size = 0;
        Extent3D extent{};
        vk::Format format{};
        AmdGpu::ImageType type{};
        bool exact_fmt = false;
        bool needs_native = false;
        BindingType binding{};
        u32 levels = 0;
        u32 layers = 0;
        u64 generation = ~0ULL;
        ImageId image_id{};
        int view_mip = -1;
        int view_slice = -1;
    };
    std::array<FindImageCacheEntry, 1024> find_image_cache{};
    std::atomic<u64> registry_generation{0};
    std::mutex samplers_mutex;
    std::mutex download_images_mutex;
    // port: std containers (upstream uses abseil, which this build does not ship).
    std::unordered_map<VAddr, MetaDataInfo> surface_metas;
};
DECLARE_ENUM_FLAG_OPERATORS(TextureCache::DownloadMemoryFlags)

} // namespace VideoCore
