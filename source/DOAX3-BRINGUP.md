# DOAX3 Scarlet bring-up — 2026-10-24

Current milestone: matched Scarlet 1.08 reaches the actual main menu, character
selection, vacation dialogue and first tutorial. A copied private save reloaded.
Full activities and audio remain unverified. No guest executable byte patches.

The supplied hr-doax3scarletv108.pkg update was initially overlooked.
Its CUSA14173 APP_VER01.08 metadata and all 37 extracted
files are now verified; merged game CUSA14173-v108 preserves the original base.
Local candidate launcher selects runtime/scarlet-108-candidate, a separate user,
and synchronous compilation. F8 maps to Options/skip FMV, respecting an existing
THEORDER_KEY_MAP override; controller Options/Start works too.

Actual main-menu image: diagnostics/smoke/scarlet-108-menu-live-1/
output_002_1920x1080.png. Travel / Owner Paradise / VR Paradise / Options appear
with a rendered island and blue sky. Input scripts explicitly press Options.

## Shader and movie fixes

Async compilation cached post-translation runtime info while lookup used original
draw inputs. NVIDIA clip-distance lowering increments FS num_inputs, causing every
subsequent lookup to miss. Async and initial sync keys now preserve the input
runtime; translation still gets its private mutable copy. No clip emulation,
descriptor comparison or cache-admission condition was weakened. Cache binary /
metadata / pipeline-key versions are now 16/12/8 to reject old ambiguous keys.

The 75-second pre-fix attribution run reported 32,706 new-permutation events;
the 90-second post-fix diagnostic reported 28. All 101 post-fix captured SPIR-V
modules validate. These are correctness diagnostics, not controlled FPS tests.
Async still leaves the menu background black after pending work reaches zero;
keep the candidate synchronous pending a separate draw/pipeline investigation.
The old async run emitted 12,458 modules but only 129 distinct binaries.

Movie EOF restart also had a verified bug: resetting raw AVIO bytes before a
demuxer seek failed 3/3 restarts on MOV_OPENING.mp4; timestamp-only seek passed3/3.
The new production RewindDemuxer helper preserves demuxer state. Demux read/seek
failures emit one error after cleanup rather than both error and EOF.
avplayer-seek-test passes four restarts using the production guest-file adapter,
on both a generated MPEG4 fixture and the game's H264 opening movie.

Async compiler, async pipeline-cache, fetch-snapshot, pipeline-binary-cache and
AvPlayer seek targets pass. Valid SPIR-V alone does not establish correct shader
selection or rendered pixels; retain the runtime captures as the correctness gate.
25,706 old dump files were CRC-verified into shader-dumps.zip, reclaiming619083822B;
logs, manifests, screenshots and saves remain. Original milestone PE is backed up.

The initial base-version bring-up findings follow.

Use matching CUSA14173 Scarlet v1.00 executable AND assets. The previous local
setup mixed Scarlet's executable with Fortune CUSA04555 assets. After the runtime
fixes, resource lookup 0x34f417f2 exposed that mismatch (present in Scarlet's catalog,
absent from Fortune's). Keep the chunk directories intact.

Link libc.prx and libSceFios2.prx from the game, then the user's own
libSceFiber.sprx, ulobjmgr.sprx and libSceUlt.sprx. Set the opt-in
`THEORDER_NATIVE_LIBRARIES=libSceFiber,ulobjmgr,libSceUlt` for this candidate.
Host overrides remain the default unless this explicit library list selects a
native binding. Native Fiber requires Windows TCB load rewriting for registers
beyond RAX; the linker/loader now preserve all 16 destinations.

Runtime fixes cover verified NID contracts, scoped-only import names, UserService
Initialize2, screenshot metadata, HMD scratch sizes, the opaque DiscMap query's
three output words, actual Getdents enumeration, event-flag AND/OR and clear modes,
and RTC ABI shadowing. KTGL's 0x22 event wait means OR plus clear matched bits.
`THEORDER_FILE_TRACE=1` enables diagnostic file operations; normal runs omit it.

Validation: import-resolution-test, event-flags-test, rtc-test, services-test pass;
Python test_import_names.py and all three test_link_modules.py tests pass;
git diff --check passes. The 45-second and 90-second private-save input smokes
reached rendered content without guest faults or unsupported-import stops and
ended at the watchdog. Timeouts alone are not treated as working-gameplay proof.
No FPS performance claim is made from startup or static UI.

This archived note refers to the original development candidate and captures.
Use the edition-specific launcher and setup documentation at this repository's
root. The original development game data, runtime images and saves are excluded.

Next gate: reach a controllable activity, check rendering/input/audio, then verify
save and reload. Diagnose a concrete remaining fault before adding more stubs.
The 75-second prepared-candidate transition repeat reaches character selection
(diagnostics/smoke/scarlet-transition-2/output_001_1920x1080.png). Background
geometry/sky has visible defects; three thread-join warnings occur without stopping
the game. Do not claim full rendering correctness or gameplay from this milestone.
