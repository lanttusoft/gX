#!/usr/bin/env python3
# opencode: motion-blur velocity temporal check — static velocity-vs-fallback
#   twin (must agree; both see no motion) + spin bursts assembled to GIFs
#   (eyeball the smear; per-arm temporal stats must both show motion).
#   Spin cross-arm pixels are NOT compared (spin phase differs run to run).
"""Temporal check for the motion-blur velocity-buffer tap. Writes GIFs."""
import os
import shutil
import sys
import time

import numpy as np
from PIL import Image

import audit_common as audit


OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/opencode/motionblur_temporal"
SHOT_DIR = os.path.join(OUT, "raw")
FRAME = sys.argv[2] if len(sys.argv) > 2 else "120"
SPIN = sys.argv[3] if len(sys.argv) > 3 else "3"
BURST_COUNT = 12

BASE_OFF = {
    "GX_T_BLOOM": "0", "GX_T_SSAO": "0", "GX_T_SHADOWS": "0",
    "GX_T_POINT_SHADOWS": "0", "GX_T_SSR": "0", "GX_T_TAA": "0",
    "GX_T_FXAA": "0", "GX_T_GODRAY": "0", "GX_T_COOKIES": "0",
    "GX_T_PARTICLES": "0", "GX_T_LENSFLARE": "0", "GX_T_DOF": "0",
    "GX_T_ENVAMBIENT": "0", "GX_T_DECALS": "0", "GX_T_WIREFRAME": "0",
    "GX_T_SSRAUX": "0", "GX_T_INSTANCING": "0",
    "GX_T_FOG": "1", "GX_FOG_DENSITY": "0",
    "GX_CAM_YAW": "30", "GX_CAM_PITCH": "25", "GX_CAM_DIST": "10",
    "GX_T_MOTIONBLUR": "1",
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
    pairs = [np.abs(frames[i + 1] - frames[i]) for i in range(len(frames) - 1)]
    mad = float(np.mean([d.mean() for d in pairs]))
    changed = float(np.mean([(np.max(d, axis=2) > 10.0).mean() for d in pairs]))
    return mad, changed


def write_gif(paths, gif_path):
    small = [Image.open(p).convert("RGB").resize((768, 505)) for p in paths]
    small[0].save(gif_path, save_all=True, append_images=small[1:],
                  duration=120, loop=0, optimize=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    with audit.settings_preserved():
        return run_temporal()


def run_temporal():
    static_vel = load(capture("static_vel", {}))
    static_fb = load(capture("static_fb", {"GX_T_NOVELOCITY": "1"}))
    twin = np.abs(static_vel[0] - static_fb[0])
    twin_frac = float((twin.mean(axis=2) > 10).mean())
    twin_mean = float(twin.mean())
    print(f"static twin velocity-vs-fallback: frac>10 {twin_frac:.4f} dmean {twin_mean:.3f}")

    spin_vel_paths = capture("spin_vel", {"GX_CAM_SPIN": SPIN})
    spin_fb_paths = capture("spin_fb", {"GX_CAM_SPIN": SPIN,
                                        "GX_T_NOVELOCITY": "1"})
    spin_vel = load(spin_vel_paths)
    spin_fb = load(spin_fb_paths)
    vel_stats = temporal_stats(spin_vel)
    fb_stats = temporal_stats(spin_fb)
    print(f"spin velocity: MAD {vel_stats[0]:.3f} changed>10 {vel_stats[1]:.4f}")
    print(f"spin fallback: MAD {fb_stats[0]:.3f} changed>10 {fb_stats[1]:.4f}")
    write_gif(spin_vel_paths, os.path.join(OUT, "spin_vel.gif"))
    write_gif(spin_fb_paths, os.path.join(OUT, "spin_fb.gif"))
    print("gifs:", os.path.join(OUT, "spin_vel.gif"), os.path.join(OUT, "spin_fb.gif"))

    ok = twin_frac < 0.01 and vel_stats[1] > 0.05 and fb_stats[1] > 0.05
    print("MOTIONBLUR TEMPORAL: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
