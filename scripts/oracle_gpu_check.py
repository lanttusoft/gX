#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: Paper 1 M5 — opt-in GL4.3 GPU surface-bounce parity gate.

import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_cpu_path as oracle  # noqa: E402
import audit_common as audit  # noqa: E402

OUT = "/tmp/opencode/oracle_gpu"
WIDTH = 48
HEIGHT = 32
FRAME = 40
EXPOSURE = 0.80


def capture(name, bounces=1, reflection=False):
    target = os.path.join(OUT, name + ".bin")
    env = audit.scene_env({
        #opencode: this gate is GL4.3-only, so it requests the opt-in context
        #   itself (the engine default stays GL3.3).
        "GX_GL43_CONTEXT": "1",
        "GX_T_ORACLE_GPU": "1",
        "GX_T_ORACLE_GPU_REFLECTION": "1" if reflection else "0",
        "GX_T_ORACLE_GPU_BOUNCES": str(bounces),
        "GX_T_ORACLE_BOUNCE": "1",
        "GX_T_HDRDUMP": "1",
        "GX_HDRDUMP_FRAME": str(FRAME),
        "GX_HDRDUMP_PATH": target,
        "GX_T_BLOOM": "1",
        "GX_T_BLOOMINT": "0",
        "GX_T_SSAO": "0",
        "GX_T_SHADOWS": "0",
        "GX_T_POINT_SHADOWS": "0",
        "GX_T_SSR": "0",
        "GX_T_REFL": "0",
        "GX_T_TAA": "0",
        "GX_T_FOG": "0",
        "GX_T_GODRAY": "0",
        "GX_T_PARTICLES": "0",
        "GX_T_COOKIES": "0",
        "GX_T_DOF": "0",
        "GX_T_MOTIONBLUR": "0",
        "GX_T_FXAA": "0",
        "GX_T_LENSFLARE": "0",
        "GX_T_ENVAMBIENT": "0",
        "GX_T_SKY": "0",
        "GX_T_INSTANCING": "0",
        "GX_T_FRUSTUM": "0",
    }, clean=True)
    #opencode: GX_FRAMES bounds the run at the dump frame (no screenshot flag).
    result = audit.run_scene("--reflectrow", env, width=WIDTH, height=HEIGHT,
                             fps=None, frames=FRAME, timeout=120, check=True,
                             extra_args=("--preset", "2"))
    if "user=430" not in result.stdout and "user=440" not in result.stdout \
            and "user=450" not in result.stdout and "user=460" not in result.stdout:
        raise RuntimeError("M5 requires an opt-in GL4.3+ context")
    if "compute compile OK" not in result.stdout:
        raise RuntimeError("M5 compute shader did not compile")
    if not os.path.isfile(target):
        raise RuntimeError("GPU HDR dump was not produced")
    with open(target, "rb") as stream:
        magic, width, height, channels = struct.unpack("<8sIII", stream.read(20))
        payload = np.frombuffer(stream.read(), dtype="<f4")
    if magic != b"GXHDR1\x00\x00" or (width, height, channels) != (WIDTH, HEIGHT, 4):
        raise RuntimeError("invalid GPU HDR dump header")
    if payload.size != width * height * channels:
        raise RuntimeError("invalid GPU HDR dump payload size")
    return payload.reshape(height, width, channels).copy()


def main():
    global OUT
    if len(sys.argv) > 1:
        OUT = os.path.abspath(sys.argv[1])
    os.makedirs(OUT, exist_ok=True)
    bvh, materials = oracle.make_scene(matte_fixture=True)
    cpu = oracle.render_gpu_surface_bounce(
        bvh, materials, WIDTH, HEIGHT, shadows=False) * EXPOSURE
    with audit.settings_preserved():
        first = capture("surface_first")
        repeat = capture("surface_repeat")
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
    print("oracle-gpu M5")
    print("  geometry pixels %d" % int(mask.sum()))
    print("  linear HDR MAE %.6f" % mae)
    print("  linear HDR P95 %.6f" % p95)
    print("  linear HDR max %.6f" % maximum)
    print("  repeat max error %.6f" % repeat_error)
    failures = []
    if mae > 0.005:
        failures.append("GPU/CPU linear-HDR MAE exceeds 0.005")
    if p95 > 0.010:
        failures.append("GPU/CPU linear-HDR P95 exceeds 0.010")
    if maximum > 0.03:
        failures.append("GPU/CPU linear-HDR maximum exceeds 0.03")
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
