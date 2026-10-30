#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: native routing/feature predicates and HDR moved-vs-fresh full-opacity regressions; no authored asset writes.
import argparse
import copy
import json
from pathlib import Path
import struct
import tempfile

import numpy as np

import audit_common as audit
from water_ripple_check import compile_run
from water_refraction_check import scene


def native(out):
    source = (audit.ROOT / 'src/renderer/gfx/glass_contract.h').read_text()
    function = source[source.index('static inline gX_I1'):source.rindex('#endif')]
    harness = r'''
//ai: exact finite float witnesses at opacity one and adjacent representable threshold values.
#include <assert.h>
#include <math.h>
typedef int gX_I1;typedef int gX_I32;typedef float gX_F32;
'''+function+r'''
int main(void) {
    float values[]={0,0.5f,nextafterf(0.999f,0),0.999f,nextafterf(0.999f,1),1};
    for(int q=0;q<3;++q)for(int r=0;r<2;++r)for(int b=0;b<2;++b)
        for(int i=0;i<6;++i) {
            int route=gfx_glass_post_capture(r,b,values[i],q);
            if(r && q>=1)assert(route);
            else assert(route==(b && values[i]<0.999f));
        }
    assert(gfx_glass_post_capture(1,0,1,1));
    assert(gfx_glass_post_capture(1,1,1,2));
    assert(!gfx_glass_post_capture(1,1,1,0));
}
'''
    compile_run(out, 'routing', harness)
    render = (audit.ROOT / 'src/renderer/gfx/gfx_render.c').read_text()
    draw = (audit.ROOT / 'src/renderer/gfx/draw.c').read_text()
    model = (audit.ROOT / 'src/renderer/gfx/model.c').read_text()
    pipeline = (audit.ROOT / 'src/renderer/gfx/gfx_pipeline.c').read_text()
    if 'gfx_glass_post_capture(' not in render or 'gfx_glass_post_capture(' not in draw:
        raise RuntimeError('Opaque/transparent routing no longer shares its tested predicate')
    if 'model->params.enableRefraction && gfx->debugVis.refractionReady' not in model:
        raise RuntimeError('Refraction sampling lost phase-local capture readiness')
    if 'gfx->debugVis.refractionReady = false;' not in pipeline:
        raise RuntimeError('Pipeline does not invalidate old capture readiness')
    copy_block = draw[draw.index('if (needRefraction &&'):draw.index('// Collect transparent models')]
    if 'gX_RHI_Blit(' not in copy_block or 'gX_Texture_Draw(' in copy_block:
        raise RuntimeError('Refraction copy reverted to shader/alpha-blended texture drawing')


def fixture():
    preset = scene()
    preset['state'].update(shaderQuality=1, ENABLE_WATER=False, ENABLE_SHADOWS=False,
                           ENABLE_SSR=False, ENABLE_TAA=False, ENABLE_MOTION_BLUR=False)
    preset.setdefault('ssao', {})['useSSGI'] = False
    preset['camera'].update(position=[0, 1, -8], target=[0, 0, 0])
    preset['models'] = []
    for y in range(4):
        for x in range(6):
            color = [230, 200, 90, 255] if (x+y) % 2 else [30, 70, 150, 255]
            preset['models'].append({'path': 'resources/model/primitive/cube.glb', 'params': {
                'pos': [(x-2.5)*1.1, (y-1.5)*1.1, 3], 'scale': [0.54, 0.54, 0.1],
                'textVisible': False, 'albedoTint': color, 'emissiveColor': color, 'emissiveFactor': 1}})
    preset['models'].append({'path': 'resources/model/primitive/sphere.glb', 'params': {
        'pos': [0, 0, 0], 'scale': [1.5, 1.5, 1.5], 'textVisible': False,
        'usePBR': True, 'roughness': 0.05, 'enableBlending': True, 'blendOpacity': 1.0,
        'enableRefraction': True, 'refractiveIndex': 1.5}})
    return preset, len(preset['models'])-1


def hdr(out, tag, preset, binary, moved, frame, target='hdr'):
    directory = out / tag
    directory.mkdir()
    variant = copy.deepcopy(preset)
    if not moved:
        variant['camera']['position'][0] = 0.75
    source = out / 'input.json'
    text = json.dumps(variant)
    source.write_text(text)
    (directory / 'input.json').write_text(text)
    dump = directory / 'hdr.bin'
    hooks = {'GX_FIXED_DT': '0.0166666667', 'GX_GFX_PRESET_LOAD': str(source),
             'GX_T_HDRDUMP': '1', 'GX_HDRDUMP_FRAME': str(frame), 'GX_HDRDUMP_PATH': str(dump)}
    if target != 'hdr':
        hooks['GX_HDRDUMP_RT'] = target
    if moved:
        hooks['GX_WATER_REFRACTION_TEST_TRANSITION'] = '1'
    run = audit.run_scene('--gfx', audit.scene_env(hooks, clean=True), binary=binary,
        width=1024 if moved else 800, height=768 if moved else 600,
        fps=None, frames=frame+1, extra_args=('--gfx-editor',), check=True)
    log = audit.strip_ansi(run.stdout or '')
    (directory / 'renderer.log').write_text(log)
    if moved and 'WATER_REFRACTION_TEST_TRANSITION: frame=12' not in log:
        raise RuntimeError('Native camera/resize transition did not execute')
    data = dump.read_bytes()
    if data[:8] != b'GXHDR1\0\0' or struct.unpack_from('<III', data, 8) != (800, 600, 4):
        raise RuntimeError('Wrong HDR capture identity/size')
    array = np.frombuffer(data, '<f4', offset=20).reshape(600, 800, 4)[..., :3]
    if not np.isfinite(array).all():
        raise RuntimeError('Non-finite glass HDR evidence')
    return array


def checks(out, binary, preset_path=None, keep_ao=False):
    out.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='run_', dir=out))
    native(run)
    if preset_path:
        preset = json.loads(preset_path.read_text())
        indices = [i for i, m in enumerate(preset['models']) if m['params'].get('enableRefraction')]
        if len(indices) != 1:
            raise RuntimeError('Diagnostic preset must contain exactly one refractive model')
        index = indices[0]
    else:
        preset, index = fixture()
    metrics = {}
    failures = []
    for opacity in (0.5, 1.0):
        for enabled in (False, True):
            variant = copy.deepcopy(preset)
            variant['models'][index]['params'].update(blendOpacity=opacity, enableRefraction=enabled)
            variant.setdefault('ssao', {})['useSSGI'] = False
            if not keep_ao:
                variant['ssao']['useGTAO'] = False
            for frame in (13, 29):
                tag = f'o{opacity}_r{int(enabled)}_f{frame}'
                moved = hdr(run, tag+'_moved', variant, binary, True, frame)
                fresh = hdr(run, tag+'_fresh', variant, binary, False, frame)
                delta = np.abs(moved-fresh)
                metrics[tag] = {'max': float(delta.max()), 'mean': float(delta.mean()),
                                'changed_1e-5': int((delta.max(axis=2)>1e-5).sum())}
                (run / 'report.json').write_text(json.dumps(metrics, indent=2)+'\n')
                if metrics[tag]['max'] > 0.0011:
                    failures.append(tag)
    (run / 'report.json').write_text(json.dumps(metrics, indent=2)+'\n')
    if failures:
        raise RuntimeError('HDR freshness bound failed for '+str(failures)+'; all case metrics retained at '+str(run))
    print('GLASS_REFRACTION_CHECK: PASS native routing and immediate/settled full-opacity HDR freshness')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, default=audit.ROOT / 'build/water_refraction/gX_build')
    parser.add_argument('--out', type=Path, default=Path('/tmp/opencode/grc'))
    parser.add_argument('--preset', type=Path)
    parser.add_argument('--keep-ao', action='store_true', help='retain authored GTAO for unresolved-history diagnostics')
    args = parser.parse_args()
    with audit.settings_preserved():
        checks(args.out.resolve(), args.binary.resolve(), args.preset, args.keep_ao)
