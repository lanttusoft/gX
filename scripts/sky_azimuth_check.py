#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: sky-azimuth gate. Uses the camera-independent equirectangular sky
#   LUT to verify that the sun-side column is the brightest, the anti-solar
#   column carries the expected Rayleigh/Belt-of-Venus lobe, and the 90-degree
#   columns are the darkest. A sign flip or an over-strong anti-solar glow
#   fails the gate.

import argparse
import json
from pathlib import Path
import subprocess
import sys

import numpy as np

import audit_common as audit
from gfx_buffer_audit import read_dump


OUT = Path("/tmp/opencode/sky_azimuth_check")
FRAME = 40
WIDTH = 1024
HEIGHT = 768
SUN_LOW = "1,-0.2,0"     # sun toward -X, equirect u=0.5
SUN_HIGH = "-1,-0.2,0"   # sun toward +X, equirect u=0.0
SIDE_RATIO_MIN = 1.3
ANTI_RATIO_MIN = 1.1
ANTI_RATIO_MAX = 1.9
PEAK_TOLERANCE = 0.08


def capture(tag, sun_dir):
    directory = OUT / tag
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_T_SSR": "1",
        "GX_T_REFL": "0",
        "GX_T_SSAO": "0",
        "GX_T_BLOOM": "0",
        "GX_T_DOF": "0",
        "GX_T_MOTIONBLUR": "0",
        "GX_T_FXAA": "0",
        "GX_T_TAA": "0",
        "GX_T_SHADOWS": "0",
        "GX_T_POINT_SHADOWS": "0",
        "GX_T_COOKIES": "0",
        "GX_T_DECALS": "0",
        "GX_T_LENSFLARE": "0",
        "GX_T_RAIN": "0",
        "GX_T_SNOWFALL": "0",
        "GX_T_SPLAT": "0",
        "GX_T_PARTICLES": "0",
        "GX_T_GODRAY": "0",
        "GX_T_FOG": "0",
        "GX_T_HUD": "0",
        "GX_T_SUNDIR": sun_dir,
        "GX_SKY_SUNINTENSITY": "0",
        "GX_SKY_STARS": "0",
        "GX_SKY_TURBIDITY": "3",
        "GX_SKY_HORIZON": "1",
        "GX_RT_VIEW": "skyLut:1:0",
        "GX_RT_DUMP_DIR": str(directory),
        "GX_RT_DUMP_FRAME": str(FRAME),
        "GX_RT_DUMP_COUNT": "1",
    }, clean=True)
    result = audit.run_scene("--showcase", environment, width=WIDTH,
        height=HEIGHT, fps=60, frames=FRAME, timeout=120, check=False)
    log = audit.strip_ansi(result.stdout or "")
    (directory / "renderer.log").write_text(log)
    if result.returncode != 0:
        raise RuntimeError(f"{tag}: renderer exited {result.returncode}")
    paths = sorted(directory.glob("skyLut_*.gxrt"))
    if not paths:
        raise RuntimeError(f"{tag}: skyLut dump missing")
    return read_dump(paths[0])


def columns(image):
    if image.ndim != 3 or image.shape[2] < 3:
        raise RuntimeError(f"unexpected skyLut shape {image.shape}")
    return image[:, :, :3].mean(axis=2).mean(axis=0)


def smooth_circular(values, window=9):
    pad = window // 2
    padded = np.concatenate([values[-pad:], values, values[:pad]])
    kernel = np.ones(window) / float(window)
    return np.convolve(padded, kernel, mode="same")[pad:-pad]


def measure(tag, sun_dir, expected_sun_u):
    image = capture(tag, sun_dir)
    column = columns(image)
    smooth = smooth_circular(column)
    peak_u = float(np.argmax(smooth) / len(column))
    sun_u = expected_sun_u % 1.0
    anti_u = (sun_u + 0.5) % 1.0
    side_u = (sun_u + 0.25) % 1.0
    side_u2 = (sun_u + 0.75) % 1.0
    at = lambda u: float(column[int(round(u * len(column))) % len(column)])
    sun = at(sun_u)
    anti = at(anti_u)
    side = (at(side_u) + at(side_u2)) * 0.5
    peak_delta = min(abs(peak_u - sun_u), 1.0 - abs(peak_u - sun_u))
    return {
        "tag": tag,
        "sun_u": sun_u,
        "anti_u": anti_u,
        "peak_u": peak_u,
        "peak_delta": peak_delta,
        "sun": sun,
        "anti": anti,
        "side": side,
        "sun_side_ratio": sun / max(side, 1e-6),
        "anti_side_ratio": anti / max(side, 1e-6),
        "anti_sun_ratio": anti / max(sun, 1e-6),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    with audit.settings_preserved():
        rows = [
            measure("sun_left", SUN_LOW, 0.5),
            measure("sun_right", SUN_HIGH, 0.0),
        ]
    for row in rows:
        print(f"{row['tag']:9s} sun={row['sun']:.4f} anti={row['anti']:.4f} "
              f"side={row['side']:.4f} peak={row['peak_u']:.3f} "
              f"sun/side={row['sun_side_ratio']:.2f} "
              f"anti/side={row['anti_side_ratio']:.2f}")
    for row in rows:
        if row["peak_delta"] > PEAK_TOLERANCE:
            raise RuntimeError(
                f"{row['tag']}: sky peak {row['peak_u']:.3f} is not at the sun "
                f"({row['sun_u']:.3f})")
        if row["sun_side_ratio"] < SIDE_RATIO_MIN:
            raise RuntimeError(
                f"{row['tag']}: sun-side glow too weak "
                f"({row['sun_side_ratio']:.2f}x side)")
        if not (ANTI_RATIO_MIN <= row["anti_side_ratio"] <= ANTI_RATIO_MAX):
            raise RuntimeError(
                f"{row['tag']}: anti-solar Rayleigh lobe outside "
                f"{ANTI_RATIO_MIN:.1f}-{ANTI_RATIO_MAX:.1f}x side "
                f"({row['anti_side_ratio']:.2f})")
        if row["anti"] >= row["sun"]:
            raise RuntimeError(
                f"{row['tag']}: anti-solar column is not below the sun")
    if args.report:
        args.report.write_text(json.dumps({
            "frame": FRAME,
            "sun_low": SUN_LOW,
            "sun_high": SUN_HIGH,
            "side_ratio_min": SIDE_RATIO_MIN,
            "anti_ratio_min": ANTI_RATIO_MIN,
            "anti_ratio_max": ANTI_RATIO_MAX,
            "peak_tolerance": PEAK_TOLERANCE,
            "rows": rows,
        }, indent=2) + "\n")
    print("SKY_AZIMUTH: PASS — sun-side peak, expected Rayleigh anti-lobe, darkest sides")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"SKY_AZIMUTH: FAIL — {error}", file=sys.stderr)
        sys.exit(1)
