#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: native asymmetric geometry witnesses; exact algebra and IEEE approximation remain separate.
import argparse
from fractions import Fraction as Q
import json
import math
import os
from pathlib import Path
import subprocess

from math_pass import ROOT, TOL_EXACT, TOL_FLOAT
from reprojection_residual_check import row, require


def reflect(v, n, d):
    signed = sum(a*b for a, b in zip(v, n))-d
    return [a-2*signed*b for a, b in zip(v, n)]


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def look_at(eye, target, up):
    def unit(v):
        length = math.sqrt(dot(v, v))
        require(length > 0, "degenerate LookAt")
        return [x/length for x in v]
    z = unit([a-b for a, b in zip(eye, target)])
    x = unit(cross(up, z))
    y = cross(z, x)
    return [x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
            -dot(x, eye), -dot(y, eye), -dot(z, eye), 1]


def product(a, b):
    return [sum(a[4*r+i]*b[4*i+c] for i in range(4)) for r in range(4) for c in range(4)]


def close(actual, expected, tolerance=TOL_EXACT):
    require(all(abs(a-b) <= tolerance*max(1, abs(b)) for a, b in zip(actual, expected)), "native correspondence exceeds existing math-pass tolerance")


def project(clip):
    require(clip[3] != 0, "zero clip denominator")
    return [(clip[i]/clip[3]+1)/2 for i in range(2)]


def check_dump(text):
    cases = {}
    for line in text.splitlines():
        if not line.startswith("PLANAR_GEOMETRY "):
            continue
        _, name, *fields = line.split()
        require(name in {"axis", "oblique"} and name not in cases, "bad native fixture name")
        require(len(fields) == 101, "bad native field count")
        f = list(map(float.fromhex, fields))
        require(all(math.isfinite(x) for x in f), "nonfinite fixture")
        q = list(map(Q.from_float, f))
        d, n, position, target, up = q[0], q[1:4], q[4:7], q[7:10], q[10:13]
        mirror_pos, mirror_target, mirror_up = q[13:16], q[16:19], q[19:22]
        point, reflected_point, twice = q[22:25], q[25:28], q[28:31]
        view, projection, vp, clip, uv = q[31:47], q[47:63], q[63:79], q[79:83], q[83:85]
        close(mirror_pos, reflect(position, n, d))
        close(mirror_target, reflect(target, n, d))
        close(mirror_up, reflect(up, n, Q(0)))
        close(reflected_point, reflect(point, n, d))
        close(twice, reflect(mirror_pos, n, d))
        close(twice, position)
        close(reflected_point, point)
        close(view, look_at(list(map(float, mirror_pos)), list(map(float, mirror_target)), list(map(float, mirror_up))), TOL_FLOAT)
        close(q[85:101], look_at(list(map(float, position)), list(map(float, target)), list(map(float, up))), TOL_FLOAT)
        close(vp, product(view, projection))
        exact_clip = row(reflected_point+[Q(1)], vp)
        require(exact_clip[3] > Q(1, 1000), "lookup gates not satisfied")
        close(clip, exact_clip)
        expected_uv = project(exact_clip)
        close(uv, expected_uv)
        transposed = [vp[4*c+r] for r in range(4) for c in range(4)]
        wrong_uv = project(row(reflected_point+[Q(1)], transposed))
        transpose_gap = max(abs(a-b) for a, b in zip(wrong_uv, expected_uv))
        flip_gap = abs(1-expected_uv[1]-expected_uv[1])
        wrong_order_gap = max(abs(a-b) for a, b in zip(product(projection, view), vp))
        wrong_up_gap = max(abs(a-b) for a, b in zip(reflect(up, n, d), mirror_up))
        require(min(transpose_gap, flip_gap, wrong_order_gap, wrong_up_gap) > TOL_FLOAT,
                "asymmetric fixture does not distinguish convention negative controls")
        squared = lambda a, b: sum((x-y)**2 for x, y in zip(a, b))
        close([squared(mirror_pos, mirror_target)], [squared(position, target)])
        cases[name] = {"stored_normal_squared_length_defect": float(abs(dot(n, n)-1)),
            "on_plane_signed_defect": float(abs(dot(n, point)-d)),
            "native_involution_error": float(max(abs(a-b) for a, b in zip(twice, position))),
            "native_squared_distance_error": float(abs(squared(mirror_pos, mirror_target)-squared(position, target))),
            "view_double_oracle_error": max(abs(float(a)-b) for a, b in zip(view, look_at(list(map(float, mirror_pos)), list(map(float, mirror_target)), list(map(float, mirror_up))))),
            "native_uv": list(map(float, uv)), "exact_stored_vp_uv": list(map(float, expected_uv)),
            "native_uv_arithmetic_error": float(max(abs(a-b) for a, b in zip(uv, expected_uv))),
            "transpose_negative_gap": float(transpose_gap), "y_flip_negative_gap": float(flip_gap),
            "composition_negative_gap": float(wrong_order_gap), "point_up_translation_negative_gap": float(wrong_up_gap),
            "gpu_certified": False}
    require(set(cases) == {"axis", "oblique"}, "missing fixtures")
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/gX_build")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    env = dict(os.environ, GX_MATH_DUMP="1", GX_PLANAR_GEOMETRY_DUMP="1", GX_NO_SETTINGS_SAVE="1")
    run = subprocess.run([str(args.binary.resolve()), "--test-only"], cwd=ROOT, env=env,
                         capture_output=True, text=True, check=True, timeout=60)
    report = {"result": "pass", "cases": check_dump(run.stdout), "limits": "Existing math-pass tolerances, approximate stored normals, measured CPU errors; LookAt/GPU accuracy not formally certified."}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2)+"\n")
    print("PLANAR_GEOMETRY: PASS — camera/row VP/UV witnesses with transpose, order, Y-flip and up-offset negatives")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
