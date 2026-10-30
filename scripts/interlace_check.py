#!/usr/bin/env python3
#opencode: deterministic opt-in interlace check. It verifies horizontal,
#   vertical, and dot cadence, phase control, strength zero, and repeatability.

import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


OUT = Path("/tmp/opencode/interlace_check")
RAW = OUT / "raw"
SEED_ARGS = ("--seed", "424242")
MODES = {"off": "0", "horizontal": "1", "vertical": "2", "dot": "3"}
STRENGTH = "0.5"


def request(mode, phase="0", strength=STRENGTH):
    return audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_T_INTERLACE_MODE": mode,
        "GX_T_INTERLACE_STRENGTH": strength,
        "GX_T_INTERLACE_PHASE": phase,
        "GX_T_INTERLACE_ANIMATE": "0",
        "GX_T_PALETTE_MODE": "0",
        "GX_T_DITHER_MODE": "0",
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


def cadence(pixels):
    luma = np.dot(pixels, np.asarray([0.299, 0.587, 0.114], dtype=np.float32))
    row_means = luma.mean(axis=1)
    column_means = luma.mean(axis=0)
    checker = (np.indices(luma.shape).sum(axis=0) % 2) == 0
    return {
        "horizontal": float(abs(row_means[::2].mean() - row_means[1::2].mean())),
        "vertical": float(abs(column_means[::2].mean() - column_means[1::2].mean())),
        "dot": float(abs(luma[checker].mean() - luma[~checker].mean())),
    }


def fixture_cadence(mode, strength=0.5):
    height, width = 192, 256
    y, x = np.mgrid[0:height, 0:width]
    reference = np.empty((height, width, 3), dtype=np.float32)
    reference[:, :, 0] = 0.35 + 0.04 * x / width
    reference[:, :, 1] = 0.45 + 0.04 * y / height
    reference[:, :, 2] = 0.55 + 0.02 * (x + y) / (width + height)
    active = np.zeros((height, width), dtype=np.float32)
    if mode == "horizontal":
        active = y % 2
    elif mode == "vertical":
        active = x % 2
    elif mode == "dot":
        active = (x + y) % 2
    output = reference * (1.0 - active[:, :, None] * 0.45 * strength)
    return cadence(output * 255.0)


def main():
    if not audit.BIN.is_file():
        raise RuntimeError("missing build binary: %s" % audit.BIN)
    OUT.mkdir(parents=True, exist_ok=True)

    requests = {
        "off": ("--showcase-minimal", request(MODES["off"]), SEED_ARGS),
        "horizontal": ("--showcase-minimal", request(MODES["horizontal"]), SEED_ARGS),
        "vertical": ("--showcase-minimal", request(MODES["vertical"]), SEED_ARGS),
        "dot": ("--showcase-minimal", request(MODES["dot"]), SEED_ARGS),
        "horizontal_repeat": ("--showcase-minimal", request(MODES["horizontal"]), SEED_ARGS),
        "horizontal_phase": ("--showcase-minimal", request(MODES["horizontal"], phase="1"), SEED_ARGS),
        "horizontal_strength0": ("--showcase-minimal", request(MODES["horizontal"], strength="0"), SEED_ARGS),
    }
    pool = audit.CapturePool(jobs=4, raw_dir=RAW)
    shots = pool.capture_all(requests, shot_frame=180)
    missing = sorted(set(requests) - set(shots))
    if missing:
        raise RuntimeError("missing interlace captures: %s" % missing)
    captures = {name: str(path) for name, path in shots.items()}
    pixels = {label: image(path) for label, path in shots.items()}
    report = {
        "schema": 1,
        "fixture": "showcase-minimal-sky",
        "resolution": [1024, 768],
        "seed": 424242,
        "strength": float(STRENGTH),
        "captures": captures,
        "cadence": {label: cadence(value) for label, value in pixels.items()},
        "fixture_cadence": {
            mode: fixture_cadence(mode) for mode in ("off", "horizontal", "vertical", "dot")
        },
        "comparisons": {
            "off_to_horizontal": difference(pixels["off"], pixels["horizontal"]),
            "off_to_vertical": difference(pixels["off"], pixels["vertical"]),
            "off_to_dot": difference(pixels["off"], pixels["dot"]),
            "horizontal_repeat": difference(pixels["horizontal"], pixels["horizontal_repeat"]),
            "horizontal_phase": difference(pixels["horizontal"], pixels["horizontal_phase"]),
            "horizontal_strength0": difference(pixels["off"], pixels["horizontal_strength0"]),
        },
        "advisory_budgets": {
            "min_changed_fraction": 0.05,
            "min_phase_mean_abs_channel": 0.10,
            "max_strength0_mean_abs_channel": 0.10,
        },
        "pass": True,
    }
    comparisons = report["comparisons"]
    budgets = report["advisory_budgets"]
    for mode in ("horizontal", "vertical", "dot"):
        if comparisons["off_to_%s" % mode]["changed_fraction"] < budgets["min_changed_fraction"]:
            report["pass"] = False
    if comparisons["horizontal_repeat"]["max_channel"] > 0.0:
        report["pass"] = False
    if comparisons["horizontal_phase"]["mean_abs_channel"] < budgets["min_phase_mean_abs_channel"]:
        report["pass"] = False
    if comparisons["horizontal_strength0"]["mean_abs_channel"] > budgets["max_strength0_mean_abs_channel"]:
        report["pass"] = False
    fixture = report["fixture_cadence"]
    if fixture["horizontal"]["horizontal"] <= fixture["horizontal"]["vertical"]:
        report["pass"] = False
    if fixture["vertical"]["vertical"] <= fixture["vertical"]["horizontal"]:
        report["pass"] = False
    if (fixture["dot"]["dot"] <= fixture["dot"]["horizontal"]
            or fixture["dot"]["dot"] <= fixture["dot"]["vertical"]):
        report["pass"] = False
    with open(OUT / "report.json", "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    print("INTERLACE-CHECK: %s" % ("PASS" if report["pass"] else "FAIL"))
    for mode in ("horizontal", "vertical", "dot"):
        metric = comparisons["off_to_%s" % mode]
        print("  %-10s mean %.3f changed %.3f%%" % (
            mode, metric["mean_abs_channel"], metric["changed_fraction"] * 100.0))
    print("  deterministic repeat max %.1f" % comparisons["horizontal_repeat"]["max_channel"])
    print("  phase toggle mean %.3f" % comparisons["horizontal_phase"]["mean_abs_channel"])
    print("  strength=0 mean %.3f" % comparisons["horizontal_strength0"]["mean_abs_channel"])
    print("  report: %s" % (OUT / "report.json"))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("INTERLACE-CHECK: FAIL — %s" % error)
        sys.exit(1)
