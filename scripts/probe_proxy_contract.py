#!/usr/bin/env python3
#ai: authored exact-solid proxy experiment; not inferred mesh bounds or renderer admission.
"""Separate world intersections, capture visibility, and sampled representation."""
import argparse
from itertools import product
import json

import numpy as np
from probe_distance_check import first_hit
from probe_distance_search import cube_sampler, load_cubes
from probe_footprint_check import footprint_guard


#ai: outward rounding encloses each box-plane subtraction/division for stored float inputs.
def boundary_uncertainty(points, rays, center, extents):
    nonparallel = rays != 0
    intervals = []
    numerators = []
    for sign in (-1, 1):
        plane = center + sign * extents
        lower = np.nextafter(np.nextafter(plane, -np.inf) - points, -np.inf)
        upper = np.nextafter(np.nextafter(plane, np.inf) - points, np.inf)
        numerators.append((lower, upper))
        a = np.divide(lower, rays, out=np.zeros_like(rays), where=nonparallel)
        b = np.divide(upper, rays, out=np.zeros_like(rays), where=nonparallel)
        intervals.append((np.nextafter(np.minimum(a, b), -np.inf),
                          np.nextafter(np.maximum(a, b), np.inf)))
    near_lower = np.where(nonparallel, np.minimum(intervals[0][0], intervals[1][0]), -np.inf).max(axis=-1)
    near_upper = np.where(nonparallel, np.minimum(intervals[0][1], intervals[1][1]), -np.inf).max(axis=-1)
    far_lower = np.where(nonparallel, np.maximum(intervals[0][0], intervals[1][0]), np.inf).min(axis=-1)
    far_upper = np.where(nonparallel, np.maximum(intervals[0][1], intervals[1][1]), np.inf).min(axis=-1)
    parallel_outside = (~nonparallel) & ((numerators[0][0] > 0) | (numerators[1][1] < 0))
    parallel_inside = (numerators[0][1] <= 0) & (numerators[1][0] >= 0)
    possible = ~parallel_outside.any(axis=-1) & (far_upper >= np.maximum(near_lower, 0)) & (far_upper > 0)
    certain = ~((~nonparallel) & ~parallel_inside).any(axis=-1) & (far_lower >= np.maximum(near_upper, 0)) & (far_lower > 0)
    return possible & ~certain


def proxy_lookup(points, rays, capture, proxies, scene_complete):
    points = np.asarray(points, dtype=float)
    rays = np.asarray(rays, dtype=float)
    capture = np.asarray(capture, dtype=float)
    if (points.shape != rays.shape or points.ndim != 2 or points.shape[1] != 3
            or capture.shape != (3,) or not np.isfinite(points).all()
            or not np.isfinite(rays).all() or not np.isfinite(capture).all()
            or not isinstance(scene_complete, bool)):
        raise ValueError("finite Nx3 points/rays, capture and explicit completeness required")
    norms = np.linalg.norm(rays, axis=-1)
    if (norms == 0).any():
        raise ValueError("nonzero rays required")
    rays = rays / norms[:, None]
    nearest = np.full(len(points), np.inf)
    owner = np.full(len(points), -1, dtype=int)
    uncertain_boundary = np.zeros(len(points), dtype=bool)
    exact_scene = scene_complete
    for index, proxy in enumerate(proxies):
        if proxy.get("kind") != "solid_aabb" or not isinstance(proxy.get("exact"), bool):
            raise ValueError("explicit solid_aabb kind and boolean exact declaration required")
        center = np.asarray(proxy["center"], dtype=float)
        extents = np.asarray(proxy["half_extents"], dtype=float)
        if center.shape != (3,) or extents.shape != (3,):
            raise ValueError("three-component proxy center/extents required")
        hit, time = first_hit(points, rays, center, extents)
        uncertain_boundary |= boundary_uncertainty(points, rays, center, extents)
        closer = hit & (time < nearest)
        nearest[closer] = time[closer]
        owner[closer] = index
        exact_scene &= proxy["exact"]
    has_proxy_hit = owner >= 0
    time = np.where(has_proxy_hit, nearest, 0)
    hit_points = points + time[:, None] * rays
    lookup = rays.copy()
    status = np.full(len(points), "uncertain_scene_contract", dtype="<U32")
    if exact_scene:
        status[~has_proxy_hit] = "sky_direction"
        status[has_proxy_hit] = "local_geometry"
        lookup[has_proxy_hit] = hit_points[has_proxy_hit] - capture
        zero_lookup = np.linalg.norm(lookup, axis=-1) == 0
        lookup[zero_lookup] = rays[zero_lookup]
        status[uncertain_boundary] = "uncertain_boundary"
    visibility = np.full(len(points), "not_certified", dtype="<U32")
    if exact_scene and (status == "local_geometry").any():
        indices = np.flatnonzero(status == "local_geometry")
        vectors = hit_points[indices] - capture
        target_distance = np.linalg.norm(vectors, axis=-1)
        nonzero = target_distance > 0
        visible_time = np.full(len(indices), np.inf)
        capture_owner = np.full(len(indices), -1, dtype=int)
        capture_boundary = np.zeros(len(indices), dtype=bool)
        capture_rays = vectors / np.where(nonzero, target_distance, 1)[:, None]
        for proxy_index, proxy in enumerate(proxies):
            hit, radial = first_hit(capture, capture_rays,
                                    proxy["center"], proxy["half_extents"])
            closer = hit & (radial < visible_time)
            visible_time[closer] = radial[closer]
            capture_owner[closer] = proxy_index
            capture_boundary |= boundary_uncertainty(capture, capture_rays,
                                                     np.asarray(proxy["center"]), np.asarray(proxy["half_extents"]))
        roundoff = 64 * np.finfo(float).eps * np.maximum(1, target_distance)
        visible = (nonzero & (capture_owner == owner[indices])
                   & (np.abs(visible_time - target_distance) <= roundoff))
        visibility[indices] = "uncertain_capture_hidden"
        visibility[indices[visible]] = "capture_visible"
        visibility[indices[capture_boundary]] = "uncertain_capture_boundary"
        visibility[indices[~nonzero]] = "uncertain_capture_origin"
    return status, time, owner, lookup, hit_points, visibility


#ai: independent triangle-plane/barycentric reference, used only after proxy lookup.
def triangle_reference(points, rays, center, extents):
    nearest = np.full(len(points), np.inf)
    center, extents = np.asarray(center), np.asarray(extents)
    for axis, sign in product(range(3), (-1, 1)):
        other = [i for i in range(3) if i != axis]
        vertices = []
        for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            corner = center.astype(float).copy()
            corner[axis] += sign * extents[axis]
            corner[other[0]] += a * extents[other[0]]
            corner[other[1]] += b * extents[other[1]]
            vertices.append(corner)
        for indices in ((0, 1, 2), (0, 2, 3)):
            a, b, c = [vertices[i] for i in indices]
            edge1, edge2 = b - a, c - a
            p = np.cross(rays, edge2)
            determinant = p @ edge1
            nonparallel = determinant != 0
            inverse = np.divide(1., determinant, out=np.zeros_like(determinant), where=nonparallel)
            offset = points - a
            u = (offset * p).sum(axis=-1) * inverse
            q = np.cross(offset, edge1)
            v = (rays * q).sum(axis=-1) * inverse
            time = (q @ edge2) * inverse
            bary_roundoff = 64 * np.finfo(float).eps
            hit = (nonparallel & (u >= -bary_roundoff) & (v >= -bary_roundoff)
                   & (u + v <= 1 + bary_roundoff) & (time >= 0))
            nearest = np.minimum(nearest, np.where(hit, time, np.inf))
    return np.isfinite(nearest), np.where(np.isfinite(nearest), nearest, 0)


def diagnose(directory, grid=401):
    proxy = {"kind": "solid_aabb", "exact": True, "center": [8., 3., -3.],
             "half_extents": [1.2] * 3}
    x, z = np.meshgrid(np.linspace(-8, 8, grid), np.linspace(-8, 8, grid))
    points = np.stack((x, np.full_like(x, .1), z), axis=-1).reshape(-1, 3)
    rays = points - np.array([4., 4., 6.])
    rays[:, 1] *= -1
    rays /= np.linalg.norm(rays, axis=-1)[:, None]
    reference, reference_time = triangle_reference(points, rays, proxy["center"], proxy["half_extents"])
    rows = []
    for origin, (frame, depth) in load_cubes(directory).items():
        status, time, _, direction, _, visibility = proxy_lookup(points, rays, origin, [proxy], True)
        local = status == "local_geometry"
        visible = visibility == "capture_visible"
        sampled_local = np.zeros(len(points), dtype=bool)
        interior = np.zeros(len(points), dtype=bool)
        if visible.any():
            sampled_local[visible] = cube_sampler(depth)(direction[visible]) > 0
            guard, _ = footprint_guard(depth, direction[visible])
            interior[visible] = guard == "interior_candidate"
        error = np.abs(time[local & reference] - reference_time[local & reference])
        boundary = status == "uncertain_boundary"
        rows.append({"capture_origin": list(origin), "frame": frame, "cube_size": depth.shape[1],
                     "world_local_hits": int(local.sum()), "world_sky_directions": int((status == "sky_direction").sum()),
                     "uncertain_world_boundaries": int(boundary.sum()),
                     "triangle_reference_resolved_label_disagreements": int(((local != reference) & ~boundary).sum()),
                     "triangle_reference_time_max_error": float(error.max()) if len(error) else 0,
                     "capture_visible_hits": int(visible.sum()),
                     "capture_hidden_hits": int((visibility == "uncertain_capture_hidden").sum()),
                     "uncertain_capture_boundaries": int((visibility == "uncertain_capture_boundary").sum()),
                     "visible_hits_with_sky_texel": int((visible & ~sampled_local).sum()),
                     "visible_hits_with_full_local_stencil": int(interior.sum()),
                     "color_fidelity": "unverified; geometry/visibility do not certify sampled RGB"})
    return {"receiver_samples": grid * grid, "explicit_proxy": proxy,
            "scene_complete_assertion": "Only this opaque cube contributes local capture geometry in this fixture; reflective slab is excluded.",
            "rows": rows,
            "limits": "Authored exact-solid geometry experiment, not inference from depth or arbitrary mesh bounds. No shader integration, RGB certification, transparency, roughness, filtering or admission."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory")
    parser.add_argument("--grid", type=int, default=401)
    args = parser.parse_args()
    if args.grid < 2:
        parser.error("grid >= 2 required")
    print(json.dumps(diagnose(args.directory, args.grid), indent=2))


if __name__ == "__main__":
    main()
