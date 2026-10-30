#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: >64-bone skinning gate. The 80-joint probe must upload all 80 bone
#   matrices in both the main and depth shader paths (the historic clamp was
#   64), and the wave clip must visibly deform the ribbon between two static
#   times. Regenerate the probe with scripts/make_skin_bones_probe.py.

import json
import re
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"
PROBE = "resources/model/probe/skinprobe80.glb"
TRACE_RE = re.compile(
    r"GX_SKIN_TRACE: pass=(\w+) model=(\S+) bones=(\d+) uploaded=(\d+) cap=(\d+)")
JOINTS = 80
CAP = 128


def make_fixture(path, anim_time):
    document = json.loads(PRESET.read_text())
    entry = document["models"][0]
    entry["path"] = PROBE
    entry["params"].update({
        "pos": [0.0, 0.0, 0.0],
        "scale": [4.0, 4.0, 4.0],
        "rotation": [0.0, 0.0, 0.0],
        "usePBR": True,
        "animClip": 0,
        "animTime": anim_time,
        "animPlaying": False,
        "animLoop": True,
        "animSpeed": 1.0,
    })
    document["models"] = [entry]
    document["state"].update({"ENABLE_SHADOWS": True, "ENABLE_POINT_SHADOWS": False})
    document["lights"][0]["castShadows"] = True
    path.write_text(json.dumps(document, indent=2) + "\n")


def capture(temp_root, tag, anim_time):
    fixture = temp_root / f"{tag}.json"
    make_fixture(fixture, anim_time)
    directory = temp_root / tag
    directory.mkdir(exist_ok=True)
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "90",
        "GX_GFX_PRESET_LOAD": str(fixture),
        "GX_SKIN_TRACE": "1",
        "GX_DOCK_TRACE": "1",
    }, clean=True)
    result = audit.run_scene("--gfx", environment, width=None, height=None,
                             fps=None, shot_frame=80, screenshot_dir=directory,
                             timeout=120, check=True,
                             extra_args=("--gfx-editor",))
    shot = audit.newest_shot(directory)
    pixels = np.asarray(Image.open(shot).convert("RGB"), dtype=np.float32)
    log = audit.strip_ansi(result.stdout or "")
    x, y, w, h = audit.dock_viewport(log, pixels.shape[1], pixels.shape[0])
    return pixels[y:y + h, x:x + w], log


def main():
    if not BIN.is_file() or not (ROOT / PROBE).is_file():
        print("GFX_SKIN_BONES_CHECK: missing binary or probe "
              "(run scripts/make_skin_bones_probe.py)", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_skin_bones_",
                                         dir="/tmp/opencode") as temporary:
            temp_root = Path(temporary)
            with audit.settings_preserved():
                rest, log_a = capture(temp_root, "rest", 0.0)
                posed, log_b = capture(temp_root, "posed", 1.0)
            rows = [row for row in TRACE_RE.findall(log_a)
                    if row[1].endswith("skinprobe80.glb")]
            if not rows:
                raise RuntimeError("no GX_SKIN_TRACE rows for the 80-bone probe")
            seen = {}
            for pass_name, _model, bones, uploaded, cap in rows:
                seen[pass_name] = (int(bones), int(uploaded), int(cap))
            for pass_name in ("main", "depth"):
                if pass_name not in seen:
                    raise RuntimeError(f"missing {pass_name} pass trace: {rows}")
                bones, uploaded, cap = seen[pass_name]
                if bones != JOINTS or uploaded != JOINTS:
                    raise RuntimeError(
                        f"{pass_name}: bones={bones} uploaded={uploaded}, "
                        f"expected {JOINTS}")
                if cap != CAP:
                    raise RuntimeError(f"{pass_name}: cap={cap}, expected {CAP}")
            if rest.shape != posed.shape:
                raise RuntimeError("captures differ in size")
            delta = np.abs(rest - posed).mean()
            if float(rest.mean()) < 5.0:
                raise RuntimeError(f"rest frame is black (mean {float(rest.mean()):.1f})")
            if delta < 0.5:
                raise RuntimeError(
                    f"80-bone pose did not deform the mesh (mean delta {delta:.3f})")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_SKIN_BONES_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"GFX_SKIN_BONES_CHECK: PASS {JOINTS} bones upload in main+depth "
          f"(cap {CAP}); wave delta {delta:.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
