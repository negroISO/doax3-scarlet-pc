#!/usr/bin/env bash
# Optional per-user install; game data and saved games are never copied or replaced.
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ ${1:-} == --gui ]]; then
    chmod +x "$source_dir/doax3-launcher" "$source_dir/bin/theorder-probe" "$source_dir/Launch-DOAX3.sh"
    exec "$source_dir/Launch-DOAX3.sh" --install
fi
destination=${1:-$HOME/Applications/DOAX3-Scarlet}
destination=$(realpath -m -- "$destination")
if [[ "$destination" == "$source_dir" || "$destination" == "$source_dir/"* ]]; then
    echo 'Choose an install folder outside the extracted package.' >&2
    exit 1
fi
if [[ ! -f "$source_dir/doax3-launcher" || ! -f "$source_dir/bin/theorder-probe" ]]; then
    echo 'Extract the complete DOAX3-Scarlet archive before installing.' >&2
    exit 1
fi
if [[ -e "$destination" ]]; then
    echo "Install folder already exists: $destination. Choose a new folder; your existing install is preserved." >&2
    exit 1
fi
mkdir -p -- "$(dirname -- "$destination")"
cp -a -- "$source_dir" "$destination"
chmod +x "$destination/Launch-DOAX3.sh" "$destination/doax3-launcher" "$destination/bin/theorder-probe"
desktop_dir=${XDG_DATA_HOME:-$HOME/.local/share}/applications
mkdir -p -- "$desktop_dir"
desktop_path=$desktop_dir/doax3-scarlet.desktop
if [[ -e "$desktop_path" ]]; then
    echo "Application copied to $destination; existing desktop entry preserved: $desktop_path"
else
    escaped=${destination//\\/\\\\}
    escaped=${escaped//\"/\\\"}
    escaped=${escaped//\$/\\\$}
    escaped=${escaped//\`/\\\`}
    escaped=${escaped//%/%%}
    printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=DOAX3 Scarlet' \
        "Exec=\"$escaped/Launch-DOAX3.sh\"" "Icon=$destination/share/doax3.png" \
        'Terminal=false' 'Categories=Game;' 'StartupWMClass=doax3-scarlet' > "$desktop_path"
    chmod +x "$desktop_path"
    echo "Installed DOAX3 Scarlet: $destination"
fi
echo 'Add Launch-DOAX3.sh to Steam as a non-Steam game. Leave Force compatibility tool unchecked.'
echo 'First launch asks for your Scarlet 1.08 game folder and sys_modules folder.'
