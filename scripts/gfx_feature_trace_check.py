#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: scene-feature closure gate. A DOF-only showcase must report the
#   effect closure and load bloom.scene; an all-effects-off scene must not
#   load the composite target. This pins the shared predicates in
#   renderer/gfx/features.c against the old hand-maintained update list.

import os
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "build" / "gX_build"
TRACE = re.compile(
    r"GFX_FEATURES hasEffects=(\d+) needHdr=(\d+) ssgi=(\d+) "
    r"bloom=(\d+) ssr=(\d+) fog=(\d+) dof=(\d+) motion=(\d+) "
    r"particles=(\d+) \| hdrLoaded=(\d+) bloomSceneLoaded=(\d+)")
PLAN = re.compile(
    r"GFX_PLAN mask=0x([0-9a-fA-F]+) count=(\d+) needHdr=(\d+) "
    r"effects=(\d+) missing=0x([0-9a-fA-F]+)")

OFF = {
    "GX_T_BLOOM": "0", "GX_T_SSR": "0", "GX_T_GODRAY": "0",
    "GX_T_FOG": "0", "GX_T_LENSFLARE": "0", "GX_T_PARTICLES": "0",
    "GX_T_DOF": "0", "GX_T_MOTIONBLUR": "0", "GX_T_SSAO": "0",
    "GX_T_TAA": "0", "GX_T_FOGSPRITE": "0",
}


def run(extra):
    env = {k: v for k, v in os.environ.items() if not k.startswith("GX_")}
    env.update({
        "DISPLAY": os.environ.get("DISPLAY", ":1"),
        "GX_FRAMES": "45",
        "GX_SKIP_TESTS": "1",
        "GX_NO_SETTINGS_SAVE": "1",
        "GX_AUDIT_WINDOW": "hidden",
        "GX_GFX_FEATURE_TRACE": "1",
        "GX_GFX_PLAN": "1",
        "GX_SSGI": "0",
    })
    env.update(OFF)
    env.update(extra)
    result = subprocess.run(
        [str(BIN), "--showcase"], cwd=str(ROOT), env=env,
        capture_output=True, text=True, timeout=180)
    log = re.sub(r"\x1b\[[0-9;]*m", "", result.stdout + result.stderr)
    rows = TRACE.findall(log)
    plans = PLAN.findall(log)
    if result.returncode != 0 or not rows:
        raise RuntimeError(f"trace missing (exit {result.returncode})")
    if not plans:
        raise RuntimeError("plan missing")
    return (tuple(int(v) for v in rows[-1]),
            tuple(int(v, 16) if i in (0, 4) else int(v)
                  for i, v in enumerate(plans[-1])))


def main():
    if not BIN.is_file():
        print("GFX_FEATURE_TRACE_CHECK: missing build/gX_build", file=sys.stderr)
        return 1
    try:
        off, off_plan = run({})
        dof, dof_plan = run({"GX_T_DOF": "1"})
        dep, dep_plan = run({"GX_T_FOG": "1", "GX_T_SHADOWS": "0"})
        # hasEffects, needHdr, ssgi, bloom, ssr, fog, dof, motion, particles,
        # hdrLoaded, bloomSceneLoaded
        if off[0] != 0 or off[10] != 0 or off_plan[2] != 0 or off_plan[3] != 0:
            raise RuntimeError(f"all-off effect/HDR closure is not empty: {off} {off_plan}")
        if dof[0] != 1 or dof[6] != 1 or dof[10] != 1:
            raise RuntimeError(f"DOF-only closure did not load composite: {dof}")
        if dof_plan[1] < 1 or dof_plan[2] != 1 or dof_plan[3] != 1:
            raise RuntimeError(f"DOF plan did not request HDR/effects: {dof_plan}")
        if dep_plan[4] == 0:
            raise RuntimeError(f"volume-fog dependency was not reported: {dep_plan}")
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"GFX_FEATURE_TRACE_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print("GFX_FEATURE_TRACE_CHECK: PASS all-off closure empty; "
          "DOF-only loads bloom.scene")
    return 0


if __name__ == "__main__":
    sys.exit(main())
