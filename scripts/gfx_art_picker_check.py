#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: original-art same-ray picker regression through GX_AI_INPUT.

import copy
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
PRESET = ROOT / "resources" / "tools" / "art.json"
SCREEN_WIDTH = 1536
SCREEN_HEIGHT = 1016
#opencode: docked viewport inset — the editor reserves the inspector width so
#   the scene fits the visible area (mirrors the GUI dock math).
INSPECTOR_WIDTH = 380.0
DOCK_SCALE_MIN = 0.70
PICK_RE = re.compile(r"GfxEd: viewport click (\d+),(\d+) -> model (-?\d+)")
DISTANCE_RE = re.compile(r"GfxEd: picked model (\d+) at distance ([\d.]+)")


def normalize(vector):
    length = math.sqrt(sum(component * component for component in vector))
    if length <= 1.0e-8:
        raise ValueError("cannot normalize a zero-length vector")
    return [component / length for component in vector]


def dot(first, second):
    return sum(a * b for a, b in zip(first, second))


def cross(first, second):
    return [
        first[1] * second[2] - first[2] * second[1],
        first[2] * second[0] - first[0] * second[2],
        first[0] * second[1] - first[1] * second[0],
    ]


def make_fixtures(original, temporary):
    camera = original["camera"]
    eye = camera["position"]
    target = camera["target"]
    point = [target[0], target[1] + 2.0, target[2]]
    ray_direction = normalize([point[i] - eye[i] for i in range(3)])
    front_position = [point[i] - ray_direction[i] * 0.9 for i in range(3)]

    overlap = copy.deepcopy(original)
    back_model = overlap["models"][1]
    back_model["params"].update({
        "pos": point,
        "scale": [0.8, 0.8, 0.8],
        "rotation": [0.0, 0.0, 0.0],
        "planarReflection": False,
        "nonPlanarReflection": False,
    })
    front_model = copy.deepcopy(back_model)
    front_model["params"]["pos"] = front_position
    overlap["models"].append(front_model)

    back_only = copy.deepcopy(overlap)
    back_only["models"][4]["params"]["pos"] = [100.0, 100.0, 100.0]

    overlap_path = temporary / "overlap.json"
    back_only_path = temporary / "back_only.json"
    overlap_path.write_text(json.dumps(overlap, indent=2) + "\n")
    back_only_path.write_text(json.dumps(back_only, indent=2) + "\n")
    return overlap_path, back_only_path, point


def project_to_window(point, camera, image_size):
    eye = camera["position"]
    forward = normalize([camera["target"][i] - eye[i] for i in range(3)])
    right = normalize(cross(forward, camera["up"]))
    up = normalize(cross(right, forward))
    relative = [point[i] - eye[i] for i in range(3)]
    depth = dot(relative, forward)
    if depth <= 0.0:
        raise ValueError("overlap point is behind the camera")
    tangent = math.tan(math.radians(camera["fovy"]) * 0.5)
    aspect = SCREEN_WIDTH / SCREEN_HEIGHT
    ndc_x = dot(relative, right) / (depth * tangent * aspect)
    ndc_y = dot(relative, up) / (depth * tangent)
    game_x = (ndc_x + 1.0) * SCREEN_WIDTH * 0.5
    game_y = (1.0 - ndc_y) * SCREEN_HEIGHT * 0.5

    window_width, window_height = image_size
    scale = min((window_width - 320.0 - INSPECTOR_WIDTH) / SCREEN_WIDTH,
                window_height / SCREEN_HEIGHT, 1.0)
    if scale < DOCK_SCALE_MIN:
        raise ValueError(f"window is too small for docked GFX editor: {image_size}")
    viewport_y = (window_height - SCREEN_HEIGHT * scale) * 0.5
    return (
        round(320.0 + game_x * scale),
        round(viewport_y + game_y * scale),
    )


def run_capture(name, preset, directory, frame, click=None):
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_GFX_PRESET_LOAD": str(preset),
        "GX_GFX_GESTURE_TRACE": "0",
    }, clean=True)
    if click:
        environment["GX_AI_INPUT"] = (
            f"1.0 move {click[0]} {click[1]} 300;"
            f"1.5 click {click[0]} {click[1]}"
        )
    try:
        with audit.settings_preserved():
            result = audit.run_scene(
                "--gfx", environment, width=SCREEN_WIDTH, height=SCREEN_HEIGHT,
                fps=None, shot_frame=frame, screenshot_dir=directory,
                timeout=120, check=False, extra_args=("--gfx-editor",))
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        raise RuntimeError(f"{name}: timed out\n{output[-3000:]}") from error
    if result.returncode != 0:
        raise RuntimeError(
            f"{name}: process failed ({result.returncode})\n{result.stdout[-3000:]}"
        )
    screenshots = list(directory.glob("gXss_*.png"))
    if not screenshots:
        raise RuntimeError(f"{name}: screenshot missing\n{result.stdout[-3000:]}")
    screenshot = max(screenshots, key=lambda path: path.stat().st_mtime)
    return result.stdout, screenshot


def require_pick(label, output, expected_index, position):
    picks = [tuple(map(int, match)) for match in PICK_RE.findall(output)]
    distances = DISTANCE_RE.findall(output)
    if len(picks) != 1 or len(distances) != 1:
        raise RuntimeError(f"{label}: expected one pick trace, got picks={picks} distances={distances}")
    x, y, selected = picks[0]
    distance_index, distance = distances[0]
    if (abs(x - position[0]) > 2 or abs(y - position[1]) > 2
            or selected != expected_index or int(distance_index) != expected_index):
        raise RuntimeError(
            f"{label}: expected model {expected_index} at {position}, "
            f"got click=({x},{y}) model={selected} distanceModel={distance_index}"
        )
    return float(distance)


def main():
    if not BIN.is_file():
        print(f"GFX_ART_PICKER_CHECK: binary missing: {BIN}", file=sys.stderr)
        return 1
    if not PRESET.is_file():
        print(f"GFX_ART_PICKER_CHECK: original art preset missing: {PRESET}", file=sys.stderr)
        return 1
    original_bytes = PRESET.read_bytes()
    try:
        original = json.loads(original_bytes)
        os.makedirs("/tmp/opencode", exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="gfx_art_picker_",
                                         dir="/tmp/opencode") as temporary:
            temporary = Path(temporary)
            overlap_path, back_only_path, point = make_fixtures(original, temporary)
            _, size_probe = run_capture(
                "window-size probe", overlap_path, temporary / "probe", 120)
            image_size = Image.open(size_probe).size
            click = project_to_window(point, original["camera"], image_size)
            overlap_output, _ = run_capture(
                "overlap", overlap_path, temporary / "overlap", 120, click)
            control_output, _ = run_capture(
                "back-only control", back_only_path,
                temporary / "back_only", 120, click)
            front_distance = require_pick("overlap", overlap_output, 4, click)
            back_distance = require_pick("back-only control", control_output, 1, click)
            if back_distance <= front_distance + 0.5:
                raise RuntimeError(
                    f"front object is not nearer: front={front_distance:.3f} "
                    f"back={back_distance:.3f}"
                )
            if PRESET.read_bytes() != original_bytes:
                raise RuntimeError("preserved original art preset changed during the check")
            print("GFX_ART_PICKER_CHECK: original-art same-ray nearest-hit behavior")
            print(
                f"  overlap winner=4 at {click}, distance={front_distance:.3f}; "
                f"back-only control=1, distance={back_distance:.3f}"
            )
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"GFX_ART_PICKER_CHECK: FAIL: {error}", file=sys.stderr)
        return 1
    print("GFX_ART_PICKER_CHECK: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
