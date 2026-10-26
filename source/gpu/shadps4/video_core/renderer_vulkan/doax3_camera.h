// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>

namespace Vulkan {
// Both retail builds were captured with four scene MRTs, this camera block,
// reversed D32S8 depth and the same post-tone-map display-copy shader.
inline bool UsesDoax3SceneLayout(std::string_view serial) {
  return serial == "CUSA14173" || serial == "CUSA04555";
}

struct Doax3Camera {
  std::array<float, 12> view{}, inverse_view{};
  std::array<float, 4> projection{};
};

// Scarlet/Fortune's 432-byte scene block: row-vector view-projection at 0, camera
// position at 16, and row-vector rigid view at 20. Admit only a consistent
// camera/projection.
inline std::optional<Doax3Camera> ReadDoax3Camera(std::span<const float> data) {
  if (data.size() != 108)
    return {};
  for (float value : data)
    if (!std::isfinite(value))
      return {};
  for (int r = 0; r < 3; ++r)
    if (std::abs(data[23 + r * 4]) > 1e-4f)
      return {};
  if (std::abs(data[35] - 1.f) > 1e-4f)
    return {};
  for (int c = 0; c < 3; ++c) {
    for (int d = c; d < 3; ++d) {
      float product = 0;
      for (int r = 0; r < 3; ++r)
        product += data[20 + r * 4 + c] * data[20 + r * 4 + d];
      if (std::abs(product - (c == d ? 1.f : 0.f)) > .002f)
        return {};
    }
  }
  Doax3Camera result;
  for (int r = 0; r < 3; ++r) {
    float origin = data[32 + r];
    for (int c = 0; c < 3; ++c) {
      result.view[r * 4 + c] = data[20 + c * 4 + r];
      result.inverse_view[r * 4 + c] = data[20 + r * 4 + c];
      origin += data[16 + c] * result.view[r * 4 + c];
    }
    if (std::abs(origin) > .02f)
      return {};
    result.view[r * 4 + 3] = data[32 + r];
    result.inverse_view[r * 4 + 3] = data[16 + r];
  }
  for (int c = 0; c < 3; ++c) {
    for (int r = 0; r < 3; ++r)
      result.projection[c] += data[r * 4 + c] * data[20 + r * 4 + c];
    for (int r = 0; r < 3; ++r)
      if (std::abs(data[r * 4 + c] -
                   data[20 + r * 4 + c] * result.projection[c]) > .002f)
        return {};
  }
  result.projection[3] = data[14] - data[34] * result.projection[2];
  for (int r = 0; r < 4; ++r)
    if (std::abs(data[r * 4 + 3] - data[20 + r * 4 + 2]) > .002f)
      return {};
  if (result.projection[0] < .05f || result.projection[1] < .05f ||
      result.projection[0] > 100.f || result.projection[1] > 100.f ||
      result.projection[2] >= 0.f || result.projection[3] <= 0.f)
    return {};
  if (std::abs(data[12] - data[32] * result.projection[0]) > .02f ||
      std::abs(data[13] - data[33] * result.projection[1]) > .02f)
    return {};
  return result;
}
} // namespace Vulkan
