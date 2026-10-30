#!/usr/bin/env python3
#ai: explicit source-drift guards, not AST equivalence or a float/GPU certificate.
import argparse
from pathlib import Path
from weather_follow_check import extract, ROOT


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--weather',type=Path,default=ROOT/'src/renderer/gfx/weather.c')
    parser.add_argument('--godray',type=Path,default=ROOT/'resources/shader/gfx_godray_gen.frag')
    args=parser.parse_args()
    weather=args.weather.read_text()
    def require(source,fragments,label):
        normalized=' '.join(source.split())
        for fragment in fragments:
            if ' '.join(fragment.split()) not in normalized:
                raise RuntimeError(label+' source contract drift: '+fragment)
    require(extract(weather,'Weather_Wrap'),[
        'if (HalfExtent <= 0.0f) return Center;',
        'gX_F32 Offset = Position - Center;',
        'if (fabsf(Offset) <= HalfExtent) return Position;',
        'gX_F32 Width = HalfExtent * 2.0f;',
        'Offset = fmodf(Offset + HalfExtent, Width);',
        'if (Offset < 0.0f) Offset += Width;',
        'return Center + Offset - HalfExtent;'],'Weather wrap')
    require(extract(weather,'Weather_VerticalBand'),[
        'gX_F32 Height = weather->spawnTop - weather->floorY;',
        'if (Height <= 0.0f) Height = 1.0f;',
        '*Bottom = fmaxf(weather->floorY, camera.camera3D.position.y - Height * 0.5f);',
        '*Top = fmaxf(weather->floorY + Height, camera.camera3D.position.y + Height * 0.5f);'],'Weather band')
    for name,field in [('Weather_RespawnFlake','posY'),('Weather_RespawnRain','rainPosY')]:
        require(extract(weather,name),[
            'Weather_VerticalBand(weather, &Bottom, &Top);',
            f'if (initial) weather->{field}[index] = Bottom + pickThree * (Top - Bottom);',
            f'else weather->{field}[index] = Top + pickThree * 1.0f;'],'Precipitation spawn')
    require(extract(weather,'Weather_RespawnFog'),[
        'weather->fogPosY[index] = camera.camera3D.position.y - 1.80f + pickThree * 2.80f;'],'Fog spawn')
    require(extract(weather,'gX_RendererGFX_Weather_Update'),[
        'gX_F32 centerX = camera.camera3D.position.x;',
        'gX_F32 centerZ = camera.camera3D.position.z;',
        'weather->fogPosY[index] = Weather_Wrap(weather->fogPosY[index], camera.camera3D.position.y - 0.40f, 1.40f);'],'Eye anchor')
    require(extract(weather,'gX_RendererGFX_Weather_Draw'),[
        'gX_F32 heightFade = 1.50f - (cy - camera.camera3D.position.y + 2.0f) / 3.0f;',
        'if (heightFade > 1.0f) heightFade = 1.0f;',
        'if (heightFade < 0.30f) heightFade = 0.30f;'],'Fog fade')
    require(args.godray.read_text(),[
        'float stepUV = min(dist * 4.0 / max(float(rayDensity), 1.0), dist * 0.25);',
        'vec2 dir = delta / dist;',
        'if (dist < 0.001)',
        'vec2 u4 = clamp(uv + dir * stepUV * 4.0, 0.0, 1.0);'],'Godray taps')
    print('WEATHER_GODRAY_CONTRACT: PASS — explicit source/formula drift guards (native/GPU obligations separate)')


if __name__=='__main__':
    main()
