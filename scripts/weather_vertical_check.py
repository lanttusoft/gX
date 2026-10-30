#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: native vertical-follow witnesses with a fixed precipitation floor; no GPU/state substitutes.
import argparse
from pathlib import Path
import subprocess
from weather_follow_check import extract, ROOT

OUT = Path('/tmp/opencode/weather_vertical_check')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=ROOT/'src/renderer/gfx/weather.c')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    source = args.source.read_text()
    update = extract(source, 'gX_RendererGFX_Weather_Update')
    start = update.index('    gX_F32 dx =')
    end = update.index('    if (gfx->state.ENABLE_SNOWFALL && weather->targetCount')
    simulation = update[start:end]
    for flag, field, active in [('SNOWFALL','targetCount','activeCount'),
        ('RAIN','rainTarget','rainActive'),('FOGSPRITE','fogTarget','fogActive')]:
        start = update.index(f'    if (gfx->state.ENABLE_{flag} && weather->{field} > 0')
        end = update.index(f'    else weather->{active} = 0;',start)+len(f'    else weather->{active} = 0;')
        simulation += update[start:end]+'\n'
    helpers = ''
    for name in ['Weather_VerticalBand','Weather_NextRandom','Weather_RespawnFlake',
        'Weather_RespawnRain','Weather_RespawnFog','Weather_Wrap']:
        if name in source:
            helpers += extract(source,name)+'\n'
    harness = '''//ai: extracted production CPU paths, actual random implementation, finite height witnesses.
#include "src/renderer/gfx/weather.h"
#include <math.h>
#include <stdio.h>
struct gX_RendererGFX {
    gX_RendererGFX_Weather weather;
    struct {gX_I1 ENABLE_SNOWFALL,ENABLE_RAIN,ENABLE_FOGSPRITE;} state;
};
struct {struct {gX_F32v3 position,target;} camera3D;} camera;
'''+helpers+'''
static gX_I0 Sim(gX_RendererGFX *gfx,gX_F32 step)
{
    gX_RendererGFX_Weather *weather=&gfx->weather;
'''+simulation+'''
}
int main(void)
{
    gX_RendererGFX G={0};
    gX_F32 X[3][8]={0},Y[3][8],Z[3][8]={0},Aux[8]={0},Speed[8],Upload[8];
    G.state.ENABLE_SNOWFALL=G.state.ENABLE_RAIN=G.state.ENABLE_FOGSPRITE=1;
    G.weather.targetCount=G.weather.rainTarget=G.weather.fogTarget=8;
    G.weather.halfExtent=3; G.weather.floorY=0; G.weather.spawnTop=6;
    G.weather.posX=X[0]; G.weather.posY=Y[0]; G.weather.posZ=Z[0];
    G.weather.rainPosX=X[1]; G.weather.rainPosY=Y[1]; G.weather.rainPosZ=Z[1];
    G.weather.fogPosX=X[2]; G.weather.fogPosY=Y[2]; G.weather.fogPosZ=Z[2];
    G.weather.flakePhase=Aux; G.weather.flakeSpeed=Speed;
    G.weather.flakeSize=G.weather.flakeAlpha=G.weather.rainAlpha=Aux;
    G.weather.snowUpload=G.weather.rainUpload=G.weather.fogUpload=Upload;
    gX_Random32_Seed(&G.weather.rng,1337,0);
    G.weather.rainRng=7331;
    const gX_F32 Heights[]={2,100,-20,50,2};
    gX_I32 Errors=0,Witnesses=0;
    for(gX_U32 Case=0;Case<5;Case++)
    {
        camera.camera3D.position=(gX_F32v3){0,Heights[Case],0};
        camera.camera3D.target=(gX_F32v3){0,Heights[Case],-6};
        for(gX_U32 Layer=0;Layer<3;Layer++) for(gX_U32 Particle=0;Particle<8;Particle++)
        {
            Y[Layer][Particle]=Case==0?1.5f:Heights[Case-1]+1.0f;
            Speed[Particle]=1;
        }
        Sim(&G,0);
        const gX_I32 Counts[]={G.weather.activeCount,G.weather.rainActive,G.weather.fogActive};
        for(gX_U32 Layer=0;Layer<3;Layer++) for(gX_I32 Particle=0;Particle<Counts[Layer];Particle++)
        {
            gX_F32 Low=Layer==2?Heights[Case]-1.8f:fmaxf(0,Heights[Case]-3);
            gX_F32 High=Layer==2?Heights[Case]+1:fmaxf(6,Heights[Case]+3)+1;
            if(Y[Layer][Particle]<Low-1e-4f || Y[Layer][Particle]>High+1e-4f) Errors++;
            if(Case==0 && Y[Layer][Particle]!=1.5f) Errors++;
            Witnesses++;
        }
        if(G.weather.floorY!=0 || G.weather.spawnTop!=6) Errors++;
    }
    camera.camera3D.position=(gX_F32v3){0,2,0};
    camera.camera3D.target=(gX_F32v3){0,2,-6};
    G.weather.fallSpeed=1; G.weather.rainFall=2;
    for(gX_U32 Particle=0;Particle<8;Particle++)
    {
        Y[0][Particle]=Y[1][Particle]=Y[2][Particle]=3;
        X[0][Particle]=X[1][Particle]=X[2][Particle]=0;
        Z[0][Particle]=Z[1][Particle]=Z[2][Particle]=0;
        Speed[Particle]=1;
    }
    Sim(&G,.25f);
    for(gX_U32 Particle=0;Particle<8;Particle++)
    {
        if(fabsf(Y[0][Particle]-2.75f)>1e-5f || fabsf(Y[1][Particle]-2.5f)>1e-5f
            || fabsf(Y[2][Particle]-3)>1e-5f) Errors++;
    }
    printf("WEATHER_VERTICAL: witnesses=%d errors=%d (up/down jumps, world floor, authored ceiling, in-band preservation)\\n",Witnesses,Errors);
    return Errors?1:0;
}
'''
    path=OUT/'vertical.c'; path.write_text(harness)
    binary=OUT/'vertical'
    result=subprocess.run(['cc','-std=c11','-O0','-ffunction-sections','-fdata-sections',
        '-I'+str(ROOT),str(path),str(ROOT/'src/gxmath/random.c'),'-Wl,--gc-sections',
        '-lm','-o',str(binary)],capture_output=True,text=True)
    (OUT/'compile.log').write_text(result.stdout+result.stderr)
    if result.returncode: raise RuntimeError('Vertical weather witness compilation failed')
    subprocess.run([str(binary)],check=True)


if __name__=='__main__':
    main()
