# Native Linux / Steam Deck package

The earlier candidate was built on Ubuntu 22.04 x86-64. This repository already
vendors the source dependencies and local FSR changes; no private server,
snapshot helper or other game-port checkout is needed.

Set `DOAX3_BUILD_ROOT` explicitly to a workspace you own. No system packages or
shell initialization files are changed. `environment.yml` describes the
conda-forge SDK: GCC14, CMake3.31, SDL3.2, FFmpeg8, fmt11, Boost1.88 and development
dependencies. The final package's `share/sdk-explicit.txt` locks exact packages.

Dependencies outside the SDK:

- Khronos Vulkan-Headers v1.4.357: `e3b1eec08173d6b825cd3ac88c885a63b621504a`.
- Khronos Vulkan-Loader v1.4.357: `5f157b62e333c63260d05d81bf66faa216ab0fb8`.
- Zydis v4.1.1: `a2278f1d254e492f6a6b39f6cb5d1f5d515659dc`.
- Its Zycore submodule: `0b2432ced0884fd152b471d97ecf0258ff4d859f`.

Place these public checkouts at the recorded revisions under
`$DOAX3_BUILD_ROOT/deps/Vulkan-Headers`, `deps/Vulkan-Loader` and `deps/zydis`.
Initialize the Zydis submodules as well. From a fresh repository checkout,
stage its source and create the SDK (Micromamba and Git must be installed):

```bash
export DOAX3_BUILD_ROOT="$PWD/.native-build"
mkdir -p "$DOAX3_BUILD_ROOT/deps"
cp -a source "$DOAX3_BUILD_ROOT/source"
micromamba create -y -p "$DOAX3_BUILD_ROOT/sdk" -f source/packaging/steamdeck/environment.yml
# Populate deps/ with the public dependency checkouts listed above.
micromamba run -p "$DOAX3_BUILD_ROOT/sdk" bash "$DOAX3_BUILD_ROOT/source/packaging/steamdeck/build-linux.sh"
```

Use a new workspace when staging again; do not copy an existing Windows build
or private configuration into it. The scripts have no machine-specific fallback.
Build concurrency
is three jobs; CPU flags remain generic x86-64. Vulkan/SDL libraries come from
the isolated prefix for building. Device drivers are never bundled.

`bundle.py --workspace WORKSPACE --profiles PROFILE_DIR --output OUTPUT_DIR --source-revision GIT_SHA`
freezes `launcher.py`, strips native binaries, bundles dependencies and rewrites
RPATHs. PROFILE_DIR holds the validated generated `content.bin` and empty
`patches.bin`, not a game executable. It emits TAR.GZ, ZIP and dependency/hash
reports. Use a fresh output directory; existing apps are not overwritten.

The archive includes a frozen graphical installer and `Install-DOAX3.desktop`.
It performs a per-user copy, prepares the supplied game, backs up/adds native
Steam shortcuts only while Steam is closed, and restarts Steam afterward.
No root or system package installation is needed on the Deck. Source revisions
must be complete published commit hashes; source URLs and dependency notices
are included in the package. Build-machine source paths are prefix-mapped.

For Fortune 1.19 use `--game fortune` with its generated full-SKU content profile
and empty patches. The separate app is DOAX3-Fortune, with the Fortune icon,
title/version checks and `~/.local/share/doax3-fortune` state. Optional DLC is
discovered under the selected game's `addcont` directory. Scarlet remains the
default profile; existing Scarlet archives and saves are preserved.
Use `verify-package.py WORKSPACE --release OUTPUT_DIR --game Fortune` to check it.

`verify-package.py WORKSPACE` checks WORKSPACE/release: installation in a path
with spaces, desktop entry, frozen CPU/Tcl startup, preservation/refusal checks,
ZIP CRCs, every TAR payload hash and maximum required GLIBC symbol. The candidate
requires at most GLIBC2.34, below the build host's GLIBC2.35.

`check-isolated.sh` runs inside an unprivileged mount namespace, with SDK and
build-prefix directories hidden. It verifies frozen startup, device enumeration
and a real Vulkan command-submission/readback check using host graphics drivers.
It is a build-host check, not an installer to run on the Deck.

Additional checks: AppContent C test, DOAX3 camera parser, motion-history test and
eleven production GPU camera-motion cases pass on Linux. The frozen launcher
prepares the actual 1.08 image with SHA256
`40b0ed022232c6f462fcd12d3a49b92a293ccf60fe5580d070443791940be9a7`, matching
Windows. A CPU-only guest-entry check ran; no full Linux gameplay session or
actual Steam Deck test is claimed. Windows rebuild and motion checks still pass.

The native target keeps the original 1920x1080 scene render and 30 FPS pacing.
The Deck's 1280x800 display does not automatically reduce guest rendering cost.
Deck-specific performance, AMD rendering and controller behavior need device
testing. User game/module files and saved games are not in the app archives.
