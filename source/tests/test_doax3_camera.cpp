// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/doax3_camera.h"
#include <cassert>
#include <cmath>
#include <limits>

int main() {
  assert(Vulkan::UsesDoax3SceneLayout("CUSA14173"));
  assert(Vulkan::UsesDoax3SceneLayout("CUSA04555"));
  assert(!Vulkan::UsesDoax3SceneLayout("UNKNOWN"));
  std::array<float, 108> data{};
  // Camera at (1,2,3), looking along +Z. Reversed projection, near=.3,
  // far=4096.
  data[16] = 1;
  data[17] = 2;
  data[18] = 3;
  data[20] = data[25] = data[30] = data[35] = 1;
  data[32] = -1;
  data[33] = -2;
  data[34] = -3;
  const float zs = .3f / (.3f - 4096.f), zo = -4096.f * zs;
  data[0] = 1.3f;
  data[5] = 2.3f;
  data[10] = zs;
  data[11] = 1;
  data[12] = -1.3f;
  data[13] = -4.6f;
  data[14] = -3 * zs + zo;
  data[15] = -3;
  auto camera = Vulkan::ReadDoax3Camera(data);
  assert(camera);
  assert(camera->view[3] == -1 && camera->inverse_view[3] == 1);
  assert(camera->view[7] == -2 && camera->inverse_view[7] == 2);
  assert(std::abs(camera->projection[0] - 1.3f) < 1e-6f);
  assert(std::abs(camera->projection[3] / (1 - camera->projection[2]) - .3f) <
         1e-6f);
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 4; ++c) {
      float product = c == 3 ? camera->view[r * 4 + 3] : 0;
      for (int k = 0; k < 3; ++k)
        product += camera->view[r * 4 + k] * camera->inverse_view[k * 4 + c];
      assert(std::abs(product - (r == c ? 1.f : 0.f)) < 1e-6f);
    }
  assert(!Vulkan::ReadDoax3Camera(std::span(data).first(107)));
  auto invalid = data;
  invalid[25] = 2;
  assert(!Vulkan::ReadDoax3Camera(invalid));
  invalid = data;
  invalid[17] += 1;
  assert(!Vulkan::ReadDoax3Camera(invalid));
  invalid = data;
  invalid[7] = .1f;
  assert(!Vulkan::ReadDoax3Camera(invalid));
  invalid = data;
  invalid[0] = std::numeric_limits<float>::quiet_NaN();
  assert(!Vulkan::ReadDoax3Camera(invalid));

  // Fortune 1.19 title island, capture frame_20261025-224329_1 pass 11:
  // four 1920x1080 MRTs + D32S8, VS cf8cc61d slot 2. Shadow cameras from
  // the same capture are orthographic and must not enter this parser.
  const std::array<float, 108> fortune{
      1.03118f, .0353186f, 4.45669e-5f, -.608443f,
      0, 2.30969f, -1.83971e-6f, .0251163f,
      -.790989f, .0460433f, 5.81e-5f, -.7932f,
      -4.27961f, -7.80169f, .298905f, 15.2482f,
      11.9421f, 2.9927f, 10.158f, 1,
      .793451f, .0152866f, -.608443f, 0,
      0, .999685f, .0251163f, 0,
      -.608635f, .0199285f, -.7932f, 0,
      -3.29299f, -3.37674f, 15.2482f, 1};
  camera = Vulkan::ReadDoax3Camera(fortune);
  assert(camera);
  assert(std::abs(camera->projection[0] - 1.29962f) < .0001f);
  assert(std::abs(camera->projection[1] - 2.31042f) < .0001f);
  assert(std::abs(camera->projection[3] / (1 - camera->projection[2]) - .3f) < .0001f);
  invalid = fortune;
  invalid[3] = invalid[7] = invalid[11] = 0;
  invalid[15] = 1;
  assert(!Vulkan::ReadDoax3Camera(invalid));
}
