#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: production top-anchor/size policy linked to real gxmath; rotated/negative-scale/legacy witnesses.
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=Path('/tmp/opencode/label_top_check')


def extract(source, name):
    match=re.search(r'(?:static )?gX_\w+ '+name+r'\(.*?^}',source,re.S|re.M)
    if not match: raise RuntimeError('Missing production helper '+name)
    return match[0]


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    draw=(ROOT/'src/renderer/gfx/draw.c').read_text()
    model=(ROOT/'src/renderer/gfx/model.c').read_text()
    harness='''//ai: matching field types with production helper bodies, not a rendering certificate.
#include "src/gxmath/matrix.h"
#include "src/gxmath/vector3.h"
#include "src/gxmath/constants.h"
#include <math.h>
#include <stdio.h>
typedef struct {
    struct { gX_F32v3 pos,scale,rotation,textOffset; gX_F32v2 textWorldSize;
        gX_I1 textFit,textAnchorTop; gX_F32 textGap; } params;
    gX_F32 aabbMin[3],aabbMax[3]; gX_I1 aabbValid;
} gX_RendererGFX_Model;
'''+extract(model,'gX_RendererGFX_Model_WorldMatrix')+'\n'+extract(draw,'gfx_label_size')+'\n'+extract(draw,'gfx_label_position')+'''
static gX_I1 check(gX_RendererGFX_Model *M, gX_F32v3 Expected)
{
    gX_F32v2 Size=gfx_label_size(M);
    gX_F32v3 Actual=gfx_label_position(M,Size);
    return fabsf(Actual.x-Expected.x)<1e-5f && fabsf(Actual.y-Expected.y)<1e-5f
        && fabsf(Actual.z-Expected.z)<1e-5f;
}
int main(void)
{
    gX_RendererGFX_Model M={0};
    M.params.pos=(gX_F32v3){4,5,6}; M.params.scale=(gX_F32v3){2,3,4};
    M.params.textOffset=(gX_F32v3){0.5f,0.75f,-0.25f};
    M.params.textWorldSize=(gX_F32v2){2,0.5f}; M.params.textGap=0.2f;
    M.aabbMin[0]=-1; M.aabbMin[1]=-2; M.aabbMin[2]=-3;
    M.aabbMax[0]=1; M.aabbMax[1]=2; M.aabbMax[2]=3; M.aabbValid=1;
    if(!check(&M,(gX_F32v3){4.5f,5.75f,5.75f})) return 1;
    M.params.textAnchorTop=1;
    if(!check(&M,(gX_F32v3){4.5f,12.2f,5.75f})) return 2;
    M.params.rotation.z=90;
    if(!check(&M,(gX_F32v3){4.5f,8.2f,5.75f})) return 3;
    M.params.scale.x=-2; M.params.textFit=1;
    gX_F32v2 Size=gfx_label_size(&M);
    if(Size.x!=2 || Size.y!=0.25f) return 4;
    if(!check(&M,(gX_F32v3){4.5f,8.075f,5.75f})) return 5;
    M.aabbValid=0;
    if(!check(&M,(gX_F32v3){4.5f,6.075f,5.75f})) return 6;
    M.aabbValid=1; M.params.rotation.z=0; M.params.scale=(gX_F32v3){2,-3,0.5f};
    M.aabbMin[0]=-2; M.aabbMin[1]=-1; M.aabbMin[2]=-0.5f;
    M.aabbMax[0]=1; M.aabbMax[1]=3; M.aabbMax[2]=4;
    if(!check(&M,(gX_F32v3){3.5f,9.075f,6.625f})) return 7;
    puts("LABEL_TOP: PASS — legacy, top, rotation, negative scale, aspect, fallback and off-center bounds");
    return 0;
}
'''
    path=OUT/'top.c'; path.write_text(harness)
    command=['cc','-std=c11','-ffunction-sections','-fdata-sections','-I'+str(ROOT),str(path),
        str(ROOT/'src/gxmath/matrix.c'),str(ROOT/'src/gxmath/vector3.c'),
        '-Wl,--gc-sections','-lm','-o',str(OUT/'top')]
    result=subprocess.run(command,capture_output=True,text=True)
    (OUT/'compile.log').write_text(result.stdout+result.stderr)
    if result.returncode: raise RuntimeError('Top-anchor witness compile failed')
    subprocess.run([str(OUT/'top')],check=True)


if __name__=='__main__': main()
