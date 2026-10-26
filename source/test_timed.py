import os
import subprocess
import sys


def _parse_timeout(argv):
    if not argv:
        return 45, argv

    if argv[0].startswith('--timeout='):
        return int(argv[0].split('=', 1)[1]), argv[1:]
    if argv[0] in ('--timeout', '-t'):
        if len(argv) < 2:
            raise SystemExit('missing timeout value after --timeout/-t')
        return int(argv[1]), argv[2:]
    if argv[0].lstrip('-').isdigit():
        return int(argv[0]), argv[1:]
    return 45, argv


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    timeout_sec, run_args = _parse_timeout(argv)
    game_dir = os.environ.get('THEORDER_GAME_DIR')
    if not game_dir or not os.path.isfile(os.path.join(game_dir, 'eboot.bin')):
        raise SystemExit('Set THEORDER_GAME_DIR to a complete extracted game before running this diagnostic.')

    dump_dir = os.path.abspath('out/dump')
    if os.path.exists(dump_dir):
        for f in os.listdir(dump_dir):
            try:
                os.remove(os.path.join(dump_dir, f))
            except OSError:
                pass

    pad_file = os.path.abspath('pad_inject.txt')
    with open(pad_file, 'w') as f:
        f.write('')

    env = dict(os.environ)
    env['THEORDER_GAME_DIR'] = os.path.abspath(game_dir)
    env['THEORDER_TIMEOUT'] = str(timeout_sec)
    env['THEORDER_FRAME_DUMP_INTERVAL'] = '60'
    env['THEORDER_USER_DIR'] = os.path.abspath('out/user_gameplay')
    env['THEORDER_PAD_FILE'] = pad_file

    import threading
    import time

    stop_event = threading.Event()
    def pad_pusher():
        time.sleep(12)
        while not stop_event.is_set():
            try:
                with open(pad_file, 'w') as f:
                    f.write('cross\n')
                time.sleep(0.3)
                with open(pad_file, 'w') as f:
                    f.write('\n')
                time.sleep(2.0)
            except Exception:
                pass

    t = threading.Thread(target=pad_pusher, daemon=True)
    t.start()

    print(f"Launching game with THEORDER_TIMEOUT={timeout_sec}s, dumping frames every 60 flips...")
    try:
        with open('run_video_test.log', 'w', encoding='utf-8', errors='replace') as f:
            result = subprocess.run(
                [sys.executable, 'run.py'] + run_args,
                env=env,
                stdout=f,
                stderr=subprocess.STDOUT,
                timeout=timeout_sec + 15
            )
        stop_event.set()
        print(f"Game finished with exit code {result.returncode}")
        subprocess.run([sys.executable, 'tools/convert_dumps.py'])
        return result.returncode
    except subprocess.TimeoutExpired:
        print(f"Subprocess timeout expired after {timeout_sec + 10} seconds; process terminated.")
        subprocess.run([sys.executable, 'tools/convert_dumps.py'])
        return 124


if __name__ == '__main__':
    raise SystemExit(main())
