#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: SSR resolution gate — full is the default; half/quarter are explicit
#   quality options. The visual budget is pinned on the deterministic SSR
#   fixture, and full-resolution output must be byte-identical across runs.
"""Gate the SSR march-resolution option.

Captures the SSR fixture at full, half and quarter resolution on the same
binary, frame and camera. Fails when:

- full-resolution captures are not byte-identical (determinism),
- half/quarter leave the agreed PSNR/mean/changed-pixel budget,
- a reduced capture is identical to full (the option did not engage).
"""

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit
import gfx_benchmark as benchmark


DEFAULT_OUT = audit.SCRATCH / "ssr_resolution_check"
FRAME = 180
WIDTH = 1024
HEIGHT = 768
HALF_BUDGET = {"psnr": 30.0, "mean": 0.60, "gt64": 0.005}
QUARTER_BUDGET = {"psnr": 27.0, "mean": 1.10, "gt64": 0.012}


def capture(out, divisor):
    directory = out / ("res-%d" % divisor)
    if directory.exists():
        for old in directory.glob("*.png"):
            old.unlink()
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        **benchmark.FEATURE_ROWS["ssr"],
        "GX_FIXED_DT": "%.12f" % (1.0 / 60.0),
        "GX_SHOT_FRAME": str(FRAME),
        "GX_T_SSR_RES": str(divisor),
    }, clean=True)
    benchmark.clean_feature_environment(environment)
    environment.update(benchmark.FEATURE_ROWS["ssr"])
    environment["GX_T_SSR_RES"] = str(divisor)
    audit.run_scene(
        "--showcase-minimal", environment, width=WIDTH, height=HEIGHT,
        fps=None, frames=FRAME + 120, timeout=180, check=True,
        screenshot_dir=directory, shot_frame=FRAME)
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError("resolution %d produced no screenshot" % divisor)
    with Image.open(shot) as image:
        return np.asarray(image.convert("RGB"), dtype=np.int16)


def compare(reference, variant):
    delta = np.abs(reference - variant)
    mse = float((delta.astype(np.float64) ** 2).mean())
    changed = (delta.max(axis=2) > 64).mean()
    return {
        "psnr": float("inf") if mse == 0 else 10.0 * math.log10((255.0 ** 2) / mse),
        "mean": float(delta.mean()),
        "max": int(delta.max()),
        "gt64": float(changed),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    full_a = capture(out, 1)
    full_b = capture(out, 1)
    half = capture(out, 2)
    quarter = capture(out, 4)

    metrics = {
        "frame": FRAME,
        "size": [WIDTH, HEIGHT],
        "full_a_vs_full_b": compare(full_a, full_b),
        "half": compare(full_a, half),
        "quarter": compare(full_a, quarter),
        "full_mean": float(full_a.mean()),
    }
    failures = []
    if metrics["full_a_vs_full_b"]["max"] != 0:
        failures.append("full resolution is not deterministic")
    if metrics["full_mean"] < 10.0:
        failures.append("full resolution frame is blank")
    for name, budget in (("half", HALF_BUDGET), ("quarter", QUARTER_BUDGET)):
        item = metrics[name]
        if item["psnr"] < budget["psnr"]:
            failures.append("%s PSNR %.2f < %.2f" % (
                name, item["psnr"], budget["psnr"]))
        if item["mean"] > budget["mean"]:
            failures.append("%s mean %.3f > %.3f" % (
                name, item["mean"], budget["mean"]))
        if item["gt64"] > budget["gt64"]:
            failures.append("%s >64 fraction %.4f > %.4f" % (
                name, item["gt64"], budget["gt64"]))
        if item["max"] == 0:
            failures.append("%s did not change the image" % name)

    (out / "report.json").write_text(
        json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("SSR_RESOLUTION: frame=%d %dx%d" % (FRAME, WIDTH, HEIGHT))
    for name in ("half", "quarter"):
        item = metrics[name]
        print("  %-7s PSNR %6.2f dB  mean %.3f  max %3d  >64 %.4f" % (
            name, item["psnr"], item["mean"], item["max"], item["gt64"]))
    if failures:
        for failure in failures:
            print("  FAIL " + failure)
        print("SSR_RESOLUTION: FAIL")
        return 1
    print("SSR_RESOLUTION: PASS (full deterministic; half/quarter within budget)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print("SSR_RESOLUTION: FAIL — %s" % error)
        sys.exit(1)
