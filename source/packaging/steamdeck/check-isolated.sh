#!/usr/bin/env bash
# Run in a new unprivileged mount namespace; hides the build SDK from the app.
set -euo pipefail
workspace=${DOAX3_BUILD_ROOT:?Set DOAX3_BUILD_ROOT to your isolated build workspace; see BUILD.md}
mount --make-rprivate /
mount -t tmpfs tmpfs "$workspace/sdk"
mount -t tmpfs tmpfs "$workspace/prefix"
export PATH=/usr/bin:/bin
unset CONDA_PREFIX CONDA_DEFAULT_ENV PYTHONHOME PYTHONPATH LD_LIBRARY_PATH
app=${DOAX3_APP_DIR:-$workspace/release/DOAX3-Scarlet}
"$app/Launch-DOAX3.sh" --self-check
LD_LIBRARY_PATH="$app/lib" "$app/bin/theorder-gpu-capabilities" --list
LD_LIBRARY_PATH="$app/lib" "$app/bin/theorder-probe" --vulkan-only
