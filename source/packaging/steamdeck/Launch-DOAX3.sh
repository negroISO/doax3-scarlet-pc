#!/usr/bin/env bash
set -euo pipefail
app_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ -z ${FONTCONFIG_PATH:-} && -d /etc/fonts ]]; then
    export FONTCONFIG_PATH=/etc/fonts
fi
exec "$app_dir/doax3-launcher" "$@"
