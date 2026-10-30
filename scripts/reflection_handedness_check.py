#!/usr/bin/env python3
#ai: native LookAt frames vs exact stored-float reflected bases; no generic GPU culling inference.
import argparse
from fractions import Fraction as Q
import json
import os
from pathlib import Path
import subprocess

from planar_geometry_check import ROOT, TOL_FLOAT, check_dump as check_geometry, close, cross, dot, product, project, reflect
from reprojection_residual_check import require, row


def axes(view):
    return [[view[i], view[i+4], view[i+8]] for i in range(3)]


def orientation(frame):
    return dot(cross(frame[0], frame[1]), frame[2])


def area(points):
    a, b, c = points
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])


def view_from_axes(frame, eye):
    x, y, z = frame
    return [x[0],y[0],z[0],0,x[1],y[1],z[1],0,x[2],y[2],z[2],0,
            -dot(x,eye),-dot(y,eye),-dot(z,eye),1]


def check_dump(text):
    check_geometry(text)
    cases = {}
    for line in text.splitlines():
        if not line.startswith("PLANAR_GEOMETRY "):
            continue
        _, name, *fields = line.split()
        q = [Q.from_float(float.fromhex(x)) for x in fields]
        normal, eye, point = q[1:4], q[13:16], q[22:25]
        original, rebuilt = axes(q[85:101]), axes(q[31:47])
        raw = [reflect(axis, normal, Q(0)) for axis in original]
        expected = [[-x for x in raw[0]], raw[1], raw[2]]
        for actual, oracle in zip(rebuilt, expected):
            close(actual, oracle, TOL_FLOAT)
        original_orientation, raw_orientation, rebuilt_orientation = map(orientation, (original, raw, rebuilt))
        require(original_orientation > 0 and raw_orientation < 0 and rebuilt_orientation > 0,
                "native orientation signs disagree with conditional handedness contract")
        close([original_orientation, rebuilt_orientation], [Q(1), Q(1)], TOL_FLOAT)
        close([raw_orientation], [-original_orientation], TOL_FLOAT)
        tangent = [normal[1], -normal[0], Q(0)]
        points = [point, [p+Q(23, 10)*t for p, t in zip(point, tangent)],
                  [point[0], point[1], point[2]+Q(17, 10)]]
        raw_vp = product(view_from_axes(raw, eye), q[47:63])
        native_vp = q[63:79]
        raw_clips, native_clips = [[row(p+[Q(1)], vp) for p in points] for vp in (raw_vp, native_vp)]
        require(all(c[3] > 0 for c in raw_clips+native_clips), "selected triangle crosses camera plane")
        raw_uv, native_uv = [list(map(project, clips)) for clips in (raw_clips, native_clips)]
        for native, raw_sample in zip(native_uv, raw_uv):
            close(native, [1-raw_sample[0], raw_sample[1]], TOL_FLOAT)
        raw_area, native_area = area(raw_uv), area(native_uv)
        require(raw_area*native_area < 0, "selected projected triangle must reverse area sign")
        close([native_area], [-raw_area], TOL_FLOAT)
        cases[name] = {"original_orientation": float(original_orientation),
            "geometrically_reflected_orientation": float(raw_orientation), "native_lookat_orientation": float(rebuilt_orientation),
            "axis_identity_error": float(max(abs(a-b) for av, bv in zip(rebuilt, expected) for a, b in zip(av, bv))),
            "geometric_frame_projected_area": float(raw_area), "lookat_frame_projected_area": float(native_area),
            "area_reversal_error": float(abs(native_area+raw_area)),
            "ccw_front_in_geometric_frame": raw_area > 0, "ccw_front_in_lookat_frame": native_area > 0,
            "cw_front_compensates_selected_flip": (raw_area > 0) == (native_area < 0),
            "gpu_culling_certified": False}
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/gX_build")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    run = subprocess.run([str(args.binary.resolve()), "--test-only"], cwd=ROOT,
                         env=dict(os.environ, GX_MATH_DUMP="1", GX_PLANAR_GEOMETRY_DUMP="1", GX_NO_SETTINGS_SAVE="1"),
                         capture_output=True, text=True, check=True, timeout=60)
    report = {"result": "pass", "cases": check_dump(run.stdout),
              "limits": "Native float bases and selected positive-w projected triangles; not all rasterized faces, clipping or GPU culling. Normalization accuracy remains external."}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2)+"\n")
    print("REFLECTION_HANDEDNESS: PASS — raw reflected frame negative, rebuilt LookAt positive; selected winding witnesses")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
