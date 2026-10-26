"""Build a relocatable native Linux archive from the isolated build outputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import zipfile
from deck_install import installer_desktop

HERE = Path(__file__).resolve().parent
# Host ABI and graphics/display stacks stay on the Deck. Application dependencies
# and the C++ runtime are bundled; no GPU driver or Vulkan ICD is shipped.
HOST_LIBRARIES = re.compile(r'^(?:ld-linux.*|lib(?:c|m|dl|rt|pthread|resolv|util|nsl|anl)|'
                            r'lib(?:vulkan|GL|GLX|GLdispatch|EGL|OpenGL|gbm|drm.*|'
                            r'X11|X11-xcb|xcb.*|Xau|Xdmcp|Xext|Xfixes|Xrender|Xi|Xcursor|Xrandr|'
                            r'wayland-.*|asound|udev|pulse.*|nvidia-.*|cuda))\.so(?:\..*)?$')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def elf(path):
    try:
        with path.open('rb') as file:
            return file.read(4) == b'\x7fELF'
    except OSError:
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--profiles', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--game', choices=('scarlet', 'fortune'), default='scarlet')
    parser.add_argument('--source-revision', required=True, help='published 40-character Git source revision')
    args = parser.parse_args()
    if not re.fullmatch(r'[0-9a-f]{40}', args.source_revision):
        parser.error('source-revision must be the complete published source commit')
    workspace = args.workspace.resolve()
    sdk = Path(os.environ['CONDA_PREFIX']).resolve()
    prefix = workspace / 'prefix'
    source = workspace / 'source'
    output = (args.output or workspace / 'dist').resolve()
    profile = ({'name': 'Fortune', 'title_id': 'CUSA04555', 'version': '01.19', 'slug': 'doax3-fortune'}
               if args.game == 'fortune' else
               {'name': 'Scarlet', 'title_id': 'CUSA14173', 'version': '01.08', 'slug': 'doax3-scarlet'})
    app = output / ('DOAX3-' + profile['name'])
    if app.exists():
        raise SystemExit('Bundle already exists; use a new output workspace or inspect it before replacing.')
    freeze = workspace / 'freeze'
    subprocess.run([str(sdk / 'bin/python'), '-m', 'PyInstaller', '--noconfirm', '--clean',
                    '--name', 'doax3-launcher', '--distpath', str(freeze / 'dist'),
                    '--workpath', str(freeze / 'work'), '--specpath', str(freeze),
                    '--paths', str(source / 'scripts'), '--hidden-import', 'prepare',
                    '--hidden-import', 'link_modules', '--hidden-import', 'link_libc',
                    '--paths', str(HERE), '--hidden-import', 'deck_install',
                    '--hidden-import', 'steam_shortcut',
                    str(HERE / 'launcher.py')], check=True)
    shutil.copytree(freeze / 'dist/doax3-launcher', app)
    (app / 'bin').mkdir()
    (app / 'lib').mkdir()
    (app / 'share').mkdir()
    (app / 'sys_modules').mkdir()
    binaries = [('out/theorder-probe', 'bin/theorder-probe'),
                ('out/theorder-gpu-capabilities', 'bin/theorder-gpu-capabilities'),
                ('out/gpu/libtheordergpu.so', 'lib/libtheordergpu.so')]
    for src, dst in binaries:
        subprocess.run(['strip', '--strip-debug', '-o', str(app / dst), str(source / src)], check=True)
    for name in ('Launch-DOAX3.sh', 'install.sh', 'README.txt'):
        text = (HERE / name).read_text()
        if args.game == 'fortune':
            for before, after in [('Scarlet', 'Fortune'), ('scarlet', 'fortune'),
                                  ('CUSA14173', 'CUSA04555'), ('1.08', '1.19'), ('v108', 'v119')]:
                text = text.replace(before, after)
            if name == 'README.txt':
                text += '\nOptional extracted DLC: keep each DLC folder under game/addcont/<16-character label>.\nAll installed packs are discovered on startup; no online shop service is supplied.\n'
        (app / name).write_text(text)
    installer = app / 'Install-DOAX3.desktop'
    installer.write_text(installer_desktop(profile['name']))
    installer.chmod(0o755)
    for path in (app / 'Launch-DOAX3.sh', app / 'install.sh', app / 'doax3-launcher',
                 app / 'bin/theorder-probe', app / 'bin/theorder-gpu-capabilities'):
        path.chmod(0o755)
    icon = 'doax3-fortune.png' if args.game == 'fortune' else 'doax3.png'
    shutil.copy2(source / 'launcher' / icon, app / 'share/doax3.png')
    (app / 'share/game.json').write_text(json.dumps(profile, indent=2) + '\n')
    source_url = 'https://github.com/negroISO/doax3-' + args.game + '-pc'
    (app / 'share/source-version.json').write_text(json.dumps({
        'repository': source_url, 'revision': args.source_revision,
        'source_archive': source_url + '/archive/' + args.source_revision + '.tar.gz',
        'platform': 'native-linux-x86_64', 'steam_deck_hardware_tested': False,
        'included_fixes': ['async shader retention and permutation keys',
                          'opaque draw command-buffer reservation',
                          'profiler submission wait and lighting clear fixes'],
        'neural_rendering': False, 'frame_generation': False}, indent=2) + '\n')
    shutil.copy2(source / 'LICENSE', app / 'LICENSE')
    for name in ('content.bin', 'patches.bin'):
        shutil.copy2(args.profiles / name, app / 'share' / name)
    environment = dict(os.environ, LD_LIBRARY_PATH=f'{app / "lib"}:{prefix / "lib"}:{sdk / "lib"}')
    pending = [app / dst for _, dst in binaries]
    copied = {}
    host = set()
    while pending:
        current = pending.pop()
        result = subprocess.run(['ldd', str(current)], env=environment, capture_output=True, text=True)
        if 'not found' in result.stdout:
            raise RuntimeError(f'Unresolved build dependency for {current}:\n{result.stdout}')
        for line in result.stdout.splitlines():
            match = re.match(r'\s*(\S+)\s+=>\s+(/\S+)', line)
            if not match:
                continue
            name, location = match.groups()
            if HOST_LIBRARIES.match(name):
                host.add(name)
                continue
            if name in copied or name == 'libtheordergpu.so':
                continue
            target = app / 'lib' / name
            shutil.copyfile(Path(location).resolve(), target)
            target.chmod(0o644)
            copied[name] = {'sha256_before_relocation': digest(target)}
            pending.append(target)
    patchelf = str(sdk / 'bin/patchelf')
    for path in (app / 'lib').iterdir():
        if elf(path):
            subprocess.run([patchelf, '--set-rpath', '$ORIGIN', str(path)], check=True)
    for path in (app / 'bin').iterdir():
        subprocess.run([patchelf, '--set-rpath', '$ORIGIN/../lib', str(path)], check=True)
    # Re-run resolution without SDK paths. A missing app dependency must fail the bundle.
    audit_env = dict(os.environ, LD_LIBRARY_PATH=str(app / 'lib'))
    dependency_audit = {}
    for path in [*(app / 'bin').iterdir(), *(app / 'lib').iterdir()]:
        if not elf(path):
            continue
        result = subprocess.run(['ldd', str(path)], env=audit_env, capture_output=True, text=True)
        if 'not found' in result.stdout or str(sdk) in result.stdout or str(prefix) in result.stdout:
            raise RuntimeError(f'Bundle is not self-contained: {path}\n{result.stdout}')
        dependency_audit[str(path.relative_to(app))] = result.stdout
    forbidden = [str(path.relative_to(app)) for path in app.rglob('*')
                 if path.is_file() and (path.suffix.lower() in ('.exe', '.dll', '.prx', '.sprx', '.pkg')
                                        or path.name in ('eboot.bin', 'boot.bin', 'boot-linked.bin'))]
    if forbidden:
        raise RuntimeError(f'Unexpected game/Windows files in native app: {forbidden}')
    lock = subprocess.check_output([str(workspace / 'tools/bin/micromamba'), '--no-rc',
        '-r', str(workspace / 'mamba'), 'list', '-p', str(sdk), '--explicit'], text=True)
    (app / 'share/sdk-explicit.txt').write_text(lock)
    licenses = app / 'licenses'
    licenses.mkdir()
    for metadata in sorted((sdk / 'conda-meta').glob('*.json')):
        record = json.loads(metadata.read_text())
        package = record.get('name', metadata.stem)
        cache = Path(record.get('link', {}).get('source', '')) / 'info/licenses'
        if cache.is_dir():
            shutil.copytree(cache, licenses / package, dirs_exist_ok=True)
    for dep in ('Vulkan-Headers', 'Vulkan-Loader', 'zydis'):
        folder = workspace / 'deps' / dep
        for notice in folder.glob('LICENSE*'):
            if notice.is_file():
                (licenses / dep).mkdir(exist_ok=True)
                shutil.copy2(notice, licenses / dep / notice.name)
    (app / 'SOURCES.txt').write_text(
        'DOAX3 native candidate, built from the recorded source snapshot.\n'
        'Corresponding source: ' + source_url + '/tree/' + args.source_revision + '\n'
        'Source archive: ' + source_url + '/archive/' + args.source_revision + '.tar.gz\n'
        'Upstream: mateussmeneses/theorder1886-port; shadPS4; FelipePrado0/gow3_pc.\n'
        'GNU GPL-2.0-or-later source lineage; see LICENSE and source/docs/upstream.\n'
        'Library source packages and exact build URLs: share/sdk-explicit.txt. Notices: licenses/.\n'
        'Vulkan-Headers/Loader v1.4.357 (Khronos); Zydis v4.1.1 with pinned Zycore.\n'
        'Only the application icon is included; no firmware, game executable/gameplay data, Windows DLLs or graphics drivers.\n')
    report = {'platform': 'native-linux-x86_64', 'game': profile, 'deck_verified': False,
              'windows_or_game_payloads': forbidden, 'bundled_libraries': copied,
              'host_libraries': sorted(host), 'dependency_audit': dependency_audit,
              'files': {str(path.relative_to(app)): digest(path) for path in app.rglob('*') if path.is_file()}}
    (output / 'bundle-report.json').write_text(json.dumps(report, indent=2))
    base = f'DOAX3-{profile["name"]}-SteamDeck-native-linux-x86_64-candidate'
    archive = output / f'{base}.tar.gz'
    with tarfile.open(archive, 'w:gz') as bundle:
        bundle.add(app, arcname=app.name)
    zipped = output / f'{base}.zip'
    with zipfile.ZipFile(zipped, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
        for path in app.rglob('*'):
            if path.is_file():
                bundle.write(path, path.relative_to(output))
    summary = {'native_runtime_sha256': digest(app / 'bin/theorder-probe'),
               'libraries': len(copied), 'files': len(report['files']),
               'archives': [{'name': path.name, 'bytes': path.stat().st_size, 'sha256': digest(path)}
                            for path in (archive, zipped)], 'deck_hardware_tested': False}
    (output / 'package-summary.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
