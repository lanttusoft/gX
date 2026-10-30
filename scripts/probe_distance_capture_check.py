#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: compare isolated GPU radial-distance face dumps to an opaque AABB oracle.
"""Validate base-level capture distances; not a reflection admission gate."""
import argparse
import json
from pathlib import Path

import numpy as np
import probe_coverage_check as coverage
from probe_distance_check import first_hit


def evaluate(path, center=None, extents=None):
    face, origin, image = coverage.read_dump(path, magic=b"GXDVF32\0")
    height, width = image.shape[:2]
    rays = coverage.face_rays(face, width, height)
    rays /= np.linalg.norm(rays, axis=-1)[..., None]
    if center is None:
        hit = np.zeros((height, width), dtype=bool)
        expected = np.zeros((height, width))
    else:
        hit, expected = first_hit(origin, rays, center, extents)
    actual = image[:, :, 3]
    expected_half = expected.astype(np.float16)
    actual_half = actual.astype(np.float16)
    ulp_delta = np.abs(actual_half.view(np.uint16).astype(np.int32)
                       - expected_half.view(np.uint16).astype(np.int32))
    local_error = np.abs(actual[hit] - expected[hit])
    return {"file": str(path), "face": face, "origin": origin.tolist(),
            "size": [width, height], "local_pixels": int(hit.sum()),
            "coverage_disagreements": int(((actual > 0) != hit).sum()),
            "negative_distances": int((actual < 0).sum()),
            "non_half_representable_values": int((actual != actual_half.astype(np.float32)).sum()),
            "exact_half_matches": int((ulp_delta[hit] == 0).sum()),
            "one_half_ulp_differences": int((ulp_delta[hit] == 1).sum()),
            "over_one_half_ulp_differences": int((ulp_delta[hit] > 1).sum()),
            "max_half_ulp_difference": int(ulp_delta[hit].max()) if hit.any() else 0,
            "max_radial_world_error": float(local_error.max()) if len(local_error) else 0,
            "mean_radial_world_error": float(local_error.mean()) if len(local_error) else 0,
            "sky_max_abs_distance": float(np.abs(actual[~hit]).max()) if (~hit).any() else 0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--center", nargs=3, type=float)
    parser.add_argument("--half-extents", nargs=3, type=float)
    args = parser.parse_args()
    if (args.center is None) != (args.half_extents is None):
        parser.error("supply both center and half-extents or neither for sky-only")
    paths = sorted(args.directory.glob("*.bin"))
    if not paths:
        parser.error("no distance dumps")
    rows = [evaluate(path, args.center, args.half_extents) for path in paths]
    fields = ("local_pixels", "coverage_disagreements", "negative_distances",
              "non_half_representable_values", "exact_half_matches",
              "one_half_ulp_differences", "over_one_half_ulp_differences")
    report = {key: sum(row[key] for row in rows) for key in fields}
    report.update({"faces": len(rows), "rows": rows,
                   "max_radial_world_error": max(row["max_radial_world_error"] for row in rows),
                   "max_half_ulp_difference": max(row["max_half_ulp_difference"] for row in rows),
                   "limits": "Opaque axis-aligned standard-shader fixture; base-level RGBA16F distance, allowing one representable half step for arithmetic/conversion. No filtering, GPU mips or reflection-search certification."})
    print(json.dumps(report, indent=2))
    return int(any(report[key] for key in ("coverage_disagreements", "negative_distances",
                                           "non_half_representable_values", "over_one_half_ulp_differences")))


if __name__ == "__main__":
    raise SystemExit(main())
