"""Audit the staged source export before publishing. Does not print secret values."""
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    files = subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode().split('\0')
    files = [name for name in files if name]
    problems = []
    forbidden = {'.pkg','.sprx','.prx','.self','.elf','.dll','.exe','.pdb','.raw','.dmp','.key','.pem'}
    private_roots = {'runtime','user','game','inputs','sys_modules','sdk','validation-local','.native-build'}
    patterns = [
        ('GitHub credential',rb'(?:gh[pousr]_[A-Za-z0-9_]{30,}|github_pat_[A-Za-z0-9_]{50,})'),
        ('private key',rb'-----BEGIN (?:RSA |OPENSSH |EC |DSA )?PRIVATE KEY-----'),
        ('URL credential',rb'https://[^/\s]+:[^/@\s]+@github\.com'),
        ('private network reference',rb'(?i)\b(?:[a-z0-9-]+\.home\.arpa|192\.168\.[0-9]+\.[0-9]+)\b'),
        ('developer home path',rb'(?i)\b[A-Z]:[\\/]+Users[\\/]+(?!<|\$|%)[A-Za-z][^\\/\s\x27\x22]*[\\/]'),
        ('private project/storage path',rb'(?i)(?:\b[A-Z]:[\\/]+(?:projects|Emulation|Downloads)[\\/]|/mnt/(?:downloads)/)'),
    ]
    total = 0
    for name in files:
        path = ROOT/name
        if Path(name).parts[0] in private_roots or name == 'local.json' or '/out/' in name:
            problems.append({'path':name,'reason':'local/generated directory staged'})
        if path.suffix.lower() in forbidden or path.name == 'eboot.bin':
            problems.append({'path':name,'reason':'excluded payload type'})
        if not path.is_file():
            problems.append({'path':name,'reason':'missing file/submodule pointer'})
            continue
        data = path.read_bytes()
        total += len(data)
        if len(data) >= 50*1024*1024:
            problems.append({'path':name,'reason':'unexpected large file'})
        if b'\0' not in data[:8192]:
            for label,pattern in patterns:
                if re.search(pattern,data):
                    problems.append({'path':name,'reason':label})
    stages = subprocess.check_output(['git','ls-files','--stage'],cwd=ROOT,text=True)
    if any(line.startswith('160000 ') for line in stages.splitlines()):
        problems.append({'reason':'unresolved submodule entry staged'})
    manifest = json.loads((ROOT/'docs/source-manifest.json').read_text(encoding='utf-8'))
    missing = [row['path'] for row in manifest['files'] if 'source/'+row['path'] not in files]
    if missing:
        problems.append({'reason':'source snapshot files omitted','paths':missing})
    for doc in [ROOT/'README.md',*[p for p in (ROOT/'docs').glob('*.md')]]:
        for target in re.findall(r'!?\[[^\]]*\]\(([^)]+)\)',doc.read_text(encoding='utf-8')):
            if re.match(r'[A-Za-z]+:',target) or target.startswith('#'):
                continue
            target = target.split('#')[0].strip('<>')
            if not (doc.parent/target).exists():
                problems.append({'path':str(doc.relative_to(ROOT)),'reason':'broken local link','target':target})
    images = sorted((ROOT/'docs/screenshots').rglob('*.png'))
    if len(images) < 2 or any(not path.read_bytes().startswith(b'\x89PNG\r\n\x1a\n') for path in images):
        problems.append({'reason':'missing/invalid screenshot PNGs'})
    print(json.dumps({'status':'FAIL' if problems else 'PASS','files':len(files),
                      'bytes':total,'source_manifest_files':len(manifest['files']),
                      'screenshots':len(images),'problems':problems},indent=2))
    return bool(problems)


if __name__ == '__main__':
    raise SystemExit(main())
