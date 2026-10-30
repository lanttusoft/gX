#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: partial-orbit GTAO freshness gate using only the gX-owned reflectrow scene.
import json
from pathlib import Path
import subprocess

import numpy as np

import audit_common as audit
from gfx_buffer_audit import read_dump

OUT = Path('/tmp/opencode/gtao_orbit_check')
FIRST_FRAME = 40
FRAME_COUNT = 8
LAST_FRAME = FIRST_FRAME + FRAME_COUNT - 1
WIDTH = 1024
HEIGHT = 768
CAMERA_SPIN = 2.0
HISTORY_PAD = 0.03
CONTACT_CUT = 0.98
MOTION_CUTOFF = 0.05
CONTACT_HISTORY_MAX_DELTA = 1.0 / 255.0
MAX_REACTIVE_ERROR = CONTACT_HISTORY_MAX_DELTA
MIN_REACTIVE_PIXELS = 100


def capture_view(name):
    directory = OUT / name
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        'GX_FIXED_DT': '0.016666667',
        'GX_CAM_YAW': '-35',
        'GX_CAM_PITCH': '16',
        'GX_CAM_DIST': '11',
        'GX_CAM_SPIN': str(CAMERA_SPIN),
        'GX_T_PARTICLES': '0',
        'GX_T_GODRAY': '0',
        'GX_T_FOG': '0',
        'GX_T_SSAO': '1',
        'GX_GTAO': '1',
        'GX_SSGI': '0',
        'GX_T_TAA': '0',
        'GX_T_SSR': '0',
        'GX_T_REFL': '0',
        'GX_T_BLOOM': '0',
        'GX_T_DOF': '0',
        'GX_T_MOTIONBLUR': '0',
        'GX_T_FXAA': '0',
        'GX_T_SHADOWS': '0',
        'GX_T_POINT_SHADOWS': '0',
        'GX_T_COOKIES': '0',
        'GX_T_DECALS': '0',
        'GX_T_LENSFLARE': '0',
        'GX_T_RAIN': '0',
        'GX_T_SNOWFALL': '0',
        'GX_T_SPLAT': '0',
        'GX_T_ORACLESEED': '0',
        'GX_T_VELOCITY_TRACE': str(FIRST_FRAME + 1),
        'GX_RT_VIEW': f'{name}:1:0',
        'GX_RT_DUMP_DIR': str(directory),
        'GX_RT_DUMP_FRAME': str(FIRST_FRAME),
        'GX_RT_DUMP_COUNT': str(FRAME_COUNT),
    }, clean=True)
    result = audit.run_scene('--reflectrow', environment, width=WIDTH,
        height=HEIGHT, fps=60, frames=LAST_FRAME, timeout=180)
    (directory / 'renderer.log').write_text(result.stdout or '')
    if 'VELOCITY_TRACE cameraMoved=1' not in (result.stdout or ''):
        raise RuntimeError(f'{name}: controlled orbit did not activate cameraMoved')
    paths = [directory / f'{name}_{frame:06}.gxrt'
        for frame in range(FIRST_FRAME, LAST_FRAME + 1)]
    missing = [path for path in paths if not path.is_file()]
    if missing:
        raise RuntimeError(f'{name}: missing render-target dumps: {missing}')
    return [read_dump(path) for path in paths]


def neighborhood_bounds(current):
    padded = np.pad(current, 1, mode='edge')
    taps = np.stack([padded[y:y + current.shape[0], x:x + current.shape[1]]
        for y in range(3) for x in range(3)])
    return taps.min(axis=0), taps.max(axis=0)


def reproject_history(previous, velocity, current):
    height, width = current.shape
    if previous.shape[:2] != current.shape or velocity.shape[:2] != (height * 2, width * 2):
        raise RuntimeError('GTAO orbit dump dimensions do not match the 2x velocity contract')
    y, x = np.indices((height, width))
    depth_x = np.minimum(np.floor((x + 0.5) / width * velocity.shape[1]), velocity.shape[1] - 1).astype(np.int32)
    depth_y = np.minimum(np.floor((y + 0.5) / height * velocity.shape[0]), velocity.shape[0] - 1).astype(np.int32)
    current_u = (depth_x + 0.5) / velocity.shape[1]
    current_v = (depth_y + 0.5) / velocity.shape[0]
    flow = velocity[depth_y, depth_x]
    velocity_valid = flow[:, :, 2] > 0.5
    history_u = current_u - flow[:, :, 0]
    history_v = current_v - flow[:, :, 1]
    in_bounds = ((history_u >= 0.0) & (history_u <= 1.0)
        & (history_v >= 0.0) & (history_v <= 1.0))
    pixel_x = history_u * width - 0.5
    pixel_y = history_v * height - 0.5
    base_x = np.floor(pixel_x).astype(np.int32)
    base_y = np.floor(pixel_y).astype(np.int32)
    fraction_x = pixel_x - base_x
    fraction_y = pixel_y - base_y
    expected_depth = flow[:, :, 2] - 2.0
    has_depth = flow[:, :, 2] >= 2.0
    depth_in_range = flow[:, :, 2] <= 3.0
    history_sum = np.zeros_like(current, dtype=np.float32)
    weight_sum = np.zeros_like(current, dtype=np.float32)
    taps = (
        (0, 0, (1.0 - fraction_x) * (1.0 - fraction_y)),
        (1, 0, fraction_x * (1.0 - fraction_y)),
        (0, 1, (1.0 - fraction_x) * fraction_y),
        (1, 1, fraction_x * fraction_y),
    )
    for offset_x, offset_y, weight in taps:
        tap_x = np.clip(base_x + offset_x, 0, width - 1)
        tap_y = np.clip(base_y + offset_y, 0, height - 1)
        tap = previous[tap_y, tap_x]
        depth_matches = np.abs(tap[:, :, 1] - expected_depth) <= 0.005
        tap_valid = (~has_depth) | (depth_in_range & depth_matches)
        accepted_weight = weight * tap_valid
        history_sum += tap[:, :, 0] * accepted_weight
        weight_sum += accepted_weight
    sampled = np.divide(history_sum, weight_sum, out=current.copy(), where=weight_sum > 1e-5)
    has_history = (~has_depth) | (depth_in_range & (weight_sum > 1e-5))
    history_valid = velocity_valid & in_bounds & has_history
    return sampled, history_valid, flow


def evaluate(blur_frames, history_frames, velocity_frames):
    frames = []
    for index in range(1, FRAME_COUNT):
        current = blur_frames[index][:, :, 0]
        previous = history_frames[index - 1]
        accumulated = history_frames[index][:, :, 0]
        sampled_history, history_valid, flow = reproject_history(
            previous, velocity_frames[index], current)
        minimum, maximum = neighborhood_bounds(current)
        preexisting_clip_accepts = ((sampled_history >= minimum - HISTORY_PAD)
            & (sampled_history <= maximum + HISTORY_PAD))
        old_accept = history_valid & preexisting_clip_accepts
        motion = np.linalg.norm(flow[:, :, :2], axis=2)
        history_delta = np.abs(sampled_history - current)
        reactive_contact = (old_accept & (motion < MOTION_CUTOFF)
            & (history_delta > CONTACT_HISTORY_MAX_DELTA)
            & (np.minimum(sampled_history, current) < CONTACT_CUT))
        count = int(reactive_contact.sum())
        if count < MIN_REACTIVE_PIXELS:
            raise RuntimeError(f'frame {FIRST_FRAME + index}: only {count} slow-motion contact pixels exercise the reactivity gate')
        nearby_reactive = reactive_contact & (history_delta <= HISTORY_PAD)
        nearby_count = int(nearby_reactive.sum())
        if nearby_count < MIN_REACTIVE_PIXELS:
            raise RuntimeError(f'frame {FIRST_FRAME + index}: only {nearby_count} within-pad contact pixels exercise the freshness gate')
        error = np.abs(accumulated - current)[reactive_contact]
        maximum_error = float(error.max())
        if maximum_error > MAX_REACTIVE_ERROR:
            raise RuntimeError(f'frame {FIRST_FRAME + index}: stale contact AO survived camera-motion gate; max error={maximum_error:.6f}')
        frames.append({
            'frame': FIRST_FRAME + index,
            'valid_velocity_fraction': float((flow[:, :, 2] > 0.5).mean()),
            'velocity_p50_uv': float(np.percentile(motion[flow[:, :, 2] > 0.5], 50)),
            'velocity_p99_uv': float(np.percentile(motion[flow[:, :, 2] > 0.5], 99)),
            'legacy_blend_still_history_heavy_fraction': float((motion < MOTION_CUTOFF).mean()),
            'reactive_contact_pixels': count,
            'within_pad_freshness_pixels': nearby_count,
            'reactive_contact_residual_max': maximum_error,
        })
    if max(row['velocity_p99_uv'] for row in frames) >= MOTION_CUTOFF:
        raise RuntimeError('orbit fixture no longer stays entirely in the legacy still/slow velocity band')
    return frames


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    with audit.settings_preserved():
        captures = {name: capture_view(name) for name in ('ssaoBlur', 'gtaoHist', 'velocity')}
    rows = evaluate(captures['ssaoBlur'], captures['gtaoHist'], captures['velocity'])
    report = {
        'scene': '--reflectrow (gX-owned proxy)',
        'camera_spin_degrees_per_frame': CAMERA_SPIN,
        'tap_rotation': 'disabled to isolate camera/view motion',
        'history_pad': HISTORY_PAD,
        'contact_history_max_delta': CONTACT_HISTORY_MAX_DELTA,
        'legacy_velocity_cutoff_uv': MOTION_CUTOFF,
        'frames': rows,
    }
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    for row in rows:
        print(row)
    print('GTAO_ORBIT: PASS — slow-orbit history keeps contact AO fresh without changing the blend band')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f'GTAO_ORBIT: FAIL — {exc}')
        raise SystemExit(1)
