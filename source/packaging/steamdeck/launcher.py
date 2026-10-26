"""Native Linux candidate launcher, frozen with Python for Steam Deck deployment."""
import argparse
from datetime import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

APP = Path(sys.executable).resolve().parent if getattr(sys, 'frozen', False) else Path(__file__).resolve().parent
MODULE_NAMES = ('libSceFiber.sprx', 'ulobjmgr.sprx', 'libSceUlt.sprx')
PROFILE = {'name': 'Scarlet', 'title_id': 'CUSA14173', 'version': '01.08', 'slug': 'doax3-scarlet'}
if (APP / 'share/game.json').is_file():
    PROFILE = json.loads((APP / 'share/game.json').read_text())
NAME = 'DOAX3 ' + PROFILE['name']


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def choose_folder(title):
    import tkinter as tk
    from tkinter import filedialog
    try:
        root = tk.Tk()
    except tk.TclError as error:
        raise RuntimeError('Complete first setup in Steam Deck Desktop Mode, or pass --game-dir and --modules-dir.') from error
    root.withdraw()
    try:
        return filedialog.askdirectory(title=title, parent=root)
    finally:
        root.destroy()


def error_dialog(text):
    print(text, file=sys.stderr)
    if os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'):
        try:
            import tkinter as tk
            from tkinter import messagebox
            root = tk.Tk()
            root.withdraw()
            messagebox.showerror(NAME, text, parent=root)
            root.destroy()
        except Exception:
            pass


def main():
    parser = argparse.ArgumentParser(description=f'{NAME} — native Linux / Steam Deck candidate')
    parser.add_argument('--game-dir', type=Path)
    parser.add_argument('--modules-dir', type=Path)
    parser.add_argument('--data-dir', type=Path)
    parser.add_argument('--timeout', type=int, default=0)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--self-check', action='store_true')
    parser.add_argument('--no-ui', action='store_true')
    args = parser.parse_args()
    if args.timeout < 0:
        parser.error('timeout must be nonnegative')
    exe = APP / 'bin/theorder-probe'
    if not exe.is_file() or exe.open('rb').read(4) != b'\x7fELF':
        raise RuntimeError('The native Linux runtime is missing. Extract the complete application archive.')
    env = dict(os.environ)
    env['LD_LIBRARY_PATH'] = str(APP / 'lib')
    env.update(THEORDER_NEURAL='0', THEORDER_UPSCALER='none', THEORDER_OBJECT_MOTION='0',
               THEORDER_ASYNC_SHADERS='1', THEORDER_FPS_LIMIT='30', THEORDER_VBLANK_HZ='60',
               THEORDER_FLIP_RATE='1', THEORDER_NATIVE_LIBRARIES='libSceFiber,ulobjmgr,libSceUlt',
               THEORDER_FILE_TRACE='0', SDL_APP_ID=PROFILE['slug'])
    for name in ('THEORDER_PAD_FILE', 'THEORDER_PAD_REPLAY', 'THEORDER_RESTART_COMMAND',
                 'THEORDER_NR_DUMP_TRIGGER', 'THEORDER_CAPTURE_TRIGGER', 'THEORDER_FRAME_DUMP_TRIGGER'):
        env.pop(name, None)
    if args.self_check:
        import tkinter as tk
        tcl_version = tk.Tcl().call('info', 'patchlevel')
        cpu = subprocess.run([str(exe), '--cpu-features'], env=env, capture_output=True, text=True, timeout=20)
        print(json.dumps({'platform': platform.platform(), 'machine': platform.machine(),
                          'native_elf': True, 'cpu_exit': cpu.returncode, 'cpu': cpu.stdout.strip(),
                          'cpu_stderr': cpu.stderr.strip(), 'tcl_version': tcl_version,
                          'runtime_sha256': sha256(exe)}, indent=2))
        return cpu.returncode
    data = (args.data_dir or Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share')) / PROFILE['slug']).resolve()
    data.mkdir(parents=True, exist_ok=True)
    locations_file = data / 'locations.json'
    locations = json.loads(locations_file.read_text()) if locations_file.is_file() else {}
    game = args.game_dir or (Path(locations['game']) if locations.get('game') else None)
    if game is None and not args.no_ui:
        selected = choose_folder(f'{NAME} {PROFILE["version"]} — choose the game folder containing eboot.bin')
        game = Path(selected) if selected else None
    if game is None:
        raise RuntimeError(f'Choose your merged {PROFILE["name"]} {PROFILE["version"]} game folder, or pass --game-dir PATH.')
    game = game.resolve()
    from prepare import prepare, read_sfo
    from link_modules import link
    sfo = read_sfo(game / 'sce_sys/param.sfo') or {}
    if (sfo.get('TITLE_ID'), sfo.get('APP_VER')) != (PROFILE['title_id'], PROFILE['version']):
        raise RuntimeError(f'This candidate needs the merged {PROFILE["name"]} {PROFILE["title_id"]} version {PROFILE["version"]} game folder.')
    modules_dir = args.modules_dir or (Path(locations['modules']) if locations.get('modules') else APP / 'sys_modules')
    if not all((modules_dir / name).is_file() for name in MODULE_NAMES) and not args.no_ui:
        selected = choose_folder('Choose your sys_modules folder: libSceFiber, ulobjmgr and libSceUlt')
        if selected:
            modules_dir = Path(selected)
    modules_dir = modules_dir.resolve()
    inputs = [exe, game / 'eboot.bin', game / 'sce_sys/param.sfo',
              game / 'sce_module/libc.prx', game / 'sce_module/libSceFios2.prx',
              *[modules_dir / name for name in MODULE_NAMES], APP / 'share/content.bin', APP / 'share/patches.bin']
    for path in inputs:
        if not path.is_file():
            raise RuntimeError(f'Missing required file: {path}')
    image = data / 'runtime'
    user = data / 'user'
    image.mkdir(exist_ok=True)
    user.mkdir(exist_ok=True)
    with (image / 'candidate.lock').open('a+b') as lock:
        try:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError(f'{NAME} is already preparing or running.') from None
        fingerprint = {str(path): sha256(path) for path in inputs}
        manifest = image / 'candidate.json'
        previous = json.loads(manifest.read_text()) if manifest.is_file() else {}
        linked = image / 'boot-linked.bin'
        if (previous.get('inputs') != fingerprint or not linked.is_file() or
                previous.get('linked_sha256') != sha256(linked)):
            prepare(game, image)
            link(game, image, ['libc.prx', 'libSceFios2.prx', *[str(modules_dir / name) for name in MODULE_NAMES]])
            (image / 'boot.bin').unlink()
            manifest.write_text(json.dumps({'inputs': fingerprint, 'linked_sha256': sha256(linked)}, indent=2))
        locations_file.write_text(json.dumps({'game': str(game), 'modules': str(modules_dir)}, indent=2))
        config = user / 'graphics.ini'
        if not config.exists():
            config.write_text('upscaler=off\nobject_motion=0\njitter=0\nasync_shaders=1\n'
                              'display_mode=borderless\nvsync=1\nfps_limit=30\nengine_fps=60\n'
                              'output_res=1920x1080\nframes_queued=1\n')
        if args.prepare_only:
            print(f'Prepared native {NAME}: {linked}')
            return 0
        env.update(THEORDER_GAME_DIR=str(game), THEORDER_DATA_DIR=str(image),
                   THEORDER_USER_DIR=str(user), THEORDER_GPU_USER_DIR=str(user),
                   THEORDER_CONFIG=str(config))
        if (game / 'addcont').is_dir():
            env['THEORDER_ADDON_DIR'] = str(game / 'addcont')
        else:
            env.pop('THEORDER_ADDON_DIR', None)
        logs = data / 'logs' / datetime.now().strftime('%Y%m%d-%H%M%S')
        logs.mkdir(parents=True)
        command = [str(exe), str(linked), '--content-profile', str(APP / 'share/content.bin'),
                   '--patches', str(APP / 'share/patches.bin'), '--app0', str(game),
                   '--user', str(user), '--timeout', str(args.timeout)]
        print(f'{NAME} native Linux — 30 FPS cap. Logs: {logs}', flush=True)
        with (logs / 'stdout.log').open('wb') as stdout, (logs / 'stderr.log').open('wb') as stderr:
            result = subprocess.call(command, env=env, cwd=APP, stdout=stdout, stderr=stderr)
        (logs / 'result.json').write_text(json.dumps({'exit_code': result, 'command': command,
            'runtime_sha256': sha256(exe), 'linked_sha256': sha256(linked), 'neural': False,
            'frame_generation': False, 'fps_limit': 30, 'timeout': args.timeout}, indent=2))
        if result:
            error_dialog(f'{NAME} stopped with code {result}. Logs: {logs}')
        return result


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        error_dialog(str(error))
        raise SystemExit(1)
