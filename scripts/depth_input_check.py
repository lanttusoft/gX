#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: native camera input sensitivity vs exact fractions; no GPU texture/rounding certification.
import argparse
from fractions import Fraction as Q
import json
import math
import os
from pathlib import Path
import subprocess

from reprojection_residual_check import ROOT, normalize, project, require, residual, row, weighted_action, weighted_bounds


def shader_clip(u, v, jx, jy, depth):
    return [2*u-1-jx, 2*v-1-jy, 2*depth-1, Q(1)]


def input_weights(eu, ev, ejx, ejy, ed):
    return [2*eu+ejx, 2*ev+ejy, 2*ed, Q(0)]


def normalization_weights(reference, weights, mu):
    reference = list(map(Q, reference))
    require(mu > 0 and reference[3] != 0, "unsafe normalization margin")
    return [(weights[i]+abs(reference[i]/reference[3])*weights[3])/mu for i in range(3)]+[Q(0)]


def check_case(values):
    require(len(values) == 72, "expected 72 native fields")
    floats = [float.fromhex(x) for x in values]
    require(all(math.isfinite(x) for x in floats), "unexpected nonfinite input witness")
    q = list(map(Q.from_float, floats))
    current, inverse, previous, world = q[:16], q[16:32], q[32:48], q[48:52]
    observed = q[60:63]
    u, v, jx, jy, depth, jx0, jy0, code, native_depth = q[63:]
    require(world[3] == 1, "affine world required")
    levels = Q(16777215)
    require(code.denominator == 1 and 0 <= code <= levels, "bad simulated grid code")
    require(abs(code-levels*native_depth) <= Q(1, 2), "nearest-grid hypothesis failed")
    grid_depth = code/levels
    grid_bound = Q(1, 2)/levels
    require(abs(grid_depth-native_depth) <= grid_bound, "grid-depth bound failed")
    reference_clip = normalize(row(world, current))
    u0, v0 = (reference_clip[0]+jx0+1)/2, (reference_clip[1]+jy0+1)/2
    d0 = (reference_clip[2]+1)/2
    eu, ev, ejx, ejy, ed = abs(u-u0), abs(v-v0), abs(jx-jx0), abs(jy-jy0), abs(depth-d0)
    depth_input_bound = abs(native_depth-d0)+grid_bound+abs(depth-grid_depth)
    require(ed <= depth_input_bound, "depth input decomposition failed")
    actual_clip = shader_clip(u, v, jx, jy, depth)
    weights = input_weights(eu, ev, ejx, ejy, depth_input_bound)
    require(all(abs(a-b) <= w for a, b, w in zip(actual_clip, reference_clip, weights)), "clip input bound failed")
    actual_raw, baseline_raw = row(actual_clip, inverse), row(reference_clip, inverse)
    require(actual_raw[3] > 0 and baseline_raw[3] > 0, "camera recovery must have positive w")
    raw_weights = weighted_action(weights, inverse)
    require(all(abs(a-b) <= w for a, b, w in zip(actual_raw, baseline_raw, raw_weights)), "inverse input bound failed")
    input_world_weights = normalization_weights(baseline_raw, raw_weights, abs(actual_raw[3]))
    actual_world, baseline_world = normalize(actual_raw), normalize(baseline_raw)
    require(all(abs(a-b) <= w for a, b, w in zip(actual_world, baseline_world, input_world_weights)), "normalized input bound failed")
    unscaled_baseline = row(row(world, current), inverse)
    baseline_clip, ideal_clip = row(baseline_world, previous), row(world, previous)
    _, baseline_weights, _, _ = weighted_bounds(world, residual(current, inverse), previous,
                                               abs(unscaled_baseline[3]), baseline_clip, ideal_clip)
    total_world = [a+b for a, b in zip(input_world_weights, baseline_weights)]
    require(all(abs(a-b) <= w for a, b, w in zip(actual_world, world, total_world)), "combined world bound failed")
    actual_previous = row(actual_world, previous)
    require(actual_previous[3] > 0 and ideal_clip[3] > 0, "positive previous clip required")
    clip_weights = weighted_action(total_world, previous)
    bounds = [(clip_weights[i]+abs(ideal_clip[i]/ideal_clip[3])*clip_weights[3])/(2*abs(actual_previous[3])) for i in range(3)]
    candidate, ideal = project(actual_previous), project(ideal_clip)
    exact_error = [abs(a-b) for a, b in zip(candidate, ideal)]
    require(all(a <= b for a, b in zip(exact_error, bounds)), "combined projection bound failed")
    cpu_deviation = [abs(a-b) for a, b in zip(observed, candidate)]
    observed_error = [abs(a-b) for a, b in zip(observed, ideal)]
    final_bounds = [a+b for a, b in zip(bounds, cpu_deviation)]
    require(all(a <= b for a, b in zip(observed_error, final_bounds)), "observed projection bound failed")
    scale, budget = [640, 480, 1], [Q(1, 20), Q(1, 20), Q(1, 2000)]
    return {
        "depth_input_error": float(ed), "native_depth_arithmetic_error": float(abs(native_depth-d0)),
        "simulated_grid_error": float(abs(grid_depth-native_depth)), "conditional_grid_bound": float(grid_bound),
        "stored_float_grid_deviation": float(abs(depth-grid_depth)), "total_depth_input_bound": float(depth_input_bound),
        "clip_input_bound": list(map(float, weights)),
        "actual_raw_margin": float(abs(actual_raw[3])),
        "input_world_bound": list(map(float, input_world_weights)), "baseline_inverse_world_bound": list(map(float, baseline_weights)),
        "exact_error_axis_pixels_and_depth": [float(x*s) for x, s in zip(exact_error, scale)],
        "cpu_deviation_axis_pixels_and_depth": [float(x*s) for x, s in zip(cpu_deviation, scale)],
        "observed_error_axis_pixels_and_depth": [float(x*s) for x, s in zip(observed_error, scale)],
        "bound_axis_pixels_and_depth": [float(x*s) for x, s in zip(final_bounds, scale)],
        "observed_budget_comparison_advisory": [x*s <= b for x, s, b in zip(observed_error, scale, budget)],
        "gpu_certified": False,
    }


def check_dump(text):
    cases = {}
    for line in text.splitlines():
        if line.startswith("DEPTH_INPUT "):
            _, name, *values = line.split()
            require(name in {"near", "mid", "far"} and name not in cases, "bad input fixture name")
            cases[name] = check_case(values)
    require(set(cases) == {"near", "mid", "far"}, "missing input fixtures")
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/gX_build")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--dump", type=Path)
    args = parser.parse_args()
    if args.dump:
        text = args.dump.read_text()
    else:
        run = subprocess.run([str(args.binary.resolve()), "--test-only"], cwd=ROOT,
                             env=dict(os.environ, GX_MATH_DUMP="1", GX_REPROJECTION_DUMP="1", GX_NO_SETTINGS_SAVE="1"),
                             capture_output=True, text=True, timeout=60, check=True)
        text = run.stdout
    report = {"result": "pass", "cases": check_dump(text),
              "limits": "Native CPU fixtures with simulated nearest UNORM24 grid; neither actual texture sampling nor GPU arithmetic. CPU deviation measured per fixture; accuracy advisory."}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2)+"\n")
    print("DEPTH_INPUT: PASS — near/mid/far input and combined residual bounds; GPU accuracy not certified")
    for name, case in report["cases"].items():
        print(name, json.dumps(case, sort_keys=True))


if __name__ == "__main__":
    main()
