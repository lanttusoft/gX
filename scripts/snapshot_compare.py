#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: generic PNG/PPM snapshot comparison — reusable image diff for GUI/visual
#   review. No display, scene launch or AI probe; PIL/numpy only.
"""Compare two PNG or binary PPM (P6) images.

Exit codes: 0 pass, 1 visual/verdict failure, 2 usage/input error.

The verdict compares pixels whose any-channel absolute delta exceeds
--tolerance. --max-diff-count/--max-diff-fraction bound the changed pixels;
--max-mean/--max-mse optionally add image-wide error bounds. --region applies
the whole verdict to a half-open x0,y0,x1,y1 window. --diff writes a PNG with
the changed pixels highlighted in red and the unchanged content dimmed.
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def _read_ppm(path):
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise ValueError(f"{path}: expected binary PPM P6, not {data[:2]!r}")
    position = 2
    tokens = []
    while len(tokens) < 3:
        while position < len(data) and data[position:position + 1].isspace():
            position += 1
        if position < len(data) and data[position:position + 1] == b"#":
            newline = data.find(b"\n", position)
            if newline < 0:
                raise ValueError(f"{path}: truncated PPM comment")
            position = newline + 1
            continue
        start = position
        while position < len(data) and not data[position:position + 1].isspace():
            position += 1
        if start == position:
            raise ValueError(f"{path}: truncated PPM header")
        tokens.append(data[start:position])
    try:
        width, height, maximum = (int(token) for token in tokens)
    except ValueError as error:
        raise ValueError(f"{path}: invalid PPM header") from error
    if maximum != 255:
        raise ValueError(f"{path}: only 8-bit PPM (maxval 255) is supported")
    if width <= 0 or height <= 0:
        raise ValueError(f"{path}: invalid PPM dimensions")
    if position >= len(data) or not data[position:position + 1].isspace():
        raise ValueError(f"{path}: missing PPM header terminator")
    position += 2 if data[position:position + 2] == b"\r\n" else 1
    expected = width * height * 3
    payload = data[position:position + expected]
    if len(payload) != expected:
        raise ValueError(f"{path}: truncated PPM pixel data")
    return np.frombuffer(payload, dtype=np.uint8).reshape(height, width, 3)


def load_rgb(path):
    path = Path(path)
    if not path.is_file():
        raise ValueError(f"{path}: image not found")
    suffix = path.suffix.lower()
    if suffix == ".ppm":
        array = _read_ppm(path)
    elif suffix == ".png":
        with Image.open(path) as image:
            array = np.asarray(image.convert("RGB"), dtype=np.uint8)
    else:
        raise ValueError(f"{path}: unsupported image type (use PNG or PPM P6)")
    return array.astype(np.int16)


def parse_region(text, width, height):
    try:
        values = [int(value.strip()) for value in text.split(",")]
    except ValueError as error:
        raise ValueError("--region must be x0,y0,x1,y1 integers") from error
    if len(values) != 4:
        raise ValueError("--region must be x0,y0,x1,y1")
    x0, y0, x1, y1 = values
    if not (0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height):
        raise ValueError(
            f"--region {text} is outside the {width}x{height} image")
    return x0, y0, x1, y1


def compare_arrays(first, second, tolerance=0):
    delta = np.abs(first - second)
    changed = (delta > tolerance).any(axis=2)
    pixel_error = delta.mean(axis=2)
    count = int(changed.sum())
    area = changed.size
    return {
        "diff_count": count,
        "diff_fraction": count / area if area else 0.0,
        "mean_abs": float(pixel_error.mean()) if area else 0.0,
        "mse": float((delta.astype(np.float64) ** 2).mean()) if area else 0.0,
        "max_delta": int(delta.max()) if area else 0,
        "changed": changed,
        "delta": delta,
    }


def write_diff(path, first, changed, region=None):
    height, width = changed.shape
    visible = changed.copy()
    if region is not None:
        x0, y0, x1, y1 = region
        mask = np.zeros_like(visible)
        mask[y0:y1, x0:x1] = True
        visible &= mask
    gray = (first.astype(np.float32) * np.array([0.299, 0.587, 0.114])).sum(axis=2)
    base = np.clip(gray * 0.45, 0, 255).astype(np.uint8)
    output = np.repeat(base[:, :, None], 3, axis=2)
    output[visible] = np.array([255, 48, 48], dtype=np.uint8)
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(output).save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    parser.add_argument("--tolerance", type=int, default=0,
                        help="per-channel absolute tolerance (0..255)")
    parser.add_argument("--max-diff-count", type=int, default=None,
                        help="maximum changed pixels (default 0 when no "
                             "--max-diff-fraction is given)")
    parser.add_argument("--max-diff-fraction", type=float,
                        help="maximum changed-pixel fraction (0..1)")
    parser.add_argument("--max-mean", type=float,
                        help="maximum mean absolute RGB error (0..255)")
    parser.add_argument("--max-mse", type=float,
                        help="maximum mean squared RGB error")
    parser.add_argument("--region",
                        help="half-open x0,y0,x1,y1 verdict window")
    parser.add_argument("--diff", type=Path,
                        help="write a PNG with changed pixels highlighted")
    parser.add_argument("--json", action="store_true",
                        help="print the summary as JSON")
    args = parser.parse_args()

    if not 0 <= args.tolerance <= 255:
        parser.error("--tolerance must be 0..255")
    if args.max_diff_count is not None and args.max_diff_count < 0:
        parser.error("--max-diff-count must be >= 0")
    if args.max_diff_fraction is not None and not 0.0 <= args.max_diff_fraction <= 1.0:
        parser.error("--max-diff-fraction must be 0..1")
    if args.max_mean is not None and args.max_mean < 0:
        parser.error("--max-mean must be >= 0")
    if args.max_mse is not None and args.max_mse < 0:
        parser.error("--max-mse must be >= 0")

    try:
        first = load_rgb(args.first)
        second = load_rgb(args.second)
        if first.shape != second.shape:
            raise ValueError(
                f"image sizes differ: {args.first}={first.shape[1]}x{first.shape[0]}, "
                f"{args.second}={second.shape[1]}x{second.shape[0]}")
        height, width = first.shape[:2]
        region = parse_region(args.region, width, height) if args.region else None
        full_first = first
        full_second = second
        if region is not None:
            x0, y0, x1, y1 = region
            first = first[y0:y1, x0:x1]
            second = second[y0:y1, x0:x1]
        result = compare_arrays(first, second, args.tolerance)
        if args.diff:
            full_result = compare_arrays(full_first, full_second, args.tolerance)
            write_diff(args.diff, full_first, full_result["changed"], region)
    except (OSError, ValueError) as error:
        print(f"SNAPSHOT_COMPARE: ERROR — {error}", file=sys.stderr)
        return 2

    max_count = args.max_diff_count
    if max_count is None and args.max_diff_fraction is None:
        max_count = 0
    failures = []
    if max_count is not None and result["diff_count"] > max_count:
        failures.append(
            f"diff_count {result['diff_count']} > {max_count}")
    if (args.max_diff_fraction is not None
            and result["diff_fraction"] > args.max_diff_fraction):
        failures.append(
            f"diff_fraction {result['diff_fraction']:.6f} > "
            f"{args.max_diff_fraction:.6f}")
    if args.max_mean is not None and result["mean_abs"] > args.max_mean:
        failures.append(f"mean_abs {result['mean_abs']:.6f} > {args.max_mean}")
    if args.max_mse is not None and result["mse"] > args.max_mse:
        failures.append(f"mse {result['mse']:.6f} > {args.max_mse}")

    summary = {
        "first": str(args.first),
        "second": str(args.second),
        "width": width,
        "height": height,
        "region": list(region) if region else None,
        "tolerance": args.tolerance,
        "diff_count": result["diff_count"],
        "diff_fraction": result["diff_fraction"],
        "mean_abs": result["mean_abs"],
        "mse": result["mse"],
        "max_delta": result["max_delta"],
        "max_diff_count": max_count,
        "max_diff_fraction": args.max_diff_fraction,
        "max_mean": args.max_mean,
        "max_mse": args.max_mse,
        "pass": not failures,
        "failures": failures,
    }
    if args.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        verdict = "PASS" if not failures else "FAIL"
        print(f"SNAPSHOT_COMPARE: {verdict}")
        print(f"  size={width}x{height} region={region} tolerance={args.tolerance}")
        print(f"  diff_count={result['diff_count']} "
              f"fraction={result['diff_fraction']:.6f} "
              f"mean_abs={result['mean_abs']:.6f} mse={result['mse']:.6f} "
              f"max_delta={result['max_delta']}")
        for failure in failures:
            print(f"  FAIL {failure}")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
