#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: deterministic GFX picking and transforms through the virtual-input bridge.

import re
import subprocess
import sys

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN

#opencode: absolute geometry in this gate's assertions — keep the engine size.
audit.keep_engine_size()
PICK_RE = re.compile(r"GfxEd: viewport click (\d+),(\d+) -> model (-?\d+)")
OVERLAP_RE = re.compile(r"GFX_PICKER_OVERLAP: back=(\d+) front=(\d+)")
#opencode: these coordinates were calibrated on the pre-inset dock (1806x1016
#   window, viewport (320,16.5,1486,983)). The dock now reserves the inspector
#   width, so the same world rays land at different window pixels: every
#   viewport coordinate below is the original mapped through game space
#   (old scale 1486/1536 -> new (1806-320-380)/1536). Panel coordinates
#   (x > 1426) are unchanged.
PICKER_INPUT = (
    "1.5 click 662 381;"
    "2.25 click 662 584;"
    "3.0 click 900 381;"
    "3.75 click 342 502;"
    "4.5 drag 677 190 751 190 450;"
    #opencode: park over the next target before pressing so the ImGui hover
    #   transition cannot consume the scripted drag's initial motion.
    "5.0 move 662 381 200;"
    "5.25 drag 662 381 751 381 600;"
    "6.25 click 662 381;"
    "7.0 click 751 381;"
    "7.75 drag 900 381 923 396 500 R;"
    "8.5 drag 900 381 923 396 500 CTRL+R;"
    "9.25 drag 900 381 923 381 500 CTRL+RX;"
    "10.0 drag 751 381 759 381 500 CTRL+X;"
    "10.75 drag 751 381 759 388 500 CTRL+Y;"
    "11.5 drag 751 381 759 388 500 CTRL+Z;"
    "12.25 click 1500 820"
)
EXPECTED = [
    (662, 381, 4),
    (662, 584, 6),
    (900, 381, 2),
    #opencode: re-baselined 2026-10-14 — the demo content's fixture slot
    #   became the normal-map cylinder (material-map diagnostic work), which
    #   is large enough to win this overlap ray against the platform slab:
    #   the nearest-hit pick is now the cylinder (5), not the slab (7).
    (342, 502, 5),
    (677, 190, -1),
    (662, 381, 4),
    (662, 381, -1),
    (751, 381, 4),
    (900, 381, 2),
    (900, 381, 2),
    (900, 381, 2),
    (751, 381, 4),
    (751, 381, 4),
    (751, 381, 4),
]
POSITION_TOLERANCE = 8
GESTURE_RE = re.compile(
    r"GfxEd: gesture end mode=(plane|axis|rotate|scale) axis=(-?\d+) model=(\d+) "
    r"startPos=\(([-\d.]+),([-\d.]+),([-\d.]+)\) "
    r"pos=\(([-\d.]+),([-\d.]+),([-\d.]+)\) "
    r"startRot=\(([-\d.]+),([-\d.]+),([-\d.]+)\) "
    r"rot=\(([-\d.]+),([-\d.]+),([-\d.]+)\) "
    r"startScale=\(([-\d.]+),([-\d.]+),([-\d.]+)\) "
    r"scale=\(([-\d.]+),([-\d.]+),([-\d.]+)\)"
)
VIEW_RE = re.compile(r" view=(\d)")
HISTORY_RE = re.compile(
    r"GfxEd: history (undo|redo) model=(-?\d+) "
    r"pos=\(([-\d.]+),([-\d.]+),([-\d.]+)\) undo=(\d+) redo=(\d+)"
)
CAMERA_RE = re.compile(
    r"GfxEd: camera gesture mode=(pan|orbit) "
    r"startPos=\(([-\d.]+),([-\d.]+),([-\d.]+)\) "
    r"endPos=\(([-\d.]+),([-\d.]+),([-\d.]+)\) "
    r"startTarget=\(([-\d.]+),([-\d.]+),([-\d.]+)\) "
    r"endTarget=\(([-\d.]+),([-\d.]+),([-\d.]+)\)"
)


def main():
    if not BIN.is_file():
        print(f"GFX_PICKER_CHECK: binary missing: {BIN}", file=sys.stderr)
        return 1
    environment = audit.scene_env({
        "GX_AI_INPUT": PICKER_INPUT,
        "GX_GFX_GESTURE_TRACE": "1",
        "GX_FRAMES": "900",
        "GX_FIXED_DT": "0.0166666667",
    }, clean=True)
    try:
        result = audit.run_scene("--gfx", environment, width=None, height=None, fps=None, timeout=60, check=False, extra_args=("--gfx-editor",))
    except subprocess.TimeoutExpired:
        print("GFX_PICKER_CHECK: timeout", file=sys.stderr)
        return 1
    if result.returncode != 0:
        print(result.stdout, end="")
        print(f"GFX_PICKER_CHECK: process failed ({result.returncode})", file=sys.stderr)
        return 1
    actual = [tuple(map(int, match)) for match in PICK_RE.findall(result.stdout)]
    positions_match = len(actual) == len(EXPECTED) and all(
        abs(actualX - expectedX) <= POSITION_TOLERANCE
        and abs(actualY - expectedY) <= POSITION_TOLERANCE
        and actualModel == expectedModel
        for (actualX, actualY, actualModel), (expectedX, expectedY, expectedModel)
        in zip(actual, EXPECTED)
    )
    if not positions_match:
        print(result.stdout, end="")
        print(
            f"GFX_PICKER_CHECK: expected {EXPECTED} (position tolerance "
            f"{POSITION_TOLERANCE}px), got {actual}",
            file=sys.stderr,
        )
        return 1
    gestures = GESTURE_RE.findall(result.stdout)
    if len(gestures) != 7:
        print(result.stdout, end="")
        print(f"GFX_PICKER_CHECK: expected 7 completed gestures, got {gestures}", file=sys.stderr)
        return 1
    plane, free_rotation, snap_rotation, axis_rotation = gestures[:4]
    axis_translations = gestures[4:]
    gesture_modes_match = (
        plane[:3] == ("plane", "-1", "4")
        and free_rotation[:3] == ("rotate", "-1", "2")
        and snap_rotation[:3] == ("rotate", "-1", "2")
        and axis_rotation[:3] == ("rotate", "0", "2")
        and [gesture[:3] for gesture in axis_translations]
        == [("axis", str(axis), "4") for axis in range(3)]
    )
    gesture_values = [tuple(map(float, values[3:])) for values in gestures]
    free_rotation_values = gesture_values[1]
    snap_rotation_values = gesture_values[2]
    axis_rotation_values = gesture_values[3]
    freeRotationStartPos = free_rotation_values[:3]
    freeRotationPos = free_rotation_values[3:6]
    freeRotationStart = free_rotation_values[6:9]
    freeRotationEnd = free_rotation_values[9:12]
    axisRotationStartPos = axis_rotation_values[:3]
    axisRotationPos = axis_rotation_values[3:6]
    axisRotationStart = axis_rotation_values[6:9]
    axisRotationEnd = axis_rotation_values[9:12]
    snapRotationEnd = snap_rotation_values[9:12]
    gesture_values_match = (
        all(abs(a - b) < 0.01 for a, b in zip(freeRotationStartPos, freeRotationPos))
        and abs(freeRotationEnd[0] - freeRotationStart[0]) > 1.0
        and abs(freeRotationEnd[1] - freeRotationStart[1]) > 1.0
        and abs(freeRotationEnd[2] - freeRotationStart[2]) < 0.01
        and abs(freeRotationEnd[0] / 15.0 - round(freeRotationEnd[0] / 15.0)) > 0.1
        and all(abs(value / 15.0 - round(value / 15.0)) < 0.01
            for value in snapRotationEnd)
        and all(abs(a - b) < 0.01 for a, b in zip(axisRotationStartPos, axisRotationPos))
        and abs(axisRotationEnd[0] - axisRotationStart[0]) > 1.0
        and abs(axisRotationEnd[1] - axisRotationStart[1]) < 0.01
        and abs(axisRotationEnd[2] - axisRotationStart[2]) < 0.01
        and all(abs(value / 15.0 - round(value / 15.0)) < 0.01
            for value in axisRotationEnd)
        and all(
            abs(end[axis] - start[axis]) > 0.02
            and abs(end[axis] / 0.25 - round(end[axis] / 0.25)) < 0.01
            and all(abs(end[component] - start[component]) < 0.01
                for component in range(3) if component != axis)
            for axis, values in enumerate(gesture_values[4:])
            for start, end in ((values[:3], values[3:6]),)
        )
    )
    if not gesture_modes_match or not gesture_values_match:
        print(result.stdout, end="")
        print(f"GFX_PICKER_CHECK: gesture behavior mismatch {gestures}", file=sys.stderr)
        return 1

    #opencode: scale has its own short input pass to keep the shared 16-action
    #   virtual-input buffer intact for the existing picker/mouse-capture fixture.
    scale_environment = audit.scene_env({
        "GX_AI_INPUT": "1.5 move 751 381 200;1.75 drag 751 381 751 366 500 S",
        "GX_GFX_GESTURE_TRACE": "1",
        "GX_FRAMES": "300",
        "GX_FIXED_DT": "0.0166666667",
    }, clean=True)
    try:
        scale_result = audit.run_scene("--gfx", scale_environment, width=None, height=None, fps=None, timeout=60, check=False, extra_args=("--gfx-editor",))
    except subprocess.TimeoutExpired:
        print("GFX_PICKER_CHECK: scale run timeout", file=sys.stderr)
        return 1
    scale_picks = [tuple(map(int, match)) for match in PICK_RE.findall(scale_result.stdout)]
    scale_gestures = GESTURE_RE.findall(scale_result.stdout)
    scale_values = tuple(map(float, scale_gestures[0][3:])) if scale_gestures else ()
    scale_start = scale_values[12:15]
    scale_end = scale_values[15:18]
    scale_ratios = [end / start for start, end in zip(scale_start, scale_end)]
    scale_ok = (
        scale_result.returncode == 0
        and len(scale_picks) == 1
        and scale_picks[0][2] == 7
        and len(scale_gestures) == 1
        and scale_gestures[0][:3] == ("scale", "-1", "7")
        and len(scale_ratios) == 3
        and all(ratio > 1.0 for ratio in scale_ratios)
        and max(scale_ratios) - min(scale_ratios) < 0.02
    )
    if not scale_ok:
        print(scale_result.stdout, end="")
        print(f"GFX_PICKER_CHECK: scale behavior mismatch {scale_gestures}", file=sys.stderr)
        return 1

    #opencode: constrain S-drag to each world axis and keep the other extents unchanged.
    axis_scale_environment = audit.scene_env({
        "GX_AI_INPUT": (
            "1.5 move 751 381 200;1.75 drag 751 381 751 366 500 SX;"
            "2.5 move 751 381 200;2.75 drag 751 381 751 366 500 SY;"
            "3.5 move 751 381 200;3.75 drag 751 381 751 366 500 SZ"
        ),
        "GX_GFX_GESTURE_TRACE": "1",
        "GX_FRAMES": "480",
        "GX_FIXED_DT": "0.0166666667",
    }, clean=True)
    try:
        axis_scale_result = audit.run_scene("--gfx", axis_scale_environment, width=None, height=None, fps=None, timeout=60, check=False, extra_args=("--gfx-editor",))
    except subprocess.TimeoutExpired:
        print("GFX_PICKER_CHECK: axis scale run timeout", file=sys.stderr)
        return 1
    axis_scale_picks = [
        tuple(map(int, match)) for match in PICK_RE.findall(axis_scale_result.stdout)
    ]
    axis_scale_gestures = GESTURE_RE.findall(axis_scale_result.stdout)
    axis_scale_ok = (
        axis_scale_result.returncode == 0
        and len(axis_scale_picks) == 3
        and all(pick[2] == 7 for pick in axis_scale_picks)
        and len(axis_scale_gestures) == 3
        and all(
            gesture[:3] == ("scale", str(axis), "7")
            and len(gesture[3:]) == 18
            and float(gesture[3 + 15 + axis]) > float(gesture[3 + 12 + axis])
            and all(
                abs(float(gesture[3 + 15 + other])
                    - float(gesture[3 + 12 + other])) < 0.01
                for other in range(3) if other != axis
            )
            for axis, gesture in enumerate(axis_scale_gestures)
        )
    )
    if not axis_scale_ok:
        print(axis_scale_result.stdout, end="")
        print(
            f"GFX_PICKER_CHECK: axis scale behavior mismatch {axis_scale_gestures}",
            file=sys.stderr,
        )
        return 1

    #opencode: held-V moves on the camera view plane; the same vertical drag on
    #   the default XZ plane changes X/Z and leaves Y untouched.
    view_environment = audit.scene_env({
        "GX_AI_INPUT": "1.5 move 751 381 200;1.75 drag 751 381 751 321 500 V",
        "GX_GFX_GESTURE_TRACE": "1",
        "GX_FRAMES": "240",
        "GX_FIXED_DT": "0.0166666667",
    }, clean=True)
    try:
        view_result = audit.run_scene("--gfx", view_environment, width=None, height=None, fps=None, timeout=60, check=False, extra_args=("--gfx-editor",))
    except subprocess.TimeoutExpired:
        print("GFX_PICKER_CHECK: view-plane run timeout", file=sys.stderr)
        return 1
    view_gestures = GESTURE_RE.findall(view_result.stdout)
    view_values = tuple(map(float, view_gestures[0][3:])) if view_gestures else ()
    view_flags = VIEW_RE.findall(view_result.stdout)
    view_start = view_values[:3]
    view_end = view_values[3:6]
    view_ok = (
        view_result.returncode == 0
        and len(view_gestures) == 1
        and view_gestures[0][:3] == ("plane", "-1", "7")
        and view_flags == ["1"]
        and abs(view_end[1] - view_start[1]) > 0.2
        and abs(view_end[1] - view_start[1])
            > abs(view_end[0] - view_start[0]) + abs(view_end[2] - view_start[2])
    )
    if not view_ok:
        print(view_result.stdout, end="")
        print(f"GFX_PICKER_CHECK: view-plane behavior mismatch {view_gestures} "
            f"flags={view_flags}", file=sys.stderr)
        return 1

    plane_environment = audit.scene_env({
        "GX_AI_INPUT": "1.5 move 751 381 200;1.75 drag 751 381 751 321 500",
        "GX_GFX_GESTURE_TRACE": "1",
        "GX_FRAMES": "240",
        "GX_FIXED_DT": "0.0166666667",
    }, clean=True)
    try:
        plane_result = audit.run_scene("--gfx", plane_environment, width=None, height=None, fps=None, timeout=60, check=False, extra_args=("--gfx-editor",))
    except subprocess.TimeoutExpired:
        print("GFX_PICKER_CHECK: XZ-plane run timeout", file=sys.stderr)
        return 1
    plane_gestures = GESTURE_RE.findall(plane_result.stdout)
    plane_values = tuple(map(float, plane_gestures[0][3:])) if plane_gestures else ()
    plane_flags = VIEW_RE.findall(plane_result.stdout)
    plane_start = plane_values[:3]
    plane_end = plane_values[3:6]
    plane_ok = (
        plane_result.returncode == 0
        and len(plane_gestures) == 1
        and plane_gestures[0][:3] == ("plane", "-1", "7")
        and plane_flags == ["0"]
        and abs(plane_end[1] - plane_start[1]) < 0.01
        and abs(plane_end[0] - plane_start[0]) + abs(plane_end[2] - plane_start[2])
            > 0.2
    )
    if not plane_ok:
        print(plane_result.stdout, end="")
        print(f"GFX_PICKER_CHECK: XZ-plane behavior mismatch {plane_gestures} "
            f"flags={plane_flags}", file=sys.stderr)
        return 1

    #opencode: Ctrl+Z reverts one whole viewport drag; Ctrl+Y restores it.
    undo_environment = audit.scene_env({
        "GX_AI_INPUT": (
            "1.5 move 751 381 200;1.75 drag 751 381 900 357 500;"
            "2.5 key 90 0 1;3.1 key 89 0 1"
        ),
        "GX_GFX_GESTURE_TRACE": "1",
        "GX_FRAMES": "300",
        "GX_FIXED_DT": "0.0166666667",
    }, clean=True)
    try:
        undo_result = audit.run_scene("--gfx", undo_environment, width=None, height=None, fps=None, timeout=60, check=False, extra_args=("--gfx-editor",))
    except subprocess.TimeoutExpired:
        print("GFX_PICKER_CHECK: undo run timeout", file=sys.stderr)
        return 1
    undo_gestures = GESTURE_RE.findall(undo_result.stdout)
    undo_values = tuple(map(float, undo_gestures[0][3:])) if undo_gestures else ()
    undo_start = undo_values[:3]
    undo_end = undo_values[3:6]
    history = [
        (match[0], tuple(map(float, match[2:5])), int(match[5]), int(match[6]))
        for match in HISTORY_RE.findall(undo_result.stdout)
    ]
    undo_ok = (
        undo_result.returncode == 0
        and len(undo_gestures) == 1
        and len(history) == 2
        and history[0][0] == "undo"
        and history[1][0] == "redo"
        and all(abs(a - b) < 0.01 for a, b in zip(history[0][1], undo_start))
        and history[0][2] == 0
        and all(abs(a - b) < 0.01 for a, b in zip(history[1][1], undo_end))
        and history[1][3] == 0
    )
    if not undo_ok:
        print(undo_result.stdout, end="")
        print(f"GFX_PICKER_CHECK: undo behavior mismatch {undo_gestures} "
            f"history={history}", file=sys.stderr)
        return 1

    #opencode: right drag pans (camera and target translate together); middle
    #   drag orbits around the fixed target.
    camera_environment = audit.scene_env({
        "GX_AI_INPUT": (
            "1.5 move 677 428 200;1.75 drag 677 428 751 428 500 RMB;"
            "2.75 move 677 428 200;3.0 drag 677 428 751 428 500 MMB"
        ),
        "GX_GFX_GESTURE_TRACE": "1",
        "GX_FRAMES": "300",
        "GX_FIXED_DT": "0.0166666667",
    }, clean=True)
    try:
        camera_result = audit.run_scene("--gfx", camera_environment, width=None, height=None, fps=None, timeout=60, check=False, extra_args=("--gfx-editor",))
    except subprocess.TimeoutExpired:
        print("GFX_PICKER_CHECK: camera run timeout", file=sys.stderr)
        return 1
    camera_gestures = [
        (match[0], tuple(map(float, match[1:13])))
        for match in CAMERA_RE.findall(camera_result.stdout)
    ]
    camera_ok = camera_result.returncode == 0 and len(camera_gestures) == 2
    if camera_ok:
        pan_mode, pan = camera_gestures[0]
        orbit_mode, orbit = camera_gestures[1]
        pan_dx = [pan[3 + i] - pan[i] for i in range(3)]
        pan_target_dx = [pan[9 + i] - pan[6 + i] for i in range(3)]
        orbit_dx = [orbit[3 + i] - orbit[i] for i in range(3)]
        orbit_target_dx = [orbit[9 + i] - orbit[6 + i] for i in range(3)]
        camera_ok = (
            pan_mode == "pan"
            and orbit_mode == "orbit"
            and all(abs(a - b) < 0.02 for a, b in zip(pan_dx, pan_target_dx))
            and sum(abs(value) for value in pan_dx) > 0.05
            and all(abs(value) < 0.001 for value in orbit_target_dx)
            and sum(abs(value) for value in orbit_dx) > 0.05
        )
    if not camera_ok:
        print(camera_result.stdout, end="")
        print(f"GFX_PICKER_CHECK: camera behavior mismatch {camera_gestures}",
            file=sys.stderr)
        return 1

    #opencode: isolate same-ray overlap so nearest-hit behavior is independent
    #   of the regular scene's transform sequence.
    overlap_environment = audit.scene_env({
        "GX_AI_INPUT": "1.5 click 662 381",
        "GX_GFX_GESTURE_TRACE": "0",
        "GX_GFX_PICKER_OVERLAP": "1",
        "GX_FRAMES": "900",
        "GX_FIXED_DT": "0.0166666667",
    }, clean=True)
    try:
        overlap_result = audit.run_scene("--gfx", overlap_environment, width=None, height=None, fps=None, timeout=60, check=False, extra_args=("--gfx-editor",))
    except subprocess.TimeoutExpired:
        print("GFX_PICKER_CHECK: overlap run timeout", file=sys.stderr)
        return 1
    overlap_picks = [tuple(map(int, match)) for match in PICK_RE.findall(overlap_result.stdout)]
    overlap_pair = OVERLAP_RE.search(overlap_result.stdout)
    overlap_ids = tuple(map(int, overlap_pair.groups())) if overlap_pair else ()
    overlap_positions_match = (
        len(overlap_ids) == 2
        and len(overlap_picks) == 1
        and abs(overlap_picks[0][0] - 662) <= POSITION_TOLERANCE
        and abs(overlap_picks[0][1] - 381) <= POSITION_TOLERANCE
        and overlap_picks[0][2] == overlap_ids[1]
    )
    if (overlap_result.returncode != 0
        or overlap_ids[0:1] != (1,)
        or len(overlap_ids) != 2
        or not overlap_positions_match
        or overlap_ids[1] == overlap_ids[0]):
        print(overlap_result.stdout, end="")
        print(
            f"GFX_PICKER_CHECK: overlap behavior mismatch pair={overlap_ids} "
            f"picks={overlap_picks}",
            file=sys.stderr,
        )
        return 1
    print(
        "GFX_PICKER_CHECK: PASS isolated=4,6 rotated=2 left_edge=5 "
        "miss=-1 drag=4 rotate=PASS snap=PASS axis_rotate=PASS axis_move=XYZ "
        "scale=PASS axis_scale=XYZ view_plane=PASS xz_plane=PASS undo=PASS "
        "camera=PASS "
        f"inspector_capture=PASS overlap={overlap_ids[1]}->{overlap_ids[0]}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
