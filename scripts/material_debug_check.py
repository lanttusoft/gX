#!/usr/bin/env python3
#ai: GPU regression for exact resource/material selection in the opt-in raw albedo-map diagnostic.
import argparse
import json
from pathlib import Path
import struct

import numpy as np

import audit_common as audit
from gfx_authoring_check import label_fixture
from glb_material_maps_check import fixture


#ai: capture an exact-match or resource-mismatch material diagnostic to the linear HDR target.
def capture(binary, out, model, debug_resource, use_material_index=False):
    preset = label_fixture()
    preset['sky'].update(color=[255, 255, 255, 255], strength=1.0)
    preset['state'].update(ENABLE_REFLECTIONS=False,
        ENABLE_PLANAR_REFLECTIONS=False, ENABLE_SSR=False, ENABLE_SSAO=False,
        ENABLE_VOLUME_FOG=False, ENABLE_BLOOM=False, ENABLE_TAA=False,
        ENABLE_WATER=False, ENABLE_PROCEDURAL_SKY=False, ENABLE_SHADOWS=False,
        ENABLE_LIGHTING_AMBIENT=True, ENABLE_LIGHTING_DIFFUSE=True,
        ENABLE_LIGHTING_SPECULAR=False, ENABLE_ENV_AMBIENT=False,
        ENABLE_MAP_ALBEDO=True, ENABLE_ACES_TONEMAP=True,
        ENABLE_AGX_TONEMAP=False, ENABLE_GAMMA_CORRECTION=True,
        exposure=0.8, shaderQuality=1)
    preset['sun'] = {'dir': [0, 0, 1], 'color': [255, 255, 255, 255],
        'brightness': 1.0, 'diffuseFactor': 1.0, 'specularFactor': 0.0,
        'castShadows': False}
    preset['camera'] = {'position': [0, 0, -8], 'target': [0, 0, 0],
        'up': [0, 1, 0], 'fovy': 45, 'projection': 0}
    preset['models'] = [{'path': str(model), 'params': {
        'pos': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1],
        'textVisible': False, 'usePBR': True, 'hasAlbedoMap': True,
        'albedoTint': [255, 255, 255, 255],
        'specularColor': [0, 0, 0, 255]}}]
    preset['lights'] = []
    preset_path = out/'preset.json'
    preset_path.write_text(json.dumps(preset))
    dump_path = out/'hdr.bin'
    debugEnv = {
        'GX_GFX_PRESET_LOAD': str(preset_path),
        'GX_FIXED_DT': '0.0166666667',
        'GX_MATERIAL_DEBUG_MODE': 'baseColorMap',
        'GX_MATERIAL_DEBUG_RESOURCE': debug_resource,
        'GX_MATERIALS_OFF': '1',
        'GX_T_HDRDUMP': '1',
        'GX_HDRDUMP_FRAME': '29',
        'GX_HDRDUMP_PATH': str(dump_path),
    }
    if use_material_index:
        debugEnv['GX_MATERIAL_DEBUG_MATERIAL_INDEX'] = '1'
    else:
        debugEnv['GX_MATERIAL_DEBUG_MATERIAL'] = 'owned_panel_1'
    env = audit.scene_env(debugEnv, clean=True)
    result = audit.run_scene('--gfx', env, binary=binary, width=800, height=600,
        frames=30, fps=None, extra_args=('--gfx-editor',), check=True)
    log = audit.strip_ansi(result.stdout or '')
    (out/'renderer.log').write_text(log)
    data = dump_path.read_bytes()
    if data[:8] != b'GXHDR1\0\0' or struct.unpack_from('<III', data, 8) != (800, 600, 4):
        raise RuntimeError('Invalid linear HDR dump')
    rgba = np.frombuffer(data, '<f4', offset=20).reshape(600, 800, 4)
    return rgba[..., :3], log


#ai: verify exact resource scoping and unchanged neighboring GLB materials.
def main():
    parser = argparse.ArgumentParser(description='Check resource-scoped GLB material diagnostics')
    parser.add_argument('--binary', type=Path, default=audit.BIN)
    parser.add_argument('--out', type=Path, default=Path('/tmp/opencode/material_debug_check'))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    selected_dir = args.out/'selected'
    index_dir = args.out/'selected_index'
    ignored_dir = args.out/'ignored'
    selected_dir.mkdir(exist_ok=True)
    index_dir.mkdir(exist_ok=True)
    ignored_dir.mkdir(exist_ok=True)
    model = args.out/'owned.glb'
    fixture(model)
    with audit.settings_preserved():
        selected, selected_log = capture(args.binary.resolve(), selected_dir,
            model, str(model))
        indexed, indexed_log = capture(args.binary.resolve(), index_dir,
            model, str(model), use_material_index=True)
        ignored, ignored_log = capture(args.binary.resolve(), ignored_dir,
            model, str(model)+'.not-selected')
    if 'MATERIAL_DEBUG: mode=baseColorMap' not in selected_log:
        raise RuntimeError('Selected material did not emit its diagnostic log')
    if 'MATERIAL_DEBUG: mode=baseColorMap' not in indexed_log:
        raise RuntimeError('Selected source index did not emit its diagnostic log')
    if 'MATERIAL_DEBUG: mode=baseColorMap' in ignored_log:
        raise RuntimeError('Non-matching resource unexpectedly activated diagnostics')
    selectedPixel = selected[300, 400]
    indexedPixel = indexed[300, 400]
    ignoredPixel = ignored[300, 400]
    if selectedPixel[1] < 0.6 or selectedPixel[1] < max(selectedPixel[0], selectedPixel[2])*4.0:
        raise RuntimeError('Selected panel did not show the raw green base-color map: '
            +str(selectedPixel.tolist()))
    if ignoredPixel[1] > 0.5:
        raise RuntimeError('Non-matching resource did not retain normal lit output: '
            +str(ignoredPixel.tolist()))
    if not np.allclose(selectedPixel, indexedPixel, atol=0.005):
        raise RuntimeError('Material-name and source-index selectors disagree: '
            +str([selectedPixel.tolist(), indexedPixel.tolist()]))
    for x in (220, 580):
        if not np.allclose(selected[300, x], ignored[300, x], atol=0.005):
            raise RuntimeError('Diagnostic affected an unselected material at x='+str(x))
    report = {
        'selected_resource': str(model),
        'selected_material': 'owned_panel_1',
        'selected_linear_rgb': selectedPixel.tolist(),
        'selected_index_linear_rgb': indexedPixel.tolist(),
        'resource_mismatch_linear_rgb': ignoredPixel.tolist(),
        'unselected_panels_unchanged': True,
    }
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print('MATERIAL_DEBUG_CHECK: PASS', json.dumps(report, sort_keys=True))


if __name__ == '__main__':
    main()
