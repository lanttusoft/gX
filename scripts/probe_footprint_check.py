#!/usr/bin/env python3
#ai: discrete silhouette uncertainty diagnostic, NOT a geometric hit certificate.
"""Reject distance-search candidates with incomplete local texel neighborhoods."""
import argparse
import json

import numpy as np
from probe_distance_check import first_hit
from probe_distance_search import cube_sampler, load_cubes, search
from probe_lookup_coverage_check import cube_texels


def footprint_guard(depth, directions):
    depth = np.asarray(depth)
    cube_sampler(depth)
    size = depth.shape[1]
    face, u, v = cube_texels(directions, size)
    interior = (u > 0) & (u < size - 1) & (v > 0) & (v < size - 1)
    local = np.ones_like(interior)
    minimum = np.full(interior.shape, np.inf)
    maximum = np.zeros(interior.shape)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            values = depth[face, np.clip(v + dy, 0, size - 1), np.clip(u + dx, 0, size - 1)]
            local &= values > 0
            minimum = np.minimum(minimum, values)
            maximum = np.maximum(maximum, values)
    status = np.full(interior.shape, "uncertain_face_seam", dtype="<U32")
    status[interior & ~local] = "uncertain_silhouette"
    status[interior & local] = "interior_candidate"
    return status, maximum - minimum


def diagnose(directory, grid=401, steps=128):
    x, z = np.meshgrid(np.linspace(-8, 8, grid), np.linspace(-8, 8, grid))
    points = np.stack((x, np.full_like(x, .1), z), axis=-1).reshape(-1, 3)
    rays = points - np.array([4., 4., 6.])
    rays[:, 1] *= -1
    rays /= np.linalg.norm(rays, axis=-1)[:, None]
    truth, truth_time = first_hit(points, rays, [8., 3., -3.], [1.2] * 3)
    rows = []
    for origin, (frame, depth) in load_cubes(directory).items():
        status, time, queries = search(points, rays, origin, cube_sampler(depth), steps=steps)
        candidate = status == "local_candidate"
        guard_status = np.full(len(points), "uncertain_search", dtype="<U32")
        spans = np.zeros(len(points))
        if candidate.any():
            direction = points[candidate] + time[candidate, None] * rays[candidate] - origin
            guard_status[candidate], spans[candidate] = footprint_guard(depth, direction)
        retained = guard_status == "interior_candidate"
        true_retained = retained & truth
        errors = np.abs(time[true_retained] - truth_time[true_retained])
        rows.append({"capture_origin": list(origin), "frame": frame, "cube_size": depth.shape[1],
                     "search_true_candidates": int((candidate & truth).sum()),
                     "search_false_candidates": int((candidate & ~truth).sum()),
                     "retained_true_candidates": int(true_retained.sum()),
                     "retained_false_candidates": int((retained & ~truth).sum()),
                     "true_candidates_reclassified_uncertain": int((candidate & ~retained & truth).sum()),
                     "false_candidates_reclassified_uncertain": int((candidate & ~retained & ~truth).sum()),
                     "all_true_hits_left_uncertain": int((~retained & truth).sum()),
                     "status_counts": {str(s): int((guard_status == s).sum()) for s in np.unique(guard_status)},
                     "retained_hit_time_error_max": float(errors.max()) if len(errors) else None,
                     "retained_observed_depth_span_max": float(spans[retained].max()) if retained.any() else None,
                     "max_search_queries_per_ray": int(queries.max()),
                     "guard_texel_queries_per_search_candidate": 9})
    return {"receiver_samples": grid * grid, "actual_local_hits": int(truth.sum()),
            "parameters": {"march_steps": steps, "max_distance": 32, "refinements": 8,
                           "guard": "complete same-face 3x3 local stencil; face seams stay uncertain"},
            "rows": rows,
            "limits": "Discrete nearest-base-level GPU dump guard, not conservative geometric coverage. Interior candidates remain unverified; stencil cannot see subtexel holes or hidden surfaces. No sky certification, shader integration or admission."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory")
    parser.add_argument("--grid", type=int, default=401)
    parser.add_argument("--steps", type=int, default=128)
    args = parser.parse_args()
    if args.grid < 2 or args.steps < 1:
        parser.error("grid >= 2 and positive steps required")
    print(json.dumps(diagnose(args.directory, args.grid, args.steps), indent=2))


if __name__ == "__main__":
    main()
