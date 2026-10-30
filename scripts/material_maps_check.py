#!/usr/bin/env python3
#opencode: live-only normal/height/parallax material-map A/B gate. Captures run
#   through audit_common (hidden window, parallel pool, settings safety).

import subprocess
import sys
from pathlib import Path

import numpy as np

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
OUT = Path("/tmp/opencode/material_maps")
RAW = OUT / "raw"


def request(map_mode="flat", parallax_mode=None):
    environment = audit.scene_env({
        "GX_T_TAA": "0",
        "GX_T_FOG": "0",
        "GX_T_SSR": "0",
        "GX_T_SSAO": "0",
        "GX_T_BLOOM": "0",
        "GX_T_MATERIAL_MAPS": map_mode,
    }, clean=True)
    if parallax_mode is not None:
        environment["GX_T_PARALLAX_MODE"] = str(parallax_mode)
    return environment


def image(path):
    return audit.load_rgb(path)


def delta(a, b):
    diff = np.abs(a - b)
    return float(diff.mean()), float((diff > 2.0).any(axis=2).mean())


def main():
    requests = {
        "flat": ("--ledrow", request()),
        "normal": ("--ledrow", request("normal")),
        "parallax0": ("--ledrow", request("parallax", 0)),
        "parallax2": ("--ledrow", request("parallax", 2)),
    }
    pool = audit.CapturePool(jobs=4, raw_dir=RAW)
    shots = pool.capture_all(requests, shot_frame=180)
    missing = sorted(set(requests) - set(shots))
    if missing:
        raise RuntimeError(f"material-map captures missing: {missing}")
    frames = {name: image(path) for name, path in shots.items()}
    shapes = {frame.shape for frame in frames.values()}
    if len(shapes) != 1:
        raise RuntimeError(f"material-map capture size mismatch: {sorted(shapes)}")
    normal_delta = delta(frames["flat"], frames["normal"])
    parallax_delta = delta(frames["normal"], frames["parallax2"])
    mode_delta = delta(frames["parallax0"], frames["parallax2"])
    print("             mean delta  frac >2")
    print(f"normal       {normal_delta[0]:10.3f}  {normal_delta[1]:7.4f}")
    print(f"parallax     {parallax_delta[0]:10.3f}  {parallax_delta[1]:7.4f}")
    print(f"mode 0 -> 2  {mode_delta[0]:10.3f}  {mode_delta[1]:7.4f}")
    if normal_delta[0] < 0.10 or normal_delta[1] < 0.001:
        raise RuntimeError("normal map produced no measurable shading change")
    if parallax_delta[0] < 0.10 or parallax_delta[1] < 0.001:
        raise RuntimeError("parallax produced no measurable image change")
    if mode_delta[0] < 0.02 or mode_delta[1] < 0.0002:
        raise RuntimeError("parallax quality modes are indistinguishable")
    print("MATERIAL-MAPS: PASS")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"MATERIAL-MAPS: FAIL — {exc}")
        sys.exit(1)
