#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: v0.1.24 — reflection verification harness for --showcase-minimal.
#   The scene contains exactly two models with known geometry:
#     plane slab : 8 x 0.15 x 8 at pos (0,-0.075,0)   -> top surface y=0
#     cube       : unit cube scaled 0.5 at (0.35,1,-0.25), rot (10,30,5)
#   The harness reconstructs the orbit camera exactly like gX_Showcase_Update
#   (FOV 90 deg) and projects analytic ground truth into screen space: the cube
#   silhouette hull and its MIRROR IMAGE across the plane (the only legal
#   reflection). Verification is diff-based against two reference captures of
#   the SAME camera angle:
#     baseline (--gfx-safe-mode) : identical scene, all reflections off
#     oracle    (--showcase-planar): mathematically exact planar mirror
#   Diffs:
#     d_add = |ssr - baseline|   -> pure reflection signal (geometry cancels)
#     d_orc = |oracle - baseline|-> exact-mirror footprint (calibrates ROI)
#     d_err = |ssr - oracle|     -> SSR deviation from the exact mirror
#   Checks per capture triple:
#     CALIBRATED   oracle mass concentrated inside predicted mirrored hull
#     EXISTS       reflection energy present in the mirrored hull
#     EXACTLY_ONE  every significant added-signal component overlaps the hull
#     NO_PHANTOMS  no comparable component outside both hulls
#     PLANAR_MATCH SSR reflection close to the exact planar mirror
#
# Usage:
#   python3 scripts/probe_reflect_verify.py --ssr f.png --baseline b.png \
#       --oracle p.png [--yaw 45 --pitch 25 --dist 6] [--json out.json]
#   python3 scripts/probe_reflect_verify.py --diff ssr.png planar.png

import argparse
import json
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

DEG2RAD = np.pi / 180.0

# scene constants — keep in sync with src/renderer/gfx/scene.c minimal branch
CUBE_CENTER = np.array([0.35, 1.0, -0.25])
CUBE_HALF = 0.5          #opencode: cube.glb mesh spans [-1,1], scale 0.5
PLANE_Y = 0.075          #opencode: floor slab TOP surface (mirror plane)
CUBE_ROT = np.array([10.0, 30.0, 5.0]) * DEG2RAD
CAM_FOVY = 45.0          #opencode: settings.json cameraFov
RENDER_W = 1536
RENDER_H = 1152


def parse_cube_rot(value):
    #opencode: match the three deterministic GX_CUBE_ROT diagnostic hooks
    #   without changing the default rotated-cube oracle.
    choices = {
        "default": CUBE_ROT,
        "0": np.array([0.0, 0.0, 0.0]),
        "y": np.array([0.0, 30.0, 0.0]) * DEG2RAD,
        "z": np.array([0.0, 0.0, 30.0]) * DEG2RAD,
    }
    key = str(value).lower()
    if key not in choices:
        raise ValueError(f"unsupported cube rotation '{value}'")
    return choices[key]


def look_at(eye, target, up):
    f = target - eye
    f = f / np.linalg.norm(f)
    s = np.cross(f, up)
    s = s / np.linalg.norm(s)
    u = np.cross(s, f)
    m = np.eye(4)
    m[0, :3], m[0, 3] = s, -np.dot(s, eye)
    m[1, :3], m[1, 3] = u, -np.dot(u, eye)
    m[2, :3], m[2, 3] = -f, np.dot(f, eye)
    return m


def perspective(fovy_deg, aspect, near=0.01, far=1000.0):
    t = 1.0 / np.tan(fovy_deg * DEG2RAD / 2.0)
    m = np.zeros((4, 4))
    m[0, 0] = t / aspect
    m[1, 1] = t
    m[2, 2] = -(far + near) / (far - near)
    m[2, 3] = -2.0 * far * near / (far - near)
    m[3, 2] = -1.0
    return m


def orbit_camera(yaw_deg, pitch_deg, dist):
    # gX_Showcase_Update evaluates SceneCenter on frame 1 before model AABBs
    # exist -> falls back to the world origin; the runtime camera therefore
    # orbits (0,0,0) (verified via trace).
    target = np.zeros(3)
    yaw, pitch = yaw_deg * DEG2RAD, pitch_deg * DEG2RAD
    eye = target + dist * np.array([
        np.cos(pitch) * np.sin(yaw),
        np.sin(pitch),
        np.cos(pitch) * np.cos(yaw)])
    return eye, target


def rot_xyz(v, rot):
    #opencode: v0.1.35 — gxmath RotateXYZ effective convention FIXED.
    #   gxmath stores the Rz*Ry*Rx layout row-major and transforms with
    #   ROW vectors (p @ M = M^T acting column-style), so the ACTIVE
    #   rotation is the TRANSPOSE: angles negate in effect. Verified
    #   against engine captures: yaw-only (0,30,0) matches -30 std within
    #   1px; zero-rotation matches exactly. The old +angle X-first order
    #   mispredicted silhouettes by up to 57px (the "wedge" ghost).
    cx, sx = np.cos(rot[0]), np.sin(rot[0])
    cy, sy = np.cos(rot[1]), np.sin(rot[1])
    cz, sz = np.cos(rot[2]), np.sin(rot[2])
    rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return (rz @ ry @ rx).T @ v


def project(points_world, eye, target, w=RENDER_W, h=RENDER_H):
    view = look_at(eye, target, np.array([0.0, 1.0, 0.0]))
    proj = perspective(CAM_FOVY, w / h)
    vp = proj @ view
    pts = np.asarray(points_world, dtype=float)
    ph = np.hstack([pts, np.ones((len(pts), 1))])
    clip = ph @ vp.T
    wc = clip[:, 3]
    valid = np.abs(wc) > 1e-6
    ndc = np.zeros((len(pts), 3))
    ndc[valid] = clip[valid, :3] / wc[valid, None]
    px = np.empty((len(pts), 2))
    px[:, 0] = (ndc[:, 0] * 0.5 + 0.5) * w
    px[:, 1] = (1.0 - (ndc[:, 1] * 0.5 + 0.5)) * h
    px[~valid] = np.nan
    return px


def cube_hull_corners(cube_rot=None):
    active_rot = CUBE_ROT if cube_rot is None else cube_rot
    corners = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            for sz in (-1, 1):
                local = rot_xyz(np.array([sx, sy, sz]) * CUBE_HALF, active_rot)
                corners.append(CUBE_CENTER + local)
    mirror = [np.array([c[0], 2 * PLANE_Y - c[1], c[2]]) for c in corners]
    return np.array(corners), np.array(mirror)


def hull_bbox(px, margin_frac=0.18):
    x0, y0 = np.nanmin(px, axis=0)
    x1, y1 = np.nanmax(px, axis=0)
    mx, my = (x1 - x0) * margin_frac, (y1 - y0) * margin_frac
    return np.array([x0 - mx, y0 - my, x1 + mx, y1 + my])


def horizon_row(eye, target, w=RENDER_W, h=RENDER_H):
    """Screen row where the infinite mirror plane vanishes."""
    fwd = np.array([target[0] - eye[0], 0.0, target[2] - eye[2]])
    n = np.linalg.norm(fwd)
    if n < 1e-5:
        return int(h * 0.5)
    fwd /= n
    far = np.array([eye[0] + fwd[0] * 2000.0, PLANE_Y, eye[2] + fwd[2] * 2000.0])
    row = project([far], eye, target, w, h)[0, 1]
    if np.isnan(row):
        return int(h * 0.5)
    return float(row)


def bbox_on_screen(bb, w=RENDER_W, h=RENDER_H, pad=8.0):
    return not (bb[2] < pad or bb[0] > w - pad or bb[3] < pad or bb[1] > h - pad)


def crop(img, bb):
    x0, y0, x1, y1 = [int(round(v)) for v in bb]
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(img.shape[1], x1), min(img.shape[0], y1)
    if x1 <= x0 or y1 <= y0:
        return None
    return img[y0:y1, x0:x1]


def region_mask(shape, bb):
    m = np.zeros(shape[:2], dtype=bool)
    x0, y0, x1, y1 = [int(round(v)) for v in bb]
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(shape[1], x1), min(shape[0], y1)
    if x1 > x0 and y1 > y0:
        m[y0:y1, x0:x1] = True
    return m


#opencode: v0.1.35 — luminance-aware magenta detection. Fresnel/edgeFade
#   dim the reflection multiplicatively: hue is preserved while absolute
#   levels drop, so fixed thresholds (r>120,g<110) under-counted dimmed
#   reflections to ~0%. Ratio test + low floor instead.
MAG_MIN = 60
MAG_G_RATIO = 0.72


#opencode: screenshot PNGs carry the output transfer encode; evaluate hue
#   and thresholds in linear light so the oracle survives display-transfer
#   changes without changing its calibrated gate values.
def magenta_mask(img):
    encoded = np.clip(img.astype(float) / 255.0, 0.0, 1.0)
    linear = np.power(encoded, 2.2) * 255.0
    r = linear[..., 0]
    g = linear[..., 1]
    b = linear[..., 2]
    mx = np.maximum(r, b)
    return (r > MAG_MIN) & (b > MAG_MIN) & (g < MAG_G_RATIO * mx)


def hull_zone_mask(px, w, h):
    """Exact mirrored-cube silhouette zone: convex hull of projected corners."""
    from scipy.spatial import ConvexHull
    pts = [(float(p[0]), float(p[1])) for p in px if np.isfinite(p[0]) and np.isfinite(p[1])]
    if len(pts) < 3:
        return np.zeros((h, w), dtype=bool)
    hull = ConvexHull(pts)
    poly = [pts[i] for i in hull.vertices]
    from PIL import ImageDraw
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).polygon(poly, outline=1, fill=1)
    return np.asarray(img, dtype=bool)


def verify(ssr_path, base_path=None, oracle_path=None, yaw=45.0, pitch=25.0,
           dist=6.0, diag=True, cube_rot=None):
    """Verify the SSR reflection of the diagnostic cube.

    With GX_DIAG_CUBE the cube renders flat magenta, so its mirror image is
    an unambiguous signature: exactly ONE magenta cluster may exist below the
    horizon, and its centroid must match the analytically projected mirror
    position. Any other magenta cluster is a phantom reflection.
    """
    ssr = np.asarray(Image.open(ssr_path).convert("RGB"), dtype=float)
    eye, target = orbit_camera(yaw, pitch, dist)
    corners, mirror = cube_hull_corners(cube_rot)
    cube_px = project(corners, eye, target)
    refl_px = project(mirror, eye, target)
    cube_bb = hull_bbox(cube_px)
    refl_bb = hull_bbox(refl_px)

    report = {
        "capture": ssr_path,
        "camera": {"yaw": yaw, "pitch": pitch, "dist": dist},
        "cube_bbox": [round(v, 1) for v in cube_bb],
        "refl_bbox": [round(v, 1) for v in refl_bb],
        "mode": "magenta" if diag else "diff",
    }

    if diag:
        mag = magenta_mask(ssr)
        labels, n = ndimage.label(mag)
        #opencode: classify each magenta cluster by WHERE ITS CENTROID lands
        #   relative to the two analytically projected hulls: inside the cube
        #   hull -> the source object; inside the MIRRORED hull -> its legal
        #   reflection; anywhere else -> phantom. A horizon-row split cannot
        #   work here: a floating cube physically straddles the horizon from
        #   low camera pitches.
        tol = 40.0
        clusters = []
        for i in range(1, n + 1):
            ys, xs = np.where(labels == i)
            if len(ys) < 200:
                continue
            cy, cx = float(ys.mean()), float(xs.mean())

            def _inside(bb):
                return bool(bb[0] - tol <= cx <= bb[2] + tol and
                            bb[1] - tol <= cy <= bb[3] + tol)
            in_cube = _inside(cube_bb)
            kind = "object" if in_cube else (
                "reflection" if _inside(refl_bb) else "phantom")
            clusters.append({
                "area": int(len(ys)),
                "centroid": [round(cx), round(cy)],
                "kind": kind,
            })
        clusters.sort(key=lambda c: -c["area"])
        report["clusters"] = clusters

        refl_on_screen = bbox_on_screen(refl_bb)
        report["refl_on_screen"] = bool(refl_on_screen)

        #opencode: degenerate geometry — looking straight down, the mirror
        #   image hides almost entirely BEHIND the object itself; the hulls
        #   then coincide and no separate reflection cluster can exist.
        #   Treat heavily-occluded configurations as not verifiable.
        def _overlap(a, b):
            ix0 = max(a[0], b[0]); iy0 = max(a[1], b[1])
            ix1 = min(a[2], b[2]); iy1 = min(a[3], b[3])
            iw, ih = max(0.0, ix1 - ix0), max(0.0, iy1 - iy0)
            inter = iw * ih
            smaller = min((a[2] - a[0]) * (a[3] - a[1]),
                          (b[2] - b[0]) * (b[3] - b[1]))
            return inter / max(smaller, 1e-6)
        degenerate = _overlap(cube_bb, refl_bb) > 0.55
        report["hulls_overlap_frac"] = round(_overlap(cube_bb, refl_bb), 3)

        if not refl_on_screen or degenerate:
            checks = {"EXISTS": None, "EXACTLY_ONE": None,
                      "NO_PHANTOMS": None, "POSITION": None,
                      "N_PHANTOMS": 0, "degenerate_or_offscreen": True}
            passed = True
        else:
            refl_clusters = [c for c in clusters if c["kind"] == "reflection"]
            phantoms = [c for c in clusters if c["kind"] == "phantom"]
            exists = bool(refl_clusters and
                          refl_clusters[0]["area"] > 800)
            exactly_one = bool(len(refl_clusters) == 1)
            no_phantoms = bool(len(phantoms) == 0)
            # position check: centroid inside predicted mirrored hull
            position_ok = None
            if refl_clusters:
                cx, cy = refl_clusters[0]["centroid"]
                position_ok = bool(
                    refl_bb[0] - 40 <= cx <= refl_bb[2] + 40 and
                    refl_bb[1] - 40 <= cy <= refl_bb[3] + 40)
            checks = {"EXISTS": exists, "EXACTLY_ONE": exactly_one,
                      "NO_PHANTOMS": no_phantoms,
                      "POSITION": position_ok,
                      "N_PHANTOMS": len(phantoms)}
            passed = bool(exists and exactly_one and no_phantoms and
                          (position_ok is not False))
        report["checks"] = checks
        report["PASS"] = passed
        return report

    # diff mode fallback (no diagnostic cube): baseline/oracle pixel-diffs
    base = np.asarray(Image.open(base_path).convert("RGB"), dtype=float)
    d_add = np.abs(ssr - base).mean(axis=2)
    below = region_mask(ssr.shape, [0, RENDER_H // 2, RENDER_W, RENDER_H])

    def mass(bb):
        m = region_mask(d_add.shape, bb) & below
        return float(d_add[m].sum())

    add_in = mass(refl_bb)
    add_total = float(d_add[below].sum())
    conc = add_in / add_total if add_total > 1e-6 else 0.0
    rm = region_mask(d_add.shape, refl_bb) & below
    err_roi = None
    if oracle_path:
        oracle = np.asarray(Image.open(oracle_path).convert("RGB"), dtype=float)
        d_err = np.abs(ssr - oracle).mean(axis=2)
        err_roi = round(float(d_err[rm].mean()), 3)
    report.update({
        "added_mass_concentration": round(conc, 3),
        "oracle_err_mean": err_roi,
        "checks": {"EXISTS": bool(add_in > 20000.0),
                   "EXACTLY_ONE": bool(conc >= 0.65),
                   "NO_PHANTOMS": bool(conc >= 0.65)},
        "PASS": bool(add_in > 20000.0 and conc >= 0.65),
    })
    return report

def diff(ssr_path, planar_path):
    a = np.asarray(Image.open(ssr_path).convert("RGB"), dtype=float)
    b = np.asarray(Image.open(planar_path).convert("RGB"), dtype=float)
    if a.shape != b.shape:
        return {"error": "size mismatch", "PASS": False}
    d = np.abs(a - b).mean(axis=2)
    eye, target = orbit_camera(45.0, 25.0, 6.0)
    _, mc = cube_hull_corners()
    rb = hull_bbox(project(mc, eye, target))
    roi = crop(d, rb)
    return {
        "ssr_vs_planar_reflROI_mean": round(float(roi.mean()), 3),
        "ssr_vs_planar_reflROI_max": round(float(roi.max()), 1),
        "global_mean": round(float(d.mean()), 3),
    }


# ---------------------------------------------------------------------------
#opencode: v0.1.28 — --trace: flipped-cube fidelity oracle.
#   For every floor pixel, reflect the view ray off the mirror plane and
#   classify what a perfect planar mirror would show there:
#     CUBE  — the reflected ray hits the probe cube's world AABB
#     SKY   — the ray escapes to sky inside the frame (env-fallback zone)
#     OUT   — the ray leaves the frame first (env fallback, harder fade)
#   With GX_DIAG_CUBE the cube renders flat magenta, so every CUBE pixel in
#   the capture must BE magenta; anything else is a counted failure with a
#   location. Output: annotated overlay PNG + per-zone stats + failure map.
# ---------------------------------------------------------------------------

def ray_aabb(o, d, lo, hi, tmax=4000.0):
    t0, t1 = 0.0, tmax
    for i in range(3):
        if abs(d[i]) < 1e-9:
            if o[i] < lo[i] or o[i] > hi[i]:
                return False
            continue
        ta = (lo[i] - o[i]) / d[i]
        tb = (hi[i] - o[i]) / d[i]
        if ta > tb:
            ta, tb = tb, ta
        t0, t1 = max(t0, ta), min(t1, tb)
        if t0 > t1:
            return False
    return True


def unproject_to_plane(px, py, eye, target, w=RENDER_W, h=RENDER_H, plane_h=None):
    """Screen pixel -> world point on the mirror plane (or None)."""
    ph = PLANE_Y if plane_h is None else plane_h
    view = look_at(eye, target, np.array([0.0, 1.0, 0.0]))
    proj = perspective(CAM_FOVY, w / h)
    inv = np.linalg.inv(proj @ view)
    ndc = np.array([(px / w) * 2 - 1, 1 - (py / h) * 2, 0.5, 1.0])
    p0 = inv @ ndc          #opencode: nearer depth sample (ndc z +0.5)
    p0 /= p0[3]
    ndc[2] = -0.5
    p1 = inv @ ndc          #opencode: farther depth sample
    p1 /= p1[3]
    d = p0[:3] - p1[:3]     #opencode: forward = from far toward near... verified: A(z+.5)-B(z-.5) matches view dir
    d /= np.linalg.norm(d)
    if abs(d[1]) < 1e-7:
        return None, None
    t = (ph - eye[1]) / d[1]
    if t <= 0:
        return None, None
    return eye + d * t, d


def trace(capture, yaw, pitch, dist, out_prefix, plane_y=None, cube_rot=None):
    img = np.asarray(Image.open(capture).convert("RGB"), dtype=float)
    H, W = img.shape[:2]
    eye, target = orbit_camera(yaw, pitch, dist)
    active_rot = CUBE_ROT if cube_rot is None else cube_rot
    corners, _mirror0 = cube_hull_corners(cube_rot)
    if plane_y is not None:
        mirror = [np.array([c[0], 2 * plane_y - c[1], c[2]]) for c in corners]
        mirror = np.array(mirror)
    else:
        mirror = _mirror0
    cube_px = project(corners, eye, target)
    refl_px = project(mirror, eye, target)

    # world AABB of the rotated cube (+small margin for emissive bleed)
    lo = corners.min(axis=0) - 0.05
    hi = corners.max(axis=0) + 0.05
    ph_used = PLANE_Y if plane_y is None else plane_y

    #opencode: vectorized expectation classification over a coarse pixel grid
    step = 4
    expect = np.zeros((H, W), dtype=np.uint8)   # 0=non-floor 1=CUBE 2=SKY_IN 3=OUT
    gys = np.arange(int(H * 0.30), H, step)
    gxs = np.arange(0, W, step)
    gx, gy = np.meshgrid(gxs, gys)
    fx = (gx.ravel() / W) * 2 - 1
    fy = 1 - (gy.ravel() / H) * 2
    view = look_at(eye, target, np.array([0.0, 1.0, 0.0]))
    proj = perspective(CAM_FOVY, W / H)
    inv = np.linalg.inv(proj @ view)

    def unproj_batch(fxv, fyv, z):
        ndc = np.stack([fxv, fyv, np.full_like(fxv, z), np.ones_like(fxv)], axis=1)
        p = ndc @ inv.T
        return p[:, :3] / p[:, 3:4]

    p_near = unproj_batch(fx, fy, 0.5)
    p_far = unproj_batch(fx, fy, -0.5)
    dirs = p_near - p_far                       # forward view rays
    dirs /= np.linalg.norm(dirs, axis=1, keepdims=True)
    ok_down = dirs[:, 1] < -1e-7
    tt = (ph_used - eye[1]) / np.where(ok_down, dirs[:, 1], -1e-7)
    valid = ok_down & (tt > 0)
    P = eye[None, :] + dirs * tt[:, None]       # floor intersection points
    rd = dirs.copy()
    rd[:, 1] = -rd[:, 1]                        # mirrored directions
    rd /= np.linalg.norm(rd, axis=1, keepdims=True)

    # CUBE zone: slab test against cube AABB (vectorized)
    inv_d = 1.0 / np.where(np.abs(rd) < 1e-9, 1e-9, rd)
    ta = (lo[None, :] - P) * inv_d
    tb = (hi[None, :] - P) * inv_d
    tmin = np.max(np.minimum(ta, tb), axis=1)
    tmax = np.min(np.maximum(ta, tb), axis=1)
    hits_cube = valid & (tmax >= np.maximum(tmin, 0.0))

    # OUT vs SKY_IN: sample reflected rays at several depths; if every
    # sampled projection leaves the frame, treat as exited
    any_inside = np.zeros(len(P), dtype=bool)
    vp = proj @ view
    for t_samp in (1.0, 4.0, 16.0, 64.0, 256.0):
        q = P + rd * t_samp
        ph4 = np.hstack([q, np.ones((len(q), 1))])
        clip = ph4 @ vp.T
        wc = clip[:, 3]
        good = np.abs(wc) > 1e-6
        safe_wc = np.where(good, wc, 1.0)
        nx = np.where(good, clip[:, 0] / safe_wc, 0.0)
        ny = np.where(good, clip[:, 1] / safe_wc, 0.0)
        any_inside |= good & (nx > -1.05) & (nx < 1.05) & (ny > -1.05) & (ny < 1.05)

    cls = np.zeros(len(P), dtype=np.uint8)
    cls[valid & ~hits_cube & any_inside] = 2
    cls[valid & ~hits_cube & ~any_inside] = 3
    cls[hits_cube] = 1
    expect = np.zeros((H, W), dtype=np.uint8)
    expect[gy.ravel(), gx.ravel()] = cls

    #opencode: v0.1.35 — EXACT CUBE zone: the AABB slab test + coarse grid
    #   inflates the predicted region well past the true mirrored silhouette
    #   (a perfect reflection could never fill it — measured as the v0.1.34
    #   "failure ring"). Replace with the convex hull of the projected
    #   mirrored corners at full resolution.
    from PIL import ImageDraw  # noqa: F401  (used by hull_zone_mask)
    cube_exact = hull_zone_mask(refl_px, W, H)
    expect[(expect == 1) & ~cube_exact] = 0
    expect[cube_exact] = 1

    # magenta detection at full res
    mag = magenta_mask(img)

    zone_stats = {}
    fail_map = np.zeros((H, W, 3), dtype=np.uint8)
    fail_map[..., 0] = 20; fail_map[..., 1] = 20; fail_map[..., 2] = 20
    for zid, zname in [(1, "CUBE"), (2, "SKY_IN"), (3, "SKY_OUT")]:
        zm = expect == zid
        n_zone = int(zm.sum())
        if zid == 1 and n_zone:
            ok = int((zm & mag).sum())
            pct = 100.0 * ok / n_zone
            zone_stats[zname] = {"pixels": n_zone, "correct": ok,
                                 "fidelity_pct": round(pct, 2)}
            # paint failure pixels red on the failure map
            fl = zm & ~mag
            fail_map[fl] = [255, 40, 40]
            fail_map[zm & mag] = [30, 160, 60]
        else:
            zone_stats[zname] = {"pixels": n_zone}
            if n_zone:
                fail_map[zm] = [60, 70, 110] if zid == 2 else [90, 90, 30]

    # annotated overlay: wireframes + expectation tint
    from PIL import ImageDraw
    ann = Image.open(capture).convert("RGB")
    dr = ImageDraw.Draw(ann)

    def draw_hull(px_pts, color, dash=False):
        pts = [(float(p[0]), float(p[1])) for p in px_pts]
        idx_edges = [(0,1),(1,3),(3,2),(2,0),(4,5),(5,7),(7,6),(6,4),(0,4),(1,5),(2,6),(3,7)]
        # corner order from cube_hull_corners: (sx,sy,sz) loops z fastest
        order = []
        for sx in (-1,1):
            for sy in (-1,1):
                for sz in (-1,1):
                    pass
        # rebuild matching order:
        cs = []
        for sx in (-1,1):
            for sy in (-1,1):
                for sz in (-1,1):
                    local = rot_xyz(np.array([sx,sy,sz])*CUBE_HALF, active_rot)
                    cs.append(tuple(np.round(local,6)))
        index_of = {c:i for i,c in enumerate(cs)}
        edges = []
        for i,a in enumerate(cs):
            for j,b2 in enumerate(cs):
                if j<=i: continue
                diff = sum(1 for k in range(3) if abs(a[k]-b2[k])>1e-5)
                if diff==1: edges.append((i,j))
        for i,j in edges:
            if not (np.isfinite(pts[i][0]) and np.isfinite(pts[j][0])): continue
            dr.line([pts[i], pts[j]], fill=color, width=3)

    draw_hull(cube_px, (255,255,255))
    draw_hull(refl_px, (0,220,255))
    Image.fromarray(fail_map).save(out_prefix + "_failmap.png")

    report = {
        "capture": capture,
        "camera": {"yaw": yaw, "pitch": pitch, "dist": dist},
        "zones": zone_stats,
        "cube_fidelity_pct": zone_stats.get("CUBE", {}).get("fidelity_pct"),
    }
    return report, ann


def main():
    ap = argparse.ArgumentParser(description="gX reflection verification harness")
    ap.add_argument("--ssr", help="SSR-mode capture to verify")
    ap.add_argument("--baseline", help="same-angle capture with reflections off")
    ap.add_argument("--oracle", help="same-angle exact planar-reflection capture")
    ap.add_argument("--yaw", type=float, default=45.0)
    ap.add_argument("--pitch", type=float, default=25.0)
    ap.add_argument("--dist", type=float, default=6.0)
    ap.add_argument("--cube-rot", default="default",
                    choices=("default", "0", "Y", "Z"))
    ap.add_argument("--no-diag", action="store_true",
                    help="pixel-diff mode instead of magenta-signature mode")
    ap.add_argument("--diff", nargs=2, metavar=("SSR", "PLANAR"))
    ap.add_argument("--trace", metavar="CAPTURE", help="flipped-cube fidelity trace")
    ap.add_argument("--out", default="/tmp/trace_out", help="output prefix for trace artifacts")
    ap.add_argument("--plane-y", type=float, default=None,
                    help="override mirror-plane height for the trace sweep")
    ap.add_argument("--json", help="write report JSON here")
    args = ap.parse_args()

    if args.trace:
        report, ann = trace(args.trace, args.yaw, args.pitch, args.dist,
                            args.out, plane_y=args.plane_y,
                            cube_rot=parse_cube_rot(args.cube_rot))
        ann.save(args.out + "_overlay.png")
        print("saved overlay:", args.out + "_overlay.png")
    elif args.diff:
        report = diff(*args.diff)
    elif args.ssr:
        report = verify(args.ssr, args.baseline, args.oracle,
                        args.yaw, args.pitch, args.dist, diag=not args.no_diag,
                        cube_rot=parse_cube_rot(args.cube_rot))
    else:
        ap.error("need --diff OR --ssr")

    text = json.dumps(report, indent=2)
    print(text)
    if args.json:
        with open(args.json, "w") as fh:
            fh.write(text)
    if "PASS" in report:
        sys.exit(0 if report["PASS"] else 1)


if __name__ == "__main__":
    main()
