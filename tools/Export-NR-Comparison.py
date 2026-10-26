"""Export lossless same-frame NR input/output images and identical pixel crops.

This converts recorded BGRA buffers; it does not synthesize, sharpen, retouch,
resample, or color-grade comparison pixels. Pillow and NumPy are required only
for this optional documentation exporter, not for launching the game.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,required=True)
    p.add_argument('--prefix',default='nr_000')
    p.add_argument('--name',required=True)
    p.add_argument('--description',required=True)
    p.add_argument('--crop',action='append',default=[],help='label:x0,y0,x1,y1; source pixels, no resize')
    a = p.parse_args()
    if not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*',a.name) or not re.fullmatch(r'nr_\d{3}',a.prefix):
        p.error('use a lowercase hyphenated name and nr_NNN prefix')
    capture = a.capture.resolve()
    run = json.loads((capture/'run.json').read_text(encoding='utf-8'))
    env = run.get('diagnostic_environment',{})
    if env.get('THEORDER_NEURAL') != '1' or env.get('THEORDER_NR_GUIDES_ONLY') == '1':
        p.error('capture was not an NR-enabled evaluation run')
    width,height = 1920,1080
    raw = {}
    for label in ('before','after'):
        source = capture/f'{a.prefix}_{label}_rgba8_{width}x{height}.raw'
        data = source.read_bytes()
        if len(data) != width*height*4:
            p.error('raw image size mismatch: '+source.name)
        raw[label] = (source,data)
    if raw['before'][1] == raw['after'][1]:
        p.error('identical buffers: do not publish as a working NR comparison')
    log = (capture/'stdout.log').read_text(encoding='utf-8',errors='replace')
    successful = re.findall(r'^.*(?:Neural frame evaluated successfully|DOAX3 NR:.*neural evaluations, evaluated).*$',log,re.M)
    if not successful:
        p.error('no successful NR evaluation evidence in the run log')
    crops = []
    for spec in a.crop:
        label,coords = spec.split(':',1)
        box = tuple(map(int,coords.split(',')))
        if not re.fullmatch(r'[a-z0-9-]+',label) or len(box)!=4 or not (0<=box[0]<box[2]<=width and 0<=box[1]<box[3]<=height):
            p.error('invalid crop '+spec)
        crops.append((label,box))
    destination = ROOT/'docs/screenshots/comparisons'/a.name
    if destination.exists():
        p.error('comparison directory exists; preserve it or choose a new name')
    destination.mkdir(parents=True)
    exported = {}
    images = {}
    for label,name in (('before','native'),('after','nr')):
        # The legacy raw filename says rgba8; the host uses VideoOut BGRA order.
        image = Image.frombytes('RGBA',(width,height),raw[label][1],'raw','BGRA').convert('RGB')
        images[name] = np.asarray(image).astype(np.int16)
        path = destination/(name+'.png')
        image.save(path)
        exported[path.name] = {'sha256':sha(path.read_bytes()),'width':width,'height':height}
        for crop_label,box in crops:
            crop = image.crop(box)
            path = destination/(name+'-'+crop_label+'.png')
            crop.save(path)
            exported[path.name] = {'sha256':sha(path.read_bytes()),'width':crop.width,'height':crop.height,'source_box_xyxy':list(box)}
    delta = np.abs(images['native']-images['nr'])
    index = int(a.prefix.split('_')[1])
    schedule = run.get('capture_schedule',{}).get('neural',[])
    report = {
        'description':a.description,'capture_run':capture.name,'capture_prefix':a.prefix,
        'started_utc':run.get('started_utc'),
        'scheduled_seconds_after_launch':schedule[index] if index<len(schedule) else None,
        'pairing':'same frame, copies immediately before and after the native NR pass, before UI composition',
        'labels':{'native':'native scene buffer immediately before NR; not an independently launched NR-off run',
                  'nr':'same frame after the experimental Feature18 NR pass'},
        'dimensions':[width,height],'raw_byte_layout':'BGRA8',
        'nr_strength':env.get('THEORDER_NR_STRENGTH') or 'default1',
        'object_motion':env.get('THEORDER_OBJECT_MOTION'),
        'frame_generation':False,'host_sha256':run.get('exe_sha256'),
        'guest_sha256':run.get('guest_sha256'),
        'runtime_libraries':{key:{'sha256':value['sha256']} for key,value in run.get('runtime_libraries',{}).items()},
        'raw_sources':{label:{'file':value[0].name,'sha256':sha(value[1])} for label,value in raw.items()},
        'processing':'lossless BGRA-to-RGB PNG conversion; identical integer pixel crops; no resizing, sharpening, color grading or generative editing',
        'difference_sanity_check':{'mean_absolute_rgb_delta_0_to_255':float(delta.mean()),
                                   'changed_pixel_fraction':float(np.any(delta,axis=2).mean()),
                                   'note':'measures changed pixels, not image quality'},
        'successful_nr_log_messages':len(successful),'exported_files':exported
    }
    (destination/'capture.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'destination':str(destination),'files':list(exported),
                      'difference':report['difference_sanity_check']},indent=2))


if __name__ == '__main__':
    main()
