# Scarlet validation

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
