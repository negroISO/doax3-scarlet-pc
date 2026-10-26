"""Repeated timed runs of the game with A/B conditions, summarized as failure rates.

    python tools/batch_runs.py --runs 8 --secs 40 --cond base= --cond nomsaa=THEORDER_NO_MSAA=1
    python tools/batch_runs.py --runs 4 --secs 90 --game-args "--level cat_010_streets"
    python tools/batch_runs.py --runs 4 --secs 90 --pad "22:cross 26:cross 30:cross"

--pad presses buttons at the given seconds after launch (SECS:TOKEN[,TOKEN][:HOLD]) through
THEORDER_PAD_FILE (src/runtime_pad.c), e.g. to get past the title screen.

Each --cond is LABEL=[VAR=VALUE[,VAR=VALUE...]]. Conditions are interleaved run by run, so a
drift (shader cache warming, temperature) affects all of them alike. Logs land in out/batch/;
a run fails on a fault or when it stops presenting before the timeout (fewer stats windows).
"""
import argparse
import os
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAME_DIR = os.environ.get('THEORDER_GAME_DIR')


def kill_orphans():
    # A run killed by the timeout can leave the game running and the .exe locked (stale builds).
    subprocess.run(['taskkill', '/F', '/IM', 'theorder-probe.exe'], capture_output=True)


def summarize(log: str, secs: int):
    flips = re.findall(r'Flip #(\d+)', log)
    fault = re.search(r'^Fault: .*? at (\S+)', log, re.M)
    windows = log.count('Frame stats:')
    compile_ms = sum(float(m) for m in re.findall(r'compiles ([0-9.]+) ms', log))
    # Windows are 5 s of presented frames: a long load (a save) has fewer of them without a
    # hang, so under half the expected count means presentation stopped.
    hung = not fault and windows < max(1, secs // 10)
    return {
        'flip': int(flips[-1]) if flips else 0,
        'fault': fault.group(1) if fault else '',
        'hung': hung,
        'compile_s': compile_ms / 1000,
        'windows': windows,
    }


def play_pad(script: str, pad_file: Path, stop: threading.Event):
    start = time.monotonic()
    for step in script.split():
        secs, tokens, *hold = step.split(':')
        if stop.wait(max(0.0, float(secs) - (time.monotonic() - start))):
            return
        pad_file.write_text(tokens.replace(',', ' '))
        stop.wait(float(hold[0]) if hold else 0.3)
        pad_file.write_text('')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runs', type=int, default=8)
    parser.add_argument('--secs', type=int, default=40)
    parser.add_argument('--cond', action='append', default=[], help='LABEL=VAR=VALUE,...')
    parser.add_argument('--game-args', default='', help='extra run.py arguments, e.g. "--level cat_010_streets"')
    parser.add_argument('--pad', default='', help='scripted presses: "SECS:TOKEN[,TOKEN][:HOLD] ..."')
    args = parser.parse_args()
    if not GAME_DIR or not (Path(GAME_DIR) / 'eboot.bin').is_file():
        parser.error('set THEORDER_GAME_DIR to a complete extracted game')
    conds = []
    for c in args.cond or ['base=']:
        label, _, assigns = c.partition('=')
        env = dict(a.split('=', 1) for a in assigns.split(',') if a)
        conds.append((label, env))

    exe = ROOT / 'out' / 'theorder-probe.exe'
    print(f'exe {exe.name} built {time.ctime(exe.stat().st_mtime)}')
    out = ROOT / 'out' / 'batch'
    out.mkdir(parents=True, exist_ok=True)
    results = {label: [] for label, _ in conds}
    for i in range(args.runs):
        for label, extra in conds:
            kill_orphans()
            env = dict(os.environ)
            env.update({'THEORDER_GAME_DIR': GAME_DIR, 'THEORDER_TIMEOUT': str(args.secs)})
            # A run's settings (the in-game menu writes them on exit) go to a fresh file of its
            # own, never to the player's theorder.ini, unless the condition picks a config.
            if 'THEORDER_CONFIG' not in extra:
                run_ini = out / f'{label}-{i + 1}.ini'
                run_ini.unlink(missing_ok=True)
                env['THEORDER_CONFIG'] = str(run_ini)
            env.update(extra)
            log_path = out / f'{label}-{i + 1}.log'
            stop = threading.Event()
            if args.pad:
                pad_file = out / 'pad'
                pad_file.write_text('')
                env['THEORDER_PAD_FILE'] = env['THEORDER_PAD_FILE'] = str(pad_file)
                threading.Thread(target=play_pad, args=(args.pad, pad_file, stop), daemon=True).start()
            with open(log_path, 'w', encoding='utf-8', errors='replace') as f:
                try:
                    subprocess.run([sys.executable, str(ROOT / 'run.py')] + args.game_args.split(),
                                   env=env, cwd=ROOT,
                                   stdout=f, stderr=subprocess.STDOUT, timeout=args.secs + 40)
                except subprocess.TimeoutExpired:
                    pass
            stop.set()
            r = summarize(log_path.read_text(encoding='utf-8', errors='replace'), args.secs)
            results[label].append(r)
            state = f'FAULT {r["fault"]}' if r['fault'] else ('HUNG' if r['hung'] else 'ok')
            print(f'{label:>10} {i + 1:2}: flip {r["flip"]:5} compile {r["compile_s"]:5.1f}s {state}',
                  flush=True)
    kill_orphans()
    print('summary:')
    for label, rs in results.items():
        failed = sum(1 for r in rs if r['fault'] or r['hung'])
        flips = sorted(r['flip'] for r in rs)
        print(f'  {label:>10}: {failed}/{len(rs)} failed '
              f'({sum(1 for r in rs if r["fault"])} faults, {sum(1 for r in rs if r["hung"])} hung), '
              f'median flip {flips[len(flips) // 2]}, '
              f'mean compile {sum(r["compile_s"] for r in rs) / len(rs):.1f}s')


if __name__ == '__main__':
    main()
