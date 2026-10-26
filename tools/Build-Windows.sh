#!/usr/bin/env bash
set -euo pipefail
# The MSYS2 installation itself resolves to '/'. Keep '/.' so the environment
# helper never constructs a Windows UNC path such as '//clang64/bin'.
toolchain_root="$(/usr/bin/cygpath -u "$DOAX3_MSYS_ROOT")"
export MSYS2_ROOT="${toolchain_root%/}/."
cd "$(/usr/bin/cygpath -u "$DOAX3_REPO_ROOT")/source"
source ./msys2-env.sh
cmake -S gpu -B out/gpu -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTHEORDER_PGO=off -DTHEORDER_LTO=OFF -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja -C out/gpu "$DOAX3_BUILD_TARGET" theorder-gpu-capabilities theorder-play -j "$DOAX3_BUILD_JOBS"
clang -shared -O2 -Wall -Wextra -Werror packaging/windows/rsvg_stub.c -o out/gpu/librsvg-2-2.dll
