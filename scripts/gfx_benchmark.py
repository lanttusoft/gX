#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: deterministic GFX benchmark runner. It records repeated CPU/GPU
#   captures for the production scene and isolated opt-in feature rows.

import argparse
import csv
import json
#ai: reject non-finite native timing samples rather than silently certifying them.
import math
import os
import shlex
import subprocess
import time

import audit_common as audit


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_BIN = os.path.join(ROOT, "build", "gX_build")
DEFAULT_OUT = "/tmp/opencode/gfx_benchmark"
DEFAULT_WIDTH = 1024
DEFAULT_HEIGHT = 768
DEFAULT_TARGET_FPS = 60.0
DEFAULT_SEED = 424242
DEFAULT_WARMUP = 60
DEFAULT_FRAMES = 180
DEFAULT_REPEATS = 3
#ai: unmeasured tail frames let nonblocking GPU queries complete without flush/finish.
GPU_DRAIN_FRAMES = 16
DEFAULT_ROWS = "default,isolated,ssr,ssao,bloom,taa"

#opencode: the isolated control mirrors the feature-matrix contract so a row
#   measures one opt-in path rather than inheriting another effect's cost.
ISOLATED_OFF = {
    "GX_T_BLOOM": "0", "GX_T_SSAO": "0", "GX_T_SHADOWS": "0",
    "GX_T_POINT_SHADOWS": "0", "GX_T_SSR": "0", "GX_T_TAA": "0",
    "GX_T_FXAA": "0", "GX_T_GODRAY": "0", "GX_T_COOKIES": "0",
    "GX_T_PARTICLES": "0", "GX_T_LENSFLARE": "0", "GX_T_DOF": "0",
    "GX_T_SNOW": "0", "GX_T_SNOWFALL": "0", "GX_T_RAIN": "0", "GX_T_FOGSPRITE": "0",
    "GX_T_SPLAT": "0", "GX_MIRROR_PAIR": "0", "GX_PROBE_GRID": "0",
    "GX_T_MOTIONBLUR": "0", "GX_T_ENVAMBIENT": "0", "GX_T_DECALS": "0",
    "GX_T_WIREFRAME": "0", "GX_T_SSRAUX": "0", "GX_T_INSTANCING": "0",
    "GX_T_FOG": "1", "GX_FOG_DENSITY": "0",
    "GX_CAM_YAW": "30", "GX_CAM_PITCH": "25", "GX_CAM_DIST": "10",
}

FEATURE_ROWS = {
    "default": {},
    "isolated": ISOLATED_OFF,
    "ssr": {**ISOLATED_OFF, "GX_T_SSR": "1"},
    "ssao": {**ISOLATED_OFF, "GX_T_SSAO": "1"},
    "bloom": {**ISOLATED_OFF, "GX_T_BLOOM": "1", "GX_T_BLOOMINT": "6"},
    "taa": {**ISOLATED_OFF, "GX_T_TAA": "1"},
    "motionblur": {**ISOLATED_OFF, "GX_T_MOTIONBLUR": "1", "GX_CAM_SPIN": "3"},
    "palette16": {**ISOLATED_OFF, "GX_T_PALETTE_MODE": "1", "GX_T_DITHER_MODE": "0"},
    "palette256": {**ISOLATED_OFF, "GX_T_PALETTE_MODE": "2", "GX_T_DITHER_MODE": "0"},
    "interlace_h": {**ISOLATED_OFF, "GX_T_INTERLACE_MODE": "1"},
    "interlace_v": {**ISOLATED_OFF, "GX_T_INTERLACE_MODE": "2"},
    "interlace_dot": {**ISOLATED_OFF, "GX_T_INTERLACE_MODE": "3"},
    "dof_full": {**ISOLATED_OFF, "GX_T_DOF": "1", "GX_T_DOFFOCAL": "5.8", "GX_T_DOFRANGE": "0.5", "GX_DOF_FIXTURE": "1", "GX_T_BLOOM": "1", "GX_T_BLOOMINT": "1.0"},
    "dof_half": {**ISOLATED_OFF, "GX_T_DOF": "1", "GX_T_DOFFOCAL": "5.8", "GX_T_DOFRANGE": "0.5", "GX_T_DOF_RES": "1", "GX_DOF_FIXTURE": "1", "GX_T_BLOOM": "1", "GX_T_BLOOMINT": "1.0"},
    "dof_quarter": {**ISOLATED_OFF, "GX_T_DOF": "1", "GX_T_DOFFOCAL": "5.8", "GX_T_DOFRANGE": "0.5", "GX_T_DOF_RES": "2", "GX_DOF_FIXTURE": "1", "GX_T_BLOOM": "1", "GX_T_BLOOMINT": "1.0"},
    "snow": {**ISOLATED_OFF, "GX_T_SNOW": "1", "GX_SNOW_COVERAGE": "0.75", "GX_SNOW_MELT": "0.20", "GX_SNOW_HEIGHT": "0.015"},
    "snowfall": {**ISOLATED_OFF, "GX_T_SNOWFALL": "1", "GX_SNOWFALL_SEED": "1337", "GX_SNOWFALL_RATE": "448", "GX_SNOWFALL_WIND": "0.35"},
    "rain": {**ISOLATED_OFF, "GX_T_RAIN": "1", "GX_RAIN_SEED": "7331", "GX_RAIN_RATE": "384", "GX_RAIN_WIND": "1.2"},
    "fogsprite": {**ISOLATED_OFF, "GX_T_FOGSPRITE": "1", "GX_FOGSPRITE_SEED": "4242", "GX_FOGSPRITE_COUNT": "28", "GX_FOGSPRITE_WIND": "0.25", "GX_FOGSPRITE_OPACITY": "0.10"},
    "probes": {**ISOLATED_OFF, "GX_PROBE_GRID": "1", "GX_PROBE_SIZE": "128", "GX_CUBEMAP_TEST": "1", "GX_PROBE_REFRESH": "30"},
    "splat": {**ISOLATED_OFF, "GX_T_SPLAT": "1", "GX_SPLAT_SEED": "777", "GX_SPLAT_COUNT": "256"},
    "mirror": {**ISOLATED_OFF, "GX_MIRROR_PAIR": "1"},
}

#opencode: portable-review budgets for the opt-in palette pass. These are
#   reported as advisory overhead against the isolated row; they never gate
#   make check because GPU timing is host/driver-specific.
PALETTE_ADVISORY_BUDGETS_MS = {
    "palette16": {"cpu_p95_delta_ms": 2.0, "gpu_p95_delta_ms": 3.0},
    "palette256": {"cpu_p95_delta_ms": 2.0, "gpu_p95_delta_ms": 2.0},
}

FEATURE_ENV_KEYS = set(ISOLATED_OFF)
for _row in FEATURE_ROWS.values():
    FEATURE_ENV_KEYS.update(_row)
FEATURE_ENV_KEYS.update({"GX_CAM_SPIN", "GX_DIAG_CUBE", "GX_EXTRA_SHADOW_LIGHTS"})


#ai: source-frame windows reject legacy/stale rows, missing data and unsupported multi-draw aggregation.
def read_gpu_window(path, warmup, frames):
    expected = set(range(warmup + 1, warmup + frames + 1))
    selected = {}
    draws = set()
    with open(path, newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        required = {"frame", "draw", "readback_frame", "total_ms", "ssr_ms"}
        if not required.issubset(reader.fieldnames or ()):
            raise RuntimeError("GPU profile lacks frame identity; legacy CSV is not a matched timing stream")
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise RuntimeError("invalid GPU timing row")
            try:
                frame = int(row["frame"])
                draw = int(row["draw"])
                readback = int(row["readback_frame"])
            except (KeyError, TypeError, ValueError) as error:
                raise RuntimeError("invalid GPU frame identity") from error
            if frame not in expected:
                continue
            if frame in selected:
                raise RuntimeError("duplicate GPU source frame; multiple draws need explicit aggregation")
            if draw < 0 or draw in draws:
                raise RuntimeError("duplicate or invalid GPU draw identity")
            if readback < frame:
                raise RuntimeError("GPU readback precedes its source frame")
            try:
                timings = {key: float(value) for key, value in row.items() if key.endswith("_ms")}
            except (TypeError, ValueError) as error:
                raise RuntimeError("invalid GPU timing value") from error
            if any(not math.isfinite(value) or value < 0 for value in timings.values()):
                raise RuntimeError("invalid GPU timing value")
            selected[frame] = (draw, readback, timings["total_ms"])
            draws.add(draw)
    missing = expected - selected.keys()
    if missing:
        raise RuntimeError("GPU profile missing measured source frames: %s" % sorted(missing))
    ordered = sorted(selected)
    return [selected[frame][2] for frame in ordered], {
        "frame_ids": ordered,
        "draw_ids": [selected[frame][0] for frame in ordered],
        "readback_frame_ids": [selected[frame][1] for frame in ordered],
        "samples": len(ordered),
    }


#ai: CPU and GPU use the same measured simulation frames, excluding asynchronous query drain frames.
def read_cpu_window(path, warmup, frames):
    expected = set(range(warmup + 1, warmup + frames + 1))
    selected = {}
    with open(path, newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            frame = int(row["frame"])
            if frame not in expected:
                continue
            value = float(row["cpu_ms"])
            if frame in selected or not math.isfinite(value) or value < 0:
                raise RuntimeError("duplicate CPU frame or invalid CPU timing value")
            selected[frame] = value
    if expected != selected.keys():
        raise RuntimeError("CPU profile missing measured source frames")
    return [selected[frame] for frame in sorted(selected)]


def statistics(values):
    if not values:
        raise RuntimeError("benchmark timing stream is empty")
    ordered = sorted(values)

    def percentile(percent):
        index = (len(ordered) - 1) * percent / 100.0
        lower = int(index)
        upper = min(lower + 1, len(ordered) - 1)
        fraction = index - lower
        return ordered[lower] + (ordered[upper] - ordered[lower]) * fraction

    return {
        "count": len(values),
        "min_ms": min(values),
        "mean_ms": sum(values) / len(values),
        "median_ms": percentile(50.0),
        "p95_ms": percentile(95.0),
        "p99_ms": percentile(99.0),
        "max_ms": max(values),
    }


def parse_env(items):
    result = {}
    for item in items:
        if "=" not in item:
            raise RuntimeError("--env requires KEY=VALUE: %s" % item)
        key, value = item.split("=", 1)
        if not key.startswith("GX_"):
            raise RuntimeError("benchmark overrides must use GX_ keys: %s" % key)
        result[key] = value
    return result


def feature_metadata(environment):
    return {
        key: environment[key]
        for key in sorted(environment)
        if key.startswith("GX_T_") or key in FEATURE_ENV_KEYS
    }


def selected_rows(value, parser):
    names = [item.strip() for item in value.split(",") if item.strip()]
    unknown = sorted(set(names) - set(FEATURE_ROWS))
    if unknown:
        parser.error("unknown benchmark row(s): " + ", ".join(unknown))
    if not names:
        parser.error("benchmark row selection is empty")
    return names


def clean_feature_environment(environment):
    for key in list(environment):
        if key.startswith("GX_T_") or key in FEATURE_ENV_KEYS:
            environment.pop(key, None)


def run_capture(args, output, row_name, row_environment, repeat, common_environment):
    repeat_output = os.path.join(output, row_name, "repeat-%02d" % repeat)
    os.makedirs(repeat_output, exist_ok=True)
    cpu_path = os.path.join(repeat_output, "cpu.csv")
    gpu_path = os.path.join(repeat_output, "gpu.csv")
    log_path = os.path.join(repeat_output, "run.log")
    for path in (cpu_path, gpu_path, log_path):
        if os.path.exists(path):
            os.unlink(path)

    total_frames = args.warmup + args.frames + GPU_DRAIN_FRAMES
    fixed_dt = 1.0 / args.target_fps
    environment = audit.scene_env(common_environment)
    environment.update(row_environment)
    environment.update({
        "GX_FIXED_DT": "%.12f" % fixed_dt,
        "GX_BENCHMARK_CPU_CSV": cpu_path,
        "GX_BENCHMARK_UNCAPPED": "1",
        "GX_BENCHMARK_NO_FLUSH": "1",
    })
    scene_tokens = shlex.split(args.scene)
    extra_args = tuple(scene_tokens[1:] + [
        "--seed", str(args.seed),
        "--gfx-profile=" + gpu_path,
    ])
    started = time.perf_counter()
    result = audit.run_scene(
        scene_tokens[0], environment, binary=os.path.abspath(args.binary),
        width=args.width, height=args.height, fps=None, frames=total_frames,
        timeout=args.timeout, check=False, extra_args=extra_args)
    with open(log_path, "w", encoding="utf-8") as log:
        log.write(audit.strip_ansi(result.stdout or ""))
    if result.returncode != 0:
        raise RuntimeError("%s repeat %d exited %d" % (
            row_name, repeat, result.returncode))
    wall_seconds = time.perf_counter() - started

    cpu_values = read_cpu_window(cpu_path, args.warmup, args.frames)
    gpu_values, gpu_identity = read_gpu_window(gpu_path, args.warmup, args.frames)
    report = {
        "row": row_name,
        "repeat": repeat,
        "features": feature_metadata(environment),
        "cpu": statistics(cpu_values),
        "gpu": statistics(gpu_values) if gpu_values else None,
        "gpu_identity": gpu_identity,
        "unmeasured_query_drain_frames": GPU_DRAIN_FRAMES,
        "wall_seconds": wall_seconds,
        "artifacts": {
            "cpu_csv": cpu_path,
            "gpu_csv": gpu_path if os.path.isfile(gpu_path) else None,
            "log": log_path,
        },
    }
    with open(os.path.join(repeat_output, "report.json"), "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    return report, cpu_values, gpu_values


def run_row(args, output, row_name, common_environment):
    row_environment = FEATURE_ROWS[row_name]
    runs = []
    cpu_values = []
    gpu_values = []
    for repeat in range(1, args.repeats + 1):
        report, cpu, gpu = run_capture(
            args, output, row_name, row_environment, repeat, common_environment)
        runs.append(report)
        cpu_values.extend(cpu)
        gpu_values.extend(gpu)
    return {
        "features": feature_metadata({**common_environment, **row_environment}),
        "repeats": runs,
        "cpu": statistics(cpu_values),
        "gpu": statistics(gpu_values) if len(gpu_values) == args.frames * args.repeats else None,
    }


def compare_rows(report, comparison_row, budget_ms):
    reference = report["rows"].get(comparison_row)
    if reference is None:
        raise RuntimeError("comparison row is not selected: %s" % comparison_row)
    reference_cpu = reference["cpu"]["p95_ms"]
    reference_gpu = reference["gpu"]["p95_ms"] if reference["gpu"] else None
    for row in report["rows"].values():
        row_cpu = row["cpu"]["p95_ms"]
        cpu_delta = row_cpu - reference_cpu
        row["comparison"] = {
            "against": comparison_row,
            "cpu_p95_delta_ms": cpu_delta,
            "cpu_p95_delta_pct": (cpu_delta / reference_cpu * 100.0)
                if reference_cpu else None,
            "cpu_p95_over_budget_ms": row_cpu - budget_ms,
            "gpu_p95_delta_ms": None,
            "gpu_p95_delta_pct": None,
            "gpu_p95_over_budget_ms": None,
        }
        if reference_gpu is not None and row["gpu"]:
            gpu_delta = row["gpu"]["p95_ms"] - reference_gpu
            row["comparison"]["gpu_p95_delta_ms"] = gpu_delta
            row["comparison"]["gpu_p95_delta_pct"] = (
                gpu_delta / reference_gpu * 100.0) if reference_gpu else None
            row["comparison"]["gpu_p95_over_budget_ms"] = (
                row["gpu"]["p95_ms"] - budget_ms)
        palette_mode = row["features"].get("GX_T_PALETTE_MODE")
        palette_row = {"1": "palette16", "2": "palette256"}.get(palette_mode)
        advisory_budget = PALETTE_ADVISORY_BUDGETS_MS.get(palette_row)
        if advisory_budget:
            cpu_pass = cpu_delta <= advisory_budget["cpu_p95_delta_ms"]
            gpu_pass = (row["comparison"]["gpu_p95_delta_ms"] is None
                        or row["comparison"]["gpu_p95_delta_ms"]
                        <= advisory_budget["gpu_p95_delta_ms"])
            row["comparison"]["palette_advisory"] = {
                "budgets_ms": advisory_budget,
                "pass": cpu_pass and gpu_pass,
            }


def compare_reference(report, reference_path):
    with open(reference_path, encoding="utf-8") as stream:
        reference = json.load(stream)
    result = {
        "path": os.path.abspath(reference_path),
        "name": reference.get("name", "unnamed"),
        "baseline_match": True,
        "rows": {},
        "pass": True,
    }
    expected_baseline = reference.get("baseline", {})
    for key in ("width", "height", "target_fps", "seed"):
        if report["baseline"].get(key) != expected_baseline.get(key):
            result["baseline_match"] = False
    if not result["baseline_match"]:
        result["pass"] = False
    reference_rows = reference.get("rows", {})
    for row_name, row in report["rows"].items():
        expected = reference_rows.get(row_name)
        row_result = {"pass": True, "cpu": None, "gpu": None}
        if expected is None:
            row_result["pass"] = False
            row_result["error"] = "row missing from reference"
        else:
            for metric in ("cpu", "gpu"):
                actual_stats = row.get(metric)
                expected_stats = expected.get(metric, {})
                budget = expected.get("budgets_ms", {}).get("%s_p95" % metric)
                metric_result = {
                    "actual_p95_ms": actual_stats["p95_ms"] if actual_stats else None,
                    "reference_p95_ms": expected_stats.get("p95_ms"),
                    "budget_p95_ms": budget,
                    "delta_ms": None,
                    "pass": False,
                }
                if actual_stats and budget is not None:
                    metric_result["delta_ms"] = (
                        actual_stats["p95_ms"] - expected_stats.get("p95_ms", 0.0))
                    metric_result["pass"] = actual_stats["p95_ms"] <= budget
                row_result[metric] = metric_result
                if not metric_result["pass"]:
                    row_result["pass"] = False
        result["rows"][row_name] = row_result
        if not row_result["pass"]:
            result["pass"] = False
    report["reference"] = result


def run(args, parser):
    binary = os.path.abspath(args.binary)
    if not os.path.isfile(binary):
        raise RuntimeError("missing benchmark binary: %s" % binary)
    if args.warmup < 0 or args.frames <= 0 or args.repeats <= 0:
        raise RuntimeError("warmup must be non-negative; frames and repeats must be positive")
    if args.width <= 0 or args.height <= 0 or args.target_fps <= 0.0:
        raise RuntimeError("width, height, and target FPS must be positive")
    rows = selected_rows(args.rows, parser)
    if args.compare_to not in rows:
        raise RuntimeError("--compare-to row is not selected: %s" % args.compare_to)

    output = os.path.abspath(args.output)
    os.makedirs(output, exist_ok=True)
    #opencode: clean env — ambient feature hooks must not leak into a row, and
    #   settings stay protected (scene_env sets GX_NO_SETTINGS_SAVE).
    common_environment = audit.scene_env(clean=True)
    clean_feature_environment(common_environment)
    common_environment.update(parse_env(args.environment))
    start = time.perf_counter()
    report = {
        "schema": 3,
        "advisory": not args.check,
        "baseline": {
            "width": args.width,
            "height": args.height,
            "target_fps": args.target_fps,
            "seed": args.seed,
            "budget_ms": 1000.0 / args.target_fps,
        },
        "scene": args.scene,
        "binary": binary,
        "warmup_frames": args.warmup,
        "measured_frames_per_repeat": args.frames,
        "measured_frame_ids": [args.warmup + 1, args.warmup + args.frames],
        "unmeasured_query_drain_frames": GPU_DRAIN_FRAMES,
        "repeats": args.repeats,
        "fixed_dt": 1.0 / args.target_fps,
        "rows_order": rows,
        "comparison_row": args.compare_to,
        "palette_advisory_budgets_ms": PALETTE_ADVISORY_BUDGETS_MS,
        "rows": {},
    }
    for row_name in rows:
        report["rows"][row_name] = run_row(
            args, output, row_name, common_environment)
    compare_rows(report, args.compare_to, report["baseline"]["budget_ms"])
    if args.reference:
        compare_reference(report, os.path.abspath(args.reference))
    report["wall_seconds"] = time.perf_counter() - start
    report_path = os.path.join(output, "report.json")
    with open(report_path, "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")

    if args.check:
        for row_name, row in report["rows"].items():
            if row["cpu"]["count"] != args.frames * args.repeats:
                raise RuntimeError("%s CPU measured count mismatch" % row_name)
            if row["gpu"] is None:
                raise RuntimeError("%s GPU timing profile is unavailable" % row_name)
        if args.reference and not report["reference"]["pass"]:
            raise RuntimeError("benchmark exceeded the pinned reference budget")
    print("GFX-BENCHMARK: %s" % ("PASS" if args.check else "ADVISORY"))
    print("  baseline: %dx%d @ %.0f FPS; seed %d; repeats %d" % (
        args.width, args.height, args.target_fps, args.seed, args.repeats))
    print("  comparison reference: %s; rows: %s" % (
        args.compare_to, ", ".join(rows)))
    for row_name in rows:
        row = report["rows"][row_name]
        cpu = row["cpu"]
        gpu = row["gpu"]
        comparison = row["comparison"]
        gpu_text = "n/a"
        if gpu:
            gpu_text = "%.3f ms" % gpu["p95_ms"]
        print("  %-9s CPU p95 %.3f ms (%+.3f vs %s), GPU p95 %s" % (
            row_name, cpu["p95_ms"], comparison["cpu_p95_delta_ms"],
            args.compare_to, gpu_text))
        if comparison.get("palette_advisory"):
            print("             palette overhead advisory: %s" % (
                "PASS" if comparison["palette_advisory"]["pass"] else "OVER"))
    if args.reference:
        print("  reference: %s (%s)" % (
            "PASS" if report["reference"]["pass"] else "FAIL",
            report["reference"]["name"]))
    print("  report: %s" % report_path)


def main():
    parser = argparse.ArgumentParser(description="Run the deterministic gX GFX benchmark.")
    parser.add_argument("--output", default=DEFAULT_OUT)
    parser.add_argument("--binary", default=DEFAULT_BIN)
    parser.add_argument("--scene", default="--showcase-minimal")
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
    parser.add_argument("--target-fps", type=float, default=DEFAULT_TARGET_FPS)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--warmup", type=int, default=DEFAULT_WARMUP)
    parser.add_argument("--frames", type=int, default=DEFAULT_FRAMES)
    parser.add_argument("--repeats", type=int, default=DEFAULT_REPEATS)
    parser.add_argument("--rows", default=DEFAULT_ROWS)
    parser.add_argument("--compare-to", default="isolated")
    parser.add_argument("--reference", help="compact JSON baseline with p95 budgets")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--env", action="append", default=[], dest="environment")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    run(args, parser)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
        print("GFX-BENCHMARK: FAIL — %s" % error)
        raise SystemExit(1)
