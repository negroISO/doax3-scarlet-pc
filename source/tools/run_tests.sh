#!/usr/bin/env bash
# tools/run_tests.sh: builds and runs the unit tests (C/C++ targets of gpu/CMakeLists.txt and the
# Python suite) from the repository root. Windows: run inside MSYS2 CLANG64, e.g.
#   MSYSTEM=CLANG64 /c/msys64/usr/bin/bash.exe -lc 'cd /c/projetos/fork/theorder1886-port && bash tools/run_tests.sh'
set -uo pipefail
cd -- "$(dirname -- "$0")/.."
[[ -f msys2-env.sh && $(uname -s) == MINGW* || $(uname -s) == MSYS* ]] && source ./msys2-env.sh
tests=(mp3 videodec services pad-map audio-downmix cpu-check isa-emulate isa-stub tsc-clock
       pipeline-binary-cache pipeline-binary-initialization motion-history motion-shader buffer-addr64 buffer-inline png-decode
       aliasing blend-rewrite depth-stencil-state async-compiler warmup-inbox wave-stats pass-census
       frame-capture lighting-test-knob amd-paths no-msaa async-pipeline-cache async-fetch-snapshot
       ui-composition upscaler-support perf-overlay readback-queue wait-diagnostics
       graphics-settings readback-sources page-state scene-resolution taa-shader camera-motion dma-barriers
       shader-resource)
declare -A args=([isa-stub]=tests/data/isa_fixture.bin)
if ! ninja -C out/gpu "${tests[@]/%/-test}" > out/tests-build.log 2>&1; then
    grep -E 'error:|FAILED' out/tests-build.log | head -20; echo 'test build failed (out/tests-build.log)'; exit 1
fi
failed=()
for t in "${tests[@]}"; do
    if ! timeout 300 "out/gpu/$t-test" ${args[$t]:-} > "out/test-$t.log" 2>&1; then
        failed+=("$t"); tail -3 "out/test-$t.log"
    fi
done
if ! timeout 300 out/gpu/buffer-inline-test disabled > out/test-buffer-inline-off.log 2>&1; then
    failed+=(buffer-inline-off); tail -3 out/test-buffer-inline-off.log
fi
# Runtime (C) tests that build on their own.
if ! { clang -std=c11 -D_GNU_SOURCE -O2 -g -Wall -Wextra -Werror -I. -Isrc tests/test_content.c \
         src/runtime_content.c -o out/content-test && out/content-test && out/content-test --addons; } \
     > out/test-content.log 2>&1; then
    failed+=(content); tail -3 out/test-content.log
fi
# The launcher tests need Tk and a Windows home: a Windows Python (WINPYTHON, as package.sh).
python=${WINPYTHON:-}
for candidate in "${LOCALAPPDATA:-/c/Users/$USER/AppData/Local}"/Python/pythoncore-3*/python.exe \
                 "${LOCALAPPDATA:-/c/Users/$USER/AppData/Local}"/Programs/Python/Python3*/python.exe; do
    [[ -z $python && -x $candidate ]] && python=$candidate
done
[[ -z ${USERPROFILE:-} && -d /c/Users/${USERNAME:-$USER} ]] && export USERPROFILE="C:\\Users\\${USERNAME:-$USER}"
"${python:-python}" -m unittest discover -s tests > out/test-python.log 2>&1 || failed+=(python)
tail -1 out/test-python.log
if ((${#failed[@]})); then echo "FAILED: ${failed[*]}"; exit 1; fi
echo "all ${#tests[@]} C/C++ tests and the Python suite passed"
