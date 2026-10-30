#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: Paper 1 M2/M3/M4 — deterministic raster/CPU linear-HDR parity gate.
#   The renderer's opt-in matte, GGX, and environment-bounce fixtures evaluate
#   the same direct-light contracts as oracle_cpu_path.py. This compares the
#   pre-tonemap RGBA16F scene target, not an 8-bit screenshot.

import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_cpu_path as oracle  # noqa: E402
import audit_common as audit  # noqa: E402

OUT = "/tmp/opencode/oracle_hdr"
WIDTH = 48
HEIGHT = 32
FRAME = 40
EXPOSURE = 0.80
TIMEOUT = 120


def capture(name, fixture):
    target = os.path.join(OUT, name + ".bin")
    env = audit.scene_env({
        "GX_T_HDRDUMP": "1",
        "GX_HDRDUMP_FRAME": str(FRAME),
        "GX_HDRDUMP_PATH": target,
        "GX_T_ORACLE_MATTE": "1" if fixture in ("matte", "bounce") else "0",
        "GX_T_ORACLE_GGX": "1" if fixture == "ggx" else "0",
        "GX_T_ORACLE_BOUNCE": "1" if fixture == "bounce" else "0",
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
    #opencode: the HDR dump hook writes the artifact; GX_FRAMES bounds the run
    #   at the dump frame so no screenshot flag is needed.
    audit.run_scene("--reflectrow", env, width=WIDTH, height=HEIGHT, fps=None,
                    frames=FRAME, timeout=TIMEOUT, check=True,
                    extra_args=("--preset", "2"))
    if not os.path.isfile(target):
        raise RuntimeError("HDR dump was not produced")
    with open(target, "rb") as stream:
        magic, width, height, channels = struct.unpack("<8sIII", stream.read(20))
        payload = np.frombuffer(stream.read(), dtype="<f4")
    if magic != b"GXHDR1\x00\x00" or (width, height, channels) != (WIDTH, HEIGHT, 4):
        raise RuntimeError("invalid HDR dump header")
    expected = width * height * channels
    if payload.size != expected:
        raise RuntimeError("invalid HDR dump payload size")
    return payload.reshape(height, width, channels).copy()


def main():
    global OUT
    if len(sys.argv) > 1:
        OUT = os.path.abspath(sys.argv[1])
    os.makedirs(OUT, exist_ok=True)

    matte_bvh, matte_materials = oracle.make_scene(matte_fixture=True)
    ggx_bvh, ggx_materials = oracle.make_scene(matte_fixture=False)
    bounce_bvh, bounce_materials = oracle.make_scene(matte_fixture=True)
    fixtures = (
        ("M2 matte", "matte", oracle.render(matte_bvh, matte_materials,
                                             WIDTH, HEIGHT, 1, 0, shadows=False)),
        ("M3 GGX", "ggx", oracle.render_ggx(ggx_bvh, ggx_materials,
                                              WIDTH, HEIGHT)),
        ("M4 bounce", "bounce", oracle.render_environment_bounce(
            bounce_bvh, bounce_materials, WIDTH, HEIGHT)),
    )
    failures = []
    captures = {}
    print("oracle-hdr M2/M3/M4")
    with audit.settings_preserved():
        for label, fixture, cpu in fixtures:
            first = capture(fixture + "_first", fixture)
            repeat = capture(fixture + "_repeat", fixture)
            captures[fixture] = (first, repeat)
            if not np.isfinite(first).all() or not np.isfinite(repeat).all():
                failures.append(label + " raster HDR readback is non-finite")
                continue
            cpu *= EXPOSURE

            # glReadPixels is bottom-left origin; oracle images are top-left origin.
            gpu = first[::-1, :, :3]
            alpha = first[::-1, :, 3]
            mask = alpha < 0.99
            if int(mask.sum()) < 100:
                failures.append(label + " HDR readback has too few geometry pixels")
                continue

            delta = np.abs(cpu[mask] - gpu[mask])
            mae = float(delta.mean())
            p95 = float(np.percentile(delta, 95))
            maximum = float(delta.max())
            repeat_error = float(np.abs(first - repeat).max())
            print("  %s geometry pixels %d" % (label, int(mask.sum())))
            print("  %s linear HDR MAE %.6f" % (label, mae))
            print("  %s linear HDR P95 %.6f" % (label, p95))
            print("  %s linear HDR max %.6f" % (label, maximum))
            print("  %s repeat max error %.6f" % (label, repeat_error))
            if label == "M3 GGX":
                #opencode: sharp low-roughness GGX peaks are intentionally judged
                #   by band P95 rather than one subpixel firefly. The 0.10 max
                #   ceiling still catches unbounded lobes; the band gates catch
                #   broad roughness/material drift.
                if mae > 0.002:
                    failures.append(label + " CPU/raster linear-HDR MAE exceeds 0.002")
                if maximum > 0.10:
                    failures.append(label + " CPU/raster linear-HDR maximum exceeds 0.10")
                bands = ((0.00, 0.03), (0.03, 0.20), (0.20, 0.60), (0.60, 1.01))
                for low, high in bands:
                    band = mask & (alpha >= low) & (alpha < high)
                    if not band.any():
                        failures.append("%s roughness band %.2f-%.2f is empty"
                                        % (label, low, high))
                        continue
                    band_delta = np.abs(cpu[band] - gpu[band])
                    band_mae = float(band_delta.mean())
                    band_p95 = float(np.percentile(band_delta, 95))
                    print("  %s band %.2f-%.2f MAE %.6f P95 %.6f"
                          % (label, low, high, band_mae, band_p95))
                    if band_mae > 0.002 or band_p95 > 0.005:
                        failures.append("%s roughness band %.2f-%.2f exceeds error gate"
                                        % (label, low, high))
            elif label == "M4 bounce":
                #opencode: M4 is a fixed one-secondary cosine sample into the
                #   analytic environment. The direct matte row is the activation
                #   baseline; requiring positive secondary energy proves the hook
                #   did not silently fall through to the direct-only branch.
                matte_gpu = captures["matte"][0][::-1, :, :3]
                bounce_gpu = first[::-1, :, :3]
                direct_cpu = oracle.render(
                    bounce_bvh, bounce_materials, WIDTH, HEIGHT, 1, 0,
                    shadows=False) * EXPOSURE
                secondary_cpu = cpu - direct_cpu
                secondary_gpu = bounce_gpu - matte_gpu
                secondary_delta = np.abs(secondary_cpu[mask] - secondary_gpu[mask])
                secondary_mae = float(secondary_delta.mean())
                secondary_energy_cpu = float(secondary_cpu[mask].mean())
                secondary_energy_gpu = float(secondary_gpu[mask].mean())
                print("  %s secondary MAE %.6f" % (label, secondary_mae))
                print("  %s secondary energy CPU %.6f GPU %.6f"
                      % (label, secondary_energy_cpu, secondary_energy_gpu))
                if mae > 0.005 or p95 > 0.010 or maximum > 0.03:
                    failures.append(label + " CPU/raster linear-HDR error exceeds gate")
                if secondary_mae > 0.005:
                    failures.append(label + " secondary transport MAE exceeds 0.005")
                if secondary_energy_cpu <= 0.001 or secondary_energy_gpu <= 0.001:
                    failures.append(label + " secondary transport did not activate")
            else:
                if mae > 0.01:
                    failures.append(label + " CPU/raster linear-HDR MAE exceeds 0.01")
                if maximum > 0.02:
                    failures.append(label + " CPU/raster linear-HDR maximum exceeds 0.02")
            if repeat_error != 0.0:
                failures.append(label + " raster HDR capture is not deterministic")
    if failures:
        for failure in failures:
            print("FAIL: " + failure, file=sys.stderr)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
