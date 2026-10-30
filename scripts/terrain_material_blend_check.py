#!/usr/bin/env python3
#ai: owned GLB isolates COLOR_1 blending from COLOR_0 tint and neighboring materials.
import argparse
import json
from pathlib import Path
import struct

import numpy as np
from PIL import Image

import audit_common as audit
from gfx_authoring_check import label_fixture
from glb_material_maps_check import fixture


#ai: normalized u16 COLOR_1 gradient and a constant COLOR_0 tint exercise separate attribute storage.
def owned_fixture(path, missing_mask=False, mask_first=False):
    fixture(path)
    raw = path.read_bytes()
    json_size = struct.unpack_from('<I', raw, 12)[0]
    doc = json.loads(raw[20:20 + json_size])
    bin_offset = 20 + json_size
    bin_size = struct.unpack_from('<I', raw, bin_offset)[0]
    payload = bytearray(raw[bin_offset + 8:bin_offset + 8 + bin_size])
    primitives = doc['meshes'][0]['primitives']
    for panel in range(2):
        primitives[panel]['material'] = panel
        doc['materials'][panel]['pbrMetallicRoughness']['baseColorTexture']['index'] = 0
        for name, component, values in (
                ('COLOR_0', 5121, np.tile([255, 255, 128, 255], (6, 1)).astype('u1')),
                ('COLOR_1', 5123, np.array([[v, v, v, 65535]
                    for v in (0, 0, 65535, 0, 65535, 65535)], dtype='<u2'))):
            if name == 'COLOR_1' and missing_mask:
                continue
            payload.extend(b'\0' * (-len(payload) % 4))
            offset = len(payload)
            data = values.tobytes()
            payload.extend(data)
            vi = len(doc['bufferViews'])
            doc['bufferViews'].append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(data)})
            ai = len(doc['accessors'])
            doc['accessors'].append({'bufferView': vi, 'componentType': component,
                'count': 6, 'type': 'VEC4', 'normalized': True})
            primitives[panel]['attributes'][name] = ai
        if mask_first and not missing_mask:
            attributes = primitives[panel]['attributes']
            primitives[panel]['attributes'] = {'COLOR_1': attributes['COLOR_1'],
                **{key: value for key, value in attributes.items() if key != 'COLOR_1'}}
    payload.extend(b'\0' * (-len(payload) % 4))
    doc['buffers'][0]['byteLength'] = len(payload)
    encoded = json.dumps(doc, separators=(',', ':')).encode()
    encoded += b' ' * (-len(encoded) % 4)
    total = 12 + 8 + len(encoded) + 8 + len(payload)
    path.write_bytes(struct.pack('<4sII', b'glTF', 2, total)
        + struct.pack('<II', len(encoded), 0x4e4f534a) + encoded
        + struct.pack('<II', len(payload), 0x004e4942) + payload)


#ai: scene parameters are symmetric; only the explicit material-sidecar switch changes.
def capture(binary, out, model, secondary, detail, enabled, use_pbr, detail_enabled=False):
    out.mkdir(parents=True, exist_ok=True)
    entries = [{'material': 'owned_panel_' + str(panel), 'terrainBlend': enabled,
                'terrainAlbedoMap': str(secondary), 'terrainDetailScale': 0.1,
                'terrainDetailMap': str(detail) if detail_enabled else ''}
               for panel in (0, 1)]
    model.with_suffix('.materials.json').write_text(json.dumps({'materials': entries}))
    preset = label_fixture()
    preset['sky'].update(color=[255, 255, 255, 255], strength=1.0)
    preset['state'].update(ENABLE_SHADOWS=False, ENABLE_LIGHTING_AMBIENT=True,
        ENABLE_LIGHTING_DIFFUSE=True, ENABLE_LIGHTING_SPECULAR=False,
        ENABLE_MAP_ALBEDO=True, shaderQuality=1)
    preset['sun'] = {'dir': [0, 0, 1], 'color': [255, 255, 255, 255],
        'brightness': 2.0, 'diffuseFactor': 1.0, 'specularFactor': 0.0}
    preset['camera'] = {'position': [0, 0, -8], 'target': [0, 0, 0],
        'up': [0, 1, 0], 'fovy': 45, 'projection': 0}
    preset['models'] = [{'path': str(model), 'params': {'pos': [0, 0, 0],
        'scale': [1, 1, 1], 'textVisible': False, 'usePBR': use_pbr,
        'hasAlbedoMap': True, 'albedoTint': [255, 255, 255, 255],
        'specularColor': [0, 0, 0, 255]}}]
    source = out / 'input.json'
    source.write_text(json.dumps(preset))
    dump = out / 'hdr.bin'
    result = audit.run_scene('--gfx', audit.scene_env({'GX_GFX_PRESET_LOAD': str(source),
        'GX_FIXED_DT': '0.0166666667', 'GX_T_HDRDUMP': '1', 'GX_HDRDUMP_FRAME': '29',
        'GX_HDRDUMP_PATH': str(dump)}, clean=True), binary=binary,
        width=800, height=600, frames=30, fps=None, extra_args=('--gfx-editor',), check=True)
    log = audit.strip_ansi(result.stdout or '')
    (out / 'renderer.log').write_text(log)
    if 'RHI_SHADER:' in log and ('compile:' in log or 'link:' in log):
        raise RuntimeError('Renderer error; see ' + str(out / 'renderer.log'))
    data = dump.read_bytes()
    assert data[:8] == b'GXHDR1\0\0' and struct.unpack_from('<III', data, 8) == (800, 600, 4)
    rgb = np.frombuffer(data, '<f4', offset=20).reshape(600, 800, 4)[..., :3]
    assert np.isfinite(rgb).all()
    Image.fromarray(np.clip(rgb[::-1] * 255, 0, 255).astype('uint8')).save(out / 'hdr.png')
    return rgb


#ai: gradients, macro response, missing-mask fallback and neighboring reset are rendered in both lighting paths.
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, default=audit.ROOT / 'build/gX_build')
    parser.add_argument('--out', type=Path, default=Path('/tmp/opencode/terrain_blend'))
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    model = out / 'owned.glb'
    missing = out / 'missing.glb'
    reordered = out / 'reordered.glb'
    secondary = out / 'blue.png'
    detail = out / 'macro.png'
    Image.new('RGB', (8, 8), (0, 0, 255)).save(secondary)
    Image.new('RGB', (8, 8), (64, 64, 64)).save(detail)
    owned_fixture(model)
    owned_fixture(missing, missing_mask=True)
    owned_fixture(reordered, mask_first=True)
    reports = {}
    for use_pbr in (False, True):
        mode = 'pbr' if use_pbr else 'phong'
        off = capture(args.binary, out / (mode + '_off'), model, secondary, detail, False, use_pbr)
        on = capture(args.binary, out / (mode + '_on'), model, secondary, detail, True, use_pbr)
        macro = capture(args.binary, out / (mode + '_macro'), model, secondary, detail,
                        True, use_pbr, detail_enabled=True)
        fallback = capture(args.binary, out / (mode + '_missing'), missing, secondary, detail, True, use_pbr)
        reordered_rgb = capture(args.binary, out / (mode + '_reordered'), reordered,
                                secondary, detail, True, use_pbr)
        neighbor = float(np.max(np.abs(on[280:320, 200:240] - off[280:320, 200:240])))
        fallback_delta = float(np.max(np.abs(fallback - off)))
        reordered_delta = float(np.max(np.abs(reordered_rgb - on)))
        left = on[300, 350].tolist()
        right = on[300, 450].tolist()
        center = on[300, 400].tolist()
        macro_ratio = float(macro[300, 400].sum() / on[300, 400].sum())
        assert neighbor < 0.001, ('neighbor state leak', neighbor)
        assert fallback_delta < 0.001, ('missing COLOR_1 did not fall back', fallback_delta)
        assert reordered_delta < 0.001, ('COLOR_1 ordering replaced COLOR_0 tint', reordered_delta)
        assert left[2] > left[0] and right[0] > right[2], ('mask gradient missing', left, right)
        assert center[0] > 0.05 and center[2] > 0.02, ('blend lost', center)
        assert 0.5 < macro_ratio < 0.85, ('non-color macro multiply missing', macro_ratio)
        reports[mode] = {'left_rgb': left, 'center_rgb': center, 'right_rgb': right,
            'neighbor_max_delta': neighbor, 'missing_mask_max_delta': fallback_delta,
            'reordered_color_max_delta': reordered_delta,
            'macro_ratio': macro_ratio}
    (out / 'report.json').write_text(json.dumps(reports, indent=2) + '\n')
    print('TERRAIN_MATERIAL_BLEND_CHECK: PASS', reports)


if __name__ == '__main__':
    main()
