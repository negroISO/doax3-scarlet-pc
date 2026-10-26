DOAX3 Scarlet — native Linux / Steam Deck candidate

This is a Linux x86-64 application using Vulkan directly. It does not use Proton,
Wine, frame generation or DLSS NR. Its native scene render remains 1920x1080 with
a 30 FPS cap; that cap is a target, not a verified Steam Deck performance result.

1. In Steam Deck Desktop Mode, extract the complete DOAX3-Scarlet archive.
2. Optional install: open a terminal in that folder and run: bash install.sh
   The default destination is ~/Applications/DOAX3-Scarlet.
   You can instead keep the extracted folder wherever you want.
3. Still in Desktop Mode, run Launch-DOAX3.sh once. Choose your merged Scarlet
   CUSA14173 version 1.08 game folder (eboot.bin, sce_module and sce_sys), then
   your sys_modules folder containing libSceFiber.sprx, ulobjmgr.sprx and
   libSceUlt.sprx. The selected locations are saved for subsequent launches.
   Those files and the game assets are not included in this application archive.
4. Add Launch-DOAX3.sh to Steam as a non-Steam game. Under Compatibility, leave
   "Force the use of a specific Steam Play compatibility tool" unchecked.
5. Choose Steam's Gamepad controller template. You can now switch to Gaming Mode
   and launch DOAX3 Scarlet from your Steam library.

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
