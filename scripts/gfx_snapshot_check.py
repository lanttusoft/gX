#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: live-only GFX editor snapshot round-trip smoke. Runs through
#   audit_common (settings safety, hidden window, cleaned hook env).

import re
import subprocess
import sys

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN


def main():
    if not BIN.is_file():
        print(f"GFX_SNAPSHOT_CHECK: binary missing: {BIN}", file=sys.stderr)
        return 1
    environment = audit.scene_env({
        "GX_GFX_SNAPSHOT_SMOKE": "1",
        "GX_FRAMES": "8",
        "GX_FIXED_DT": "0.0166666667",
        "GX_SSR_NORMALS": "1",
    }, clean=True)
    try:
        result = audit.run_scene("--gfx-editor", environment, width=None,
                                 height=None, fps=None, timeout=60, check=False)
    except subprocess.TimeoutExpired:
        print("GFX_SNAPSHOT_CHECK: timeout", file=sys.stderr)
        return 1
    if result.returncode != 0:
        print(result.stdout, end="")
        print(f"GFX_SNAPSHOT_CHECK: process failed ({result.returncode})", file=sys.stderr)
        return 1
    match = re.search(
        r"GFX_SNAPSHOT_SMOKE: (PASS|FAIL) model=(\d+) camera=(\d+) renderer=(\d+)",
        result.stdout,
    )
    if not match or match.group(1) != "PASS":
        print(result.stdout, end="")
        print("GFX_SNAPSHOT_CHECK: round-trip verdict missing or failed", file=sys.stderr)
        return 1
    print(
        "GFX_SNAPSHOT_CHECK: PASS "
        f"model={match.group(2)} camera={match.group(3)} renderer={match.group(4)}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
