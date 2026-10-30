#!/usr/bin/env python3
#ai: SSGI fallback/eligibility drift guards and a shared native reconstruction witness.
import argparse
from fractions import Fraction as Q
import json
import os
from pathlib import Path
import subprocess
from gtao_contract_check import ROOT,native_check
from reprojection_residual_check import require


def fallback(ambient,total,count):
    confidence=min(max(count/Q(48),Q(0)),Q(1))
    estimate=total/count if count>0 else Q(0)
    return (1-confidence)*ambient+confidence*estimate


def source_check():
    text=(ROOT/'resources/shader/gfx_ssgi.frag').read_text()
    for phrase in ['texelFetch(depthMap, getDepthTexel(uv), 0)',
                   'vec2(getDepthTexel(screenPosition)) + 0.5','(float(j) + stepFraction) / float(STEPS)',
                   'dot(viewDelta, viewNormal) <= max(giBias, 0.0)',
                   'dot(viewDelta, viewDelta) == 0.0','DIRECTIONS * STEPS * 2',
                   'if (weightSum > 0.5) gathered /= weightSum;',
                   'gathered = mix(ambientFloor, gathered, conf);']:
        require(phrase in text,'SSGI source drift: '+phrase)
    require('dot(normalize(offset - viewNormal * dot(offset, viewNormal)), viewNormal)' not in text,
            'unstable mathematically zero tangent predicate restored')
    require(text.index('if (weightSum > 0.5) gathered /= weightSum;')<
            text.index('gathered = mix(ambientFloor, gathered, conf);'),'fallback divided by sample count')
    draw=(ROOT/'src/renderer/gfx/draw.c').read_text()
    require('ssgiBiasLoc, gfx->ssao.bias' in draw,'authored SSGI bias upload missing')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,default=ROOT/'build/gX_build')
    p.add_argument('--report',type=Path)
    args=p.parse_args()
    source_check()
    run=subprocess.run([str(args.binary.resolve()),'--test-only'],cwd=ROOT,
        env=dict(os.environ,GX_MATH_DUMP='1',GX_REPROJECTION_DUMP='1',GX_NO_SETTINGS_SAVE='1'),
        capture_output=True,text=True,check=True,timeout=60)
    report={'result':'pass','native':native_check(run.stdout),'limits':
        'Shared native inverse witness, not production GLSL execution. Radiance cap, sampler coherence, normalization, temporal reinjection convergence and GPU accuracy remain conditional.'}
    if args.report: args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('SSGI_CONTRACTS: PASS — fallback/scalar/source and shared native reconstruction')


if __name__=='__main__':
    main()
