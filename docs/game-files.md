# Required game data: Scarlet

The tested profile is **CUSA14173 / 01.08**. A matching base game **plus**
that update is required; a base-only dump and another edition's files are not
interchangeable. Optional DLC is not required for the build or normal launch.

## Recorded local package inputs

| Role | Recorded package filename | Version / identity |
|---|---|---|
| Base |hr-doax3scarlet.pkg|01.00, CATEGORY gd, CUSA14173|
| Update |hr-doax3scarletv108.pkg|01.08, CATEGORY gp, JP0106-CUSA14173_00-ISLANDGAMEAPP000|

Update package SHA256:
01b5f6adc2a2d79599d7496446b0a3925196996b2a8f417caf07f1bc6071ee2b.
Tested merged eboot.bin SHA256:
bfa30d5cdbe22f919a6e4f5589b94ed19bd20cb7ffa0addfdda84bfb0738b88e.
The base package was 19486867456bytes and the update 259850240bytes; a full base
package hash is not asserted here. The update overlaid 37 extracted files plus
public metadata onto the 8075-file base view.

No exhaustive Scarlet DLC pack set was validated. Matching optional DLC can be
mounted from addcont/, but Fortune DLC is not a substitute.

Filenames are local provenance labels, not a requirement to obtain a particular
third-party repack. No game download links or package payloads are provided.
A lawful equivalent dump must match the required title/version and supported
plaintext executable layout.

## Assemble already extracted files

Extract the base and update separately using your own lawful dump/extraction
workflow. Give the helper the roots that contain eboot.bin and sce_sys/param.sfo
(for extraction tools with a uroot/ directory, pass that uroot directory).

```powershell
python tools/Merge-Game.py --base "./inputs/base/uroot" --update "./inputs/update/uroot" --output "./game/CUSA14173"
```

The output must be a new folder. The helper checks title/category/update
metadata, copies the full base tree and overlays the matching update. Preserve
all numbered chunk directories, archives, executables and sce_sys metadata;
mixing one edition's executable with the other edition's assets caused earlier
boot/rendering failures.

For optional matching add-ons, add --dlc followed by each extracted DLC root.
Each pack is placed under addcont/ plus its16-character CONTENT_ID suffix.
Example final layout:

```text
CUSA14173-merged/
  eboot.bin
  sce_sys/param.sfo           # TITLE_ID=CUSA14173; APP_VER=01.08
  sce_module/libc.prx
  sce_module/libSceFios2.prx
  00/ ...                    # preserve all other original chunk directories
  addcont/                   # optional
    CONTENT_LABEL_16/
      sce_sys/param.sfo
      ...original DLC files...
```

Supply libSceFiber.sprx, ulobjmgr.sprx and libSceUlt.sprx separately under
sys_modules/, or pass --modules-dir. The repository does not extract/decrypt
firmware or packages and does not include these modules.

The launcher's offline full-SKU profile is a runtime metadata setting, not a
license or entitlement grant. Installing DLC files does not necessarily unlock
progression-gated/unowned costumes or restore PSN services.
