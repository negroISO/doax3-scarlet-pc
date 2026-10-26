#!/usr/bin/env bash
# Builds port_dlss.dll (gpu/dlss_bridge, the only code that uses the NVIDIA DLSS SDK) with
# MSVC and puts it with NVIDIA's nvngx_dlss.dll next to out/theorder-probe.exe; package.sh ships both.
# Needs Visual Studio 2022 (Build Tools) and a checkout of https://github.com/NVIDIA/DLSS:
#   DLSS_SDK_ROOT=/path/to/DLSS bash packaging/windows/build_dlss.sh
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
sdk=${DLSS_SDK_ROOT:?Set DLSS_SDK_ROOT to a checkout of github.com/NVIDIA/DLSS}
source ./msys2-env.sh
cmake=${CMAKE_EXE:-cmake}
# Only the Vulkan headers: MSYS2's C headers must not reach MSVC.
rm -rf out/vkinclude && mkdir -p out/vkinclude
headers=${VULKAN_INCLUDE:-$msys2_root/clang64/include}
[[ -f "$headers/vulkan/vulkan.h" && -d "$headers/vk_video" ]] || { echo 'Set VULKAN_INCLUDE to standalone Vulkan headers, or set MSYS2_ROOT.' >&2; exit 1; }
cp -r "$headers/vulkan" "$headers/vk_video" out/vkinclude/
"$cmake" -S gpu/dlss_bridge -B out/dlss-bridge -G "Visual Studio 17 2022" -A x64 \
    -DDLSS_SDK_ROOT="$(cygpath -m "$sdk")" -DVULKAN_INCLUDE="$(cygpath -m "$PWD/out/vkinclude")"
"$cmake" --build out/dlss-bridge --config Release
cp out/dlss-bridge/Release/port_dlss.dll "$sdk/lib/Windows_x86_64/rel/nvngx_dlss.dll" out/
cp "$sdk/LICENSE.txt" out/NVIDIA-DLSS-LICENSE.txt
echo "DLSS bridge ready: out/port_dlss.dll, out/nvngx_dlss.dll"
