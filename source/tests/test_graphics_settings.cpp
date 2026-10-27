// graphics.ini keys (port_settings.cpp, port_graphics.h).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include "port_graphics.h"
#include "port_settings.h"

namespace G = PortGraphics;

static std::string Read(const char* path) {
    std::ifstream in(path);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

int main() {
    assert(G::ParseDisplayMode("borderless") == G::Borderless && G::ParseDisplayMode("bogus") == G::Windowed);
    assert(G::ParseFpsLimit(240) == 240 && G::ParseFpsLimit(75) == 0);
    assert(G::ParseAspect("16:10") == 2 && G::ParseAspect("32:9") == 0);
    // The picture an aspect ratio patch leaves in the 1920x1080 display buffer (measured in dumps).
    const auto wide = G::PictureRect(1920, 1080, G::AspectValues[3]);
    assert(wide.x == 0 && wide.y == 135 && wide.w == 1920 && wide.h == 810);
    const auto tall = G::PictureRect(1920, 1080, G::AspectValues[2]);
    assert(tall.x == 96 && tall.y == 0 && tall.w == 1728 && tall.h == 1080);
    const auto original = G::PictureRect(1920, 1080, G::AspectValues[0]);
    assert(original.y == 140 && original.h == 800);
    const auto full = G::PictureRect(1920, 1080, G::AspectValues[1]);
    assert(full.x == 0 && full.y == 0 && full.w == 1920 && full.h == 1080);
    assert(G::ParseEngineFps(240) == 240 && G::ParseEngineFps(90) == 120);
    assert(G::ParseFramesQueued(2) == 2 && G::ParseFramesQueued(0) == 0 && G::ParseFramesQueued(5) == 1);
    assert(G::ParseRcasStrength(100) == 100 && G::ParseRcasStrength(101) == 75 && G::ParseRcasStrength(-1) == 75);
    assert(G::RcasAttenuation(100) == 0.0f && G::RcasAttenuation(0) == 2.0f && G::RcasAttenuation(75) == 0.5f);
    assert(G::FsrPassRuns(true, false, 1280, 720, 2560, 1440) && !G::FsrPassRuns(false, false, 1280, 720, 2560, 1440));
    assert(G::FsrPassRuns(false, true, 1920, 1080, 1920, 1080) && !G::FsrPassRuns(true, false, 1920, 1080, 1920, 1080));
    assert(!G::FsrPassRuns(true, true, 2560, 1440, 1920, 1080));  // larger image: regular scaling
    assert(G::EffectiveFps(0, 120) == 120 && G::EffectiveFps(240, 120) == 120 && G::EffectiveFps(60, 240) == 60);
    // An unpatched engine steps 1/60 s per frame: presenting faster speeds the game up.
    assert(G::PresentLimit(120, 60) == 60 && G::PresentLimit(240, 120) == 120 && G::PresentLimit(240, 240) == 240);
    assert(G::PresentLimit(0, 60) == 0 && G::PresentLimit(30, 60) == 30);  // 0: paced by vblank

    const char* path = "graphics-settings-test.ini";
    {
        std::ofstream out(path);
        out << "display_mode=borderless\nvsync=0\nfps_limit=240\nasync_shaders=0\n"
               "aspect_ratio=16:10\nengine_fps=240\ndeferred_readback=0\nstale_readback=1\n"
               "fsr1=1\nrcas=0\nrcas_strength=40\nframes_queued=2\n";
    }
    _putenv_s("THEORDER_CONFIG", path);
    _putenv_s("THEORDER_ASYNC_SHADERS", "");
    auto& v = PortSettings::Get();
    PortSettings::Load();
    assert(v.display_mode == G::Borderless && !v.vsync && v.fps_limit == 240 && !v.async_shaders);
    assert(v.aspect == 2 && v.engine_fps == 240 && !v.deferred_readback && v.stale_readback);
    // FSR 1 left the menus in 2.2.0: a saved fsr1=1 (or THEORDER_FSR1) no longer turns it on.
    assert(!v.fsr1 && !v.rcas && v.rcas_strength == 40 && v.frames_queued == 2);
    _putenv_s("THEORDER_FSR1", "1");
    PortSettings::Load();
    assert(!v.fsr1);
    _putenv_s("THEORDER_FSR1", "");
    assert(!PortSettings::GraphicsNeedRestart());
    assert(v.applied_engine_fps == 240);  // no THEORDER_ENGINE_FPS: the configured rate
    _putenv_s("THEORDER_ENGINE_FPS", "60");    // run.py: the frame rate patch did not apply
    PortSettings::Load();
    assert(v.engine_fps == 240 && v.applied_engine_fps == 60 && !PortSettings::GraphicsNeedRestart());
    _putenv_s("THEORDER_ENGINE_FPS", "");
    v.engine_fps = 120;
    assert(PortSettings::GraphicsNeedRestart());
    v.fps_limit = 60;  // live: no restart because of it
    v.engine_fps = 240;
    assert(!PortSettings::GraphicsNeedRestart());

    PortSettings::Save();
    const std::string saved = Read(path);
    for (const char* line : {"display_mode=borderless\n", "vsync=0\n", "fps_limit=60\n", "async_shaders=0\n",
                             "aspect_ratio=16:10\n", "engine_fps=240\n", "deferred_readback=0\n",
                             "stale_readback=1\n", "fsr1=0\n", "rcas=0\n", "rcas_strength=40\n",
                             "frames_queued=2\n"}) {
        assert(saved.find(line) != std::string::npos);
    }

    {
        std::ofstream out(path);
        out << "display_mode=huge\nfps_limit=7\naspect_ratio=32:9\nengine_fps=1\n"
               "rcas_strength=250\nframes_queued=9\n";
    }
    _putenv_s("THEORDER_ASYNC_SHADERS", "1");  // the environment still wins at start
    v.async_shaders = false;
    PortSettings::Load();
    assert(v.display_mode == G::Windowed && v.fps_limit == 0 && v.aspect == 0 && v.engine_fps == 120);
    assert(v.rcas_strength == 75 && v.frames_queued == 1);
    assert(v.async_shaders);
    // Live NVIDIA controls survive closing/reopening the menu and a new launch.
    v.dlss_model = 13;
    v.neural = true;
    v.nr_preset = 3;
    v.nr_style = 2;
    v.nr_passes = 2;
    v.nr_strength = 65;
    PortSettings::Save();
    v.dlss_model = 0;
    v.neural = false;
    v.nr_preset = v.nr_style = 0;
    v.nr_passes = 1;
    v.nr_strength = 100;
    _putenv_s("THEORDER_NEURAL", "");
    _putenv_s("THEORDER_NR_STRENGTH", "");
    PortSettings::Load();
    assert(v.dlss_model == 13 && v.neural && v.nr_preset == 3 && v.nr_style == 2);
    assert(v.nr_passes == 2 && v.nr_strength == 65);
    std::remove(path);
    std::puts("PASS: graphics settings");
}
