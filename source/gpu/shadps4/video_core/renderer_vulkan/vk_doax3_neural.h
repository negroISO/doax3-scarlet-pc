// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "video_core/texture_cache/image.h"

namespace VideoCore {
class TextureCache;
}
namespace Vulkan {
class Instance;
class Scheduler;
class Runtime;
class CameraMotion;

// Native-resolution SDR, after the DOAX3 tone-map copy and before its UI.
class Doax3Neural {
public:
  Doax3Neural(const Instance &, Scheduler &, Runtime &,
              VideoCore::TextureCache &, CameraMotion &);
  ~Doax3Neural();
  void Record(VideoCore::ImageId color);

private:
  const Instance &instance;
  Scheduler &scheduler;
  Runtime &runtime;
  VideoCore::TextureCache &textures;
  CameraMotion &camera;
  VideoCore::UniqueImage motion;
  vk::UniqueImageView motion_view;
  u32 width = 0, height = 0, dump_index = 0;
  u64 frames = 0;
  bool enabled = false, guides_only = false, initialized = false, noted = false;
};
} // namespace Vulkan
