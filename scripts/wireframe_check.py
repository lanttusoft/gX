#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: wireframe placement pin — the transpose=0 defect rendered
#   (PV)^T garbage that PASSED the difference-style matrix row (garbage
#   differs from baseline just as well as a correct render). Only a
#   placement gate catches this class: the button.glb disc must render
#   contained (no stray lines to frame edges) with nontrivial coverage.
#
#   Usage:
#     DISPLAY=:1 python3 scripts/wireframe_check.py
#
#   Exit 0 iff the white-pixel bbox is contained and coverage is sane.
#   GPU/display required — use `make wireframe-check`.

import sys
from pathlib import Path

import numpy as np

import audit_common as audit


OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/tmp/opencode/wireframe")


def main():
    if not audit.BIN.is_file():
        print("wireframe | missing binary | FAIL")
        return 1
    raw = OUT / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    pool = audit.CapturePool(jobs=1, raw_dir=raw)
    shots = pool.capture_all(
        {"wireframe": ("--wireframe", audit.scene_env({}, clean=True))},
        shot_frame=120)
    if "wireframe" not in shots:
        print("wireframe | no shot | FAIL")
        return 1
    a = audit.load_rgb(shots["wireframe"])
    height, width = a.shape[:2]
    white = a.max(axis=2) > 200
    frac = float(white.mean())
    ys, xs = np.nonzero(white)
    if len(xs) == 0:
        print("wireframe | empty frame | FAIL")
        return 1
    #opencode: contained placement — the defect sprayed lines to every
    #   frame edge (x-span full width, y-span full height). The healthy
    #   disc sits center-frame.
    x0, x1 = float(xs.min()) / width, float(xs.max()) / width
    y0, y1 = float(ys.min()) / height, float(ys.max()) / height
    ok = (0.005 <= frac <= 0.20
          and x0 > 0.05 and x1 < 0.95
          and y0 > 0.05 and y1 < 0.95)
    print(f"wireframe | frac {frac:.4f} bbox x[{x0:.2f},{x1:.2f}] "
          f"y[{y0:.2f},{y1:.2f}] | {'ok disc placed' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
