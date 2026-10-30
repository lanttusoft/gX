#!/usr/bin/env python3
#ai: opt-in owned transfer diagnostic pins the existing raw/composite routing contract.
import json
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image
import audit_common as audit


def main():
    fixture = Path('/tmp/opencode/lightmap_filter_check/point.json')
    if not fixture.exists():
        raise RuntimeError('Run scripts/lightmap_filter_check.py first to generate the owned fixture')
    base = json.loads(fixture.read_text())
    out = Path('/tmp/opencode/lightmap_transfer_check')
    out.mkdir(exist_ok=True)
    measurements = {}
    for label, composite, gamma in [('raw',False,True),('composite_linear',True,False),
                                     ('composite_srgb',True,True)]:
        preset = json.loads(json.dumps(base))
        preset['state'].update(ENABLE_BLOOM=composite,ENABLE_GAMMA_CORRECTION=gamma,
            ENABLE_AGX_TONEMAP=False,ENABLE_ACES_TONEMAP=False,exposure=1)
        preset['bloom'] = {'intensity':0,'vignette':0,'saturation':1,'contrast':1}
        path = out/(label+'.json')
        path.write_text(json.dumps(preset,indent=2)+'\n')
        capture = out/(label+'.png')
        env = audit.scene_env(clean=True,GX_GFX_PRESET_LOAD=str(path),
            GX_GFX_RENDER_CAMERA=str(capture),GX_GFX_RENDER_CAMERA_INDEX='0',
            GX_FIXED_DT='0.0166666667',GX_FRAMES='90',GX_NO_AE='1',GX_T_DITHER_STRENGTH='0')
        result = subprocess.run([str(audit.BIN),'--screenWidth','1024','--screenHeight','768',
            '--targetFPS','60','--gfx','--gfx-editor'],cwd=audit.ROOT,env=env,
            stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=300)
        (out/(label+'.log')).write_text(result.stdout)
        if result.returncode or not capture.exists():
            raise RuntimeError('Capture failed: '+label)
        image = np.array(Image.open(capture).convert('RGB'))
        h,w = image.shape[:2]
        roi = image[h*3//8:h*5//8,w*3//8:w*5//8]
        if len(np.unique(roi.reshape(-1,3),axis=0)) < 4:
            raise RuntimeError('Owned fixture coverage missing')
        measurements[label] = roi.mean((0,1)).tolist()
    report = {'roi_mean_rgb':measurements,
        'scope':'owned routing diagnostic, Reinhard composite with zero bloom; not Source parity'}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2),flush=True)
    if np.mean(measurements['composite_srgb']) <= np.mean(measurements['composite_linear'])+10:
        raise RuntimeError('Gamma transfer not distinguished')


if __name__ == '__main__':
    main()
