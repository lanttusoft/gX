#!/usr/bin/env python3
#ai: source-grounded selector/fade witnesses; no GPU cascade coverage certificate.
import argparse
from fractions import Fraction as Q
import json
import math
import os
from pathlib import Path
import subprocess
from planar_geometry_check import ROOT,close
from reprojection_residual_check import require


def select(count,splits,distance):
    a,b,c=splits
    if count<=1 or a<=0:
        return 0
    if distance>c and count>=4:
        return 3
    if distance>b and count>=3:
        return 2
    return 1 if distance>a else 0


def smooth(u):
    u=min(max(u,Q(0)),Q(1))
    return u*u*(3-2*u)


def mix(previous,current,weight):
    return (1-weight)*previous+weight*current


def source_check():
    for path,token in [('resources/shader/standard.frag','vDist'),
                       ('resources/shader/gfx_terrain_depth.frag','vDist'),
                       ('resources/shader/gfx_fog_vol_gen.frag','t')]:
        text=(ROOT/path).read_text()
        for phrase in ['cascadeCount > 1 && csmSplits.x > 0.0',
                       token+' > csmSplits.z && cascadeCount >= 4',
                       token+' > csmSplits.y && cascadeCount >= 3',token+' > csmSplits.x']:
            require(phrase in text,'cascade source changed: '+path+' '+phrase)
        if token=='vDist':
            require('smoothstep(boundary, boundary * (1.0 + 0.1 * csmBlendScale), vDist)' in text,
                    'cascade blend source changed: '+path)
    shadow=(ROOT/'resources/shader/common/shadow.glsl').read_text()
    require('fragPosLightSpace.xyz / fragPosLightSpace.w' in shadow and 'projCoords.z > 1.0' in shadow,
            'shadow projection source changed')
    require('projCoords.z < 0.0' not in shadow,'lower-depth guard added: review model')


def check_dump(text):
    cases=[]
    projection={}
    seen=set()
    for line in text.splitlines():
        if line.startswith('CSM_CONTRACT '):
            _,count,layer,*fields=line.split()
            count,layer=int(count),int(layer)
            require(len(fields)==4 and 1<=count<=4,'bad scalar fixture')
            floats=list(map(float.fromhex,fields))
            require(all(map(math.isfinite,floats)),'nonfinite CSM values')
            distance,u,weight,mixed=map(Q.from_float,floats)
            require((count,distance) not in seen,'duplicate CSM fixture')
            seen.add((count,distance))
            require(layer==select(count,[15,50,120],distance) and layer<count,'native selection mismatch')
            boundary=15 if layer==1 else 50 if layer==2 else 120
            close([u],[min(max((distance-boundary)/(Q(boundary)/10),0),1)])
            close([weight,mixed],[smooth(u),mix(Q.from_float(0.20000000298023224),Q.from_float(0.800000011920929),weight)])
            require(0<=weight<=1 and 0<=mixed<=1,'fade range failed')
            cases.append({'count':count,'distance':float(distance),'layer':layer,'weight':float(weight)})
        elif line.startswith('CSM_PROJECTION '):
            _,index,*fields=line.split()
            index=int(index)
            require(index in (0,1,2) and index not in projection and len(fields)==4,'bad projection fixture')
            values=list(map(float.fromhex,fields))
            require(all(map(math.isfinite,values)),'nonfinite clip')
            x,y,z,w=map(Q.from_float,values)
            require(w>0,'nonpositive ortho w')
            uv=[(v/w+1)/2 for v in (x,y,z)]
            for coord in (x,y,z):
                require((0<=(coord/w+1)/2<=1)==(-w<=coord<=w),'positive-w bound equivalence failed')
            accepts=uv[2]<=1 and 0<=uv[0]<=1 and 0<=uv[1]<=1
            require(accepts==(index!=1),'production center guard mismatch')
            if index==2:
                require(uv[2]<0 and accepts,'lower-depth guard limitation not exercised')
            projection[index]={'uv_depth':list(map(float,uv)),'center_guard_accepts':accepts}
    require(len(cases)==36 and len(projection)==3,'missing CSM fixtures')
    return {'selection_fade':cases,'projection':projection}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,default=ROOT/'build/gX_build')
    p.add_argument('--report',type=Path)
    args=p.parse_args()
    source_check()
    run=subprocess.run([str(args.binary.resolve()),'--test-only'],cwd=ROOT,
        env=dict(os.environ,GX_MATH_DUMP='1',GX_CSM_CONTRACT_DUMP='1',GX_NO_SETTINGS_SAVE='1'),
        capture_output=True,text=True,check=True,timeout=60)
    report={'result':'pass','fixtures':check_dump(run.stdout),'limits':
        'CPU selector/fade is a transcription, not production GLSL execution. Fog ray-distance differs from surface view-depth. Strict ordered splits and finished preceding fade required for boundary agreement. GPU coverage/filtering/precision external.'}
    if args.report:
        args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('CSM_CONTRACTS: PASS — 36 CPU selector/fade and three native ortho fixtures')


if __name__=='__main__':
    main()
