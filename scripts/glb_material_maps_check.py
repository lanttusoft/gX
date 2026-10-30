#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: generated owned GLBs test per-mesh embedded albedo selection without third-party assets.
import argparse
import io
import json
from pathlib import Path
import struct
import tempfile

import numpy as np
from PIL import Image
import audit_common as audit
from gfx_authoring_check import label_fixture
from water_ripple_check import compile_run


#ai: exercise the production source-table mapper, including native defaults and invalid indices.
def native_mapping(out):
    out.mkdir(parents=True, exist_ok=True)
    header = audit.ROOT/'src/renderer/gfx/material_contract.h'
    source = '#include <assert.h>\n#include "'+str(header)+'"\n'+r'''
int main(void)
{
    for (int native = 0; native <= 8; native++)
        for (int source = 0; source <= 8; source++)
            for (int index = -1; index <= native + 1; index++)
            {
                int mapped = gfx_glb_material_index(index, native, source);
                int prefix = native > source ? native - source : 0;
                if (index < 0 || index >= native || source == 0 || index < prefix)
                    assert(mapped == -1);
                else
                    assert(mapped == index - prefix && mapped < source);
            }
    assert(gfx_glb_material_index(0, 4, 3) == -1);
    assert(gfx_glb_material_index(1, 4, 3) == 0);
    assert(gfx_glb_material_index(3, 4, 3) == 2);
    assert(gfx_glb_material_index(2, 6, 4) == 0);
    assert(gfx_glb_material_index(0, 3, 3) == 0);
    assert(gfx_glb_material_index(0, -1, 3) == -1);
    assert(gfx_glb_material_index(0, 3, -1) == -1);
    assert(gfx_glb_material_index(2147483646, 2147483647, 2147483647) == 2147483646);
    assert(gfx_glb_material_index(12, 177, 176) == 11);
    assert(gfx_glb_material_index(41, 177, 176) == 40);
    assert(gfx_glb_material_index(176, 177, 176) == 175);
    assert(gfx_glb_material_index(177, 177, 176) == -1);
    return 0;
}
'''
    compile_run(out, 'material_mapping', source)
    draw = (audit.ROOT/'src/renderer/gfx/model.c').read_text()
    if 'gfx_glb_material_index((gX_I32)matIdx,' not in draw:
        raise RuntimeError('Native draw stopped using its tested file-material mapper')
    if 'model->glbData->materials[matIdx].blendOpacity' in draw:
        raise RuntimeError('Native opacity lookup reverted to unadjusted material index')


#ai: compile exact draw-local upload snippets; resets, repeated alpha and repair phase stay observable.
def native_uploads(out):
    out.mkdir(parents=True, exist_ok=True)
    draw = (audit.ROOT/'src/renderer/gfx/model.c').read_text()
    start = draw.index('gX_I1 HaveOpacity = 0;')
    setup = draw[start:draw.index('for (gX_I32 mi = 0;', start)]
    start = draw.index('gX_F32 Opacity = needBlend ? bFac : 1.0f;', start)
    upload = draw[start:draw.index('gX_Color meshAlbedo =', start)]
    source = '#include <assert.h>\n#include "'+str(audit.ROOT/'src/renderer/gfx/material_contract.h')+'"\n'+r'''
typedef struct { struct { gX_I32 roughnessAlphaLoc, blendOpacityLoc; } locations; } ShaderState;
typedef struct { struct { gX_I1 transparentRoughnessPass; } state; } GfxState;
static gX_I32 FloatCalls, FlagCalls, LastFlag;
static gX_F32 Uploaded[16];
static void gX_Shader_SetValue_Float(gX_I32 program, gX_I32 location, gX_F32 value)
{
    assert(program == 7 && location == 13 && FloatCalls < 16);
    Uploaded[FloatCalls++] = value;
}
static void gX_Shader_SetValue_Int(gX_I32 program, gX_I32 location, gX_I32 value)
{
    assert(program == 7 && location == 809);
    FlagCalls++;
    LastFlag = value;
}
static void draw_values(const gX_F32 *values, gX_I32 count, gX_I1 repair,
    gX_I1 blended)
{
    ShaderState shader = {{809, 13}};
    ShaderState *rshader = &shader;
    GfxState state = {{repair}};
    GfxState *gfx = &state;
    gX_I32 shaderProg = 7;
    gX_I1 BlendedMaterials = blended;
'''+setup+r'''
    for (gX_I32 index = 0; index < count; index++)
    {
        gX_F32 bFac = values[index];
        gX_I1 needBlend = bFac < 0.999f;
'''+upload+r'''
    }
}
int main(void)
{
    gX_F32 plain[] = {1, 1, 1};
    draw_values(plain, 3, 0, 1);
    assert(FloatCalls == 1 && Uploaded[0] == 1 && FlagCalls == 1 && LastFlag == 0);
    draw_values(plain, 3, 1, 1);
    assert(FloatCalls == 2 && FlagCalls == 2 && LastFlag == 1);
    gX_F32 mixed[] = {-1, -1, 0.65f, 0.65f, 1, 1, 0.65f};
    draw_values(mixed, 7, 0, 1);
    assert(FloatCalls == 6 && FlagCalls == 3 && LastFlag == 0);
    assert(Uploaded[2] == -1 && Uploaded[3] == 0.65f && Uploaded[4] == 1 && Uploaded[5] == 0.65f);
    draw_values(mixed, 7, 0, 0);
    assert(FloatCalls == 6 && FlagCalls == 3);
    return 0;
}
'''
    compile_run(out, 'material_uploads', source)


#ai: generate owned multi-material GLBs with controllable factors, maps and checker albedo.
def fixture(path, emissive_tail=False, textureless=False, water_material=False, material_order=None,
            surface_case=None, surface_uniform=False, albedo_pattern=False,
            large_material_count=0):
    binary = bytearray()
    views, accessors, primitives, images = [], [], [], []

    def view(data):
        binary.extend(b'\0' * (-len(binary) % 4))
        index = len(views)
        views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(data)})
        binary.extend(data)
        return index

    def accessor(values, kind, count):
        index = len(accessors)
        accessors.append({'bufferView': view(struct.pack('<'+'f'*len(values), *values)),
                          'componentType': 5126, 'count': count, 'type': kind})
        return index

    panel_colors = ((240, 20, 20), (20, 240, 20), (20, 20, 240))
    factor_colors = ((1.0, 0.05, 0.05, 1.0),
                     (0.05, 1.0, 0.05, 1.0),
                     (0.05, 0.05, 1.0, 1.0))
    material_indices = (11, 40, 2) if large_material_count else (0, 1, 2)
    material_count = large_material_count if large_material_count else 3
    if material_count <= max(material_indices):
        raise ValueError('large material fixture is too small for assigned indices')
    scalar_factors = {
        'metalness_factor': (0.0, 0.5, 1.0),
        'roughness_factor': (0.1, 0.5, 0.9),
    }
    map_colors = {
        'normal': ((128, 128, 255), (255, 128, 128), (128, 255, 128)),
        'metalness': ((128, 128, 0), (128, 128, 128), (128, 128, 255)),
        'roughness': ((128, 0, 0), (128, 128, 0), (128, 255, 0)),
        'ao': ((24, 24, 24), (128, 128, 128), (255, 255, 255)),
    }
    for panel, color in enumerate(panel_colors):
        x = (panel-1)*2
        points = [(x-.8, -.8, 0), (x-.8, .8, 0), (x+.8, .8, 0),
                  (x-.8, -.8, 0), (x+.8, .8, 0), (x+.8, -.8, 0)]
        pos = accessor([v for point in points for v in point], 'VEC3', 6)
        accessors[pos].update(min=[x-.8, -.8, 0], max=[x+.8, .8, 0])
        normal = accessor([0, 0, -1]*6, 'VEC3', 6)
        uv = accessor([0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0], 'VEC2', 6)
        primitives.append({'attributes': {'POSITION': pos, 'NORMAL': normal, 'TEXCOORD_0': uv},
                           'material': material_indices[panel]})
        if surface_case is None:
            image_color = color
        elif surface_case in map_colors:
            image_color = map_colors[surface_case][panel]
        else:
            image_color = None
        if image_color is not None:
            stream = io.BytesIO()
            if albedo_pattern and surface_case is None:
                yy, xx = np.indices((8, 8))
                levels = np.where((xx + yy) % 2 == 0, 1.0, 0.25)
                pixels = (np.asarray(image_color, dtype=np.float32)[None, None, :]
                    * levels[:, :, None]).clip(0, 255).astype(np.uint8)
                Image.fromarray(pixels, 'RGB').save(stream, format='PNG')
            else:
                Image.new('RGB', (8, 8), image_color).save(stream, format='PNG')
            images.append({'bufferView': view(stream.getvalue()), 'mimeType': 'image/png'})
    binary.extend(b'\0' * (-len(binary) % 4))
    materials = []
    for material_index in range(material_count):
        panel = material_indices.index(material_index) if material_index in material_indices else -1
        pbr = {'metallicFactor': 0.0, 'roughnessFactor': 1.0}
        material = {'name': 'owned_panel_'+str(panel) if panel >= 0
                    else 'owned_unused_'+str(material_index), 'doubleSided': True}
        if surface_case == 'factor':
            pbr['baseColorFactor'] = factor_colors[panel] if panel >= 0 else [1, 1, 1, 1]
        elif surface_case in scalar_factors:
            if panel >= 0:
                value = scalar_factors[surface_case][0 if surface_uniform else panel]
                if surface_case == 'metalness_factor':
                    pbr.update(metallicFactor=value, roughnessFactor=0.35)
                else:
                    pbr.update(metallicFactor=0.25, roughnessFactor=value)
        elif surface_case is None:
            if panel >= 0:
                pbr['baseColorTexture'] = {'index': panel}
        else:
            pbr['baseColorFactor'] = [1.0, 1.0, 1.0, 1.0]
            if panel >= 0 and surface_case == 'normal':
                material['normalTexture'] = {'index': 0 if surface_uniform else panel}
            elif panel >= 0 and surface_case in ('metalness', 'roughness'):
                if surface_case == 'metalness':
                    pbr['metallicFactor'] = 1.0
                pbr['metallicRoughnessTexture'] = {
                    'index': 0 if surface_uniform else panel}
            elif panel >= 0 and surface_case == 'ao':
                material['occlusionTexture'] = {'index': 0 if surface_uniform else panel}
        material['pbrMetallicRoughness'] = pbr
        materials.append(material)
    doc = {'asset': {'version': '2.0'}, 'buffers': [{'byteLength': len(binary)}],
           'bufferViews': views, 'accessors': accessors, 'images': images,
           'textures': [{'source': i} for i in range(len(images))],
           'materials': materials,
           'meshes': [{'primitives': primitives}], 'nodes': [{'mesh': 0}],
           'scenes': [{'nodes': [0]}], 'scene': 0}
    if emissive_tail:
        doc['materials'].append({'name': 'owned_hidden_emitter', 'emissiveFactor': [1, 1, 1]})
        doc['meshes'].append({'primitives': [dict(primitives[0], material=3) for _ in range(130)]})
        doc['nodes'] = [{'mesh': 1, 'translation': [1000, 0, 0]}, {'mesh': 0}]
        doc['scenes'][0]['nodes'] = [0, 1]
    if textureless:
        del doc['materials'][2]['pbrMetallicRoughness']['baseColorTexture']
    if water_material:
        doc['materials'][1]['name'] = 'owned_water_panel'
    if material_order is not None:
        original = doc['materials']
        doc['materials'] = [original[i] for i in material_order]
        for mesh in doc['meshes']:
            for primitive in mesh['primitives']:
                primitive['material'] = material_order.index(primitive['material'])
    encoded = json.dumps(doc).encode()
    encoded += b' ' * (-len(encoded) % 4)
    chunks = struct.pack('<II', len(encoded), 0x4E4F534A)+encoded
    chunks += struct.pack('<II', len(binary), 0x004E4942)+binary
    path.write_bytes(struct.pack('<4sII', b'glTF', 2, len(chunks)+12)+chunks)


#ai: this +Z-facing camera projects the +X third panel to screen left.
#ai: explicit directional lighting makes material probes independent of default-scene light values.
def checks(binary, out, emissive_tail=False, textureless=False, injection=False,
           water_material=False, material_order=None, model_blending=False,
           surface_case=None, surface_uniform=False, large_material_count=0,
           balanced_reflection=None, reflections=False):
    out.mkdir(parents=True, exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(prefix='run_', dir=out))
    model = run_dir/'owned.glb'
    fixture(model, emissive_tail, textureless, water_material, material_order,
            surface_case, surface_uniform, large_material_count=large_material_count)
    preset = label_fixture()
    preset['sky'].update(color=[255, 255, 255, 255], strength=1.0)
    preset['state'].update(ENABLE_SHADOWS=False, ENABLE_LIGHTING_AMBIENT=True,
                           ENABLE_LIGHTING_DIFFUSE=True, ENABLE_LIGHTING_SPECULAR=False,
                           ENABLE_MAP_ALBEDO=True, shaderQuality=1)
    if surface_case == 'normal':
        preset['state']['ENABLE_MAP_NORMAL'] = True
    if surface_case in ('metalness', 'roughness'):
        preset['state']['ENABLE_MAP_METALNESS'] = True
    preset['sun'] = {'dir': [0, 0, 1], 'color': [255, 255, 255, 255],
                     'brightness': 2.0, 'diffuseFactor': 1.0, 'specularFactor': 0.0}
    preset['camera'] = {'position': [0, 0, -8], 'target': [0, 0, 0],
                        'up': [0, 1, 0], 'fovy': 45, 'projection': 0}
    preset['models'] = [{'path': str(model), 'params': {'pos': [0, 0, 0],
                         'scale': [1, 1, 1], 'textVisible': False, 'usePBR': True,
                         'hasAlbedoMap': True, 'albedoTint': [255, 255, 255, 255],
                          'specularColor': [0, 0, 0, 255]}}]
    #ai: owned optional reflection controls preserve every existing material-map fixture default.
    if balanced_reflection is not None:
        preset['models'][0]['params']['balancedReflection'] = balanced_reflection
    if reflections:
        preset['state']['ENABLE_REFLECTIONS'] = True
        preset['models'][0]['params']['enableReflections'] = True
    if surface_case in ('normal', 'metalness', 'roughness', 'ao',
                         'metalness_factor', 'roughness_factor'):
        preset['models'][0]['params']['specularColor'] = [255, 255, 255, 255]
    if surface_case == 'ao':
        preset['sun']['brightness'] = 0.1
    if injection:
        texture = run_dir/'owned_override.png'
        Image.new('RGB', (8, 8), (240, 20, 240)).save(texture)
        preset['textures'] = [{'path': str(texture)}]
        preset['models'][0]['textureID'] = [0]+[-1]*7
        preset['models'][0]['texturePurpose'] = [0]+[-1]*7
    if model_blending:
        preset['models'][0]['params'].update(enableBlending=True, blendOpacity=0.5)
    source = run_dir/'input.json'
    source.write_text(json.dumps(preset))
    dump = run_dir/'hdr.bin'
    result = audit.run_scene('--gfx', audit.scene_env({'GX_GFX_PRESET_LOAD': str(source),
                            'GX_FIXED_DT': '0.0166666667', 'GX_T_HDRDUMP': '1',
                            'GX_HDRDUMP_FRAME': '29', 'GX_HDRDUMP_PATH': str(dump)}, clean=True),
                            binary=binary, width=800, height=600, frames=30, fps=None,
                            extra_args=('--gfx-editor',), check=True)
    (run_dir/'renderer.log').write_text(audit.strip_ansi(result.stdout or ''))
    data = dump.read_bytes()
    assert data[:8] == b'GXHDR1\0\0' and struct.unpack_from('<III', data, 8) == (800, 600, 4)
    rgba = np.frombuffer(data, '<f4', offset=20).reshape(600, 800, 4)
    rgb = rgba[..., :3]
    assert np.isfinite(rgba).all()
    Image.fromarray(np.clip(rgb[::-1]*255, 0, 255).astype('uint8')).save(run_dir/'hdr.png')
    counts = []
    for channel in range(3):
        other = np.max(np.delete(rgb, channel, axis=2), axis=2)
        counts.append(int(((rgb[..., channel] > 0.05) & (rgb[..., channel] > other*3)).sum()))
    samples = {str(x): rgb[300, x].tolist() for x in (220, 400, 580)}
    (run_dir/'report.json').write_text(json.dumps({'dominant_rgb_pixels': counts,
                     'center_rgb': samples}, indent=2)+'\n')
    if water_material:
        return rgba
    if surface_case == 'factor':
        centers = [rgb[300, x] for x in (220, 400, 580)]
        expected = (2, 1, 0)
        valid = all(sample[axis] > 0.1
                    and sample[axis] > max(sample[(axis + 1) % 3],
                                           sample[(axis + 2) % 3]) * 2.0
                    for sample, axis in zip(centers, expected))
        if not valid:
            raise RuntimeError('Per-material '+surface_case+' did not affect panel samples: '
                               +str([sample.tolist() for sample in centers]))
        print('GLB_MATERIAL_SURFACE_CHECK: PASS', surface_case,
              [sample.tolist() for sample in centers])
        return rgb
    if surface_case:
        return rgb
    valid = min(counts) >= 1000
    if textureless:
        patch = rgb[280:320, 200:240]
        valid = min(counts[:2]) >= 1000 and bool((patch.min(axis=2) > 0.2).all())
        valid = valid and bool((np.ptp(patch, axis=2) < 0.01).all())
    if injection:
        valid = all(bool(((rgb[280:320, x-20:x+20, 0] > 0.2)
                          & (rgb[280:320, x-20:x+20, 2] > 0.2)
                          & (rgb[280:320, x-20:x+20, 1] < 0.05)).all())
                    for x in (220, 400, 580))
    if not valid:
        raise RuntimeError('Embedded per-mesh albedo lost: '+str(counts)+' evidence '+str(run_dir))
    print('GLB_MATERIAL_MAPS_CHECK: PASS', {'rgb': counts, 'emissive_tail': emissive_tail,
          'textureless': textureless, 'injection': injection})
    return rgb


#ai: same-owned-geometry A/B makes each packed material map independently observable.
def surface_material_check(binary, out, surface_case):
    valid_cases = ('normal', 'metalness', 'roughness', 'ao',
                   'metalness_factor', 'roughness_factor')
    if surface_case not in valid_cases:
        raise ValueError('unknown material surface case: '+str(surface_case))
    varied = checks(binary, out/'varied', surface_case=surface_case)
    uniform = checks(binary, out/'uniform', surface_case=surface_case,
                     surface_uniform=True)
    deltas = []
    for x in (220, 400, 580):
        varied_patch = varied[292:308, x-8:x+8]
        uniform_patch = uniform[292:308, x-8:x+8]
        deltas.append(float(np.abs(varied_patch-uniform_patch).mean()))
    #ai: first material is the identical control; other panels must vary with their own factors or maps.
    if max(deltas[1:]) <= 0.005:
        raise RuntimeError('Per-material '+surface_case+' map did not change its panels: '
                           +str(deltas))
    (out/'surface_map_report.json').write_text(json.dumps(
        {'case': surface_case, 'panel_mean_abs_delta': deltas}, indent=2)+'\n')
    print('GLB_MATERIAL_SURFACE_SLOT_CHECK: PASS', surface_case, deltas)


#ai: a large GLB keeps raylib's default prefix separate from gX's 32-entry metadata cache.
def large_material_surface_check(binary, out):
    rgba = checks(binary, out, surface_case='factor',
                  large_material_count=176)
    rgb = rgba[..., :3]
    centers = [rgb[300, x] for x in (220, 400, 580)]
    expected = (2, 1, 0)
    valid = all(sample[axis] > 0.1
                and sample[axis] > max(sample[(axis + 1) % 3],
                                       sample[(axis + 2) % 3]) * 2.0
                for sample, axis in zip(centers, expected))
    if not valid:
        raise RuntimeError('Large-GLB factors failed for cached/uncached materials: '
                           +str([sample.tolist() for sample in centers]))
    (out/'large_material_surface_report.json').write_text(json.dumps(
        {'source_material_count': 176, 'metadata_cache_count': 32,
         'panel_rgb': [sample.tolist() for sample in centers]}, indent=2)+'\n')
    print('GLB_LARGE_MATERIAL_SURFACE_CHECK: PASS',
          [sample.tolist() for sample in centers])


#ai: equivalent material-table permutations must not move the file-authored opacity to a neighbour.
def opacity_check(binary, out, model_blending=False):
    reference = checks(binary, out/'reference', water_material=True, model_blending=model_blending)
    metrics = {}
    failures = []
    for order in ((1, 2, 0), (2, 0, 1)):
        name = ''.join(map(str, order))
        candidate = checks(binary, out/name, water_material=True, material_order=order,
                           model_blending=model_blending)
        delta = np.abs(reference-candidate)
        metrics[name] = {'max': float(delta.max()), 'mean': float(delta.mean()),
                         'changed_1e-5': int((delta.max(axis=2)>1e-5).sum())}
        if metrics[name]['max'] > 0.0011:
            failures.append(name)
    (out/'opacity_report.json').write_text(json.dumps(metrics, indent=2)+'\n')
    if failures:
        raise RuntimeError('Material permutation changed opacity: '+str(failures)+' '+str(metrics))
    print('GLB_OPACITY_CHECK: PASS material-table permutation invariance', metrics)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, default=audit.ROOT/'build/gX_build')
    parser.add_argument('--out', type=Path, default=Path('/tmp/opencode/glb_maps'))
    parser.add_argument('--emissive-tail', action='store_true')
    parser.add_argument('--textureless', action='store_true')
    parser.add_argument('--injection', action='store_true')
    parser.add_argument('--surface-case', choices=('factor', 'normal', 'metalness',
                        'roughness', 'ao', 'metalness_factor', 'roughness_factor'))
    args = parser.parse_args()
    cases = [('albedo', {}), ('emissive_tail', {'emissive_tail': True}),
             ('textureless', {'textureless': True}), ('injection', {'injection': True}),
             ('surface_factor', {'surface_case': 'factor'})]
    if args.surface_case:
        cases = ([('surface_factor', {'surface_case': 'factor'})]
                 if args.surface_case == 'factor' else [])
    elif args.emissive_tail or args.textureless or args.injection:
        cases = [('selected', {'emissive_tail': args.emissive_tail,
                  'textureless': args.textureless, 'injection': args.injection})]
    failures = []
    with audit.settings_preserved():
        native_mapping(args.out.resolve()/'native')
        native_uploads(args.out.resolve()/'native_uploads')
        for name, flags in cases:
            try:
                checks(args.binary.resolve(), args.out.resolve()/name, **flags)
            except Exception as error:
                failures.append(name+': '+str(error))
        if not (args.emissive_tail or args.textureless or args.injection) \
                and args.surface_case in (None, 'factor'):
            try:
                large_material_surface_check(args.binary.resolve(),
                    args.out.resolve()/'large_material_surface')
            except Exception as error:
                failures.append('large_material_surface: '+str(error))
        #ai: run per-map A/B fixtures in the regular gate and in a selected case.
        map_cases = ((args.surface_case,) if args.surface_case in
                     ('normal', 'metalness', 'roughness', 'ao',
                      'metalness_factor', 'roughness_factor') else
                     ('normal', 'metalness', 'roughness', 'ao',
                      'metalness_factor', 'roughness_factor')
                     if not (args.emissive_tail or args.textureless or args.injection
                             or args.surface_case) else ())
        for surface_case in map_cases:
            try:
                surface_material_check(args.binary.resolve(),
                    args.out.resolve()/('surface_'+surface_case), surface_case)
            except Exception as error:
                failures.append('surface_'+surface_case+': '+str(error))
        if not (args.emissive_tail or args.textureless or args.injection
                or args.surface_case):
            try:
                opacity_check(args.binary.resolve(), args.out.resolve()/'opacity')
                opacity_check(args.binary.resolve(), args.out.resolve()/'opacity_blended', model_blending=True)
            except Exception as error:
                failures.append('opacity: '+str(error))
    if failures:
        raise RuntimeError('\n'.join(failures))
