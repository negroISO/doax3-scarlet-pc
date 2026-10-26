// SPDX-License-Identifier: GPL-2.0-or-later
// THEORDER_LIGHTING_TEST=shading,msdepth,dref,bias (vk_lighting_test.h): the four changes of the
// KrachDev/shadPS4_THEORDER "lighting and scanline" build, each behind its own token so a tester
// can bisect them. No game or Vulkan device required.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "video_core/renderer_vulkan/vk_lighting_test.h"

using namespace Vulkan;

int main() {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
#endif
    using LightingTest::Flags;
    const Flags none{};
    assert(LightingTest::Parse(nullptr) == none);
    assert((LightingTest::Parse("") == none));
    assert((LightingTest::Parse("0") == none));
    assert(!none.Any());

    // Tokens in any order, separated by commas or spaces; unknown tokens are ignored.
    const Flags all{.shading = true, .msdepth = true, .dref = true, .bias = true};
    assert((LightingTest::Parse("shading,msdepth,dref,bias") == all));
    assert((LightingTest::Parse("all") == all));
    assert((LightingTest::Parse("1") == all));
    assert((LightingTest::Parse("bias dref") == Flags{.dref = true, .bias = true}));
    assert((LightingTest::Parse("bogus,msdepth") == Flags{.msdepth = true}));
    assert((LightingTest::Parse("SHADING") == Flags{.shading = true}));
    assert(all.Any());

    // The marker a tester's last_run.log must contain, in token order.
    assert(LightingTest::Marker(all) == "GPU: lighting test on: shading msdepth dref bias");
    assert((LightingTest::Marker(Flags{.dref = true}) == "GPU: lighting test on: dref"));
    assert(LightingTest::Marker(none).empty());

    // Every combination of codegen settings gets a separate shader binary cache version.
    assert(LightingTest::ShaderBinarySalt(false, false, false) == 0u);
    assert(LightingTest::ShaderBinarySalt(true, false, false) == 1u);
    assert(LightingTest::ShaderBinarySalt(false, true, false) == 2u);
    assert(LightingTest::ShaderBinarySalt(true, true, false) == 3u);
    for (unsigned salt = 0; salt < 8; ++salt) {
        assert(LightingTest::ShaderBinarySalt(salt & 1, salt & 2, salt & 4) == salt);
    }
    assert(LightingTest::ShaderBinaryVersion(7u, 0u) == 7u);
    assert(LightingTest::ShaderBinaryVersion(7u, 3u) == 7u + 16u * 3u);

    std::puts("lighting test knob OK");
    return EXIT_SUCCESS;
}
