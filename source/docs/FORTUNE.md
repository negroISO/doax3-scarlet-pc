# Fortune 1.19 candidate

CUSA04555 version 01.19 uses the shared native runtime and renderer fixes used
by Scarlet. The Windows `doax3-fortune` CMake target shares `theordergpu` and
retains a separate product title, version resource and application icon.

Install the merged base/update assets outside the application. Keep extracted
DLC folders under `GAME/addcont/LABEL`, where LABEL is the 16-character suffix
of that pack's CONTENT_ID. The candidate launcher sets THEORDER_ADDON_DIR to
this directory. AppContent accepts up to 256 distinct labels and the VFS holds
512 simultaneous mounts. Overflow fails instead of presenting partial content.

Regression checks: content-test (default, --addons, --many-addons), the real
73-mount/read file-mods test on Linux, doax3-camera-test, and production
camera-motion-test. Windows playtests confirm all 73 actual labels mount,
island/menu and character scenes render, and save data is created/reloaded.
The offline PSN shop-data notice is dismissible. No online service is supplied.

The Fortune camera fixture is from a real 1.19 G-buffer capture: four scene
MRTs, 432-byte view/projection block, reversed D32S8 depth and final copy shader
PS 5921d2a8, matching the measured Scarlet path. NR is opt-in, uses the native
motion/depth adapter and does not enable frame generation. History resets stay
at that display-copy boundary. Transparent/effect motion coverage is incomplete.

For the native Steam Deck bundle, see packaging/steamdeck/BUILD.md and use
bundle.py --game fortune with its generated content.bin and empty patches.bin.
The package keeps separate Fortune settings/saves/cache and discovers DLC from
GAME/addcont. NR is disabled in the Linux/AMD candidate. Build-host checks do
not establish actual Deck gameplay or performance.
