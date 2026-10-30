#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: deterministic opt-in dither mode check. It captures the same
#   minimal sky fixture with off/IGN/Bayer/seeded-noise modes and verifies that
#   the modes are stable and observable without changing the default path.
#   Captures run in parallel through audit_common (hidden window, serial retry).

import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


OUT = Path("/tmp/opencode/dither_check")
MODES = {"off": "0", "ign": "1", "bayer": "2", "noise": "3"}
SEED = "17"
SEED_ARGS = ("--seed", "424242")


def request(mode):
    return audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_T_DITHER_MODE": mode,
        "GX_T_DITHER_SEED": SEED,
        "GX_T_DITHER_ANIMATE": "0",
        "GX_T_TAA": "0",
        "GX_T_FXAA": "0",
    }, clean=True)


def image(path):
    pixels = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    if not np.isfinite(pixels).all():
        raise RuntimeError("non-finite pixels in %s" % path)
    return pixels


def difference(first, second):
    delta = np.abs(first - second)
    return {
        "mean_abs_channel": float(delta.mean()),
        "changed_fraction": float((delta.max(axis=2) > 0.5).mean()),
        "max_channel": float(delta.max()),
    }


def plateau_fraction(pixels):
    luma = np.dot(pixels, np.asarray([0.299, 0.587, 0.114], dtype=np.float32))
    horizontal = np.abs(luma[:, 1:] - luma[:, :-1]) < 0.5
    vertical = np.abs(luma[1:, :] - luma[:-1, :]) < 0.5
    return {
        "horizontal": float(horizontal.mean()),
        "vertical": float(vertical.mean()),
    }


def main():
    if not audit.BIN.is_file():
        raise RuntimeError("missing build binary: %s" % audit.BIN)
    requests = {
        label: ("--showcase-minimal", request(mode), SEED_ARGS)
        for label, mode in MODES.items()
    }
    requests["bayer_repeat"] = ("--showcase-minimal", request(MODES["bayer"]),
                                SEED_ARGS)
    pool = audit.CapturePool(jobs=4, raw_dir=OUT / "raw")
    shots = pool.capture_all(requests, shot_frame=180)
    missing = sorted(set(requests) - set(shots))
    if missing:
        raise RuntimeError("missing dither captures: %s" % missing)

    pixels = {label: image(path) for label, path in shots.items()}
    report = {
        "schema": 1,
        "fixture": "showcase-minimal-sky",
        "resolution": [audit.MATRIX_WIDTH, audit.MATRIX_HEIGHT],
        "seed": 424242,
        "dither_seed": int(SEED),
        "modes": {},
        "comparisons": {},
    }
    for label in MODES:
        report["modes"][label] = {
            "capture": str(shots[label]),
            "plateaus": plateau_fraction(pixels[label]),
        }
        if label != "off":
            report["comparisons"]["off_to_%s" % label] = difference(
                pixels["off"], pixels[label])
    report["comparisons"]["bayer_repeat"] = difference(
        pixels["bayer"], pixels["bayer_repeat"])
    report["pass"] = True
    if report["comparisons"]["bayer_repeat"]["max_channel"] > 0.0:
        report["pass"] = False
    for label in ("ign", "bayer", "noise"):
        if report["comparisons"]["off_to_%s" % label]["mean_abs_channel"] <= 0.01:
            report["pass"] = False
    report_path = OUT / "report.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    print("DITHER-CHECK: %s" % ("PASS" if report["pass"] else "FAIL"))
    for label in ("ign", "bayer", "noise"):
        metric = report["comparisons"]["off_to_%s" % label]
        print("  %-5s mean %.4f changed %.3f%%" % (
            label, metric["mean_abs_channel"], metric["changed_fraction"] * 100.0))
    print("  deterministic Bayer repeat max %.1f" %
          report["comparisons"]["bayer_repeat"]["max_channel"])
    print("  report: %s" % report_path)
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("DITHER-CHECK: FAIL — %s" % error)
        sys.exit(1)
