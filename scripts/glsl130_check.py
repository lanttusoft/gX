#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: hosted desktop GLSL 1.30 compatibility regression. The production
# GL33 context compiles the native-GL3.0 shader dialect through GX_GLSL130=1;
# this validates core/batch source compatibility only, not context creation.
#
# Usage:
#   DISPLAY=:1 python3 scripts/glsl130_check.py
#   DISPLAY=:1 python3 scripts/glsl130_check.py /tmp/opencode/glsl130_check

import argparse
import re
import shutil
import subprocess
from pathlib import Path

import numpy as np

import audit_common as audit


DEFAULT_OUT = Path("/tmp/opencode/glsl130_check")
TIMEOUT = 120
SHOT_FRAME = 60
#opencode: the production baseline runs with every later effect off; the rows
#   differ only by the dialect switch.
SCENE_ENV = {
    "GX_GL_LEVEL": "300",
    "GX_FIXED_DT": "0.0166666667",
    "GX_T_TAA": "0",
    "GX_T_BLOOM": "0",
    "GX_T_SSR": "0",
    "GX_T_FOG": "0",
    "GX_T_SSAO": "0",
    "GX_T_SHADOWS": "0",
    "GX_T_POINT_SHADOWS": "0",
    "GX_T_REFL": "0",
    "GX_T_PARTICLES": "0",
    "GX_T_FXAA": "0",
    "GX_T_MOTIONBLUR": "0",
    "GX_T_DOF": "0",
    "GX_T_COOKIES": "0",
    "GX_T_DECALS": "0",
    "GX_T_ENVAMBIENT": "0",
    "GX_T_INSTANCING": "0",
    "GX_T_GEOMETRY": "0",
    "GX_T_MSAA": "0",
    "GX_NO_AE": "1",
}


#opencode: capture one deterministic GL3.0-policy GFX row and retain its full
# log so a dialect compile failure has a direct artifact for inspection.
def capture(label, env, screenshots, output):
    capture_dir = screenshots / label
    capture_dir.mkdir()
    result = audit.run_scene("--gfx", env, width=None, height=None, fps=None,
                             screenshot_dir=capture_dir, shot_frame=SHOT_FRAME,
                             timeout=TIMEOUT, check=False)
    log = audit.strip_ansi(result.stdout or "")
    (output / f"{label}.log").write_text(log, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        print(log, end="")
        raise RuntimeError(f"{label} capture failed")
    shot = audit.newest_shot(capture_dir)
    if shot is None:
        raise RuntimeError(f"{label} screenshot missing")
    return log, shot


#opencode: compare only image content after dimensions agree. A small tolerance
# permits capture-stack variation without masking a wrong shader dialect.
def image_delta(reference, candidate):
    ref_pixels = audit.load_rgb(reference)
    candidate_pixels = audit.load_rgb(candidate)
    if ref_pixels.shape != candidate_pixels.shape:
        raise RuntimeError("GLSL130 screenshot dimensions differ")
    delta = np.abs(ref_pixels - candidate_pixels)
    return audit.mean_abs_diff(ref_pixels, candidate_pixels), \
        float((delta.max(axis=2) > 4).mean())


#opencode: GLSL 130 must be the startup dialect, preserve the GL3.0 selected
# policy, and compile through the raw linker that binds legacy fragment outputs.
def require_legacy_contract(log):
    if "GLSL130: desktop compatibility dialect enabled" not in log:
        raise RuntimeError("GLSL130 startup dialect log missing")
    if "RHI_SHADER: 'batch' compiled OK" not in log:
        raise RuntimeError("GLSL130 batch shader did not compile")
    if "RHI_SHADER: 'glsl130' compiled OK" not in log:
        raise RuntimeError("GLSL130 wrapper shader path did not compile")
    if re.search(r"GLSL 130 compile failed|RHI_SHADER: (vertex|fragment|link) (compile|error)", log):
        raise RuntimeError("GLSL130 shader compile/link error in capture log")
    policy = re.search(
        r"GL: detected version (\d+)\.(\d+).*user=300.*geom=0 msaaTex=0 instancing=0", log)
    if not policy:
        raise RuntimeError("GLSL130 GL3.0 selected-policy pin missing")


def main():
    parser = argparse.ArgumentParser(
        description="Validate the hosted desktop GLSL 1.30 compatibility dialect.")
    parser.add_argument("output", nargs="?", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    output = args.output.resolve()
    screenshots = output / "screenshots"

    try:
        if not audit.BIN.is_file():
            raise RuntimeError(f"binary missing: {audit.BIN} (run make first)")
        if output.exists():
            shutil.rmtree(output)
        screenshots.mkdir(parents=True)

        with audit.settings_preserved():
            env = audit.scene_env(SCENE_ENV, clean=True)
            #opencode: the production baseline must not inherit a dialect hook.
            env.pop("GX_GLSL130", None)
            baseline_log, baseline_shot = capture("glsl330", env, screenshots, output)
            if "GLSL130: desktop compatibility dialect enabled" in baseline_log:
                raise RuntimeError("GLSL130 leaked into the production baseline")

            legacy_env = dict(env)
            legacy_env["GX_GLSL130"] = "1"
            legacy_log, legacy_shot = capture("glsl130", legacy_env, screenshots, output)
            require_legacy_contract(legacy_log)
            mean_delta, changed_fraction = image_delta(baseline_shot, legacy_shot)
            if mean_delta > 0.25 or changed_fraction > 0.01:
                raise RuntimeError(
                    f"GLSL130 visual drift exceeds host-probe tolerance "
                    f"({mean_delta:.3f}/{changed_fraction:.4f})")

        print("GLSL130-HOST-PROBE: PASS "
              f"mean-delta={mean_delta:.3f} changed={changed_fraction:.4f} "
              "(GL3.3 host; not native-context certification)")
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"GLSL130-HOST-PROBE: FAIL: {error}")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
