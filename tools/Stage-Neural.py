"""Stage user-supplied runtime DLLs and locally built adapters; never downloads files."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {
    'nvngx_dlss.dll':'2ecf41de71383416a5fb1476d26d495fc391fdc254d6e5a27689b598b2d05702',
    'nvngx_dlssnr.dll':'8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206',
}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime-dir',type=Path,required=True)
    p.add_argument('--bridge-dir',type=Path,default=ROOT/'source/out/dlss-bridge/Release')
    a = p.parse_args()
    sources = {name:a.runtime_dir/name for name in EXPECTED}
    sources.update({'port_dlss.dll':a.bridge_dir/'port_dlss.dll',
                    'nvngx.dll/port_nr.dll':a.bridge_dir/'port_nr.dll'})
    for name,path in sources.items():
        if not path.is_file():
            p.error('missing '+str(path))
        if name == 'nvngx.dll/port_nr.dll' and (ROOT/'source/out/gpu/nvngx.dll').is_file():
            p.error('nvngx.dll must be a directory, not a DLL file')
    report = []
    for name,path in sources.items():
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if name in EXPECTED and digest != EXPECTED[name]:
            print('WARNING: unvalidated runtime identity for '+name)
        destination = ROOT/'source/out/gpu'/name
        destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(path,destination)
        if hashlib.sha256(destination.read_bytes()).hexdigest() != digest:
            raise RuntimeError('copy verification failed')
        report.append({'file':name,'sha256':digest,'tested_runtime_match':
                       digest==EXPECTED[name] if name in EXPECTED else None})
    (ROOT/'runtime').mkdir(exist_ok=True)
    (ROOT/'runtime/neural-runtime.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
