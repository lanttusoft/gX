#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GFX editor onboarding gate — the F1/Help guide must open headlessly and
#   the editor must still shut down cleanly on the GFX stage. Display hidden.
import os
from pathlib import Path
import re
import subprocess
import sys

import audit_common as audit


ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "build" / "gX_build"
GUIDE_RE = re.compile(
    r"GFX_EDITOR: guide bounds=(\d+)x(\d+) at (\d+),(\d+) "
    r"stage=(\d+)x(\d+) at (\d+),(\d+)")


def main():
    if not BIN.is_file():
        print("GFX_EDITOR_GUIDE_CHECK: FAIL — missing binary", file=sys.stderr)
        return 1
    environment = dict(os.environ)
    environment.update({
        "DISPLAY": audit.display(),
        "GX_AUDIT_WINDOW": "hidden",
        "GX_NO_SETTINGS_SAVE": "1",
        "GX_SKIP_TESTS": "1",
        "GX_FRAMES": "90",
        "GX_TOOLS_OPEN": "gfx",
        "GX_GFX_GUIDE": "force",
        "GX_TOUR": "0",
    })
    try:
        result = subprocess.run([str(BIN), "--gfx"], cwd=str(ROOT),
            env=environment, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, timeout=120)
    except (OSError, subprocess.SubprocessError) as error:
        print(f"GFX_EDITOR_GUIDE_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    log = audit.strip_ansi(result.stdout or "")
    if result.returncode != 0:
        print(f"GFX_EDITOR_GUIDE_CHECK: FAIL — editor exited "
              f"{result.returncode}: {log[-500:]}", file=sys.stderr)
        return 1
    if "Tools: GFX editor opened" not in log:
        print("GFX_EDITOR_GUIDE_CHECK: FAIL — GFX editor did not open",
              file=sys.stderr)
        return 1
    if "GFX_EDITOR: guide opened" not in log:
        print("GFX_EDITOR_GUIDE_CHECK: FAIL — editor guide did not open",
              file=sys.stderr)
        return 1
    matches = GUIDE_RE.findall(log)
    if not matches:
        print("GFX_EDITOR_GUIDE_CHECK: FAIL — guide bounds trace missing",
              file=sys.stderr)
        return 1
    width, height, x, y, stage_w, stage_h, stage_x, stage_y = (
        int(value) for value in matches[-1])
    if width <= 0 or height <= 0:
        print("GFX_EDITOR_GUIDE_CHECK: FAIL — guide has no area",
              file=sys.stderr)
        return 1
    if (x < stage_x - 1 or y < stage_y - 1
            or x + width > stage_x + stage_w + 1
            or y + height > stage_y + stage_h + 1):
        print("GFX_EDITOR_GUIDE_CHECK: FAIL — guide is not inside the free "
              f"stage (guide {x},{y} {width}x{height}; "
              f"stage {stage_x},{stage_y} {stage_w}x{stage_h})",
              file=sys.stderr)
        return 1
    print("GFX_EDITOR_GUIDE_CHECK: PASS editor opens on the GFX stage, the "
          "F1/Help guide opens and fits inside the free stage")
    return 0


if __name__ == "__main__":
    sys.exit(main())
