#!/usr/bin/env python3
#ai: owned-fixture water regressions; no scene-refraction feature or visual tuning.
import copy
import json
from pathlib import Path
import tempfile

import numpy as np
from PIL import Image

import audit_common as audit
from gfx_authoring_check import label_fixture

OUT = Path('/tmp/opencode/gfx_water_authoring_check')


#ai: fixed authored settings cover all water schema fields without importing assets.
def fixture():
    preset = label_fixture()
    preset['name'] = 'Owned water regression'
    preset['camera'] = {'position': [0, 2, -10], 'target': [0, 0, 0],
                        'up': [0, 1, 0], 'fovy': 45, 'projection': 0}
    preset['models'][0]['params']['textVisible'] = False
    preset['state']['ENABLE_WATER'] = True
    preset['water'] = {
        'planeHeight': 0.0, 'tint': [180, 220, 200, 123],
        'normalMapStrength': 0.0, 'normalMapScale': [3.0, 5.0],
        'proceduralNormalStrength': 1.0, 'proceduralNormalScale': 1.0,
        'normalMapScroll': [0.01, 0.01],
        'normalMapPath': '', 'waveCount': 0,
        'automaticRipples': True,
        'waveDirections': [0.1, 0.3, 0.5, 0.7, 0.9, 1.1],
        'waveAmplitudes': [0.0] * 6,
        'waveFrequencies': [0.5, 0.6, 0.7, 0.8, 0.9, 1.0],
        'waveSpeeds': [-0.2, 0.0, 0.2, 0.4, 0.6, 0.8],
        'waveSteepness': [0.0] * 6,
    }
    return preset


#ai: early preset saves check serialization, not per-frame live height; pixels check that separately.
def check_saved(preset, saved):
    if saved['state']['ENABLE_WATER'] != preset['state']['ENABLE_WATER']:
        raise RuntimeError('Water enable state changed during save/load')
    for field, expected in preset['water'].items():
        actual = saved['water'][field]
        equal = actual == expected if isinstance(expected, str) else np.allclose(
            actual, expected, rtol=0, atol=1e-6)
        if not equal:
            raise RuntimeError('Water round-trip changed ' + field)


#ai: negative controls prove each authored field and the enable flag are checked.
def saved_controls():
    preset = fixture()
    check_saved(preset, copy.deepcopy(preset))
    count = 0
    for field in preset['water']:
        changed = copy.deepcopy(preset)
        value = changed['water'][field]
        if isinstance(value, str):
            changed['water'][field] = 'unexpected.png'
        elif isinstance(value, list):
            value[0] += 1
        else:
            changed['water'][field] += 1
        try:
            check_saved(preset, changed)
        except RuntimeError:
            count += 1
        else:
            raise RuntimeError('Round-trip negative control missed ' + field)
    changed = copy.deepcopy(preset)
    changed['state']['ENABLE_WATER'] = False
    try:
        check_saved(preset, changed)
    except RuntimeError:
        count += 1
    else:
        raise RuntimeError('Round-trip negative control missed water enable')
    print('WATER_AUTHORING_CONTROLS: PASS', count, 'corruption negatives')


#ai: shared audit launch/settings/window contracts; logs/captures are retained privately.
def capture(tag, preset):
    directory = OUT / tag
    directory.mkdir(parents=True, exist_ok=False)
    source, saved_path = directory / 'input.json', directory / 'saved.json'
    source.write_text(json.dumps(preset))
    env = audit.scene_env({
        'GX_FIXED_DT': '0.0166666667', 'GX_GFX_PRESET_LOAD': str(source),
        'GX_GFX_PRESET_SAVE': str(saved_path), 'GX_DOCK_TRACE': '1',
    }, clean=True)
    run = audit.run_scene('--gfx', env, frames=30, shot_frame=30, fps=None,
                          screenshot_dir=directory, timeout=120, check=True,
                          extra_args=('--gfx-editor',))
    log = run.stdout or ''
    (directory / 'renderer.log').write_text(log)
    pixels = np.asarray(Image.open(audit.newest_shot(directory)).convert('RGB'),
                        dtype=np.float32)
    x, y, width, height = audit.dock_viewport(
        audit.strip_ansi(log), pixels.shape[1], pixels.shape[0])
    image = pixels[y:y + height, x:x + width]
    if not image.size or not np.isfinite(image).all():
        raise RuntimeError('Missing/non-finite presented water image')
    saved = json.loads(saved_path.read_text())
    check_saved(preset, saved)
    return image, saved


def delta(first, second):
    if first.shape != second.shape:
        raise RuntimeError('Water capture dimensions differ')
    return float(np.abs(first - second).mean())


#ai: new gate assertions do not alter existing water/height thresholds.
def main():
    global OUT
    from water_ripple_check import checks
    checks()
    saved_controls()
    OUT.mkdir(parents=True, exist_ok=True)
    OUT = Path(tempfile.mkdtemp(prefix='run_', dir=OUT))
    metrics = {}
    with audit.settings_preserved():
        preset = fixture()
        surface, saved = capture('surface', preset)
        reloaded, _ = capture('reloaded', saved)
        metrics['reload'] = delta(surface, reloaded)
        if metrics['reload'] > 0.1:
            raise RuntimeError('Saved water scene did not reproduce its image')

        disabled = copy.deepcopy(preset)
        disabled['state']['ENABLE_WATER'] = False
        off, disabled_saved = capture('disabled', disabled)
        disabled_saved['state']['ENABLE_WATER'] = True
        enabled, _ = capture('reenabled_from_saved', disabled_saved)
        metrics['enabled'] = delta(surface, off)
        metrics['reenabled'] = delta(surface, enabled)
        if metrics['enabled'] < 0.5 or metrics['reenabled'] > 0.1:
            raise RuntimeError('Water enabled/disabled presets did not reproduce')

        variants = [
            ('height', {'planeHeight': 1.5}, 0.5),
            ('tint', {'tint': [255, 30, 30, 123]}, 0.5),
            ('waves', {'waveCount': 2, 'waveAmplitudes': [0.3, 0.2, 0, 0, 0, 0],
                       'waveSteepness': [0.1, 0.1, 0, 0, 0, 0]}, 0.1),
            ('procedural_normals_off', {'proceduralNormalStrength': 0.0}, 0.1),
            ('procedural_normals_stronger', {'proceduralNormalStrength': 2.0}, 0.1),
            ('procedural_normals_scaled', {'proceduralNormalScale': 2.5}, 0.1),
        ]
        for name, params, minimum in variants:
            variant = copy.deepcopy(preset)
            variant['water'].update(params)
            image, _ = capture(name, variant)
            metrics[name] = delta(surface, image)
            if metrics[name] < minimum:
                raise RuntimeError('Authored water ' + name + ' has no demonstrated effect')

        cleared = copy.deepcopy(preset)
        cleared['water']['normalMapStrength'] = 1.5
        no_map, _ = capture('normal_cleared', cleared)
        metrics['normal_clear'] = delta(surface, no_map)
        if metrics['normal_clear'] > 0.1:
            raise RuntimeError('Cleared normal map did not disable detail')
        mapped = copy.deepcopy(cleared)
        mapped['water']['normalMapPath'] = 'resources/texture/texture_normal.png'
        detail, _ = capture('normal_detail', mapped)
        mapped['water']['normalMapStrength'] = 0.0
        neutral, _ = capture('normal_disabled', mapped)
        metrics['normal_detail'] = delta(detail, neutral)
        if metrics['normal_detail'] < 0.1:
            raise RuntimeError('Loaded normal map has no demonstrated detail effect')

        below = copy.deepcopy(preset)
        below['camera']['position'] = [0, -2, -10]
        underwater, _ = capture('underwater', below)
        below['state']['ENABLE_WATER'] = False
        underwater_off, _ = capture('underwater_off', below)
        metrics['underwater'] = delta(underwater, underwater_off)
        metrics['underwater_mean'] = float(underwater.mean())
        if metrics['underwater'] < 0.5 or metrics['underwater_mean'] < 1:
            raise RuntimeError('Underwater fixture has missing/black water output')

    (OUT / 'metrics.json').write_text(json.dumps(metrics, indent=2) + '\n')
    for name, value in metrics.items():
        print('WATER_AUTHORING:', name, '=', value)
    print('WATER_AUTHORING_CHECK: PASS')


if __name__ == '__main__':
    main()
