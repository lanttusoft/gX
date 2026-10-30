#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: isolated flat-slab SSGI stripe and nonzero localized-bounce GPU regression.
import json
from pathlib import Path
import numpy as np
import audit_common as audit
from gtao_stripe_check import matte
from gfx_buffer_audit import read_dump

OUT=Path('/tmp/opencode/ssgi_stripe_check')
QUANTUM=1/255


def capture(flat,seed,buffer):
    base=json.loads((audit.ROOT/'resources/tools/gfx_editor.json').read_text())
    base['camera']={'position':[0,12,8],'target':[0,0,0],'up':[0,1,0],'fovy':50,'projection':0}
    for key in ['ENABLE_SSR','ENABLE_REFLECTIONS','ENABLE_LIGHTING_SPECULAR']:
        base['state'][key]=False
    base['state']['ENABLE_SSAO']=True
    base['ssao'].update(useGTAO=False,useSSGI=True,bias=.025)
    matte(base)
    if flat: base['models']=[base['models'][7]]
    name=('flat' if flat else 'bounce')+'_'+str(seed)+'_'+buffer
    directory=OUT/name
    directory.mkdir(parents=True,exist_ok=True)
    path=directory/'fixture.json'
    path.write_text(json.dumps(base)+'\n')
    env=audit.scene_env({'DISPLAY':':1','GX_GFX_PRESET_LOAD':str(path),'GX_NO_AE':'1',
        'GX_T_ORACLESEED':str(seed),'GX_RT_VIEW':buffer+':1:0','GX_RT_DUMP_DIR':str(directory),
        'GX_RT_DUMP_FRAME':'90','GX_RT_DUMP_COUNT':'1'},clean=True)
    run=audit.run_scene('--gfx',env,width=1800,height=1010,fps=None,shot_frame=90,
        screenshot_dir=directory,extra_args=('--gfx-editor',),timeout=180)
    (directory/'renderer.log').write_text(run.stdout or '')
    a=read_dump(directory/(buffer+'_000090.gxrt'))[::-1,:,:3]
    if not np.isfinite(a).all(): raise RuntimeError('nonfinite SSGI')
    h,w,c=a.shape
    roi=a[int(h*.78):int(h*.89),int(w*.34):int(w*.41)]
    rows=roi.mean(axis=1)
    bounce=a[int(h*.45):int(h*.52),int(w*.54):int(w*.68)]
    return name,{'flat_mean':roi.mean(axis=(0,1)).tolist(),'row_range':np.ptp(rows,axis=0).tolist(),
        'row_step':np.abs(np.diff(rows,axis=0)).max(axis=0).tolist(),
        'bounce_p90':np.percentile(bounce,90,axis=(0,1)).tolist()}


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    legacy=audit.ROOT/'screenshot000.png'
    original=legacy.read_bytes() if legacy.exists() else None
    result={}
    try:
        with audit.settings_preserved():
            for flat,seed in [(True,0),(True,1),(False,0)]:
                for buffer in ['ssgiRaw','ssgi']:
                    name,m=capture(flat,seed,buffer)
                    result[name]=m
                    print(name,m,flush=True)
                    if flat:
                        if max(m['row_range'])>QUANTUM or max(m['row_step'])>QUANTUM:
                            raise RuntimeError('flat-only SSGI stripes exceed one output-code quantum')
                        if min(m['flat_mean'])<=0: raise RuntimeError('ambient fallback disabled')
                    else:
                        reference=result['flat_0_'+buffer]['flat_mean']
                        if not all(a>1.25*b for a,b in zip(m['bounce_p90'],reference)):
                            raise RuntimeError('localized indirect light lost')
    finally:
        if original is not None: legacy.write_bytes(original)
        elif legacy.exists(): legacy.unlink()
        (OUT/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    print('SSGI_STRIPES: PASS — flat fallback static/rotating taps and localized bounce retained')


if __name__=='__main__':
    main()
