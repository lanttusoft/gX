#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: 2026-10-22 layout-dump CLI contract; unblocks UI diff/layout report tools.
"""Run the title scene and verify the structured layout dump.

The dump is a prerequisite for the deferred gameX tools (ui_diff,
layout_report, layout_sweep). This check pins the CLI option, the one-shot
frame, the JSON shape and a non-empty title element set.
"""

import json
import sys
from pathlib import Path

import audit_common as audit

ROOT = audit.ROOT
BIN = audit.BIN
DUMP_FRAME = 8


def main():
    if not BIN.is_file():
        print(f"LAYOUT_DUMP_CHECK: missing {BIN}; run make first", file=sys.stderr)
        return 1

    out = ROOT / ".ai" / "log" / "layout-dump-check.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()

    environment = audit.scene_env({"GX_FRAMES": "12"}, clean=True)
    result = audit.run_scene(
        "--title",
        environment,
        width=1024,
        height=768,
        fps=None,
        frames=12,
        extra_args=("--layout-dump", str(out),
                    "--layout-dump-frame", str(DUMP_FRAME)),
        check=False)
    if result.returncode != 0:
        print(f"LAYOUT_DUMP_CHECK: scene exited {result.returncode}", file=sys.stderr)
        return 1
    if not out.is_file():
        print("LAYOUT_DUMP_CHECK: no dump file", file=sys.stderr)
        return 1

    try:
        data = json.loads(out.read_text())
    except (OSError, ValueError) as error:
        print(f"LAYOUT_DUMP_CHECK: invalid JSON: {error}", file=sys.stderr)
        return 1

    if data.get("screenW") != 1024 or data.get("screenH") != 768:
        print("LAYOUT_DUMP_CHECK: wrong screen dimensions", file=sys.stderr)
        return 1
    canvases = data.get("canvases")
    if not isinstance(canvases, list) or not canvases:
        print("LAYOUT_DUMP_CHECK: no canvases", file=sys.stderr)
        return 1
    elements = sum(len(canvas.get("elements", [])) for canvas in canvases)
    if elements < 1:
        print("LAYOUT_DUMP_CHECK: no elements", file=sys.stderr)
        return 1

    print(f"LAYOUT_DUMP_CHECK: PASS — {len(canvases)} canvas, {elements} elements "
          f"at frame {DUMP_FRAME}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
