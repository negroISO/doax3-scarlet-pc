# Fortune Poolside camera freeze

The CUSA04555 1.19 guest calls sceGnmDrawOpaqueAuto with a command buffer,
seven DWORDs of reserved space and flags. Its wrapper at guest offset 1306a0
advances the write pointer by 0x1c only when this function returns success.
The inherited retail stub returned success without writing the reservation.
Old command data then became live PM4 packets when the pool water effect ran.
The parser stopped at those words, skipped the remainder including completion
commands, and the guest stopped producing frames.

Evidence: exact-entry GDB register capture (pointer / size7 / flags0), disassembly
of the actual guest call, and full failing DCB snapshots. The first bad snapshot
stops at word73539/97933 after partial/stale commands; the GPU command processor
is idle afterward. The faulty GNM implementation is shared by the native and
neural routes; NR was the reported and reproduced route.

The function now validates the arguments and writes a complete seven-word
DRAW_INDEX_AUTO packet (zero explicit count, AUTO_INDEX | USE_OPAQUE) plus valid
NOP padding. Invalid arguments return failure without touching the reservation.
The opaque flag follows the existing AMD byte-count draw convention in
[Mesa's draw emission](https://chromium.googlesource.com/chromiumos/third_party/mesa/+/a11d1bd247df18057ecb9945e01bf39ff85d8d9d/src/amd/vulkan/radv_cmd_buffer.c).
gnm-opaque-test calls the real driver function against stale data and verifies
the next-packet boundary, sentinels and invalid-argument behavior.

The private saved-game NR replay now passes the pool animation, returns to
the hotel and saves. Residual streamout/geometry-shader and COPY_DATA support
remain incomplete, so this fix does not establish full splash/particle fidelity.

Performance instrumentation also had a separate deadlock: collecting timestamps
with WAIT on the draw recorder could wait for that same recorder to reach
submission. Collection is now gated on the completed scheduler timeline and
omits in-flight samples without resetting their queries. gpu-profiler-test
executes 64 unsubmitted frame boundaries and then 16 recovery submissions.
DOAX3 profiling uses its measured final scene-copy boundary, excluding the
many UI draws that also write its display buffer from the frame count.

Counting once per actual scene also exposed inherited render-target clearing
heuristics intended for The Order's volumetric/blood effects. Their float-size
and additive-blend patterns match DOAX3 lighting targets. A once-per-frame
counter armed their zero clears, blacking out the pool before NR processing.
The identical cold-cache route rendered correctly with profiling off; all103
shared programs had byte-identical cached variants. Excluding DOAX3 from the
inferred clears restores the profiled cold-cache image. Guest-requested CMask
clears remain active. DOAX3 scene frame accounting now advances with profiling
both enabled and disabled, so diagnostics do not select different clear rules.

Evidence: private tests pool-opaque-fixed-cold-7 (black scene, profiler on),
pool-cold-no-profile-8 (correct image, profiler off), and
pool-profile-clear-fixed-9 (correct image, profiler on after guard). All use
the same production savedata copied into private users, with no cache copied.
The corrected run also renders the title's island, and paired NR captures have
finite motion/depth guides. These tests do not establish every DLC costume,
shader permutation, map or minigame; see the project validation report.

The corrected NR run completed its 1200-second test, including poker and a
day transition. A separate cold-cache native run completed 300 seconds through
the same pool-camera sequence and hotel return. Over the matched 170-275-second
window after the first flip, both averaged 29.97 completed guest flips/second
at 1920x1080 and the 30 FPS cap. The NR route's scoped guide/evaluation work
cost approximately 3.45 GPU milliseconds per frame on the test RTX 5090.
This does not imply zero NR overhead or the same performance on other hardware.

Subsequent private 1800- and 3600-second runs exercised daytime beach/pool
activities, repeated water-entry/reset sequences, menus and day transitions.
Their save data was not promoted to the production user. The detailed coverage
matrix and raw capture references are kept in
`diagnostics/fortune-119/validation-report.md` at the project root.
