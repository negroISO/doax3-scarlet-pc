"""Assemble a NEW game folder from already extracted base/update and optional DLC."""
import argparse
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'source/scripts'))
from prepare import read_sfo


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base', type=Path, required=True)
    p.add_argument('--update', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--dlc', type=Path, nargs='*', default=[])
    a = p.parse_args()
    profile = json.loads((ROOT/'project.json').read_text())
    base, update, output = a.base.resolve(), a.update.resolve(), a.output.resolve()
    if output.exists():
        p.error('output already exists; choose a new folder')
    if any(output.is_relative_to(path) or path.is_relative_to(output) for path in (base,update)):
        p.error('output and input trees must be separate')
    for path, category in ((base,'gd'),(update,'gp')):
        meta = read_sfo(path/'sce_sys/param.sfo') or {}
        if meta.get('TITLE_ID') != profile['title_id'] or meta.get('CATEGORY') != category:
            p.error('wrong title/category for '+str(path))
        if category == 'gp' and meta.get('APP_VER') != profile['version']:
            p.error('update must be version'+profile['version'])
    addons = []
    for path in a.dlc:
        path = path.resolve()
        if output.is_relative_to(path) or path.is_relative_to(output):
            p.error('DLC and output trees must be separate')
        meta = read_sfo(path/'sce_sys/param.sfo') or {}
        label = meta.get('CONTENT_ID','')[20:]
        if meta.get('TITLE_ID') != profile['title_id'] or meta.get('CATEGORY') != 'ac' or len(label) != 16 or not label.replace('_','').isalnum():
            p.error('wrong DLC metadata: '+str(path))
        if label in [entry[0] for entry in addons]:
            p.error('duplicate DLC content label '+label)
        addons.append((label,path))
    shutil.copytree(base,output)
    shutil.copytree(update,output,dirs_exist_ok=True)
    for label,path in addons:
        shutil.copytree(path,output/'addcont'/label)
    meta = read_sfo(output/'sce_sys/param.sfo') or {}
    if (meta.get('TITLE_ID'),meta.get('APP_VER')) != (profile['title_id'],profile['version']):
        raise RuntimeError('merged metadata mismatch')
    print(json.dumps({'game_dir':str(output),'title_id':profile['title_id'],
                      'version':profile['version'],'optional_dlc':len(addons)},indent=2))


if __name__ == '__main__':
    main()
