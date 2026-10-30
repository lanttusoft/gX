#!/usr/bin/env python3
# opencode: same-process temporal SSGI spin-pair regression harness.
"""Measure temporal SSGI stability from same-process spinning-frame bursts."""
import os
import shutil
import sys
import time

import numpy as np

import audit_common as audit


OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/opencode/ssgi_spin_pair"
SHOT_DIR = os.path.join(OUT, "raw")
FRAME = sys.argv[2] if len(sys.argv) > 2 else "120"
SPIN = sys.argv[3] if len(sys.argv) > 3 else "3"
BURST_COUNT = 12


def capture(label, ssgi):
    env = audit.scene_env({
        "GX_BURST": "1",
        "GX_CAM_SPIN": SPIN,
        "GX_T_SSAO": "1",
        "GX_SSGI": str(ssgi),
    }, clean=True)
    started = time.time_ns()
    result = audit.run_scene("--showcase", env, width=None, height=None,
                             fps=None, screenshot_dir=SHOT_DIR,
                             shot_frame=int(FRAME), screenshot_after=True,
                             timeout=180, check=False,
                             extra_args=("--disableAudio",))
    log_text = audit.strip_ansi(result.stdout or "")
    log_path = os.path.join(OUT, f"{label}.log")
    with open(log_path, "w", encoding="ascii", errors="replace") as output:
        output.write(log_text)
    if result.returncode != 0:
        raise RuntimeError(f"{label}: renderer exited {result.returncode}; see {log_path}")
    if "GL_INVALID" in log_text or "shader compilation failed" in log_text.lower():
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


def metrics(paths):
    frames = [audit.load_rgb(path) for path in paths]
    if any(frame.shape != frames[0].shape for frame in frames):
        raise RuntimeError("burst frames have inconsistent dimensions")
    pairs = [np.abs(frames[index + 1] - frames[index]) for index in range(len(frames) - 1)]
    pair_mad = [float(delta.mean()) for delta in pairs]
    pair_changed = [float((np.max(delta, axis=2) > 10.0).mean()) for delta in pairs]
    temporal_std = np.stack(frames, axis=0).std(axis=0).mean(axis=2)
    return float(np.mean(pair_mad)), float(np.mean(pair_changed)), float(temporal_std.mean()), float((temporal_std > 8.0).mean())


def main():
    os.makedirs(OUT, exist_ok=True)
    with audit.settings_preserved():
        return run_pair()


def run_pair():
    on = metrics(capture("ssgi_on", 1))
    off = metrics(capture("ssgi_off", 0))
    print(" mode | pair MAD changed>10 temporal std hotspot>8")
    print(f"   on | {on[0]:8.4f} {on[1]:10.4f} {on[2]:12.4f} {on[3]:9.4f}")
    print(f"  off | {off[0]:8.4f} {off[1]:10.4f} {off[2]:12.4f} {off[3]:9.4f}")
    print("SSGI SPIN PAIR: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
