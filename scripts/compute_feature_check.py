#!/usr/bin/env python3
#opencode: GL4.3 Forward+ compute regression pin. Builds the opt-in compute
#   binary, runs the 64-light showcase with the cluster parity hook, and
#   requires an exact GPU image-grid vs CPU-reference match, a bounded
#   final-frame delta between GPU- and CPU-assembled grids, and a visible
#   GL4.3-64-light versus GL3.3-32-light ceiling-lift delta.
#
#   Usage:
#     DISPLAY=:1 python3 scripts/compute_feature_check.py
#     DISPLAY=:1 python3 scripts/compute_feature_check.py /tmp/opencode/cf
#
#   Exit 0 iff the GL4.3 capability path, 64-light load, compute kernel, and
#   zero-mismatch tile-grid parity all pass.

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
BUILD_DIR = ROOT / "build" / "compute"
BIN = BUILD_DIR / "gX_build"
GL33_BUILD_DIR = ROOT / "build" / "forward33"
GL33_BIN = GL33_BUILD_DIR / "gX_build"
DEFAULT_OUT = Path("/tmp/opencode/compute_feature_check")
TIMEOUT = 300


def checked(command, cwd=ROOT, timeout=TIMEOUT):
    result = subprocess.run(command, cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=timeout)
    if result.returncode != 0:
        print(result.stdout, end="")
        raise RuntimeError("command failed: " + " ".join(command))
    return result.stdout


#opencode: capture a deterministic Forward+ frame into an isolated directory.
#   The GPU and GX_NO_COMPUTE CPU-cull variants keep the same GL4.3 fragment
#   SSBO consumer, light data, scene seed, and effect gates; only tile-mask
#   assembly differs.
#opencode: capture one named binary. The dedicated GL33 build stays in its
#   own build/forward33 subdir so this opt-in check never reconfigures the
#   production default.
def capture(binary, label, env, screenshot_dir, output):
    capture_dir = screenshot_dir / label
    capture_dir.mkdir()
    result = audit.run_scene("--showcase", env, binary=binary, width=None,
                             height=None, fps=None, screenshot_dir=capture_dir,
                             shot_frame=None, timeout=TIMEOUT, check=False,
                             extra_args=("--seed", "1"))
    log = audit.strip_ansi(result.stdout or "")
    (output / f"{label}.log").write_text(log, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        print(log, end="")
        raise RuntimeError(f"{label} capture failed")
    shot = audit.newest_shot(capture_dir)
    if shot is None:
        raise RuntimeError(f"{label} screenshot missing")
    saved = output / f"{label}.png"
    shutil.copy2(shot, saved)
    return log, saved


#opencode: the culling math has already been read back bit-for-bit. This
#   second proof exercises the actual standard.frag SSBO/grid consumer and
#   catches a valid-looking grid that was not visible to the following draw.
#opencode: report reusable RGB-delta metrics. GPU/CPU grid parity requires an
#   effectively identical frame; the ceiling-lift proof requires a deliberate
#   positive difference when the GL33 uniform path clamps at 32 lights.
def frame_metrics(first_path, second_path):
    first = audit.load_rgb(first_path)
    second = audit.load_rgb(second_path)
    if first.shape != second.shape:
        raise RuntimeError("screenshot size mismatch")
    delta = np.abs(first - second).mean(axis=2)
    mean_delta = float(delta.mean())
    max_delta = int(delta.max())
    changed_fraction = float((delta > 2.0).mean())
    black_fraction = min(audit.black_fraction(first), audit.black_fraction(second))
    return mean_delta, max_delta, changed_fraction, black_fraction


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", nargs="?", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    out = args.output.resolve()
    screenshot_dir = out / "screenshots"

    try:
        if out.exists():
            shutil.rmtree(out)
        screenshot_dir.mkdir(parents=True)
        checked(["cmake", "-B", str(BUILD_DIR), str(ROOT),
                  "-DCMAKE_BUILD_TYPE=Debug", "-DGX_COMPUTE=ON"])
        checked(["cmake", "--build", str(BUILD_DIR), "-j" + str(os.cpu_count() or 1)])
        checked(["cmake", "-B", str(GL33_BUILD_DIR), str(ROOT),
                 "-DCMAKE_BUILD_TYPE=Debug", "-DGX_COMPUTE=OFF"])
        checked(["cmake", "--build", str(GL33_BUILD_DIR), "-j" + str(os.cpu_count() or 1)])

        with audit.settings_preserved():
            env = audit.scene_env({
                "GX_NO_COMPUTE": "0",
                "GX_NO_CLUSTER": "0",
                "GX_T_CLUSTER_PARITY": "1",
                "GX_EXTRA_LIGHTS": "60",
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
                "GX_NO_AE": "1",
            }, clean=True)
            log, gpu_shot = capture(BIN, "gpu", env, screenshot_dir, out)

            if not re.search(r"GL: detected version .*compute=1 ssbo=1", log):
                raise RuntimeError("GL4.3 compute/SSBO capability pin missing")
            if not re.search(r"EXTRA LIGHTS: added 60 \(total 64\)", log):
                raise RuntimeError("64-light load pin missing")
            if "compute cull kernel loaded" not in log:
                raise RuntimeError("Forward+ compute cull kernel did not load")
            parity = re.search(
                r"CLUSTER_PARITY: tiles=(\d+)x(\d+) mismatchedTiles=(\d+) "
                r"mismatchedWords=(\d+)", log)
            if not parity:
                raise RuntimeError("cluster parity result missing")
            tiles_x, tiles_y, mismatched_tiles, mismatched_words = map(int, parity.groups())
            grid_ok = (tiles_x > 0 and tiles_y > 0
                       and mismatched_tiles == 0 and mismatched_words == 0)

            cpu_env = dict(env)
            cpu_env["GX_NO_COMPUTE"] = "1"
            _, cpu_shot = capture(BIN, "cpu_grid", cpu_env, screenshot_dir, out)
            mean_delta, max_delta, changed_fraction, black_fraction = frame_metrics(gpu_shot, cpu_shot)
            frame_ok = (mean_delta <= 0.01 and max_delta <= 2
                        and changed_fraction <= 0.0001 and black_fraction < 0.5)

            gl33_log, gl33_shot = capture(GL33_BIN, "gl33_uniform", env, screenshot_dir, out)
            if not re.search(r"GL: detected version 3\.3.*compute=0 ssbo=0", gl33_log):
                raise RuntimeError("GL33 compute/SSBO fallback pin missing")
            if "CLUSTERS: Init ran (GX_COMPUTE=0)" not in gl33_log:
                raise RuntimeError("GL33 uniform build pin missing")
            lift_mean, lift_max, lift_changed, lift_black = frame_metrics(gpu_shot, gl33_shot)
            lift_ok = (lift_mean >= 0.10 and lift_changed >= 0.005 and lift_black < 0.5)
            ok = grid_ok and frame_ok and lift_ok
            print("compute-feature | GL4.3 kernel=loaded lights=64 "
                  f"tiles={tiles_x}x{tiles_y} mismatchedTiles={mismatched_tiles} "
                  f"mismatchedWords={mismatched_words} "
                  f"frameMeanDelta={mean_delta:.4f} frameMaxDelta={max_delta} "
                  f"frameChanged={changed_fraction:.6f} black={black_fraction:.4f} "
                  f"ceilingMeanDelta={lift_mean:.4f} ceilingMaxDelta={lift_max} "
                  f"ceilingChanged={lift_changed:.6f} ceilingBlack={lift_black:.4f} | "
                  f"{'PASS' if ok else 'FAIL'}")
            return 0 if ok else 1
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"compute-feature | {error} | FAIL")
        return 1


if __name__ == "__main__":
    sys.exit(main())
