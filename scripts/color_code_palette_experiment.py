#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: deterministic palette-density study for the screen color code.
#   Compares 4/8/16-colour subsets at their real cell densities (2/3/4 bits
#   per cell) for capacity and nearest-colour error under gain/bias/noise.

import argparse
import itertools
import json
import sys

import numpy as np


PALETTE = np.array([
    (0, 0, 0), (255, 255, 255),
    (55, 10, 10), (110, 20, 20), (165, 30, 30), (220, 40, 40),
    (10, 50, 20), (20, 100, 40), (30, 150, 60), (40, 200, 80),
    (10, 20, 55), (20, 40, 110), (30, 60, 165), (40, 80, 220),
    (64, 64, 64), (192, 192, 192),
], dtype=np.float64)

MODES = ((16, 4), (8, 3), (4, 2))
SIGMAS = (0, 5, 10, 15, 20, 30)
DATA_CELLS = 32 * 35
ECC_BITS = 13


def greedy_subset(count):
    selected = [0, 1]
    while len(selected) < count:
        best = None
        best_score = -1.0
        for index in range(len(PALETTE)):
            if index in selected:
                continue
            score = min(np.linalg.norm(PALETTE[index] - PALETTE[other])
                        for other in selected)
            if score > best_score:
                best_score = score
                best = index
        selected.append(best)
    return selected


def min_distance(indices):
    return min(np.linalg.norm(PALETTE[a] - PALETTE[b])
               for a, b in itertools.combinations(indices, 2))


def capacity(bits_per_cell):
    bits = DATA_CELLS * bits_per_cell
    encoded = bits // ECC_BITS
    compressed = encoded
    while compressed + (compressed + 7) // 8 > encoded:
        compressed -= 1
    return encoded, compressed, max(0, compressed - 1)


def error_rate(indices, sigma, samples, seed, gain=0.95, bias=5.0):
    rng = np.random.default_rng(seed)
    symbols = rng.integers(0, len(indices), samples)
    selected = PALETTE[np.asarray(indices)]
    colors = selected[symbols]
    observed = np.clip(colors * gain + bias
                       + rng.normal(0.0, sigma, colors.shape), 0.0, 255.0)
    distances = ((observed[:, None, :] - selected[None, :, :]) ** 2).sum(axis=2)
    decoded = distances.argmin(axis=1)
    return float((decoded != symbols).mean())


def run(samples, seed):
    rows = []
    for count, bits in MODES:
        indices = greedy_subset(count)
        encoded, compressed, payload = capacity(bits)
        errors = [error_rate(indices, sigma, samples, seed)
                  for sigma in SIGMAS]
        rows.append({
            "colors": count,
            "bits_per_cell": bits,
            "indices": indices,
            "min_distance": round(min_distance(indices), 2),
            "encoded_bytes": encoded,
            "compressed_bytes": compressed,
            "payload_bytes": payload,
            "error_rates": {str(sigma): round(error * 100.0, 4)
                            for sigma, error in zip(SIGMAS, errors)},
        })
    return rows


def check(rows):
    by_colors = {row["colors"]: row for row in rows}
    if not (by_colors[4]["payload_bytes"] < by_colors[8]["payload_bytes"]
            < by_colors[16]["payload_bytes"]):
        return "payload capacity is not ordered 4 < 8 < 16"
    for sigma in ("15", "20"):
        if not (by_colors[4]["error_rates"][sigma]
                <= by_colors[8]["error_rates"][sigma]
                <= by_colors[16]["error_rates"][sigma]):
            return f"error rates are not ordered 4 <= 8 <= 16 at sigma {sigma}"
    return None


def main():
    parser = argparse.ArgumentParser(description="Color-code palette-density study.")
    parser.add_argument("--samples", type=int, default=200000)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    rows = run(args.samples, args.seed)
    if args.json:
        print(json.dumps(rows, indent=2, sort_keys=True))
    else:
        print(f"{'colors':>6} {'bits':>4} {'min_dist':>8} {'payload':>7} "
              + " ".join(f"sig{s:>2}" for s in SIGMAS))
        for row in rows:
            errors = " ".join(f"{row['error_rates'][str(s)]:6.4f}" for s in SIGMAS)
            print(f"{row['colors']:>6} {row['bits_per_cell']:>4} "
                  f"{row['min_distance']:>8.1f} {row['payload_bytes']:>7} "
                  f"{errors}")
        print("indices:", {row["colors"]: row["indices"] for row in rows})
    if args.check:
        problem = check(rows)
        if problem:
            print("PALETTE_EXPERIMENT: FAIL — " + problem, file=sys.stderr)
            return 1
        print("PALETTE_EXPERIMENT: PASS — capacity and robustness ordering")
    return 0


if __name__ == "__main__":
    sys.exit(main())
