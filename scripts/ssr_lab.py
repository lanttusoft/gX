#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: deterministic SSR parameter lab. Captures one fixed scene/camera
#   baseline, sweeps one variable at a time, and reports reflection signal,
#   high-frequency structure, and optional planar-oracle error.

import argparse
import json
import os
import shutil
import sys

import numpy as np

import audit_common as audit


VARIABLES = {
    "step": ("GX_T_STEP", float),
    "maxsteps": ("GX_T_MAXSTEPS", int),
    "maxdist": ("GX_T_MAXDIST", float),
    "maxlod": ("GX_T_MAXLOD", float),
    "bias": ("GX_T_BIAS", float),
    "opacity": ("GX_T_OPACITY", float),
    "edgefade": ("GX_T_EDGEFADE", float),
    "maxdepth": ("GX_T_MAXDEPTH", float),
    "worldgap": ("GX_T_WORLDGAP", float),
    "finaldepth": ("GX_T_FINALDEPTH", float),
    "hitfacing": ("GX_T_HITFACING", float),
    "bounces": ("GX_T_BOUNCES", int),
}

COMMON_ENV = {
    "GX_T_SSR": "1",
    "GX_T_REFL": "0",
    "GX_T_SSRAUX": "0",
    "GX_PLANAR_AUX": "0",
    "GX_T_BLOOM": "0",
    "GX_T_TAA": "0",
    "GX_T_FXAA": "0",
    "GX_T_SSAO": "0",
    "GX_T_SHADOWS": "0",
    "GX_T_POINT_SHADOWS": "0",
    "GX_T_GODRAY": "0",
    "GX_T_COOKIES": "0",
    "GX_T_PARTICLES": "0",
    "GX_T_LENSFLARE": "0",
    "GX_T_DOF": "0",
    "GX_T_MOTIONBLUR": "0",
    "GX_T_DECALS": "0",
    "GX_T_WIREFRAME": "0",
    "GX_T_ENVAMBIENT": "0",
    "GX_T_FOG": "0",
    "GX_SCREENSHOT_DIR": "/tmp/ssr_lab/raw",
}


def parse_sweep(spec):
    if "=" not in spec:
        raise ValueError("sweep must be NAME=VALUE,VALUE")
    name, values = spec.split("=", 1)
    if name not in VARIABLES:
        raise ValueError(f"unknown SSR variable '{name}'")
    converter = VARIABLES[name][1]
    parsed = [converter(value) for value in values.split(",") if value]
    if not parsed:
        raise ValueError(f"empty sweep for '{name}'")
    return name, parsed


def fresh_shot(directory, before):
    fresh = [name for name in os.listdir(directory)
             if name.startswith("gXss_") and name not in before]
    if not fresh:
        raise RuntimeError("capture produced no fresh screenshot")
    return max(fresh, key=lambda name: os.path.getmtime(os.path.join(directory, name)))


def capture(binary, scene_flag, env, output_path, timeout):
    raw_dir = env["GX_SCREENSHOT_DIR"]
    os.makedirs(raw_dir, exist_ok=True)
    before = {name for name in os.listdir(raw_dir) if name.startswith("gXss_")}
    result = audit.run_scene(scene_flag, env, binary=binary, width=None,
                             height=None, fps=None, screenshot_dir=raw_dir,
                             shot_frame=None, timeout=timeout, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"capture exited with status {result.returncode}")
    source = os.path.join(raw_dir, fresh_shot(raw_dir, before))
    shutil.copyfile(source, output_path)


def load_rgb(path):
    return audit.load_rgb(path)


def high_frequency(gray):
    horizontal = np.abs(gray[:, 2:] - 2.0 * gray[:, 1:-1] + gray[:, :-2])
    vertical = np.abs(gray[2:, :] - 2.0 * gray[1:-1, :] + gray[:-2, :])
    return 0.5 * (horizontal[1:-1, :] + vertical[:, 1:-1])


def image_metrics(image, baseline, oracle):
    if image.shape != baseline.shape:
        raise RuntimeError("baseline and variant dimensions differ")
    height = image.shape[0]
    roi = (slice(int(height * 0.30), height - 2), slice(1, image.shape[1] - 1))
    delta = np.abs(image - baseline).mean(axis=2)
    gray = image.mean(axis=2)
    base_gray = baseline.mean(axis=2)
    hf = high_frequency(gray)
    base_hf = high_frequency(base_gray)
    edge_horizontal = np.abs(gray[:, 1:] - gray[:, :-1]) > 12.0
    edge_vertical = np.abs(gray[1:, :] - gray[:-1, :]) > 12.0
    edge_roi = (slice(int(height * 0.30), height - 2), slice(1, image.shape[1] - 1))
    edge_density = 0.5 * (edge_horizontal[edge_roi].mean() + edge_vertical[edge_roi].mean())
    result = {
        "reflection_delta_mean": round(float(delta[roi].mean()), 4),
        "reflection_delta_p95": round(float(np.percentile(delta[roi], 95)), 4),
        "reflection_coverage_pct": round(float((delta[roi] > 4.0).mean() * 100.0), 3),
        "hf_energy": round(float(hf[roi].mean()), 4),
        "hf_delta": round(float((hf[roi] - base_hf[roi]).mean()), 4),
        "edge_density_pct": round(float(edge_density * 100.0), 3),
    }
    if oracle is not None:
        if image.shape != oracle.shape:
            raise RuntimeError("oracle and variant dimensions differ")
        oracle_delta = np.abs(image - oracle).mean(axis=2)
        result["oracle_error_mean"] = round(float(oracle_delta[roi].mean()), 4)
        result["oracle_error_p95"] = round(float(np.percentile(oracle_delta[roi], 95)), 4)
    return result


def scene_config(scene, yaw, pitch, distance):
    flags = {
        "showcase": "--showcase",
        "reflectrow": "--reflectrow",
        "minimal": "--showcase-minimal",
    }
    env = audit.scene_env(COMMON_ENV, clean=True)
    env["GX_CAM_YAW"] = str(yaw)
    env["GX_CAM_PITCH"] = str(pitch)
    env["GX_CAM_DIST"] = str(distance)
    return flags[scene], env


def print_results(results):
    columns = ["name", "value", "reflection_delta_mean", "reflection_coverage_pct",
               "hf_delta", "edge_density_pct", "oracle_error_mean"]
    print(" ".join(f"{column:>23}" for column in columns))
    print("-" * (24 * len(columns)))
    for row in results:
        print(" ".join(f"{str(row.get(column, '-')):>23}" for column in columns))


def main():
    parser = argparse.ArgumentParser(description="gX SSR parameter lab")
    parser.add_argument("--scene", choices=("showcase", "reflectrow", "minimal"),
                        default="showcase")
    parser.add_argument("--sweep", action="append", default=[], metavar="NAME=V1,V2",
                        help="sweep one variable; repeat for independent sweeps")
    parser.add_argument("--binary", default="./build/gX_build")
    parser.add_argument("--output", default="/tmp/ssr_lab")
    parser.add_argument("--json", help="write the result table to this path")
    parser.add_argument("--yaw", type=float, default=45.0)
    parser.add_argument("--pitch", type=float, default=25.0)
    parser.add_argument("--distance", type=float)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    try:
        sweeps = [parse_sweep(spec) for spec in args.sweep]
    except ValueError as error:
        parser.error(str(error))
    if not sweeps:
        sweeps = [("step", [0.25, 0.5, 1.0]),
                  ("maxlod", [0.0, 1.0, 2.5]),
                  ("maxsteps", [16, 32, 64])]

    os.makedirs(args.output, exist_ok=True)
    raw_dir = os.path.join(args.output, "raw")
    distance = args.distance
    if distance is None:
        distance = 6.0 if args.scene in ("reflectrow", "minimal") else 9.0
    scene_flag, base_env = scene_config(args.scene, args.yaw, args.pitch, distance)
    base_env["GX_SCREENSHOT_DIR"] = raw_dir

    baseline_env = dict(base_env)
    baseline_env["GX_T_SSR"] = "0"
    baseline_env["GX_T_REFL"] = "0" if args.scene != "minimal" else "1"
    baseline_path = os.path.join(args.output, "baseline.png")
    with audit.settings_preserved():
        capture(args.binary, scene_flag, baseline_env, baseline_path, args.timeout)
        baseline = load_rgb(baseline_path)

        oracle = None
        if args.scene == "minimal":
            oracle_env = dict(base_env)
            oracle_env["GX_T_SSR"] = "0"
            oracle_env["GX_T_REFL"] = "1"
            oracle_path = os.path.join(args.output, "oracle.png")
            capture(args.binary, "--showcase-planar", oracle_env, oracle_path, args.timeout)
            oracle = load_rgb(oracle_path)

        results = []
        for name, values in sweeps:
            variable, _converter = VARIABLES[name]
            for value in values:
                env = dict(base_env)
                env[variable] = str(value)
                tag = f"{name}_{str(value).replace('.', 'p').replace('-', 'm')}"
                output_path = os.path.join(args.output, f"{tag}.png")
                capture(args.binary, scene_flag, env, output_path, args.timeout)
                row = {"name": name, "value": value, "path": output_path}
                row.update(image_metrics(load_rgb(output_path), baseline, oracle))
                results.append(row)

    print_results(results)
    payload = {
        "scene": args.scene,
        "camera": {"yaw": args.yaw, "pitch": args.pitch, "distance": distance},
        "baseline": baseline_path,
        "oracle": os.path.join(args.output, "oracle.png") if oracle is not None else None,
        "results": results,
    }
    if args.json:
        with open(args.json, "w", encoding="utf-8") as stream:
            json.dump(payload, stream, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
