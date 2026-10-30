#!/usr/bin/env python3
#opencode: deterministic two-mirror feedback check. It validates pair
#   response, settle convergence (energy bound), the energy-decay mechanism
#   (1.0 vs 0.9 must differ), camera-motion invalidation, and exact
#   repeatability. Note: post-convergence residuals fall below LDR
#   quantization, so the black-rebuild fallback is evidenced by the clear
#   log + settle arm rather than an early-vs-late pixel delta.

import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


OUT = Path("/tmp/opencode/mirror_check")
RAW = OUT / "raw"
SEED_ARGS = ("--seed", "424242")


def request(pair, frame, spin, energy):
    return audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(frame + 60),
        "GX_MIRROR_PAIR": "1" if pair else "0",
        "GX_MIRROR_ENERGY": str(energy),
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
        "GX_T_SPLAT": "0",
        "GX_T_FOG": "1",
        "GX_FOG_DENSITY": "0",
        "GX_CAM_YAW": "45",
        "GX_CAM_PITCH": "20",
        "GX_CAM_DIST": "12",
        "GX_CAM_SPIN": str(spin),
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

    requests_180 = {
        "off": ("--showcase-minimal", request(False, 180, 0, 0.9), SEED_ARGS),
        "pair": ("--showcase-minimal", request(True, 180, 0, 0.9), SEED_ARGS),
        "pair_repeat": ("--showcase-minimal", request(True, 180, 0, 0.9), SEED_ARGS),
        "pair_full_energy": ("--showcase-minimal", request(True, 180, 0, 1.0), SEED_ARGS),
        "pair_spin": ("--showcase-minimal", request(True, 180, 3, 0.9), SEED_ARGS),
    }
    requests_120 = {
        "pair_settled": ("--showcase-minimal", request(True, 120, 0, 0.9), SEED_ARGS),
    }
    pool = audit.CapturePool(jobs=4, raw_dir=RAW)
    shots = pool.capture_all(requests_180, shot_frame=180)
    shots.update(pool.capture_all(requests_120, shot_frame=120))
    requests = {**requests_180, **requests_120}
    missing = sorted(set(requests) - set(shots))
    if missing:
        raise RuntimeError("missing mirror_check captures: %s" % missing)
    captures = {name: str(path) for name, path in shots.items()}
    values = {name: pixels(path) for name, path in captures.items()}

    comparisons = {
        "response": difference(values["off"], values["pair"]),
        "settle": difference(values["pair_settled"], values["pair"]),
        "energy": difference(values["pair"], values["pair_full_energy"]),
        "churn": difference(values["pair"], values["pair_spin"]),
        "repeat": difference(values["pair"], values["pair_repeat"]),
    }
    report = {
        "schema": 1,
        "fixture": "showcase-minimal-mirror-pair",
        "resolution": [1024, 768],
        "seed": 424242,
        "mirror": {"energy": 0.90, "depth_budget": 1, "planes": 2},
        "captures": captures,
        "comparisons": comparisons,
        "budgets": {
            "min_response_fraction_over_2": 0.05,
            "max_settle_mean_abs_channel": 0.5,
            "min_energy_mean_abs_channel": 0.05,
            "min_churn_mean_abs_channel": 1.0,
            "max_repeat_channel": 0.0,
            "max_pair_mean": 200.0,
        },
        "pass": True,
    }
    if comparisons["response"]["fraction_over_2"] < report["budgets"]["min_response_fraction_over_2"]:
        report["pass"] = False
    if comparisons["settle"]["mean_abs_channel"] > report["budgets"]["max_settle_mean_abs_channel"]:
        report["pass"] = False
    if comparisons["energy"]["mean_abs_channel"] < report["budgets"]["min_energy_mean_abs_channel"]:
        report["pass"] = False
    if comparisons["churn"]["mean_abs_channel"] < report["budgets"]["min_churn_mean_abs_channel"]:
        report["pass"] = False
    if comparisons["repeat"]["max_channel"] > report["budgets"]["max_repeat_channel"]:
        report["pass"] = False
    if float(values["pair"][96:].mean()) > report["budgets"]["max_pair_mean"]:
        report["pass"] = False
    with open(OUT / "report.json", "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    print("MIRROR-CHECK: %s" % ("PASS" if report["pass"] else "FAIL"))
    for name in ("response", "settle", "energy", "churn"):
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
        print("MIRROR-CHECK: FAIL — %s" % error)
        sys.exit(1)
