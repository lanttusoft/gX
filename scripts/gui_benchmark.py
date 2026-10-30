#!/usr/bin/env python3
#opencode: advisory GUI benchmark runner — times a fixed template set with
#   repeated wall-clock samples and writes one JSON artifact. `--check` runs
#   the measurements and validates the artifact shape (rows, sample counts,
#   positive medians, baseline-relative costs) without host-specific
#   thresholds, mirroring the GFX benchmark's portable-artifact contract.
#
# Usage:
#   GX_AUDIT_WINDOW= DISPLAY=:1 python3 scripts/gui_benchmark.py
#   GX_AUDIT_WINDOW= DISPLAY=:1 python3 scripts/gui_benchmark.py --check
#   ... --rows baseline,settings --samples 1 --frames 90

import argparse
import json
import os
import statistics
import sys
import time

import json
import tempfile
from pathlib import Path

import audit_common as audit


#opencode: (template, effect-theme kind) — effect rows time the same template
#   with a temporary theme enabling one effect so the cost is attributable.
ROWS = {
    "baseline": (None, None),
    "titlegx": ("TITLEGX", None),
    "escapemenu": ("ESCAPEMENU", None),
    "settings": ("SETTINGS", None),
    "guitester": ("GUITESTER", None),
    "guitester-glass": ("GUITESTER", "glass"),
    "guitester-cursor": ("GUITESTER", "cursor"),
    "guitester-shadow": ("GUITESTER", "shadow"),
}
EFFECT_GLASS = {"blur": 10.0, "tint": 0.5, "rim": 0.5, "refraction": 3.0,
                "gloss": 0.32}
EFFECT_CURSOR = {"strength": 1.0, "radius": 180.0,
                 "color": [210, 240, 255, 255]}
EFFECT_SHADOW = {"offsetX": 3.0, "offsetY": 3.0, "offsetXSmall": 1.5,
                 "offsetYSmall": 1.5, "softness": 3.0,
                 "color": [0, 0, 0, 200]}
DEFAULT_OUT = "/tmp/opencode/gui_benchmark"
ARTIFACT_VERSION = 1
DEFAULT_FRAMES = 150
DEFAULT_SAMPLES = 3
DEFAULT_TIMEOUT = 180
DEFAULT_SEED = 1337


def effect_theme(kind, directory):
    if not kind:
        return None
    base = json.loads((audit.ROOT / "resources" / "style" / "theme.json")
                      .read_text(encoding="utf-8"))
    if kind == "glass":
        base["glass"] = dict(EFFECT_GLASS)
    elif kind == "cursor":
        base["cursorLight"] = dict(EFFECT_CURSOR)
    elif kind == "shadow":
        base["textShadow"] = dict(EFFECT_SHADOW)
    else:
        return None
    path = directory / f"bench-{kind}.json"
    path.write_text(json.dumps(base), encoding="utf-8")
    return path


def run_row(template, theme, kind, frames, timeout):
    environment = audit.scene_env({
        "GX_SKIP_TESTS": "1",
        "GX_FIXED_DT": "0.0166666667",
    }, clean=True)
    if template:
        environment["GX_GUI_TEMPLATE_FORCE"] = template
    if theme:
        environment["GX_GUI_THEME"] = str(theme)
    if kind == "shadow":
        environment["GX_GUI_TEXT_SHADOW"] = "1"
    started = time.perf_counter()
    audit.run_scene("--title", environment, width=None, height=None, fps=None,
                    frames=frames, timeout=timeout, check=True,
                    extra_args=("--seed", str(DEFAULT_SEED)))
    return time.perf_counter() - started


def measure(rows, frames, samples, timeout, theme_dir):
    measured = {}
    baseline = None
    for name in rows:
        template, kind = ROWS[name]
        theme = effect_theme(kind, theme_dir)
        values = [round(run_row(template, theme, kind, frames, timeout), 4)
                  for _ in range(samples)]
        median = statistics.median(values)
        if name == "baseline":
            baseline = median
        measured[name] = {
            "template": template or "baseline",
            "effect": kind or "none",
            "samples_s": values,
            "median_s": round(median, 4),
        }
        print(f"{name:<12} median {median:6.2f}s "
              f"samples {', '.join(f'{value:.2f}' for value in values)}",
              flush=True)
    for name, row in measured.items():
        if baseline and baseline > 0:
            row["cost_pct"] = round((row["median_s"] - baseline) / baseline * 100.0, 2)
        else:
            row["cost_pct"] = 0.0
    return measured


def validation_errors(report, rows, samples, frames):
    problems = []
    if report.get("version") != ARTIFACT_VERSION:
        problems.append(f"artifact version {report.get('version')} != {ARTIFACT_VERSION}")
    if report.get("frames") != frames or report.get("samples") != samples:
        problems.append("capture geometry does not match the request")
    measured = report.get("rows", {})
    for name in rows:
        row = measured.get(name)
        if not row:
            problems.append(f"row '{name}' missing")
            continue
        values = row.get("samples_s")
        if not isinstance(values, list) or len(values) != samples:
            problems.append(f"row '{name}' has {len(values or [])} samples, expected {samples}")
            continue
        if any(not isinstance(value, (int, float)) or value <= 0 for value in values):
            problems.append(f"row '{name}' has a non-positive sample")
        if row.get("median_s", 0) <= 0:
            problems.append(f"row '{name}' median is not positive")
        if not isinstance(row.get("cost_pct"), (int, float)):
            problems.append(f"row '{name}' cost_pct missing")
    if "baseline" not in measured:
        problems.append("baseline row missing")
    return problems


def main():
    parser = argparse.ArgumentParser(
        description="Advisory GUI frame-cost benchmark with a portable artifact check.")
    parser.add_argument("--output", default=DEFAULT_OUT,
                        help=f"artifact directory (default: {DEFAULT_OUT})")
    parser.add_argument("--rows", default=",".join(ROWS),
                        help="comma-separated row names")
    parser.add_argument("--frames", type=int, default=DEFAULT_FRAMES)
    parser.add_argument("--samples", type=int, default=DEFAULT_SAMPLES)
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    parser.add_argument("--check", action="store_true",
                        help="validate the artifact shape (still runs the measurements)")
    args = parser.parse_args()
    if not audit.BIN.is_file():
        print(f"GUI_BENCHMARK: FAIL — missing binary: {audit.BIN}", file=sys.stderr)
        return 1
    rows = [item.strip() for item in args.rows.split(",") if item.strip()]
    unknown = sorted(set(rows) - set(ROWS))
    if unknown:
        parser.error("unknown row(s): " + ", ".join(unknown))
    if args.frames < 10 or args.samples < 1:
        parser.error("--frames must be >= 10 and --samples >= 1")
    theme_dir = Path(tempfile.mkdtemp(prefix="gui_bench_theme_",
                                      dir="/tmp/opencode"))
    try:
        measured = measure(rows, args.frames, args.samples, args.timeout,
                           theme_dir)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GUI_BENCHMARK: FAIL — {error}", file=sys.stderr)
        return 1
    report = {
        "version": ARTIFACT_VERSION,
        "scene": "--title",
        "width": int(os.environ.get("GX_AUDIT_WIDTH", "1024")),
        "height": int(os.environ.get("GX_AUDIT_HEIGHT", "768")),
        "frames": args.frames,
        "samples": args.samples,
        "rows": measured,
    }
    os.makedirs(args.output, exist_ok=True)
    artifact = os.path.join(args.output, "report.json")
    with open(artifact, "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2, sort_keys=True)
        stream.write("\n")
    if args.check:
        problems = validation_errors(report, rows, args.samples, args.frames)
        for problem in problems:
            print(f"GUI_BENCHMARK: {problem}", file=sys.stderr)
        print(f"GUI_BENCHMARK: {'PASS' if not problems else 'FAIL'}")
        return 1 if problems else 0
    print(f"GUI_BENCHMARK: ADVISORY (artifact: {artifact})")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"GUI_BENCHMARK: FAIL — {exc}", file=sys.stderr)
        sys.exit(1)
