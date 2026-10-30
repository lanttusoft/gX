#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: exact stored-float residual oracle; CPU rounding witnesses are not GPU error certificates.
"""Check native inverse/reprojection fixtures against GfxReprojectionError bounds."""
import argparse
from fractions import Fraction as Q
import json
import math
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {"perspective", "diagonal", "dependent", "hilbert", "rounded_singular"}


def row(v, m):
    return [sum(v[r] * m[4*r+c] for r in range(4)) for c in range(4)]


def normalize(v):
    if not v[3]:
        raise ValueError("zero homogeneous denominator")
    return [x/v[3] for x in v]


def project(v):
    return [x/v[3]/2+Q(1, 2) for x in v[:3]]


def residual(m, inv):
    return [sum(m[4*r+i]*inv[4*i+c] for i in range(4))-(r == c)
            for r in range(4) for c in range(4)]


#ai: mirror the proved componentwise sums without sign cancellation or a global max envelope.
def weighted_action(weights, matrix):
    return [sum(weights[r]*abs(matrix[4*r+c]) for r in range(4)) for c in range(4)]


def weighted_bounds(world, exact_residual, previous, mu, candidate_clip, ideal_clip):
    recovery = weighted_action(list(map(abs, world)), exact_residual)
    normalized = [(recovery[i]+abs(world[i])*recovery[3])/mu for i in range(3)]+[Q(0)]
    clip = weighted_action(normalized, previous)
    projected = [(clip[i]+abs(ideal_clip[i]/ideal_clip[3])*clip[3])/(2*abs(candidate_clip[3]))
                 for i in range(3)]
    return recovery, normalized, clip, projected


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check_case(name, values):
    require(len(values) == 63, f"{name}: expected 63 hex values")
    native = [float.fromhex(x) for x in values]
    if name == "rounded_singular":
        require(all(math.isfinite(x) for x in native[:16]), "singular input must be finite")
        require(native[0] == native[1] == native[4] == native[5] == 1, "perturbation must round away")
        require(any(not math.isfinite(x) for x in native[16:32]), "singular inverse unexpectedly finite")
        require(any(not math.isfinite(x) for x in native[60:]), "singular projection unexpectedly finite")
        return {"classification": "expected_nonfinite", "bound_applicable": False}
    require(all(math.isfinite(x) for x in native), f"{name}: unexpected nonfinite value")
    exact = list(map(Q.from_float, native))
    m, inv, prev = exact[:16], exact[16:32], exact[32:48]
    world, observed_world, observed_clip, observed = exact[48:52], exact[52:56], exact[56:60], exact[60:]
    require(world[3] == 1, "world must be affine")
    exact_residual = residual(m, inv)
    double_residual = residual(native[:16], native[16:32])
    measurement_uncertainty = max(abs(a-Q.from_float(b)) for a, b in zip(exact_residual, double_residual))
    measured = max(abs(Q.from_float(x)) for x in double_residual)
    e = max(map(abs, exact_residual))
    require(e <= measured+measurement_uncertainty, "measurement margin does not contain exact residual")
    clip = row(world, m)
    recovered = row(clip, inv)
    candidate_world = normalize(recovered)
    shader_world = normalize(row(normalize(clip), inv))
    require(shader_world == candidate_world, "normalize-first correspondence failed")
    w = max(map(abs, world))
    k = 4*w*e
    mu = abs(recovered[3])
    delta = (k+w*k)/mu
    require(max(abs(a-b) for a, b in zip(candidate_world, world)) <= delta, "world bound violated")
    candidate_clip, ideal_clip = row(candidate_world, prev), row(world, prev)
    p = max(map(abs, prev))
    clip_bound = 4*delta*p
    require(max(abs(a-b) for a, b in zip(candidate_clip, ideal_clip)) <= clip_bound, "clip bound violated")
    require(candidate_clip[3] > 0 and ideal_clip[3] > 0, "previous clip must be in front")
    nu = abs(candidate_clip[3])
    candidate, ideal = project(candidate_clip), project(ideal_clip)
    bounds = [(clip_bound+abs(ideal_clip[i]/ideal_clip[3])*clip_bound)/(2*nu) for i in range(3)]
    errors = [abs(a-b) for a, b in zip(candidate, ideal)]
    require(all(a <= b for a, b in zip(errors, bounds)), "exact projected bound violated")
    cpu_uncertainty = [abs(a-b) for a, b in zip(observed, candidate)]
    observed_errors = [abs(a-b) for a, b in zip(observed, ideal)]
    observed_bounds = [a+b for a, b in zip(bounds, cpu_uncertainty)]
    require(all(a <= b for a, b in zip(observed_errors, observed_bounds)), "observed bound violated")
    recovery_weights, world_weights, clip_weights, weighted = weighted_bounds(
        world, exact_residual, prev, mu, candidate_clip, ideal_clip)
    require(all(abs(a-b) <= weight for a, b, weight in zip(recovered, world, recovery_weights)),
            "weighted residual action violated")
    require(all(abs(a-b) <= weight for a, b, weight in zip(candidate_world, world, world_weights)),
            "weighted normalized world bound violated")
    require(all(abs(a-b) <= weight for a, b, weight in zip(candidate_clip, ideal_clip, clip_weights)),
            "weighted previous clip bound violated")
    require(all(a <= b for a, b in zip(errors, weighted)), "weighted projection bound violated")
    require(all(a <= b for a, b in zip(weighted, bounds)), "weighted bound exceeded uniform envelope")
    weighted_observed = [a+b for a, b in zip(weighted, cpu_uncertainty)]
    require(all(a <= b for a, b in zip(observed_errors, weighted_observed)), "weighted observed bound violated")
    scale = [640, 480, 1]
    budget = [Q(1, 20), Q(1, 20), Q(1, 2000)]
    return {
        "classification": "finite", "bound_applicable": True,
        "exact_residual": float(e), "double_measured_residual": float(measured),
        "residual_measurement_uncertainty": float(measurement_uncertainty),
        "recovery_margin_unscaled": float(mu), "previous_margin": float(nu),
        "exact_world_error": float(max(abs(a-b) for a, b in zip(candidate_world, world))),
        "world_bound": float(delta),
        "cpu_world_deviation": float(max(abs(a-b) for a, b in zip(observed_world, candidate_world))),
        "cpu_clip_deviation": float(max(abs(a-b) for a, b in zip(observed_clip, candidate_clip))),
        "exact_projected_error": list(map(float, errors)),
        "observed_axis_pixels_and_depth": [float(x*s) for x, s in zip(observed_errors, scale)],
        "exact_bound_axis_pixels_and_depth": [float(x*s) for x, s in zip(bounds, scale)],
        "measured_cpu_uncertainty_axis_pixels_and_depth": [float(x*s) for x, s in zip(cpu_uncertainty, scale)],
        "observed_bound_axis_pixels_and_depth": [float(x*s) for x, s in zip(observed_bounds, scale)],
        "observed_within_existing_budgets_advisory": [x*s <= b for x, s, b in zip(observed_errors, scale, budget)],
        "bound_within_existing_budgets_advisory": [x*s <= b for x, s, b in zip(observed_bounds, scale, budget)],
        "weighted_recovery_bound": list(map(float, recovery_weights)),
        "weighted_world_bound": list(map(float, world_weights)),
        "weighted_clip_bound": list(map(float, clip_weights)),
        "weighted_exact_bound_axis_pixels_and_depth": [float(x*s) for x, s in zip(weighted, scale)],
        "weighted_observed_bound_axis_pixels_and_depth": [float(x*s) for x, s in zip(weighted_observed, scale)],
        "weighted_bound_within_existing_budgets_advisory": [x*s <= b for x, s, b in zip(weighted_observed, scale, budget)],
        "gpu_certified": False,
    }


def check_dump(text):
    cases = {}
    for line in text.splitlines():
        if line.startswith("REPROJECTION "):
            _, name, *values = line.split()
            require(name in EXPECTED and name not in cases, "unknown or duplicate fixture")
            cases[name] = check_case(name, values)
    require(set(cases) == EXPECTED, "missing native fixtures")
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/gX_build")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--dump", type=Path, help="read a previously captured native dump")
    args = parser.parse_args()
    if args.dump:
        text = args.dump.read_text()
    else:
        env = dict(os.environ, GX_MATH_DUMP="1", GX_REPROJECTION_DUMP="1", GX_NO_SETTINGS_SAVE="1")
        run = subprocess.run([str(args.binary.resolve()), "--test-only"], cwd=ROOT, env=env,
                             capture_output=True, text=True, check=True, timeout=60)
        text = run.stdout
    report = {"result": "pass", "cases": check_dump(text),
              "limits": "Exact stored-float algebra with measured fixture-specific CPU deviation; not a universal IEEE or GPU bound. Accuracy budgets advisory."}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2)+"\n")
    print("REPROJECTION_RESIDUAL: PASS — 4 finite bounds and 1 expected singular fixture; GPU accuracy not certified")
    for name, case in report["cases"].items():
        print(name, json.dumps(case, sort_keys=True))


if __name__ == "__main__":
    main()
