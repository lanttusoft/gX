#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: deterministic reduced-resolution DOF check. It compares full,
#   half, and quarter blur paths over a small focus rack and repeat capture.

import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


OUT = Path("/tmp/opencode/dof_resolution_check")
RAW = OUT / "raw"
SEED_ARGS = ("--seed", "424242")
RESOLUTIONS = {"full": "0", "half": "1", "quarter": "2"}
FOCUS_RACK = ("5.4", "5.8", "7.4")


def request(resolution, focal, spin="0"):
    return audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "120",
        "GX_T_DOF": "1",
        "GX_T_DOF_RES": resolution,
        "GX_T_DOFFOCAL": focal,
        "GX_T_DOFRANGE": "1.0",
        "GX_T_TAA": "0",
        "GX_T_FXAA": "0",
        "GX_T_BLOOM": "1",
        "GX_T_BLOOMINT": "1.0",
        "GX_T_SSAO": "0",
        "GX_T_SSR": "0",
        "GX_T_SHADOWS": "1",
        "GX_T_MOTIONBLUR": "0",
        "GX_T_PARTICLES": "0",
        "GX_T_LENSFLARE": "0",
        "GX_T_GODRAY": "0",
        "GX_T_FOG": "0",
        "GX_T_PALETTE_MODE": "0",
        "GX_T_DITHER_MODE": "0",
        "GX_T_INTERLACE_MODE": "0",
        "GX_DOF_FIXTURE": "1",
        "GX_CAM_YAW": "45",
        "GX_CAM_PITCH": "25",
        "GX_CAM_DIST": "6",
        "GX_CAM_SPIN": spin,
    }, clean=True)


def image(path):
    pixels = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    if not np.isfinite(pixels).all():
        raise RuntimeError("non-finite pixels in %s" % path)
    return pixels


def luma(pixels):
    return np.dot(pixels, np.asarray([0.299, 0.587, 0.114], dtype=np.float32))


def difference(first, second, roi=None):
    if roi is None:
        first = first[96:]
        second = second[96:]
    else:
        x0, y0, x1, y1 = roi
        first = first[y0:y1, x0:x1]
        second = second[y0:y1, x0:x1]
    delta = np.abs(first - second)
    return {
        "mean_abs_channel": float(delta.mean()),
        "max_channel": float(delta.max()),
    }


def edge_energy(pixels):
    values = luma(pixels[96:])
    edges = np.concatenate((
        np.abs(values[:, 1:] - values[:, :-1]).reshape(-1),
        np.abs(values[1:, :] - values[:-1, :]).reshape(-1),
    ))
    return float(edges.mean())


def main():
    if not audit.BIN.is_file():
        raise RuntimeError("missing build binary: %s" % audit.BIN)
    OUT.mkdir(parents=True, exist_ok=True)

    requests = {}
    for resolution, mode in RESOLUTIONS.items():
        for focal in FOCUS_RACK:
            label = "%s_focus_%s" % (resolution, focal.replace(".", "_"))
            requests[label] = ("--showcase-minimal", request(mode, focal), SEED_ARGS)
    requests["quarter_motion"] = ("--showcase-minimal",
        request(RESOLUTIONS["quarter"], FOCUS_RACK[1], spin="1.5"), SEED_ARGS)
    requests["quarter_repeat"] = ("--showcase-minimal",
        request(RESOLUTIONS["quarter"], FOCUS_RACK[1]), SEED_ARGS)
    pool = audit.CapturePool(jobs=4, raw_dir=RAW)
    shots = pool.capture_all(requests, shot_frame=120)
    missing = sorted(set(requests) - set(shots))
    if missing:
        raise RuntimeError("missing DOF captures: %s" % missing)
    captures = {name: str(path) for name, path in shots.items()}
    pixels = {label: image(path) for label, path in shots.items()}
    report = {
        "schema": 1,
        "fixture": "showcase-minimal-focus-rack",
        "resolution": [1024, 768],
        "seed": 424242,
        "focus_rack": [float(value) for value in FOCUS_RACK],
        "motion": {"spin_degrees_per_frame": 1.5, "frames": 120},
        "captures": captures,
        "edge_energy": {
            label: edge_energy(value) for label, value in pixels.items()
        },
        "comparisons": {},
        "advisory_budgets": {
            "max_reduced_mean_abs_channel": 12.0,
            "min_focus_response_mean_abs_channel": 0.10,
            "focus_roi": [150, 96, 800, 500],
            "min_reduced_edge_energy_ratio": 0.55,
            "max_reduced_edge_energy_ratio": 1.15,
        },
        "pass": True,
    }
    for resolution in ("half", "quarter"):
        for focal in FOCUS_RACK:
            suffix = focal.replace(".", "_")
            reduced = "%s_focus_%s" % (resolution, suffix)
            full = "full_focus_%s" % suffix
            report["comparisons"]["full_to_%s" % reduced] = difference(
                pixels[full], pixels[reduced])
            if (report["comparisons"]["full_to_%s" % reduced]["mean_abs_channel"]
                    > report["advisory_budgets"]["max_reduced_mean_abs_channel"]):
                report["pass"] = False
            full_edges = report["edge_energy"][full]
            reduced_edges = report["edge_energy"][reduced]
            edge_ratio = reduced_edges / max(full_edges, 0.001)
            report["comparisons"]["edge_ratio_%s" % reduced] = edge_ratio
            if (edge_ratio < report["advisory_budgets"]["min_reduced_edge_energy_ratio"]
                    or edge_ratio > report["advisory_budgets"]["max_reduced_edge_energy_ratio"]):
                report["pass"] = False
    near_suffix = FOCUS_RACK[0].replace(".", "_")
    far_suffix = FOCUS_RACK[-1].replace(".", "_")
    for resolution in RESOLUTIONS:
        near = "%s_focus_%s" % (resolution, near_suffix)
        far = "%s_focus_%s" % (resolution, far_suffix)
        response = difference(
            pixels[near], pixels[far], report["advisory_budgets"]["focus_roi"])
        report["comparisons"]["focus_response_%s" % resolution] = response
        if response["mean_abs_channel"] < report["advisory_budgets"]["min_focus_response_mean_abs_channel"]:
            report["pass"] = False
    repeat_suffix = FOCUS_RACK[1].replace(".", "_")
    repeat = difference(pixels["quarter_focus_%s" % repeat_suffix], pixels["quarter_repeat"])
    report["comparisons"]["quarter_repeat"] = repeat
    if repeat["max_channel"] > 0.0:
        report["pass"] = False
    motion = difference(pixels["quarter_focus_%s" % repeat_suffix], pixels["quarter_motion"])
    report["comparisons"]["quarter_motion"] = motion
    if motion["mean_abs_channel"] < 0.25:
        report["pass"] = False
    with open(OUT / "report.json", "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    print("DOF-RESOLUTION-CHECK: %s" % ("PASS" if report["pass"] else "FAIL"))
    for resolution in RESOLUTIONS:
        response = report["comparisons"]["focus_response_%s" % resolution]
        print("  %-7s focus response %.3f" % (resolution, response["mean_abs_channel"]))
    print("  quarter repeat max %.1f" % report["comparisons"]["quarter_repeat"]["max_channel"])
    print("  quarter motion response %.3f" % report["comparisons"]["quarter_motion"]["mean_abs_channel"])
    print("  report: %s" % (OUT / "report.json"))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("DOF-RESOLUTION-CHECK: FAIL — %s" % error)
        sys.exit(1)
