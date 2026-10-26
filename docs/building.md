# Build the Windows runtime

Use the root Build.cmd and version-specific launchers. This repository is a
source export; it does not contain a prebuilt executable or game payload.

## Prerequisites

- Windows x64, Git, Python 3.11 or later, and an x86-64 Vulkan GPU/driver.
- MSYS2 **CLANG64**, normally C:\msys64. This is the compiler/runtime used by
  this project; Visual Studio is only needed for the optional neural bridge.
- Your extracted, plaintext game files and three separately supplied system
  modules. See [game-files.md](game-files.md).

In the MSYS2 CLANG64 shell, install the dependencies used by the inherited build:

```bash
pacman -S --needed git patch \
  mingw-w64-clang-x86_64-{toolchain,cmake,ninja,pkgconf,python} \
  mingw-w64-clang-x86_64-{vulkan-headers,vulkan-loader,vulkan-memory-allocator,glslang} \
  mingw-w64-clang-x86_64-{sdl3,fmt,boost,robin-map,xxhash,ffmpeg,zydis} \
  mingw-w64-clang-x86_64-{spirv-tools,spirv-headers,spirv-cross}
```

The recorded source dependencies are already vendored under source/. No recursive
submodule initialization or external FSR checkout is needed for this snapshot.
Some generated shader headers have long filenames. On Windows use a short
checkout path and `git -c core.longpaths=true clone <repository-url>`; spaces
are supported, but deeply nested paths may exceed Git/build-tool limits.

## Build

From Windows PowerShell in the repository root:

```powershell
.\Build.cmd -MSYS2Root C:\msys64 -Jobs 4
```

A different install location is supported. The wrapper configures Clang/Ninja,
RelWithDebInfo, no PGO and LTO off, then builds this edition's host plus the
GPU capability and play helpers. It also builds the librsvg loader stub.
Build files and outputs go under source/out/gpu/ and are ignored by Git.

The game executable is specified in project.json. Keep MSYS2's clang64/bin
available: this source build uses its SDL3/FFmpeg and other runtime DLLs. The
game launcher adds that directory to its own PATH from --msys2-root, MSYS2_ROOT
or the remembered local.json value; no system-wide PATH edit is required.

For systems without MOVBE/BMI1/LZCNT, the inherited optional instruction-rewrite
path needs iced-x86; that hardware path has not been validated by this export.
The smoke-test workstation supports the guest instructions directly.

## Optional neural adapters

Native rendering builds without the DLSS SDK or NVIDIA runtime DLLs.
See [neural-rendering.md](neural-rendering.md) for the separate MSVC bridge
build and exact directory layout.

## Native Linux / Steam Deck

The shared runtime includes native Linux build and packaging sources at
[source/packaging/steamdeck](../source/packaging/steamdeck/BUILD.md).
The previous Linux candidate was built on Ubuntu 22.04 with an isolated GCC 14
SDK; actual Steam Deck gameplay was not tested, and those archived binaries
predate the latest Fortune pool fixes. No old binary archive is included here.
A current native package must be rebuilt from this source and tested on the Deck.
That route uses Vulkan directly, with NR and frame generation disabled.

## Directory layout

| Path | Purpose |
|---|---|
| source/ | Runtime, translated graphics, vendored dependencies and original notices |
| tools/ | Portable Windows build/launch, extracted-game assembly and DLL staging |
| project.json | This repository's title ID, required update and host target |
| docs/ | Requirements, provenance, validation and selected screenshots |
| local.json | Remembered machine-local paths; generated and ignored |
| runtime/ | Generated linked game image, metadata and run logs; ignored |
| user/ | Saves, graphics settings and persistent shader/pipeline caches; ignored |
| game/, sys_modules/, sdk/ | Optional local input locations; ignored |

Do not use the inherited generic GUI/updater as the documented launch path.
The version-specific root launchers are the supported path for this export.
