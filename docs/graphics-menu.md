# Live graphics controls

Open the menu with **Insert** or **R3 + L2**. DLSS, NR, output resolution,
display mode and sharpening update while the game runs. Model/size changes can
briefly stall while GPU resources are recreated; no game restart is required.

Close the menu to save to **user/graphics.ini**, separately for each edition.
Both shortcuts respect saved choices. The NR shortcut enables NR on first setup
when no preference exists. Explicit `--neural 0/1` and `--nr-strength 0.65`
override the saved choice for one launch.

Defaults are native **1920 x 1080 at 30 FPS**, background shader compilation and
optional NVIDIA processing off. There is **no 60 FPS gameplay hack or frame
generation**. Guest vblank remains 60 Hz with every-other-vblank presentation.

| Control | Choices |
|---|---|
| Output resolution | 720p, 1080p, 1440p, 2160p; independent of window size |
| Anti-aliasing / upscale | Native or NVIDIA DLSS / DLAA |
| DLSS quality | DLAA, Quality, Balanced, Performance, Ultra Performance |
| DLSS model hint | Driver default, J, K, L, M |
| NR render hint | Default, 1, 2, 3 |
| NR style | Default, Natural, Cinematic |
| NR strength | 0 to 100 percent |
| NR passes | 1 to 3, with separate history and scratch images per pass |
| Display | Windowed, borderless, fullscreen |
| Sharpening | RCAS and strength, separate from anti-aliasing |

**DLAA is anti-aliasing, not sharpening.** DLAA renders at output size; other
DLSS modes reconstruct from a smaller scene. For example, 4K Performance renders
at 1920 x 1080, and 4K Quality at 2560 x 1440. NR processes the scene before DLSS;
the game interface is drawn afterward at output size. Some effects retain
their original resolution.

Model choices are hints defined by the build's SDK, not an enumeration of every
DLL installed on the computer. The loaded runtime and driver choose the
effective model. Rejected hints retry the runtime default. Replacing DLLs on
disk needs a restart; changing supported presets inside the loaded runtime is
live.

One NR pass is the default. Extra passes increase GPU time/memory and can
reinterpret faces and materials. Turning NR off avoids its processing cost;
lowering strength does not guarantee a speed improvement. Higher resolutions
and multiple passes remain experimental, with incomplete map/outfit coverage
and no universal 30 FPS guarantee.

Rebuild the host **and both native NVIDIA adapters for bridge ABI 3**, then run
the [staging helper](neural-rendering.md). Older adapters leave NR unavailable
instead of reading an incompatible options structure. Proprietary DLLs remain
separate from Git.

This update changes motion-vector shader interfaces and cache versions.
Incompatible shader/pipeline entries regenerate once; saves are preserved and
compatible caches persist between later launches. The settings test covers
saving/reloading NVIDIA choices; Windows smoke tests exercised live controls
and output changes. See [validation](validation.md) for limits. These Windows
checks do not establish native Linux NVIDIA support or actual Steam Deck play.
