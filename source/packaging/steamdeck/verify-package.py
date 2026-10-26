"""Exercise installation, relocation and archive integrity without a Steam Deck."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import zipfile


def run(command, env):
    return subprocess.run([str(part) for part in command], env=env, capture_output=True, text=True, timeout=45)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--release', type=Path)
    parser.add_argument('--game', choices=('Scarlet', 'Fortune'), default='Scarlet')
    args = parser.parse_args()
    release = (args.release or args.workspace.resolve() / 'release').resolve()
    app = release / ('DOAX3-' + args.game)
    slug = 'doax3-' + args.game.lower()
    with tempfile.TemporaryDirectory(prefix='package-check-', dir=args.workspace) as temp:
        temporary = Path(temp)
        home = temporary / 'home'
        home.mkdir()
        data = temporary / 'data'
        env = {key: value for key, value in os.environ.items()
               if key not in ('CONDA_PREFIX', 'PYTHONHOME', 'PYTHONPATH', 'LD_LIBRARY_PATH')}
        env.update(HOME=str(home), XDG_DATA_HOME=str(data), PATH='/usr/bin:/bin')
        installed = temporary / 'native app with spaces'
        install = run(['bash', app / 'install.sh', installed], env)
        assert install.returncode == 0, install.stderr
        desktop = data / f'applications/{slug}.desktop'
        text = desktop.read_text()
        assert f'Exec="{installed}/Launch-DOAX3.sh"' in text
        assert f'Name=DOAX3 {args.game}' in text
        self_check = run([installed / 'Launch-DOAX3.sh', '--self-check'], env)
        assert self_check.returncode == 0, self_check.stderr
        check = json.loads(self_check.stdout)
        assert check['native_elf'] and check['cpu_exit'] == 0 and check['tcl_version']
        marker = data / f'{slug}/user/savedata/preserve-test'
        marker.parent.mkdir(parents=True)
        marker.write_bytes(b'keep existing save')
        repeated = run(['bash', app / 'install.sh', installed], env)
        assert repeated.returncode != 0
        assert marker.read_bytes() == b'keep existing save'
        invalid = run([installed / 'Launch-DOAX3.sh', '--no-ui', '--prepare-only',
                       '--game-dir', temporary / 'missing-game', '--data-dir', temporary / 'bad-input'], env)
        assert invalid.returncode != 0
        assert not (temporary / 'missing-game').exists()
    archives = []
    for archive in release.glob('*.zip'):
        with zipfile.ZipFile(archive) as bundle:
            assert bundle.testzip() is None
            assert all(not name.startswith('/') and '..' not in Path(name).parts for name in bundle.namelist())
        archives.append({'name': archive.name, 'integrity': 'CRC passed'})
    for archive in release.glob('*.tar.gz'):
        with tarfile.open(archive) as bundle:
            members = bundle.getmembers()
            assert all(not member.name.startswith('/') and '..' not in Path(member.name).parts for member in members)
            for member in members:
                if member.isfile():
                    with bundle.extractfile(member) as file:
                        assert hashlib.sha256(file.read()).hexdigest() == hashlib.sha256((release / member.name).read_bytes()).hexdigest()
        archives.append({'name': archive.name, 'integrity': 'all payload SHA256 passed'})
    versions = set()
    for path in app.rglob('*'):
        if not path.is_file():
            continue
        with path.open('rb') as file:
            if file.read(4) != b'\x7fELF':
                continue
        output = subprocess.check_output(['readelf', '--version-info', str(path)], text=True)
        versions.update(re.findall(r'GLIBC_(\d+\.\d+(?:\.\d+)?)', output))
    maximum = max(versions, key=lambda value: tuple(map(int, value.split('.'))))
    assert tuple(map(int, maximum.split('.'))) <= (2, 35), f'Unexpected build-host ABI: {maximum}'
    result = {'install_and_desktop_entry': 'passed', 'relocated_path_with_spaces': 'passed',
              'existing_install_refused': 'passed', 'savedata_preserved': 'passed',
              'missing_game_refused': 'passed', 'frozen_cpu_tcl_check': check,
              'maximum_glibc_symbol': maximum, 'archives': archives,
              'steam_deck_hardware_tested': False}
    (release / 'package-verification.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
