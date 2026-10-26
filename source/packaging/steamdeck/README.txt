DOAX3 Scarlet — native Linux / Steam Deck candidate

This is a Linux x86-64 application using Vulkan directly. It does not use Proton,
Wine, frame generation or DLSS NR. Its native scene render remains 1920x1080 with
a 30 FPS cap; that cap is a target, not a verified Steam Deck performance result.

ONE-TIME SETUP — no terminal commands or root password
1. Switch to Steam Deck Desktop Mode and extract the complete archive.
2. Open DOAX3-Scarlet/Install-DOAX3.desktop. Allow/trust launching if KDE asks.
   If copied through a Windows filesystem, Properties > Permissions > Is executable
   may need enabling for this desktop launcher. The installer fixes app permissions.
3. Check the detected folders (or use Browse) and select Install and add to Steam.
   The installer closes Steam briefly, prepares the game, adds the native shortcut
   with a backup of existing entries, then reopens Steam. Existing saves are kept.
4. Return to Gaming Mode and launch DOAX3 Scarlet from the Non-Steam library.
   Use the Gamepad controller template. Leave Force compatibility tool unchecked.

For automatic folder detection, transfer this layout together:
  DOAX3-Scarlet/             extracted application package
  game/CUSA14173/            your merged base + update 1.08, including optional addcont
  sys_modules/              libSceFiber.sprx, ulobjmgr.sprx, libSceUlt.sprx
The app and game/ and sys_modules/ folders are siblings. The system modules are
copied into the installed app. Game data stays where you selected it: keep that
folder or storage drive connected. No game data/modules are in the public archive.
The default app destination is ~/Applications/DOAX3-Scarlet. If it already exists,
choose a new parent folder; existing application directories are never overwritten.

Stock SteamOS needs this one-time Desktop setup. Subsequent launches need no
Desktop Mode or typing. The portable folder can also launch via Launch-DOAX3.sh.

Controls: use B / Circle to confirm in this region of the game; Menu / Options
skips the opening movie. Keyboard equivalents: Left Shift / Escape = Circle,
Space / Enter = Cross, F8 = Options. Mouse/keyboard are available in Desktop Mode.

Settings, saves, shader caches and logs are separate from the application:
  ~/.local/share/doax3-scarlet/
  locations.json   selected game/module folders
  user/            savedata, persistent cache and graphics.ini
  logs/            each run's stdout, stderr and result.json
Removing the application folder does not remove these files. Do not copy your
Windows shader cache; let this device create its own. Existing Windows savedata
can be copied separately into user/savedata while the application is closed.

To change the paths without a dialog:
  ./Launch-DOAX3.sh --game-dir /path/to/CUSA14173-v108 --modules-dir /path/to/sys_modules
Diagnostics:
  ./Launch-DOAX3.sh --self-check

This package is a candidate, not Deck Verified. The Deck's AMD graphics path,
controller handling and performance require testing on the actual device.
Do not infer Deck results from the Windows/RTX 5090 playtests.
Original port attribution and licensing are included in LICENSE and SOURCES.txt.
Exact source revision and included fixes: share/source-version.json.
