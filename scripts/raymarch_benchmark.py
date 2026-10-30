#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: deterministic raymarch benchmark — CPU frame time (engine CSV) plus the
#   module's own GPU timers (--raymarch-profile), across quality (render
#   scale) and feature rows. The output is advisory: `--check` only validates
#   the artifact shape, sample counts, and the resolution-scaling trend.

import argparse
import csv
import json
import os
import statistics
import subprocess
import sys
import time

import audit_common as audit

ROOT = str(audit.ROOT)
DEFAULT_BIN = os.path.join(ROOT, "build", "gX_build")
DEFAULT_OUT = "/tmp/opencode/raymarch_benchmark"
DEFAULT_WIDTH = 1024
DEFAULT_HEIGHT = 768
DEFAULT_TARGET_FPS = 60.0
DEFAULT_WARMUP = 45
DEFAULT_FRAMES = 150
DEFAULT_REPEATS = 3

#ai: row set — quality (render scale) plus one-feature-off rows. The default
#   row is the module's authored scene at the shipped renderScale (0.35).
ROWS = {
    "default": {},
    "scale-0.25": {"GX_GFX_RAYMARCH_RENDERSCALE": "0.25"},
    "scale-0.35": {"GX_GFX_RAYMARCH_RENDERSCALE": "0.35"},
    "scale-0.50": {"GX_GFX_RAYMARCH_RENDERSCALE": "0.50"},
    "scale-0.75": {"GX_GFX_RAYMARCH_RENDERSCALE": "0.75"},
    "scale-1.00": {"GX_GFX_RAYMARCH_RENDERSCALE": "1.00"},
    "terrain-off": {"GX_GFX_RAYMARCH_FEATURE": "terrain",
                    "GX_GFX_RAYMARCH_FEATURE_STATE": "0"},
    "water-off": {"GX_GFX_RAYMARCH_FEATURE": "water",
                  "GX_GFX_RAYMARCH_FEATURE_STATE": "0"},
    "objects-off": {"GX_GFX_RAYMARCH_FEATURE": "objects",
                    "GX_GFX_RAYMARCH_FEATURE_STATE": "0"},
    "fog-off": {"GX_GFX_RAYMARCH_FEATURE": "fog",
                "GX_GFX_RAYMARCH_FEATURE_STATE": "0"},
    "grid-off": {"GX_GFX_RAYMARCH_FEATURE": "grid",
                 "GX_GFX_RAYMARCH_FEATURE_STATE": "0"},
}


def read_column(path, column):
    values = []
    if not os.path.isfile(path):
        return values
    with open(path, newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            try:
                values.append(float(row[column]))
            except (KeyError, TypeError, ValueError):
                continue
    return values


def statistics_of(values):
    if not values:
        return None
    ordered = sorted(values)
    return {
        "p50": statistics.median(ordered),
        "p95": ordered[min(len(ordered) - 1, int(0.95 * len(ordered)))],
        "min": ordered[0],
        "max": ordered[-1],
        "count": len(ordered),
    }


def run_capture(args, output, row_name, row_environment, repeat):
    repeat_output = os.path.join(output, row_name, "repeat-%02d" % repeat)
    os.makedirs(repeat_output, exist_ok=True)
    cpu_path = os.path.join(repeat_output, "cpu.csv")
    gpu_path = os.path.join(repeat_output, "gpu.csv")
    log_path = os.path.join(repeat_output, "run.log")
    for path in (cpu_path, gpu_path, log_path):
        if os.path.exists(path):
            os.unlink(path)
    total_frames = args.warmup + args.frames
    environment = audit.scene_env({
        "GX_FIXED_DT": "%.12f" % (1.0 / args.target_fps),
        "GX_BENCHMARK_CPU_CSV": cpu_path,
        "GX_BENCHMARK_UNCAPPED": "1",
        "GX_BENCHMARK_NO_FLUSH": "1",
    }, clean=True)
    environment.update(row_environment)
    result = audit.run_scene(
        "--raymarch", environment, binary=os.path.abspath(args.binary),
        width=args.width, height=args.height, fps=None, frames=total_frames,
        timeout=args.timeout, check=False,
        extra_args=("--raymarch-profile=" + gpu_path,))
    with open(log_path, "w", encoding="utf-8") as log:
        log.write(audit.strip_ansi(result.stdout or ""))
    if result.returncode != 0:
        raise RuntimeError("%s repeat %d exited %d"
                           % (row_name, repeat, result.returncode))
    cpu_values = read_column(cpu_path, "cpu_ms")[-args.frames:]
    draw_values = read_column(gpu_path, "draw_ms")[-args.frames:]
    total_values = read_column(gpu_path, "total_ms")[-args.frames:]
    if len(cpu_values) < args.frames:
        raise RuntimeError("%s repeat %d CPU rows %d < requested %d"
                           % (row_name, repeat, len(cpu_values), args.frames))
    if len(total_values) < args.frames or len(draw_values) < args.frames:
        raise RuntimeError("%s repeat %d GPU rows %d/%d < requested %d"
                           % (row_name, repeat, len(draw_values),
                              len(total_values), args.frames))
    return {
        "row": row_name,
        "repeat": repeat,
        "cpu_ms": statistics_of(cpu_values),
        "draw_ms": statistics_of(draw_values),
        "total_ms": statistics_of(total_values),
    }


#ai: pool post-warmup frame samples, not repeat medians, for actual frame percentiles.
def summarize_frames(args, output, row_name):
    summary = {}
    for metric, filename, column in (("cpu_ms", "cpu.csv", "cpu_ms"),
                                      ("draw_ms", "gpu.csv", "draw_ms"),
                                      ("total_ms", "gpu.csv", "total_ms")):
        values = []
        for repeat in range(1, args.repeats + 1):
            path = os.path.join(output, row_name, "repeat-%02d" % repeat, filename)
            samples = read_column(path, column)[-args.frames:]
            if len(samples) != args.frames:
                raise RuntimeError(f"{row_name}: incomplete {metric} samples")
            values.extend(samples)
        summary[metric] = statistics_of(values)
    return summary


def run_row(args, output, row_name):
    captures = []
    for repeat in range(1, args.repeats + 1):
        captures.append(run_capture(args, output, row_name,
                                    ROWS[row_name], repeat))
        print(f"  {row_name:14s} repeat {repeat}/{args.repeats} "
              f"cpu {captures[-1]['cpu_ms']['p50']:7.3f} "
              f"gpu {captures[-1]['total_ms']['p50']:7.3f} ms", flush=True)
    summary = summarize_frames(args, output, row_name)
    return {"row": row_name, "captures": captures, "summary": summary}


def select_rows(value):
    if not value:
        return list(ROWS)
    selected = [name.strip() for name in value.split(",") if name.strip()]
    unknown = [name for name in selected if name not in ROWS]
    if unknown:
        raise ValueError("unknown row(s): " + ", ".join(unknown))
    return selected


def check_artifact(rows):
    problems = []
    for name, row in rows.items():
        summary = row["summary"]
        if not summary["total_ms"] or summary["total_ms"]["p50"] <= 0.0:
            problems.append(f"{name}: gpu p50 not positive")
        if not summary["cpu_ms"] or summary["cpu_ms"]["p50"] <= 0.0:
            problems.append(f"{name}: cpu p50 not positive")
        for metric in ("cpu_ms", "draw_ms", "total_ms"):
            expected = sum(capture[metric]["count"] for capture in row["captures"])
            if summary[metric]["count"] != expected:
                problems.append(f"{name}: {metric} frame samples not pooled")
    for name in ("scale-0.25", "scale-1.00"):
        if name not in rows:
            problems.append(f"{name}: missing for the scaling check")
    if not problems:
        low = rows["scale-0.25"]["summary"]["total_ms"]["p50"]
        high = rows["scale-1.00"]["summary"]["total_ms"]["p50"]
        if high < low * 2.0:
            problems.append(
                f"resolution scaling not visible (0.25 {low:.3f} ms vs "
                f"1.00 {high:.3f} ms)")
    return problems


def write_report(args, rows):
    artifact = {
        "width": args.width,
        "height": args.height,
        "frames": args.frames,
        "warmup": args.warmup,
        "repeats": args.repeats,
        "rows": rows,
    }
    json_path = os.path.join(args.output, "raymarch_benchmark.json")
    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(artifact, handle, indent=2)
        handle.write("\n")
    md_path = os.path.join(args.output, "raymarch_benchmark.md")
    with open(md_path, "w", encoding="utf-8") as handle:
        handle.write("# Raymarch benchmark\n\n")
        handle.write(f"{args.width}x{args.height}, {args.frames} frames, "
                     f"{args.warmup} warmup, {args.repeats} repeats\n\n")
        handle.write("| row | cpu p50 | cpu p95 | gpu draw p50 | gpu total p50 | gpu total p95 |\n")
        handle.write("| --- | --- | --- | --- | --- | --- |\n")
        for name, row in rows.items():
            summary = row["summary"]
            handle.write(
                f"| {name} | {summary['cpu_ms']['p50']:.3f} | "
                f"{summary['cpu_ms']['p95']:.3f} | "
                f"{summary['draw_ms']['p50']:.3f} | "
                f"{summary['total_ms']['p50']:.3f} | "
                f"{summary['total_ms']['p95']:.3f} |\n")
    return json_path, md_path


def main():
    parser = argparse.ArgumentParser(
        description="Measure the raymarch module's CPU/GPU cost across "
                    "quality and feature rows.")
    parser.add_argument("--binary", default=DEFAULT_BIN)
    parser.add_argument("--output", default=DEFAULT_OUT)
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
    parser.add_argument("--target-fps", type=float, default=DEFAULT_TARGET_FPS)
    parser.add_argument("--warmup", type=int, default=DEFAULT_WARMUP)
    parser.add_argument("--frames", type=int, default=DEFAULT_FRAMES)
    parser.add_argument("--repeats", type=int, default=DEFAULT_REPEATS)
    parser.add_argument("--rows", default=None,
                        help="comma-separated row names (default: all)")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--check", action="store_true",
                        help="reduced run that validates the artifact shape "
                             "and the resolution-scaling trend")
    args = parser.parse_args()
    if args.check:
        args.warmup = 30
        args.frames = 60
        args.repeats = 1
    if not os.path.isfile(args.binary):
        print("RAYMARCH_BENCHMARK: missing binary", file=sys.stderr)
        return 1
    try:
        selected = select_rows(args.rows)
        os.makedirs(args.output, exist_ok=True)
        rows = {}
        for name in selected:
            rows[name] = run_row(args, args.output, name)
        json_path, md_path = write_report(args, rows)
        if args.check:
            problems = check_artifact(rows)
            if problems:
                for line in problems:
                    print(f"RAYMARCH_BENCHMARK: {line}", file=sys.stderr)
                print(f"RAYMARCH_BENCHMARK: FAIL ({len(problems)} problems)",
                      file=sys.stderr)
                return 1
            low = rows["scale-0.25"]["summary"]["total_ms"]["p50"]
            high = rows["scale-1.00"]["summary"]["total_ms"]["p50"]
            print(f"RAYMARCH_BENCHMARK: PASS ({len(rows)} rows; scale trend "
                  f"0.25 {low:.2f} ms -> 1.00 {high:.2f} ms; report "
                  f"{md_path})")
            return 0
        print(f"RAYMARCH_BENCHMARK: wrote {json_path}")
        return 0
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) \
            as error:
        print(f"RAYMARCH_BENCHMARK: FAIL — {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
