#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: trace-oracle for the non-planar analytic revival
#   (--showcase-planar + GX_NONPLANAR_TEST=1 + GX_DIAG_CUBE=1).
#   Mirror-unfold oracle: the planar RT is a photo from the mirrored camera,
#   so sampling it at the plane hit Q along mirrored ray (Cm -> Q) hitting
#   the real cube is EXACTLY equivalent to the real-camera ray (C -> Q)
#   hitting the MIRRORED cube (Q is on the plane, hence invariant).
#   Sphere pixel P shows cube-magenta iff:
#     V = norm(P - C); N = norm(P - S); R = reflect(V, N);
#     Q = P + R*t with Q.y = PLANE_Y (t > 0);
#     ray(C -> Q) hits the mirrored rotated cube (exact, via inverse
#     rotation into cube-local frame + ray_aabb, reusing probe logic).
#   No reflMVP needed on CPU. Gate: predicted vs measured magenta centroid.
#
# Usage:
#   DISPLAY=:1 env GX_NONPLANAR_TEST=1 GX_DIAG_CUBE=1 GX_CAM_YAW=45 \
#     GX_CAM_PITCH=25 GX_CAM_DIST=6 ./build/gX_build --showcase-planar \
#     --ai-screenshot-final
#   python3 scripts/probe_nonplanar_verify.py --trace <png> [--json out.json]

import argparse
import json
import sys

import numpy as np
from PIL import Image

import probe_reflect_verify as P

# fixture constants - keep in sync with scene.c GX_NONPLANAR_TEST block
SPHERE_C = np.array([-1.1, 0.45, 0.7])
SPHERE_R = 0.35  #opencode: sphere.glb spans [-1,1] like cube.glb, scale 0.35

#opencode: zone floors calibrated on the first revival capture (yaw45,
#   pitch25, dist6): planar-zone 0.54, cubemap-zone 0.76 observed.
PASS_PLANAR_FRAC = 0.30
PASS_CUBE_FRAC = 0.50
ZONE_DILATE_PX = 3


def mirrored_cube_hit(o, d, cube_rot=None):
    #opencode: exact ray vs mirrored rotated cube. Mirror the rotated
    #   corners across the plane, or equivalently (used here): mirror the
    #   RAY across the plane, then inverse-rotate into cube-local frame.
    #   Reflection is an involution: ray(C->Q) hits mirrored cube iff
    #   mirrored ray hits the real cube.
    om = o.copy()
    dm = d.copy()
    om[1] = 2.0 * P.PLANE_Y - om[1]
    dm[1] = -dm[1]
    rot = P.CUBE_ROT if cube_rot is None else cube_rot
    cx, sx = np.cos(rot[0]), np.sin(rot[0])
    cy, sy = np.cos(rot[1]), np.sin(rot[1])
    cz, sz = np.cos(rot[2]), np.sin(rot[2])
    rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    R = (rz @ ry @ rx).T  #opencode: active rotation (same convention)
    ol = R.T @ (om - P.CUBE_CENTER)
    dl = R.T @ dm
    lo = np.array([-P.CUBE_HALF] * 3)
    hi = np.array([P.CUBE_HALF] * 3)
    return P.ray_aabb(ol, dl, lo, hi)


def cube_local_frames(pts, dirs):
    #opencode: world -> cube-local (active rotation convention, same as
    #   probe_reflect_verify.rot_xyz): exact ray vs rotated cube via AABB.
    rot = P.CUBE_ROT
    cx, sx = np.cos(rot[0]), np.sin(rot[0])
    cy, sy = np.cos(rot[1]), np.sin(rot[1])
    cz, sz = np.cos(rot[2]), np.sin(rot[2])
    rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    R = (rz @ ry @ rx).T
    lo = np.array([-P.CUBE_HALF] * 3)
    hi = np.array([P.CUBE_HALF] * 3)
    return (pts - P.CUBE_CENTER) @ R, dirs @ R, lo, hi


def predict_cube(eye, target, w, h, n=20000):
    #opencode: cubemap-fallback component - reflected ray from the sphere
    #   surface hits the REAL cube (upper sphere; the dynamic cubemap holds
    #   the whole scene, so this is the direct convex-mirror image).
    i = np.arange(n)
    z = 1.0 - 2.0 * (i + 0.5) / n
    a = np.pi * (3.0 - np.sqrt(5.0)) * i
    r = np.sqrt(np.maximum(0.0, 1.0 - z * z))
    dirs = np.stack([r * np.cos(a), z, r * np.sin(a)], axis=1)
    pts = SPHERE_C[None, :] + SPHERE_R * dirs
    nrm = dirs
    vis = np.einsum("ij,ij->i", nrm, eye[None, :] - pts) > 0
    pts, nrm = pts[vis], nrm[vis]
    v = pts - eye[None, :]
    v /= np.linalg.norm(v, axis=1, keepdims=True)
    rd = v - 2.0 * np.einsum("ij,ij->i", v, nrm)[:, None] * nrm
    ol, dl, lo, hi = cube_local_frames(pts, rd)
    hit = np.array([P.ray_aabb(o, d, lo, hi) for o, d in zip(ol, dl)])
    px = P.project(pts[hit], eye, target, w, h)
    px = px[np.isfinite(px[:, 0]) & np.isfinite(px[:, 1])]
    inside = (px[:, 0] >= 0) & (px[:, 0] < w) & (px[:, 1] >= 0) & (px[:, 1] < h)
    return px[inside]


def predict(eye, target, w, h, n=20000, cube_rot=None):
    #opencode: planar-RT component - fibonacci sphere, front hemisphere
    #   only, plane-hit filter, mirrored-cube test, project survivors.
    #   (Lower sphere: floor-bounce through the global mirror-plane.)
    rng = np.random.default_rng(0)
    i = np.arange(n)
    z = 1.0 - 2.0 * (i + 0.5) / n
    a = np.pi * (3.0 - np.sqrt(5.0)) * i
    r = np.sqrt(np.maximum(0.0, 1.0 - z * z))
    dirs = np.stack([r * np.cos(a), z, r * np.sin(a)], axis=1)
    pts = SPHERE_C[None, :] + SPHERE_R * dirs
    nrm = dirs
    to_cam = eye[None, :] - pts
    vis = np.einsum("ij,ij->i", nrm, to_cam) > 0
    pts, nrm = pts[vis], nrm[vis]
    v = pts - eye[None, :]
    v /= np.linalg.norm(v, axis=1, keepdims=True)
    rd = v - 2.0 * np.einsum("ij,ij->i", v, nrm)[:, None] * nrm
    hdist = P.PLANE_Y - pts[:, 1]
    denom = rd[:, 1]
    #opencode: plane below the fragment, ray heading down (same shape as
    #   the shader: t = hDist/denom guarded positive)
    ok = (hdist * denom > 1e-6)
    t = np.zeros(len(pts))
    t[ok] = hdist[ok] / denom[ok]
    ok = ok & (t > 0)
    pts, rd = pts[ok], rd[ok]
    q = pts + rd * t[ok][:, None]
    hit = np.array([mirrored_cube_hit(eye, qq - eye, cube_rot) for qq in q])
    pred_pts = pts[hit]
    px = P.project(pred_pts, eye, target, w, h)
    px = px[np.isfinite(px[:, 0]) & np.isfinite(px[:, 1])]
    inside = (px[:, 0] >= 0) & (px[:, 0] < w) & (px[:, 1] >= 0) & (px[:, 1] < h)
    return px[inside]


def zone_mask(pred, w, h, dilate=ZONE_DILATE_PX):
    #opencode: predicted sample points -> boolean zone (dilated discs).
    from scipy import ndimage
    m = np.zeros((h, w), dtype=bool)
    if len(pred) == 0:
        return m
    xi = np.clip(pred[:, 0].astype(int), 0, w - 1)
    yi = np.clip(pred[:, 1].astype(int), 0, h - 1)
    m[yi, xi] = True
    if dilate > 0:
        m = ndimage.binary_dilation(m, iterations=dilate)
    return m


def verify(png_path, yaw=45.0, pitch=25.0, dist=6.0, cube_rot=None):
    img = np.asarray(Image.open(png_path).convert("RGB"))
    h, w = img.shape[:2]
    eye, target = P.orbit_camera(yaw, pitch, dist)
    pred_planar = predict(eye, target, w, h, cube_rot=cube_rot)
    pred_cube = predict_cube(eye, target, w, h)
    mag = P.magenta_mask(img)
    sc = P.project(SPHERE_C[None, :], eye, target, w, h)[0]
    edge = P.project((SPHERE_C + np.array([SPHERE_R, 0, 0]))[None, :],
                     eye, target, w, h)[0]
    radius = abs(edge[0] - sc[0])
    report = {
        "capture": png_path,
        "camera": {"yaw": yaw, "pitch": pitch, "dist": dist},
        "sphere_px": [float(sc[0]), float(sc[1])],
        "sphere_radius_px": float(radius),
        "pred_planar_count": int(len(pred_planar)),
        "pred_cube_count": int(len(pred_cube)),
    }
    if len(pred_planar) == 0 or len(pred_cube) == 0:
        report.update({"verdict": "FAIL",
                       "reason": "oracle predicts no magenta in a component"})
        return report, None
    z_planar = zone_mask(pred_planar, w, h)
    z_cube = zone_mask(pred_cube, w, h) & ~z_planar
    f_planar = float(mag[z_planar].mean()) if z_planar.any() else 0.0
    f_cube = float(mag[z_cube].mean()) if z_cube.any() else 0.0
    report["planar_zone_frac"] = f_planar
    report["cube_zone_frac"] = f_cube
    ok = f_planar >= PASS_PLANAR_FRAC and f_cube >= PASS_CUBE_FRAC
    report["verdict"] = "PASS" if ok else "FAIL"
    return report, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace", required=True)
    ap.add_argument("--yaw", type=float, default=45.0)
    ap.add_argument("--pitch", type=float, default=25.0)
    ap.add_argument("--dist", type=float, default=6.0)
    ap.add_argument("--cube-rot", default="default")
    ap.add_argument("--json", default=None)
    a = ap.parse_args()
    sys.path.insert(0, "scripts")
    report, _ = verify(a.trace, a.yaw, a.pitch, a.dist,
                       P.parse_cube_rot(a.cube_rot))
    print(json.dumps(report, indent=1))
    if a.json:
        with open(a.json, "w") as f:
            json.dump(report, f, indent=1)
    sys.exit(0 if report["verdict"] == "PASS" else 1)


if __name__ == "__main__":
    main()
