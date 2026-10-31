#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: SSGI temporal-lag gate. A deterministic camera cut at frame 40
#   makes the old screen-space GI invalid; the gate compares the accumulated
#   history with the current spatial gather and requires the lag to collapse
#   immediately instead of ghosting for ~10 frames.

import argparse
import os
from pathlib import Path
import subprocess
import sys

import numpy as np

import audit_common as audit
from gfx_buffer_audit import read_dump


OUT = Path("/tmp/opencode/ssgi_cut_check")
FIRST_FRAME = 40
FRAME_COUNT = 12
LAST_FRAME = FIRST_FRAME + FRAME_COUNT - 1
CUT_FRAME = 40
CUT_YAW = 180.0
WIDTH = 1024
HEIGHT = 768
#opencode: the cut frame may carry the historical contribution; by the first
#   two post-cut frames the current gather must dominate.
SETTLED_FRAMES = 2
MAX_MEAN_AFTER_CUT = 0.0003
MAX_PEAK_AFTER_CUT = 0.06
MAX_SETTLED_MEAN = 0.0002


def capture(name):
    directory = OUT / name
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.016666667",
        "GX_CAM_YAW": "-35",
        "GX_CAM_PITCH": "16",
        "GX_CAM_DIST": "11",
        "GX_CAM_CUT_FRAME": str(CUT_FRAME),
        "GX_CAM_CUT_YAW": str(CUT_YAW),
        "GX_T_PARTICLES": "0",
        "GX_T_GODRAY": "0",
        "GX_T_FOG": "0",
        "GX_T_SSAO": "1",
        "GX_GTAO": "0",
        "GX_SSGI": "1",
        "GX_T_SSGI_BOUNCE": "0",
        "GX_T_TAA": "0",
        "GX_T_SSR": "0",
        "GX_T_REFL": "0",
        "GX_T_BLOOM": "0",
        "GX_T_DOF": "0",
        "GX_T_MOTIONBLUR": "0",
        "GX_T_FXAA": "0",
        "GX_T_SHADOWS": "0",
        "GX_T_POINT_SHADOWS": "0",
        "GX_T_COOKIES": "0",
        "GX_T_DECALS": "0",
        "GX_T_LENSFLARE": "0",
        "GX_T_RAIN": "0",
        "GX_T_SNOWFALL": "0",
        "GX_T_SPLAT": "0",
        "GX_RT_VIEW": f"{name}:1:0",
        "GX_RT_DUMP_DIR": str(directory),
        "GX_RT_DUMP_FRAME": str(FIRST_FRAME),
        "GX_RT_DUMP_COUNT": str(FRAME_COUNT),
    }, clean=True)
    result = audit.run_scene("--reflectrow", environment, width=WIDTH,
        height=HEIGHT, fps=60, frames=LAST_FRAME, timeout=180, check=False)
    log = audit.strip_ansi(result.stdout or "")
    (directory / "renderer.log").write_text(log)
    if result.returncode != 0:
        raise RuntimeError(f"{name}: renderer exited {result.returncode}; see {directory / 'renderer.log'}")
    if "GL_INVALID" in log or "shader compilation failed" in log.lower():
        raise RuntimeError(f"{name}: GL/shader error; see {directory / 'renderer.log'}")
    paths = [directory / f"{name}_{frame:06}.gxrt"
        for frame in range(FIRST_FRAME, LAST_FRAME + 1)]
    missing = [path for path in paths if not path.is_file()]
    if missing:
        raise RuntimeError(f"{name}: missing render-target dumps: {missing}")
    return [read_dump(path) for path in paths]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    with audit.settings_preserved():
        blur = capture("ssgiBlur")
        history = capture("ssgiHist")
    if len(blur) != len(history):
        raise RuntimeError("SSGI cut capture lengths do not match")
    rows = []
    for index, (current, accumulated) in enumerate(zip(blur, history)):
        if current.shape != accumulated.shape:
            raise RuntimeError("SSGI cut dump dimensions do not match")
        delta = np.abs(accumulated - current)
        rows.append({
            "frame": FIRST_FRAME + index,
            "mean": float(delta.mean()),
            "p95": float(np.percentile(delta, 95)),
            "max": float(delta.max()),
        })
    for row in rows:
        print(f"{row['frame']:3d} mean={row['mean']:.6f} "
              f"p95={row['p95']:.6f} max={row['max']:.6f}")
    settled = rows[SETTLED_FRAMES]
    tail = rows[-1]
    if settled["mean"] > MAX_MEAN_AFTER_CUT or settled["max"] > MAX_PEAK_AFTER_CUT:
        raise RuntimeError(
            f"SSGI history still lags {SETTLED_FRAMES} frames after the cut: "
            f"mean={settled['mean']:.6f} max={settled['max']:.6f}")
    if tail["mean"] > MAX_SETTLED_MEAN:
        raise RuntimeError(
            f"SSGI history did not settle: mean={tail['mean']:.6f}")
    if args.report:
        args.report.write_text(__import__("json").dumps({
            "cut_frame": CUT_FRAME,
            "cut_yaw": CUT_YAW,
            "settled_frames": SETTLED_FRAMES,
            "max_mean_after_cut": MAX_MEAN_AFTER_CUT,
            "max_peak_after_cut": MAX_PEAK_AFTER_CUT,
            "max_settled_mean": MAX_SETTLED_MEAN,
            "rows": rows,
        }, indent=2) + "\n")
    print("SSGI_CUT: PASS — history rejects the camera cut without a ghost tail")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"SSGI_CUT: FAIL — {error}", file=sys.stderr)
        sys.exit(1)
