#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: deterministic CPU falling-snow check. It validates on/off response,
#   wind and rate control arms, distance visibility, accumulation reuse, and
#   exact repeatability.

import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


OUT = Path("/tmp/opencode/snowfall_check")
RAW = OUT / "raw"
SEED_ARGS = ("--seed", "424242")


def request(snowfall, seed, rate, wind, accumulation, distance,
            gust=None, turbulence=None, size=None, alpha=None):
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "240",
        "GX_T_SNOWFALL": "1" if snowfall else "0",
        "GX_SNOWFALL_SEED": str(seed),
        "GX_SNOWFALL_RATE": str(rate),
        "GX_SNOWFALL_WIND": str(wind),
        "GX_T_SNOW": "1" if accumulation else "0",
        "GX_SNOW_COVERAGE": "0.75",
        "GX_SNOW_MELT": "0.20",
        "GX_SNOW_HEIGHT": "0.015",
        "GX_T_DECALS": "0",
        "GX_T_BLOOM": "0",
        "GX_T_SSAO": "0",
        "GX_T_SSR": "0",
        "GX_T_COOKIES": "0",
        "GX_T_ENVAMBIENT": "0",
        "GX_T_TAA": "0",
        "GX_T_FXAA": "0",
        "GX_T_SHADOWS": "0",
        "GX_T_POINT_SHADOWS": "0",
        "GX_T_MOTIONBLUR": "0",
        "GX_T_PARTICLES": "0",
        "GX_T_LENSFLARE": "0",
        "GX_T_GODRAY": "0",
        "GX_T_FOG": "1",
        "GX_FOG_DENSITY": "0",
        "GX_CAM_YAW": "30",
        "GX_CAM_PITCH": "25",
        "GX_CAM_DIST": str(distance),
        "GX_CAM_SPIN": "0",
    }, clean=True)
    #opencode: profile hooks are opt-in; leaving them unset keeps the fixture
    #   byte-identical to the pre-profile snowfall cases.
    if gust is not None:
        environment["GX_SNOWFALL_GUST"] = str(gust)
    if turbulence is not None:
        environment["GX_SNOWFALL_TURBULENCE"] = str(turbulence)
    if size is not None:
        environment["GX_SNOWFALL_SIZE"] = str(size)
    if alpha is not None:
        environment["GX_SNOWFALL_ALPHA"] = str(alpha)
    return environment


def pixels(path):
    value = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    if not np.isfinite(value).all():
        raise RuntimeError("non-finite pixels in %s" % path)
    return value


def difference(first, second):
    delta = np.abs(first[96:] - second[96:])
    return {
        "mean_abs_channel": float(delta.mean()),
        "fraction_over_2": float((delta.mean(axis=2) > 2.0).mean()),
        "max_channel": float(delta.max()),
    }


def main():
    if not audit.BIN.is_file():
        raise RuntimeError("missing build binary: %s" % audit.BIN)
    OUT.mkdir(parents=True, exist_ok=True)

    requests = {
        "off_near": ("--showcase", request(False, 1337, 448, 0.35, False, 6), SEED_ARGS),
        "fall_near": ("--showcase", request(True, 1337, 448, 0.35, False, 6), SEED_ARGS),
        "fall_repeat": ("--showcase", request(True, 1337, 448, 0.35, False, 6), SEED_ARGS),
        "fall_windy": ("--showcase", request(True, 1337, 448, 1.80, False, 6), SEED_ARGS),
        "fall_sparse": ("--showcase", request(True, 1337, 96, 0.35, False, 6), SEED_ARGS),
        "fall_combined": ("--showcase", request(True, 1337, 448, 0.35, True, 6), SEED_ARGS),
        "fall_default": ("--showcase", request(
            True, 1337, 448, 0.35, False, 6,
            gust=0.0, turbulence=0.0, size=1.0, alpha=1.0), SEED_ARGS),
        "fall_gust": ("--showcase", request(
            True, 1337, 448, 0.35, False, 6, gust=1.5), SEED_ARGS),
        "fall_turbulence": ("--showcase", request(
            True, 1337, 448, 0.35, False, 6, turbulence=2.0), SEED_ARGS),
        "fall_size": ("--showcase", request(
            True, 1337, 448, 0.35, False, 6, size=2.0), SEED_ARGS),
        "fall_alpha": ("--showcase", request(
            True, 1337, 448, 0.35, False, 6, alpha=0.35), SEED_ARGS),
        "fall_profile": ("--showcase", request(
            True, 1337, 448, 0.35, False, 6,
            gust=1.5, turbulence=2.0, size=2.0, alpha=0.35), SEED_ARGS),
        "fall_profile_repeat": ("--showcase", request(
            True, 1337, 448, 0.35, False, 6,
            gust=1.5, turbulence=2.0, size=2.0, alpha=0.35), SEED_ARGS),
        "off_far": ("--showcase", request(False, 1337, 448, 0.35, False, 18), SEED_ARGS),
        "fall_far": ("--showcase", request(True, 1337, 448, 0.35, False, 18), SEED_ARGS),
    }
    pool = audit.CapturePool(jobs=4, raw_dir=RAW)
    shots = pool.capture_all(requests, shot_frame=180)
    missing = sorted(set(requests) - set(shots))
    if missing:
        raise RuntimeError("missing snowfall captures: %s" % missing)
    captures = {name: str(path) for name, path in shots.items()}
    values = {name: pixels(path) for name, path in shots.items()}

    comparisons = {
        "near_response": difference(values["off_near"], values["fall_near"]),
        "far_response": difference(values["off_far"], values["fall_far"]),
        "wind_response": difference(values["fall_near"], values["fall_windy"]),
        "rate_response": difference(values["fall_near"], values["fall_sparse"]),
        "combined_response": difference(values["fall_near"], values["fall_combined"]),
        "repeat": difference(values["fall_near"], values["fall_repeat"]),
        "default_neutral": difference(values["fall_near"], values["fall_default"]),
        "gust_response": difference(values["fall_near"], values["fall_gust"]),
        "turbulence_response": difference(values["fall_near"], values["fall_turbulence"]),
        "size_response": difference(values["fall_near"], values["fall_size"]),
        "alpha_response": difference(values["fall_near"], values["fall_alpha"]),
        "profile_response": difference(values["fall_near"], values["fall_profile"]),
        "profile_repeat": difference(values["fall_profile"], values["fall_profile_repeat"]),
    }
    report = {
        "schema": 1,
        "fixture": "showcase-cpu-snowfall",
        "resolution": [1024, 768],
        "seed": 424242,
        "snowfall": {"seed": 1337, "rate": 448, "wind": 0.35},
        "profile": {"gust": 1.5, "turbulence": 2.0,
                    "size": 2.0, "alpha": 0.35},
        "captures": captures,
        "comparisons": comparisons,
        "budgets": {
            "min_near_fraction_over_2": 0.0005,
            "min_far_fraction_over_2": 0.0001,
            "min_control_response": 0.005,
            "max_repeat_channel": 0.0,
        },
        "pass": True,
    }
    if comparisons["near_response"]["fraction_over_2"] < report["budgets"]["min_near_fraction_over_2"]:
        report["pass"] = False
    if comparisons["far_response"]["fraction_over_2"] < report["budgets"]["min_far_fraction_over_2"]:
        report["pass"] = False
    for name in ("wind_response", "rate_response", "combined_response"):
        if comparisons[name]["mean_abs_channel"] < report["budgets"]["min_control_response"]:
            report["pass"] = False
    for name in ("gust_response", "turbulence_response",
                 "size_response", "alpha_response", "profile_response"):
        if comparisons[name]["mean_abs_channel"] < report["budgets"]["min_control_response"]:
            report["pass"] = False
    for name in ("repeat", "default_neutral", "profile_repeat"):
        if comparisons[name]["max_channel"] > report["budgets"]["max_repeat_channel"]:
            report["pass"] = False
    with open(OUT / "report.json", "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    print("SNOWFALL-CHECK: %s" % ("PASS" if report["pass"] else "FAIL"))
    for name in ("near_response", "far_response", "wind_response",
                 "rate_response", "combined_response", "gust_response",
                 "turbulence_response", "size_response", "alpha_response",
                 "profile_response"):
        print("  %-20s mean %.3f frac>2 %.4f" % (
            name, comparisons[name]["mean_abs_channel"], comparisons[name]["fraction_over_2"]))
    print("  repeat max %.1f" % comparisons["repeat"]["max_channel"])
    print("  default-neutral max %.1f" % comparisons["default_neutral"]["max_channel"])
    print("  profile repeat max %.1f" % comparisons["profile_repeat"]["max_channel"])
    print("  report: %s" % (OUT / "report.json"))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("SNOWFALL-CHECK: FAIL — %s" % error)
        sys.exit(1)
