# Optional native neural rendering / “DLSS5 mod” path

This is the experimental NGX Feature 18 path tested with the VamDlssNr
runtime files. It supplies native 1920×1080 color, calibrated depth and
camera/object-motion guides. It does **not** enable frame generation or claim
that every arbitrary NVIDIA DLL exposes the same interface.

Native rendering does not require any of these DLLs. Start with the normal
launcher if you do not have the compatible optional runtime or NVIDIA hardware.
Validation was on RTX 5090 / driver621.1; compatibility and cost on other GPUs
have not been established. Measured NR+guide work in Fortune's pool was about
3.45 GPU milliseconds/frame, even while presentation held the30 FPS cap.

## Required layout on Windows

Place the DLLs **beside this edition's executable in source/out/gpu/**:

```text
source/out/gpu/
  doax3-fortune.exe OR theorder-probe.exe
  port_dlss.dll
  nvngx_dlss.dll
  nvngx_dlssnr.dll
  nvngx.dll/                  <-- a DIRECTORY, despite the .dll suffix
    port_nr.dll
```

port_dlss.dll and port_nr.dll are this project's native adapters, built from
source/gpu/dlss_bridge/. nvngx_dlss.dll and nvngx_dlssnr.dll are separately
supplied proprietary runtime files. **Do not replace nvngx.dll/ with a DLL file.**
Do not install the VAM BepInEx/Unity plugin into this native application.

VamDlssNr/ngx was the compatibility reference, not a required installation path.
Supply compatible DLLs from any directory. The recorded runtime hashes:

| File | SHA256 |
|---|---|
| nvngx_dlss.dll |2ecf41de71383416a5fb1476d26d495fc391fdc254d6e5a27689b598b2d05702|
| nvngx_dlssnr.dll |8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206|

Different DLLs are unvalidated and may omit the required Feature 18 exports/ABI.
The seven-argument VULKAN_Init_Ext contract is retained; changing exports or
renaming unrelated DLLs does not provide compatibility.

## Build and stage the adapters

Install Visual Studio 2022 C++ Build Tools and obtain the
[NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS) separately under its own license.
VULKAN_INCLUDE must point at standalone Vulkan headers containing vulkan/vulkan.h.
Avoid adding the whole MSYS2 include directory to an MSVC build: its C library
headers can shadow the MSVC headers.

```powershell
git clone https://github.com/NVIDIA/DLSS.git sdk/DLSS
git -C sdk/DLSS checkout 9981c6350dc58c0ce21f024fd65e31e851c31689
git clone https://github.com/KhronosGroup/Vulkan-Headers.git sdk/Vulkan-Headers
cmake -S source/gpu/dlss_bridge -B source/out/dlss-bridge -G "Visual Studio 17 2022" -A x64 -DDLSS_SDK_ROOT="$PWD/sdk/DLSS" -DVULKAN_INCLUDE="$PWD/sdk/Vulkan-Headers/include"
cmake --build source/out/dlss-bridge --config Release
python tools/Stage-Neural.py --runtime-dir "./inputs/neural-runtime"
```

The staging helper copies the two newly built adapters and the two user-supplied
runtime DLLs to the layout above. It records local hashes and warns if the donor
runtime differs from the validated pair. It does not download or distribute
NVIDIA files. Run the edition's -NR.cmd launcher after staging; --nr-strength 0.5
reduces the effect, and the normal launcher remains the native-rendering path.

No proprietary DLL, DLSS SDK library, license key or BepInEx payload is committed.

The SDK revision above is the recorded checkout used for the tested bridge.
Keep its original NVIDIA license and obtain it directly from its upstream.
