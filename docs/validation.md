# Scarlet validation

## Live graphics update, 2026-10-26

Both edition exports build on Windows with Clang 23.1.3 (RelWithDebInfo).
Both ABI 3 NVIDIA adapters build with MSVC. Graphics-settings persistence,
upscaler-support and scene-resolution tests pass in both exports. The portable
launcher's path/version/dependency check passes against the required merged
game data. Native 1080p/30 FPS is the first-run baseline; the saved graphics
choices are no longer overwritten by the NR shortcut or an unchanged diagnostic
preset file.

A fresh public-source Scarlet executable and its newly built adapters completed
a 125-second title/island/menu smoke test with exit 0, 1080p DLAA and one NR pass.
The captured island renders, and runtime logs confirm successful neural
evaluations. This bounded check does not cover all activities or outfits.

Live DLSS/NR controls and higher output resolutions share the renderer exercised
in the latest Fortune Windows tests. Its last nine-minute 4K Quality/three-pass
run ended normally, but does not establish Scarlet activity coverage. The older
Scarlet measurements below remain dated evidence rather than a benchmark of
the new modes.

Validation-layer retesting of the shared renderer cleared motion-varying
component-limit and sRGB image-view usage errors. Startup swapchain
write-after-present hazards and buffer barriers inside dynamic rendering remain
unresolved. An earlier, pre-fix high-resolution transition lost the device;
later bounded Fortune runs did not reproduce it. High-resolution and multi-pass
modes remain experimental, without a new performance guarantee.

The shader interface/cache version changed, so incompatible entries compile
again once. These checks cover Windows; the native Linux/Steam Deck release
has not been rebuilt or validated with this graphics update.

## Clean repository export checks, 2026-10-26

The host and optional two-DLL bridge both build from this clean source export.
The portable preparation command reproduces the original edition's linked-image
hash. Normal and NR root CMD launchers each complete a12-second startup with
exit0 using a separate new user directory. These short checks are startup tests,
not new full-scene/NR-quality benchmarks; no successful neural evaluation is
claimed from them. The staged-file audit covers payload exclusions, source
completeness, secret patterns and local README/image links.
See [export-validation.json](export-validation.json) for recorded hashes/checks.

## Earlier gameplay evidence

Recorded2026-10-25, Windows x64, RTX 5090,1920×1080,30 FPS target,60 Hz
guest vblank, async shaders. Tests used private save/cache copies.

| Test |Duration|Measured mean|p99 interval|Scope|
|---|---:|---:|---:|---|
|scarlet-108-branding-native-1|120s|29.974FPS|34.280ms|Native title/island/menu|
|scarlet-108-branding-nr-1|220s|29.963FPS|34.301ms|Animated Relax scene with NR/object motion|

Both reached their planned test boundary with exit0; neither logged the tested
GPU/shader-capture/compilation or PM4 failure patterns. The NR run recorded over
5100 successful neural evaluations. Separate normal/NR root development
launchers passed 12-second startup checks.89 launcher and12 shortcut/artwork
tests passed, including the disabled upstream-updater guard.

The source also includes the later shared Fortune opaque-draw/profiler/clear
fixes. Their inclusion does not mean the entire Scarlet activity matrix was
retested afterward. The new portable repository wrappers are checked separately
during export; the screenshots/performance figures above remain dated evidence
from the original working project.

Included images: native title/island from branding-native-1; evening beach/menu
from nr-native-object-vacation-1. The latter is selected scene evidence, not an
extra full-game benchmark.

Unverified/incomplete: exhaustive activities/DLC/outfits/maps/Photo Paradise,
every rotating-island angle and other black-background reports, photo-file
export, audio playback, complete streamout/COPY_DATA effects and actual Steam
Deck rendering/performance. No game save, proprietary DLL or linked image is
included in the repository.
