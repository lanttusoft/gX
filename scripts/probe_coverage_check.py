#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: offline base-level coverage oracle for the isolated cubemap experiment.
import argparse
import json
from pathlib import Path
import struct

import numpy as np


def read_dump(path, magic=b"GXCVF32\0"):
    data = Path(path).read_bytes()
    if len(data) < 32 or data[:8] != magic:
        raise ValueError(f"invalid coverage header: {path}")
    width, height, face = struct.unpack_from("<IIi", data, 8)
    origin = np.array(struct.unpack_from("<fff", data, 20), dtype=float)
    if not width or not height or face not in range(6):
        raise ValueError(f"invalid coverage dimensions/face: {path}")
    if len(data) != 32 + width * height * 4 * 4:
        raise ValueError(f"invalid coverage payload size: {path}")
    image = np.frombuffer(data, dtype="<f4", offset=32).reshape(height, width, 4)
    if not np.isfinite(origin).all() or not np.isfinite(image).all():
        raise ValueError(f"nonfinite coverage dump: {path}")
    return face, origin, image


def face_rays(face, width, height):
    y, x = np.mgrid[:height, :width]
    sc = 2 * (x + 0.5) / width - 1
    tc = 2 * (y + 0.5) / height - 1
    one = np.ones_like(sc)
    directions = ((one, -tc, -sc), (-one, -tc, sc), (sc, one, tc),
                  (sc, -one, -tc), (sc, -tc, one), (-sc, -tc, -one))
    return np.stack(directions[face], axis=-1)


def box_coverage(origin, rays, center, half_extents):
    lower = np.asarray(center) - np.asarray(half_extents) - origin
    upper = np.asarray(center) + np.asarray(half_extents) - origin
    nonparallel = rays != 0
    t0 = np.divide(lower, rays, out=np.zeros_like(rays), where=nonparallel)
    t1 = np.divide(upper, rays, out=np.zeros_like(rays), where=nonparallel)
    near = np.where(nonparallel, np.minimum(t0, t1), -np.inf).max(axis=2)
    far = np.where(nonparallel, np.maximum(t0, t1), np.inf).min(axis=2)
    outside_parallel = ((~nonparallel) & ((lower > 0) | (upper < 0))).any(axis=2)
    return (~outside_parallel) & (far >= np.maximum(near, 0)) & (far > 0)


def evaluate(path, center=None, half_extents=None):
    face, origin, image = read_dump(path)
    height, width = image.shape[:2]
    alpha = image[:, :, 3]
    if not ((alpha == 0) | (alpha == 1)).all():
        raise ValueError(f"nonbinary base-level marker: {path}")
    expected = (box_coverage(origin, face_rays(face, width, height), center, half_extents)
                if center is not None else np.zeros_like(alpha, dtype=bool))
    observed = alpha == 1
    mip_fractional = None
    if width % 2 == 0 and height % 2 == 0:
        mip = alpha.reshape(height // 2, 2, width // 2, 2).mean(axis=(1, 3))
        mip_fractional = int(((mip > 0) & (mip < 1)).sum())
    return {
        "file": str(path), "face": face, "origin": origin.tolist(),
        "size": [width, height], "expected_pixels": int(expected.sum()),
        "observed_pixels": int(observed.sum()),
        "disagreements": int((observed != expected).sum()),
        "cpu_box_filtered_mip1_fractional_pixels": mip_fractional,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--center", type=float, nargs=3)
    parser.add_argument("--half-extents", type=float, nargs=3)
    args = parser.parse_args()
    if (args.center is None) != (args.half_extents is None):
        parser.error("provide both --center and --half-extents, or neither for sky-only")
    if args.half_extents is not None and (
            not np.isfinite(args.center).all() or not np.isfinite(args.half_extents).all()
            or min(args.half_extents) <= 0):
        parser.error("finite center and positive finite half-extents required")
    paths = sorted(args.directory.glob("*.bin"))
    if not paths:
        parser.error("no coverage dumps")
    rows = [evaluate(path, args.center, args.half_extents) for path in paths]
    report = {"faces": len(rows), "disagreements": sum(r["disagreements"] for r in rows),
              "observed_pixels": sum(r["observed_pixels"] for r in rows), "rows": rows,
              "limits": "base-level axis-aligned opaque cube witness; mip values are CPU estimates"}
    print(json.dumps(report, indent=2))
    return int(report["disagreements"] != 0)


if __name__ == "__main__":
    raise SystemExit(main())
