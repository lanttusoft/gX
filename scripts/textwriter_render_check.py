#!/usr/bin/env python3
#ai: deterministic textwriter render suite. Temporary fixtures exercise the
#   plain/tagged tags, reveal progression and choice line through the real
#   renderer; no shipped text or parser syntax changes.
"""Textwriter renderer matrix.

Cases: plain reveal at early/mid/full frames, a tagged line with color/bold/
italic/underline/size/spacing/matrix/wave/rainbow/gradient/shake/disintegrate,
a choice line selected with GX_TEXTWRITER_LINE, and a tagged repeat.

The test-only GX_TEXTWRITER_FILE / GX_TEXTWRITER_LINE hooks live in
src/renderer/textwriter.c; unset behavior keeps the shipped auto-load path and
line zero. Captures are fixed 1024x768 and are not an aesthetic sign-off.
"""

import argparse
import hashlib
import json
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


OUT = Path("/tmp/opencode/textwriter_render_check")
WIDTH = 1024
HEIGHT = 768
ROI = (40, 40, 900, 500)
MIN_INK_PIXELS = 30
MIN_INK_FRACTION = 0.0001
MIN_STATE_MEAN = 0.1
MIN_STATE_FRACTION = 0.0001

PLAIN = "Plain renderer text line for the textwriter suite."
TAGGED = (
    "<color r='255' g='180' b='80' />color <red>red</> <b>bold</> "
    "<i>ital</> <u>under</> <size v='30'>size</> <spacing v='6'>space</> "
    "<matrix a='1' c='-0.3'>matrix</> <wave amp='5' freq='0.15'>wave</> "
    "<rainbow>rain</> <gradient r='255' g='0' b='0'>grad</> "
    "<shake>shake</> <disintegrate>dis</>")
CHOICE = ("Choose a path: <choice><option line='3'>Go left</option>"
          "<option line='4'>Go right</option></choice>")
FIXTURE_LINES = (PLAIN, TAGGED, CHOICE, "left target", "right target")

CASES = {
    "plain-early": {"line": 0, "frame": 8},
    "plain-mid": {"line": 0, "frame": 70},
    "plain-full": {"line": 0, "frame": 220},
    "tagged-full": {"line": 1, "frame": 260},
    "tagged-repeat": {"line": 1, "frame": 260},
    "choice": {"line": 2, "frame": 220},
}


def select_cases(values):
    if not values:
        return list(CASES)
    names = []
    for value in values:
        for part in str(value).split(","):
            name = part.strip()
            if name and name not in names:
                names.append(name)
    unknown = sorted(set(names) - set(CASES))
    if unknown:
        raise ValueError("unknown case(s): " + ", ".join(unknown))
    if not names:
        raise ValueError("--only selected no cases")
    return names


def write_fixture(directory):
    path = Path(directory) / "textwriter_render_fixture.txt"
    path.write_text("\n".join(FIXTURE_LINES) + "\n", encoding="utf-8")
    return path


def luma(image):
    return (0.299 * image[:, :, 0] + 0.587 * image[:, :, 1]
            + 0.114 * image[:, :, 2])


def text_metrics(image):
    if not np.isfinite(image).all():
        raise ValueError("non-finite pixels")
    x0, y0, x1, y1 = ROI
    region = luma(image[y0:y1, x0:x1])
    ink = region > 8.0
    ys, xs = np.nonzero(ink)
    bbox = None
    if xs.size:
        bbox = [int(xs.min()) + x0, int(ys.min()) + y0,
                int(xs.max()) + x0 + 1, int(ys.max()) + y0 + 1]
    return {
        "finite": True,
        "width": int(image.shape[1]),
        "height": int(image.shape[0]),
        "roi": list(ROI),
        "ink_pixels": int(ink.sum()),
        "ink_fraction": float(ink.mean()),
        "mean_luma": float(region.mean()),
        "ink_bbox": bbox,
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


def reveal_is_progressive(metrics):
    ink = [row["ink_pixels"] for row in metrics]
    return (len(ink) == 3
            and all(value >= MIN_INK_PIXELS for value in ink)
            and ink[0] < ink[1] < ink[2])


def states_are_distinct(delta):
    return (delta["mean_abs_channel"] >= MIN_STATE_MEAN
            or delta["fraction_over_1"] >= MIN_STATE_FRACTION)


def capture(name, fixture, directory):
    case = CASES[name]
    out = Path(directory) / name
    out.mkdir(parents=True, exist_ok=True)
    env = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(case["frame"] + 4),
        "GX_SKIP_TESTS": "1",
        "GX_TEXTWRITER_FILE": str(fixture),
        "GX_TEXTWRITER_LINE": str(case["line"]),
        "GX_NO_AE": "1",
        "GX_T_TAA": "0",
        "GX_T_BLOOM": "0",
    }, clean=True)
    result = audit.run_scene(
        "--textwriter", env, width=WIDTH, height=HEIGHT, fps=60,
        frames=case["frame"] + 4, shot_frame=case["frame"],
        screenshot_dir=out, check=False, timeout=180)
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
        "line": case["line"],
        "frame": case["frame"],
    }


def build_report(case_names):
    failures = []
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="gx-textwriter-render-") as temp:
        fixture = write_fixture(temp)
        rows = {}
        for name in case_names:
            row = capture(name, fixture, OUT)
            row["metrics"] = text_metrics(row["image"])
            rows[name] = row
            print(f"capture {name:<14} ink {row['metrics']['ink_pixels']:5d} "
                  f"bbox {row['metrics']['ink_bbox']}", flush=True)

        repeat = rows.get("tagged-repeat")
        tagged = rows.get("tagged-full")
        if repeat and tagged:
            repeat_delta = image_delta(tagged["image"], repeat["image"])
        else:
            repeat_delta = None

        comparisons = {}
        if "plain-full" in rows and "tagged-full" in rows:
            comparisons["plain-vs-tagged"] = image_delta(
                rows["plain-full"]["image"], rows["tagged-full"]["image"])
        if "plain-full" in rows and "choice" in rows:
            comparisons["plain-vs-choice"] = image_delta(
                rows["plain-full"]["image"], rows["choice"]["image"])

        reveal = [rows[name]["metrics"] for name in
                  ("plain-early", "plain-mid", "plain-full") if name in rows]
        if len(reveal) == 3 and not reveal_is_progressive(reveal):
            failures.append("plain reveal is not progressive")

        for name, row in rows.items():
            metrics = row["metrics"]
            if not metrics["finite"]:
                failures.append(f"{name}: non-finite pixels")
            if metrics["ink_pixels"] < MIN_INK_PIXELS:
                failures.append(f"{name}: empty text ROI")
            if metrics["ink_fraction"] < MIN_INK_FRACTION:
                failures.append(f"{name}: text ROI is empty")
            if metrics["ink_bbox"] is None:
                failures.append(f"{name}: no ink bbox")
        if repeat_delta is None:
            failures.append("tagged repeat case missing")
        elif repeat_delta["max_channel"] != 0.0:
            failures.append("tagged repeat is not byte-identical")
        for pair, delta in comparisons.items():
            if not states_are_distinct(delta):
                failures.append(f"{pair}: captures are not distinct")

        report = {
            "schema": "gx-textwriter-render-check-v1",
            "width": WIDTH,
            "height": HEIGHT,
            "roi": list(ROI),
            "fixture_lines": list(FIXTURE_LINES),
            "cases": {name: {
                "line": row["line"], "frame": row["frame"],
                "sha256": row["sha256"], "metrics": row["metrics"]}
                for name, row in rows.items()},
            "repeat": {
                "sha256": repeat["sha256"] if repeat else None,
                "identical": bool(repeat and tagged
                                  and repeat["sha256"] == tagged["sha256"]),
                "delta": repeat_delta,
            },
            "comparisons": comparisons,
            "thresholds": {
                "min_ink_pixels": MIN_INK_PIXELS,
                "min_ink_fraction": MIN_INK_FRACTION,
                "min_state_mean": MIN_STATE_MEAN,
                "min_state_fraction": MIN_STATE_FRACTION,
            },
            "failures": failures,
            "notes": [
                "Fixtures are temporary; no shipped text or parser syntax is changed.",
                "Plain reveal cases use line 0 at frames 8/70/220; tagged uses line 1 at 260.",
                "The choice case selects line 2 with GX_TEXTWRITER_LINE; labels are rendered by the existing parser.",
                "This is a renderer regression matrix, not OCR or aesthetic approval.",
                "GPU captures are for main review; Xvfb/llvmpipe is not authoritative.",
            ],
        }
    return report


def render_markdown(report):
    lines = [
        "# Textwriter render suite",
        "",
        f"{report['width']}x{report['height']}; ROI {report['roi']}.",
        "",
        "| Case | Line | Frame | Ink pixels | Ink fraction | BBox |",
        "|---|---:|---:|---:|---:|---|",
    ]
    for name, row in report["cases"].items():
        metrics = row["metrics"]
        lines.append(
            f"| {name} | {row['line']} | {row['frame']} | "
            f"{metrics['ink_pixels']} | {metrics['ink_fraction']:.6f} | "
            f"{metrics['ink_bbox']} |")
    lines.extend(["", "## Comparisons", "",
                  "| Pair | Mean abs | Fraction >1 | Max |",
                  "|---|---:|---:|---:|"])
    for pair, delta in report["comparisons"].items():
        lines.append(f"| {pair} | {delta['mean_abs_channel']:.3f} | "
                     f"{delta['fraction_over_1']:.5f} | "
                     f"{delta['max_channel']:.0f} |")
    lines.extend(["", "## Repeat", ""])
    repeat = report["repeat"]
    lines.append(f"- tagged repeat identical: {repeat['identical']}")
    if repeat["delta"]:
        lines.append(f"- repeat max channel: {repeat['delta']['max_channel']:.0f}")
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
    parser.add_argument("--only", action="append", metavar="CASE",
                        help="comma-separated case names (repeatable)")
    parser.add_argument("--out", type=Path, default=OUT)
    args = parser.parse_args()
    OUT = args.out
    try:
        names = select_cases(args.only)
        report = build_report(names)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"TEXTWRITER_RENDER_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "report.json").write_text(json.dumps(report, indent=2,
                                                sort_keys=True) + "\n",
                                    encoding="utf-8")
    (OUT / "report.md").write_text(render_markdown(report), encoding="utf-8")
    print(f"TEXTWRITER_RENDER_CHECK: {len(names)} cases, "
          f"{len(report['failures'])} failure(s)")
    for failure in report["failures"]:
        print(f"  {failure}")
    print(f"  report: {OUT / 'report.json'}")
    return 0 if not report["failures"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"TEXTWRITER_RENDER_CHECK: FAIL — {error}", file=sys.stderr)
        sys.exit(1)
