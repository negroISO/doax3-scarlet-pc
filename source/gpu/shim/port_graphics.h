// SPDX-License-Identifier: GPL-2.0-or-later
// port: graphics options of the in-game menu (Insert / R3+L2), kept in theorder.ini. The live ones
// apply on the next frame; the startup ones (patches, GPU readback modes) are read by run.py and
// apply through "Apply and restart". The launcher reads and writes the same keys.
#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace PortGraphics {

enum DisplayMode : int { Windowed = 0, Borderless = 1, Fullscreen = 2, DisplayModeCount };
inline constexpr std::array<std::string_view, DisplayModeCount> DisplayModeKeys = {
    "windowed", "borderless", "fullscreen"};
inline constexpr std::array<const char*, DisplayModeCount> DisplayModeNames = {
    "Windowed", "Borderless", "Fullscreen"};

/// Present rate limits; 0 = unlimited.
inline constexpr std::array<int, 5> FpsLimits = {30, 60, 120, 240, 0};

/// Aspect ratios: game patch suffixes ("Aspect Ratio - <name>"); "original" is the game's own
/// 2.40:1 picture with black bars, the others fill a screen of that shape.
inline constexpr std::array<std::string_view, 4> Aspects = {"original", "16:9", "16:10", "21:9"};
inline constexpr std::array<const char*, 4> AspectNames = {"Original (2.40:1, black bars)", "16:9",
                                                           "16:10", "21:9"};
/// The width / height the patch gives the game's picture (21:9 is 2560x1080's 64:27).
inline constexpr std::array<float, 4> AspectValues = {2.4f, 16.0f / 9.0f, 1.6f, 64.0f / 27.0f};
/// Engine frame rate ceilings: 60 is the game's own, 120 and 240 are frame rate patches.
inline constexpr std::array<int, 3> EngineFps = {60, 120, 240};

template <typename T, std::size_t N>
constexpr int IndexOf(const std::array<T, N>& values, const T& value, int fallback) {
    for (std::size_t i = 0; i < N; ++i) {
        if (values[i] == value) return int(i);
    }
    return fallback;
}

inline int ParseDisplayMode(std::string_view text) {
    return IndexOf(DisplayModeKeys, text, Windowed);
}
/// A listed limit, else unlimited.
inline int ParseFpsLimit(int value) {
    return IndexOf(FpsLimits, value, -1) >= 0 ? value : 0;
}
/// Index into Aspects; unknown text is the original.
inline int ParseAspect(std::string_view text) {
    return IndexOf(Aspects, text, 0);
}
/// A listed ceiling, else 120 (the default patch).
inline int ParseEngineFps(int value) {
    return IndexOf(EngineFps, value, -1) >= 0 ? value : 120;
}

/// Frames the GPU command thread may queue ahead of the GPU; 0 = unbounded.
inline constexpr std::array<int, 3> FramesQueued = {1, 2, 0};
inline int ParseFramesQueued(int value) {
    return IndexOf(FramesQueued, value, -1) >= 0 ? value : 1;
}
/// RCAS strength in percent; invalid values fall back to 75.
inline int ParseRcasStrength(int value) {
    return value >= 0 && value <= 100 ? value : 75;
}
/// FSR RCAS attenuation for a strength: 0 is the sharpest, 2 the softest.
inline float RcasAttenuation(int strength) {
    return 2.0f * (1.0f - float(ParseRcasStrength(strength)) / 100.0f);
}
/// Whether the FSR pass runs: FSR 1 when the window is larger than the game's image, RCAS
/// alone when they are the same size. A smaller window is left to the regular scaling.
inline bool FsrPassRuns(bool fsr1, bool rcas, int in_w, int in_h, int out_w, int out_h) {
    const bool upscale = in_w < out_w && in_h < out_h;
    const bool same = in_w == out_w && in_h == out_h;
    return (fsr1 && upscale) || (rcas && same);
}

/// The game's picture inside its w x h display buffer: the engine centers a picture of the
/// patched aspect ratio there, with black bars on the sides (16:10) or above and below (21:9).
struct Rect {
    int x, y, w, h;
};
inline Rect PictureRect(int w, int h, float aspect) {
    if (w <= 0 || h <= 0 || aspect <= 0.0f) return {0, 0, w, h};
    if (aspect >= float(w) / float(h)) {
        const int ph = std::min(h, int(float(w) / aspect + 0.5f));
        return {0, (h - ph) / 2, w, ph};
    }
    const int pw = std::min(w, int(float(h) * aspect + 0.5f));
    return {(w - pw) / 2, 0, pw, h};
}

/// What the present limit really allows: the engine ceiling caps it.
inline int EffectiveFps(int limit, int engine_fps) {
    return limit == 0 || limit > engine_fps ? engine_fps : limit;
}

/// The present thread's frame limit: never above the engine rate the patches applied, since the
/// engine steps a fixed time per frame and faster flips speed the game up. 0 stays vblank paced.
inline int PresentLimit(int limit, int engine_fps) {
    return limit == 0 ? 0 : EffectiveFps(limit, engine_fps);
}

} // namespace PortGraphics
