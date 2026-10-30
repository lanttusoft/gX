#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: native desktop GL 3.x certification. Runs the GL3.0/3.1/3.2 policy
# rungs on REAL 3.x contexts (hard-capped Mesa + the opt-in raylib context
# hook) instead of the GL3.3 host: each rung checks its capability pin,
# denies-absent / first-eligible-visible visual fixtures, and zero error lines.
#
#   Prerequisites (fail loudly when missing):
#     1. A build.sh-built binary (build.sh auto-applies
#        patches/raylib-gx-gl30-context.patch; cmake-direct build dirs lack
#        the 3.x context hook — the script detects its log line and points
#        at the patch when absent).
#     2. Mesa GLX vendor reachable: __GLX_VENDOR_LIBRARY_NAME=mesa with
#        MESA_GL_VERSION_OVERRIDE / MESA_GLSL_VERSION_OVERRIDE per rung.
#
# Usage:
#   DISPLAY=:1 python3 scripts/gl_legacy_native_check.py
#   DISPLAY=:1 python3 scripts/gl_legacy_native_check.py /tmp/opencode/gl_legacy_native

import argparse
import os
import re
import shutil
import subprocess
from pathlib import Path

import numpy as np

import audit_common as audit


BIN = audit.BIN
DEFAULT_OUT = Path("/tmp/opencode/gl_legacy_native")
TIMEOUT = 240
SHOT_FRAME = 60
#opencode: every rung runs with all later effects off; only the context hook,
#   Mesa override, and the requested policy level change between rows.
SCENE_ENV = {
    "__GLX_VENDOR_LIBRARY_NAME": "mesa",
    "GALLIUM_DRIVER": "llvmpipe",
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

RUNGS = (
    {"minor": 0, "glsl": 130, "ctx": "GX_GL30_CONTEXT",
     "override": ("3.0", "130"), "dialect": "GLSL130"},
    {"minor": 1, "glsl": 140, "ctx": "GX_GL31_CONTEXT",
     "override": ("3.1", "140"), "dialect": "GLSL140"},
    {"minor": 2, "glsl": 150, "ctx": "GX_GL32_CONTEXT",
     "override": ("3.2", "150"), "dialect": "GLSL150"},
)


def base_env():
    env = audit.scene_env(SCENE_ENV, clean=True)
    #opencode: ambient dialect/fallback hooks must not leak between rungs.
    env.pop("GX_GLSL130", None)
    env.pop("GX_NO_IMMUTABLE_STORAGE", None)
    return env


#opencode: captures are isolated per label so a stale file can never satisfy
# a later rung after a failed launch. Preset 0 is pinned (like the hosted
# policy matrix) so the baseline is the true minimal set — the showcase
# default is preset 3 (Ultra), which would smuggle MSAA/HIGH-shader effects
# into every "all-off" row.
def capture(label, env, screenshots, output):
    capture_dir = screenshots / label
    capture_dir.mkdir()
    result = audit.run_scene("--showcase", env, width=None, height=None,
                             fps=None, screenshot_dir=capture_dir,
                             shot_frame=SHOT_FRAME, timeout=TIMEOUT,
                             check=False, extra_args=("--preset", "0"))
    log = audit.strip_ansi(result.stdout or "")
    (output / f"{label}.log").write_text(log, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        print(log, end="")
        raise RuntimeError(f"{label} capture failed")
    if re.search(r"ERROR!|FATAL", log):
        raise RuntimeError(f"{label} has ERROR/FATAL lines")
    shot = audit.newest_shot(capture_dir)
    if shot is None:
        raise RuntimeError(f"{label} screenshot missing")
    return log, shot


#opencode: same-driver deltas with the hosted matrix thresholds: a denied
# request must be pixel-identical; a first-eligible request must clearly move.
def image_delta(reference, candidate):
    ref_pixels = audit.load_rgb(reference)
    candidate_pixels = audit.load_rgb(candidate)
    if ref_pixels.shape != candidate_pixels.shape:
        raise RuntimeError("native fixture screenshot dimensions differ")
    delta = np.abs(ref_pixels - candidate_pixels)
    return audit.mean_abs_diff(ref_pixels, candidate_pixels), \
        float((delta.max(axis=2) > 4).mean())


def require_absent(label, reference, candidate):
    mean_delta, changed_fraction = image_delta(reference, candidate)
    if mean_delta > 3.0 or changed_fraction > 0.15:
        raise RuntimeError(f"{label} changed despite its lower selected policy")
    return mean_delta, changed_fraction


def require_visible(label, reference, candidate, denied_delta):
    mean_delta, changed_fraction = image_delta(reference, candidate)
    denied_mean, denied_changed = denied_delta
    if (mean_delta <= denied_mean + 0.20
            and changed_fraction <= denied_changed + 0.01):
        raise RuntimeError(f"{label} is not visually distinguishable at its first eligible policy")
    return mean_delta, changed_fraction


def rung_env(rung, level, extra=None):
    env = base_env()
    env["MESA_GL_VERSION_OVERRIDE"] = rung["override"][0]
    env["MESA_GLSL_VERSION_OVERRIDE"] = rung["override"][1]
    env[rung["ctx"]] = "1"
    env["GX_GL_LEVEL"] = str(level)
    if extra:
        env.update(extra)
    return env


def require_native_contract(label, log, rung, level):
    if "gX experiment requesting desktop OpenGL 3." not in log:
        raise RuntimeError(
            f"{label}: raylib 3.x context hook missing — apply "
            "patches/raylib-gx-gl30-context.patch inside external/raylib and rebuild")
    caps = re.search(
        r"RHI caps: GL (\d+)\.(\d+) \(GLSL (\d+)\) .*?geom=(\d+) ms=(\d+) instancing=(\d+)",
        log)
    if not caps:
        raise RuntimeError(f"{label}: RHI caps line missing")
    major, minor, shader_model, geom, ms, inst = map(int, caps.groups())
    if (major, minor, shader_model) != (3, rung["minor"], rung["glsl"]):
        raise RuntimeError(f"{label}: expected native 3.{rung['minor']}/GLSL {rung['glsl']}")
    if f"{rung['dialect']}: desktop compatibility dialect enabled" not in log:
        raise RuntimeError(f"{label}: {rung['dialect']} dialect did not auto-select")
    if "[host probe]" in log:
        raise RuntimeError(f"{label}: host-probe hook leaked into a native run")
    policy = re.search(r"GL: detected version 3\.(\d+).*user=(\d+), ", log)
    if not policy or int(policy.group(1)) != rung["minor"] or int(policy.group(2)) != level:
        raise RuntimeError(f"{label}: GL300-policy pin missing")
    return {"geom": geom, "ms": ms, "inst": inst}


def main():
    parser = argparse.ArgumentParser(
        description="Certify gX on native desktop GL 3.0/3.1/3.2 contexts.")
    parser.add_argument("output", nargs="?", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    output = args.output.resolve()
    screenshots = output / "screenshots"

    try:
        if not BIN.is_file():
            raise RuntimeError(f"binary missing: {BIN} (run make first)")
        if output.exists():
            shutil.rmtree(output)
        screenshots.mkdir(parents=True)
        with audit.settings_preserved():
            run_rungs(screenshots, output)
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"GL-NATIVE-MATRIX: FAIL: {error}")
        raise SystemExit(1)


def run_rungs(screenshots, output):
    by_minor = {r['minor']: r for r in RUNGS}
    #opencode: 3.0 rung — native render plus forced-fallback identity.
    r30 = by_minor[0]
    base30_log, base30_shot = capture("gl30", rung_env(r30, 300), screenshots, output)
    require_native_contract("gl30", base30_log, r30, 300)
    fb_log, fb_shot = capture(
        "gl30-nofallback",
        rung_env(r30, 300, {"GX_NO_IMMUTABLE_STORAGE": "1"}),
        screenshots, output)
    require_native_contract("gl30-nofallback", fb_log, r30, 300)
    fb_delta = require_absent("GL30 mutable fallback", base30_shot, fb_shot)

    #opencode: 3.1 rung — instancing denied at 300, visible at 310.
    r31 = by_minor[1]
    _, shot300 = capture("gl31-base300", rung_env(r31, 300), screenshots, output)
    base31_log, base31_shot = capture("gl31-base310", rung_env(r31, 310), screenshots, output)
    caps31 = require_native_contract("gl31-base310", base31_log, r31, 310)
    if caps31["inst"] != 1 or caps31["geom"] != 0 or caps31["ms"] != 0:
        raise RuntimeError("GL31 capability pin mismatch (want inst=1 geom=0 ms=0)")
    _, inst300_shot = capture(
        "gl31-inst300", rung_env(r31, 300, {"GX_T_INSTANCING": "1"}),
        screenshots, output)
    _, inst310_shot = capture(
        "gl31-inst310", rung_env(r31, 310, {"GX_T_INSTANCING": "1"}),
        screenshots, output)
    inst_denied = require_absent("GL31 instancing", shot300, inst300_shot)
    inst_allowed = require_visible("GL31 instancing", base31_shot, inst310_shot,
                                   inst_denied)

    #opencode: 3.2 rung — geometry + MSAA denied at 300, live at 320.
    r32 = by_minor[2]
    _, shot320_300 = capture("gl32-base300", rung_env(r32, 300), screenshots, output)
    base32_log, base32_shot = capture("gl32-base320", rung_env(r32, 320), screenshots, output)
    caps32 = require_native_contract("gl32-base320", base32_log, r32, 320)
    if caps32["inst"] != 1 or caps32["geom"] != 1 or caps32["ms"] != 1:
        raise RuntimeError("GL32 capability pin mismatch (want inst=1 geom=1 ms=1)")
    _, geo300_shot = capture(
        "gl32-geo300", rung_env(r32, 300, {"GX_T_GEOMETRY": "1"}),
        screenshots, output)
    _, geo320_shot = capture(
        "gl32-geo320", rung_env(r32, 320, {"GX_T_GEOMETRY": "1"}),
        screenshots, output)
    geo_denied = require_absent("GL32 geometry", shot320_300, geo300_shot)
    geo_allowed = require_visible("GL32 geometry", base32_shot, geo320_shot,
                                  geo_denied)
    msaa300_log, msaa300_shot = capture(
        "gl32-msaa300", rung_env(r32, 300, {"GX_T_MSAA": "4"}),
        screenshots, output)
    msaa320_log, msaa320_shot = capture(
        "gl32-msaa320", rung_env(r32, 320, {"GX_T_MSAA": "4"}),
        screenshots, output)
    if "GL_POLICY: MSAA texture request=4x suppressed at selected GL300" not in msaa300_log:
        raise RuntimeError("GL32 MSAA texture suppression log missing")
    if "MSAA FBO created:" not in msaa320_log:
        raise RuntimeError("GL32 MSAA texture allocation log missing")
    msaa_denied = require_absent("GL32 MSAA", shot320_300, msaa300_shot)
    msaa_allowed = image_delta(base32_shot, msaa320_shot)

    #opencode: 3.2 feature ladder — one effect per rung against the same
    # all-off baseline (independent rungs, no cascade). Fog at defaults
    # is a negative control (must stay absent).
    _, shadow_shot = capture(
        "gl32-shadows", rung_env(r32, 320, {"GX_T_SHADOWS": "1"}),
        screenshots, output)
    shadows = require_visible("GL32 shadows", base32_shot, shadow_shot,
                              (0.0, 0.0))
    _, ssao_shot = capture(
        "gl32-ssao", rung_env(r32, 320, {"GX_T_SSAO": "1"}),
        screenshots, output)
    ssao = require_visible("GL32 SSAO", base32_shot, ssao_shot,
                           (0.0, 0.0))
    _, ssr_shot = capture(
        "gl32-ssr", rung_env(r32, 320, {"GX_T_SSR": "1"}),
        screenshots, output)
    ssr = require_visible("GL32 SSR", base32_shot, ssr_shot,
                          (0.0, 0.0))
    _, refl_shot = capture(
        "gl32-refl", rung_env(r32, 320, {"GX_T_REFL": "1"}),
        screenshots, output)
    refl = require_visible("GL32 reflections", base32_shot, refl_shot,
                           (0.0, 0.0))
    _, ssr_refl_shot = capture(
        "gl32-ssr-refl", rung_env(
            r32, 320, {"GX_T_SSR": "1", "GX_T_REFL": "1"}),
        screenshots, output)
    ssr_refl = require_visible("GL32 SSR+reflections", base32_shot,
                               ssr_refl_shot, (0.0, 0.0))
    _, fog_shot = capture(
        "gl32-fog", rung_env(r32, 320, {"GX_T_FOG": "1"}),
        screenshots, output)
    fog = require_absent("GL32 fog", base32_shot, fog_shot)

    print("GL-NATIVE-3X: "
          f"30fallback=({fb_delta[0]:.2f}/{fb_delta[1]:.3f}) "
          f"31inst=denied({inst_denied[0]:.2f}/{inst_denied[1]:.3f}) "
          f"allowed({inst_allowed[0]:.2f}/{inst_allowed[1]:.3f}) "
          f"32geo=denied({geo_denied[0]:.2f}/{geo_denied[1]:.3f}) "
          f"allowed({geo_allowed[0]:.2f}/{geo_allowed[1]:.3f}) "
          f"32msaa=denied({msaa_denied[0]:.2f}/{msaa_denied[1]:.3f}) "
          f"allowed({msaa_allowed[0]:.2f}/{msaa_allowed[1]:.3f}) "
          f"32shadows=({shadows[0]:.2f}/{shadows[1]:.3f}) "
          f"32ssao=({ssao[0]:.2f}/{ssao[1]:.3f}) "
          f"32ssr=({ssr[0]:.2f}/{ssr[1]:.3f}) "
          f"32refl=({refl[0]:.2f}/{refl[1]:.3f}) "
          f"32ssr+refl=({ssr_refl[0]:.2f}/{ssr_refl[1]:.3f}) "
          f"32fog=({fog[0]:.2f}/{fog[1]:.3f})")
    print("GL-NATIVE-MATRIX: PASS (native GL3.0/3.1/3.2 contexts)")


if __name__ == "__main__":
    main()
