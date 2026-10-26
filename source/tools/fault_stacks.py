"""Symbolized fault stacks of game logs: python tools/fault_stacks.py out/batch/*.log

For each log with a 'Fault:' report, prints the faulting thread and the unwound host frames,
resolving theorder-probe.exe offsets with llvm-symbolizer (MSYS2 CLANG64).
"""
import re
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / 'out' / 'theorder-probe.exe'
IMAGE_BASE = 0x140000000


def symbolizer():
    found = os.environ.get('LLVM_SYMBOLIZER') or shutil.which('llvm-symbolizer')
    if found:
        return found
    candidate = Path(os.environ.get('MSYS2_ROOT', 'C:/msys64')) / 'clang64/bin/llvm-symbolizer.exe'
    return str(candidate) if candidate.exists() else None


def symbolize(offsets):
    tool = symbolizer()
    if not tool or not offsets:
        return {}
    text = '\n'.join(hex(IMAGE_BASE + o) for o in offsets)
    out = subprocess.run([tool, f'--obj={EXE}', '-f', '-C'], input=text, capture_output=True,
                         text=True).stdout.strip().split('\n\n')
    names = {}
    for offset, block in zip(offsets, out):
        lines = block.split('\n')
        where = lines[1].replace(str(ROOT).replace('\\', '/') + '/gpu/shadps4/', '') if len(lines) > 1 else ''
        names[offset] = f'{lines[0][:90]}  {where[:70]}'
    return names


def main():
    for path in sys.argv[1:]:
        log = Path(path).read_text(encoding='utf-8', errors='replace')
        fault = re.search(r'^Fault: .*$', log, re.M)
        if not fault:
            continue
        rest = log[fault.start():]
        thread = re.search(r'thread name: (.*)', rest[:400])
        frames = re.findall(r'^\s+\d+ (\S+)', rest.split('globals:')[0], re.M)
        offsets = [int(m.group(1), 16) for f in frames
                   for m in [re.match(r'theorder-probe\.exe\+0x([0-9a-f]+)', f)] if m]
        names = symbolize(offsets)
        flip = re.findall(r'Flip #(\d+)', log[:fault.start()])
        print(f'== {Path(path).name}: {fault.group(0)[7:110]} '
              f'[{thread.group(1) if thread else "?"}] after flip {flip[-1] if flip else 0}')
        for f in frames[:12]:
            m = re.match(r'theorder-probe\.exe\+0x([0-9a-f]+)', f)
            print('    ' + (names.get(int(m.group(1), 16), f) if m else f))


if __name__ == '__main__':
    main()
