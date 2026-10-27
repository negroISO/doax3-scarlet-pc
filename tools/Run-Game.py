"""Portable Windows candidate entry point; user paths and generated images stay local."""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PROFILE = json.loads((ROOT / 'project.json').read_text())
MODULES = ('libSceFiber.sprx', 'ulobjmgr.sprx', 'libSceUlt.sprx')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path)
    parser.add_argument('--modules-dir', type=Path)
    parser.add_argument('--msys2-root', type=Path)
    parser.add_argument('--addon-dir', type=Path)
    parser.add_argument('--exe', type=Path, help='advanced: override the native host')
    parser.add_argument('--neural', choices=('0', '1'), default=None, help='override saved NR enable state')
    parser.add_argument('--prefer-neural', action='store_true', help='enable NR on first setup, then respect saved choices')
    parser.add_argument('--nr-strength', type=float, default=None, help='override saved NR strength (0..1)')
    parser.add_argument('--timeout', type=int, default=0)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--check', action='store_true', help='validate paths/version without creating game images')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('this entry point is Windows-only; see docs/building.md for native Linux status')
    if args.timeout < 0 or (args.nr_strength is not None and not 0 <= args.nr_strength <= 1):
        parser.error('timeout must be nonnegative and NR strength must be between0 and1')
    config_file = ROOT / 'local.json'
    config = json.loads(config_file.read_text()) if config_file.exists() else {}
    def location(option, key, fallback):
        return (option or Path(config.get(key, fallback))).resolve()
    game = location(args.game_dir, 'game_dir', ROOT / 'game' / PROFILE['title_id'])
    modules = location(args.modules_dir, 'modules_dir', ROOT / 'sys_modules')
    msys = location(args.msys2_root, 'msys2_root', os.environ.get('MSYS2_ROOT', r'C:\msys64'))
    exe = (args.exe or ROOT / 'source/out/gpu' / (PROFILE['target']+'.exe')).resolve()
    addon = (args.addon_dir.resolve() if args.addon_dir else
             Path(config['addon_dir']).resolve() if config.get('addon_dir') else game / 'addcont')
    required = [exe, game / 'eboot.bin', game / 'sce_sys/param.sfo',
                game / 'sce_module/libc.prx', game / 'sce_module/libSceFios2.prx',
                *[modules / name for name in MODULES]]
    for path in required:
        if not path.is_file():
            parser.error('missing '+str(path)+'; configure --game-dir/--modules-dir or build the host first')
    if args.addon_dir and not addon.is_dir():
        parser.error('explicit add-on directory does not exist')
    sys.path.insert(0, str(ROOT / 'source/scripts'))
    from prepare import prepare, read_sfo
    from link_modules import link
    from content_profile import prepare as content_profile
    from patches import write_patches
    meta = read_sfo(game / 'sce_sys/param.sfo') or {}
    if (meta.get('TITLE_ID'), meta.get('APP_VER')) != (PROFILE['title_id'], PROFILE['version']):
        parser.error('requires merged '+PROFILE['title_id']+' version'+PROFILE['version']+' game data')
    if args.neural == '1':
        for name in ('port_dlss.dll', 'nvngx.dll/port_nr.dll', 'nvngx_dlss.dll', 'nvngx_dlssnr.dll'):
            if not (exe.parent / name).is_file():
                parser.error('NR dependency missing: '+str(exe.parent / name)+'; see docs/neural-rendering.md')
    if args.check:
        print(json.dumps({'status':'ok', 'game':PROFILE, 'game_dir':str(game),
                          'modules_dir':str(modules), 'exe':str(exe)}, indent=2))
        return 0
    config.update(game_dir=str(game), modules_dir=str(modules), msys2_root=str(msys))
    if args.addon_dir:
        config['addon_dir'] = str(addon)
    config_file.write_text(json.dumps(config, indent=2)+'\n')
    image = ROOT / 'runtime'
    image.mkdir(exist_ok=True)
    import msvcrt
    with (image / 'candidate.lock').open('a+b') as lock:
        if not lock.tell():
            lock.write(b'\0')
            lock.flush()
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            parser.error('this repository is already preparing or running a game')
        inputs = [*required[1:], *[ROOT / 'source/scripts' / name for name in
                  ('prepare.py','link_modules.py','link_libc.py')]]
        identity = {str(path):digest(path) for path in inputs}
        manifest_path = image / 'candidate.json'
        previous = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
        linked = image / 'boot-linked.bin'
        if previous.get('inputs') != identity or not linked.exists() or previous.get('linked_sha256') != digest(linked):
            prepare(game, image)
            link(game, image, ['libc.prx','libSceFios2.prx', *[str(modules/name) for name in MODULES]])
            (image / 'boot.bin').unlink()
            manifest_path.write_text(json.dumps({'inputs':identity,'linked_sha256':digest(linked)}, indent=2)+'\n')
        content_profile(game, image, 'full')
        write_patches(image, [])
        if args.prepare_only:
            print('Prepared local image: '+str(linked))
            return 0
        user = ROOT / 'user'
        user.mkdir(exist_ok=True)
        graphics = user / 'graphics.ini'
        saved = dict(line.split('=', 1) for line in graphics.read_text().splitlines()
                     if '=' in line and not line.lstrip().startswith('#')) if graphics.is_file() else {}
        selected_neural = args.neural if args.neural is not None else str(int(
            saved.get('neural', '1' if args.prefer_neural else '0').strip() == '1'))
        if not graphics.is_file():
            defaults = (ROOT / 'config/graphics.defaults.ini').read_text()
            graphics.write_text(defaults.replace('neural=0', 'neural='+selected_neural))
        env = os.environ.copy()
        for key in ('THEORDER_PAD_FILE','THEORDER_PAD_REPLAY','THEORDER_SAMPLE','THEORDER_SAMPLE_OUT',
                    'THEORDER_RESTART_COMMAND','THEORDER_GPU_PROFILE','THEORDER_PM4_BAD_DUMP',
                    'THEORDER_DUMP_SHADERS','THEORDER_FRAME_DUMP_TRIGGER','THEORDER_NR_DUMP_TRIGGER',
                    'THEORDER_CAPTURE_TRIGGER','THEORDER_FRAME_TIMES','THEORDER_PRESET_FILE',
                    'THEORDER_UPSCALER','THEORDER_PRESET','THEORDER_NEURAL','THEORDER_NR_STRENGTH',
                    'THEORDER_OUTPUT_RES','THEORDER_RENDER_RES','THEORDER_ENGINE_FPS'):
            env.pop(key, None)
        env.update(THEORDER_GAME_DIR=str(game), THEORDER_DATA_DIR=str(image),
                   THEORDER_USER_DIR=str(user), THEORDER_GPU_USER_DIR=str(user),
                   THEORDER_CONFIG=str(graphics),
                   THEORDER_ASYNC_SHADERS='1', THEORDER_FPS_LIMIT='30', THEORDER_VBLANK_HZ='60',
                   THEORDER_FLIP_RATE='1', THEORDER_NATIVE_LIBRARIES='libSceFiber,ulobjmgr,libSceUlt',
                   THEORDER_NEURAL=selected_neural,
                   THEORDER_NR_GUIDES_ONLY='0', THEORDER_OBJECT_MOTION='1',
                   THEORDER_FILE_TRACE='0')
        if args.nr_strength is not None:
            env['THEORDER_NR_STRENGTH'] = str(args.nr_strength)
        env.setdefault('THEORDER_KEY_MAP', 'options=F8')
        if addon.is_dir():
            env['THEORDER_ADDON_DIR'] = str(addon)
        else:
            env.pop('THEORDER_ADDON_DIR', None)
        env['PATH'] = os.pathsep.join((str(exe.parent), str(ROOT/'source/out'),
                                      str(msys/'clang64/bin'), env.get('PATH','')))
        logs = image / 'logs' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
        logs.mkdir(parents=True)
        command = [str(exe),str(linked),'--content-profile',str(image/'content.bin'),
                   '--patches',str(image/'patches.bin'),'--app0',str(game),
                   '--user',str(user),'--timeout',str(args.timeout)]
        print(PROFILE['name']+'; Circle=Left Shift/Esc, Cross=Space/Enter, Options=F8', flush=True)
        print('Saves/cache: '+str(user)+'; logs: '+str(logs), flush=True)
        print('Native 30 FPS; Insert opens live graphics controls. Settings: '+str(graphics), flush=True)
        with (logs/'stdout.log').open('wb') as out, (logs/'stderr.log').open('wb') as err:
            code = subprocess.call(command, cwd=ROOT/'source', env=env, stdout=out, stderr=err)
        (logs/'result.json').write_text(json.dumps({'exit_code':code,'command':command,
            'host_sha256':digest(exe),'linked_sha256':digest(linked),'neural':selected_neural}, indent=2)+'\n')
        return code


if __name__ == '__main__':
    raise SystemExit(main())
