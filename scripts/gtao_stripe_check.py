#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: flat-slab stripe/contact regression using disposable presets and raw AO dumps.
import json
from pathlib import Path
import numpy as np
import audit_common as audit
from gfx_buffer_audit import read_dump

OUT=Path('/tmp/opencode/gtao_stripe_check')
QUANTUM=1/255


def matte(value):
    if isinstance(value,dict):
        for key in value:
            if key=='metalness': value[key]=0
            elif key=='roughness': value[key]=1
            else: matte(value[key])
    elif isinstance(value,list):
        for item in value: matte(item)


def metrics(values):
    a=values[::-1,:,0]
    if not np.isfinite(a).all():
        raise RuntimeError('nonfinite GTAO dump')
    h,w=a.shape
    flat=a[int(h*.78):int(h*.89),int(w*.34):int(w*.41)]
    rows=flat.mean(axis=1)
    cube=a[int(h*.45):int(h*.52),int(w*.54):int(w*.68)]
    sphere=a[int(h*.66):int(h*.73),int(w*.19):int(w*.30)]
    return {'flat_mean':float(flat.mean()),'row_range':float(np.ptp(rows)),
            'row_std':float(rows.std()),'row_step':float(np.abs(np.diff(rows)).max()),
            'cube_p10':float(np.percentile(cube,10)),'sphere_p10':float(np.percentile(sphere,10))}


def capture(name,bias,seed,buffer):
    preset=json.loads((audit.ROOT/'resources/tools/gfx_editor.json').read_text())
    preset['camera']={'position':[0,12,8],'target':[0,0,0],'up':[0,1,0],'fovy':50,'projection':0}
    for key in ['ENABLE_SSR','ENABLE_REFLECTIONS','ENABLE_LIGHTING_SPECULAR']:
        preset['state'][key]=False
    preset['state']['ENABLE_SSAO']=True
    preset['ssao'].update(useGTAO=True,useSSGI=False,bias=bias)
    matte(preset)
    directory=OUT/(name+'_'+buffer)
    directory.mkdir(parents=True,exist_ok=True)
    path=directory/'fixture.json'
    path.write_text(json.dumps(preset)+'\n')
    env=audit.scene_env({'DISPLAY':':1','GX_GFX_PRESET_LOAD':str(path),'GX_NO_AE':'1',
        'GX_T_ORACLESEED':str(seed),'GX_RT_VIEW':buffer+':1:0','GX_RT_DUMP_DIR':str(directory),
        'GX_RT_DUMP_FRAME':'90','GX_RT_DUMP_COUNT':'1'},clean=True)
    run=audit.run_scene('--gfx',env,width=1800,height=1010,fps=None,shot_frame=90,
        screenshot_dir=directory,extra_args=('--gfx-editor',),timeout=180)
    (directory/'renderer.log').write_text(run.stdout or '')
    return metrics(read_dump(directory/(buffer+'_000090.gxrt')))


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    legacy=audit.ROOT/'screenshot000.png'
    existed=legacy.exists()
    original=legacy.read_bytes() if existed else None
    results={}
    try:
        with audit.settings_preserved():
            for name,bias,seed in [('authored_static',.025,0),('authored_temporal',.025,1),('zero_bias_control',0,0)]:
                for buffer in ['ssao','ssaoBlur']:
                    m=capture(name,bias,seed,buffer)
                    results[name+'_'+buffer]=m
                    print(name,buffer,m,flush=True)
                    if name!='zero_bias_control':
                        if m['row_range']>QUANTUM or m['row_step']>QUANTUM or m['flat_mean']<1-QUANTUM:
                            raise RuntimeError('flat-slab stripes exceed one output-code quantum')
                        if buffer=='ssaoBlur' and (m['cube_p10']>=.9 or m['sphere_p10']>=.9):
                            raise RuntimeError('contact shadows washed out')
            if results['zero_bias_control_ssaoBlur']['row_range']<=QUANTUM:
                raise RuntimeError('zero-bias self-occlusion negative control no longer exercised')
    finally:
        if existed: legacy.write_bytes(original)
        elif legacy.exists(): legacy.unlink()
        (OUT/'report.json').write_text(json.dumps(results,indent=2)+'\n')
    print('GTAO_STRIPES: PASS — authored bias, static/rotating taps, contact shadows and zero-bias negative')


if __name__=='__main__':
    main()
