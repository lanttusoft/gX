#!/usr/bin/env python3
#ai: offline bounded search of GPU first-layer radial distances; no renderer activation.
"""Diagnose bounded distance lookup, retaining unresolved results as uncertain."""
import argparse
import json
from pathlib import Path

import numpy as np
import probe_coverage_check as coverage
from probe_distance_check import first_hit
from probe_lookup_coverage_check import cube_texels


def cube_sampler(depth):
    depth = np.asarray(depth)
    if (depth.ndim != 3 or depth.shape[0] != 6 or depth.shape[1] != depth.shape[2]
            or depth.shape[1] < 1 or not np.isfinite(depth).all() or (depth < 0).any()
            or (depth >= np.finfo(np.float16).max).any()):
        raise ValueError("six square finite nonnegative distance faces required")

    def sample(directions):
        valid = np.abs(directions).max(axis=-1) > 0
        safe = np.where(valid[..., None], directions, np.array([1., 0., 0.]))
        face, u, v = cube_texels(safe, depth.shape[1])
        return np.where(valid, depth[face, v, u], 0)

    return sample


def field(points, capture, sample):
    direction = points - capture
    radial = np.linalg.norm(direction, axis=-1)
    depth = sample(direction)
    return depth > 0, radial - depth, depth


def search(points, rays, capture, sample, max_distance=32., steps=128, refinements=8):
    points = np.asarray(points, dtype=float)
    rays = np.asarray(rays, dtype=float)
    capture = np.asarray(capture, dtype=float)
    if (points.shape != rays.shape or points.ndim != 2 or points.shape[1] != 3
            or capture.shape != (3,) or not np.isfinite(points).all()
            or not np.isfinite(rays).all() or not np.isfinite(capture).all()
            or not np.isfinite(max_distance) or max_distance <= 0
            or steps < 1 or refinements < 1):
        raise ValueError("finite Nx3 rays/points, capture and positive search budgets required")
    norm = np.linalg.norm(rays, axis=-1)
    if (norm == 0).any():
        raise ValueError("nonzero rays required")
    rays = rays / norm[:, None]
    count = len(points)
    status = np.full(count, "uncertain_unbracketed", dtype="<U32")
    times = np.zeros(count)
    lower = np.zeros(count)
    upper = np.zeros(count)
    previous_local, previous_residual, _ = field(points, capture, sample)
    active = ~(previous_local & (previous_residual >= 0))
    status[~active] = "uncertain_start_behind"
    bracketed = np.zeros(count, dtype=bool)
    query_count = np.ones(count, dtype=int)
    for step in range(1, steps + 1):
        indices = np.flatnonzero(active)
        if not len(indices):
            break
        time = max_distance * step / steps
        local, residual, _ = field(points[indices] + time * rays[indices], capture, sample)
        query_count[indices] += 1
        discontinuous = (~previous_local[indices]) & local & (residual >= 0)
        found = previous_local[indices] & local & (previous_residual[indices] < 0) & (residual >= 0)
        bad = indices[discontinuous]
        status[bad] = "uncertain_coverage_jump"
        active[bad] = False
        good = indices[found]
        lower[good] = max_distance * (step - 1) / steps
        upper[good] = time
        bracketed[good] = True
        active[good] = False
        previous_local[indices] = local
        previous_residual[indices] = residual
    for _ in range(refinements):
        indices = np.flatnonzero(bracketed)
        if not len(indices):
            break
        middle = (lower[indices] + upper[indices]) / 2
        local, residual, _ = field(points[indices] + middle[:, None] * rays[indices], capture, sample)
        query_count[indices] += 1
        bad = indices[~local]
        status[bad] = "uncertain_refinement_sky"
        bracketed[bad] = False
        below = local & (residual < 0)
        above = local & ~below
        lower[indices[below]] = middle[below]
        upper[indices[above]] = middle[above]
    indices = np.flatnonzero(bracketed)
    if len(indices):
        middle = (lower[indices] + upper[indices]) / 2
        local, residual, depth = field(points[indices] + middle[:, None] * rays[indices], capture, sample)
        query_count[indices] += 1
        half = depth.astype(np.float16)
        tolerance = np.nextafter(half, np.float16(np.inf), dtype=np.float16).astype(float) - half.astype(float)
        accepted = local & (np.abs(residual) <= tolerance)
        status[indices] = "uncertain_residual"
        status[indices[accepted]] = "local_candidate"
        times[indices[accepted]] = middle[accepted]
    return status, times, query_count


def load_cubes(directory):
    groups = {}
    for path in sorted(Path(directory).glob("*.bin")):
        face, origin, image = coverage.read_dump(path, magic=b"GXDVF32\0")
        key = (path.name.split("_", 1)[0], tuple(origin))
        if face in groups.setdefault(key, {}):
            raise ValueError(f"duplicate face {path}")
        groups[key][face] = image[:, :, 3].copy()
    cubes = {}
    for (frame, origin), faces in sorted(groups.items()):
        if set(faces) != set(range(6)):
            raise ValueError(f"incomplete capture {frame} {origin}")
        if origin not in cubes:
            cubes[origin] = (frame, np.stack([faces[i] for i in range(6)]))
    if not cubes:
        raise ValueError("no complete distance captures")
    return cubes


def diagnose(directory, grid=401, steps=128, refinements=8, max_distance=32.):
    x, z = np.meshgrid(np.linspace(-8, 8, grid), np.linspace(-8, 8, grid))
    points = np.stack((x, np.full_like(x, .1), z), axis=-1).reshape(-1, 3)
    rays = points - np.array([4., 4., 6.])
    rays[:, 1] *= -1
    rays /= np.linalg.norm(rays, axis=-1)[:, None]
    truth, truth_time = first_hit(points, rays, [8., 3., -3.], [1.2] * 3)
    rows = []
    for origin, (frame, depth) in load_cubes(directory).items():
        status, time, queries = search(points, rays, origin, cube_sampler(depth),
                                      max_distance, steps, refinements)
        candidate = status == "local_candidate"
        accepted_true = candidate & truth
        error = np.abs(time[accepted_true] - truth_time[accepted_true])
        false_points = points[candidate & ~truth] + time[candidate & ~truth, None] * rays[candidate & ~truth]
        false_geometry_distance = np.linalg.norm(
            np.maximum(np.abs(false_points - np.array([8., 3., -3.])) - 1.2, 0), axis=-1)
        witnesses = {}
        for label, mask in (("false_local", candidate & ~truth),
                            ("uncertain_true_hit", ~candidate & truth)):
            indices = np.flatnonzero(mask)
            if len(indices):
                i = indices[len(indices) // 2]
                witnesses[label] = {"point": points[i].tolist(), "ray": rays[i].tolist(),
                                    "status": str(status[i]), "candidate_time": float(time[i]),
                                    "reference_time": float(truth_time[i]) if truth[i] else None}
        rows.append({"capture_origin": list(origin), "frame": frame, "cube_size": depth.shape[1],
                     "status_counts": {str(s): int((status == s).sum()) for s in np.unique(status)},
                     "accepted_true_hits": int(accepted_true.sum()), "false_local_candidates": int((candidate & ~truth).sum()),
                     "uncertain_true_hits": int((~candidate & truth).sum()),
                     "accepted_hit_time_error_mean": float(error.mean()) if len(error) else None,
                     "accepted_hit_time_error_max": float(error.max()) if len(error) else None,
                     "false_candidate_distance_to_geometry_max": float(false_geometry_distance.max()) if len(false_geometry_distance) else None,
                     "max_queries_per_ray": int(queries.max()), "witnesses": witnesses})
    return {"receiver_samples": grid * grid, "actual_local_hits": int(truth.sum()),
            "parameters": {"steps": steps, "refinements": refinements, "max_distance": max_distance,
                           "residual_acceptance": "one upward half-float step of sampled radial distance"},
            "rows": rows,
            "limits": "Offline nearest base-level GPU distance search, no truth-guided directions. Candidates are NOT certified hits. Uncertain is NOT sky. No GPU sampling, filtering, mips, roughness, screen clipping or renderer admission."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--grid", type=int, default=401)
    parser.add_argument("--steps", type=int, default=128)
    parser.add_argument("--refinements", type=int, default=8)
    parser.add_argument("--max-distance", type=float, default=32)
    args = parser.parse_args()
    if args.grid < 2 or args.steps < 1 or args.refinements < 1 or not np.isfinite(args.max_distance) or args.max_distance <= 0:
        parser.error("positive budgets and grid >= 2 required")
    print(json.dumps(diagnose(args.directory, args.grid, args.steps, args.refinements, args.max_distance), indent=2))


if __name__ == "__main__":
    main()
