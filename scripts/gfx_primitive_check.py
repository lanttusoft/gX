#!/usr/bin/env python3
#opencode: blockout-primitive gate. GX_GFX_PRIMITIVE_CHECK adds one cube,
#   sphere and torus through the same helper the Add-primitive button calls;
#   the traces must report geometry, and the frame must visibly change
#   against a run without primitives.

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
HOOK_RE = re.compile(r"GFX_PRIMITIVE_CHECK: (PASS|FAIL) added=(\d+)")
ADDED_RE = re.compile(
    r"GfxEd: primitive added type=(\w+) size=([\d.]+) detail=(\d+) "
    r"index=(\d+) verts=(\d+)")
REQUEST = "cube,sphere,torus"


def capture(temp_root, tag, primitive_check=None):
    directory = temp_root / tag
    directory.mkdir(exist_ok=True)
    overrides = {
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "60",
        "GX_GFX_PRESET_LOAD": str(PRESET),
        "GX_DOCK_TRACE": "1",
    }
    if primitive_check:
        overrides["GX_GFX_PRIMITIVE_CHECK"] = primitive_check
    environment = audit.scene_env(overrides, clean=True)
    result = audit.run_scene("--gfx", environment, width=None, height=None,
                             fps=None, shot_frame=50, screenshot_dir=directory,
                             timeout=120, check=True,
                             extra_args=("--gfx-editor",))
    shot = audit.newest_shot(directory)
    pixels = np.asarray(Image.open(shot).convert("RGB"), dtype=np.float32)
    log = audit.strip_ansi(result.stdout or "")
    x, y, w, h = audit.dock_viewport(log, pixels.shape[1], pixels.shape[0])
    return pixels[y:y + h, x:x + w], log


def main():
    if not BIN.is_file() or not PRESET.is_file():
        print("GFX_PRIMITIVE_CHECK: missing binary or preset", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_primitive_",
                                         dir="/tmp/opencode") as temporary:
            temp_root = Path(temporary)
            with audit.settings_preserved():
                baseline, _ = capture(temp_root, "baseline")
                frame, log = capture(temp_root, "primitives", REQUEST)
            hook = HOOK_RE.findall(log)
            if not hook or hook[-1][0] != "PASS":
                raise RuntimeError(f"primitive hook did not pass: {hook}")
            if int(hook[-1][1]) != 3:
                raise RuntimeError(f"expected 3 primitives, got {hook[-1][1]}")
            added = ADDED_RE.findall(log)
            kinds = {row[0]: int(row[4]) for row in added}
            for kind in ("cube", "sphere", "torus"):
                if kind not in kinds:
                    raise RuntimeError(f"{kind} trace missing: {added}")
                if kinds[kind] <= 0:
                    raise RuntimeError(f"{kind} generated no vertices")
            if kinds["sphere"] <= kinds["cube"] or kinds["torus"] <= kinds["cube"]:
                raise RuntimeError(f"suspicious vertex counts: {kinds}")
            if baseline.shape != frame.shape:
                raise RuntimeError("capture size mismatch")
            delta = float(np.abs(baseline - frame).mean())
            if delta < 1.0:
                raise RuntimeError(
                    f"primitives do not change the frame (mean {delta:.3f})")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_PRIMITIVE_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"GFX_PRIMITIVE_CHECK: PASS cube/sphere/torus added "
          f"({kinds['cube']}/{kinds['sphere']}/{kinds['torus']} verts), "
          f"frame delta {delta:.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
