# Backlog (The Order: 1886 port)

Ordered by what a player notices. Each item: what is known, the next step, how it is checked.
Status: `todo`, `doing`, `done (commit)`, `parked (why)`. Details and history: docs/PROGRESS.md.

Measuring scene: the London rooftop from the user's save (THEORDER_USER_DIR=out/user_gameplay,
default `--pad`), ~1430 draws/frame; at 0.3.2 it runs 28-30 FPS at 30, 14-17 us/draw.

## Road to 1.0.0 (2026-10-18): playable from start to finish without graphics bugs

Sources read on 2026-10-18: upstream shadPS4 (174 commits after the vendored b4e7ae7, cloned in
~/tools/shadPS4-upstream), TdlifePeng/shadPS4 branch `1886` (20 commits written for this game,
shadPS4 PR #4798 and its comments: a player finished the game on it), the sibling ports
(bloodborne_pc merged upstream 5aa7ffa3 onto the same base and port threading; gow3_pc texture,
queue and recompiler fixes), illusion0001's patch notes. Shader hashes below are in our cache.

### R1. Graphics correctness (1.0 blockers)
1. **Float atomic min/max fallback runs both atomics** — done (2026-10-18). Control flow instead of
   OpSelect; only shaders with float atomics are translated again.
2. **Buffers bound 2 bytes off a dword (AMD jitter / Issue #18)** — done (2026-10-18): 16-bit vertex
   stream offsets enabled via push constant buffer offsets in `spirv_emit_context.cpp` when
   `vsharp.GetElementSize() == 2` even on AMD (`!needs_buffer_offsets`); `EmitReadConstBuffer`
   reconstructs 32-bit dwords from two 16-bit reads (`lo | (hi << 16)`), eliminating AMD object jitter.
3. **VSM shadows & AMD hang** — done (2026-10-18): HTILE reads emulated; VSM depth-reduction clear
   hack in `Rasterizer::DispatchDirect` (`0x9846aaff` forces `0x7F80000F` into flattened UD) populates
   shadow tile coverage masks so conversion doesn't skip shadows. DownloadMemory pipelineBarrier2
   flushes L2 cache before CPU cascade bounds readback.
4. **Texture streaming through the backing view** — done (2026-10-18, gow3_pc a101edb).
0c (aim blur) — done (2026-10-18): CPU reads of GPU-written buffers (read watch + deferred buffer
readbacks); see docs/PROGRESS.md.
5. **Pool bubbles and GPU particles** — done (2026-10-18). MUBUF addr64 translation implemented
   for loads, stores, and atomics with 64-bit pair `{vaddr, vaddr+1}` + `soffset` and carry handling.
   CPU regression test: `buffer-addr64-test`.
6. **Upstream recompiler and video core fixes** — done (2026-10-18): branch `upstream-0.20`
   has shadPS4 0.20.1 plus #5406, #5384, #5365 and #5364.
7. **NVIDIA SSBO store-load barrier** (TdlifePeng fb95ce445) — done (2026-10-18): `InsertSsboStoreLoadBarrier`
   IR pass inserts `DeviceMemoryBarrier` after stores of buffers loaded in the same dispatch on NVIDIA,
   fixing exposure compute shader flicker.
8. **Aim blur (0c), black triangle (6), one-frame artifacts** — re-check after 1-7. The
   stretched black triangles of #8 (poles and wires, RTX 30/50) match bloodborne_pc 5012c6f: index
   buffers bound with the draw's count, read past it as index 0 by drivers that check; in the merge.
9. **Black characters without fragment barycentrics** (#1 Intel Arc, #10 Intel HD 630, #15 GTX
   1070 Ti; RTX and AMD fine) — doing. Neither VK_AMD_shader_explicit_vertex_parameter nor
   VK_KHR_fragment_shader_barycentric: a pixel shader that reads its barycentric registers itself
   (not through v_interp) needs barycentric registers. Commits 69ba4f9/ec8c131 now provide
   defined registers and a per-vertex geometry shader fallback. THEORDER_NO_BARYCENTRIC=1
   exercises that path on any GPU. Remaining: confirmation on Intel/Pascal; unsupported
   topology/interpolation combinations still use the constant fallback.

### R2. Stability and progression
1. **60 FPS mode in QTEs** — todo. illusion0001's 60 FPS patch notes softlocks in chapters 6 and 9
   QTEs; ours changes the simulation clock instead, untested there. Test with out/user_ch9; if it
   locks, find the QTE timer (RE) or drop to 30 for QTEs.
2. **End of chapter 8 / chapter 9 crash** (#11: vkEndCommandBuffer fails with
   ErrorOutOfHostMemory on an RX Vega at the chapter IX load, one command buffer per guest
   submission; long submissions are now split after 65536 commands, THEORDER_SPLIT_COMMANDS;
   needs a test on AMD. Earlier: ErrorOutOfHostMemory on AMD; deadlock between the
   game and render threads from memory protection plus a readback, PR #4798; TdlifePeng e23e35535
   read-only mirror view) — todo.
3. **Chapter 2 crash** ("ArrayPtr is not implemented", PR #4798 comment) — todo: find the shader.
4. Rare start hangs after a killed run (Progress 2026-10-17) — doing.
4b. **Boot fault at 0xc00ed7** — done (2026-10-18): `sub_89fa60` unlocked the singleton registry
   mutex (`[0x16abd28]`) between its lookup (`sub_208d60`) and `scePthreadKeyCreate` for keys
   `[0x16a2660]`, `[0x16b1b90]` and `[0x16a2640]`, allowing a worker thread (`sub_20a560` /
   `sub_802210`) to overwrite `[0x16a2640]` after another thread stored its `0x1384`-byte MT19937
   state (`sub_814870`). `runtime_kernel.c` now deduplicates those singleton key creations under
   `g_key_mutex` and lazily seeds a per-thread fallback MT19937 state if `[0x16a2640]` is read
   before `sub_89fa60` stores it.
4c. **Present thread driver fault** (nvoglv64.dll+0xf0e128 in vkWaitForFences of
   Presenter::GetRenderFrame via PrepareBlankFrame, 1 in 8 boots, 2026-10-18; 0 in 8 with 0.5.0):
   watch for it in more runs before looking for a race in the blank-frame path.
5. gow3_pc: empty shader guard (368d899) — done (2026-10-18, `TranslateProgram` substitutes
   `s_endpgm`); sparse binding queue (195c7ab, RADV/Steam Deck) — todo.

### R3. Performance
1. Pipeline compiles during play (the main blinking cause on a new PC, #14): ship the pipeline
   key list a full playthrough meets and build it at the first start; cut permutations (upstream
   PR #5218 skips images and samplers behind untaken branches).
2. Items 1, 3, 4 below; upstream renderer optimizations (#5112, #5219), memory tracker (#5100).
3. gow3_pc b646683: partial-height integer views reuse the cached image (image churn) — done
   (2026-10-18, `TextureCache::ResolveOverlap` + `THEORDER_PARTIAL_HEIGHT_VIEWS`).
4. VRAM on 4-8 GB cards (bloodborne_pc acfe3a8, 96502c8, 5f65e2d); textures over budget here
   (4.5 of 2.5 GB, nothing evicted; upstream GC part 2, #3396). In the merge: bloodborne_pc's
   time-based collector (20 s idle, 5 s under pressure), GPU-written tiled images written back,
   arena memory falling back to system memory and a VRAM reserve (5f65e2d).
5. **Command buffer pages shared with shader-written data** (item 3's main cost, found
   2026-10-18): The Order embeds two 192-byte shader-written blocks (0x106ea85360, 0x106ea85440)
   in its command buffers, beside the packets our Gnm driver writes and a fence the game polls.
   Each packet written there and each fence read downloaded a 512 KiB window after a full GPU
   wait: 39 writes and 14 reads per second, 0.42 s of each second in the rooftop scene (0.6.0).
   In the merge, a published copy-back settles a range (its pages leave the GPU-modified state)
   and watched ranges sharing an accessed page are promoted together: ~9 downloads per second,
   10-25 ms. Measured while another game used the GPU: confirm on an idle GPU.
6. MarkAsMaybeGpuDirty (upstream 0.20.1) hashed whole images on every use: 14% of the draw
   recording thread; the merge only flags them.

### R4. Launcher and features (from the sibling ports)
Controller diagrams and keyboard icons in game (gow3_pc #234, #471), Exit to Desktop (#462),
HiDPI and GUI scale (#584), window fitted to the screen (#470), screenshots/vibration/filtering
(#341), frame generation (bloodborne_pc 026f8ff), Discord presence (gow3_pc #217).

### R5. Coverage and cleanup
1. Saves: user_gameplay (ch. 1-2 rooftop), user_aim (street fight), user_ch9 (end of ch. 8). A
   run per chapter needs more saves (community saves are encrypted; chapter select after a save).
2. Remove what nothing uses (tools, env vars, docs of finished plans); check each launcher option
   reaches the game. tools/soak.sh, restart.sh, press.sh use pgrep and out/session.log (Linux era).
3. **Windows Vulkan tests** — done (2026-10-18): scene-resolution, taa-shader and camera-motion
   compile Vulkan-Hpp with the renderer's NDEBUG ABI, then enable their own assertions. The
   scene-resolution fixture uses the 0.20.1 SlotVector API. All three run in tools/run_tests.sh
   on Windows (11 scene, 29 TAA and 6 camera checks passed on RTX 5060 Laptop).

## GitHub issues (2026-10-17)
- #3 (01.00 eboot crashed at start): fixed in 0.3.3 (01.02-only hooks and patch sites); asked
  the reporter to test.
- #1 (Intel Arc, black/missing textures): 0.3.3 stops speculative SRT walks from patching the
  walker for good; not reproducible here, asked the reporter to test.
- #2 (Linux): answered, no Linux build yet; left open to track it.
- 0.3.8: #7/#9 (CUSA00785 stopped at the AppContent service label) fixed; #1/#10 (Intel black
  characters) VK_EXT_vertex_input_dynamic_state off on Intel; #4/#5 all 30 PS4 languages in the
  launcher. Asked to test. #8 (RTX 5070 Ti: black dots on faces, stretched black triangles from the
  rooftop's poles and wires; not seen here at 1080p): asked for the log and settings. #10 also: subtitles
  miss letters ("didn t"), MSAA on goes black (HD 630).
- 0.4.0: #8's black dots are probably NaN pixels spread by the depth of field (bokeh hexagons in the
  shadPS4 reports); GCN NaN rules ported (shadPS4 #5257/#5346), not reproduced here. #11 (RX Vega 64,
  chapter IX, ErrorOutOfHostMemory; the same in shadPS4 on that PC): the reporter's chapter IX save
  runs here; Check() now names the failing call, asked for a new log, RAM size and page file.
- 2026-10-18: #12 (CUSA00076: its code differs from 01.02, the 60 FPS patch is left out) answered;
  #14 waits for a second session after 0.5.0; #15 (0.4.1, "GTX", black screen, no log) unanswered.
- 2026-10-18 afternoon: #12's 01.00 log gave the sites: one signature takes rcx or rax now (cc55320).
  #15 is a GTX 1070 Ti with black characters (video): the barycentric path of #1/#10 (R1.9). #11's
  new log: vkEndCommandBuffer out of host memory on AMD (R2.2). #8 (RTX 3080, 0.6.0): log, video
  and a save (savedata/1/CUSA00035, rooftop); stretched polygons fixed by the merge's index buffer
  binding. #16 (Armoury outfits, DLC folders as add-ons) and #17 (language selection & other regions)
  resolved (2026-10-18 late night: SKU regional restriction bypass patch + Armoury menu unlock patch + CLI & INI language forwarding);
  #18 (RX 9070 flicker in chapter 2) resolved via U16 vertex stream alignment.

## 0c. Everything blurred while aiming — done (2026-10-18)
The CPU computes the focus from a GPU-written buffer that relaxed readbacks never brought back
(docs/PROGRESS.md 2026-10-18). Check: out/user_aim, L2 held from 45 s, the target sharp.

## 0d. White tutorial prompt band — todo (seen 2026-10-18)
The "hatching" over the street fight's buildings is the tutorial prompt's background (a dark brush
band with diagonal lines: the game's style). Once, when the player went down ("BLACKWATER"), the
band was white and had no text yet (out/aimdump/fix1 frame 38): a fade-in, or an Iggy color
transform/blend going wrong. Next: compare with PS4 footage of the Blackwater prompt; if wrong,
capture the frame and look at the Iggy draw's blend state and cxform constants.

## 0b. Blinking objects (draws skipped while pipelines build) — doing
0.4.1: stored keys jump the compile queue, more compiler threads (cold rooftop 100966 -> 34706
skipped draws). Left: ~20-80 truly new pipelines per warm session (garbage T#s in unused slots
make new permutations; shadPS4 PR #5218 "resource guards" is the approach), and restores past
the 8 ms budget (~5 ms each, 2.2 ms of it the 2 MB file read).
Night of 2026-10-17: hot_pipelines.txt restores the pipelines recent sessions used at start;
the particle lighting compute runs on a near permutation while a new one builds. Left: the first
session on a new PC or driver still compiles everything it meets (GitHub #14).

## 0. UI past the screen edges with an aspect ratio patch — done (2026-10-17 night)
The aspect patches give Iggy the original 2.40:1 height and center it with GDraw's tile origin
(docs/PROGRESS.md). Possible leftover: full-screen UI fades now cover the middle 2.40:1 band only.

## 1. GPU thread time per draw in heavy scenes — doing
The 60 FPS mode drops far below 60 in gameplay: the GPU command thread is the limit
(1430 draws x 14-17 us = 20-24 ms per frame of its own CPU time, plus waits).
- Stage B (port:DrawRec) is the limit in the 60 FPS mode. The mapped range check's lock was a
  fifth of it: per-thread copies (0.3.4) took the rooftop from ~57k to ~71k draws/s (~33 -> ~39 FPS).
- After 0.3.4 stage B is idle about half the time: the GPU command thread's waits (drains,
  item 3's Finish) set the pace; stage B's cost per draw still sets how long a drain waits.
- Image refreshes (THEORDER_FRAME_STATS "Image refreshes by bytes"): 0x1053e00000 (1728x1080 and
  864x540 at one address) is GPU-dirty ~110 times/s, ~800 MB/s of buffer-to-image copies on the
  GPU (written as a buffer, read as a texture); 0x1057eb0000 (1024x1024, GPU-modified and
  CPU-dirty) ~20 times/s has every mip hashed, ~650 MB/s of xxhash on stage B.
- Next: hash only the pages the CPU wrote (MaybeCpuDirty/CpuDirty refresh); look at why the
  1728x1080 target is written as a buffer.
- Check: us/draw and FPS on the rooftop, 60 FPS mode, interleaved runs.

## 2. Draw pipe drains at WaitRegMem — done (finding, 2026-10-17)
They wait for labels the compute queues write; the compute queues drain before every packet
anyway. Handing compute work to the recording thread removed the drains but lost ~15% (it moved
work onto stage B, the limit). Details: docs/PROGRESS.md 2026-10-17 (later).

## 3. GPU readback waits for the whole GPU — todo (cause found, 2026-10-17)
BufferCache::DownloadMemory -> Scheduler::Finish: ~20% of the GPU command thread on the rooftop
(60 FPS mode, 0.3.4). Once a frame, the CPU writes a 4 KiB page that holds two 192-byte storage
buffers a compute dispatch writes (0x106ea85360, 0x106ea85440) next to a compute ReleaseMem label
(+0x520) and the compute command buffer the CPU fills (+0x5a0, sceGnmDispatchInitDefaultHardwareState
and the eboot at 0xcc8791): the page is GPU-modified, so the write faults and a 512 KiB window is
downloaded after a Finish (~4.6 ms). The CPU never needs those bytes; the download only keeps the
page consistent (a later upload of the CPU-modified page would overwrite the GPU's bytes).
Without readbacks (THEORDER_READBACKS=0) the game loads the scene later with fewer draws: they are
needed elsewhere. Writing the label through the backing view (tried, 3+3 runs) only moved the download to
the next CPU write of the page (sceGnmDispatchInitDefaultHardwareState), no gain.
- Tried (2026-10-17, not kept): copies of small written bindings at each submission end (shadows)
  plus the compute label through the backing view. The command buffer page's downloads went from
  ~170 to ~2 ms/s, but the eboot's window 0x105b500000 then paid the wait (~250 ms/s): its 1024-byte
  binding is written again in the current, unsubmitted command buffer just before the CPU writes
  next to it ("stale"), so its data needs that submission. Total unchanged; and the label through
  the backing view hung the game (a GFX wait on 0x1066181700 saw an older value: the GPU's copy of
  the label stays old and a later download brings it back).
- Tried (2026-10-18, not kept): the byte-precise model. 2026-10-18 rooftop at 60 FPS: ~110 such
  downloads a second, ~0.4 s of waits a second; none of 6656 hit GPU-written bytes. Skipping the
  GPU's bytes at upload hung the boot: the CPU later rewrites 0x105b502000's ring buffer over the
  GPU's old bytes without faulting (the page was unprotected by an earlier write).
- Next: know which bytes the CPU writes after a fault (emulate the faulting stores, keeping the
  page protected for those pages only), or make the GPU's bytes reach memory without a full wait
  (copy them back right after the write and wait for that submission only).
- Tools: THEORDER_FRAME_STATS prints "Buffer downloads by time" (window, CPU writes, instruction)
  and "Write-fault downloads: N on GPU-written bytes, M beside them";
  THEORDER_GPU_WRITE_WATCH=<hex> prints the writable bindings in that address's 64 KiB.

## 4. Scene-load pauses (pipeline restores) — doing
~95 stored pipelines restore at the prologue start (260-390 ms), 30-pipeline bursts 100-180 ms;
about 4-5 ms each (file read ~1.3 ms, driver ~0.8 ms). Early restores from workers did not work
(their programs are not loaded yet).
0.3.5: pipelines with a permutation made this session build in the background (they were taken
as known and compiled on the GPU thread, 130-190 ms each, 4-8 a run). Left: 2-4 a run
(unskippable compute dispatches; stored keys whose index a later session reused), and the
permutation count itself (hundreds per material shader: image type and sRGB per texture).
0.3.6: past 8 ms of known-pipeline builds in a 33 ms window the rest build in the background:
the scene start burst is gone (prologue 67 ms, rooftop load 300-399 ms instead of 833 ms).
0.3.7: skippable compute dispatches always build in the background (no compute freeze left).
0.3.9: a known graphics pipeline only restores its stored binary on the GPU thread; a miss
(another session's permutation at that index) builds in the background. Prologue worst frames
99-119 ms, 0-1 slow synchronous compiles a run; rooftop none.
- Next: permutations identified by their specialization instead of the order they appear
  (cache format change), or fewer specializations (2D views of array images).

## 5. Rare startup fault in the task queue (0x20aa22) — closed (not reproduced, 2026-10-17)
Seen once in ~40 starts before 0.3.1; none in the ~100 batch runs of 2026-10-16/17. The fault
report prints the queue header if it returns.

## 6. One-frame black triangle — parked (no reproduction)
The engine's fullscreen vertex buffer is static; the cause is on the port's side.
- Next: a detector that compares the vertex data a draw read with guest memory.

## 7. Settings file leftovers from God of War — done (9b79d7f)
The in-game menu already rewrote the file without them; the launcher and run.py kept them.
Both writers now drop them (graphics_options.clean_lines).

## 8. `-startlevel` ignored — parked (testing convenience only)
Reaches the title screen even with the gate patched; see docs/ENGINE_RE.md.

## 9. Coverage — parked (needs saves or routes)
Most of the campaign, sound, AMD and Intel GPUs are unchecked (no AMD/Intel hardware here).
