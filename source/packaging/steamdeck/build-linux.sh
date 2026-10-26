#!/usr/bin/env bash
# Run inside the isolated Linux SDK environment; never changes system packages.
set -euo pipefail
workspace=${DOAX3_BUILD_ROOT:?Set DOAX3_BUILD_ROOT to your isolated workspace; see BUILD.md}
[[ -d "$workspace/source/gpu" ]] || { echo 'DOAX3_BUILD_ROOT/source must contain this repository source/ tree.' >&2; exit 1; }
sdk=${CONDA_PREFIX:?Run with micromamba run -p the isolated sdk directory}
prefix=$workspace/prefix
mkdir -p "$prefix" "$workspace/build"
export CMAKE_PREFIX_PATH="$prefix:$sdk${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig:$prefix/share/pkgconfig:$sdk/lib/pkgconfig:$sdk/share/pkgconfig"
export LD_LIBRARY_PATH="$sdk/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$prefix/bin:$PATH"
pkg-config --print-errors --exists x11 xrandr sdl3 wayland-client

cmake -S "$workspace/deps/Vulkan-Headers" -B "$workspace/build/vulkan-headers" -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DVULKAN_HEADERS_ENABLE_TESTS=OFF
cmake --install "$workspace/build/vulkan-headers"
cmake -S "$workspace/deps/Vulkan-Loader" -B "$workspace/build/vulkan-loader" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DVULKAN_HEADERS_INSTALL_DIR="$prefix" -DBUILD_TESTS=OFF \
    -DBUILD_WSI_XCB_SUPPORT=OFF -DBUILD_WSI_XLIB_SUPPORT=ON -DBUILD_WSI_WAYLAND_SUPPORT=ON
cmake --build "$workspace/build/vulkan-loader" -j3
cmake --install "$workspace/build/vulkan-loader"
cmake -S "$workspace/deps/zydis/dependencies/zycore" -B "$workspace/build/zycore" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$workspace/build/zycore" -j3
cmake --install "$workspace/build/zycore"
cmake -S "$workspace/deps/zydis" -B "$workspace/build/zydis" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DZYAN_SYSTEM_ZYCORE=OFF -DZYDIS_BUILD_TOOLS=OFF -DZYDIS_BUILD_EXAMPLES=OFF -DZYDIS_BUILD_TESTS=OFF
cmake --build "$workspace/build/zydis" -j3
cmake --install "$workspace/build/zydis"

cd "$workspace/source"
export THEORDER_LTO=OFF THEORDER_PGO=off THEORDER_BUILD_JOBS=3
# Keep build-machine directory names out of compiled source-location strings.
export CFLAGS="${CFLAGS:+$CFLAGS }-ffile-prefix-map=$workspace=/usr/src/doax3"
export CXXFLAGS="${CXXFLAGS:+$CXXFLAGS }-ffile-prefix-map=$workspace=/usr/src/doax3"
# build.sh applies the source's FSR patches before configuring and uses the
# portable CPU flags supplied by the SDK. One configure avoids duplicate builds.
bash build.sh
