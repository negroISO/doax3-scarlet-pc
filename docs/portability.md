# Fresh checkout and portability

The supported Windows path is the root `Build.cmd` followed by
`Launch-DOAX3-Scarlet.cmd`. Everything it imports comes from this checkout
or explicitly supplied inputs. Existing saves, shader caches, linked images,
developer settings and another game-port checkout are not required.

## Inputs a new machine still needs

| Input | Required? | Supplied by |
|---|---|---|
| Git, Windows x64 Python 3.11+, MSYS2 CLANG64 packages | Yes, to build | Install the [documented build tools](building.md) |
| Vulkan-capable GPU and driver | Yes, to run | The target PC |
| Matching CUSA14173 base and required update | Yes | Your own extracted plaintext game dump; see [game-files.md](game-files.md) |
| libSceFiber.sprx, ulobjmgr.sprx, libSceUlt.sprx | Yes | Separately supplied compatible PS4 system modules |
| Matching extracted DLC | Optional | Your own packs under game/addcont |
| DLSS SDK, MSVC and compatible NR DLLs | Only for NR | [Separate neural setup](neural-rendering.md) |

**The repository plus encrypted PKGs alone is insufficient.** Package
extraction/decryption is outside the included tools. The three system modules
are separate inputs even after base/update extraction. The native launcher does
not require NVIDIA DLLs, a VAM installation or any network service.

## Repeatable setup

1. Clone into a writable directory; spaces are supported. Use a short checkout
   path on Windows and `git -c core.longpaths=true clone <repository-url>`
   because some vendored shader filenames are long. Deeply nested paths can
   exceed the path limits of Git or build tools.
2. Install dependencies and run `Build.cmd -MSYS2Root <your-install> -Jobs 4`.
   `C:\msys64` is a conventional default, not a required location.
3. Extract base/update separately, then run `tools/Merge-Game.py` as documented.
   The default final game folder is `game/CUSA14173/`.
4. Put the three system modules in `sys_modules/`, or supply `--modules-dir`.
5. Run `Launch-DOAX3-Scarlet.cmd --check` to verify the host and game metadata.
6. Run `Launch-DOAX3-Scarlet.cmd --prepare-only` to regenerate the linked image
   and content profile, then launch normally. Generated state stays in ignored
   `local.json`, `runtime/` and `user/`.

All input locations can be overridden explicitly. Do not transfer another
machine's `local.json` or CMake output/cache directories to a fresh checkout.
The launcher builds its DLL search path from the chosen MSYS2 installation and
the local executable directory. Source dependencies are vendored.

## What was cleaned up

- Removed the older runner's implicit developer game-directory search.
- Legacy timed/batch diagnostics now require an explicit `THEORDER_GAME_DIR`
  and reject missing inputs before starting work.
- Linux SDK scripts require `DOAX3_BUILD_ROOT` explicitly; their setup no
  longer names a private build host or storage mount.
- Diagnostic symbolizers honor `LLVM_SYMBOLIZER`, PATH and `MSYS2_ROOT`.
  The legacy DLSS build helper also honors `VULKAN_INCLUDE`/`MSYS2_ROOT`.
- Historical notes use neutral directory placeholders. Public upstream
  contributor names, copyright notices and repository URLs remain as attribution.
- The repository audit rejects private-network references, fixed developer home
  directories and private project/storage paths, as well as credentials/payloads.

## Verification scope

The accompanying [portability validation record](portability-validation.json)
states exactly which checks were run. A relocated source/build/startup check
on the development OS is not an independent clean Windows VM installation test.
Linux scripts receive syntax/input checks here; native Steam Deck gameplay
still requires actual device testing.

To repeat the static publishing checks from a Git checkout:

```powershell
python -B tools/Audit-Repository.py
```

The audit covers tracked files, including historical notes and comparison
metadata. Generated local configuration and binary debug information can contain
the paths of the machine that creates them; these are excluded from Git.
This public repository begins with a sanitized source snapshot. Earlier
development commits are excluded from its history. Public upstream authorship,
copyright notices and license terms remain intact.
