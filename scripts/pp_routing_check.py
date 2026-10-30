#!/usr/bin/env python3
#ai: asymmetric quadrant/notch GPU evidence for actual postprocess DrawTo routing.
import argparse
import json
import re
from pathlib import Path
import numpy as np
from PIL import Image
import audit_common as audit


def inspect(output):
    rows=[]
    for case in range(16):
        source=Image.open(output/f'case{case}_input.png').convert('RGB')
        result=Image.open(output/f'case{case}_output.png').convert('RGB')
        reference=np.asarray(source.resize(result.size,Image.Resampling.BILINEAR),dtype=np.float32)
        actual=np.asarray(result,dtype=np.float32)
        if case in (4,5): reference=255-reference
        if case == 12: reference[:,:,0] *= 128/255
        height,width=actual.shape[:2]
        errors=[]
        for y in (0.25,0.75):
            for x in (0.25,0.75):
                cy,cx=int(y*height),int(x*width)
                radius=3
                region=(slice(cy-radius,cy+radius),slice(cx-radius,cx+radius))
                errors.append(float(np.abs(actual[region]-reference[region]).max()))
        delta=float(np.abs(actual-reference).mean())
        #ai: constant interiors lie outside the 4x4-tap support; five RGBA8 steps
        # bound four normalized passes plus one bilinear presentation quantization.
        passed=max(errors)<=5.0 and actual.std()>20
        if case not in (3,7,14,15): passed=passed and delta<3.0
        if case in (3,7,14,15): passed=passed and delta>0.1
        rows.append({'case':case,'corner_max_error':max(errors),'mean_error':delta,
                     'passed':bool(passed),'output_size':result.size})
    return rows


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path('/tmp/opencode/pp_routing_check'))
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    legacy=audit.ROOT/'screenshot000.png'
    saved=legacy.read_bytes() if legacy.exists() else None
    try:
        with audit.settings_preserved():
            env=audit.scene_env({'GX_PP_ROUTING_CHECK':str(args.output.resolve()),
                'GX_PP_VIEW':'1','GX_FIXED_DT':'0.0166666667'},clean=True)
            run=audit.run_scene('--gfx',env,width=800,height=600,fps=None,frames=4,timeout=120)
            (args.output/'renderer.log').write_text(run.stdout or '')
            if (run.stdout or '').count('export=1')!=32:
                raise RuntimeError('PP routing fixture exports missing; see renderer.log')
            scopes=re.findall(r'PP_ROUTING_SCOPE: case=(\d+) before=(\d+) after=(\d+)',run.stdout or '')
            if len(scopes)!=16 or any(before!=after for _,before,after in scopes):
                raise RuntimeError('PP routing GL scope mismatch; see renderer.log')
            targets=re.findall(r'PP_ROUTING_TARGET: stage=(\w+) expected=(\d+) actual=(\d+) '
                r'size=(\d+)x(\d+) viewport=(\d+),(\d+),(\d+),(\d+)',run.stdout or '')
            if not targets or any(expected!=actual or x!='0' or y!='0' or (w,h)!=(vw,vh)
                for _,expected,actual,w,h,x,y,vw,vh in targets):
                raise RuntimeError('PP routing actual FBO/viewport mismatch; see renderer.log')
            rows=inspect(args.output)
            (args.output/'metrics.json').write_text(json.dumps(rows,indent=2)+'\n')
            for row in rows: print('PP_ROUTING:',row)
            if not all(row['passed'] for row in rows): raise RuntimeError('postprocess routing regression')
    finally:
        if saved is not None: legacy.write_bytes(saved)
        elif legacy.exists(): legacy.unlink()
    print('PP_ROUTING: PASS — empty/default/input, half-scale blur, odd inversion, zero tail/all-zero, feedback and canvas alias')


if __name__=='__main__': main()
