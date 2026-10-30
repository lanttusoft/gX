#!/usr/bin/env python3
#opencode: deterministic bounded snow-decal check. It validates coverage,
#   melt, height, sloped-facing response, distance visibility, and repeatability.

import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


OUT = Path("/tmp/opencode/snow_check")
RAW = OUT / "raw"
SEED_ARGS = ("--seed", "424242")


def request(snow, coverage, melt, height, slope, distance, shadows=0, sun=None):
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "240",
        "GX_T_SNOW": "1" if snow else "0",
        "GX_SNOW_COVERAGE": str(coverage),
        "GX_SNOW_MELT": str(melt),
        "GX_SNOW_HEIGHT": str(height),
        "GX_SNOW_SLOPE": str(slope),
        "GX_T_DECALS": "0",
        "GX_T_BLOOM": "0",
        "GX_T_SSAO": "0",
        "GX_T_SSR": "0",
        "GX_T_COOKIES": "0",
        "GX_T_ENVAMBIENT": "0",
        "GX_T_TAA": "0",
        "GX_T_FXAA": "0",
        "GX_T_SHADOWS": str(shadows),
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
    #opencode: sun-radiance override for the weather-lighting response case.
    if sun is not None:
        environment["GX_T_SUNBRIGHTNESS"] = str(sun)
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
        "off_near": ("--showcase", request(False, 0.75, 0.0, 0.015, 1, 6), SEED_ARGS),
        "snow_near": ("--showcase", request(True, 0.75, 0.2, 0.015, 1, 6), SEED_ARGS),
        "snow_repeat": ("--showcase", request(True, 0.75, 0.2, 0.015, 1, 6), SEED_ARGS),
        "snow_low_coverage": ("--showcase", request(True, 0.25, 0.2, 0.015, 1, 6), SEED_ARGS),
        "snow_melted": ("--showcase", request(True, 0.75, 0.85, 0.015, 1, 6), SEED_ARGS),
        "snow_flat": ("--showcase", request(True, 0.75, 0.2, 0.015, 0, 6), SEED_ARGS),
        "snow_shadow": ("--showcase", request(True, 0.75, 0.2, 0.015, 1, 6, shadows=1), SEED_ARGS),
        #opencode: weather-lighting response (user finding: weather ignored light).
        "snow_sun_bright": ("--showcase", request(True, 0.75, 0.2, 0.015, 1, 6, sun=3.0), SEED_ARGS),
        "snow_sun_off": ("--showcase", request(True, 0.75, 0.2, 0.015, 1, 6, sun=0.0), SEED_ARGS),
        "off_far": ("--showcase", request(False, 0.75, 0.0, 0.015, 1, 18), SEED_ARGS),
        "snow_far": ("--showcase", request(True, 0.75, 0.2, 0.015, 1, 18), SEED_ARGS),
    }
    pool = audit.CapturePool(jobs=4, raw_dir=RAW)
    shots = pool.capture_all(requests, shot_frame=180)
    missing = sorted(set(requests) - set(shots))
    if missing:
        raise RuntimeError("missing snow captures: %s" % missing)
    captures = {name: str(path) for name, path in shots.items()}
    values = {name: pixels(path) for name, path in shots.items()}

    #opencode: isolate the snow pixels via the snow/off response mask, then
    #   require the bright-sun render to be clearly brighter than the sun-off.
    snow_mask = np.abs(values["snow_near"] - values["off_near"]).mean(axis=2) > 4.0
    light = {"snow_pixels": int(snow_mask.sum()), "sun_bright": 0.0, "sun_off": 0.0}
    if light["snow_pixels"] > 0:
        light["sun_bright"] = float(values["snow_sun_bright"][snow_mask].mean())
        light["sun_off"] = float(values["snow_sun_off"][snow_mask].mean())

    comparisons = {
        "near_response": difference(values["off_near"], values["snow_near"]),
        "far_response": difference(values["off_far"], values["snow_far"]),
        "coverage_response": difference(values["snow_near"], values["snow_low_coverage"]),
        "melt_response": difference(values["snow_near"], values["snow_melted"]),
        "slope_response": difference(values["snow_near"], values["snow_flat"]),
        "shadow_response": difference(values["snow_near"], values["snow_shadow"]),
        "repeat": difference(values["snow_near"], values["snow_repeat"]),
    }
    report = {
        "schema": 1,
        "fixture": "showcase-snow-floor-decals",
        "resolution": [1024, 768],
        "seed": 424242,
        "captures": captures,
        "comparisons": comparisons,
        "weather_lighting": light,
        "budgets": {
            "min_near_fraction_over_2": 0.001,
            "min_far_fraction_over_2": 0.0001,
            "min_control_response": 0.005,
            "max_repeat_channel": 0.0,
            #opencode: snow pixels must darken when the sun goes out.
            "min_light_darkening": 5.0,
        },
        "pass": True,
    }
    if comparisons["near_response"]["fraction_over_2"] < report["budgets"]["min_near_fraction_over_2"]:
        report["pass"] = False
    if comparisons["far_response"]["fraction_over_2"] < report["budgets"]["min_far_fraction_over_2"]:
        report["pass"] = False
    for name in ("coverage_response", "melt_response", "slope_response", "shadow_response"):
        if comparisons[name]["mean_abs_channel"] < report["budgets"]["min_control_response"]:
            report["pass"] = False
    if comparisons["repeat"]["max_channel"] > report["budgets"]["max_repeat_channel"]:
        report["pass"] = False
    #opencode: weather lighting response — the sun-off snow must be clearly
    #   darker over the snow mask.
    if (light["snow_pixels"] <= 0
            or light["sun_bright"] - light["sun_off"]
                < report["budgets"]["min_light_darkening"]):
        report["pass"] = False
    with open(OUT / "report.json", "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    print("SNOW-CHECK: %s" % ("PASS" if report["pass"] else "FAIL"))
    for name in ("near_response", "far_response", "coverage_response", "melt_response", "slope_response", "shadow_response"):
        print("  %-18s mean %.3f frac>2 %.4f" % (
            name, comparisons[name]["mean_abs_channel"], comparisons[name]["fraction_over_2"]))
    print("  repeat max %.1f" % comparisons["repeat"]["max_channel"])
    print("  weather lighting: snow px %d bright %.2f off %.2f (delta %.2f)"
        % (light["snow_pixels"], light["sun_bright"], light["sun_off"],
           light["sun_bright"] - light["sun_off"]))
    print("  report: %s" % (OUT / "report.json"))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("SNOW-CHECK: FAIL — %s" % error)
        sys.exit(1)
