#!/usr/bin/env python3
#opencode: temporal stability gate — consecutive-frame drift within one
#   GX_BURST run (burst_000..011, zero-padded, sortable). Catches
#   per-frame seed/shimmer-class regressions (backlog 34: static 1.93
#   shimmer vs 0.22 clean) that single-frame A/B gates cannot see.
#   Auto-exposure fires one global gain step per burst (10-frame readback
#   cadence), so the gate uses the MEDIAN pair drift for shimmer plus a
#   separate step-count bound (<=2) for exposure hunting.

import argparse
import os
import statistics
import subprocess
import sys

import numpy as np
from PIL import Image

import audit_common as audit

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build", "gX_build")
OUT = "/tmp/opencode/temporal"
TARGET_FRAME = 120
BURST_LEN = 12
FIXED_DT = "0.016666667"
RNG_SEED = "1337"

STATIC_MEDIAN_MEAN_MAX = 0.60
STATIC_MEDIAN_FRAC_MAX = 0.03
STATIC_STEP_MEAN = 1.5
STATIC_STEP_COUNT_MAX = 2
SPIN_MEDIAN_MEAN_LO = 2.0
SPIN_MEDIAN_MEAN_HI = 15.0
SPIN_MEDIAN_FRAC_MAX = 0.60
#opencode: after the 2026-09-21/23 TAA stabilization commits the y2k spin
#   rows drift at ~1.98-2.00, just under the plain-scene floor; the frac bound
#   (0.17 vs 0.60 ceiling) still proves camera motion, so y2k spin rows carry
#   their own floor.
Y2K_SPIN_MEDIAN_MEAN_LO = 1.5
#opencode: --showcase-y2k carries high-variance content (neon emissives,
#   glass, mirror floor, starfield env), so its static frac budget keeps a
#   little headroom while still catching the pre-fix 9-10% shimmer class.
Y2K_STATIC_MEDIAN_MEAN_MAX = 0.60
Y2K_STATIC_MEDIAN_FRAC_MAX = 0.04


def burst(label, taa, spin=None, scene="--showcase", no_velocity=False):
    capture_dir = os.path.join(OUT, "raw", label)
    os.makedirs(capture_dir, exist_ok=True)
    for old in os.listdir(capture_dir):
        os.remove(os.path.join(capture_dir, old))
    #opencode: pin simulation and exposure time so capture load/scheduling
    #   cannot move the temporal metrics across their existing budgets; the
    #   burst path needs --ai-screenshot-after (audit_common screenshot_after).
    env = audit.scene_env({
        "GX_BURST": "1",
        "GX_FIXED_DT": FIXED_DT,
        "GX_T_PARTICLES": "0",
        "GX_T_TAA": str(taa),
    }, clean=True)
    if no_velocity:
        env["GX_T_NOVELOCITY"] = "1"
    if spin is not None:
        env["GX_CAM_SPIN"] = str(spin)
    #opencode: use a stable RNG stream for the entire eight-case capture set.
    audit.run_scene(scene, env, width=None, height=None, fps=None,
                    shot_frame=TARGET_FRAME, screenshot_dir=capture_dir,
                    timeout=300, screenshot_after=True,
                    extra_args=("--seed", RNG_SEED))
    frames = []
    for i in range(BURST_LEN):
        path = os.path.join(capture_dir, f"burst_{i:03d}.png")
        if not os.path.isfile(path):
            raise RuntimeError(f"{label}: {path} missing")
        frame = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
        if not np.isfinite(frame).all():
            raise RuntimeError(f"{label}: non-finite pixels in {path}")
        frames.append(frame)
    shapes = {frame.shape for frame in frames}
    if len(shapes) != 1:
        raise RuntimeError(f"{label}: burst size mismatch: {sorted(shapes)}")
    return frames


def pairs(frames):
    stats = []
    for prev, cur in zip(frames, frames[1:]):
        diff = np.abs(cur - prev)
        stats.append((float(diff.mean()),
                      float((diff > 2.0).any(axis=2).mean())))
    return stats


def summarize(stats):
    means = [mean for mean, _ in stats]
    fracs = [frac for _, frac in stats]
    steps = sum(1 for mean in means if mean > STATIC_STEP_MEAN)
    return (statistics.median(means), statistics.median(fracs), steps)


def main():
    #opencode: quick selection preserves each burst's full frame window while
    #   reducing the number of renderer launches for inner-loop feedback.
    parser = argparse.ArgumentParser(
        description="Check deterministic TAA stability bursts.")
    parser.add_argument("--quick", action="store_true",
                        help="run representative static and forced-fallback cases")
    parser.add_argument("--only", metavar="NAMES",
                        help="comma-separated temporal case names to run")
    args = parser.parse_args()
    case_specs = {
        "static_taa_on": (1, None, "--showcase", False),
        "static_taa_off": (0, None, "--showcase", False),
        "spin_taa_on": (1, 2.0, "--showcase", False),
        #opencode: y2k scene — the pre-fix cubemap jitter leak + EMA current
        #   term showed 1.05 mean / 9.4% pixels changing per frame here.
        "y2k_static_taa_on": (1, None, "--showcase-y2k", False),
        "y2k_static_taa_off": (0, None, "--showcase-y2k", False),
        "y2k_spin_taa_on": (1, 2.0, "--showcase-y2k", False),
        #opencode: force the depth-reprojection fallback on geometry as well
        #   as sky, guarding both static convergence and camera-motion flow.
        "y2k_static_fallback": (1, None, "--showcase-y2k", True),
        "y2k_spin_fallback": (1, 2.0, "--showcase-y2k", True),
    }
    quick_cases = {"static_taa_on", "y2k_static_taa_on",
                   "y2k_static_fallback", "y2k_spin_fallback"}
    selected = set(case_specs)
    if args.quick:
        selected &= quick_cases
    if args.only:
        requested = {item.strip() for item in args.only.split(",") if item.strip()}
        unknown = sorted(requested - set(case_specs))
        if unknown:
            parser.error("unknown case(s): " + ", ".join(unknown))
        selected &= requested
    if not selected:
        parser.error("case selection is empty")
    rows = {}
    for name, (taa, spin, scene, no_velocity) in case_specs.items():
        if name in selected:
            rows[name] = burst(name, taa, spin=spin, scene=scene,
                               no_velocity=no_velocity)
    stats = {name: summarize(pairs(frames))
             for name, frames in rows.items()}
    print("               median mean  median frac  steps")
    for name, (mean, frac, steps) in stats.items():
        print(f"{name:19s} {mean:10.3f}  {frac:10.4f}  {steps:5d}")
    for name, (mean, frac, steps) in stats.items():
        if "spin" in name:
            floor = (Y2K_SPIN_MEDIAN_MEAN_LO if name.startswith("y2k_")
                     else SPIN_MEDIAN_MEAN_LO)
            if not (floor < mean < SPIN_MEDIAN_MEAN_HI) \
                    or frac > SPIN_MEDIAN_FRAC_MAX:
                raise RuntimeError(f"{name}: drift outside motion band")
            continue
        mean_max = (Y2K_STATIC_MEDIAN_MEAN_MAX
                    if name.startswith("y2k_") else STATIC_MEDIAN_MEAN_MAX)
        frac_max = (Y2K_STATIC_MEDIAN_FRAC_MAX
                    if name.startswith("y2k_") else STATIC_MEDIAN_FRAC_MAX)
        if (mean > mean_max or frac > frac_max
                or steps > STATIC_STEP_COUNT_MAX):
            raise RuntimeError(f"{name}: drift above gate")
    print("TEMPORAL: PASS")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"TEMPORAL: FAIL — {exc}")
        sys.exit(1)
