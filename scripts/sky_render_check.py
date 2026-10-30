#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: deterministic sky renderer suite. Captures fixed 1024x768 standalone-sky
#   states and a no-sky control; reports finite/non-empty/repeat/state deltas.
#   No shader, scene or preset changes.
"""Sky renderer state matrix.

States:
  dawn  -- GX_SKY_CHECK=1 (sun near the horizon)
  noon  -- GX_SKY_CHECK=0 (sun above the horizon)
  dusk  -- GX_SKY_TIME=15000 (ephemeris epoch selected for a low-sun response)
  night -- GX_SKY_CHECK=2 (sun below the horizon)
  moon  -- GX_SKY_CHECK=3 (polar/moon conjunction)
  control-nosky -- --showcase-y2k with GX_T_SKY=0

The standalone --sky scene has no content; the control uses the existing
showcase-y2k path so no second sky renderer is added. GPU captures are for
main validation; this script does not certify aesthetic quality.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


OUT = Path("/tmp/opencode/sky_render_check")
WIDTH = 1024
HEIGHT = 768
FRAME = 8
FRAMES = 12
ROI = (40, 40, 480, 360)  # upper-left sky, clear of the sun/ground line
MIN_ROI_MEAN = 1.0
MIN_ROI_STD = 0.05
MIN_STATE_MEAN = 0.5
MIN_STATE_FRACTION = 0.001
HORIZON_TOLERANCE = 32

STATES = {
    "dawn": {
        "scene": "--sky",
        "env": {"GX_SKY_CHECK": "1", "GX_SKY_TIME": str(6 * 3600)},
        "description": "sun near the horizon",
    },
    "noon": {
        "scene": "--sky",
        "env": {"GX_SKY_CHECK": "0", "GX_SKY_TIME": str(12 * 3600)},
        "description": "sun above the horizon",
    },
    "dusk": {
        "scene": "--sky",
        "env": {"GX_SKY_TIME": "15000"},
        "description": "low-sun ephemeris epoch",
    },
    "night": {
        "scene": "--sky",
        "env": {"GX_SKY_CHECK": "2", "GX_SKY_TIME": "0"},
        "description": "sun below the horizon",
    },
    "moon": {
        "scene": "--sky",
        "env": {"GX_SKY_CHECK": "3", "GX_SKY_TIME": "0"},
        "description": "polar/moon conjunction",
    },
    "control-nosky": {
        "scene": "--showcase-y2k",
        "env": {"GX_T_SKY": "0"},
        "description": "existing showcase-y2k control with sky disabled",
    },
}
SKY_STATES = tuple(name for name in STATES if name != "control-nosky")


def select_states(values):
    if not values:
        return list(STATES)
    names = []
    for value in values:
        for part in str(value).split(","):
            name = part.strip()
            if name and name not in names:
                names.append(name)
    unknown = sorted(set(names) - set(STATES))
    if unknown:
        raise ValueError("unknown state(s): " + ", ".join(unknown))
    if not names:
        raise ValueError("--only selected no states")
    return names


def capture(name, directory):
    state = STATES[name]
    out = Path(directory) / name
    out.mkdir(parents=True, exist_ok=True)
    env = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(FRAMES),
        "GX_SKIP_TESTS": "1",
        "GX_NO_AE": "1",
        "GX_T_TAA": "0",
        "GX_T_BLOOM": "0",
        "GX_T_SSAO": "0",
        "GX_T_SSR": "0",
        "GX_T_PARTICLES": "0",
        "GX_T_FOG": "0",
        "GX_T_SNOWFALL": "0",
        "GX_T_RAIN": "0",
        "GX_T_FOGSPRITE": "0",
        **state["env"],
    }, clean=True)
    result = audit.run_scene(
        state["scene"], env, width=WIDTH, height=HEIGHT, fps=60,
        frames=FRAMES, shot_frame=FRAME, screenshot_dir=out,
        check=False, timeout=120)
    shot = audit.newest_shot(out)
    if shot is None:
        raise RuntimeError(f"{name} produced no screenshot (exit "
                           f"{result.returncode})")
    image = np.asarray(Image.open(shot).convert("RGB"), dtype=np.float32)
    if image.shape[:2] != (HEIGHT, WIDTH):
        raise RuntimeError(f"{name} capture is {image.shape[1]}x"
                           f"{image.shape[0]}, expected {WIDTH}x{HEIGHT}")
    return {
        "path": str(shot),
        "sha256": hashlib.sha256(Path(shot).read_bytes()).hexdigest(),
        "image": image,
        **state,
    }


def luma(image):
    return (0.299 * image[:, :, 0] + 0.587 * image[:, :, 1]
            + 0.114 * image[:, :, 2])


def analyze_image(image):
    if not np.isfinite(image).all():
        raise ValueError("non-finite pixels")
    x0, y0, x1, y1 = ROI
    region = image[y0:y1, x0:x1]
    region_luma = luma(region)
    full_luma = luma(image)
    row_mean = full_luma.mean(axis=1)
    gradient = np.abs(np.diff(row_mean))
    horizon = int(np.argmax(gradient)) + 1 if gradient.size else 0
    h = region_luma.shape[0]
    third = max(1, h // 3)
    bands = [
        float(region_luma[:third].mean()),
        float(region_luma[third:2 * third].mean()),
        float(region_luma[2 * third:].mean()),
    ]
    return {
        "finite": True,
        "width": int(image.shape[1]),
        "height": int(image.shape[0]),
        "roi": list(ROI),
        "roi_mean": float(region_luma.mean()),
        "roi_std": float(region_luma.std()),
        "roi_min": float(region_luma.min()),
        "roi_max": float(region_luma.max()),
        "bands": [round(value, 3) for value in bands],
        "horizon_row": horizon,
    }


def image_delta(left, right):
    if left.shape != right.shape:
        raise ValueError(f"image dimensions differ: {left.shape} vs {right.shape}")
    delta = np.abs(left - right)
    return {
        "mean_abs_channel": float(delta.mean()),
        "fraction_over_1": float((delta.mean(axis=2) > 1.0).mean()),
        "max_channel": float(delta.max()),
    }


def roi_is_nonempty(metrics):
    return (metrics["roi_mean"] > MIN_ROI_MEAN
            and metrics["roi_std"] >= MIN_ROI_STD)


def states_are_distinct(delta):
    return (delta["mean_abs_channel"] >= MIN_STATE_MEAN
            or delta["fraction_over_1"] >= MIN_STATE_FRACTION)


def repeat_is_identical(delta):
    return delta["max_channel"] == 0.0


def build_report(names):
    OUT.mkdir(parents=True, exist_ok=True)
    captures = {}
    for name in names:
        row = capture(name, OUT)
        row["metrics"] = analyze_image(row["image"])
        captures[name] = row
        print(f"capture {name:<14} {row['metrics']['roi_mean']:.2f} "
              f"std {row['metrics']['roi_std']:.2f} "
              f"horizon {row['metrics']['horizon_row']}", flush=True)

    repeats = {}
    for name in names:
        if name not in captures:
            continue
        repeat = capture(name, OUT / "repeat")
        repeats[name] = {
            "sha256": repeat["sha256"],
            "identical": repeat["sha256"] == captures[name]["sha256"],
            "delta": image_delta(captures[name]["image"], repeat["image"]),
        }

    state_deltas = {}
    present_sky = [name for name in SKY_STATES if name in captures]
    for index, left in enumerate(present_sky):
        for right in present_sky[index + 1:]:
            state_deltas[f"{left}->{right}"] = image_delta(
                captures[left]["image"], captures[right]["image"])
    for name, row in captures.items():
        row.pop("image", None)
        row["repeat"] = repeats.get(name)

    failures = []
    for name, row in captures.items():
        metrics = row["metrics"]
        if not metrics["finite"]:
            failures.append(f"{name}: non-finite pixels")
        if not roi_is_nonempty(metrics):
            failures.append(f"{name}: empty/flat sky ROI")
        if not 0 <= metrics["horizon_row"] < HEIGHT:
            failures.append(f"{name}: invalid horizon row")
    for name, repeat in repeats.items():
        if not repeat_is_identical(repeat["delta"]):
            failures.append(f"{name}: repeat is not byte-identical")
    for pair, delta in state_deltas.items():
        if not states_are_distinct(delta):
            failures.append(f"{pair}: states are not visually distinct")
    horizons = [captures[name]["metrics"]["horizon_row"]
                for name in present_sky if name in captures]
    if horizons and max(horizons) - min(horizons) > HORIZON_TOLERANCE:
        failures.append("horizon row is not stable across sky states")

    report = {
        "schema": "gx-sky-render-check-v1",
        "width": WIDTH,
        "height": HEIGHT,
        "frame": FRAME,
        "roi": list(ROI),
        "states": {name: {
            key: value for key, value in row.items() if key != "image"}
            for name, row in captures.items()},
        "state_deltas": state_deltas,
        "repeats": repeats,
        "thresholds": {
            "min_roi_mean": MIN_ROI_MEAN,
            "min_roi_std": MIN_ROI_STD,
            "min_state_mean": MIN_STATE_MEAN,
            "min_state_fraction": MIN_STATE_FRACTION,
            "horizon_tolerance": HORIZON_TOLERANCE,
        },
        "failures": failures,
        "notes": [
            "Dawn/noon/night use the existing GX_SKY_CHECK diagnostic states; dusk uses GX_SKY_TIME.",
            "The no-sky control is the existing --showcase-y2k path, not a second sky renderer.",
            "ROI is upper-left sky and avoids the sun and ground band; this is not whole-sky coverage.",
            "GPU captures are for main review; pixel thresholds are not aesthetic approval.",
        ],
    }
    return report


def render_markdown(report):
    lines = [
        "# Sky render suite",
        "",
        f"{WIDTH}x{HEIGHT}, frame {FRAME}; ROI {report['roi']}.",
        "",
        "| State | ROI mean | ROI std | Bands | Horizon | Repeat |",
        "|---|---:|---:|---|---:|---|",
    ]
    for name, row in report["states"].items():
        metrics = row["metrics"]
        repeat = row.get("repeat")
        lines.append(
            f"| {name} | {metrics['roi_mean']:.2f} | {metrics['roi_std']:.2f} "
            f"| {', '.join(f'{value:.2f}' for value in metrics['bands'])} "
            f"| {metrics['horizon_row']} | "
            f"{'PASS' if repeat and repeat['identical'] else 'FAIL'} |")
    lines.extend(["", "## State deltas", "",
                  "| Pair | Mean abs | Fraction >1 | Max |",
                  "|---|---:|---:|---:|"])
    for pair, delta in report["state_deltas"].items():
        lines.append(f"| {pair} | {delta['mean_abs_channel']:.3f} | "
                     f"{delta['fraction_over_1']:.4f} | "
                     f"{delta['max_channel']:.0f} |")
    lines.extend(["", "## Failures", ""])
    lines.extend(f"- {failure}" for failure in report["failures"])
    if not report["failures"]:
        lines.append("None.")
    lines.extend(["", "## Notes", ""])
    lines.extend(f"- {note}" for note in report["notes"])
    return "\n".join(lines).rstrip() + "\n"


def main():
    global OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", action="append", metavar="STATE",
                        help="comma-separated state names (repeatable)")
    parser.add_argument("--out", type=Path, default=OUT)
    args = parser.parse_args()
    OUT = args.out
    try:
        names = select_states(args.only)
        report = build_report(names)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"SKY_RENDER_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"SKY_RENDER_CHECK: {len(names)} states, "
          f"{len(report['failures'])} failure(s)")
    for failure in report["failures"]:
        print(f"  {failure}")
    (OUT / "report.json").write_text(json.dumps(report, indent=2,
                                                sort_keys=True) + "\n",
                                    encoding="utf-8")
    (OUT / "report.md").write_text(render_markdown(report), encoding="utf-8")
    print(f"  report: {OUT / 'report.json'}")
    return 0 if not report["failures"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"SKY_RENDER_CHECK: FAIL — {error}", file=sys.stderr)
        sys.exit(1)
