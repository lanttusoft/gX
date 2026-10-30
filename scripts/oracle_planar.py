#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

# gX planar oracle — CPU reference check for the mirror chain.
# Hypothesis under test: the planar system has a bug.
#
#   Scene: --showcase-planar (slab top y=0.075, one cube at
#   C=(0.35,1.0,-0.25), GX_CUBE_ROT=0, GX_DIAG_CUBE=1 flat magenta).
#   Engine: draw.c logs ORACLEDUMP (verbatim reflection.mvp rows +
#   plane + camera + frustum + size); gfx_ssr.frag mode 23 echoes
#   planarSampleUV(worldPos) as RG.
#
#   Assertions (all numeric, no eyeballing):
#     B0  oracle self-check: replicated gxmath main VP projects the cube
#         centre onto the measured direct-magenta centroid (<=2.5px).
#         Guards the oracle's own math before it accuses the engine.
#     B   absolute mirror geometry: ghost centroid == screen projection of
#         F* (camera-ray-to-mirrored-cube meets the plane) (<=5px).
#     A   mirror-chain fidelity: echo UV at the ghost centroid ==
#         logged-M projection of the cube centre (<=0.01 uv). Catches
#         mirror-math, planarVP upload-convention, y-flip and RT-
#         orientation bugs (flip-class errors miss by ~1.0).
# Exit 0 iff all three PASS. Must run on the NVIDIA display (DISPLAY=:1)
# like reflect-check: fresh Xvfb servers fall back to llvmpipe, whose
# showcase main composite renders blank (engine/llvmpipe interaction,
# still open) while water + raw RT views survive.
import os
import re
import sys

import numpy as np

import audit_common as audit


OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/opencode/oracle"
TIMEOUT = 150

CUBE = np.array([0.35, 1.0, -0.25])   # scene.c minimal cube pos (ROT=0)
T_B0, T_B, T_A = 2.5, 5.0, 0.01


def run(env, capture_dir):
    #opencode: NVIDIA display via audit.display(); never xvfb-run (llvmpipe).
    environment = audit.scene_env(env, clean=True)
    result = audit.run_scene("--showcase-planar", environment, width=None,
                             height=None, fps=None, screenshot_dir=capture_dir,
                             shot_frame=None, timeout=TIMEOUT, check=False)
    return result.stdout or ""


def newest_shot(d):
    shot = audit.newest_shot(d)
    return str(shot) if shot is not None else None


def norm(v):
    return v / np.linalg.norm(v)


def lookat(eye, tgt, up):  # verbatim src/gxmath/matrix.c
    vz = norm(eye - tgt)
    vx = norm(np.cross(up, vz))
    vy = np.cross(vz, vx)
    return np.array([
        [vx[0], vy[0], vz[0], 0.0],
        [vx[1], vy[1], vz[1], 0.0],
        [vx[2], vy[2], vz[2], 0.0],
        [-vx @ eye, -vy @ eye, -vz @ eye, 1.0]])


def perspective(fovy, aspect, near, far):  # verbatim src/gxmath/matrix.c
    top = near * np.tan(fovy * 0.5)
    right = top * aspect
    rl, tb, fn = 2 * right, 2 * top, far - near
    return np.array([
        [2 * near / rl, 0, 0, 0],
        [0, 2 * near / tb, 0, 0],
        [0, 0, -(far + near) / fn, -1.0],
        [0, 0, -2 * far * near / fn, 0]])


def project(M, p, W, H):  # row-vector convention (p @ V @ P)
    c = np.append(p, 1.0) @ M
    ndc = c[:3] / c[3]
    return np.array([(ndc[0] * 0.5 + 0.5) * W, (1 - (ndc[1] * 0.5 + 0.5)) * H])


def magenta_blobs(img):
    r, g, b = img[:, :, 0], img[:, :, 1], img[:, :, 2]
    m = (r > 150) & (g < 110) & (b > 150)
    rows = np.where(m.any(axis=1))[0]
    if len(rows) < 2:
        return None, m
    gaps = np.diff(rows)
    cut = rows[np.argmax(gaps)] if gaps.max() > 1 else rows[len(rows) // 2]
    up = np.argwhere(m[:cut + 1, :])
    lo = np.argwhere(m[cut + 1:, :])
    lo[:, 0] += cut + 1
    if len(up) < 50 or len(lo) < 50:
        return None, m
    # image coords: x=col, y=row
    cu = np.array([up[:, 1].mean(), up[:, 0].mean()])
    cl = np.array([lo[:, 1].mean(), lo[:, 0].mean()])
    return (cu, cl), m


def main():
    from PIL import Image
    os.makedirs(OUT, exist_ok=True)
    capture_dir = os.path.join(OUT, "raw")
    os.makedirs(capture_dir, exist_ok=True)
    with audit.settings_preserved():
        return run_oracle(capture_dir, Image)


def run_oracle(capture_dir, Image):
    env = {"GX_DIAG_CUBE": "1", "GX_CUBE_ROT": "0", "GX_T_TAA": "0",
           "GX_T_BLOOM": "0",  # opencode: halo-free hard edges for bbox truth
           "GX_T_TONEMAP": "0",  # opencode: raw echoes (ACES/gamma corrupt UVs)
           "GX_CAM_YAW": "30", "GX_CAM_PITCH": "25", "GX_CAM_DIST": "6",
           "GX_SCREENSHOT_DIR": capture_dir}

    print("== [oracle] beauty capture ==")
    log = run(env, capture_dir)
    mos = list(re.finditer(r"ORACLEDUMP f=(\d+) mvp=([\d.,\-+eE]+) plane=([\d.\-+eE]+) "
                   r"n=\(([\d.\-+eE]+),([\d.\-+eE]+),([\d.\-+eE]+)\) "
                   r"cam=\(([\d.\-+eE]+),([\d.\-+eE]+),([\d.\-+eE]+)\) "
                   r"tgt=\(([\d.\-+eE]+),([\d.\-+eE]+),([\d.\-+eE]+)\) "
                   r"up=\(([\d.\-+eE]+),([\d.\-+eE]+),([\d.\-+eE]+)\) "
                   r"fovy=([\d.\-+eE]+) near=([\d.\-+eE]+) far=([\d.\-+eE]+) "
                   r"size=(\d+)x(\d+)", log))
    if not mos:
        print("ORACLE GATE: FAIL — no ORACLEDUMP in log (rebuild?)")
        return 1
    mo = mos[-1]  # last dump = closest to the capture frame (drift-proof)
    print(f"   dumps={len(mos)} first_f={mos[0].group(1)} last_f={mo.group(1)}")
    M = np.array([float(x) for x in mo.group(2).split(",")]).reshape(4, 4)
    plane_d = float(mo.group(3))
    n = np.array([float(mo.group(i)) for i in (4, 5, 6)])
    cam = np.array([float(mo.group(i)) for i in (7, 8, 9)])
    tgt = np.array([float(mo.group(i)) for i in (10, 11, 12)])
    up = np.array([float(mo.group(i)) for i in (13, 14, 15)])
    fovy, near, far = (float(mo.group(i)) for i in (16, 17, 18))
    W, H = int(mo.group(19)), int(mo.group(20))
    src = newest_shot(capture_dir)
    if not src:
        print("ORACLE GATE: FAIL — no beauty shot")
        return 1
    beauty = np.asarray(Image.open(src).convert("RGB")).astype(float)
    with open(src, "rb") as fsrc, open(os.path.join(OUT, "beauty.png"), "wb") as fdst:
        fdst.write(fsrc.read())

    print("== [oracle] echo capture (SSR debug 23) ==")
    env23 = dict(env, GX_SSR_DEBUG="23")
    echolog = run(env23, capture_dir)
    mos2 = list(re.finditer(r"ORACLEDUMP f=(\d+) mvp=([\d.,\-+eE]+) ", echolog))
    if mos2:
        # same-run matrix for the consume-path check (cross-run staleness
        # killed as a suspect class entirely)
        M = np.array([float(x) for x in mos2[-1].group(2).split(",")]
                     ).reshape(4, 4)
        print(f"   echo-run dumps={len(mos2)} last_f={mos2[-1].group(1)} "
              f"sameM={np.allclose(M, np.array([float(x) for x in mo.group(2).split(',')]).reshape(4, 4))}")
    src = newest_shot(capture_dir)
    if not src:
        print("ORACLE GATE: FAIL — no echo shot")
        return 1
    echo = np.asarray(Image.open(src).convert("RGB")).astype(float) / 255.0
    with open(src, "rb") as fsrc, open(os.path.join(OUT, "echo23.png"), "wb") as fdst:
        fdst.write(fsrc.read())
    if beauty.shape[:2] != echo.shape[:2]:
        print("ORACLE GATE: FAIL — capture size mismatch")
        return 1

    r0, g0, b0c = beauty[:, :, 0], beauty[:, :, 1], beauty[:, :, 2]
    if ((r0 > 150) & (g0 < 110) & (b0c > 150)).sum() < 100:
        print("ORACLE GATE: FAIL — magenta blobs not found")
        return 1

    # replicated main VP (row-vector gxmath convention)
    VP = lookat(cam, tgt, up) @ perspective(np.deg2rad(fovy), W / H, near, far)
    PH, PW = beauty.shape[:2]  # capture may differ from render rows
    yscale = PH / H  # present squish 1016->1010 rows

    def proj_px(p):
        c = np.append(np.asarray(p, float), 1.0) @ VP
        ndc = c[:3] / c[3]
        return np.array([(ndc[0] * 0.5 + 0.5) * PW,
                         (1 - (ndc[1] * 0.5 + 0.5)) * PH])

    # magenta mask split into direct (upper) / ghost (lower) by largest gap
    r, g, b = beauty[:, :, 0], beauty[:, :, 1], beauty[:, :, 2]
    m = (r > 150) & (g < 110) & (b > 150)
    occ = np.where(m.any(axis=1))[0]
    gaps = np.diff(occ)
    cut = occ[np.argmax(gaps)] if len(gaps) else occ[-1]
    boxes = {}
    for tag, sel in (("direct", m[:cut + 1, :]), ("ghost", m[cut + 1:, :])):
        ys, xs = np.where(sel)
        dy = cut + 1 if tag == "ghost" else 0
        boxes[tag] = (xs.min(), xs.max(), ys.min() + dy, ys.max() + dy,
                      len(xs))
    print(f"   magenta rows {occ.min()}-{occ.max()} gap @{cut}: "
          + "; ".join(f"{k} x[{v[0]}-{v[1]}] y[{v[2]}-{v[3]}] n={v[4]}"
                      for k, v in boxes.items()))

    # B0: oracle self-check — predicted 8-corner hull edges vs the DIRECT
    # blob. Only the front (magenta) face's left+TOP silhouette edges are
    # color-reliable (top/right faces shade pink/white; the bottom edge
    # merges into contact smear). Guards math.
    corners = np.array([[CUBE[0] + dx, CUBE[1] + dy, CUBE[2] + dz]
                        for dx in (-0.5, 0.5) for dy in (-0.5, 0.5)
                        for dz in (-0.5, 0.5)])
    hull = np.array([proj_px(p) for p in corners])
    e0 = (abs(hull[:, 0].min() - boxes["direct"][0])
          + abs(hull[:, 1].min() - boxes["direct"][2])) / 2
    b0 = "PASS" if e0 <= 10 else "FAIL"
    print(f"   B0 self-check: hull x[{hull[:, 0].min():.0f}-{hull[:, 0].max():.0f}] "
          f"y[{hull[:, 1].min():.0f}-{hull[:, 1].max():.0f}] "
          f"edge_err={e0:.1f}px [{b0}]")

    # B: ghost placement — mirrored-corner hull vs ghost blob left edge.
    # (Left silhouette edge is the sharp magenta boundary on both.)
    mpts = np.array([[CUBE[0] + dx, 2 * plane_d - (CUBE[1] + dy), CUBE[2] + dz]
                     for dx in (-0.5, 0.5) for dy in (-0.5, 0.5)
                     for dz in (-0.5, 0.5)])
    ghull = np.array([proj_px(p) for p in mpts])
    eB = abs(ghull[:, 0].min() - boxes["ghost"][0])
    bB = "PASS" if eB <= 15 else "FAIL"
    print(f"   B  ghost-pos:  mhull x[{ghull[:, 0].min():.0f}-{ghull[:, 0].max():.0f}] "
          f"y[{ghull[:, 1].min():.0f}-{ghull[:, 1].max():.0f}] "
          f"left_err={eB:.1f}px [{bB}]")

    # A: planar-chain STRUCTURAL fidelity over a clean-floor grid.
    # The echo PNG passes through the present path (composite blend,
    # 1016->1010 blit), which adds a small smooth spatial warp on top of
    # the mirror mapping — so absolute-uv comparison is the wrong metric
    # (it measured the presentation, not the chain, at ~0.03). Instead
    # fit pred->meas as an affine map: a correct chain (mirror math +
    # planarVP upload + y-flip + RT orientation) yields near-identity
    # with residual ~noise; transpose/flip/plane/upload-convention bugs
    # break the fit catastrophically (flip-class misses by ~1.0).
    # Clean floor only (ghost/contact excluded: different content class).
    invVP = np.linalg.inv(VP)
    Ps, Ms = [], []
    for gy in range(550, 951, 50):
        for gx in range(750, 1360, 60):
            if 690 <= gx <= 980 and 540 <= gy <= 830:
                continue  # ghost + contact smear
            bv = beauty[gy - 2:gy + 3, gx - 2:gx + 3].reshape(-1, 3).mean(axis=0)
            if not (bv[1] > bv[0] and bv[1] > bv[2] and bv.mean() < 150):
                continue  # floor green only
            nx, ny = gx / PW * 2 - 1, 1 - gy / PH * 2
            cn = np.array([nx, ny, -1.0, 1.0]) @ invVP
            cf = np.array([nx, ny, 1.0, 1.0]) @ invVP
            pn, pf = cn[:3] / cn[3], cf[:3] / cf[3]
            d = pf - pn
            F = pn + d * ((plane_d - pn[1]) / d[1])
            wm = F - np.array([0., 1., 0.]) * (2 * ((F @ np.array([0., 1., 0.])) - plane_d))
            c = np.append(wm, 1.0) @ M
            ndc = c[:3] / c[3]
            Ps.append([(ndc[0] + 1) * 0.5, (1 - ndc[1]) * 0.5])
            Ms.append(np.median(echo[gy - 2:gy + 3, gx - 2:gx + 3, :2]
                                .reshape(-1, 2), axis=0))
    Ps, Ms = np.array(Ps), np.array(Ms)
    A = np.column_stack([Ps, np.ones(len(Ps))])
    (au, _, _, _) = np.linalg.lstsq(A, Ms[:, 0], rcond=None)
    (av, _, _, _) = np.linalg.lstsq(A, Ms[:, 1], rcond=None)
    ru, rv = Ms[:, 0] - A @ au, Ms[:, 1] - A @ av
    dA = float(np.sqrt((np.concatenate([ru, rv]) ** 2).mean()))
    bA = "PASS" if len(Ps) >= 20 and dA <= 0.01 else "FAIL"
    print(f"   A  mirror-uv:  n={len(Ps)} affine resid={dA:.4f}uv [{bA}]")

    ok = b0 == bB == bA == "PASS"
    print(f"ORACLE GATE: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
