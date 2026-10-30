#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: ideal first-hit distance experiment; no GPU distance capture or admission.
"""Measure what a single-layer local-distance cubemap can reconstruct."""
import argparse
import json

import numpy as np
import probe_coverage_check as coverage
import probe_lookup_coverage_check as lookup


def first_hit(origin, rays, center, extents):
    rays = np.asarray(rays, dtype=float)
    if (rays.shape[-1] != 3 or not np.isfinite(rays).all()
            or not np.isfinite(origin).all() or not np.isfinite(center).all()
            or not np.isfinite(extents).all() or np.any(np.asarray(extents) <= 0)):
        raise ValueError("finite rays/origins and positive finite extents required")
    lower = np.asarray(center) - np.asarray(extents) - origin
    upper = np.asarray(center) + np.asarray(extents) - origin
    nonparallel = rays != 0
    t0 = np.divide(lower, rays, out=np.zeros_like(rays), where=nonparallel)
    t1 = np.divide(upper, rays, out=np.zeros_like(rays), where=nonparallel)
    entry = np.where(nonparallel, np.minimum(t0, t1), -np.inf).max(axis=-1)
    exit_time = np.where(nonparallel, np.maximum(t0, t1), np.inf).min(axis=-1)
    parallel_outside = ((~nonparallel) & ((lower > 0) | (upper < 0))).any(axis=-1)
    time = np.where(entry >= 0, entry, exit_time)
    hit = (~parallel_outside) & (exit_time >= np.maximum(entry, 0)) & (exit_time > 0)
    hit &= np.isfinite(time)
    return hit, np.where(hit, time, 0)


def reconstructed_hit(capture, target, center, extents):
    direction = np.asarray(target, dtype=float) - capture
    radial_length = np.linalg.norm(direction, axis=-1)
    if (radial_length == 0).any():
        raise ValueError("target must differ from capture origin")
    direction /= radial_length[..., None]
    hit, distance = first_hit(capture, direction, center, extents)
    return hit, capture + distance[..., None] * direction, distance


def diagnose(grid=401, size=512):
    center = np.array([8., 3., -3.])
    extents = np.full(3, 1.2)
    camera = np.array([4., 4., 6.])
    x, z = np.meshgrid(np.linspace(-8, 8, grid), np.linspace(-8, 8, grid))
    points = np.stack((x, np.full_like(x, .1), z), axis=-1)
    rays = points - camera
    rays[..., 1] *= -1
    rays /= np.linalg.norm(rays, axis=-1)[..., None]
    hit, time = first_hit(points, rays, center, extents)
    target = (points + time[..., None] * rays)[hit]
    origins = ((4., -4., 6.), (-3., 1.5, -3.), (3., 1.5, -3.),
               (-3., 1.5, 3.), (3., 1.5, 3.))
    rows = []
    for origin in origins:
        capture = np.array(origin)
        local, reconstructed, distance = reconstructed_hit(capture, target, center, extents)
        error = np.linalg.norm(reconstructed - target, axis=-1)
        visible = local & (error < 1e-8)
        direction = target - capture
        face, u, v = lookup.cube_texels(direction, size)
        texel_rays = np.stack([coverage.face_rays(i, size, size) for i in range(6)])[face, v, u]
        texel_rays /= np.linalg.norm(texel_rays, axis=-1)[..., None]
        texel_hit, texel_distance = first_hit(capture, texel_rays, center, extents)
        texel_position = capture + texel_distance[..., None] * texel_rays
        texel_error = np.linalg.norm(texel_position - target, axis=-1)
        errors_visible = texel_error[visible & texel_hit]
        witnesses = {}
        for name, mask in (("visible", visible), ("hidden", ~visible)):
            indices = np.flatnonzero(mask)
            if len(indices):
                i = indices[len(indices) // 2]
                witnesses[name] = {"receiver": points[hit][i].tolist(),
                                   "actual_hit": target[i].tolist(),
                                   "capture_first_hit": reconstructed[i].tolist(),
                                   "capture_distance": float(distance[i]),
                                   "world_error": float(error[i])}
        rows.append({"capture_origin": list(origin), "actual_local_hits": len(target),
                     "ideal_visible_hits": int(visible.sum()),
                     "ideal_hidden_hits": int((~visible).sum()),
                     "ideal_capture_misses": int((~local).sum()),
                     "ideal_visible_max_world_error": float(error[visible].max()) if visible.any() else None,
                     "nearest_depth_missing_visible_hits": int((visible & ~texel_hit).sum()),
                     "nearest_depth_reconstructed_visible_hits": int((visible & texel_hit).sum()),
                     "nearest_depth_visible_world_error_mean": float(errors_visible.mean()) if len(errors_visible) else None,
                     "nearest_depth_visible_world_error_max": float(errors_visible.max()) if len(errors_visible) else None,
                     "witnesses": witnesses})
    return {"grid": grid, "receiver_samples": grid * grid, "simulated_cube_size": size,
            "rows": rows, "limits": "Analytic opaque-cube first-hit distances, queried at ground-truth hit directions (oracle upper bound, NOT a lookup algorithm). No GPU distance data, filtering, visibility clipping, iterative search or admission."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--grid", type=int, default=401)
    parser.add_argument("--size", type=int, default=512)
    args = parser.parse_args()
    if args.grid < 2 or args.size < 1:
        parser.error("grid >= 2 and size >= 1 required")
    print(json.dumps(diagnose(args.grid, args.size), indent=2))


if __name__ == "__main__":
    main()
