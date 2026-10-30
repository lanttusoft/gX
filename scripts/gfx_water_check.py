#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: water object-reflection pin — SSR on/off A/B over
#   --showcase-water with exposure locked. Object zones (below torus/cube)
#   must carry ~2x the delta of open water at the same depth; the sky
#   control guards global blowups and exposure leaks.

import os
import subprocess
import sys

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
OUT = "/tmp/opencode/water_check"

OBJECT_ROIS = {
    "below-torus": ((620, 800), (850, 1300)),
    "below-cube": ((400, 600), (1050, 1450)),
}
CONTROL_ROIS = {
    "open-same-depth": ((620, 800), (100, 500)),
    "sky-top": ((55, 100), (100, 1436)),
}

OBJECT_MEAN_MIN = 8.0
OBJECT_MEAN_MAX = 30.0
OBJECT_FRAC_MIN = 0.5
SKY_MEAN_MAX = 5.0


def capture(ssr):
    os.makedirs(OUT, exist_ok=True)
    before = set(os.listdir(OUT))
    env = audit.scene_env({"GX_NO_AE": "1", "GX_T_SSR": str(ssr)}, clean=True)
    env["GX_SCREENSHOT_DIR"] = OUT
    subprocess.run([BIN, "--showcase-water", "--ai-screenshot-final"],
                   cwd=ROOT, env=env, check=True, timeout=120,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    fresh = [p for p in os.listdir(OUT)
             if p.startswith("gXss_") and p not in before]
    if not fresh:
        raise RuntimeError(f"ssr={ssr}: no screenshot produced")
    path = max((os.path.join(OUT, p) for p in fresh),
               key=os.path.getmtime)
    frame = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    if not np.isfinite(frame).all():
        raise RuntimeError(f"ssr={ssr}: non-finite pixels")
    return frame


def stats(diff, rows, cols):
    region = diff[rows[0]:rows[1], cols[0]:cols[1]]
    return (float(region.mean()),
            float((region > 2.0).any(axis=2).mean()))


def main():
    on = capture(1)
    off = capture(0)
    if on.shape != off.shape:
        raise RuntimeError(f"capture size mismatch: {on.shape} {off.shape}")
    diff = np.abs(on - off)
    print("                     mean   frac>2")
    results = {}
    for name, (rows, cols) in {**OBJECT_ROIS, **CONTROL_ROIS}.items():
        results[name] = stats(diff, rows, cols)
        print(f"{name:20s} {results[name][0]:6.3f}  {results[name][1]:6.4f}")
    for name in OBJECT_ROIS:
        mean, frac = results[name]
        if not (OBJECT_MEAN_MIN < mean < OBJECT_MEAN_MAX
                and frac > OBJECT_FRAC_MIN):
            raise RuntimeError(f"{name}: object reflection outside gate")
    if results["sky-top"][0] > SKY_MEAN_MAX:
        raise RuntimeError("sky-top: global disturbance above ceiling")
    print("WATER-CHECK: PASS")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"WATER-CHECK: FAIL — {exc}")
        sys.exit(1)
