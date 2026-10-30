#!/usr/bin/env python3
#opencode: OpenGL selected-policy matrix. raylib has desktop 3.3 and 4.3
#   context profiles only, so GL3.0/3.1/3.2 rows are policy inspection, NOT
#   native-context certification. Each lower capability is requested alone
#   against an all-off scene baseline: unavailable requests must stay visually
#   absent and first-eligible requests must remain visible without later effects.
#
#   Usage:
#     DISPLAY=:1 python3 scripts/gl_level_check.py
#     DISPLAY=:1 python3 scripts/gl_level_check.py /tmp/opencode/gl_level

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

import audit_common as audit


ROOT = audit.ROOT
GL33_BUILD_DIR = ROOT / "build" / "gl-level33"
GL43_BUILD_DIR = ROOT / "build" / "gl-level43"
GL33_BIN = GL33_BUILD_DIR / "gX_build"
GL43_BIN = GL43_BUILD_DIR / "gX_build"
DEFAULT_OUT = Path("/tmp/opencode/gl_level_check")
TIMEOUT = 300
LEVELS = (330, 400, 410, 420, 430)
LOW_LEVELS = (300, 310, 320)


def checked(command, cwd=ROOT, timeout=TIMEOUT):
    result = subprocess.run(command, cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=timeout)
    if result.returncode != 0:
        print(result.stdout, end="")
        raise RuntimeError("command failed: " + " ".join(command))
    return result.stdout


#opencode: capture one selected policy level. Captures are isolated so stale
#   files cannot satisfy a later row after a failed launch.
def capture(binary, label, level, env, screenshots, output):
    capture_dir = screenshots / label
    capture_dir.mkdir()
    run_env = dict(env)
    run_env["GX_GL_LEVEL"] = str(level)
    result = audit.run_scene("--showcase", run_env, binary=binary, width=None,
                             height=None, fps=None, screenshot_dir=capture_dir,
                             shot_frame=None, timeout=TIMEOUT, check=False,
                             extra_args=("--seed", "1", "--preset", "0"))
    log = audit.strip_ansi(result.stdout or "")
    (output / f"{label}.log").write_text(log, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        print(log, end="")
        raise RuntimeError(f"{label} capture failed")
    shot = audit.newest_shot(capture_dir)
    if shot is None:
        raise RuntimeError(f"{label} screenshot missing")
    return log, shot


#opencode: the detected line is the contract shared by compatibility tests.
#   Keep the parser deliberately narrow so a changed log shape fails loudly.
def flags_from(log):
    match = re.search(
        r"GL: detected version (\d+)\.(\d+).*level (\d+), user=(\d+), "
        r"tess=(\d+) compute=(\d+) ssbo=(\d+) bindless=(\d+) "
        r"geom=(\d+) msaaTex=(\d+) instancing=(\d+)", log)
    if not match:
        raise RuntimeError("GL feature log missing")
    values = tuple(map(int, match.groups()))
    return {
        "major": values[0], "minor": values[1], "detected": values[2],
        "user": values[3], "tess": values[4], "compute": values[5],
        "ssbo": values[6], "bindless": values[7], "geometry": values[8],
        "msaa": values[9], "instancing": values[10],
    }


#opencode: selected-policy state must be derived from the host capability and
#   the requested core version. Extensions do not lift the selected policy.
def expect_policy(label, flags, level, hardware):
    expected = {
        "tess": hardware["tess"] if level >= 400 else 0,
        "compute": hardware["compute"] if level >= 430 else 0,
        "ssbo": hardware["ssbo"] if level >= 430 else 0,
        "bindless": hardware["bindless"] if level >= 420 else 0,
        "geometry": hardware["geometry"] if level >= 320 else 0,
        "msaa": hardware["msaa"] if level >= 320 else 0,
        "instancing": hardware["instancing"] if level >= 310 else 0,
    }
    if flags["user"] != level:
        raise RuntimeError(f"{label} selected-level pin missing")
    if any(flags[name] != value for name, value in expected.items()):
        raise RuntimeError(f"{label} feature policy mismatch")


#opencode: calculate image deltas only after matching dimensions. The low-GL
#   visual fixtures use this to distinguish a denied request from its allowed
#   first-eligible sibling without turning unrelated later effects back on.
def image_delta(reference, candidate):
    ref_pixels = audit.load_rgb(reference)
    candidate_pixels = audit.load_rgb(candidate)
    if ref_pixels.shape != candidate_pixels.shape:
        raise RuntimeError("visual fixture screenshot dimensions differ")
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


#opencode: run the actual GL33 context plus the GL43 selected-policy ladder.
#   The low-GL visual fixtures run on the GL33 binary, with all later effects
#   explicitly disabled, because they validate policy behavior—not native GL3.0.
def main():
    parser = argparse.ArgumentParser(
        description="Validate gX GL33 fallback and selected GL-level policy.")
    parser.add_argument("output", nargs="?", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    out = args.output.resolve()
    screenshots = out / "screenshots"

    try:
        if out.exists():
            shutil.rmtree(out)
        screenshots.mkdir(parents=True)
        checked(["cmake", "-B", str(GL33_BUILD_DIR), str(ROOT),
                 "-DCMAKE_BUILD_TYPE=Debug", "-DGX_COMPUTE=OFF"])
        checked(["cmake", "--build", str(GL33_BUILD_DIR), "-j" + str(os.cpu_count() or 1)])
        checked(["cmake", "-B", str(GL43_BUILD_DIR), str(ROOT),
                 "-DCMAKE_BUILD_TYPE=Debug", "-DGX_COMPUTE=ON"])
        checked(["cmake", "--build", str(GL43_BUILD_DIR), "-j" + str(os.cpu_count() or 1)])

        env = audit.scene_env({
            "GX_FIXED_DT": "0.0166666667",
            "GX_NO_COMPUTE": "0",
            "GX_NO_CLUSTER": "0",
            "GX_SHOT_FRAME": "60",
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
        }, clean=True)

        with audit.settings_preserved():
            gl33_log, _ = capture(GL33_BIN, "gl33", 330, env, screenshots, out)
            gl33 = flags_from(gl33_log)
            if (gl33["major"], gl33["minor"], gl33["detected"], gl33["user"],
                    gl33["compute"], gl33["ssbo"], gl33["geometry"], gl33["msaa"],
                    gl33["instancing"]) != (3, 3, 330, 330, 0, 0, 1, 1, 1):
                raise RuntimeError("GL33 fallback feature policy mismatch")

            baseline_log, _ = capture(GL43_BIN, "gl430", 430, env, screenshots, out)
            baseline = flags_from(baseline_log)
            if (baseline["detected"] < 430 or baseline["user"] != 430
                    or baseline["compute"] != 1 or baseline["ssbo"] != 1):
                raise RuntimeError("GL43 compute/SSBO capability pin missing")

            rows = []
            for level in LEVELS:
                if level == 430:
                    flags = baseline
                else:
                    log, _ = capture(GL43_BIN, f"gl{level}", level, env, screenshots, out)
                    flags = flags_from(log)
                if flags["detected"] < 430:
                    raise RuntimeError(f"GL{level} context capability pin missing")
                expect_policy(f"GL{level}", flags, level, baseline)
                rows.append((level, flags))

            low = {}
            for level in LOW_LEVELS:
                log, shot = capture(GL33_BIN, f"gl33-policy{level}", level,
                                    env, screenshots, out)
                flags = flags_from(log)
                if (flags["major"], flags["minor"], flags["detected"]) != (3, 3, 330):
                    raise RuntimeError(f"GL33 policy-{level} context pin missing")
                expect_policy(f"GL33 policy-{level}", flags, level, gl33)
                low[level] = (log, shot, flags)

            inst_env = dict(env)
            inst_env["GX_T_INSTANCING"] = "1"
            inst300_log, inst300_shot = capture(GL33_BIN, "gl300-instancing", 300,
                                                 inst_env, screenshots, out)
            inst310_log, inst310_shot = capture(GL33_BIN, "gl310-instancing", 310,
                                                 inst_env, screenshots, out)
            expect_policy("GL300 instancing", flags_from(inst300_log), 300, gl33)
            expect_policy("GL310 instancing", flags_from(inst310_log), 310, gl33)
            inst_denied = require_absent("GL300 instancing", low[300][1], inst300_shot)
            inst_allowed = require_visible("GL310 instancing", low[310][1], inst310_shot,
                                           inst_denied)

            geo_env = dict(env)
            geo_env["GX_T_GEOMETRY"] = "1"
            geo300_log, geo300_shot = capture(GL33_BIN, "gl300-geometry", 300,
                                               geo_env, screenshots, out)
            geo320_log, geo320_shot = capture(GL33_BIN, "gl320-geometry", 320,
                                               geo_env, screenshots, out)
            expect_policy("GL300 geometry", flags_from(geo300_log), 300, gl33)
            expect_policy("GL320 geometry", flags_from(geo320_log), 320, gl33)
            geo_denied = require_absent("GL300 geometry", low[300][1], geo300_shot)
            geo_allowed = require_visible("GL320 geometry", low[320][1], geo320_shot,
                                          geo_denied)

            msaa_env = dict(env)
            msaa_env["GX_T_MSAA"] = "4"
            msaa300_log, msaa300_shot = capture(GL33_BIN, "gl300-msaa", 300,
                                                 msaa_env, screenshots, out)
            msaa320_log, msaa320_shot = capture(GL33_BIN, "gl320-msaa", 320,
                                                 msaa_env, screenshots, out)
            if "GL_POLICY: MSAA texture request=4x suppressed at selected GL300" not in msaa300_log:
                raise RuntimeError("GL300 MSAA texture suppression log missing")
            if "MSAA FBO created:" not in msaa320_log:
                raise RuntimeError("GL320 MSAA texture allocation log missing")
            expect_policy("GL300 MSAA", flags_from(msaa300_log), 300, gl33)
            expect_policy("GL320 MSAA", flags_from(msaa320_log), 320, gl33)
            msaa_denied = require_absent("GL300 MSAA", low[300][1], msaa300_shot)
            msaa_allowed = image_delta(low[320][1], msaa320_shot)

            print("gl-level | context policy | tess compute ssbo bindless geom msaa instancing | verdict")
            print(f"    330 | {gl33['major']}.{gl33['minor']}     {gl33['user']} | "
                  f"{gl33['tess']:>4} {gl33['compute']:>7} {gl33['ssbo']:>4} "
                  f"{gl33['bindless']:>8} {gl33['geometry']:>4} {gl33['msaa']:>4} "
                  f"{gl33['instancing']:>10} | PASS")
            for level in LOW_LEVELS:
                flags = low[level][2]
                print(f"    {level} | {flags['major']}.{flags['minor']}     {flags['user']} | "
                      f"{flags['tess']:>4} {flags['compute']:>7} {flags['ssbo']:>4} "
                      f"{flags['bindless']:>8} {flags['geometry']:>4} {flags['msaa']:>4} "
                      f"{flags['instancing']:>10} | PASS")
            for level, flags in rows:
                print(f"    {level} | {flags['major']}.{flags['minor']}     {flags['user']} | "
                      f"{flags['tess']:>4} {flags['compute']:>7} {flags['ssbo']:>4} "
                      f"{flags['bindless']:>8} {flags['geometry']:>4} {flags['msaa']:>4} "
                      f"{flags['instancing']:>10} | PASS")
            print("GL-LEVEL-VISUAL: "
                  f"instancing=denied({inst_denied[0]:.2f}/{inst_denied[1]:.3f}) "
                  f"allowed({inst_allowed[0]:.2f}/{inst_allowed[1]:.3f}) "
                  f"geometry=denied({geo_denied[0]:.2f}/{geo_denied[1]:.3f}) "
                  f"allowed({geo_allowed[0]:.2f}/{geo_allowed[1]:.3f}) "
                  f"msaa=denied({msaa_denied[0]:.2f}/{msaa_denied[1]:.3f}) "
                  f"allowed({msaa_allowed[0]:.2f}/{msaa_allowed[1]:.3f})")
            print("GL-LEVEL-MATRIX: PASS (GL3.0-3.2 policy inspection; not native-context certification)")
            return 0
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"gl-level | {error} | FAIL")
        return 1


if __name__ == "__main__":
    sys.exit(main())
