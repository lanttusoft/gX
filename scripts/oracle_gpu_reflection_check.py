#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: Paper 1 M8 — opt-in GL4.3 GPU perfect-reflection parity gate.

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_cpu_path as oracle  # noqa: E402
import oracle_gpu_check as gpu_check  # noqa: E402


OUT = "/tmp/opencode/oracle_gpu_reflection"


def main():
    global OUT
    if len(sys.argv) > 1:
        OUT = os.path.abspath(sys.argv[1])
    gpu_check.OUT = OUT
    os.makedirs(OUT, exist_ok=True)
    bvh, materials = oracle.make_scene(matte_fixture=False)
    cpu = oracle.render_gpu_reflection(
        bvh, materials, gpu_check.WIDTH, gpu_check.HEIGHT,
        shadows=False) * gpu_check.EXPOSURE
    first = gpu_check.capture("reflection_first", reflection=True)
    repeat = gpu_check.capture("reflection_repeat", reflection=True)
    gpu = first[::-1, :, :3]
    alpha = first[::-1, :, 3]
    mask = alpha < 0.99
    if int(mask.sum()) < 100:
        print("FAIL: GPU HDR readback has too few geometry pixels", file=sys.stderr)
        return 1
    delta = np.abs(cpu[mask] - gpu[mask])
    mae = float(delta.mean())
    p95 = float(np.percentile(delta, 95))
    maximum = float(delta.max())
    repeat_error = float(np.abs(first - repeat).max())
    print("oracle-gpu M8")
    print("  geometry pixels %d" % int(mask.sum()))
    print("  linear HDR MAE %.6f" % mae)
    print("  linear HDR P95 %.6f" % p95)
    print("  linear HDR max %.6f" % maximum)
    print("  repeat max error %.6f" % repeat_error)
    failures = []
    if mae > 0.008:
        failures.append("GPU/CPU linear-HDR MAE exceeds 0.008")
    if p95 > 0.016:
        failures.append("GPU/CPU linear-HDR P95 exceeds 0.016")
    if maximum > 0.05:
        failures.append("GPU/CPU linear-HDR maximum exceeds 0.05")
    if repeat_error != 0.0:
        failures.append("GPU HDR capture is not deterministic")
    if failures:
        for failure in failures:
            print("FAIL: " + failure, file=sys.stderr)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
