#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: pixel regression for ambient-only Phong albedo sampling on an owned GLB.
import argparse
import json
from pathlib import Path
import struct
import sys

import numpy as np
from PIL import Image

import audit_common as audit
from gfx_authoring_check import label_fixture
from glb_material_maps_check import fixture


def capture(binary, out):
    out.mkdir(parents=True, exist_ok=True)
    model = out / 'ambient_checker.glb'
    fixture(model, albedo_pattern=True)
    preset = label_fixture()
    preset['state'].update(
        ENABLE_REFLECTIONS=False, ENABLE_PLANAR_REFLECTIONS=False,
        ENABLE_SSR=False, ENABLE_SSAO=False, ENABLE_VOLUME_FOG=False,
        ENABLE_BLOOM=False, ENABLE_TAA=False, ENABLE_WATER=False,
        ENABLE_PROCEDURAL_SKY=False, ENABLE_SHADOWS=False,
        ENABLE_LIGHTING_AMBIENT=True, ENABLE_LIGHTING_DIFFUSE=False,
        ENABLE_LIGHTING_SPECULAR=False, ENABLE_PHONG=True,
        ENABLE_GOURAUD=False, ENABLE_ENV_AMBIENT=False,
        ENABLE_MAP_ALBEDO=True, ENABLE_MAP_NORMAL=False,
        ENABLE_MAP_METALNESS=False, ENABLE_MAP_PARALLAX=False,
        ENABLE_ACES_TONEMAP=False, ENABLE_AGX_TONEMAP=False,
        ENABLE_GAMMA_CORRECTION=False, exposure=1.0, shaderQuality=0,
    )
    preset['sky'].update(color=[255, 255, 255, 255], strength=1.0)
    preset['sun'] = {
        'dir': [0, 0, 1], 'color': [255, 255, 255, 255],
        'brightness': 0.0, 'diffuseFactor': 0.0, 'specularFactor': 0.0,
        'castShadows': False,
    }
    preset['camera'] = {
        'position': [0, 0, -8], 'target': [0, 0, 0],
        'up': [0, 1, 0], 'fovy': 45, 'projection': 0,
    }
    preset['models'] = [{
        'path': str(model),
        'params': {
            'pos': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1],
            'textVisible': False, 'usePBR': False, 'hasAlbedoMap': True,
            'albedo': 1.0, 'albedoTint': [255, 255, 255, 255],
            'specularColor': [0, 0, 0, 255],
        },
    }]
    preset['lights'] = []
    preset_path = out / 'preset.json'
    preset_path.write_text(json.dumps(preset))
    dump_path = out / 'hdr.bin'
    env = audit.scene_env({
        'GX_GFX_PRESET_LOAD': str(preset_path),
        'GX_FIXED_DT': '0.0166666667',
        'GX_MATERIALS_OFF': '1',
        'GX_T_HDRDUMP': '1',
        'GX_HDRDUMP_FRAME': '29',
        'GX_HDRDUMP_PATH': str(dump_path),
    }, clean=True)
    result = audit.run_scene(
        '--gfx', env, binary=binary, width=800, height=600, frames=30,
        fps=None, timeout=180, extra_args=('--gfx-editor',), check=True,
    )
    log = audit.strip_ansi(result.stdout or '')
    (out / 'renderer.log').write_text(log)
    data = dump_path.read_bytes()
    if data[:8] != b'GXHDR1\0\0' or struct.unpack_from('<III', data, 8) != (800, 600, 4):
        raise RuntimeError('Invalid ambient-only Phong HDR capture')
    rgba = np.frombuffer(data, '<f4', offset=20).reshape(600, 800, 4)
    if not np.isfinite(rgba).all():
        raise RuntimeError('Ambient-only Phong capture contains non-finite pixels')
    rgb = rgba[..., :3]
    metrics = []
    for x in (220, 400, 580):
        patch = rgb[280:320, x-20:x+20]
        luminance = audit.luma(patch)
        mean = patch.mean(axis=(0, 1))
        variation = float(luminance.std())
        if variation < 0.02:
            raise RuntimeError(
                f'Ambient-only Phong lost checker albedo detail at x={x}: '
                f'luma_std={variation:.6g}'
            )
        metrics.append({
            'center_x': x, 'rgb_mean': mean.tolist(),
            'luma_std': variation,
            'dominant_channel': int(np.argmax(mean)),
        })
    if sorted(item['dominant_channel'] for item in metrics) != [0, 1, 2]:
        raise RuntimeError('Ambient-only Phong did not preserve all three map colors: '
            + str(metrics))
    report = {
        'lighting': 'ambient-only Phong; no direct or local lights',
        'model': str(model), 'checker_materials': metrics,
    }
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    Image.fromarray(np.clip(rgb[::-1] * 255.0, 0, 255).astype(np.uint8), 'RGB').save(
        out / 'hdr.png'
    )
    print('PHONG_AMBIENT_CHECK: PASS', json.dumps(report, sort_keys=True))


def main():
    parser = argparse.ArgumentParser(
        description='Check ambient-only Phong albedo sampling on an owned GLB'
    )
    parser.add_argument('--binary', type=Path, default=audit.BIN)
    parser.add_argument('--out', type=Path, default=Path('/tmp/opencode/phong_ambient_check'))
    args = parser.parse_args()
    with audit.settings_preserved():
        capture(args.binary.resolve(), args.out.resolve())


if __name__ == '__main__':
    main()
