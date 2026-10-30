#!/usr/bin/env python3
#opencode: deterministic reflection-map probe check. The minimal scene with
#   explicit cubemap consumers attributes every probe pixel: grid response
#   proves bake+projection engagement, parity bounds guard against corrupt
#   bakes, refresh cadence must be bit-stable on a static scene, and repeats
#   must match exactly.

import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


OUT = Path("/tmp/opencode/probe_check")
RAW = OUT / "raw"
SEED_ARGS = ("--seed", "424242")


def request(grid, blend, refresh, distance):
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "240",
        "GX_PROBE_GRID": "1" if grid else "0",
        "GX_PROBE_SIZE": "128",
        "GX_PROBE_BLEND": str(blend),
        "GX_CUBEMAP_TEST": "1",
        "GX_T_BLOOM": "0",
        "GX_T_SSAO": "0",
        "GX_T_SSR": "0",
        "GX_T_SSRAUX": "0",
        "GX_T_COOKIES": "0",
        "GX_T_ENVAMBIENT": "0",
        "GX_T_TAA": "0",
        "GX_T_FXAA": "0",
        "GX_T_SHADOWS": "1",
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
    if refresh > 0:
        environment["GX_PROBE_REFRESH"] = str(refresh)
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
        "off_near": ("--showcase-minimal", request(False, 1, 0, 6), SEED_ARGS),
        "grid_near": ("--showcase-minimal", request(True, 1, 0, 6), SEED_ARGS),
        "grid_repeat": ("--showcase-minimal", request(True, 1, 0, 6), SEED_ARGS),
        "grid_blend": ("--showcase-minimal", request(True, 0, 0, 6), SEED_ARGS),
        "grid_refresh": ("--showcase-minimal", request(True, 1, 60, 6), SEED_ARGS),
        "off_far": ("--showcase-minimal", request(False, 1, 0, 14), SEED_ARGS),
        "grid_far": ("--showcase-minimal", request(True, 1, 0, 14), SEED_ARGS),
    }
    pool = audit.CapturePool(jobs=4, raw_dir=RAW)
    shots = pool.capture_all(requests, shot_frame=180)
    missing = sorted(set(requests) - set(shots))
    if missing:
        raise RuntimeError("missing probe captures: %s" % missing)
    captures = {name: str(path) for name, path in shots.items()}
    values = {name: pixels(path) for name, path in shots.items()}

    comparisons = {
        "near_response": difference(values["off_near"], values["grid_near"]),
        "far_response": difference(values["off_far"], values["grid_far"]),
        "blend_response": difference(values["grid_near"], values["grid_blend"]),
        "refresh_stability": difference(values["grid_near"], values["grid_refresh"]),
        "repeat": difference(values["grid_near"], values["grid_repeat"]),
    }
    report = {
        "schema": 1,
        "fixture": "showcase-minimal-cubemap-probe",
        "resolution": [1024, 768],
        "seed": 424242,
        "probes": {"count": 4, "size": 128, "positions": "x/z +-3, y 1.5"},
        "memory_budget_bytes": {
            "faces": 4 * 6 * 128 * 128 * 8,
            "depth_rbo": 4 * 128 * 128 * 4,
        },
        "captures": captures,
        "comparisons": comparisons,
        "budgets": {
            "min_near_fraction_over_2": 0.0002,
            "max_near_mean_abs_channel": 0.5,
            #opencode: re-baselined 2026-10-14 — 291c193's brighter sky (the
            #   same contrast change as the horizon zenith) raised the grazing
            #   slab highlight: probes-on reflects the local bake (scene
            #   ground), probes-off the environment (bright sky). Measured
            #   max 146; the ceiling still catches a saturating corrupt bake.
            "max_near_channel": 192.0,
            "min_far_fraction_over_2": 0.0005,
            "max_blend_mean_abs_channel": 0.5,
            "max_refresh_mean_abs_channel": 0.0,
            "max_repeat_channel": 0.0,
        },
        "pass": True,
    }
    near = comparisons["near_response"]
    if near["fraction_over_2"] < report["budgets"]["min_near_fraction_over_2"]:
        report["pass"] = False
    if near["mean_abs_channel"] > report["budgets"]["max_near_mean_abs_channel"]:
        report["pass"] = False
    if near["max_channel"] > report["budgets"]["max_near_channel"]:
        report["pass"] = False
    if comparisons["far_response"]["fraction_over_2"] < report["budgets"]["min_far_fraction_over_2"]:
        report["pass"] = False
    if comparisons["blend_response"]["mean_abs_channel"] > report["budgets"]["max_blend_mean_abs_channel"]:
        report["pass"] = False
    if comparisons["refresh_stability"]["mean_abs_channel"] > report["budgets"]["max_refresh_mean_abs_channel"]:
        report["pass"] = False
    if comparisons["repeat"]["max_channel"] > report["budgets"]["max_repeat_channel"]:
        report["pass"] = False
    with open(OUT / "report.json", "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    print("PROBE-CHECK: %s" % ("PASS" if report["pass"] else "FAIL"))
    for name in ("near_response", "far_response", "blend_response", "refresh_stability"):
        print("  %-18s mean %.4f frac>2 %.4f max %.1f" % (
            name, comparisons[name]["mean_abs_channel"],
            comparisons[name]["fraction_over_2"], comparisons[name]["max_channel"]))
    print("  repeat max %.1f" % comparisons["repeat"]["max_channel"])
    print("  report: %s" % (OUT / "report.json"))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("PROBE-CHECK: FAIL — %s" % error)
        sys.exit(1)
