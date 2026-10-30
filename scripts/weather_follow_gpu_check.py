#!/usr/bin/env python3
#ai: permitted empty-scene weather translation covariance, using the shared engine capture contract.
import copy
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image
import audit_common as audit

OUT = Path('/tmp/opencode/weather_follow_gpu')


def capture(tag, preset):
    directory = OUT/tag
    directory.mkdir(parents=True, exist_ok=True)
    path = directory/'input.json'
    path.write_text(json.dumps(preset))
    env = audit.scene_env({'GX_FIXED_DT':'0.0166666667',
        'GX_GFX_PRESET_LOAD':str(path), 'GX_DOCK_TRACE':'1'}, clean=True)
    run = audit.run_scene('--gfx', env, frames=8, shot_frame=8, fps=None,
        screenshot_dir=directory, timeout=120, extra_args=('--gfx-editor','--seed','424242'))
    (directory/'renderer.log').write_text(run.stdout or '')
    pixels = audit.load_rgb(audit.newest_shot(directory))
    x,y,w,h = audit.dock_viewport(audit.strip_ansi(run.stdout or ''),pixels.shape[1],pixels.shape[0])
    return pixels[y:y+h,x:x+w]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--vertical', action='store_true')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    preset = json.loads((audit.ROOT/'resources/tools/gfx_editor.json').read_text())
    preset['models'] = []
    for key in preset['state']:
        if key.startswith('ENABLE_'):
            preset['state'][key] = False
    preset['state'].update(ENABLE_LIGHTING_AMBIENT=True, ENABLE_LIGHTING_DIFFUSE=True)
    preset['camera'] = {'position':[0,2,6], 'target':[0,2,0], 'up':[0,1,0],
        'fovy':45, 'projection':0}
    #ai: translation is an integer multiple of snow/rain and fog torus widths (11/22).
    shifted = copy.deepcopy(preset)
    shifted['camera'].update(position=[220,2,-434], target=[220,2,-440])
    elevated = copy.deepcopy(preset)
    elevated['camera'].update(position=[0,114,6], target=[0,114,0])
    rows = []
    with audit.settings_preserved():
        origin_off = capture('origin_off',preset)
        shifted_off = capture('shifted_off',shifted)
        elevated_off = capture('elevated_off',elevated) if args.vertical else None
        for layer, flag in [('snow','ENABLE_SNOWFALL'),('rain','ENABLE_RAIN'),('fog','ENABLE_FOGSPRITE')]:
            preset['state'][flag] = shifted['state'][flag] = True
            origin = capture(layer+'_origin',preset)-origin_off
            moved = capture(layer+'_shifted',shifted)-shifted_off
            preset['state'][flag] = shifted['state'][flag] = False
            mask = np.abs(origin).mean(axis=2)>2
            moved_mask = np.abs(moved).mean(axis=2)>2
            union = np.logical_or(mask,moved_mask).sum()
            iou = float(np.logical_and(mask,moved_mask).sum()/max(union,1))
            error = float(np.abs(origin-moved).mean())
            row = {'layer':layer,'origin_pixels':int(mask.sum()),'shifted_pixels':int(moved_mask.sum()),
                'mask_iou':iou,'delta_mean_error':error}
            row['pass'] = min(row['origin_pixels'],row['shifted_pixels'])>=30 and iou>=.65 and error<=.5
            rows.append(row)
            if args.vertical:
                elevated['state'][flag] = True
                up = capture(layer+'_elevated',elevated)-elevated_off
                elevated['state'][flag] = False
                pixels = int((np.abs(up).mean(axis=2)>2).sum())
                ratio = pixels/max(int(mask.sum()),1)
                vertical = {'layer':layer+'_vertical','origin_pixels':int(mask.sum()),
                    'elevated_pixels':pixels,'visibility_ratio':ratio,
                    'pass':pixels>=30 and .25<=ratio<=4}
                rows.append(vertical)
                Image.fromarray(np.clip(np.abs(up),0,255).astype(np.uint8)).save(OUT/(layer+'_elevated_delta.png'))
                print('WEATHER_GPU:',json.dumps(vertical))
            Image.fromarray(np.clip(np.abs(origin),0,255).astype(np.uint8)).save(OUT/(layer+'_origin_delta.png'))
            Image.fromarray(np.clip(np.abs(moved),0,255).astype(np.uint8)).save(OUT/(layer+'_shifted_delta.png'))
            print('WEATHER_GPU:',json.dumps(row))
    (OUT/'report.json').write_text(json.dumps({'budgets':{'min_pixels':30,'min_iou':.65,'max_error':.5},
        'translation':[220,0,-440], 'vertical_budgets':{'min_pixels':30,'min_ratio':.25,'max_ratio':4},
        'elevation':112 if args.vertical else None, 'rows':rows},indent=2)+'\n')
    if not all(row['pass'] for row in rows):
        raise RuntimeError('Weather camera-relative translation/visibility mismatch')
    print('WEATHER_GPU: PASS — snow/rain/fogsprite stay visible after paired camera translation')


if __name__ == '__main__':
    main()
