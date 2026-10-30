#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: Paper 1 M2 — CPU two-bounce transport gate.
#   This intentionally uses the matte variant of the fixed reflectrow layout:
#   exact metallic response is still a later parity row, while diffuse bounce
#   energy must be observable here. No runtime renderer or window is involved.

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_cpu_path as oracle  # noqa: E402


OUT = "/tmp/opencode/oracle_bounce"


def main():
    global OUT
    if len(sys.argv) > 1:
        OUT = os.path.abspath(sys.argv[1])
    os.makedirs(OUT, exist_ok=True)
    bvh, materials = oracle.make_scene(matte_fixture=True)
    #opencode: keep the compact fixture coarse enough that the sparse second
    #   bounce is not averaged away by the fixed camera's mostly-floor pixels.
    width, height = 8, 6
    direct = oracle.render(bvh, materials, width, height, 2, 0)
    one = oracle.render(bvh, materials, width, height, 4, 1)
    two = oracle.render(bvh, materials, width, height, 4, 2)
    repeat = oracle.render(bvh, materials, width, height, 4, 2)
    reference = oracle.render(bvh, materials, width, height, 8, 2)
    images = (direct, one, two, repeat, reference)
    if not all(np.isfinite(image).all() for image in images):
        print("FAIL: non-finite CPU oracle radiance", file=sys.stderr)
        return 1

    one_delta = float(np.abs(one - direct).mean())
    two_delta = float(np.abs(two - one).mean())
    one_error = float(np.abs(one - reference).mean())
    two_error = float(np.abs(two - reference).mean())
    repeat_error = float(np.abs(two - repeat).max())
    np.savez_compressed(os.path.join(OUT, "bounce_oracle.npz"),
                        direct=direct, one=one, two=two, reference=reference)
    oracle.save_image(os.path.join(OUT, "bounce_direct.png"), direct)
    oracle.save_image(os.path.join(OUT, "bounce_one.png"), one)
    oracle.save_image(os.path.join(OUT, "bounce_two.png"), two)
    print("oracle-bounce M2")
    print("  one-bounce delta    %.6f" % one_delta)
    print("  two-bounce delta    %.6f" % two_delta)
    print("  one-bounce error    %.6f" % one_error)
    print("  two-bounce error    %.6f" % two_error)
    print("  repeat max error    %.6f" % repeat_error)

    failures = []
    if one_delta <= 0.05:
        failures.append("one-bounce transport is not observable")
    if two_delta <= 0.01:
        failures.append("second-bounce transport is not observable")
    if two_error >= one_error:
        failures.append("additional samples do not improve the two-bounce estimate")
    if repeat_error != 0.0:
        failures.append("CPU oracle is not deterministic")
    if failures:
        for failure in failures:
            print("FAIL: " + failure, file=sys.stderr)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
