#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: deterministic GFX picking and transforms on the owned picker scene.
#   Coordinates come from GX_PICKER_TRACE so the gate does not depend on the
#   demo scene's model indices or the editor's panel layout.

import re
import subprocess
import sys

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
PRESET = ROOT / "resources" / "tools" / "picker.json"
audit.keep_engine_size()

PICK_RE = re.compile(r"GfxEd: viewport click (\d+),(\d+) -> model (-?\d+)")
MODEL_RE = re.compile(r"GFX_PICKER_MODEL: index=(\d+) x=(-?\d+) y=(-?\d+)")
DOCK_RE = re.compile(
    r"GFX_DOCK:.*viewport=\((-?\d+),(-?\d+),(-?\d+),(-?\d+)\)")
GESTURE_RE = re.compile(
    r"GfxEd: gesture end mode=(plane|axis|rotate|scale) axis=(-?\d+) model=(\d+) "
    r"startPos=\(([-\d.]+),([-\d.]+),([-\d.]+)\) "
    r"pos=\(([-\d.]+),([-\d.]+),([-\d.]+)\) "
    r"startRot=\(([-\d.]+),([-\d.]+),([-\d.]+)\) "
    r"rot=\(([-\d.]+),([-\d.]+),([-\d.]+)\) "
    r"startScale=\(([-\d.]+),([-\d.]+),([-\d.]+)\) "
    r"scale=\(([-\d.]+),([-\d.]+),([-\d.]+)\)"
)


def run(actions=None, frames=240, trace=False):
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(frames),
        "GX_GFX_PRESET_LOAD": str(PRESET),
        "GX_GFX_GESTURE_TRACE": "1",
        "GX_DOCK_TRACE": "1",
    }, clean=True)
    if trace:
        environment["GX_PICKER_TRACE"] = "1"
    if actions:
        environment["GX_AI_INPUT"] = actions
    result = audit.run_scene("--gfx", environment, width=None, height=None,
                             fps=None, timeout=120, check=False,
                             extra_args=("--gfx-editor",))
    if result.returncode != 0:
        raise RuntimeError(f"picker editor exited {result.returncode}\n"
                           f"{audit.strip_ansi(result.stdout)[-3000:]}")
    return audit.strip_ansi(result.stdout or "")


def centers():
    log = run(frames=60, trace=True)
    found = {}
    for index, x, y in MODEL_RE.findall(log):
        found[int(index)] = (int(x), int(y))
    if not all(index in found for index in (0, 1, 2)):
        raise RuntimeError(f"picker model trace incomplete: {found}")
    dock = DOCK_RE.findall(log)
    if not dock:
        raise RuntimeError("picker dock trace missing")
    view = tuple(int(value) for value in dock[-1])
    return found, view


def click(log, x, y, model):
    picks = [tuple(map(int, row)) for row in PICK_RE.findall(log)]
    matches = [(px, py, pm) for px, py, pm in picks
               if abs(px - x) <= 8 and abs(py - y) <= 8]
    if not matches:
        raise RuntimeError(f"no pick at {x},{y}; got {picks}")
    if matches[-1][2] != model:
        raise RuntimeError(
            f"pick at {x},{y} selected {matches[-1][2]}, expected {model}")


def gestures(log):
    return GESTURE_RE.findall(log)


def main():
    if not BIN.is_file() or not PRESET.is_file():
        print("GFX_PICKER_CHECK: missing binary or preset", file=sys.stderr)
        return 1
    try:
        found, (view_x, view_y, view_w, view_h) = centers()
        m0, m1 = found[0], found[1]
        sky = (view_x + view_w // 2, view_y + 8)

        clicks = (
            f"1.0 click {m0[0]} {m0[1]};"
            f"1.8 click {m1[0]} {m1[1]};"
            f"2.6 click {sky[0]} {sky[1]}")
        click_log = run(clicks)
        click(click_log, m0[0], m0[1], 0)
        click(click_log, m1[0], m1[1], 1)
        click(click_log, sky[0], sky[1], -1)

        rotate = (
            f"1.0 move {m0[0]} {m0[1]} 100;"
            f"1.2 drag {m0[0]} {m0[1]} {m0[0] + 40} {m0[1]} 400;"
            f"2.2 move {m1[0]} {m1[1]} 100;"
            f"2.4 drag {m1[0]} {m1[1]} {m1[0] + 30} {m1[1] + 20} 400 R;"
            f"3.4 move {m1[0]} {m1[1]} 100;"
            f"3.6 drag {m1[0]} {m1[1]} {m1[0] + 30} {m1[1] + 20} 400 CTRL+R;"
            f"4.6 move {m1[0]} {m1[1]} 100;"
            f"4.8 drag {m1[0]} {m1[1]} {m1[0] + 30} {m1[1]} 400 CTRL+RX")
        rotate_log = run(rotate, frames=480)
        rows = gestures(rotate_log)
        if len(rows) != 4:
            raise RuntimeError(f"rotate gestures: {rows}")
        plane, free, snap, axis = rows
        if plane[:3] != ("plane", "-1", "0"):
            raise RuntimeError(f"plane gesture: {plane}")
        if free[:3] != ("rotate", "-1", "1"):
            raise RuntimeError(f"free rotate gesture: {free}")
        if snap[:3] != ("rotate", "-1", "1"):
            raise RuntimeError(f"snap rotate gesture: {snap}")
        if axis[:3] != ("rotate", "0", "1"):
            raise RuntimeError(f"axis rotate gesture: {axis}")
        if sum(abs(float(a) - float(b)) for a, b in zip(
                plane[3:6], plane[6:9])) < 0.05:
            raise RuntimeError("plane drag did not move the model")
        free_rot = [float(value) for value in free[12:15]]
        snap_rot = [float(value) for value in snap[12:15]]
        if all(abs(value / 15.0 - round(value / 15.0)) < 0.01
               for value in snap_rot) is not True:
            raise RuntimeError(f"snap rotation not snapped: {snap_rot}")
        if not any(abs(value / 15.0 - round(value / 15.0)) > 0.1
                   for value in free_rot):
            raise RuntimeError(f"free rotation was snapped: {free_rot}")
        axis_rot = [float(value) for value in axis[12:15]]
        if abs(axis_rot[0] - float(axis[9])) < 0.01:
            raise RuntimeError(f"axis rotation did not change X: {axis_rot}")
        if any(abs(axis_rot[i] - float(axis[9 + i])) > 0.01 for i in (1, 2)):
            raise RuntimeError(f"axis rotation changed other axes: {axis_rot}")

        translation_rows = []
        for axis_index, chord in enumerate(("X", "Y", "Z")):
            delta = ((30, 0), (0, 30), (-30, -30))[axis_index]
            log = run(
                f"1.0 move {m0[0]} {m0[1]} 100;"
                f"1.2 drag {m0[0]} {m0[1]} {m0[0] + delta[0]} "
                f"{m0[1] + delta[1]} 400 CTRL+{chord}", frames=240)
            rows = gestures(log)
            if len(rows) != 1:
                raise RuntimeError(f"translation gesture {chord}: {rows}")
            translation_rows.append(rows[0])
        for axis_index, row in enumerate(translation_rows):
            if row[:3] != ("axis", str(axis_index), "0"):
                raise RuntimeError(f"translation gesture {axis_index}: {row}")
            start = [float(value) for value in row[3:6]]
            end = [float(value) for value in row[6:9]]
            if abs(end[axis_index] - start[axis_index]) < 0.02:
                raise RuntimeError(f"translation axis {axis_index} did not move")
            if abs(end[axis_index] / 0.25
                   - round(end[axis_index] / 0.25)) > 0.01:
                raise RuntimeError(f"translation axis {axis_index} not snapped")
            if any(abs(end[i] - start[i]) > 0.01
                   for i in range(3) if i != axis_index):
                raise RuntimeError(f"translation axis {axis_index} changed another axis")

        scale_log = run(
            f"1.0 move {m0[0]} {m0[1]} 100;"
            f"1.2 drag {m0[0]} {m0[1]} {m0[0]} {m0[1] - 20} 400 S")
        scale_rows = gestures(scale_log)
        if len(scale_rows) != 1 or scale_rows[0][:3] != ("scale", "-1", "0"):
            raise RuntimeError(f"scale gesture: {scale_rows}")
        start = [float(value) for value in scale_rows[0][15:18]]
        end = [float(value) for value in scale_rows[0][18:21]]
        ratios = [b / a for a, b in zip(start, end)]
        if not all(ratio > 1.0 for ratio in ratios) \
                or max(ratios) - min(ratios) > 0.02:
            raise RuntimeError(f"uniform scale ratios: {ratios}")

        axis_scale_rows = []
        for axis_index, chord in enumerate(("X", "Y", "Z")):
            log = run(
                f"1.0 move {m0[0]} {m0[1]} 100;"
                f"1.2 drag {m0[0]} {m0[1]} {m0[0]} {m0[1] - 20} "
                f"400 S{chord}", frames=240)
            rows = gestures(log)
            if len(rows) != 1:
                raise RuntimeError(f"axis scale gesture {chord}: {rows}")
            axis_scale_rows.append(rows[0])
        for axis_index, row in enumerate(axis_scale_rows):
            if row[:3] != ("scale", str(axis_index), "0"):
                raise RuntimeError(f"axis scale gesture {axis_index}: {row}")
            start = [float(value) for value in row[15:18]]
            end = [float(value) for value in row[18:21]]
            if end[axis_index] <= start[axis_index]:
                raise RuntimeError(f"axis scale {axis_index} did not grow")
            if any(abs(end[i] - start[i]) > 0.01
                   for i in range(3) if i != axis_index):
                raise RuntimeError(f"axis scale {axis_index} changed another axis")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_PICKER_CHECK: FAIL: {error}", file=sys.stderr)
        return 1
    print("GFX_PICKER_CHECK: PASS stable picker scene: picks, plane/rotate/"
          "snap/axis, translations and scale")
    return 0


if __name__ == "__main__":
    sys.exit(main())
