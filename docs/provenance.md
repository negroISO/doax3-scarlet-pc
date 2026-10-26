# Provenance and acknowledgements

The shared source was exported from the validated local DOAX3 working tree at
commit a4aa8986f27fc71e81d015886113ef0b0d8e9c3e,2026-10-26.
Its starting port baseline was The Order: 1886 v0.7.2 / d96794e.
This is a **fresh source snapshot**, not a preservation of upstream Git history.
Original copyright/license notices and archived upstream documentation remain.

The local VideoOut driver edit and six FSR working-tree edits already present in
the tested builds are included in the snapshot. No changes were discarded from
the original development checkout. docs/source-manifest.json records the source
archive identity and pre-export working-tree file hashes. Git checkout line
ending normalization can change byte hashes without changing source semantics.
The new root launch/build/assembly tools are export-specific additions.

Later portability cleanup removes development-machine paths from scripts and
historical notes. The manifest above remains a record of the initial export,
not a claim that later tracked files still have their original byte hashes.
The public snapshot includes those subsequent changes without the earlier
development commit history. See [portability.md](portability.md).

## Work used directly

| Project/person | Contribution or relationship |
|---|---|
| [mateussmeneses/theorder1886-port](https://github.com/mateussmeneses/theorder1886-port) | Immediate runtime/loader/graphics starting point for DOAX3 |
| [Supermedo/uncharted_pc](https://github.com/Supermedo/uncharted_pc) | Nathan Drake Collection reference project, shared local Windows toolchain and earlier integration work consulted alongside 1886; not a claim that DOAX3 is its direct Git fork |
| [shadPS4](https://github.com/shadps4-emu/shadPS4) | Embedded PS4 GPU command and shader-to-Vulkan core |
| [IFreemz / eFMann bridge](https://github.com/eFMann/shadPS4-Bloodborne-DLSS-FSR) | Source provenance of the native DLSS bridge; original IFreemz MIT notice retained in source/gpu/dlss_bridge/LICENSE.txt |
| VamDlssNr local integration | User-supplied compatible NR runtime pair and earlier VAM/Uncharted/1886 integration reference; a separate upstream source-repository URL was not recorded, so none is invented |
| [Mesa AMD Vulkan code](https://chromium.googlesource.com/chromiumos/third_party/mesa/+/a11d1bd247df18057ecb9945e01bf39ff85d8d9d/src/amd/vulkan/radv_cmd_buffer.c) | Opaque draw packet convention consulted during the Fortune pool command-buffer investigation |

## Earlier inherited lineage

The 1886 source's own credits trace the runtime further through:

1. [Felipe Prado's God of War III Remastered port](https://github.com/FelipePrado0/SpartanPort)
   (formerly gow3_pc).
2. [Supermedo's Bloodborne Windows fork](https://github.com/Supermedo/bloodborne_pc)
   and [deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc).
3. [cuesta4/shadPS4, eltutz branch](https://github.com/cuesta4/shadPS4/tree/eltutz),
   for earlier GNM blend, depth/stencil and image-aliasing work.

These are inherited ancestry/credits, not a statement that this session ported
each of those games or directly authored their rendering fixes.

## Vendored submodule revisions

The export contains source files rather than unresolved Git submodule pointers.

| Dependency | Recorded revision |
|---|---|
| [FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan) |c64f093404125e960813f95f1996c14780f0dd69 plus recorded local changes|
| [Dear ImGui](https://github.com/ocornut/imgui) |f1cc2ae15e53a861a874c3034aae6798fde194ab|
| [magic_enum](https://github.com/Neargye/magic_enum) |e046b69a3736d314fad813e159b1c192eaef92cd|
| [miniz](https://github.com/richgel999/miniz) |2948eebafc5b9f1c69bebed80333e403a09bd62d|
| [xbyak](https://github.com/herumi/xbyak) |cd6bad3cad2e07a3e277c23d2b1f065320024717|
| [LibAtrac9](https://github.com/Thealexbarney/LibAtrac9) |efca2e3af35562a09a9bb6deed90e45b4b824dc4|

Additional retained/runtime dependencies include
[SDL3](https://github.com/libsdl-org/SDL),
[FFmpeg](https://github.com/FFmpeg/FFmpeg),
[sirit](https://github.com/shadps4-emu/sirit),
[Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator),
[Khronos Vulkan](https://github.com/KhronosGroup/Vulkan-Headers),
[SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools),
[Zydis](https://github.com/zyantific/zydis),
[fmt](https://github.com/fmtlib/fmt),
[Boost](https://github.com/boostorg/boost),
[robin-map](https://github.com/Tessil/robin-map),
[xxHash](https://github.com/Cyan4973/xxHash), AMD FidelityFX and the separately
licensed [NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS).
Per-file notices govern these components; the main inherited runtime is GPLv2.

DOAX3 code added during bring-up covers import/NID/ABI fixes, native Fiber/Ult
loading, async draw retention, permutation keys, camera/object-motion guides,
offline content mounting, branding/native packaging, the Fortune opaque-draw
fix and profiler/lighting-clear fixes. It does not establish complete game
compatibility. Team NINJA / Koei Tecmo retain rights in the original games,
artwork and the in-game content shown in the screenshots.
