#!/usr/bin/env python3
#ai: exact clipping oracle over native float clip coordinates; not a GPU clipper/raster certificate.
import argparse
from fractions import Fraction as Q
import json
import math
import os
from pathlib import Path
import subprocess

from planar_geometry_check import ROOT, close
from reflection_handedness_check import area
from reprojection_residual_check import require, row


def determinant(a, b, c):
    return a[0]*(b[1]*c[3]-b[3]*c[1])-a[1]*(b[0]*c[3]-b[3]*c[0])+a[3]*(b[0]*c[1]-b[1]*c[0])


def lerp(a, b, t):
    return [(1-t)*x+t*y for x, y in zip(a, b)]


def projected_area(a, b, c):
    require(all(p[3] != 0 for p in (a,b,c)), "zero-w division not allowed")
    return area([[p[0]/p[3],p[1]/p[3]] for p in (a,b,c)])


def near(p):
    return p[2]+p[3]


def clip_polygon(points, distance):
    if not points:
        return []
    output = []
    a = points[-1]
    for b in points:
        da, db = distance(a), distance(b)
        if (da < 0) != (db < 0):
            t = -da/(db-da)
            require(0 <= t <= 1, "clipping edge parameter out of range")
            vertex = lerp(a,b,t)
            require(distance(vertex) == 0, "intersection not on clip boundary")
            output.append(vertex)
        if db >= 0:
            output.append(b)
        a = b
    return output


PLANES = [near, lambda p:p[3]-p[2], lambda p:p[3]+p[0], lambda p:p[3]-p[0],
          lambda p:p[3]+p[1], lambda p:p[3]-p[1]]


def check_dump(text):
    cases = {}
    for line in text.splitlines():
        if not line.startswith("PROJECTION_CLIP "):
            continue
        _, name, *fields = line.split()
        require(name in {"positive","cross_near","cross_camera"} and name not in cases, "bad fixture name")
        require(len(fields) == 40, "bad native field count")
        f = list(map(float.fromhex, fields))
        require(all(math.isfinite(x) for x in f), "nonfinite native clip")
        q = list(map(Q.from_float, f))
        matrix = q[:16]
        vertices = [q[i:i+4] for i in range(16,28,4)]
        clips = [q[i:i+4] for i in range(28,40,4)]
        for v,c in zip(vertices,clips):
            close(c,row(v,matrix))
        d = determinant(*clips)
        require(d != 0, "degenerate source triangle")
        original_area = projected_area(*clips)
        require(original_area == d/(clips[0][3]*clips[1][3]*clips[2][3]), "perspective-area identity failed")
        if name == "cross_camera":
            require(clips[0][3] < 0 and original_area*d < 0, "camera-plane counterexample not exercised")
        else:
            require(all(c[3] > 0 for c in clips) and original_area*d > 0, "positive-w sign relation failed")
        clipped = clip_polygon(clips,near)
        require(len(clipped) == (3 if name == "positive" else 4), "near clip topology mismatch")
        if name != "positive":
            a,b,c = clips
            require(near(a)<0 and near(b)>0 and near(c)>0, "one-outside premise missing")
            s,t = -near(a)/(near(b)-near(a)), -near(a)/(near(c)-near(a))
            ab,ac = lerp(a,b,s),lerp(a,c,t)
            require(determinant(ab,b,c) == (1-s)*d, "first clipped fan identity failed")
            require(determinant(ab,c,ac) == s*(1-t)*d, "second clipped fan identity failed")
            require(0<s<1 and 0<t<1, "strict fan parameters failed")
        scale,bias = 1-matrix[10],-matrix[14]
        require(scale>0 and bias>0, "standard perspective near mapping unavailable")
        ez = max(abs(c[2]+c[3]-(scale*c[3]-bias)) for c in clips)
        margin = (bias-ez)/scale
        require(margin>0 and all(p[3]>=margin for p in clipped), "perturbed near-clip w margin failed")
        near_areas = [projected_area(clipped[0],clipped[i],clipped[i+1]) for i in range(1,len(clipped)-1)]
        require(all(a*d>0 for a in near_areas), "near-clipped winding inconsistent")
        full = clips
        for plane in PLANES:
            full = clip_polygon(full,plane)
        require(len(full)>=3 and all(p[3]>0 and all(plane(p)>=0 for plane in PLANES) for p in full), "canonical clipped polygon invalid")
        full_areas = [projected_area(full[0],full[i],full[i+1]) for i in range(1,len(full)-1)]
        require(any(a*d>0 for a in full_areas) and all(a*d>=0 for a in full_areas), "canonical polygon winding inconsistent")
        cases[name] = {"homogeneous_orientation":float(d),"unclipped_divided_area":float(original_area),
            "original_w": [float(c[3]) for c in clips],"near_clipped_vertices":len(clipped),
            "near_clipped_area":list(map(float,near_areas)),"minimum_safe_w":float(margin),
            "measured_clip_z_error":float(ez),"canonical_clipped_vertices":len(full),
            "canonical_clipped_area":list(map(float,full_areas)),"gpu_clipping_certified":False}
    require(set(cases)=={"positive","cross_near","cross_camera"},"missing fixtures")
    return cases


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary",type=Path,default=ROOT/"build/gX_build")
    parser.add_argument("--report",type=Path)
    args=parser.parse_args()
    run=subprocess.run([str(args.binary.resolve()),"--test-only"],cwd=ROOT,
        env=dict(os.environ,GX_MATH_DUMP="1",GX_PROJECTION_CLIP_DUMP="1",GX_NO_SETTINGS_SAVE="1"),
        capture_output=True,text=True,check=True,timeout=60)
    report={"result":"pass","cases":check_dump(run.stdout),
        "limits":"Exact oracle on native stored-float clips; near-plane identities proved, six-plane algorithm checked on fixtures only; not GPU clipping/raster/IEEE certification."}
    if args.report:
        args.report.write_text(json.dumps(report,indent=2)+"\n")
    print("PROJECTION_CLIPPING: PASS — positive/near/camera-crossing fixtures, clipping before divide")
    print(json.dumps(report,indent=2))


if __name__=="__main__":
    main()
