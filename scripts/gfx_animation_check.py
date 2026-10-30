#!/usr/bin/env python3
#opencode: GLB clip playback gate — evaluates the generated animation probe
#   (two joints, one "swing" clip) through the authored preset and proves that
#   (a) the skinned render bends with the clip pose and (b) playback advances,
#   loops/one-shot clamps, and reports through GX_ANIM_TRACE.
#
#   Usage: DISPLAY=:1 python3 scripts/gfx_animation_check.py
#   GPU/display required — use `make gfx-animation-check`.

import json
import re
import shutil
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"
PROBE = "resources/model/probe/animprobe.glb"
FRAME = 90
ANIM_RE = re.compile(
    r"GFX_ANIM: clip=(-?\d+) time=([-0-9.]+) playing=(\d) "
    r"pose=\(([-0-9.]+),([-0-9.]+),([-0-9.]+)\) "
    r"world=\(([-0-9.]+),([-0-9.]+),([-0-9.]+)\)"
)
ANIM_RANGE_RE = re.compile(r"range=\(([-0-9.]+),([-0-9.]+)\)")
#opencode: timeline widget rect + resolved range for virtual-input aiming.
ANIM_UI_RE = re.compile(
    r"GFX_ANIMUI: x0=([-0-9.]+) y0=([-0-9.]+) x1=([-0-9.]+) y1=([-0-9.]+) "
    r"duration=([-0-9.]+) start=([-0-9.]+) end=([-0-9.]+) time=([-0-9.]+)"
)
#opencode: pose-editor widget rects for virtual-input aiming.
ANIM_POSE_RE = re.compile(
    r"GFX_ANIMPOSE: bone=(\d+) over=(\d) keys=(\d+) ovr=\(([-0-9.]+),([-0-9.]+),"
    r"([-0-9.]+),([-0-9.]+)\) rot=\(([-0-9.]+),([-0-9.]+),([-0-9.]+),([-0-9.]+)\) "
    r"key=\(([-0-9.]+),([-0-9.]+),([-0-9.]+),([-0-9.]+)\) "
    r"clr=\(([-0-9.]+),([-0-9.]+),([-0-9.]+),([-0-9.]+)\)"
)
#opencode: blend slider rect + weight for the cross-fade UI test.
ANIM_BLEND_RE = re.compile(
    r"GFX_ANIMBLEND: w=([-0-9.]+) enabled=(\d) "
    r"rect=\(([-0-9.]+),([-0-9.]+),([-0-9.]+),([-0-9.]+)\)"
)
#opencode: blend trace fields on the evaluator line.
ANIM_BLEND_STATE_RE = re.compile(r"blend=([-0-9.]+) clip2=(-?\d+)")
#opencode: Save keys button rect for the sidecar persistence test.
ANIM_SAVE_RE = re.compile(
    r"GFX_ANIMSAVE: rect=\(([-0-9.]+),([-0-9.]+),([-0-9.]+),([-0-9.]+)\)"
)
#opencode: an identity override must not move the pose; a 90-degree Z rotation
#   must (translation/scale stay at the bind values).
IDENTITY_OVERRIDE = [[0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0,
                      1.0, 1.0, 1.0]]
ROTATED_OVERRIDE = [[0.0, 0.0, 0.0, 0.0, 0.7071, 0.0, 0.0, 0.7071,
                     1.0, 1.0, 1.0]]


#opencode: timeline clicks land on pixel-quantized times, so pose comparisons
#   need a small tolerance (0.02 covers ~0.005s of clip motion).
def close_enough(first, second, tolerance=0.02):
    first_values = [float(value) for group in first for value in group]
    second_values = [float(value) for group in second for value in group]
    return all(abs(a - b) <= tolerance
               for a, b in zip(first_values, second_values))
#opencode: the probe is a 0.25 x 2 ribbon scaled 4x at the origin; the swing
#   moves the top half, so a healthy skinning path changes thousands of pixels.
MIN_SWING_PIXELS = 5000
MIN_RIBBON_PIXELS = 2000
#opencode: the sun depth pass must skin too — the probe's shadow on the floor
#   moves with the pose, which only happens when the depth pass shares the
#   bone matrices (a bind-pose shadow would stay put between poses).
MIN_SHADOW_PIXELS = 2000


def make_preset(temp, name, anim_time, playing, loop,
                anim_start=None, anim_end=None, anim_clip=0, overrides=None,
                anim_clip2=None, anim_blend=None):
    base = json.loads(PRESET.read_text())
    if not base.get("models"):
        raise RuntimeError("editor preset has no model entry to clone")
    entry = json.loads(json.dumps(base["models"][0]))
    entry["path"] = PROBE
    entry["params"].update({
        "pos": [0.0, 0.0, 0.0],
        "scale": [4.0, 4.0, 4.0],
        "rotation": [0.0, 0.0, 0.0],
        "usePBR": True,
        "animClip": anim_clip,
        "animTime": anim_time,
        "animPlaying": playing,
        "animLoop": loop,
        "animSpeed": 1.0,
    })
    #opencode: only author the trim when a case needs it, so the other rows
    #   keep the schema defaults (full clip).
    if anim_start is not None:
        entry["params"]["animStart"] = anim_start
    if anim_end is not None:
        entry["params"]["animEnd"] = anim_end
    if overrides is not None:
        entry["params"]["poseOverrideCount"] = len(overrides)
        flat = []
        for values in overrides:
            flat.extend(values)
        entry["params"]["poseOverrides"] = flat
    if anim_clip2 is not None:
        entry["params"]["animClip2"] = anim_clip2
    if anim_blend is not None:
        entry["params"]["animBlend"] = anim_blend
    document = json.loads(json.dumps(base))
    document["models"] = [entry]
    path = temp / f"{name}.json"
    path.write_text(json.dumps(document))
    return path


#opencode: floor + animated caster fixture so the sun depth pass is exercised
#   (the probe alone self-shadows too little to measure).
def make_shadow_preset(temp, name, anim_time, sun_shadows):
    base = json.loads(PRESET.read_text())
    cube = next((model for model in base.get("models", [])
                 if model.get("path", "").endswith("primitive/cube.glb")), None)
    if cube is None:
        raise RuntimeError("editor preset has no cube entry for the floor")
    floor = json.loads(json.dumps(cube))
    floor["params"].update({
        "pos": [0.0, -0.06, 0.0],
        "scale": [10.0, 0.06, 10.0],
        "rotation": [0.0, 0.0, 0.0],
        "usePBR": False,
        "albedo": 1.0,
        "albedoTint": [220, 220, 220, 255],
        "metalness": 0.0,
        "roughness": 0.9,
        "emissiveFactor": 0.0,
        "enableReflections": False,
        "planarReflection": False,
        "nonPlanarReflection": False,
        "receiveSSR": False,
    })
    caster = json.loads(json.dumps(base["models"][0]))
    caster["path"] = PROBE
    caster["params"].update({
        "pos": [0.0, 0.0, 0.0],
        "scale": [4.0, 4.0, 4.0],
        "rotation": [0.0, 0.0, 0.0],
        "usePBR": True,
        "animClip": 0,
        "animTime": anim_time,
        "animPlaying": False,
        "animLoop": False,
        "animSpeed": 1.0,
    })
    document = json.loads(json.dumps(base))
    document["models"] = [floor, caster]
    document["state"]["ENABLE_SHADOWS"] = sun_shadows
    document["state"]["ENABLE_POINT_SHADOWS"] = False
    path = temp / f"{name}.json"
    path.write_text(json.dumps(document))
    return path


def run(preset, frames=180, trace=False, ui_trace=False, focus=False,
        ai_input=None, save=None, pose_trace=False):
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(frames),
        "GX_GFX_PRESET_LOAD": str(preset),
    }, clean=True)
    environment.pop("GX_AI_INPUT", None)
    if trace:
        environment["GX_ANIM_TRACE"] = "1"
    if ui_trace:
        environment["GX_ANIM_UI_TRACE"] = "1"
    if pose_trace:
        environment["GX_ANIMPOSE_TRACE"] = "1"
    if focus:
        environment["GX_GFX_ANIM_FOCUS"] = "1"
    if ai_input:
        environment["GX_AI_INPUT"] = ai_input
    if save is not None:
        environment["GX_GFX_PRESET_SAVE"] = str(save)
    result = audit.run_scene("--gfx", environment, width=1536, height=1016,
                             fps=None, timeout=120, check=False,
                             extra_args=("--gfx-editor",))
    if result.returncode != 0:
        raise RuntimeError(f"editor exited {result.returncode}\n"
                           f"{audit.strip_ansi(result.stdout)[-2000:]}")
    return audit.strip_ansi(result.stdout)


def capture(preset, shot_dir, tag):
    shot_dir.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_GFX_PRESET_LOAD": str(preset),
    }, clean=True)
    environment.pop("GX_AI_INPUT", None)
    audit.run_scene("--gfx", environment, width=1536, height=1016, fps=None,
                    shot_frame=FRAME, screenshot_dir=shot_dir, timeout=120,
                    check=True, extra_args=("--gfx-editor",))
    shot = audit.newest_shot(shot_dir)
    if shot is None:
        raise RuntimeError(f"{tag}: screenshot missing")
    return audit.load_rgb(shot)


def main():
    if not BIN.is_file() or not PRESET.is_file():
        print("GFX_ANIMATION_CHECK: missing binary or preset", file=sys.stderr)
        return 1
    if not (ROOT / PROBE).is_file():
        print(f"GFX_ANIMATION_CHECK: probe missing: {PROBE} "
              f"(run scripts/make_animation_probe.py)", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_animation_",
                                         dir="/tmp/opencode") as temporary:
            temp = Path(temporary)

            def exercise():
                bind_preset = make_preset(temp, "bind", 0.0, False, False)
                swing_preset = make_preset(temp, "swing", 1.0, False, False)
                bind = capture(bind_preset, temp / "bind", "bind")
                swing = capture(swing_preset, temp / "swing", "swing")
                if bind.shape != swing.shape:
                    raise RuntimeError("bind/swing capture size mismatch")
                viewport = (slice(0, bind.shape[0]),
                            slice(340, 1426))
                delta = np.abs(bind[viewport] - swing[viewport]).sum(axis=2)
                changed = int((delta > 10).sum())
                if changed < MIN_SWING_PIXELS:
                    raise RuntimeError(
                        f"skin pose changed only {changed} viewport pixels "
                        f"(floor {MIN_SWING_PIXELS})")
                #opencode: the bind ribbon must be present, so the swing delta
                #   cannot pass on an empty scene.
                region = bind[viewport]
                background = region[5, 5]
                lit = int((np.abs(region - background).sum(axis=2) > 40).sum())
                if lit < MIN_RIBBON_PIXELS:
                    raise RuntimeError(
                        f"probe ribbon not visible ({lit} pixels)")

                loop_preset = make_preset(temp, "loop", 0.0, True, True)
                loop_log = run(loop_preset, frames=240, trace=True)
                loop_rows = [(float(row[1]), int(row[2]))
                             for row in ANIM_RE.findall(loop_log)]
                if len(loop_rows) < 100:
                    raise RuntimeError(
                        f"too few animation trace rows: {len(loop_rows)}")
                times = [row[0] for row in loop_rows]
                #opencode: the loop wraps between samples, so the playhead
                #   approaches but does not land exactly on the clip end.
                if max(times) < 1.5:
                    raise RuntimeError(
                        f"loop playback stalled before the clip end: {max(times):.3f}")
                if not any(t < 0.5 for t in times[60:]):
                    raise RuntimeError("loop playback never wrapped")
                if any(row[1] != 1 for row in loop_rows):
                    raise RuntimeError("loop playback paused unexpectedly")

                once_preset = make_preset(temp, "once", 0.0, True, False)
                once_log = run(once_preset, frames=240, trace=True)
                once_rows = [(float(row[1]), int(row[2]))
                             for row in ANIM_RE.findall(once_log)]
                if len(once_rows) < 100:
                    raise RuntimeError(
                        f"too few one-shot trace rows: {len(once_rows)}")
                if once_rows[-1][1] != 0 or abs(once_rows[-1][0] - 2.0) > 0.05:
                    raise RuntimeError(
                        f"one-shot did not clamp at the clip end: {once_rows[-3:]}")

                #opencode: non-destructive trim — a playhead outside the range
                #   clamps into it, and the clamped pose must equal the pose at
                #   the same time evaluated without a trim.
                trim_preset = make_preset(temp, "trim", 0.75, False, False,
                                          anim_start=1.0, anim_end=2.0)
                trim_log = run(trim_preset, frames=90, trace=True)
                trim_rows = ANIM_RE.findall(trim_log)
                trim_ranges = ANIM_RANGE_RE.findall(trim_log)
                if not trim_rows or not trim_ranges:
                    raise RuntimeError("trim trace rows missing")
                if abs(float(trim_rows[-1][1]) - 1.0) > 0.001:
                    raise RuntimeError(
                        f"trim did not clamp the playhead: {trim_rows[-1][1]}")
                if (abs(float(trim_ranges[-1][0]) - 1.0) > 0.001
                        or abs(float(trim_ranges[-1][1]) - 2.0) > 0.001):
                    raise RuntimeError(
                        f"trim range not reported: {trim_ranges[-1]}")
                reference_log = run(
                    make_preset(temp, "trim_ref", 1.0, False, False),
                    frames=90, trace=True)
                reference_rows = ANIM_RE.findall(reference_log)
                if not reference_rows:
                    raise RuntimeError("trim reference trace missing")
                if trim_rows[-1][3:6] != reference_rows[-1][3:6]:
                    raise RuntimeError(
                        f"trim clamp pose differs from the in-range pose: "
                        f"{trim_rows[-1][3:6]} vs {reference_rows[-1][3:6]}")

                #opencode: playback obeys the trim too — looping wraps inside
                #   the range, one-shot clamps at the range end and stops.
                trim_loop = make_preset(temp, "trim_loop", 0.0, True, True,
                                        anim_start=1.0, anim_end=1.5)
                trim_loop_log = run(trim_loop, frames=240, trace=True)
                trim_loop_rows = [(float(row[1]), int(row[2]))
                                  for row in ANIM_RE.findall(trim_loop_log)]
                if len(trim_loop_rows) < 100:
                    raise RuntimeError(
                        f"too few trimmed loop rows: {len(trim_loop_rows)}")
                trim_times = [row[0] for row in trim_loop_rows]
                if min(trim_times) < 0.999 or max(trim_times) > 1.501:
                    raise RuntimeError(
                        f"trimmed loop left the range: "
                        f"{min(trim_times):.3f}..{max(trim_times):.3f}")
                if not any(t < 1.1 for t in trim_times[60:]):
                    raise RuntimeError(
                        "trimmed loop never wrapped to the range start")
                if any(row[1] != 1 for row in trim_loop_rows):
                    raise RuntimeError("trimmed loop paused unexpectedly")
                trim_once = make_preset(temp, "trim_once", 0.0, True, False,
                                        anim_start=1.0, anim_end=1.5)
                trim_once_log = run(trim_once, frames=240, trace=True)
                trim_once_rows = [(float(row[1]), int(row[2]))
                                  for row in ANIM_RE.findall(trim_once_log)]
                if len(trim_once_rows) < 100:
                    raise RuntimeError(
                        f"too few trimmed one-shot rows: {len(trim_once_rows)}")
                if (trim_once_rows[-1][1] != 0
                        or abs(trim_once_rows[-1][0] - 1.5) > 0.05):
                    raise RuntimeError(
                        f"trimmed one-shot did not clamp at the range end: "
                        f"{trim_once_rows[-3:]}")

                #opencode: the trim keys persist through the preset schema.
                saved = temp / "trim_saved.json"
                run(trim_preset, frames=30, save=saved)
                saved_params = json.loads(saved.read_text())["models"][0]["params"]
                if (abs(float(saved_params.get("animStart", -1.0)) - 1.0) > 1e-6
                        or abs(float(saved_params.get("animEnd", -1.0)) - 2.0) > 1e-6):
                    raise RuntimeError(
                        f"trim range did not round-trip: "
                        f"{saved_params.get('animStart')}/"
                        f"{saved_params.get('animEnd')}")

                #opencode: timeline UI — the trace reports the track rect and
                #   the resolved range; virtual input then scrubs and drags the
                #   trim handle at coordinates derived from that trace.
                ui_preset = make_preset(temp, "ui_timeline", 0.0, False, False,
                                        anim_start=1.0, anim_end=2.0)
                ui_probe = run(ui_preset, frames=90, ui_trace=True, focus=True)
                ui_rows = ANIM_UI_RE.findall(ui_probe)
                if not ui_rows:
                    raise RuntimeError("timeline UI trace missing")
                (ui_x0, ui_y0, ui_x1, ui_y1, ui_duration,
                 ui_start, ui_end, _) = (float(value) for value in ui_rows[-1])
                if ui_duration <= 0.0 or ui_end <= ui_start:
                    raise RuntimeError(f"timeline UI range invalid: {ui_rows[-1]}")
                if ui_x1 <= ui_x0 or ui_y1 <= ui_y0:
                    raise RuntimeError(f"timeline UI rect invalid: {ui_rows[-1]}")
                click_y = 0.5 * (ui_y0 + ui_y1)
                target = ui_start + 0.25 * (ui_end - ui_start)
                click_x = ui_x0 + (target / ui_duration) * (ui_x1 - ui_x0)
                scrub_log = run(ui_preset, frames=150, trace=True, focus=True,
                                ai_input=f"1.0 move {click_x:.1f} {click_y:.1f} 200;"
                                         f"1.4 click {click_x:.1f} {click_y:.1f}")
                scrub_rows = ANIM_RE.findall(scrub_log)
                if not scrub_rows:
                    raise RuntimeError("timeline scrub trace missing")
                scrub_time = float(scrub_rows[-1][1])
                if abs(scrub_time - target) > 0.08:
                    raise RuntimeError(
                        f"timeline scrub landed at {scrub_time:.3f}s, expected "
                        f"{target:.3f}s")
                handle_x = ui_x0 + (ui_end / ui_duration) * (ui_x1 - ui_x0)
                drag_x = ui_x0 + 0.75 * (ui_x1 - ui_x0)
                drag_log = run(ui_preset, frames=180, ui_trace=True, focus=True,
                               ai_input=f"1.0 move {handle_x:.1f} {click_y:.1f} 200;"
                                        f"1.2 drag {handle_x:.1f} {click_y:.1f} "
                                        f"{drag_x:.1f} {click_y:.1f} 500")
                drag_rows = ANIM_UI_RE.findall(drag_log)
                if not drag_rows:
                    raise RuntimeError("trim drag UI trace missing")
                dragged_end = float(drag_rows[-1][6])
                if abs(dragged_end - 1.5) > 0.08:
                    raise RuntimeError(
                        f"trim handle drag set end={dragged_end:.3f}s, expected "
                        f"1.500s")

                #opencode: pose overrides — an identity override reproduces the
                #   sampled pose, a rotated override moves it, and the same
                #   holds with no clip selected (bind pose + override).
                def pose_world(log_text):
                    rows = ANIM_RE.findall(log_text)
                    if not rows:
                        raise RuntimeError("animation trace rows missing")
                    return rows[-1][3:6], rows[-1][6:9]

                base_pose = pose_world(run(
                    make_preset(temp, "pose_none", 0.5, False, False),
                    frames=60, trace=True))
                identity_pose = pose_world(run(
                    make_preset(temp, "pose_identity", 0.5, False, False,
                                overrides=IDENTITY_OVERRIDE),
                    frames=60, trace=True))
                rotated_pose = pose_world(run(
                    make_preset(temp, "pose_rotated", 0.5, False, False,
                                overrides=ROTATED_OVERRIDE),
                    frames=60, trace=True))
                if base_pose != identity_pose:
                    raise RuntimeError(
                        f"identity pose override moved the pose: "
                        f"{base_pose} vs {identity_pose}")
                if rotated_pose == base_pose:
                    raise RuntimeError("rotated pose override had no effect")
                no_clip_identity = pose_world(run(
                    make_preset(temp, "pose_noclip_id", 0.5, False, False,
                                anim_clip=-1, overrides=IDENTITY_OVERRIDE),
                    frames=60, trace=True))
                no_clip_rotated = pose_world(run(
                    make_preset(temp, "pose_noclip_rot", 0.5, False, False,
                                anim_clip=-1, overrides=ROTATED_OVERRIDE),
                    frames=60, trace=True))
                if no_clip_identity == no_clip_rotated:
                    raise RuntimeError(
                        "pose override had no effect without a selected clip")
                #opencode: the override array round-trips through the preset.
                saved_pose = temp / "pose_saved.json"
                run(make_preset(temp, "pose_persist", 0.5, False, False,
                                overrides=ROTATED_OVERRIDE),
                    frames=30, save=saved_pose)
                saved_params = json.loads(
                    saved_pose.read_text())["models"][0]["params"]
                saved_flat = saved_params.get("poseOverrides", [])
                if (int(saved_params.get("poseOverrideCount", 0)) != 1
                        or len(saved_flat) < 11
                        or abs(float(saved_flat[4]) - 0.7071) > 1e-3
                        or abs(float(saved_flat[7]) - 0.7071) > 1e-3):
                    raise RuntimeError(
                        f"pose override did not round-trip: "
                        f"{saved_params.get('poseOverrideCount')}/"
                        f"{saved_flat[:11]}")

                #opencode: pose editing UI — Override, drag the rotation field,
                #   Key: the clip gains keys, the override clears, and the keyed
                #   pose survives a scrub away and back.
                pose_preset = make_preset(temp, "pose_ui", 1.35, False, False)
                pose_probe = run(pose_preset, frames=90, ui_trace=True,
                                 focus=True, pose_trace=True)
                pose_rows = ANIM_POSE_RE.findall(pose_probe)
                if not pose_rows:
                    raise RuntimeError("pose editor trace missing")
                _bone, over, keys, *rect_values = pose_rows[-1]
                if int(over) != 0 or int(keys) <= 0:
                    raise RuntimeError(
                        f"pose probe state unexpected: {pose_rows[-1]}")
                ovr_rect = [float(value) for value in rect_values[0:4]]
                rot_rect = [float(value) for value in rect_values[4:8]]
                key_rect = [float(value) for value in rect_values[8:12]]
                ovr_click = (0.5 * (ovr_rect[0] + ovr_rect[2]),
                             0.5 * (ovr_rect[1] + ovr_rect[3]))
                rot_x = rot_rect[0] + (rot_rect[2] - rot_rect[0]) * 0.125
                rot_y = 0.5 * (rot_rect[1] + rot_rect[3])
                key_click = (0.5 * (key_rect[0] + key_rect[2]),
                             0.5 * (key_rect[1] + key_rect[3]))
                edit_input = (
                    f"0.6 move {ovr_click[0]:.1f} {ovr_click[1]:.1f} 200;"
                    f"0.8 click {ovr_click[0]:.1f} {ovr_click[1]:.1f};"
                    f"1.4 move {rot_x:.1f} {rot_y:.1f} 200;"
                    f"1.6 drag {rot_x:.1f} {rot_y:.1f} "
                    f"{rot_x + 40.0:.1f} {rot_y:.1f} 400;"
                    f"2.6 move {key_click[0]:.1f} {key_click[1]:.1f} 200;"
                    f"2.8 click {key_click[0]:.1f} {key_click[1]:.1f}")
                edit_log = run(pose_preset, frames=240, trace=True,
                               ui_trace=True, focus=True, pose_trace=True,
                               ai_input=edit_input)
                edit_rows = ANIM_POSE_RE.findall(edit_log)
                edit_anim = ANIM_RE.findall(edit_log)
                if not edit_rows or not edit_anim:
                    raise RuntimeError("pose edit trace rows missing")
                if int(edit_rows[-1][2]) < int(edit_rows[0][2]) + 3:
                    raise RuntimeError(
                        f"pose Key did not add keys: {edit_rows[0][2]} -> "
                        f"{edit_rows[-1][2]}")
                if int(edit_rows[-1][1]) != 0:
                    raise RuntimeError("pose override did not clear after Key")
                keyed_pose = (edit_anim[-1][3:6], edit_anim[-1][6:9])
                ui_rows = ANIM_UI_RE.findall(edit_log)
                if not ui_rows:
                    raise RuntimeError(
                        "timeline trace missing during the pose edit")
                (ui_x0, ui_y0, ui_x1, ui_y1, ui_duration,
                 ui_start, _ui_end, _) = (float(value)
                                          for value in ui_rows[-1])
                ui_y = 0.5 * (ui_y0 + ui_y1)
                scrub_away_x = ui_x0 + (ui_start / ui_duration) * (ui_x1 - ui_x0)
                scrub_back_x = ui_x0 + (1.35 / ui_duration) * (ui_x1 - ui_x0)
                roundtrip_input = (
                    edit_input +
                    f";4.0 move {scrub_away_x:.1f} {ui_y:.1f} 200;"
                    f"4.2 click {scrub_away_x:.1f} {ui_y:.1f};"
                    f"5.2 move {scrub_back_x:.1f} {ui_y:.1f} 200;"
                    f"5.4 click {scrub_back_x:.1f} {ui_y:.1f}")
                roundtrip_log = run(pose_preset, frames=420, trace=True,
                                    ui_trace=True, focus=True, pose_trace=True,
                                    ai_input=roundtrip_input)
                roundtrip_anim = ANIM_RE.findall(roundtrip_log)
                if not roundtrip_anim:
                    raise RuntimeError("pose round-trip trace missing")
                roundtrip_pose = (roundtrip_anim[-1][3:6],
                                  roundtrip_anim[-1][6:9])
                if not close_enough(roundtrip_pose, keyed_pose):
                    raise RuntimeError(
                        f"keyed pose did not survive a scrub round trip: "
                        f"{roundtrip_pose} vs {keyed_pose}")
                unkeyed_pose = pose_world(run(pose_preset, frames=60,
                                              trace=True))
                if close_enough(roundtrip_pose, unkeyed_pose):
                    raise RuntimeError(
                        "keyed pose matches the un-keyed clip pose — the new "
                        "key was not sampled")

                #opencode: authored keys persist in a `<model>.anim.json`
                #   sidecar (materials-sidecar convention): save from the editor
                #   UI, then reload the model in a fresh process and confirm the
                #   keyed pose comes back (and differs from the un-keyed clip).
                sidecar_dir = temp / "sidecar"
                sidecar_dir.mkdir()
                sidecar_model = sidecar_dir / "animprobe.glb"
                shutil.copyfile(ROOT / PROBE, sidecar_model)
                sidecar_preset = make_preset(temp, "sidecar", 1.35, False, False)
                sidecar_doc = json.loads(sidecar_preset.read_text())
                sidecar_doc["models"][0]["path"] = str(sidecar_model)
                sidecar_preset.write_text(json.dumps(sidecar_doc))
                sidecar_probe = run(sidecar_preset, frames=90, trace=True,
                                    ui_trace=True, focus=True, pose_trace=True)
                sidecar_pose_rows = ANIM_POSE_RE.findall(sidecar_probe)
                sidecar_save_rows = ANIM_SAVE_RE.findall(sidecar_probe)
                if not sidecar_pose_rows or not sidecar_save_rows:
                    raise RuntimeError("sidecar probe traces missing")
                _bone, _over, _keys, *sidecar_rects = sidecar_pose_rows[-1]
                s_ovr = [float(value) for value in sidecar_rects[0:4]]
                s_rot = [float(value) for value in sidecar_rects[4:8]]
                s_key = [float(value) for value in sidecar_rects[8:12]]
                s_save = [float(value) for value in sidecar_save_rows[-1]]
                s_ovr_click = (0.5 * (s_ovr[0] + s_ovr[2]),
                               0.5 * (s_ovr[1] + s_ovr[3]))
                s_rot_x = s_rot[0] + (s_rot[2] - s_rot[0]) * 0.125
                s_rot_y = 0.5 * (s_rot[1] + s_rot[3])
                s_key_click = (0.5 * (s_key[0] + s_key[2]),
                               0.5 * (s_key[1] + s_key[3]))
                s_save_click = (0.5 * (s_save[0] + s_save[2]),
                                0.5 * (s_save[1] + s_save[3]))
                sidecar_input = (
                    f"0.6 move {s_ovr_click[0]:.1f} {s_ovr_click[1]:.1f} 200;"
                    f"0.8 click {s_ovr_click[0]:.1f} {s_ovr_click[1]:.1f};"
                    f"1.4 move {s_rot_x:.1f} {s_rot_y:.1f} 200;"
                    f"1.6 drag {s_rot_x:.1f} {s_rot_y:.1f} "
                    f"{s_rot_x + 40.0:.1f} {s_rot_y:.1f} 400;"
                    f"2.6 move {s_key_click[0]:.1f} {s_key_click[1]:.1f} 200;"
                    f"2.8 click {s_key_click[0]:.1f} {s_key_click[1]:.1f};"
                    f"3.6 move {s_save_click[0]:.1f} {s_save_click[1]:.1f} 200;"
                    f"3.8 click {s_save_click[0]:.1f} {s_save_click[1]:.1f}")
                sidecar_edit = run(sidecar_preset, frames=240, trace=True,
                                   ui_trace=True, focus=True, pose_trace=True,
                                   ai_input=sidecar_input)
                sidecar_file = sidecar_model.with_suffix("").with_suffix(
                    ".anim.json")
                if not sidecar_file.is_file():
                    raise RuntimeError("Save keys did not write the sidecar")
                sidecar_saved = json.loads(sidecar_file.read_text())
                if sidecar_saved.get("format") != "gx-anim-v1":
                    raise RuntimeError("sidecar format marker missing")
                sidecar_keys = sidecar_saved["clips"][0]["keys"]
                keyed_at_time = [key for key in sidecar_keys
                                 if abs(float(key["time"]) - 1.35) < 1e-6]
                if len(keyed_at_time) < 3:
                    raise RuntimeError(
                        f"sidecar missing the authored keys: {sidecar_keys}")
                sidecar_reload = run(sidecar_preset, frames=90, trace=True)
                edit_anim = ANIM_RE.findall(sidecar_edit)
                reload_anim = ANIM_RE.findall(sidecar_reload)
                probe_anim = ANIM_RE.findall(sidecar_probe)
                if not edit_anim or not reload_anim or not probe_anim:
                    raise RuntimeError("sidecar trace rows missing")
                keyed_pose = (edit_anim[-1][3:6], edit_anim[-1][6:9])
                reload_pose = (reload_anim[-1][3:6], reload_anim[-1][6:9])
                unkeyed_sidecar_pose = (probe_anim[-1][3:6],
                                        probe_anim[-1][6:9])
                if not close_enough(reload_pose, keyed_pose):
                    raise RuntimeError(
                        f"sidecar reload pose differs: {reload_pose} vs "
                        f"{keyed_pose}")
                if close_enough(reload_pose, unkeyed_sidecar_pose):
                    raise RuntimeError(
                        "sidecar reload matches the un-keyed clip — keys did "
                        "not load")

                #opencode: clip blending — weight 0 is the primary pose, weight
                #   1 the secondary, the mid blend differs from both, and a
                #   missing secondary is a no-op.
                swing_pose = pose_world(run(
                    make_preset(temp, "blend_primary", 0.5, False, False),
                    frames=60, trace=True))
                twist_pose = pose_world(run(
                    make_preset(temp, "blend_twist", 0.5, False, False,
                                anim_clip=1),
                    frames=60, trace=True))
                zero_pose = pose_world(run(
                    make_preset(temp, "blend_zero", 0.5, False, False,
                                anim_clip2=1, anim_blend=0.0),
                    frames=60, trace=True))
                full_pose = pose_world(run(
                    make_preset(temp, "blend_full", 0.5, False, False,
                                anim_clip2=1, anim_blend=1.0),
                    frames=60, trace=True))
                mid_pose = pose_world(run(
                    make_preset(temp, "blend_mid", 0.5, False, False,
                                anim_clip2=1, anim_blend=0.5),
                    frames=60, trace=True))
                if zero_pose != swing_pose:
                    raise RuntimeError(
                        f"blend weight 0 changed the primary pose: "
                        f"{zero_pose} vs {swing_pose}")
                if full_pose != twist_pose:
                    raise RuntimeError(
                        f"blend weight 1 differs from the secondary clip: "
                        f"{full_pose} vs {twist_pose}")
                if mid_pose == swing_pose or mid_pose == twist_pose:
                    raise RuntimeError(
                        f"mid blend matches an endpoint: {mid_pose}")
                missing_pose = pose_world(run(
                    make_preset(temp, "blend_missing", 0.5, False, False,
                                anim_blend=0.7),
                    frames=60, trace=True))
                if missing_pose != swing_pose:
                    raise RuntimeError(
                        "blend without a secondary clip moved the pose")
                #opencode: the secondary (twist, 1.0s) wraps inside its own
                #   duration when looping and clamps to its end otherwise.
                loop_pose = pose_world(run(
                    make_preset(temp, "blend_loop", 1.5, False, True,
                                anim_clip2=1, anim_blend=1.0),
                    frames=60, trace=True))
                if loop_pose != twist_pose:
                    raise RuntimeError(
                        f"looped secondary did not wrap to its own 0.5s pose: "
                        f"{loop_pose} vs {twist_pose}")
                clamp_pose = pose_world(run(
                    make_preset(temp, "blend_clamp", 1.5, False, False,
                                anim_clip2=1, anim_blend=1.0),
                    frames=60, trace=True))
                twist_end = pose_world(run(
                    make_preset(temp, "blend_twist_end", 1.0, False, False,
                                anim_clip=1),
                    frames=60, trace=True))
                if clamp_pose != twist_end:
                    raise RuntimeError(
                        f"clamped secondary did not hold its end pose: "
                        f"{clamp_pose} vs {twist_end}")
                #opencode: the blend keys persist through the preset schema and
                #   the trace reports the effective weight/clip.
                saved_blend = temp / "blend_saved.json"
                run(make_preset(temp, "blend_persist", 0.5, False, False,
                                anim_clip2=1, anim_blend=0.4),
                    frames=30, save=saved_blend)
                blend_params = json.loads(
                    saved_blend.read_text())["models"][0]["params"]
                if (int(blend_params.get("animClip2", -1)) != 1
                        or abs(float(blend_params.get("animBlend", -1)) - 0.4)
                        > 1e-6):
                    raise RuntimeError(
                        f"blend settings did not round-trip: "
                        f"{blend_params.get('animClip2')}/"
                        f"{blend_params.get('animBlend')}")
                blend_state = ANIM_BLEND_STATE_RE.findall(run(
                    make_preset(temp, "blend_state", 0.5, False, False,
                                anim_clip2=1, anim_blend=0.5),
                    frames=60, trace=True))
                if not blend_state:
                    raise RuntimeError("blend trace fields missing")
                if (abs(float(blend_state[-1][0]) - 0.5) > 1e-6
                        or int(blend_state[-1][1]) != 1):
                    raise RuntimeError(
                        f"blend trace state wrong: {blend_state[-1]}")

                #opencode: the blend slider is real UI — drag it and verify the
                #   effective weight and pose both move.
                blend_preset = make_preset(temp, "blend_ui", 0.5, False, False,
                                           anim_clip2=1, anim_blend=0.0)
                blend_probe = run(blend_preset, frames=90, ui_trace=True,
                                  focus=True)
                blend_rows = ANIM_BLEND_RE.findall(blend_probe)
                if not blend_rows:
                    raise RuntimeError("blend slider trace missing")
                _weight, enabled, *blend_rect = blend_rows[-1]
                if int(enabled) != 1:
                    raise RuntimeError(
                        "blend slider not enabled for the fixture")
                slider_x0 = float(blend_rect[0])
                slider_y = 0.5 * (float(blend_rect[1]) + float(blend_rect[3]))
                drag_input = (
                    f"0.8 move {slider_x0 + 10.0:.1f} {slider_y:.1f} 200;"
                    f"1.0 drag {slider_x0 + 10.0:.1f} {slider_y:.1f} "
                    f"{slider_x0 + 70.0:.1f} {slider_y:.1f} 400")
                dragged = run(blend_preset, frames=180, trace=True,
                              ui_trace=True, focus=True, ai_input=drag_input)
                dragged_state = ANIM_BLEND_STATE_RE.findall(dragged)
                dragged_anim = ANIM_RE.findall(dragged)
                if not dragged_state or not dragged_anim:
                    raise RuntimeError("dragged blend trace missing")
                dragged_weight = float(dragged_state[-1][0])
                if dragged_weight < 0.3:
                    raise RuntimeError(
                        f"blend slider drag set only {dragged_weight:.3f}")
                dragged_pose = (dragged_anim[-1][3:6], dragged_anim[-1][6:9])
                if close_enough(dragged_pose, swing_pose):
                    raise RuntimeError(
                        "blend slider drag did not move the blended pose")

                #opencode: shadow skinning — the depth pass must use the same
                #   bone matrices, otherwise the floor shadow stays in the bind
                #   pose while the model bends. Pixels that change with the pose
                #   only while shadows are enabled are shadow-only pixels.
                shadow_bind = capture(
                    make_shadow_preset(temp, "shadow_bind", 0.0, True),
                    temp / "shadow_bind", "shadow_bind")
                shadow_swing = capture(
                    make_shadow_preset(temp, "shadow_swing", 1.0, True),
                    temp / "shadow_swing", "shadow_swing")
                plain_bind = capture(
                    make_shadow_preset(temp, "plain_bind", 0.0, False),
                    temp / "plain_bind", "plain_bind")
                plain_swing = capture(
                    make_shadow_preset(temp, "plain_swing", 1.0, False),
                    temp / "plain_swing", "plain_swing")
                with_shadows = (np.abs(shadow_bind[viewport]
                                       - shadow_swing[viewport]).sum(axis=2) > 10)
                without_shadows = (np.abs(plain_bind[viewport]
                                          - plain_swing[viewport]).sum(axis=2) > 10)
                shadow_only = int((with_shadows & ~without_shadows).sum())
                if shadow_only < MIN_SHADOW_PIXELS:
                    raise RuntimeError(
                        f"pose changed only {shadow_only} shadow-only pixels "
                        f"(floor {MIN_SHADOW_PIXELS}) — the depth pass is not "
                        f"skinning")

            with audit.settings_preserved():
                exercise()
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_ANIMATION_CHECK: FAIL: {error}", file=sys.stderr)
        return 1
    print("GFX_ANIMATION_CHECK: PASS skinned pose bends, shadows follow the "
          "pose, loop wraps, one-shot clamps, trim clamps/loops/persists, "
          "timeline scrubs and drags, pose overrides apply/round-trip, the "
          "editor keys them, keys persist through the sidecar, and clip "
          "blending endpoints/wrap/clamp/UI pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
