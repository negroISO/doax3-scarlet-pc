# DOAX3 Scarlet on Windows

The local validated entry points are `Launch-DOAX3-Scarlet-Candidate.cmd` and
`Launch-DOAX3-Scarlet-NR.cmd` in the parent project folder. See `SETUP.md` there
for game paths, runtime staging, saves and test evidence.

`package.sh` creates `dist/doax3-scarlet-windows` and its ZIP with the optional
configurable GUI `DOAX3 Scarlet.exe` and `Play DOAX3 Scarlet.exe`. It requires a
current build plus Windows Python/PyInstaller. Packaging is a separate workflow;
the project launchers use `out/gpu` directly. Optional NR runtime staging is
handled by the project tools, not this generic package script.

The executable resource files embed `launcher/doax3.ico` and DOAX3 product
metadata. Internal build target names stay compatible with the runtime.
Historical upstream packaging instructions are in `docs/upstream/windows`.
