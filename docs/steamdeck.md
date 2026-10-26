# Native Steam Deck package

This is a Linux x86-64/Vulkan application for SteamOS, with the current async
shader, opaque-draw/pool, profiler and lighting-clear fixes. It uses native
rendering at 1920×1080 with a 30 FPS cap. **No Proton, frame generation or NR.**
Steam Deck graphics, performance and controls still require testing on hardware.

## Minimal setup

1. Switch to Desktop Mode once and extract the complete native archive.
2. Open `DOAX3-Scarlet/Install-DOAX3.desktop`. Allow/trust launching if KDE asks.
3. Check the detected game/module folders, or select them with Browse.
   Select **Install and add to Steam**.
4. Return to Gaming Mode and launch **DOAX3 Scarlet** from the Non-Steam library.

No terminal, sudo password, system package install or OS read-only-mode change
is required. A file copied through a Windows filesystem may need the desktop
launcher's **Properties → Permissions → Is executable** checkbox enabled once.
The installer restores the application permissions itself.

The installer briefly requests a normal Steam shutdown, waits for it to exit,
backs up its shortcut files and preserves unrelated games, then restarts Steam.
It refuses to modify shortcut files while Steam is running. Steam needs to
have been signed in at least once. Leave **Force compatibility tool** unchecked
and use the Gamepad controller template.

This one-time Desktop setup follows SteamOS's normal non-Steam application
workflow; later launches use Gaming Mode. See
[Valve's Desktop FAQ](https://help.steampowered.com/en/faqs/view/671A-4453-E8D2-323C/).

## Transfer your game data

Public app archives contain no PS4 games, DLC, firmware, saves or shader caches.
Use your own extracted matching base+update and the
[three required system modules](game-files.md).
The installer detects this layout without typing paths:

```text
transfer-folder/
  DOAX3-Scarlet/             extracted native app
  game/CUSA14173/          matching merged base/update; optional addcont inside
  sys_modules/              libSceFiber.sprx, ulobjmgr.sprx, libSceUlt.sprx
```

Both editions can share the transfer folder and module input folder. Their
apps, Steam entries and user state remain separate. The three modules are
copied into the installed application. **Game assets stay in the selected
folder**, so keep that folder or storage drive connected. You can move the
transfer folder to permanent internal/SD storage before installing.

Default application location: `~/Applications/DOAX3-Scarlet`.
Default saves/settings/cache/logs: `~/.local/share/doax3-scarlet/`.
Existing app directories are refused rather than overwritten. For an update,
choose a new application parent/folder; the separate savedata stays in place.
Transfer Windows savedata separately while the game is closed. Rebuild shader
caches on the Deck rather than transferring Windows/GPU-specific caches.

## Packaging and validation

The portable folder plus graphical installer avoids requiring AppImage/FUSE or
a Flatpak sandbox. Both TAR.GZ and ZIP artifacts are generated. Prefer TAR.GZ
when transferring through Linux because it preserves executable permissions.
An archive is not a Valve store installation or a Deck Verified certification.

Source/build details: [native packaging instructions](../source/packaging/steamdeck/BUILD.md).
Each app includes `share/source-version.json`, dependency locks, notices,
`LICENSE` and `SOURCES.txt`. The source revision links to the corresponding
public repository snapshot. Build-machine paths are removed from compiled
source-location strings and proprietary inputs are rejected during bundling.
