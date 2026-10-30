#!/usr/bin/env python3
#opencode: GL4.3 compute particle-velocity regression pin. Builds the opt-in
#   compute binary, captures the velocity RT for one same-process 12-frame
#   burst, and requires moving particle-band vectors rather than only shader
#   compilation. The normal GL33 build is intentionally not touched.
#
#   Usage:
#     DISPLAY=:1 python3 scripts/particle_velocity_check.py
#     DISPLAY=:1 python3 scripts/particle_velocity_check.py /tmp/opencode/pv
#
#   Exit 0 iff the GL4.3 capability path, particle velocity program, burst
#   captures, and temporal particle-vector pin all pass.

import argparse
import json
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
DEFAULT_OUT = Path("/tmp/opencode/particle_velocity_check")
FRAME = "120"
TIMEOUT = 300


def checked(command, cwd=ROOT, timeout=TIMEOUT):
    result = subprocess.run(command, cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=timeout)
    if result.returncode != 0:
        print(result.stdout, end="")
        raise RuntimeError("command failed: " + " ".join(command))
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", nargs="?", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    out = args.output.resolve()
    raw = out / "raw"

    try:
        if out.exists():
            shutil.rmtree(out)
        raw.mkdir(parents=True)

        checked(["cmake", "-B", str(BUILD_DIR), str(ROOT),
                 "-DCMAKE_BUILD_TYPE=Debug", "-DGX_COMPUTE=ON"])
        checked(["cmake", "--build", str(BUILD_DIR), "-j" + str(os.cpu_count() or 1)])

        with audit.settings_preserved():
            env = audit.scene_env({
                "GX_BURST": "1",
                "GX_T_PARTICLES": "1",
                "GX_RT_VIEW": "velocity",
                "GX_T_TAA": "1",
                "GX_T_MOTIONBLUR": "0",
                "GX_CAM_SPIN": "0",
                "GX_T_BLOOM": "0",
                "GX_T_SSR": "0",
                "GX_T_FOG": "0",
                "GX_T_SSAO": "0",
                "GX_T_SHADOWS": "0",
                "GX_T_POINT_SHADOWS": "0",
                "GX_T_REFL": "0",
                "GX_T_FXAA": "0",
                "GX_NO_AE": "1",
            }, clean=True)
            result = audit.run_scene("--showcase", env, binary=BIN, width=None,
                                     height=None, fps=None, screenshot_dir=raw,
                                     shot_frame=FRAME, screenshot_after=True,
                                     timeout=TIMEOUT, check=False,
                                     extra_args=("--disableAudio",))
            log = audit.strip_ansi(result.stdout or "")
            (out / "run.log").write_text(log, encoding="utf-8", errors="replace")
            if result.returncode != 0:
                print(log, end="")
                raise RuntimeError("compute particle capture failed")

            if not re.search(r"GL: detected version .*compute=1 ssbo=1", log):
                raise RuntimeError("GL4.3 compute/SSBO capability pin missing")
            if "'particle_velocity' compiled OK" not in log:
                raise RuntimeError("particle velocity shader did not compile")
            particle_match = re.search(
                r"Particles: compute=(\d+) render=(\d+) velocity=(\d+)", log)
            if not particle_match or any(int(value) == 0 for value in particle_match.groups()):
                raise RuntimeError("particle compute/render/velocity program pin failed")

            paths = sorted(raw.glob("burst_*.png"))
            if len(paths) != 12:
                raise RuntimeError(f"expected 12 burst captures, found {len(paths)}")
            frames = [audit.load_rgb(path) for path in paths]
            height, width, _ = frames[0].shape
            y0 = int(height * 0.89)
            y1 = height
            x0 = int(width * 0.42)
            x1 = int(width * 0.62)

            #opencode: the velocity viewer uses channelMode=2 (RG only, gain 20,
            #   bias 0.5) and forces B to 0.5 — motion reads as RG deviation from
            #   the neutral 127, not as a blue channel (the old B>240 predicate
            #   predates 85701e8, which changed the viewer).
            def vector_mask(band):
                return ((np.abs(band[:, :, 0] - 127) > 8)
                        | (np.abs(band[:, :, 1] - 127) > 8))

            vector_counts = []
            for frame in frames:
                band = frame[y0:y1, x0:x1]
                vector_counts.append(int(np.sum(vector_mask(band))))
            changes = []
            for before, after in zip(frames, frames[1:]):
                delta = np.max(np.abs(
                    after[y0:y1, x0:x1] - before[y0:y1, x0:x1]), axis=2)
                changes.append(float((delta > 10).mean()))
            metrics = {
                "frames": len(frames),
                "vector_counts": vector_counts,
                "frames_with_vectors": sum(count > 0 for count in vector_counts),
                "peak_vector_pixels": max(vector_counts),
                "temporal_changed_gt10": changes,
                "temporal_peak": max(changes),
            }
            (out / "metrics.json").write_text(json.dumps(metrics, indent=2) + "\n")
            ok = (metrics["frames_with_vectors"] >= 1
                  and metrics["peak_vector_pixels"] >= 3
                  and metrics["temporal_peak"] >= 0.0001)
            print("particle-velocity | GL4.3 compute=1 shader=compiled "
                  f"vectors={metrics['frames_with_vectors']}/12 "
                  f"peak={metrics['peak_vector_pixels']} "
                  f"temporal_peak={metrics['temporal_peak']:.6f} | "
                  f"{'PASS' if ok else 'FAIL'}")
            return 0 if ok else 1
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"particle-velocity | {error} | FAIL")
        return 1


if __name__ == "__main__":
    sys.exit(main())
