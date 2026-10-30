#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: owned clip-boundary, cache-lifetime and exact HDR regressions for mesh culling.
import argparse
import copy
import json
import struct
import subprocess
from pathlib import Path

import numpy as np
import audit_common as audit
from glb_material_maps_check import fixture
from gfx_authoring_check import label_fixture


def native(out):
    out.mkdir(parents=True, exist_ok=True)
    model = (audit.ROOT/'src/renderer/gfx/model.c').read_text()
    start = model.index('static gX_I0 gfx_model_mesh_bounds_clear(')
    helpers = model[start:model.index('//ai: zero-default label offset', start)]
    header = audit.ROOT/'src/renderer/gfx/mesh_visibility.h'
    source = '#include "'+str(header)+'"\n#include <assert.h>\n#include <stdlib.h>\n'+r'''
typedef struct gX_RendererGFX_Model
{
    gX_Model model;
    gX_F32 *meshBounds;
    gX_Mesh *meshBoundsOwner;
    gX_I32 meshBoundsCount;
} gX_RendererGFX_Model;
static gX_I32 Allocations, Frees, Fail;
static void *allocate(size_t count, size_t size)
{
    Allocations++;
    return Fail ? NULL : calloc(count, size);
}
static void release(void *value)
{
    if (value) Frees++;
    free(value);
}
#define calloc allocate
#define free release
#define gX_TraceLog(...) ((void)0)
'''+helpers+r'''
#undef calloc
#undef free
int main(void)
{
    gX_Matrix identity = {0};
    identity.m0 = identity.m5 = identity.m10 = identity.m15 = 1;
    gX_F32 box[6] = {-10,-10,-10,10,10,10};
    assert(!gfx_mesh_bounds_outside(box, identity));
    for (gX_I32 axis = 0; axis < 3; axis++)
    {
        for (gX_I32 side = 0; side < 2; side++)
        {
            gX_F32 outside[6] = {-0.5f,-0.5f,-0.5f,0.5f,0.5f,0.5f};
            outside[axis] = side ? 2 : -3;
            outside[axis+3] = side ? 3 : -2;
            assert(gfx_mesh_bounds_outside(outside, identity));
        }
    }
    gX_F32 touch[6] = {1,-0.5f,-0.5f,2,0.5f,0.5f};
    assert(!gfx_mesh_bounds_outside(touch, identity));
    gX_F32 flat[6] = {0,0,0,0,0,0};
    assert(!gfx_mesh_bounds_outside(flat, identity));
    gX_Matrix shift = identity;
    shift.m12 = 3;
    assert(gfx_mesh_bounds_outside(flat, shift));
    gX_Matrix crossing = identity;
    crossing.m3 = 0.2f;
    crossing.m15 = 0;
    assert(!gfx_mesh_bounds_outside(box, crossing));
    box[0] = NAN;
    assert(!gfx_mesh_bounds_outside(box, identity));
    identity.m0 = INFINITY;
    assert(!gfx_mesh_bounds_outside(flat, identity));
    gX_F32 vertices[] = {-1,-2,0,1,2,0};
    gX_Mesh meshes[2] = {{0},{0}};
    meshes[0].vertices = meshes[1].vertices = vertices;
    meshes[0].vertexCount = meshes[1].vertexCount = 2;
    gX_F32 bounds[6];
    assert(gfx_mesh_bounds_build(meshes, bounds) && bounds[0] == -1 && bounds[4] == 2);
    meshes[0].boneWeights = vertices;
    assert(!gfx_mesh_bounds_build(meshes, bounds));
    assert(!gfx_mesh_bounds_outside(bounds, shift));
    meshes[0].boneWeights = NULL;
    meshes[0].animVertices = vertices;
    assert(!gfx_mesh_bounds_build(meshes, bounds));
    meshes[0].animVertices = NULL;
    vertices[0] = NAN;
    assert(!gfx_mesh_bounds_build(meshes, bounds));
    vertices[0] = -1;
    gX_RendererGFX_Model first = {0}, second = {0};
    first.model.meshes = second.model.meshes = meshes;
    first.model.meshCount = second.model.meshCount = 1;
    gfx_model_mesh_bounds_ensure(&first);
    assert(first.meshBounds && Allocations == 1);
    gfx_model_mesh_bounds_ensure(&first);
    assert(Allocations == 1);
    gfx_model_mesh_bounds_ensure(&second);
    assert(second.meshBounds != first.meshBounds && Allocations == 2);
    first.model.meshCount = 2;
    gfx_model_mesh_bounds_ensure(&first);
    assert(first.meshBoundsCount == 2 && Frees == 1 && Allocations == 3);
    first.model.meshes = meshes + 1;
    first.model.meshCount = 1;
    gfx_model_mesh_bounds_ensure(&first);
    assert(first.meshBoundsOwner == meshes + 1 && Frees == 2 && Allocations == 4);
    gfx_model_mesh_bounds_clear(&first);
    gfx_model_mesh_bounds_clear(&first);
    assert(!first.meshBounds && !first.meshBoundsOwner && !first.meshBoundsCount);
    Fail = 1;
    gfx_model_mesh_bounds_ensure(&first);
    assert(!first.meshBounds);
    Fail = 0;
    gfx_model_mesh_bounds_ensure(&first);
    assert(first.meshBounds);
    gfx_model_mesh_bounds_clear(&first);
    gfx_model_mesh_bounds_clear(&second);
    assert(Frees == 5);
    return 0;
}
'''
    path = out/'native.c'
    path.write_text(source)
    subprocess.run(['cc', '-std=gnu11', '-O1', '-fsanitize=address,undefined',
                    str(path), '-lm', '-o', str(out/'native')], check=True)
    subprocess.run([str(out/'native')], check=True)
    for signature in ('gX_I32 gX_RendererGFX_Model_Load(', 'gX_I32 gX_RendererGFX_Model_Unload('):
        body = model[model.index(signature):]
        assert 'gfx_model_mesh_bounds_clear(model);' in body[:220]
    assert 'gX_Model_FrustumTest' not in model


def owned_model(path, blended=False):
    fixture(path, water_material=blended)
    content = path.read_bytes()
    length = struct.unpack_from('<I', content, 12)[0]
    document = json.loads(content[20:20+length])
    binary = bytearray(content[28+length:])
    for index, primitive in enumerate(document['meshes'][0]['primitives']):
        accessor = document['accessors'][primitive['attributes']['POSITION']]
        view = document['bufferViews'][accessor['bufferView']]
        offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
        values = np.frombuffer(binary, '<f4', count=accessor['count']*3, offset=offset).reshape(-1,3)
        if index == 0:
            values[:,0] -= values[:,0].mean()
            values[:,:2] *= 30
        elif index == 1:
            values[:,0] += 100
        else:
            values[:,2] = -20
        accessor['min'] = values.min(axis=0).tolist()
        accessor['max'] = values.max(axis=0).tolist()
    encoded = json.dumps(document,separators=(',',':')).encode()
    encoded += b' ' * (-len(encoded)%4)
    path.write_bytes(struct.pack('<4sII',b'glTF',2,28+len(encoded)+len(binary))
                     +struct.pack('<I4s',len(encoded),b'JSON')+encoded
                     +struct.pack('<I4s',len(binary),b'BIN\0')+binary)


def gpu(binary, out):
    out.mkdir(parents=True, exist_ok=True)
    report = {}
    for case in ('opaque', 'blended', 'transformed', 'taa', 'reflections'):
        model = out/(case+'.glb')
        owned_model(model, case == 'blended')
        base = label_fixture()
        base['state'].update(ENABLE_FRUSTUM_CULLING=False, ENABLE_SHADOWS=False,
                            ENABLE_LIGHTING_AMBIENT=True, ENABLE_LIGHTING_DIFFUSE=True,
                            ENABLE_LIGHTING_SPECULAR=False, ENABLE_MAP_ALBEDO=True, shaderQuality=1)
        base['sun'] = {'dir':[0,0,1], 'color':[255,255,255,255], 'brightness':2,
                       'diffuseFactor':1, 'specularFactor':0}
        base['camera'] = {'position':[0,0,-8], 'target':[0,0,0], 'up':[0,1,0], 'fovy':45, 'projection':0}
        base['models'] = [{'path':str(model), 'params':{'pos':[0,0,0], 'scale':[1,1,1],
                           'textVisible':False, 'usePBR':True, 'hasAlbedoMap':True}}]
        if case == 'blended':
            base['models'][0]['params'].update(enableBlending=True, blendOpacity=0.5)
        if case == 'transformed':
            base['models'][0]['params'].update(pos=[1,0,0], scale=[-1,0.8,1.2], rotation=[0,25,0])
            base['camera']['position'] = [0,0,8]
            base['sun']['dir'] = [0,0,-1]
        if case == 'taa': base['state']['ENABLE_TAA'] = True
        if case == 'reflections': base['state']['ENABLE_REFLECTIONS'] = True
        arrays = []
        for enabled in (False, True):
            preset = copy.deepcopy(base)
            preset['state']['ENABLE_FRUSTUM_CULLING'] = enabled
            tag = case+'_'+str(int(enabled))
            source = out/(tag+'.json')
            source.write_text(json.dumps(preset))
            dump = out/(tag+'.bin')
            result = audit.run_scene('--gfx', audit.scene_env({'GX_GFX_PRESET_LOAD':str(source),
                                    'GX_FIXED_DT':'0.0166666667', 'GX_T_HDRDUMP':'1',
                                    'GX_HDRDUMP_FRAME':'39', 'GX_HDRDUMP_PATH':str(dump)}, clean=True),
                                    binary=binary, width=800, height=600, frames=40, fps=None,
                                    extra_args=('--gfx-editor','--seed','424242'), check=True)
            (out/(tag+'.log')).write_text(audit.strip_ansi(result.stdout or ''))
            data = dump.read_bytes()
            assert data[:8] == b'GXHDR1\0\0'
            array = np.frombuffer(data,'<f4',offset=20).reshape(600,800,4)
            assert np.isfinite(array).all() and array[...,:3].max() > 0.1
            arrays.append(array)
        delta = np.abs(arrays[0]-arrays[1])
        report[case] = {'max':float(delta.max()), 'mean':float(delta.mean()), 'changed':int((delta>0).sum())}
        print(case, report[case], flush=True)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    if any(value['changed'] for value in report.values()):
        raise RuntimeError('Culling changed owned HDR output: '+str(report))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary',type=Path,default=audit.ROOT/'build/gX_build')
    parser.add_argument('--out',type=Path,default=Path('/tmp/opencode/gfx_mesh_culling_check'))
    parser.add_argument('--native-only',action='store_true')
    args = parser.parse_args()
    native(args.out.resolve()/'native')
    if not args.native_only:
        with audit.settings_preserved(): gpu(args.binary.resolve(),args.out.resolve()/'gpu')
    print('GFX_MESH_CULLING_CHECK: PASS',flush=True)
