#!/usr/bin/env python3
#opencode: deterministic gaussian splat check. It validates cloud response,
#   seed and count control arms, distance LOD, and exact repeatability.

import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


OUT = Path("/tmp/opencode/splat_check")
RAW = OUT / "raw"
SEED_ARGS = ("--seed", "424242")


def request(splat, seed, count, distance):
    return audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "240",
        "GX_T_SPLAT": "1" if splat else "0",
        "GX_SPLAT_SEED": str(seed),
        "GX_SPLAT_COUNT": str(count),
        "GX_T_BLOOM": "0",
        "GX_T_SSAO": "0",
        "GX_T_SSR": "0",
        "GX_T_SSRAUX": "0",
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
        "GX_T_DOF": "0",
        "GX_T_SNOW": "0",
        "GX_T_SNOWFALL": "0",
        "GX_T_RAIN": "0",
        "GX_T_FOGSPRITE": "0",
        "GX_T_FOG": "1",
        "GX_FOG_DENSITY": "0",
        "GX_CAM_YAW": "30",
        "GX_CAM_PITCH": "25",
        "GX_CAM_DIST": str(distance),
        "GX_CAM_SPIN": "0",
    }, clean=True)


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
        "off_near": ("--showcase", request(False, 777, 256, 10), SEED_ARGS),
        "splat_near": ("--showcase", request(True, 777, 256, 10), SEED_ARGS),
        "splat_repeat": ("--showcase", request(True, 777, 256, 10), SEED_ARGS),
        "splat_reseed": ("--showcase", request(True, 31337, 256, 10), SEED_ARGS),
        "splat_sparse": ("--showcase", request(True, 777, 64, 10), SEED_ARGS),
        "off_far": ("--showcase", request(False, 777, 256, 20), SEED_ARGS),
        "splat_far": ("--showcase", request(True, 777, 256, 20), SEED_ARGS),
    }
    pool = audit.CapturePool(jobs=4, raw_dir=RAW)
    shots = pool.capture_all(requests, shot_frame=180)
    missing = sorted(set(requests) - set(shots))
    if missing:
        raise RuntimeError("missing splat_check captures: %s" % missing)
    captures = {name: str(path) for name, path in shots.items()}
    values = {name: pixels(path) for name, path in captures.items()}

    comparisons = {
        "near_response": difference(values["off_near"], values["splat_near"]),
        "far_response": difference(values["off_far"], values["splat_far"]),
        "seed_response": difference(values["splat_near"], values["splat_reseed"]),
        "count_response": difference(values["splat_near"], values["splat_sparse"]),
        "repeat": difference(values["splat_near"], values["splat_repeat"]),
    }
    report = {
        "schema": 1,
        "fixture": "showcase-splat-cloud",
        "resolution": [1024, 768],
        "seed": 424242,
        "splat": {"seed": 777, "count": 256, "sigma": 0.06, "center": [0.0, 2.2, -4.5], "radius": 1.0},
        "memory_budget_bytes": {
            "cloud": 1024 * 10 * 4,
            "projection": 1024 * 9 * 4,
            "upload": 1024 * 6 * 16 * 4,
        },
        "captures": captures,
        "comparisons": comparisons,
        "budgets": {
            "min_near_fraction_over_2": 0.001,
            "max_near_fraction_over_2": 0.60,
            "min_far_fraction_over_2": 0.0001,
            "min_control_response": 0.005,
            "max_repeat_channel": 0.0,
        },
        "pass": True,
    }
    near_frac = comparisons["near_response"]["fraction_over_2"]
    if near_frac < report["budgets"]["min_near_fraction_over_2"]:
        report["pass"] = False
    if near_frac > report["budgets"]["max_near_fraction_over_2"]:
        report["pass"] = False
    if comparisons["far_response"]["fraction_over_2"] < report["budgets"]["min_far_fraction_over_2"]:
        report["pass"] = False
    for name in ("seed_response", "count_response"):
        if comparisons[name]["mean_abs_channel"] < report["budgets"]["min_control_response"]:
            report["pass"] = False
    if comparisons["repeat"]["max_channel"] > report["budgets"]["max_repeat_channel"]:
        report["pass"] = False
    with open(OUT / "report.json", "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    print("SPLAT-CHECK: %s" % ("PASS" if report["pass"] else "FAIL"))
    for name in ("near_response", "far_response", "seed_response", "count_response"):
        print("  %-18s mean %.3f frac>2 %.4f max %.1f" % (
            name, comparisons[name]["mean_abs_channel"],
            comparisons[name]["fraction_over_2"], comparisons[name]["max_channel"]))
    print("  repeat max %.1f" % comparisons["repeat"]["max_channel"])
    print("  report: %s" % (OUT / "report.json"))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("SPLAT-CHECK: FAIL — %s" % error)
        sys.exit(1)
