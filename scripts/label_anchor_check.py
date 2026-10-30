#!/usr/bin/env python3
#ai: extracted production transform plus actual gxmath: rotation/size cannot displace the label anchor.
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = Path('/tmp/opencode/label_anchor_check')


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'src/common/utils/billboard.c').read_text()
    helper=re.search(r'static gX_Matrix billboard_transform\(.*?^}',source,re.S|re.M)
    if not helper or source.count('= billboard_transform(position, size,')!=2:
        raise RuntimeError('Both label paths must share the production transform')
    harness='''//ai: extracted production composition, linked against unchanged production gxmath.
#include "src/gxmath/matrix.h"
#include "src/gxmath/vector3.h"
#include <math.h>
#include <stdio.h>
'''+helper[0]+'''
int main(void)
{
    const gX_F32v3 Anchors[]={{2,3,5},{-7,0.5f,4},{0,0,0}};
    const gX_F32 Angles[]={0,0.25f,1.5707963f,3.1415926f,-1.2f};
    const gX_F32v2 Sizes[]={{2,0.5f},{0.25f,3},{1,1}};
    gX_U32 Count=0;
    for(gX_U32 a=0;a<3;a++) for(gX_U32 r=0;r<5;r++) for(gX_U32 s=0;s<3;s++)
    {
        gX_Matrix Matrix=billboard_transform(Anchors[a],Sizes[s],Angles[r]);
        gX_F32v3 Actual=gX_F32v3_Transform((gX_F32v3){0,0,0},Matrix);
        if(fabsf(Actual.x-Anchors[a].x)>1e-6f || fabsf(Actual.y-Anchors[a].y)>1e-6f
            || fabsf(Actual.z-Anchors[a].z)>1e-6f) return 1;
        Count++;
    }
    printf("LABEL_ANCHOR: PASS %u native origin witnesses\\n",Count);
    gX_Matrix Legacy=gX_Matrix_Scale(2,0.5f,1);
    Legacy=gX_Matrix_Multiply(gX_Matrix_RotateY(1.5707963f),Legacy);
    Legacy=gX_Matrix_Multiply(gX_Matrix_Translate(2,3,5),Legacy);
    gX_F32v3 Wrong=gX_F32v3_Transform((gX_F32v3){0,0,0},Legacy);
    if(fabsf(Wrong.x-2)<1e-6f && fabsf(Wrong.y-3)<1e-6f && fabsf(Wrong.z-5)<1e-6f) return 2;
    printf("LABEL_ANCHOR: legacy negative displaced origin=(%.6f,%.6f,%.6f) expected=(2,3,5)\\n",
        Wrong.x,Wrong.y,Wrong.z);
    return 0;
}
'''
    path=OUT/'anchor.c'
    path.write_text(harness)
    command=['cc','-std=c11','-ffunction-sections','-fdata-sections','-I'+str(ROOT),
        str(path),str(ROOT/'src/gxmath/matrix.c'),str(ROOT/'src/gxmath/vector3.c'),
        '-Wl,--gc-sections','-lm','-o',str(OUT/'anchor')]
    run=subprocess.run(command,capture_output=True,text=True)
    (OUT/'compile.log').write_text(run.stdout+run.stderr)
    if run.returncode: raise RuntimeError('Anchor witness compile failed')
    subprocess.run([str(OUT/'anchor')],check=True)


if __name__=='__main__': main()
