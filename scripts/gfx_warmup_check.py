#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: first-frame warmup pin — bursts frames 1-12 of --showcase and
#   gates the startup envelope: content appears by frame 7, exactly one
#   shape, settled drift by frame 12. Startup black frames and the single
#   early resize are expected; late pop-in or unsettled drift fails.

import os
import sys

import numpy as np

import audit_common as audit


OUT = "/tmp/opencode/warmup_check"
BURST_LEN = 12
CONTENT_FIRST = 6
CONTENT_MEAN_MIN = 10.0
SETTLE_MEAN_MAX = 2.0
SETTLE_FRAC_MAX = 0.2


def main():
    os.makedirs(OUT, exist_ok=True)
    for old in os.listdir(OUT):
        os.remove(os.path.join(OUT, old))
    with audit.settings_preserved():
        #opencode: GX_BURST writes burst_000..N frames into the screenshot dir
        #   in one run; the first after-shot frame is the burst trigger.
        environment = audit.scene_env({"GX_BURST": "1"}, clean=True)
        audit.run_scene("--showcase", environment, width=None, height=None,
                        fps=None, screenshot_dir=OUT, shot_frame=1,
                        screenshot_after=True, timeout=300, check=False)
    frames = []
    for i in range(BURST_LEN):
        path = os.path.join(OUT, f"burst_{i:03d}.png")
        if not os.path.isfile(path):
            raise RuntimeError(f"burst_{i:03d}.png missing")
        frame = audit.load_rgb(path)
        if not np.isfinite(frame).all():
            raise RuntimeError(f"burst_{i:03d}.png non-finite pixels")
        frames.append(frame)
    print("          shape        mean")
    for i, frame in enumerate(frames):
        print(f"frame {i + 1:2d}  {frame.shape[1]}x{frame.shape[0]:<5d} {frame.mean():6.2f}")
    for i in range(CONTENT_FIRST, BURST_LEN):
        if frames[i].mean() < CONTENT_MEAN_MIN:
            raise RuntimeError(f"frame {i + 1}: no content "
                               f"(mean {frames[i].mean():.2f})")
    shapes = {frames[i].shape for i in range(CONTENT_FIRST, BURST_LEN)}
    if len(shapes) != 1:
        raise RuntimeError(f"content frames change shape: {sorted(shapes)}")
    print("          mean   frac>2")
    for i in range(BURST_LEN - 3, BURST_LEN - 1):
        prev, cur = frames[i], frames[i + 1]
        if prev.shape != cur.shape:
            raise RuntimeError(f"pair {i}->{i + 1}: shape change")
        diff = np.abs(cur - prev)
        mean = audit.mean_abs_diff(cur, prev)
        frac = float((diff > 2.0).any(axis=2).mean())
        print(f"pair {i}->{i + 1}  {mean:6.3f}  {frac:.4f}")
        if mean > SETTLE_MEAN_MAX or frac > SETTLE_FRAC_MAX:
            raise RuntimeError(f"pair {i}->{i + 1}: warmup not settled")
    print("WARMUP-CHECK: PASS")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as exc:
        print(f"WARMUP-CHECK: FAIL — {exc}")
        sys.exit(1)
