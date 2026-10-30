#!/usr/bin/env python3
#ai: colour-independence review for the fixed title/battle surfaces. It
#   captures default and colour-vision themes, writes protan/deutan/tritan
#   simulations for manual review, and guards the non-colour battle cues in
#   source. Simulation is diagnostic; labels/signs/shape remain authoritative.
"""Colour-independent state review.

The gate does not claim that a palette is colour-blind safe by itself. It
records the current redundant cues and provides simulation captures for the
author/reviewer, while failing when a required battle cue or theme response
disappears.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


OUT = Path("/tmp/opencode/colour_independence_check")
WIDTH = 1024
HEIGHT = 768
FRAME = 60
FRAMES = 64
MIN_MEAN = 5.0
MIN_STD = 5.0
MIN_THEME_DELTA = 0.5
# Battle uses its own scene palette; its colour independence is carried by
# labels/numbers/signs, not by theme recolouring. Keep the theme delta visible
# as a warning until the scene palette is tokenized.
THEME_REQUIRED = ("title",)

THEMES = (
    ("default", "resources/style/theme.json"),
    ("color-vision", "resources/style/theme_color_vision.json"),
)
SCENES = (
    ("title", "--title", ()),
    ("battle", "--battle", ()),
)
SIMULATIONS = {
    "protanopia": np.array([
        [0.152286, 1.052583, -0.204868],
        [0.114503, 0.786281, 0.099216],
        [-0.003882, -0.048116, 1.051998],
    ], dtype=np.float32),
    "deuteranopia": np.array([
        [0.367322, 0.860646, -0.227968],
        [0.280085, 0.672501, 0.047413],
        [-0.011820, 0.042940, 0.968881],
    ], dtype=np.float32),
    "tritanopia": np.array([
        [1.255528, -0.076749, -0.178779],
        [-0.078411, 0.930809, 0.147602],
        [0.004733, 0.691367, 0.303900],
    ], dtype=np.float32),
}
REQUIRED_BATTLE_CUES = (
    "HP", "ATB", "STATUS", "RECENT IMPACTS", "YOU", "AI",
)


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


def simulate(image, kind):
    matrix = SIMULATIONS.get(kind)
    if matrix is None:
        raise ValueError(f"unknown simulation: {kind}")
    linear = np.asarray(image, dtype=np.float32)
    simulated = linear @ matrix.T
    return np.clip(simulated, 0.0, 255.0)


def required_cues_present(source, cues=REQUIRED_BATTLE_CUES):
    return [cue for cue in cues if cue not in source]


def capture(scene, extra_args, theme_path, out):
    env = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(FRAMES),
        "GX_SKIP_TESTS": "1",
        "GX_NO_AE": "1",
        "GX_GUI_THEME": theme_path,
    }, clean=True)
    result = audit.run_scene(
        scene, env, width=WIDTH, height=HEIGHT, fps=60,
        frames=FRAMES, shot_frame=FRAME, screenshot_dir=out,
        check=False, timeout=180, extra_args=extra_args)
    shot = audit.newest_shot(out)
    if shot is None:
        raise RuntimeError(f"{scene} produced no screenshot "
                           f"(exit {result.returncode})")
    image = np.asarray(Image.open(shot).convert("RGB"), dtype=np.float32)
    if image.shape[:2] != (HEIGHT, WIDTH):
        raise RuntimeError(f"{scene} capture is {image.shape[1]}x"
                           f"{image.shape[0]}, expected {WIDTH}x{HEIGHT}")
    return image, shot


def build_report():
    OUT.mkdir(parents=True, exist_ok=True)
    captures = {}
    failures = []
    warnings = []
    for scene, flag, extra_args in SCENES:
        for theme, path in THEMES:
            key = f"{scene}-{theme}"
            out = OUT / key
            image, shot = capture(flag, extra_args, path, out)
            if not np.isfinite(image).all():
                failures.append(f"{key}: non-finite pixels")
            mean = float(luma(image).mean())
            std = float(luma(image).std())
            if mean < MIN_MEAN or std < MIN_STD:
                failures.append(f"{key}: blank/flat capture")
            sims = {}
            for name, matrix in SIMULATIONS.items():
                simulated = simulate(image, name)
                sim_path = out / f"{name}.png"
                Image.fromarray(simulated.astype(np.uint8), "RGB").save(
                    sim_path)
                sims[name] = {
                    "path": str(sim_path),
                    "sha256": hashlib.sha256(sim_path.read_bytes()).hexdigest(),
                }
            captures[key] = {
                "path": str(shot),
                "sha256": hashlib.sha256(Path(shot).read_bytes()).hexdigest(),
                "mean": mean,
                "std": std,
                "simulations": sims,
            }
            print(f"capture {key:<20} mean {mean:6.2f} std {std:6.2f}",
                  flush=True)

    theme_deltas = {}
    for scene, _, _ in SCENES:
        left = captures.get(f"{scene}-default")
        right = captures.get(f"{scene}-color-vision")
        if not left or not right:
            continue
        left_image = np.asarray(
            Image.open(left["path"]).convert("RGB"), dtype=np.float32)
        right_image = np.asarray(
            Image.open(right["path"]).convert("RGB"), dtype=np.float32)
        delta = image_delta(left_image, right_image)
        theme_deltas[scene] = delta
        if delta["mean_abs_channel"] < MIN_THEME_DELTA:
            if scene in THEME_REQUIRED:
                failures.append(
                    f"{scene}: colour-vision theme did not change pixels")
            else:
                warnings.append(
                    f"{scene}: scene palette is not theme-driven "
                    "(colour independence relies on labels/signs)")

    battle_source = (audit.ROOT / "src" / "scene" / "battle.c").read_text(
        encoding="utf-8")
    missing = required_cues_present(battle_source)
    if missing:
        failures.append("battle cue(s) missing: " + ", ".join(missing))

    report = {
        "schema": "gx-colour-independence-check-v1",
        "width": WIDTH,
        "height": HEIGHT,
        "frame": FRAME,
        "captures": captures,
        "theme_deltas": theme_deltas,
        "required_battle_cues": list(REQUIRED_BATTLE_CUES),
        "missing_battle_cues": missing,
        "theme_required": list(THEME_REQUIRED),
        "warnings": warnings,
        "simulations": list(SIMULATIONS),
        "thresholds": {
            "min_mean": MIN_MEAN,
            "min_std": MIN_STD,
            "min_theme_delta": MIN_THEME_DELTA,
        },
        "failures": failures,
        "notes": [
            "Simulation images are diagnostic, not a colour-blind safety certificate.",
            "Battle HP/MP/ATB/status and team cues retain labels, numbers, signs or bar position.",
            "The source guard only checks that the named non-colour cues still exist; it does not prove their rendered geometry.",
            "Battle uses a scene palette; its theme delta is reported as a warning, while title must respond to the shipped colour-vision theme.",
            "Captures use the shipped default and colour-vision themes at a fixed 1024x768 frame.",
        ],
    }
    return report


def render_markdown(report):
    lines = [
        "# Colour-independence review",
        "",
        f"{report['width']}x{report['height']}, frame {report['frame']}.",
        "",
        "| Capture | Mean | Std | Simulations |",
        "|---|---:|---:|---|",
    ]
    for key, row in report["captures"].items():
        lines.append(
            f"| {key} | {row['mean']:.2f} | {row['std']:.2f} | "
            f"{', '.join(report['simulations'])} |")
    lines.extend(["", "## Theme deltas", "",
                  "| Scene | Mean abs | Fraction >1 | Max |",
                  "|---|---:|---:|---:|"])
    for scene, delta in report["theme_deltas"].items():
        lines.append(f"| {scene} | {delta['mean_abs_channel']:.3f} | "
                     f"{delta['fraction_over_1']:.4f} | "
                     f"{delta['max_channel']:.0f} |")
    lines.extend(["", "## Redundant cues", "",
                  "Required battle cues: "
                  + ", ".join(f"`{cue}`" for cue in report["required_battle_cues"]),
                  "",
                  "Missing: "
                  + (", ".join(report["missing_battle_cues"]) or "none"),
                  "", "## Warnings", ""])
    lines.extend(f"- {warning}" for warning in report["warnings"])
    if not report["warnings"]:
        lines.append("None.")
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
    parser.add_argument("--out", type=Path, default=OUT)
    args = parser.parse_args()
    OUT = args.out
    try:
        report = build_report()
    except (OSError, RuntimeError, ValueError) as error:
        print(f"COLOUR_INDEPENDENCE_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    (OUT / "report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")
    (OUT / "report.md").write_text(
        render_markdown(report), encoding="utf-8")
    print(f"COLOUR_INDEPENDENCE_CHECK: {len(report['failures'])} failure(s)")
    for failure in report["failures"]:
        print(f"  {failure}")
    for warning in report["warnings"]:
        print(f"  warning: {warning}")
    print(f"  report: {OUT / 'report.json'}")
    return 0 if not report["failures"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"COLOUR_INDEPENDENCE_CHECK: FAIL — {error}", file=sys.stderr)
        sys.exit(1)
