#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: Reduce Motion coverage for the opt-in analog CRT/glitch renderer. The
#   reduced path must show the clean input and stay static; the normal path
#   must remain animated. GPU captures only, no shipped asset changes.
"""Analog CRT/VHS reduced-motion capture gate.

The standalone `--analog` scene supplies a static testcard. At a fixed
timestep, normal analog processing changes between the early and late frames;
Reduce Motion bypasses the time-driven CRT/VHS/static path and shows the clean
input, so both captures must be byte-identical.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


OUT = Path("/tmp/opencode/analog_reduced_check")
WIDTH = 1024
HEIGHT = 768
EARLY_FRAME = 8
LATE_FRAME = 68
MIN_MEAN = 1.0
MIN_STD = 0.05
MIN_NORMAL_DELTA = 0.5
MIN_REDUCED_DELTA = 0.5


def luma(image):
    return (0.299 * image[:, :, 0] + 0.587 * image[:, :, 1]
            + 0.114 * image[:, :, 2])


def image_delta(left, right):
    if left.shape != right.shape:
        raise ValueError(
            f"image dimensions differ: {left.shape} vs {right.shape}")
    delta = np.abs(left - right)
    return {
        "mean_abs_channel": float(delta.mean()),
        "fraction_over_1": float((delta.mean(axis=2) > 1.0).mean()),
        "max_channel": float(delta.max()),
    }


def capture(reduced, frame, directory):
    name = "reduced" if reduced else "normal"
    out = Path(directory) / f"{name}-{frame}"
    out.mkdir(parents=True, exist_ok=True)
    env = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(frame + 4),
        "GX_SKIP_TESTS": "1",
        "GX_NO_AE": "1",
        "GX_REDUCE_MOTION": "1" if reduced else "0",
    }, clean=True)
    result = audit.run_scene(
        "--analog", env, width=WIDTH, height=HEIGHT, fps=60,
        frames=frame + 4, shot_frame=frame, screenshot_dir=out,
        check=False, timeout=120)
    shot = audit.newest_shot(out)
    if shot is None:
        raise RuntimeError(f"{name}-{frame} produced no screenshot "
                           f"(exit {result.returncode})")
    image = np.asarray(Image.open(shot).convert("RGB"), dtype=np.float32)
    if image.shape[:2] != (HEIGHT, WIDTH):
        raise RuntimeError(f"{name}-{frame} capture is {image.shape[1]}x"
                           f"{image.shape[0]}, expected {WIDTH}x{HEIGHT}")
    return {
        "path": str(shot),
        "sha256": hashlib.sha256(Path(shot).read_bytes()).hexdigest(),
        "image": image,
        "metrics": {
            "finite": bool(np.isfinite(image).all()),
            "width": int(image.shape[1]),
            "height": int(image.shape[0]),
            "mean": float(luma(image).mean()),
            "std": float(luma(image).std()),
        },
    }


def reduced_is_static(delta):
    return delta["max_channel"] == 0.0


def normal_is_animated(delta):
    return (delta["mean_abs_channel"] >= MIN_NORMAL_DELTA
            or delta["fraction_over_1"] > 0.0)


def build_report():
    OUT.mkdir(parents=True, exist_ok=True)
    captures = {}
    for reduced in (False, True):
        for frame in (EARLY_FRAME, LATE_FRAME):
            name = f"{'reduced' if reduced else 'normal'}-{frame}"
            row = capture(reduced, frame, OUT)
            captures[name] = row
            print(f"capture {name:<14} mean {row['metrics']['mean']:.2f} "
                  f"std {row['metrics']['std']:.2f} sha "
                  f"{row['sha256'][:12]}", flush=True)

    normal_delta = image_delta(
        captures[f"normal-{EARLY_FRAME}"]["image"],
        captures[f"normal-{LATE_FRAME}"]["image"])
    reduced_delta = image_delta(
        captures[f"reduced-{EARLY_FRAME}"]["image"],
        captures[f"reduced-{LATE_FRAME}"]["image"])
    state_delta = image_delta(
        captures[f"normal-{EARLY_FRAME}"]["image"],
        captures[f"reduced-{EARLY_FRAME}"]["image"])

    failures = []
    for name, row in captures.items():
        metrics = row["metrics"]
        if not metrics["finite"]:
            failures.append(f"{name}: non-finite pixels")
        if metrics["mean"] < MIN_MEAN or metrics["std"] < MIN_STD:
            failures.append(f"{name}: empty/flat capture")
    if not normal_is_animated(normal_delta):
        failures.append("normal analog frames are not animated")
    if not reduced_is_static(reduced_delta):
        failures.append("reduced analog frames are not static")
    if (state_delta["mean_abs_channel"] < MIN_REDUCED_DELTA
            and state_delta["fraction_over_1"] <= 0.0):
        failures.append("reduced and normal captures are not distinct")

    report = {
        "schema": "gx-analog-reduced-check-v1",
        "width": WIDTH,
        "height": HEIGHT,
        "frames": [EARLY_FRAME, LATE_FRAME],
        "captures": {
            name: {key: value for key, value in row.items()
                   if key != "image"}
            for name, row in captures.items()
        },
        "normal_delta": normal_delta,
        "reduced_delta": reduced_delta,
        "state_delta": state_delta,
        "thresholds": {
            "min_mean": MIN_MEAN,
            "min_std": MIN_STD,
            "min_normal_delta": MIN_NORMAL_DELTA,
            "min_reduced_delta": MIN_REDUCED_DELTA,
        },
        "failures": failures,
        "notes": [
            "Reduce Motion bypasses the time-driven analog CRT/VHS/static path and shows the clean input.",
            "Normal frames use the existing analog path; the testcard input is static.",
            "The test-only GX_REDUCE_MOTION hook drives the same settings field as the native control.",
            "GPU captures are for main review; this is not an aesthetic sign-off.",
        ],
    }
    return report


def render_markdown(report):
    lines = [
        "# Analog reduced-motion capture",
        "",
        f"{report['width']}x{report['height']}, frames "
        f"{report['frames'][0]}/{report['frames'][1]}.",
        "",
        "| Capture | Mean | Std | SHA-256 |",
        "|---|---:|---:|---|",
    ]
    for name, row in report["captures"].items():
        lines.append(
            f"| {name} | {row['metrics']['mean']:.2f} | "
            f"{row['metrics']['std']:.2f} | `{row['sha256'][:16]}` |")
    lines.extend([
        "",
        "## Deltas",
        "",
        "| Pair | Mean abs | Fraction >1 | Max |",
        "|---|---:|---:|---:|",
        f"| normal early -> late | "
        f"{report['normal_delta']['mean_abs_channel']:.3f} | "
        f"{report['normal_delta']['fraction_over_1']:.4f} | "
        f"{report['normal_delta']['max_channel']:.0f} |",
        f"| reduced early -> late | "
        f"{report['reduced_delta']['mean_abs_channel']:.3f} | "
        f"{report['reduced_delta']['fraction_over_1']:.4f} | "
        f"{report['reduced_delta']['max_channel']:.0f} |",
        f"| normal -> reduced | "
        f"{report['state_delta']['mean_abs_channel']:.3f} | "
        f"{report['state_delta']['fraction_over_1']:.4f} | "
        f"{report['state_delta']['max_channel']:.0f} |",
        "",
        "## Failures",
        "",
    ])
    lines.extend(f"- {failure}" for failure in report["failures"])
    if not report["failures"]:
        lines.append("None.")
    lines.extend(["", "## Notes", ""])
    lines.extend(f"- {note}" for note in report["notes"])
    return "\n".join(lines).rstrip() + "\n"


def main():
    global OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=OUT)
    args = parser.parse_args()
    OUT = args.out
    try:
        report = build_report()
    except (OSError, RuntimeError, ValueError) as error:
        print(f"ANALOG_REDUCED_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    (OUT / "report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")
    (OUT / "report.md").write_text(
        render_markdown(report), encoding="utf-8")
    print(f"ANALOG_REDUCED_CHECK: {len(report['failures'])} failure(s)")
    for failure in report["failures"]:
        print(f"  {failure}")
    print(f"  report: {OUT / 'report.json'}")
    return 0 if not report["failures"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"ANALOG_REDUCED_CHECK: FAIL — {error}", file=sys.stderr)
        sys.exit(1)
