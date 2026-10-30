#!/usr/bin/env python3
#ai: extract the real camera-center policy and all three CPU loops; GPU coverage stays separate.
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = Path('/tmp/opencode/weather_follow_check')


def extract(source, name):
    match = re.search(r'(?:static )?gX_\w+ '+name+r'\(.*?^}', source, re.S|re.M)
    if not match:
        raise RuntimeError('Missing production function '+name)
    return match[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=ROOT/'src/renderer/gfx/weather.c')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    source = args.source.read_text()
    update = extract(source, 'gX_RendererGFX_Weather_Update')
    center = update[update.index('    gX_F32 dx ='):update.index('    if (gfx->state.ENABLE_SNOWFALL && weather->targetCount')]
    loops = []
    for flag, field, active in [('SNOWFALL', 'targetCount', 'activeCount'),
        ('RAIN', 'rainTarget', 'rainActive'), ('FOGSPRITE', 'fogTarget', 'fogActive')]:
        start = update.index(f'    if (gfx->state.ENABLE_{flag} && weather->{field} > 0')
        end = update.index(f'    else weather->{active} = 0;', start)+len(f'    else weather->{active} = 0;')
        loops.append(update[start:end])
    helpers = extract(source,'Weather_VerticalBand')+'\n' if 'Weather_VerticalBand' in source else ''
    helpers += '\n'.join(extract(source, name) for name in
        ['Weather_NextRandom', 'Weather_RespawnFlake', 'Weather_RespawnRain', 'Weather_RespawnFog'])
    if 'static gX_F32 Weather_Wrap' in source:
        helpers += '\n'+extract(source, 'Weather_Wrap')
    harness = '''//ai: actual production CPU loop bodies, zero-dt geometry witnesses and real random helpers.
#include "src/renderer/gfx/weather.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
struct gX_RendererGFX {
    gX_RendererGFX_Weather weather;
    struct { gX_I1 ENABLE_SNOWFALL, ENABLE_RAIN, ENABLE_FOGSPRITE; } state;
};
struct { struct { gX_F32v3 position, target; } camera3D; } camera;
'''+helpers+'''
static gX_I0 Sim(gX_RendererGFX *gfx, gX_F32 step)
{
    gX_RendererGFX_Weather *weather=&gfx->weather;
'''+center+'\n'.join(loops)+'''
}
int main(void)
{
    gX_RendererGFX G={0};
    gX_F32 X[3][8],Y[3][8],Z[3][8],Aux[8]={0},Speed[8],Upload[8];
    G.state.ENABLE_SNOWFALL=G.state.ENABLE_RAIN=G.state.ENABLE_FOGSPRITE=1;
    G.weather.targetCount=G.weather.rainTarget=G.weather.fogTarget=8;
    G.weather.halfExtent=3; G.weather.floorY=-10; G.weather.spawnTop=10;
    G.weather.posX=X[0]; G.weather.posY=Y[0]; G.weather.posZ=Z[0];
    G.weather.rainPosX=X[1]; G.weather.rainPosY=Y[1]; G.weather.rainPosZ=Z[1];
    G.weather.fogPosX=X[2]; G.weather.fogPosY=Y[2]; G.weather.fogPosZ=Z[2];
    G.weather.flakePhase=Aux; G.weather.flakeSpeed=Speed;
    G.weather.flakeSize=G.weather.flakeAlpha=G.weather.rainAlpha=Aux;
    G.weather.snowUpload=G.weather.rainUpload=G.weather.fogUpload=Upload;
    const gX_F32v3 Positions[]={{0,2,0},{100,2,-75},{-1000,2,3000},{9,2,-9},{4,2,5}};
    const gX_F32v3 Targets[]={{0,0,-2},{0,0,-2},{-1000,0,2998},{0,0,0},{-90,0,30}};
    gX_I32 Errors=0, Witnesses=0;
    for (gX_U32 Case=0; Case<5; Case++)
    {
        camera.camera3D.position=Positions[Case]; camera.camera3D.target=Targets[Case];
        for(gX_U32 Layer=0;Layer<3;Layer++) for(gX_U32 Particle=0;Particle<8;Particle++)
        {
            X[Layer][Particle]=(gX_F32)Particle*.2f;
            Z[Layer][Particle]=-(gX_F32)Particle*.1f;
            Y[Layer][Particle]=1.5f; Speed[Particle]=1;
        }
        gX_U32 RainSeed=G.weather.rainRng, FogSeed=G.weather.fogRng;
        Sim(&G,0);
        const gX_I32 Counts[]={G.weather.activeCount,G.weather.rainActive,G.weather.fogActive};
        for(gX_U32 Layer=0;Layer<3;Layer++) for(gX_I32 Particle=0;Particle<Counts[Layer];Particle++)
        {
            gX_F32 Extent=G.weather.halfExtent*(Layer==2?2:1);
            if(fabsf(X[Layer][Particle]-Positions[Case].x)>Extent+1e-4f
                || fabsf(Z[Layer][Particle]-Positions[Case].z)>Extent+1e-4f
                || Y[Layer][Particle]!=1.5f) Errors++;
            if(Case==0 && (X[Layer][Particle]!=(gX_F32)Particle*.2f
                || Z[Layer][Particle]!=-(gX_F32)Particle*.1f)) Errors++;
            Witnesses++;
        }
        if(G.weather.rainRng!=RainSeed || G.weather.fogRng!=FogSeed) Errors++;
        G.state.ENABLE_SNOWFALL=G.state.ENABLE_RAIN=G.state.ENABLE_FOGSPRITE=0;
        Sim(&G,0);
        if(G.weather.activeCount || G.weather.rainActive || G.weather.fogActive) Errors++;
        G.state.ENABLE_SNOWFALL=G.state.ENABLE_RAIN=G.state.ENABLE_FOGSPRITE=1;
    }
    printf("WEATHER_FOLLOW: witnesses=%d errors=%d (fixed-target, paired move, orbit, jumps, Y/seeds/toggles)\\n",Witnesses,Errors);
    return Errors?1:0;
}
'''
    path = OUT/'follow.c'; path.write_text(harness)
    binary = OUT/'follow'
    result = subprocess.run(['cc','-std=c11','-O0','-ffunction-sections','-fdata-sections',
        '-I'+str(ROOT),str(path),str(ROOT/'src/gxmath/random.c'),
        '-Wl,--gc-sections','-lm','-o',str(binary)],capture_output=True,text=True)
    (OUT/'compile.log').write_text(result.stdout+result.stderr)
    if result.returncode:
        raise RuntimeError('Weather follow harness compilation failed')
    subprocess.run([str(binary)],check=True)


if __name__ == '__main__':
    main()
