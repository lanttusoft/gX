#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: HUD-over-scene contrast probe. Captures fixed scenes, samples documented
#   text ROIs, and reports WCAG text contrast with alpha-aware math. Findings
#   are report-only unless a baseline/strict option is supplied; no OCR or AI.
"""Measure HUD text contrast over live gX scenes.

Scenes: --title and --battle. At 1024x768,
frame 24, each ROI is a tight, documented text band. Rendered pixels are final
composited scene pixels; the contrast helpers also expose alpha compositing
for token/fallback callers. ROIs are explicit and cover only the named text —
this is not whole-HUD or OCR coverage.

Exit 0 for a report (even with findings), 1 for tool/scene/ROI failure or new
baseline drift, and 0 with known-baseline findings. --strict fails on every
below-4.5:1 finding.
"""

import argparse
import json
import sys
import tempfile
from pathlib import Path

import numpy as np

import audit_common as audit


ROOT = Path(__file__).resolve().parents[1]
WIDTH = 1024
HEIGHT = 768
FRAME = 24
FRAMES = 30
TEXT_MIN = 4.5
DEFAULT_JSON = "/tmp/opencode/gui_hud_contrast.json"
DEFAULT_BASELINE = ROOT / "scripts" / "gui_hud_contrast_baseline.json"

SCENES = {
    "title": {
        "flag": "--title",
        "env": {},
        "extra": (),
    },
    "battle": {
        "flag": "--battle",
        "env": {},
        "extra": (),
    },
}

#ai: fixed text bands at 1024x768; keep these explicit so the report never
#   claims OCR/whole-HUD coverage.
ROIS = {
    "title": [
        ("title.wordmark", (390, 175, 635, 272)),
        ("title.menu", (294, 360, 730, 400)),
        ("title.credits", (813, 704, 925, 731)),
        ("title.hint", (172, 704, 750, 737)),
    ],
    "battle": [
        ("battle.header", (56, 20, 360, 50)),
        ("battle.party", (54, 544, 320, 572)),
        ("battle.command", (360, 544, 560, 572)),
        ("battle.enemy", (704, 544, 960, 575)),
        ("battle.turn", (380, 470, 645, 500)),
        ("battle.message", (300, 710, 740, 740)),
    ],
}


def relative_luminance(rgb):
    values = np.asarray(rgb, dtype=np.float64) / 255.0
    linear = np.where(values <= 0.04045, values / 12.92,
                       ((values + 0.055) / 1.055) ** 2.4)
    return (0.2126 * linear[..., 0] + 0.7152 * linear[..., 1]
            + 0.0722 * linear[..., 2])


def composite_rgba(fg, bg):
    """Alpha-composite fg over bg; both are RGBA 0..255."""
    fg = np.asarray(fg, dtype=np.float64)
    bg = np.asarray(bg, dtype=np.float64)
    if fg.shape[-1] == 3:
        fg = np.concatenate((fg, [255.0]))
    if bg.shape[-1] == 3:
        bg = np.concatenate((bg, [255.0]))
    fa = fg[3] / 255.0
    ba = bg[3] / 255.0
    out_a = fa + ba * (1.0 - fa)
    if out_a <= 0.0:
        return np.zeros(4, dtype=np.float64)
    rgb = (fg[:3] * fa + bg[:3] * ba * (1.0 - fa)) / out_a
    return np.concatenate((rgb, [out_a * 255.0]))


def contrast_ratio(fg, bg, canvas=(0, 0, 0, 255)):
    """WCAG contrast with optional alpha compositing."""
    fg = np.asarray(fg, dtype=np.float64)
    bg = np.asarray(bg, dtype=np.float64)
    if bg.shape[-1] == 4 and bg[-1] < 255:
        bg = composite_rgba(bg, canvas)
    if fg.shape[-1] == 4 and fg[-1] < 255:
        fg = composite_rgba(fg, bg)
    first = relative_luminance(fg[:3])
    second = relative_luminance(bg[:3])
    if first < second:
        first, second = second, first
    return float((first + 0.05) / (second + 0.05))


def roi_contrast(image, roi):
    x0, y0, x1, y1 = roi
    crop = np.asarray(image[y0:y1, x0:x1], dtype=np.float64)
    if crop.size == 0:
        raise ValueError(f"empty ROI {roi}")
    luminance = relative_luminance(crop)
    dark_mask = luminance <= np.percentile(luminance, 2)
    light_mask = luminance >= np.percentile(luminance, 98)
    if not dark_mask.any() or not light_mask.any():
        raise ValueError(f"ROI {roi} has no luminance range")
    dark = crop[dark_mask].mean(axis=0)
    light = crop[light_mask].mean(axis=0)
    return {
        "ratio": contrast_ratio(light, dark),
        "fg": [round(float(value), 1) for value in light] + [255.0],
        "bg": [round(float(value), 1) for value in dark] + [255.0],
        "dark_pixels": int(dark_mask.sum()),
        "light_pixels": int(light_mask.sum()),
        "pixels": int(luminance.size),
    }


def select_scenes(values):
    if not values:
        return list(SCENES)
    names = []
    for value in values:
        for part in str(value).split(","):
            name = part.strip().lstrip("-")
            if name and name not in names:
                names.append(name)
    unknown = sorted(set(names) - set(SCENES))
    if unknown:
        raise ValueError("unknown scene(s): " + ", ".join(unknown))
    if not names:
        raise ValueError("--only selected no scenes")
    return names


def capture_scene(scene, directory):
    config = SCENES[scene]
    environment = audit.scene_env({
        "GX_SKIP_TESTS": "1",
        "GX_FIXED_DT": "0.0166666667",
        **config["env"],
    }, clean=True)
    result = audit.run_scene(
        config["flag"], environment,
        width=WIDTH, height=HEIGHT, fps=60,
        frames=FRAMES, shot_frame=FRAME, screenshot_dir=directory,
        extra_args=config["extra"], check=False, timeout=120)
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError(f"{scene} produced no screenshot (exit "
                           f"{result.returncode})")
    image = audit.load_rgb(shot).astype(np.float64)
    if image.shape[:2] != (HEIGHT, WIDTH):
        raise RuntimeError(f"{scene} capture is {image.shape[1]}x"
                           f"{image.shape[0]}, expected {WIDTH}x{HEIGHT}")
    return image


def evaluate_scene(image, scene):
    rows = []
    for label, roi in ROIS[scene]:
        values = roi_contrast(image, roi)
        rows.append({
            "label": label,
            "scene": scene,
            "rect": list(roi),
            "ratio": round(values["ratio"], 3),
            "minimum": TEXT_MIN,
            "pass": values["ratio"] + 1e-6 >= TEXT_MIN,
            "fg": values["fg"],
            "bg": values["bg"],
            "dark_pixels": values["dark_pixels"],
            "light_pixels": values["light_pixels"],
            "pixels": values["pixels"],
        })
    return rows


def build_report(scenes):
    report = {
        "width": WIDTH,
        "height": HEIGHT,
        "frame": FRAME,
        "minimum": TEXT_MIN,
        "scenes": {},
        "notes": [
            "ROIs are fixed text bands at 1024x768, not whole-HUD/OCR coverage.",
            "Contrast is sampled from final rendered pixels (engine compositing already applied).",
            "Percentiles 2/98 pick the dark/light text-vs-background clusters in each ROI.",
            "Known failures are baseline evidence, not approval of those ratios.",
        ],
    }
    with tempfile.TemporaryDirectory(prefix="gx-hud-contrast-") as directory:
        for scene in scenes:
            scene_dir = Path(directory) / scene
            scene_dir.mkdir(parents=True, exist_ok=True)
            image = capture_scene(scene, scene_dir)
            report["scenes"][scene] = {
                "capture": SCENES[scene]["flag"],
                "rois": evaluate_scene(image, scene),
            }
    return report


def classify(report, baseline):
    known = set()
    if baseline:
        for values in baseline.get("known", {}).values():
            known.update(values)
    failures = []
    measured = set()
    for scene in report["scenes"].values():
        for row in scene["rois"]:
            measured.add(row["label"])
            if not row["pass"]:
                failures.append(row)
    new = [row for row in failures if row["label"] not in known]
    stale = sorted(known - measured)
    return failures, new, stale


def load_baseline(path):
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValueError(f"cannot read baseline {path}: {error}") from error
    if not isinstance(data, dict) or not isinstance(data.get("known"), dict):
        raise ValueError(f"{path}: expected {{'known': {{scene: [roi...]}}}}")
    return data


def render_markdown(report, failures, new):
    lines = [
        "# HUD-over-scene contrast",
        "",
        f"{len(report['scenes'])} scenes, {WIDTH}x{HEIGHT}, frame "
        f"{report['frame']}; text minimum {TEXT_MIN}:1.",
        "",
        "| Scene | ROI | Ratio | Minimum | Status | Rect |",
        "|---|---|---:|---:|---|---|",
    ]
    for scene, data in report["scenes"].items():
        for row in data["rois"]:
            status = "PASS" if row["pass"] else (
                "NEW FAIL" if row in new else "known fail")
            rect = row["rect"]
            lines.append(
                f"| {scene} | {row['label']} | {row['ratio']:.2f}:1 | "
                f"{row['minimum']}:1 | {status} | "
                f"{rect[0]},{rect[1]},{rect[2]},{rect[3]} |")
    lines.extend(["", "## Notes", ""])
    lines.extend(f"- {note}" for note in report["notes"])
    return "\n".join(lines).rstrip() + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", action="append", metavar="SCENE",
                        help="comma-separated scene names (repeatable)")
    parser.add_argument("--out", type=Path, help="write the Markdown report")
    parser.add_argument("--json", nargs="?", const=DEFAULT_JSON,
                        help=f"write JSON (default: {DEFAULT_JSON})")
    parser.add_argument("--baseline", type=Path,
                        help=f"known failures JSON (default candidate: "
                             f"{DEFAULT_BASELINE})")
    parser.add_argument("--strict", action="store_true",
                        help="fail on every below-minimum ROI")
    args = parser.parse_args()
    try:
        scenes = select_scenes(args.only)
        baseline = load_baseline(args.baseline) if args.baseline else None
        report = build_report(scenes)
    except (RuntimeError, ValueError, OSError) as error:
        print(f"GUI_HUD_CONTRAST: FAIL — {error}", file=sys.stderr)
        return 1

    failures, new, stale = classify(report, baseline)
    summary = {
        "scenes": len(report["scenes"]),
        "rois": sum(len(data["rois"]) for data in report["scenes"].values()),
        "failures": len(failures),
        "known": len(failures) - len(new),
        "new": len(new),
        "stale": stale,
    }
    report["summary"] = summary
    report["baseline"] = str(args.baseline) if args.baseline else None
    markdown = render_markdown(report, failures, new)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(markdown, encoding="utf-8")
    else:
        sys.stdout.write(markdown)
    if args.json:
        path = Path(args.json)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(report, indent=2, sort_keys=True),
                        encoding="utf-8")
    print(f"GUI_HUD_CONTRAST: {summary['scenes']} scenes, "
          f"{summary['rois']} ROIs, {summary['failures']} failure(s) "
          f"({summary['known']} known, {summary['new']} new)")
    for row in failures:
        print(f"  {'NEW ' if row in new else 'known '}"
              f"{row['label']:<18} {row['ratio']:.2f}:1 "
              f"< {row['minimum']}:1")
    for label in stale:
        print(f"  note baseline ROI not measured: {label}")
    if args.baseline and new:
        return 1
    if args.strict and failures:
        print("GUI_HUD_CONTRAST: STRICT FAIL")
        return 1
    if args.baseline:
        print("GUI_HUD_CONTRAST: PASS (baseline)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
