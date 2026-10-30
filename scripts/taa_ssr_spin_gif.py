#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

# opencode: TAA x SSR temporal stability probe — 2x2 arms (taa 0/1, ssr 0/1),
#   static twins + spin bursts to GIFs. Instability hypothesis: TAA jitter x
#   SSR march interact (shimmer beyond the sum of parts). Spin cross-arm
#   pixels are NOT compared (phase differs); only within-arm temporal stats.
"""TAA vs SSR instability check. Writes GIFs."""
import os
import shutil
import sys
import time

import numpy as np
from PIL import Image

import audit_common as audit


OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/opencode/taa_ssr_temporal"
SHOT_DIR = os.path.join(OUT, "raw")
FRAME = sys.argv[2] if len(sys.argv) > 2 else "120"
SPIN = sys.argv[3] if len(sys.argv) > 3 else "3"
BURST_COUNT = 12

BASE_OFF = {
    "GX_T_BLOOM": "0", "GX_T_SSAO": "0", "GX_T_SHADOWS": "0",
    "GX_T_POINT_SHADOWS": "0", "GX_T_FXAA": "0",
    "GX_T_SSR": "0", "GX_T_TAA": "0",
    "GX_T_GODRAY": "0", "GX_T_COOKIES": "0",
    "GX_T_PARTICLES": "0", "GX_T_LENSFLARE": "0", "GX_T_DOF": "0",
    "GX_T_MOTIONBLUR": "0", "GX_T_ENVAMBIENT": "0", "GX_T_DECALS": "0",
    "GX_T_WIREFRAME": "0", "GX_T_SSRAUX": "0", "GX_T_INSTANCING": "0",
    "GX_T_FOG": "1", "GX_FOG_DENSITY": "0",
    "GX_CAM_YAW": "30", "GX_CAM_PITCH": "25", "GX_CAM_DIST": "10",
}


def capture(label, extra):
    env = audit.scene_env({**BASE_OFF, **extra, "GX_BURST": "1"}, clean=True)
    started = time.time_ns()
    result = audit.run_scene("--showcase", env, width=None, height=None,
                             fps=None, screenshot_dir=SHOT_DIR,
                             shot_frame=int(FRAME), screenshot_after=True,
                             timeout=240, check=False,
                             extra_args=("--disableAudio",))
    log_text = audit.strip_ansi(result.stdout or "")
    log_path = os.path.join(OUT, f"{label}.log")
    with open(log_path, "w", encoding="ascii", errors="replace") as output:
        output.write(log_text)
    if result.returncode != 0:
        raise RuntimeError(f"{label}: renderer exited {result.returncode}; see {log_path}")
    low = log_text.lower()
    if "gl_invalid" in low or "compile failed" in low or "link failed" in low:
        raise RuntimeError(f"{label}: GL/shader error; see {log_path}")
    paths = []
    for index in range(BURST_COUNT):
        src = os.path.join(SHOT_DIR, f"burst_{index:03d}.png")
        if not os.path.exists(src) or os.stat(src).st_mtime_ns < started:
            raise RuntimeError(f"{label}: stale or missing burst frame {index}: {src}")
        dst = os.path.join(OUT, f"{label}_{index:03d}.png")
        shutil.copyfile(src, dst)
        paths.append(dst)
    return paths


def load(paths):
    frames = [audit.load_rgb(path) for path in paths]
    if any(frame.shape != frames[0].shape for frame in frames):
        raise RuntimeError("burst frames have inconsistent dimensions")
    return frames


def temporal_stats(frames):
    stack = np.stack(frames, axis=0)
    tstd = stack.std(axis=0).mean(axis=2)
    pairs = [np.abs(frames[i + 1] - frames[i]) for i in range(len(frames) - 1)]
    mad = float(np.mean([d.mean() for d in pairs]))
    changed = float(np.mean([(np.max(d, axis=2) > 10.0).mean() for d in pairs]))
    return mad, changed, float(tstd.mean()), float((tstd > 8.0).mean())


def write_gif(paths, gif_path):
    small = [Image.open(p).convert("RGB").resize((768, 505)) for p in paths]
    small[0].save(gif_path, save_all=True, append_images=small[1:],
                  duration=120, loop=0, optimize=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    with audit.settings_preserved():
        return run_temporal()


def run_temporal():
    arms = {
        "off": {},
        "taa": {"GX_T_TAA": "1"},
        "ssr": {"GX_T_SSR": "1"},
        "both": {"GX_T_TAA": "1", "GX_T_SSR": "1"},
    }
    print("static: mode | pair MAD changed>10 temporal std hotspot>8")
    static_frames = {}
    for name, extra in arms.items():
        frames = load(capture(f"static_{name}", extra))
        static_frames[name] = frames
        print(f"  {name:5s} | {temporal_stats(frames)[0]:8.4f} "
              f"{temporal_stats(frames)[1]:10.4f} {temporal_stats(frames)[2]:12.4f} "
              f"{temporal_stats(frames)[3]:9.4f}")
    print("spin: mode | pair MAD changed>10 temporal std hotspot>8")
    for name, extra in arms.items():
        paths = capture(f"spin_{name}", dict(extra, GX_CAM_SPIN=SPIN))
        frames = load(paths)
        print(f"  {name:5s} | {temporal_stats(frames)[0]:8.4f} "
              f"{temporal_stats(frames)[1]:10.4f} {temporal_stats(frames)[2]:12.4f} "
              f"{temporal_stats(frames)[3]:9.4f}")
        write_gif(paths, os.path.join(OUT, f"spin_{name}.gif"))
    print("TAA-SSR TEMPORAL: done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
