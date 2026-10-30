#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: raymarch revival gate. Runs the --raymarch scene headlessly; the
#   renderer's GX_GFX_RAYMARCH_CHECK hook saves the live scene through the
#   restored preset schema, mutates fields, reloads from disk and reports the
#   restored values. The gate re-reads the JSON independently and checks the
#   rendered frame.

import json
import re
import shlex
import subprocess
import sys
import tempfile
import time
from contextlib import contextmanager
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
HOOK_RE = re.compile(
    r"GFX_RAYMARCH_CHECK: (PASS|FAIL) save=(\d+) load=(\d+) restored=(\d+) "
    r"sun=([\d.]+) seed=(\d+) water=(\d+) objects=(\d+) lights=(\d+)")
SECTIONS = ("state", "parameters", "fog", "clouds", "volfog", "sun", "terrain",
            "water", "flags", "misc", "debug", "objects", "lights", "textures")
UI_MARKER = "GFX_RAYMARCH_UI: panel=1"
GALLERY_RE = re.compile(
    r"GFX_RAYMARCH_GALLERY: (primitives|reference|new-types) objects=(\d+)")

#ai: retained artifacts and per-probe timing make failures independently reproducible.
PROBE_RESULTS = []


@contextmanager
def artifact_run():
    directory = Path(tempfile.mkdtemp(prefix="gfx_raymarch_", dir="/tmp/opencode"))
    PROBE_RESULTS.clear()
    started = time.monotonic()
    verdict = "FAIL"
    error = None
    try:
        yield str(directory)
        verdict = "PASS"
    except Exception as failure:
        error = str(failure)
        raise
    finally:
        report = {"verdict": verdict, "error": error,
                  "seconds": time.monotonic() - started, "probes": PROBE_RESULTS,
                  "thresholds": {"analytic_holes_max": 0.001,
                                 "analytic_false_hits_max": 0.001,
                                 "default_burst_max_delta": 1.0,
                                 "ui_mean_min": 20.0, "ui_std_min": 5.0,
                                 "ui_control_std_min": 5.0}}
        (directory / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"GFX_RAYMARCH_ARTIFACTS: {directory}", flush=True)


def run_probe(name, function, *args, **kwargs):
    started = time.monotonic()
    record = {"name": name, "verdict": "FAIL"}
    try:
        result = function(*args, **kwargs)
        record.update(verdict="PASS", measurements=result)
        return result
    except Exception as error:
        record["error"] = str(error)
        raise
    finally:
        record["seconds"] = time.monotonic() - started
        PROBE_RESULTS.append(record)


def capture_scene(scene, environment, **kwargs):
    directory = Path(kwargs["screenshot_dir"])
    request = {"scene": scene, "options": {key: str(value) if isinstance(value, Path) else value
               for key, value in kwargs.items()},
               "environment": {key: value for key, value in environment.items()
                               if key.startswith("GX_") or key == "DISPLAY"}}
    (directory / "request.json").write_text(json.dumps(request, indent=2) + "\n")
    check = kwargs.pop("check", True)
    try:
        result = audit.run_scene(scene, environment, check=False, **kwargs)
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        (directory / "engine.log").write_text(output)
        raise RuntimeError(f"{scene} timed out after {error.timeout}s; see {directory}") from error
    (directory / "engine.log").write_text(result.stdout or "")
    replay_env = dict(request["environment"])
    replay_env["GX_SCREENSHOT_DIR"] = str(directory)
    if kwargs.get("frames") is not None:
        replay_env["GX_FRAMES"] = str(kwargs["frames"])
    if kwargs.get("shot_frame") is not None:
        replay_env["GX_SHOT_FRAME"] = str(kwargs["shot_frame"])
    command = "env " + " ".join(shlex.quote(f"{key}={value}")
                                 for key, value in sorted(replay_env.items()))
    (directory / "reproduce.sh").write_text(
        "#!/bin/sh\n" + "cd " + shlex.quote(str(ROOT)) + "\n" + command + " "
        + shlex.join(result.args) + "\n")
    if check and result.returncode:
        raise RuntimeError(f"{scene} exited {result.returncode}; see {directory / 'engine.log'}")
    return result


#ai: reject off-screen, clipped, absent and visually empty control evidence.
def check_item_evidence(log, pixels, required=("selected", "type", "position", "scale")):
    matches = re.findall(r"GFX_RAYMARCH_ITEM: frame=(\d+) name=(\w+) visible=(\d+) "
                         r"rect=([-\d.,]+)", log)
    height, width = pixels.shape[:2]
    frames = {}
    for frame, name, visible, rectangle in matches:
        frames.setdefault(int(frame), {})[name] = (int(visible), rectangle)
    if not frames:
        raise RuntimeError("inspector has no rendered-item evidence")
    evidence = frames[max(frames)]
    for name in required:
        if name not in evidence:
            raise RuntimeError(f"inspector control missing: {name}")
        visible, rectangle = evidence[name]
        bounds = np.asarray([float(value) for value in rectangle.split(",")])
        if bounds.size != 4 or not np.isfinite(bounds).all():
            raise RuntimeError(f"invalid inspector bounds: {name}")
        x0, y0, x1, y1 = bounds
        if not visible or not (0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height):
            raise RuntimeError(f"inspector control not fully visible: {name}")
        roi = pixels[int(np.ceil(y0)):int(y1), int(np.ceil(x0)):int(x1)]
        if not roi.size or float(roi.std()) < 5.0:
            raise RuntimeError(f"inspector control looks empty: {name}")
    return {name: evidence[name][1] for name in evidence}
#opencode: the "Primitives" built-in row — 8 objects at x=-7..7 step 2, z=6,
#   y=1.5, scale 1.5 (fovy 90): screen centers 64 + 128*i, y ~ 288. The
#   five-object new-types row sits at x=-4..4 -> centers 256 + 128*i.
#   Both center sets are symmetric, so the checks stay correct even though
#   the shader camera basis mirrors X; per-type attribution is avoided.
GALLERY_CENTERS = tuple(64 + 128 * i for i in range(8))
GALLERY_NEW_CENTERS = tuple(256 + 128 * i for i in range(5))
GALLERY_ROI_HALF = 46
GALLERY_ROI_HALF_Y = 50
GALLERY_ROI_Y = 288
GALLERY_MIN_OVERALL = 3.0
GALLERY_MIN_OBJECT = 5.0


def check_ui(temp_root, inspect=False, matrix=False):
    """The editor's Raymarch panel draws over the --raymarch scene."""
    directory = temp_root / ("ui_matrix" if matrix else "ui_inspector" if inspect else "ui")
    directory.mkdir(exist_ok=True)
    overrides = {
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "30",
    }
    if inspect:
        overrides["GX_RAYMARCH_EDITOR_INSPECT"] = "1"
    if matrix:
        overrides["GX_RAYMARCH_EDITOR_INSPECT"] = "2"
    environment = audit.scene_env(overrides, clean=True)
    with audit.settings_preserved():
        #opencode: parse order matters — --raymarch must follow --gfx-editor
        #   (the editor flag routes to the GFX scene by default).
        result = capture_scene("--gfx-editor", environment, width=None,
            height=None, fps=None, frames=30, shot_frame=20,
            screenshot_dir=directory, timeout=120, check=True,
            extra_args=("--raymarch",))
    log = audit.strip_ansi(result.stdout or "")
    if UI_MARKER not in log:
        raise RuntimeError("raymarch editor panel did not draw")
    if inspect and "GFX_RAYMARCH_INSPECTOR: selected=0 type=2" not in log:
        raise RuntimeError("selected-object inspector did not draw")
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError("UI capture missing")
    pixels = np.asarray(Image.open(shot).convert("RGB"), dtype=np.float32)
    if inspect:
        evidence = check_item_evidence(log, pixels)
        (directory / "visible_controls.json").write_text(json.dumps(evidence, indent=2) + "\n")
    if matrix:
        evidence = check_item_evidence(log,pixels,("graph_output","graph_slot63","graph_operation"))
        (directory / "visible_controls.json").write_text(json.dumps(evidence,indent=2)+"\n")
    mean = float(pixels.mean())
    std = float(pixels.std())
    if mean < 20.0 or std < 5.0:
        raise RuntimeError(f"UI frame looks empty (mean {mean:.1f}, "
                           f"std {std:.1f})")
    return mean, std


def capture_gallery(temp_root, state, jitter=None, shot_frame=20):
    directory = temp_root / f"gallery{state}_{jitter}_{shot_frame}"
    directory.mkdir(exist_ok=True)
    overrides = {
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "30",
        "GX_GFX_RAYMARCH_GALLERY": str(state),
    }
    if jitter is not None:
        overrides["GX_GFX_RAYMARCH_HIT_JITTER"] = str(jitter)
    environment = audit.scene_env(overrides, clean=True)
    with audit.settings_preserved():
        result = capture_scene("--raymarch", environment, width=None,
            height=None, fps=None, frames=30, shot_frame=shot_frame,
            screenshot_dir=directory, timeout=120, check=True)
    log = audit.strip_ansi(result.stdout or "")
    markers = GALLERY_RE.findall(log)
    if not markers:
        raise RuntimeError("gallery hook did not fire")
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError("gallery capture missing")
    pixels = np.asarray(Image.open(shot).convert("RGB"), dtype=np.float32)
    return pixels, markers[-1]


def roi_delta(delta, center):
    top = GALLERY_ROI_Y - GALLERY_ROI_HALF_Y
    bottom = GALLERY_ROI_Y + GALLERY_ROI_HALF_Y
    return float(delta[top:bottom,
                       center - GALLERY_ROI_HALF:center + GALLERY_ROI_HALF].mean())


def check_gallery(temp_root):
    """Every primitive in the built-in row renders: the full row changes every
    ROI, and the five v0.15 types change all five of their row's ROIs."""
    primitives, primitive_marker = capture_gallery(temp_root, 1)
    reference, reference_marker = capture_gallery(temp_root, 2)
    new_types, new_marker = capture_gallery(temp_root, 3)
    if primitive_marker != ("primitives", "8"):
        raise RuntimeError(f"unexpected gallery marker: {primitive_marker}")
    if reference_marker != ("reference", "0"):
        raise RuntimeError(f"unexpected reference marker: {reference_marker}")
    if new_marker != ("new-types", "5"):
        raise RuntimeError(f"unexpected new-types marker: {new_marker}")
    row_delta = np.abs(primitives - reference).mean(axis=2)
    new_delta = np.abs(new_types - reference).mean(axis=2)
    overall = float(row_delta.mean())
    new_overall = float(new_delta.mean())
    if overall < GALLERY_MIN_OVERALL:
        raise RuntimeError(f"gallery row invisible (overall {overall:.2f})")
    if new_overall < GALLERY_MIN_OVERALL:
        raise RuntimeError(f"new-types row invisible (overall {new_overall:.2f})")
    weak = [f"row slot {index} delta {roi_delta(row_delta, center):.2f}"
            for index, center in enumerate(GALLERY_CENTERS)
            if roi_delta(row_delta, center) < GALLERY_MIN_OBJECT]
    if weak:
        raise RuntimeError("primitive row slots not visible: " + ", ".join(weak))
    weak = [f"new slot {index} delta {roi_delta(new_delta, center):.2f}"
            for index, center in enumerate(GALLERY_NEW_CENTERS)
            if roi_delta(new_delta, center) < GALLERY_MIN_OBJECT]
    if weak:
        raise RuntimeError("new primitives not visible: " + ", ".join(weak))
    return overall, new_overall


#ai: compare opaque sphere/ellipsoid silhouettes to analytic ray intersections;
#   these checks reject missing interior pixels, false hits, and early divergence.
def check_hits(temp_root):
    residuals = []
    for state in (4, 5, 6, 7, 9):
        pixels, _ = capture_gallery(temp_root, state)
        height, width = pixels.shape[:2]
        yy, xx = np.mgrid[:height, :width]
        rays = np.stack((-(xx + 0.5 - width * 0.5),
                         height * 0.5 - yy - 0.5,
                         np.full_like(xx, height * 0.5)), axis=-1)
        rays /= np.linalg.norm(rays, axis=-1, keepdims=True)
        objects = [((0, 0, 6), (3, 1.5, 1), 1.5)] if state == 4 else [
            ((0, 0, 0 if state == 5 else 45 if state == 6 else 6),
             (1, 1, 1), 1.5)]
        if state == 6:
            objects.append(((3, 0, 2), (1, 1, 1), 1.0))
        basis = np.eye(3)
        if state == 9:
            objects = [((0, 0, 6), (2, 2/3, 0.5), 1.5)]
            angle = np.deg2rad(30)
            basis = np.array(((np.cos(angle), -np.sin(angle), 0),
                              (np.sin(angle), np.cos(angle), 0), (0, 0, 1)))

        def mask(radius_scale):
            result = np.zeros((height, width), dtype=bool)
            for center, scale, radius in objects:
                direction = (rays @ basis) * np.asarray(scale)
                origin = (-np.asarray(center) @ basis) * np.asarray(scale)
                a = (direction * direction).sum(axis=-1)
                b = (direction * origin).sum(axis=-1)
                c = (origin * origin).sum() - (radius * radius_scale) ** 2
                disc = b * b - a * c
                exit_t = (-b + np.sqrt(np.maximum(disc, 0))) / a
                result |= (disc >= 0) & (exit_t > 0.01)
            return result

        white = (pixels.min(axis=-1) > 240)
        if state == 7:
            if white.any():
                raise RuntimeError("exhausted step budget fabricated a surface hit")
            continue
        interior = mask(0.97)
        exterior = ~mask(1.03)
        missing = float((interior & ~white).sum()) / max(interior.sum(), 1)
        false = float((exterior & white).sum()) / max(exterior.sum(), 1)
        if missing > 0.001 or false > 0.001:
            Image.fromarray(((interior & ~white).astype(np.uint8) * 255)).save(
                temp_root / f"hits_{state}_missing.png")
            Image.fromarray(((exterior & white).astype(np.uint8) * 255)).save(
                temp_root / f"hits_{state}_false.png")
            raise RuntimeError(f"solid probe {state}: holes={missing:.6f} "
                               f"false hits={false:.6f}")
        residuals.append(f"{state}:{missing:.4f}/{false:.4f}")
    return " ".join(residuals)


#ai: time-only and subpixel-motion comparisons exclude the silhouette so a
#   normal edge transition cannot mask interior white/black speckle instability.
def check_temporal_hits(temp_root):
    baseline, _ = capture_gallery(temp_root, 8, jitter=0)
    height, width = baseline.shape[:2]
    yy, xx = np.mgrid[:height, :width]
    interior = ((xx + 0.5 - width / 2) ** 2
                + (yy + 0.5 - height / 2) ** 2) < (height * 0.10) ** 2
    residuals = []
    for jitter, frame in ((0, 25), (-0.002, 20), (0.002, 20)):
        pixels, _ = capture_gallery(temp_root, 8, jitter=jitter, shot_frame=frame)
        delta = np.abs(pixels - baseline).max(axis=-1)[interior]
        mean = float(delta.mean())
        flashes = float((delta > 40).mean())
        if mean > 2.0 or flashes > 0.001:
            raise RuntimeError(f"white sphere unstable: jitter={jitter} "
                               f"frame={frame} mean={mean:.3f} flashes={flashes:.6f}")
        residuals.append(f"{mean:.3f}/{flashes:.6f}")
    return " ".join(residuals)


#ai: consecutive frames of the actual default light orb catch HDR alpha feedback;
#   separate launches and an authored sphere fixture cannot exercise that history.
def check_default_burst(temp_root):
    directory = temp_root / "default_burst"
    directory.mkdir(exist_ok=True)
    environment = audit.scene_env({"GX_BURST": "1",
                                   "GX_FIXED_DT": "0.0166666667"}, clean=True)
    with audit.settings_preserved():
        capture_scene("--raymarch", environment, frames=40, shot_frame=20,
                        screenshot_dir=directory, screenshot_after=True)
    shots = sorted(directory.glob("burst_*.png"))
    if len(shots) != 12:
        raise RuntimeError(f"default light burst: expected 12 frames, got {len(shots)}")
    frames = np.stack([np.asarray(Image.open(shot).convert("RGB"),
                                 dtype=np.float32) for shot in shots])
    maximum = float(np.ptp(frames, axis=0).max())
    if maximum > 1.0:
        Image.fromarray(np.ptp(frames, axis=0).clip(0, 255).astype(np.uint8)).save(
            directory / "temporal_difference.png")
        raise RuntimeError(f"stationary default scene flickers: max delta {maximum:.1f}")
    return maximum


#ai: cycle every built-in in one live process, not nine fresh module allocations.
def check_preset_cycle(temp_root):
    directory = temp_root / "preset_cycle"
    directory.mkdir(exist_ok=True)
    environment = audit.scene_env({"GX_RAYMARCH_PRESET_CYCLE": "1",
                                   "GX_FIXED_DT": "0.0166666667",
                                   "GX_SHOT_READY_FRAMES": "170"}, clean=True)
    with audit.settings_preserved():
        result = capture_scene("--raymarch", environment, frames=180,
                                shot_frame=170, screenshot_dir=directory,
                                timeout=180)
    log = audit.strip_ansi(result.stdout or "")
    indices = [int(index) for index in re.findall(
        r"RAYMARCH_PRESET_CYCLE: preset=(\d+)", log)]
    if indices != list(range(9)):
        raise RuntimeError(f"preset cycle incomplete: {indices}")
    if "RHI_SHADER: link error" in log or "compile failed" in log:
        raise RuntimeError("preset cycle shader failure")
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError("preset cycle capture missing")
    pixels = np.asarray(Image.open(shot).convert("RGB"), dtype=np.float32)
    if float(pixels.mean()) < 20.0 or float(pixels.std()) < 5.0:
        raise RuntimeError("final preset-cycle frame looks empty")
    return len(indices)


#ai: standalone sky checks cover color/elevation transitions and the former polar NaN/black-sky trap.
def check_sky(temp_root):
    frames = []
    for mode in range(4):
        directory = temp_root / f"sky_{mode}"
        directory.mkdir(exist_ok=True)
        environment = audit.scene_env({"GX_SKY_CHECK": str(mode),
                                       "GX_FIXED_DT": "0.0166666667"}, clean=True)
        with audit.settings_preserved():
            result = capture_scene("--sky", environment, frames=30, shot_frame=20,
                                   screenshot_dir=directory, timeout=120)
        if "compile failed" in (result.stdout or "") or "link error" in (result.stdout or ""):
            raise RuntimeError(f"sky mode {mode}: shader failure")
        shot = audit.newest_shot(directory)
        if shot is None:
            raise RuntimeError(f"sky mode {mode}: capture missing")
        frames.append(audit.load_rgb(shot))
    day, twilight, night, polar = frames
    upper = slice(0, day.shape[0] // 3)
    day_rgb = day[upper].mean(axis=(0,1))
    if day_rgb[2] < day_rgb[0] * 1.2:
        raise RuntimeError(f"day sky is not blue above the horizon: {day_rgb}")
    day_night = audit.mean_abs_diff(day[upper], night[upper])
    day_twilight = audit.mean_abs_diff(day[upper], twilight[upper])
    if day_night < 5 or day_twilight < 5:
        raise RuntimeError(f"sky elevation transitions missing: {day_night}/{day_twilight}")
    polar_visible = float((polar.max(axis=2) > 1).mean())
    if polar_visible < 0.75:
        raise RuntimeError(f"polar conjunction loses its atmospheric sky: {polar_visible}")
    return {"day_night_delta": day_night, "day_twilight_delta": day_twilight,
            "polar_visible_fraction": polar_visible}


#ai: a dense CPU sign-field sweep is independent of the GPU's stepping and DAG traversal.
def check_graph(temp_root, operations=range(2,10)):
    measurements = {}
    for operation in operations:
        directory = temp_root / f"graph_{operation}"
        directory.mkdir(exist_ok=True)
        environment = audit.scene_env({"GX_RAYMARCH_GRAPH_TEST": str(operation),
                                       "GX_FIXED_DT": "0.0166666667"}, clean=True)
        with audit.settings_preserved():
            result = capture_scene("--raymarch",environment,frames=30,shot_frame=20,
                                   screenshot_dir=directory,timeout=180)
        expected_nodes = 2 if operation >= 8 else 3
        if f"RAYMARCH_GRAPH: operation={operation} nodes={expected_nodes}" not in (result.stdout or ""):
            raise RuntimeError(f"graph {operation}: validated program missing")
        shot = audit.newest_shot(directory)
        if shot is None:
            raise RuntimeError(f"graph {operation}: capture missing")
        pixels = audit.load_rgb(shot)
        height,width = pixels.shape[:2]
        yy,xx = np.mgrid[:height:4,:width:4]
        rays = np.stack((-(xx+0.5-width*0.5),height*0.5-yy-0.5,
                         np.full_like(xx,height*0.5)),axis=-1)
        rays /= np.linalg.norm(rays,axis=-1,keepdims=True)
        mask = np.zeros(xx.shape,dtype=bool)
        for depth in np.linspace(3,10,512):
            point = rays*depth
            if operation >= 8:
                nearest = np.rint(point[:,:,0]/2)
                if operation == 9:
                    nearest = np.clip(nearest,-1,1)
                local = point-np.stack((nearest*2,np.zeros_like(nearest),np.full_like(nearest,6)),axis=-1)
                field = np.linalg.norm(local,axis=-1)-0.6
            else:
                a = np.linalg.norm(point-np.array((-0.7,0,6)),axis=-1)-1.2
                b = np.linalg.norm(point-np.array((0.7,0,6)),axis=-1)-1.2
                if operation in (3,6):
                    b = -b
                maximum = operation in (3,4,6,7)
                field = np.maximum(a,b) if maximum else np.minimum(a,b)
                if operation >= 5:
                    blend = np.maximum(0.4-np.abs(a-b),0)/0.4
                    field += (1 if maximum else -1)*0.1*blend*blend
            mask |= field < 0
        padded = np.pad(mask,1)
        neighborhoods = [padded[y:y+mask.shape[0],x:x+mask.shape[1]] for y in range(3) for x in range(3)]
        interior = np.logical_and.reduce(neighborhoods)
        exterior = ~np.logical_or.reduce(neighborhoods)
        if interior.sum() < 100:
            raise RuntimeError(f"graph {operation}: oracle silhouette is empty")
        white = pixels[::4,::4].min(axis=2)>240
        missing = float((interior & ~white).sum())/interior.sum()
        false = float((exterior & white).sum())/max(exterior.sum(),1)
        measurements[str(operation)] = {"holes":missing,"false_hits":false}
        if missing > 0.001 or false > 0.001:
            Image.fromarray((interior & ~white).astype(np.uint8)*255).save(directory/"missing.png")
            Image.fromarray((exterior & white).astype(np.uint8)*255).save(directory/"false.png")
            raise RuntimeError(f"graph {operation}: holes={missing:.6f} false={false:.6f}")
    return measurements


def main():
    if not BIN.is_file():
        print("GFX_RAYMARCH_CHECK: missing binary", file=sys.stderr)
        return 1
    try:
        with artifact_run() as temporary:
            roundtrip_started = time.monotonic()
            folder = Path(temporary) / "check"
            folder.mkdir()
            directory = folder / "shot"
            directory.mkdir()
            overrides = {
                "GX_FIXED_DT": "0.0166666667",
                "GX_FRAMES": "40",
                "GX_GFX_RAYMARCH_CHECK": str(folder),
            }
            environment = audit.scene_env(overrides, clean=True)
            with audit.settings_preserved():
                result = capture_scene("--raymarch", environment, width=None,
                    height=None, fps=None, frames=40, shot_frame=30,
                    screenshot_dir=directory, timeout=120, check=True)
            log = audit.strip_ansi(result.stdout or "")
            hook = HOOK_RE.findall(log)
            if not hook or hook[-1][0] != "PASS":
                raise RuntimeError(f"round-trip hook did not pass: {hook}")
            preset_path = folder / "raymarch_roundtrip.json"
            if not preset_path.is_file():
                raise RuntimeError("preset file missing")
            preset = json.loads(preset_path.read_text())
            for section in SECTIONS:
                if section not in preset:
                    raise RuntimeError(f"preset section missing: {section}")
            sun = float(preset["sun"]["brightness"])
            if abs(sun - 1.234) > 1e-3:
                raise RuntimeError(f"sun.brightness {sun} != 1.234")
            seed = int(preset["terrain"]["seed"])
            if seed != 4242:
                raise RuntimeError(f"terrain.seed {seed} != 4242")
            objects = preset["objects"]
            if int(objects.get("amount", 0)) != 1:
                raise RuntimeError(f"objects.amount {objects.get('amount')}")
            obj = objects.get("obj[0]", {})
            pos = obj.get("pos", [])
            if len(pos) != 3 or abs(pos[0] - 1.5) > 1e-3:
                raise RuntimeError(f"obj[0].pos {pos}")
            if int(obj.get("type", 0)) != 3:
                raise RuntimeError(f"obj[0].type {obj.get('type')}")
            lights = preset["lights"]
            if int(lights.get("amount", 0)) != 1:
                raise RuntimeError(f"lights.amount {lights.get('amount')}")
            light = lights.get("light[0]", {})
            if abs(float(light.get("brightness", 0)) - 2.5) > 1e-3:
                raise RuntimeError(
                    f"light[0].brightness {light.get('brightness')}")
            textures = preset["textures"]
            tex1 = textures.get("tex1", {})
            if tex1.get("path") != "resources/texture/texture_normal.png":
                raise RuntimeError(f"tex1.path {tex1.get('path')}")
            if "skybox" not in textures or "path" not in textures["skybox"]:
                raise RuntimeError("textures.skybox missing")
            if "layout" not in textures["skybox"]:
                raise RuntimeError("textures.skybox.layout missing")
            clouds = preset["clouds"]
            for field in ("bottom", "top", "coverage", "density", "scale",
                          "absorb", "powder"):
                if field not in clouds:
                    raise RuntimeError(f"clouds.{field} missing")
            volfog = preset["volfog"]
            for field in ("density", "heightFalloff", "scattering",
                          "hgAsymmetry"):
                if field not in volfog:
                    raise RuntimeError(f"volfog.{field} missing")
            shot = audit.newest_shot(directory)
            if shot is None:
                raise RuntimeError("capture missing")
            pixels = np.asarray(Image.open(shot).convert("RGB"),
                                dtype=np.float32)
            mean = float(pixels.mean())
            std = float(pixels.std())
            if mean < 20.0 or std < 5.0:
                raise RuntimeError(f"frame looks empty (mean {mean:.1f}, "
                                    f"std {std:.1f})")
            PROBE_RESULTS.append({"name": "roundtrip", "verdict": "PASS",
                                  "seconds": time.monotonic() - roundtrip_started,
                                  "measurements": {"sun": sun, "seed": seed,
                                                   "mean": mean, "std": std}})
            ui_mean, ui_std = run_probe("editor", check_ui, Path(temporary))
            run_probe("inspector", check_ui, Path(temporary), inspect=True)
            run_probe("node_matrix", check_ui, Path(temporary), matrix=True)
            gallery_overall, gallery_new = run_probe("gallery", check_gallery, Path(temporary))
            hit_residuals = run_probe("analytic_hits", check_hits, Path(temporary))
            temporal_residuals = run_probe("temporal_hits", check_temporal_hits, Path(temporary))
            burst_delta = run_probe("default_burst", check_default_burst, Path(temporary))
            preset_count = run_probe("preset_cycle", check_preset_cycle, Path(temporary))
            run_probe("standalone_sky", check_sky, Path(temporary))
            run_probe("CSG_graph", check_graph, Path(temporary))
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        print(f"GFX_RAYMARCH_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"GFX_RAYMARCH_CHECK: PASS preset round-trip (sun 1.234, seed 4242, "
          f"1 object, 1 light) + scene frame (mean {mean:.1f}, std {std:.1f}) "
          f"+ editor panel (mean {ui_mean:.1f}, std {ui_std:.1f}) "
          f"+ primitive gallery (row {gallery_overall:.1f}, "
          f"new-types {gallery_new:.1f}) + analytic hits (holes/false {hit_residuals}) "
          f"+ temporal sphere (mean/flashes {temporal_residuals}) "
          f"+ default light burst (max delta {burst_delta:.1f}) "
          f"+ live preset cycle ({preset_count}/9) + standalone sky (4 modes) + CSG graph (8 operators)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
