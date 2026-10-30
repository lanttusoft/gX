#!/usr/bin/env python3
#ai: staged opt-in spike controls; diagnostic pixels are not a general surface/refraction certificate.
import argparse
import copy
import json
from pathlib import Path
import re
import struct
import tempfile

import numpy as np
from PIL import Image

import audit_common as audit
from gfx_water_authoring_check import fixture


def scene():
    preset = fixture()
    preset['camera'] = {'position': [0, 3, -8], 'target': [0, -1, 0],
                        'up': [0, 1, 0], 'fovy': 45, 'projection': 0}
    preset['models'] = []
    for position, scale, color in [
        ([-1.0, -2, 0], [0.7, 0.7, 0.7], [230, 40, 20, 255]),
        ([1.0, -2, 0.8], [0.6, 0.4, 0.6], [20, 60, 230, 255]),
        ([0.0, 1, 2], [0.5, 0.5, 0.5], [220, 200, 30, 255]),
    ]:
        preset['models'].append({'path': 'resources/model/primitive/cube.glb',
            'params': {'pos': position, 'scale': scale, 'rotation': [0, 0, 0],
                       'textVisible': False, 'albedoTint': color,
                       'emissiveColor': color, 'emissiveFactor': 1.0}})
    return preset


def capture(out, tag, preset, binary, hooks, width=1024, height=768):
    directory = out / tag
    directory.mkdir()
    source = directory / 'input.json'
    source.write_text(json.dumps(preset))
    env = audit.scene_env({'GX_FIXED_DT': '0.0166666667', 'GX_DOCK_TRACE': '1',
                           'GX_GFX_PRESET_LOAD': str(source), **hooks}, clean=True)
    run = audit.run_scene('--gfx', env, binary=binary, width=width, height=height, fps=None, frames=30,
        shot_frame=30, screenshot_dir=directory, extra_args=('--gfx-editor',))
    log = audit.strip_ansi(run.stdout or '')
    (directory / 'renderer.log').write_text(log)
    image = np.asarray(Image.open(audit.newest_shot(directory)).convert('RGB'), dtype=np.float32)
    x, y, width, height = audit.dock_viewport(log, image.shape[1], image.shape[0])
    return image[y:y + height, x:x + width], log


#ai: float readback and CPU matrix reconstruction check diagnostic ownership independently of screenshot colors.
def ownership(path, offset, require_eligible=True, invalid=False):
    payload = path.read_bytes()
    if payload[:8] != b'WTRF0001':
        raise RuntimeError('Missing water float-evidence header')
    width, height = struct.unpack_from('<II', payload, 8)
    count = width * height
    inverse = np.frombuffer(payload, '<f4', 16, 16).reshape(4, 4).astype(np.float64)
    view = np.frombuffer(payload, '<f4', 16, 80).reshape(4, 4).astype(np.float64)
    plane = struct.unpack_from('<f', payload, 144)[0]
    values = np.frombuffer(payload, '<f4', offset=148)
    if values.size != count * 9 or not np.isfinite(values).all():
        raise RuntimeError('Truncated/non-finite raw water evidence')
    depth = values[count * 4:count * 5].reshape(height, width)
    presented = values[count * 5:].reshape(height, width, 4)[:, :, :3]
    yy, xx = np.indices((height, width))
    uv = np.stack(((xx + 0.5) / width, (yy + 0.5) / height), axis=-1)

    def reconstruct(coords, z):
        clip = np.concatenate((coords * 2 - 1, z[..., None] * 2 - 1,
                               np.ones(z.shape + (1,))), axis=-1)
        point = clip @ inverse.T
        return point[..., :3] / point[..., 3:4]

    direction = (view @ np.array([0, -1, 0, 0]))[:2]
    direction /= np.linalg.norm(direction)
    candidate = uv + direction * offset / np.array([width, height])
    inside = (candidate >= 0).all(axis=-1) & (candidate < 1).all(axis=-1)
    px = np.clip(np.floor(candidate[:, :, 0] * width).astype(int), 0, width - 1)
    py = np.clip(np.floor(candidate[:, :, 1] * height).astype(int), 0, height - 1)
    sampled_depth = depth[py, px]
    sampled_uv = np.stack(((px + 0.5) / width, (py + 0.5) / height), axis=-1)
    reasons = np.full(depth.shape, 3)
    reasons[inside] = 4
    geometry = inside & (sampled_depth >= 0) & (sampled_depth < 1)
    if invalid:
        if np.any(inverse):
            raise RuntimeError('Invalid-reconstruction control did not upload its singular matrix')
        reasons[geometry] = 7
    else:
        near = reconstruct(uv, np.zeros(depth.shape))
        far = reconstruct(uv, np.ones(depth.shape))
        ray = far - near
        water = near + ray * ((plane - near[:, :, 1]) / ray[:, :, 1])[..., None]
        water_depth = -(np.concatenate((water, np.ones(depth.shape + (1,))), axis=-1) @ view.T)[:, :, 2]
        world = reconstruct(sampled_uv, sampled_depth)
        candidate_depth = -(np.concatenate((world, np.ones(depth.shape + (1,))), axis=-1) @ view.T)[:, :, 2]
        reasons[geometry] = 5
        behind = geometry & (candidate_depth > water_depth + np.maximum(0.01, 0.001 * water_depth))
        reasons[behind] = 6
        reasons[behind & (world[:, :, 1] < plane - 0.01)] = 1
    colors = {1: [0, 1, 0], 3: [1, 1, 0], 4: [0, 1, 1],
              5: [1, 0, 0], 6: [1, 0.5, 0], 7: [1, 0, 1]}
    result = {}
    for reason, color in colors.items():
        mask = np.max(np.abs(presented - color), axis=-1) < 0.001
        result[str(reason)] = {'pixels': int(mask.sum()),
                              'conflicts': int((mask & (reasons != reason)).sum())}
    if (require_eligible and result['1']['pixels'] < 30) or any(item['conflicts'] for item in result.values()):
        raise RuntimeError('CPU/GPU water ownership mismatch: ' + json.dumps(result))
    return result


#ai: transitions use the native resolution reload; raw matching avoids presentation scaling differences.
def remaining_controls(out, preset, binary, legacy):
    metrics = {}
    invalid, _ = capture(out, 'invalid_fallback', preset, binary,
        {'GX_WATER_REFRACTION': '1', 'GX_WATER_REFRACTION_INVALID': '1'})
    metrics['invalid_fallback_max_delta'] = float(np.abs(invalid - legacy).max())
    if metrics['invalid_fallback_max_delta'] != 0:
        raise RuntimeError('Invalid reconstruction changed legacy fallback')
    capture(out, 'invalid_diagnostic', preset, binary,
        {'GX_WATER_REFRACTION': '1', 'GX_WATER_REFRACTION_INVALID': '1',
         'GX_WATER_REFRACTION_DIAGNOSTIC': '1',
         'GX_WATER_REFRACTION_DUMP': str(out / 'invalid.bin')})
    metrics['invalid'] = ownership(out / 'invalid.bin', 4, require_eligible=False, invalid=True)
    if metrics['invalid']['1']['pixels'] or metrics['invalid']['7']['pixels'] < 30:
        raise RuntimeError('Invalid reconstruction did not produce explicit rejection')

    for tag, y in [('above_plane', 0.025), ('plane_margin', -0.025)]:
        offset = 4 if tag == 'above_plane' else 0
        variant = copy.deepcopy(preset)
        variant['models'] = [{'path': 'resources/model/primitive/cube.glb',
            'params': {'pos': [0, y, 15], 'scale': [3, 0.02, 3], 'rotation': [0, 0, 0],
                       'textVisible': False, 'emissiveFactor': 1.0,
                       'emissiveColor': [100, 60, 200, 255]}}]
        capture(out, tag, variant, binary,
            {'GX_WATER_REFRACTION': '1', 'GX_WATER_REFRACTION_PIXELS': str(offset),
             'GX_WATER_REFRACTION_DIAGNOSTIC': '1',
             'GX_WATER_REFRACTION_DUMP': str(out / (tag + '.bin'))})
        metrics[tag] = ownership(out / (tag + '.bin'), offset, require_eligible=False)
        if tag == 'above_plane':
            if metrics[tag]['1']['pixels'] or metrics[tag]['5']['pixels'] < 30:
                raise RuntimeError('Dry geometry was not rejected as foreground')
        elif metrics[tag]['6']['pixels'] < 30:
            raise RuntimeError('No dedicated plane-margin rejection witnesses')

    transitioned = copy.deepcopy(preset)
    transitioned['state']['ENABLE_SHADOWS'] = False
    transitioned['state']['ENABLE_FRUSTUM_CULLING'] = False
    _, transition_log = capture(out, 'transition', transitioned, binary,
        {'GX_WATER_REFRACTION': '1', 'GX_WATER_REFRACTION_TEST_TRANSITION': '1',
         'GX_WATER_REFRACTION_DIAGNOSTIC': '1',
         'GX_WATER_REFRACTION_DUMP': str(out / 'transition.bin')})
    if 'WATER_REFRACTION_TEST_TRANSITION: frame=12' not in transition_log:
        raise RuntimeError('No live camera/resize transition occurred')
    sizes = re.findall(r'WATER_REFRACTION_CAPTURE_RESIZE: frame=(\d+) size=(\d+)x(\d+)', transition_log)
    if not any(int(frame) >= 12 and (width, height) == ('800', '600')
               for frame, width, height in sizes):
        raise RuntimeError('Capture did not reallocate for the native target resize')
    metrics['transition'] = ownership(out / 'transition.bin', 4)
    current = (out / 'transition.bin').read_bytes()
    if struct.unpack_from('<II', current, 8) != (800, 600):
        raise RuntimeError('Transition sampled a wrong-size capture')
    view = np.frombuffer(current, '<f4', 16, 80).reshape(4, 4)
    camera_x = float(np.linalg.inv(view)[0, 3])
    metrics['transition_camera_x'] = camera_x
    if abs(camera_x - 0.75) > 1e-5:
        raise RuntimeError('Transition retained an old camera matrix')
    transitioned['camera']['position'][0] = 0.75
    capture(out, 'fresh_800', transitioned, binary,
        {'GX_WATER_REFRACTION': '1', 'GX_WATER_REFRACTION_DIAGNOSTIC': '1',
         'GX_WATER_REFRACTION_DUMP': str(out / 'fresh_800.bin')}, width=800, height=600)
    fresh = (out / 'fresh_800.bin').read_bytes()
    if len(fresh) != len(current):
        raise RuntimeError('Fresh and transitioned evidence dimensions differ')
    delta = float(np.abs(np.frombuffer(current, '<f4', offset=148)
                        - np.frombuffer(fresh, '<f4', offset=148)).max())
    metrics['transition_fresh_max_delta'] = delta
    if delta != 0:
        raise RuntimeError('Transition capture differs from fresh current-camera reference')
    return metrics


#ai: separately gated contact foam must require shallow eligible depth and preserve missing/deep fallbacks.
def depth_foam_controls(out, preset, binary):
    metrics = {}
    shallow = copy.deepcopy(preset)
    shallow['models'] = [{'path': 'resources/model/primitive/cube.glb',
        'params': {'pos': [0, -0.25, 0], 'scale': [3, 0.2, 3],
                   'rotation': [0, 0, 0], 'textVisible': False,
                   'albedoTint': [230, 40, 20, 255],
                   'emissiveColor': [230, 40, 20, 255], 'emissiveFactor': 1.0}}]
    valid = {'GX_WATER_REFRACTION': '1', 'GX_WATER_REFRACTION_PIXELS': '0'}
    shallow_off, _ = capture(out, 'depth_foam_shallow_off', shallow, binary, valid)
    shallow_on, log = capture(out, 'depth_foam_shallow_on', shallow, binary,
        {**valid, 'GX_WATER_DEPTH_FOAM': '1'})
    trace = re.search(r'WATER_REFRACTION: blend=(\d+) src=(\d+) dst=(\d+) equation=(\d+) valid=(\d+)', log)
    if not trace or trace[5] != '1':
        raise RuntimeError('Depth-foam fixture has no valid current-frame depth capture')
    metrics['shallow_changed_pixels'] = int((np.abs(shallow_on - shallow_off).max(axis=2) > 2).sum())
    if metrics['shallow_changed_pixels'] < 30:
        raise RuntimeError('Shallow submerged geometry produced no depth-aware contact foam')
    metrics['shallow_changed_fraction'] = metrics['shallow_changed_pixels'] / (
        shallow_off.shape[0] * shallow_off.shape[1])
    if metrics['shallow_changed_fraction'] > 0.10:
        raise RuntimeError('Depth-aware contact foam spread beyond the shoreline band')

    deep_off, _ = capture(out, 'depth_foam_deep_off', preset, binary, valid)
    deep_on, _ = capture(out, 'depth_foam_deep_on', preset, binary,
        {**valid, 'GX_WATER_DEPTH_FOAM': '1'})
    metrics['deep_max_delta'] = float(np.abs(deep_on - deep_off).max())
    if metrics['deep_max_delta'] != 0:
        raise RuntimeError('Deep/above-plane geometry changed under the shallow contact-foam gate')

    missing, _ = capture(out, 'depth_foam_missing', shallow, binary,
        {'GX_WATER_REFRACTION': '1', 'GX_WATER_REFRACTION_PIXELS': '0',
         'GX_WATER_REFRACTION_MISSING': '1', 'GX_WATER_DEPTH_FOAM': '1'})
    metrics['missing_max_delta'] = float(np.abs(missing - shallow_off).max())
    if metrics['missing_max_delta'] != 0:
        raise RuntimeError('Missing depth capture changed the depth-foam fallback')
    return metrics


def main(binary, out):
    out.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run_', dir=out))
    metrics = {'evidence': str(out)}
    preset = scene()
    with audit.settings_preserved():
        off, _ = capture(out, 'off', preset, binary, {})
        zero, log = capture(out, 'zero', preset, binary,
            {'GX_WATER_REFRACTION': '1', 'GX_WATER_REFRACTION_PIXELS': '0'})
        trace = re.search(r'WATER_REFRACTION: blend=(\d+) src=(\d+) dst=(\d+) equation=(\d+) valid=(\d+)', log)
        if not trace or trace[5] != '1':
            raise RuntimeError('Blend/capture prerequisite failed; stop before displacement. See ' + str(out))
        metrics['blend'] = list(map(int, trace.groups()))
        metrics['zero_max_delta'] = float(np.abs(off - zero).max())
        if metrics['zero_max_delta'] != 0:
            raise RuntimeError('Zero-offset image differs from legacy; stop before displacement. See ' + str(out))
        missing, _ = capture(out, 'missing', preset, binary,
            {'GX_WATER_REFRACTION': '1', 'GX_WATER_REFRACTION_MISSING': '1'})
        metrics['missing_max_delta'] = float(np.abs(off - missing).max())
        if metrics['missing_max_delta'] != 0:
            raise RuntimeError('Missing-capture fallback changed legacy output')
        offset, _ = capture(out, 'offset', preset, binary,
            {'GX_WATER_REFRACTION': '1', 'GX_WATER_REFRACTION_PIXELS': '4'})
        metrics['offset_changed_pixels'] = int((np.abs(offset - zero).max(axis=2) > 2).sum())
        if metrics['offset_changed_pixels'] < 30:
            raise RuntimeError('No demonstrated submerged scene displacement')
        diagnostic, _ = capture(out, 'diagnostic', preset, binary,
            {'GX_WATER_REFRACTION': '1', 'GX_WATER_REFRACTION_DIAGNOSTIC': '1',
             'GX_WATER_REFRACTION_DUMP': str(out / 'ownership.bin')})
        metrics['green_candidates'] = int(((diagnostic[:, :, 1] > 100)
            & (diagnostic[:, :, 0] < 30) & (diagnostic[:, :, 2] < 30)).sum())
        if metrics['green_candidates'] < 30:
            raise RuntimeError('No eligible submerged diagnostic candidates')
        metrics['ownership'] = ownership(out / 'ownership.bin', 4)
        corrupted = bytearray((out / 'ownership.bin').read_bytes())
        width, height = struct.unpack_from('<II', corrupted, 8)
        depth_start = 148 + width * height * 16
        depth_end = depth_start + width * height * 4
        corrupted[depth_start:depth_end] = np.ones(width * height, dtype='<f4').tobytes()
        negative = out / 'ownership_depth_negative.bin'
        negative.write_bytes(corrupted)
        try:
            ownership(negative, 4)
        except RuntimeError:
            metrics['corrupt_depth_rejected'] = True
        else:
            raise RuntimeError('Ownership oracle accepted corrupted source depth')
        metrics['remaining'] = remaining_controls(out, preset, binary, off)
        metrics['depth_foam'] = depth_foam_controls(out, preset, binary)
        from water_refraction_editor_check import checks
        checks(out / 'editor', binary)
    (out / 'report.json').write_text(json.dumps(metrics, indent=2) + '\n')
    print(json.dumps(metrics, indent=2))
    print('WATER_REFRACTION_CONTROLS: PASS (bounded fixtures; not physical refraction certification)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, default=audit.ROOT / 'build/water_refraction/gX_build')
    parser.add_argument('--out', type=Path, default=Path('/tmp/opencode/water_refraction_check'))
    args = parser.parse_args()
    main(args.binary.resolve(), args.out.resolve())
