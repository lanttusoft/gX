#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GL face algebra/native capture oracle; not a hardware cubemap seam/filter certificate.
import argparse
from fractions import Fraction as Q
import json
import math
import os
from pathlib import Path
import subprocess

from planar_geometry_check import ROOT, close
from reprojection_residual_check import require


def direction(face,s,t):
    return [(1,-t,-s),(-1,-t,s),(s,1,t),(s,-1,-t),(s,-t,1),(-s,-t,-1)][face]


def face_uv(d):
    x,y,z=d
    a,b,c=map(abs,d)
    require(max(a,b,c)>0,"zero direction has no valid face UV")
    if a>=b and a>=c:
        f=0 if x>=0 else 1
        s,t=(-z if x>=0 else z)/a,-y/a
    elif b>=c:
        f=2 if y>=0 else 3
        s,t=x/b,(z if y>=0 else -z)/b
    else:
        f=4 if z>=0 else 5
        s,t=(x if z>=0 else -x)/c,-y/c
    return f,(s+1)/2,(t+1)/2


def source_check():
    shadow=(ROOT/'resources/shader/common/shadow.glsl').read_text()
    fog=(ROOT/'resources/shader/gfx_fog_vol_gen.frag').read_text()
    require('direction.x >= 0.0 ? -direction.z : direction.z' in shadow,"surface -X mapping changed")
    require('dir.x >= 0.0 ? -dir.z : dir.z' in fog,"fog -X mapping disagrees with GL")
    for i,formula in enumerate(['1.0, -p.y, -p.x','-1.0, -p.y, p.x','p.x, 1.0, p.y',
                              'p.x, -1.0, -p.y','p.x, -p.y, 1.0','-p.x, -p.y, -1.0']):
        require('vec3('+formula+')' in shadow,f'face {i} inverse source changed')
    light=(ROOT/'src/renderer/gfx/light.c').read_text()
    import re
    nums=lambda text: tuple(float(x) for x in re.findall(r'-?\d+(?:\.\d+)?',text))
    expected_targets=[(1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1)]
    expected_ups=[(0,-1,0),(0,-1,0),(0,0,1),(0,0,-1),(0,-1,0),(0,-1,0)]
    cube=(ROOT/'src/renderer/cubemap.c').read_text()
    for name,expected in [('TARGETS',expected_targets),('UPS',expected_ups)]:
        m=re.search(r'gX_CUBEMAP_'+name+r'\[6\]\s*=\s*\{(.*?)\};',cube,re.S)
        require(m is not None and [nums(v) for v in re.findall(r'\{([^}]+)\}',m[1])]==expected,
                f'dynamic reflection {name} convention changed')
    for i in range(6):
        m=re.search(r'targets\['+str(i)+r'\].*?\{([^}]+)\}.*?ups\['+str(i)+r'\].*?\{([^}]+)\}',light)
        require(m is not None and nums(m[1])==expected_targets[i] and nums(m[2])==expected_ups[i],f'point capture frame {i} changed')
    update=(ROOT/'src/renderer/gfx/update.c').read_text()
    for name,expected in [('targets',expected_targets),('ups',expected_ups)]:
        arrays=re.findall(r'const gX_F32v3 '+name+r'\[6\]\s*=\s*\{(.*?)\};',update,re.S)
        require(len(arrays)==2,f'IBL {name} capture arrays changed')
        for array in arrays:
            require([nums(v) for v in re.findall(r'\{([^}]+)\}',array)]==expected,f'IBL {name} convention changed')


def check_dump(text):
    result=[]
    seen=set()
    for line in text.splitlines():
        if not line.startswith('CUBEMAP_GEOMETRY '):
            continue
        _,f,k,*fields=line.split()
        f,k=int(f),int(k)
        require(0<=f<6 and 0<=k<4 and (f,k) not in seen and len(fields)==10,'bad native fixture')
        seen.add((f,k))
        values=list(map(float.fromhex,fields))
        require(all(math.isfinite(x) for x in values),'nonfinite native fixture')
        s,t,*rest=map(Q.from_float,values)
        eye,clip=rest[:4],rest[4:]
        close(eye,[3*s,3*t,-3,1])
        require(clip[3]>0,'capture projection w must be positive')
        close([clip[0]/clip[3],clip[1]/clip[3]],[s,t])
        d=list(map(Q,direction(f,s,t)))
        selected,u,v=face_uv(d)
        recovered=direction(selected,2*u-1,2*v-1)
        m=max(map(abs,d))
        require(tuple(x*m for x in recovered)==tuple(d),'direction roundtrip mismatch')
        require(0<=u<=1 and 0<=v<=1,'selected UV outside face')
        if k<2:
            require(selected==f and u==(s+1)/2 and v==(t+1)/2,'interior selection mismatch')
        if k==3:
            require(selected!=f,'edge-crossing tap did not reroute')
        result.append({'face':f,'sample':k,'selected_face':selected,'uv':[float(u),float(v)],
                       'capture_ndc':[float(clip[0]/clip[3]),float(clip[1]/clip[3])]})
    require(len(seen)==24,'missing native fixtures')
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,default=ROOT/'build/gX_build')
    p.add_argument('--report',type=Path)
    args=p.parse_args()
    source_check()
    run=subprocess.run([str(args.binary.resolve()),'--test-only'],cwd=ROOT,
        env=dict(os.environ,GX_MATH_DUMP='1',GX_CUBEMAP_GEOMETRY_DUMP='1',GX_NO_SETTINGS_SAVE='1'),
        capture_output=True,text=True,check=True,timeout=60)
    report={'result':'pass','fixtures':check_dump(run.stdout),'limits':
        'Canonical 90-degree capture frames; source checks not semantic C/GLSL equivalence; GL seam ties/filtering and GPU execution unproved. Custom shadow FOV excluded.'}
    if args.report:
        args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('CUBEMAP_GEOMETRY: PASS — 24 native capture/interior/seam/cross-edge witnesses')


if __name__=='__main__':
    main()
