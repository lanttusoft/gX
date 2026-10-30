#!/usr/bin/env python3
#ai: scalar radial-depth contract witnesses; actual GPU storage/filter error remains supplied.
import argparse
from fractions import Fraction as Q
import json
import math
import os
from pathlib import Path
import subprocess

from planar_geometry_check import ROOT,close
from reprojection_residual_check import require


def blocked(receiver,bias,sample,far):
    require(far>0,'nonpositive far plane outside contract')
    return receiver-bias>sample*far


def error_bound(receiver,measured,bias,caster,sample,far):
    require(far>0,'nonpositive far plane outside contract')
    er,es=abs(measured-receiver),abs(sample-caster/far)
    ideal=receiver-bias-caster
    actual=measured-bias-sample*far
    bound=er+far*es
    require(abs(actual-ideal)<=bound,'comparison error containment failed')
    if ideal>bound:
        require(actual>0,'robust shadow implication failed')
    if ideal<=-bound:
        require(actual<=0,'robust lit implication failed')
    return bound


def source_check():
    capture=(ROOT/'resources/shader/gfx_pointdepth.frag').read_text()
    surface=(ROOT/'resources/shader/common/shadow.glsl').read_text()
    fog=(ROOT/'resources/shader/gfx_fog_vol_gen.frag').read_text()
    require('length(worldPos - lightPos)' in capture and 'gl_FragDepth = depth / farPlane;' in capture,'radial capture source changed')
    for phrase in ['float currentDepth = length(lightToFrag);','currentDepth - bias > stored * farPlane',
                   'currentDepth - bias > d * farPlane','return shadow / 5.0;','for (int s = 0; s < 4; s++)']:
        require(phrase in surface,'surface point-depth source changed: '+phrase)
    require('currentDepth - fogPointShadowBias > stored * farPlane' in fog,'fog compare source changed')
    draw=(ROOT/'src/renderer/gfx/draw.c').read_text()
    start=draw.index('static gX_I0 gX_RendererGFX_PointShadow_Draw')
    end=draw.index('//opencode: restore the main shader',start)
    body=draw[start:end]
    use=body.index('gX_RHI_Shader_Use(gfx->pointDepth.shader.shader.id)')
    upload=body.index('gfx->pointDepth.farPlaneLoc, light->shadowFar')
    require(use<upload,'capture shader must activate before far upload')
    require('gX_RHI_Depth_Func(gX_RHI_LESS)' in body,'point-depth test source changed')
    shader=(ROOT/'src/renderer/gfx/shader.c').read_text()
    require('farPlane = lightPtrs[li].shadowFar;' in shader and 'd[20] = light->shadowFar;' in shader,
            'receiver far upload source changed')
    fog_upload=(ROOT/'src/renderer/gfx/fog_volume.c').read_text()
    require('gfx->fogVolume.ptShadowTiles[n * 4 + 1] = l->shadowFar;' in fog_upload,'fog far upload source changed')


def check_dump(text):
    expected={'self':False,'blocked':True,'bias_equal':False,'clear_inside':False,'clear_beyond':True,'rounded':True}
    cases={}
    for line in text.splitlines():
        if not line.startswith('POINT_SHADOW_DEPTH '):
            continue
        _,name,*fields=line.split()
        require(name in expected and name not in cases and len(fields)==15,'bad native fixture')
        f=list(map(float.fromhex,fields))
        require(all(math.isfinite(x) for x in f),'nonfinite native scalar')
        q=list(map(Q.from_float,f))
        far,bias,caster,receiver,encoded,stored,decoded,biased,flag=q[6:]
        require(far>0 and 0<=caster<=far,'capture range premise missing')
        close([caster,receiver],[math.hypot(*f[:3]),math.hypot(*f[3:6])])
        close([encoded,decoded,biased],[caster/far,stored*far,receiver-bias])
        require(flag in (0,1) and bool(flag)==(biased>decoded)==expected[name],'native comparison mismatch')
        ideal_sample=far if name.startswith('clear_') else caster
        bound=error_bound(receiver,receiver,bias,ideal_sample,stored,far)
        exact_margin=receiver-bias-stored*far
        cpu_margin=biased-decoded
        cpu_error=abs(cpu_margin-exact_margin)
        total=bound+cpu_error
        ideal=receiver-bias-ideal_sample
        require(abs(cpu_margin-ideal)<=total,'native combined error containment failed')
        if ideal>total:
            require(flag==1,'native robust shadow failed')
        if ideal<=-total:
            require(flag==0,'native robust lit failed')
        cases[name]={'caster_radius':float(caster),'receiver_radius':float(receiver),'far':float(far),
            'bias':float(bias),'encoded':float(encoded),'stored':float(stored),'shadow':bool(flag),
            'encoding_decoding_bound':float(bound),'measured_cpu_compare_error':float(cpu_error),
            'total_measured_bound':float(total),'ideal_margin':float(ideal),
            'robust_margin_certified':ideal>total or ideal<=-total}
    require(set(cases)==set(expected),'missing native fixtures')
    return cases


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,default=ROOT/'build/gX_build')
    p.add_argument('--report',type=Path)
    args=p.parse_args()
    source_check()
    run=subprocess.run([str(args.binary.resolve()),'--test-only'],cwd=ROOT,
        env=dict(os.environ,GX_MATH_DUMP='1',GX_POINT_SHADOW_DEPTH_DUMP='1',GX_NO_SETTINGS_SAVE='1'),
        capture_output=True,text=True,check=True,timeout=60)
    report={'result':'pass','cases':check_dump(run.stdout),'limits':
        'Native stored radii supplied; CPU sqrt is checked with existing tolerance, not universally proved. No actual depth-texture quantization/filtering, GPU compare rounding or raster coverage certificate.'}
    if args.report:
        args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('POINT_SHADOW_DEPTH: PASS — six native radial/bias/clear/error witnesses')


if __name__=='__main__':
    main()
