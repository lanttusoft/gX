#!/usr/bin/env python3
#ai: offline dual-lookup diagnosis; not a reflection admission gate.
"""Compare outgoing/projected capture coverage with actual receiver-ray hits."""
import argparse
import json
from pathlib import Path

import numpy as np
import probe_coverage_check as coverage


def projected_rays(points, rays, capture_origin, half_extents):
    local = points - capture_origin
    inside = (np.abs(local) <= half_extents).all(axis=-1)
    planes = np.where(rays > 0, half_extents, -np.asarray(half_extents))
    times = np.divide(planes - local, rays, out=np.full_like(rays, np.inf),
                      where=rays != 0).min(axis=-1)
    lookup = local + times[..., None] * rays
    valid = inside & (np.abs(lookup).max(axis=-1) > 0)
    return np.where(valid[..., None], lookup, rays)


def cube_texels(rays, size):
    if size <= 0 or not np.isfinite(rays).all() or (np.abs(rays).max(axis=-1) == 0).any():
        raise ValueError("positive cube size and finite nonzero directions required")
    axis = np.abs(rays).argmax(axis=-1)
    major = np.take_along_axis(rays, axis[..., None], axis=-1)[..., 0]
    face = 2 * axis + (major < 0)
    x, y, z = np.moveaxis(rays, -1, 0)
    sc = np.choose(face, (-z, z, x, x, x, -x)) / np.abs(major)
    tc = np.choose(face, (-y, -y, z, -z, -y, -y)) / np.abs(major)
    u = np.clip(np.floor((sc + 1) * size / 2).astype(int), 0, size - 1)
    v = np.clip(np.floor((tc + 1) * size / 2).astype(int), 0, size - 1)
    return face, u, v


def dump_sample(faces, rays):
    size = faces[0].shape[0]
    face, u, v = cube_texels(rays, size)
    alpha = np.stack([faces[i][:, :, 3] for i in range(6)])
    sampled = alpha[face, v, u] == 1
    selected_rays = np.stack([coverage.face_rays(i, size, size) for i in range(6)])[face, v, u]
    return sampled, selected_rays


def diagnose(directory, grid=401):
    groups = {}
    for path in sorted(Path(directory).glob("*.bin")):
        face, origin, image = coverage.read_dump(path)
        key = (path.name.split("_", 1)[0], tuple(origin))
        if face in groups.setdefault(key, {}):
            raise ValueError(f"duplicate face: {path}")
        groups[key][face] = image
    if not groups:
        raise ValueError("no coverage dumps")
    #ai: fixed temporary fixture, not inferred from observed coverage.
    center = np.array([8., 3., -3.])
    extents = np.array([1.2, 1.2, 1.2])
    camera = np.array([4., 4., 6.])
    x, z = np.meshgrid(np.linspace(-8, 8, grid), np.linspace(-8, 8, grid))
    points = np.stack((x, np.full_like(x, .1), z), axis=-1)
    rays = points - camera
    rays[..., 1] *= -1
    truth = coverage.box_coverage(points, rays, center, extents)
    rows = []
    for (frame, origin_tuple), faces in groups.items():
        if set(faces) != set(range(6)):
            raise ValueError(f"incomplete cube: {frame}, {origin_tuple}")
        size = faces[0].shape[0]
        if any(im.shape != (size, size, 4) for im in faces.values()):
            raise ValueError("inconsistent cube dimensions")
        origin = np.array(origin_tuple)
        projected = projected_rays(points, rays, origin, np.array([50., 12., 50.]))
        direct_local = coverage.box_coverage(origin, rays, center, extents)
        projected_local = coverage.box_coverage(origin, projected, center, extents)
        combinations = {}
        witnesses = {}
        for direct in (False, True):
            for corrected in (False, True):
                for actual in (False, True):
                    mask = ((direct_local == direct) & (projected_local == corrected)
                            & (truth == actual))
                    label = f"outgoing{int(direct)}_projected{int(corrected)}_truth{int(actual)}"
                    combinations[label] = int(mask.sum())
                    if mask.any():
                        indices = np.argwhere(mask)
                        j, i = indices[len(indices) // 2]
                        witnesses[label] = {"point": points[j, i].tolist(),
                                            "outgoing": rays[j, i].tolist(),
                                            "projected": projected[j, i].tolist()}
        samples = {}
        for label, lookup in (("outgoing", rays), ("projected", projected)):
            observed, texel_ray = dump_sample(faces, lookup)
            texel_truth = coverage.box_coverage(origin, texel_ray, center, extents)
            exact = direct_local if label == "outgoing" else projected_local
            samples[label] = {
                "dump_vs_selected_texel_oracle_disagreements": int((observed != texel_truth).sum()),
                "nearest_texel_vs_continuous_direction_disagreements": int((observed != exact).sum()),
            }
        rows.append({"frame": frame, "capture_origin": origin.tolist(), "size": size,
                     "combinations": combinations, "witnesses": witnesses, "samples": samples,
                     "coverage_union_false_positives": int(((direct_local | projected_local) & ~truth).sum()),
                     "coverage_union_false_negatives": int((~(direct_local | projected_local) & truth).sum())})
    return {"receiver_samples_per_cube": grid * grid, "actual_local_hits": int(truth.sum()),
            "fixture": {"camera": camera.tolist(), "emitter_center": center.tolist(),
                        "emitter_half_extents": extents.tolist(), "receiver_y": .1,
                        "receiver_xz_range": [-8, 8], "box_half_extents": [50, 12, 50]},
            "limits": "CPU continuous directions and nearest base-level GPU dump texels; no visibility clipping, bilinear or GPU mip sampling",
            "rows": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--grid", type=int, default=401)
    args = parser.parse_args()
    if args.grid < 2:
        parser.error("grid must be at least 2")
    report = diagnose(args.directory, args.grid)
    print(json.dumps(report, indent=2))
    return int(any(s["dump_vs_selected_texel_oracle_disagreements"]
                   for row in report["rows"] for s in row["samples"].values()))


if __name__ == "__main__":
    raise SystemExit(main())
