#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#backend-sdl lane check — A/B the GLFW and SDL platform builds from the same
#   source. Requires prebuilt binaries; build them in the lane with:
#       cmake -B build/glfw . && cmake --build build/glfw -j
#       cmake -B build/sdl  . -DPLATFORM=SDL && cmake --build build/sdl -j
#
#   Usage:
#     python3 scripts/backend_sdl_check.py \
#         --glfw build/glfw/gX_build --sdl build/sdl/gX_build \
#         [--drivers wayland,x11] [--output /tmp/opencode/backend_sdl/check]
#
#   Verifies per scene (--gfx, --title), hidden window, fixed 1/60 timestep:
#     - both arms exit 0, capture one screenshot, print the expected
#       PLATFORM: DESKTOP (GLFW|SDL3) line, and matching RHI caps
#       (timestamps stripped);
#     - same-batch cross-arm divergence stays inside the documented bands
#       (--gfx frac>2 <= 0.05; it measured 0.000000 in both 2026-10-18
#       batches; --title frac>2 <= 0.30 with <= 6000 px >40 against the
#       known background-layer divergence). Minutes-apart repeats of the
#       same arm drift up to ~0.02 frac>2, so compare same-batch only —
#       see docs/backend_sdl_report.md.
#   Opt-in lane gate, not part of `make check`.

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import audit_common as audit  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
FRAMES = 150
SHOT = 120
WIDTH, HEIGHT = 1024, 768
# Same-batch cross-arm gates (2026-10-18 measurements): --gfx 0.000000 in
# both batches; --title frac>2 0.11-0.23 against a same-batch same-arm floor
# of 0.000000 (minutes-apart repeats drift up to ~0.02 on both arms via
# wall-clock-sensitive elements, see docs/backend_sdl_report.md).
GFX_FRAC2_MAX = 0.05
TITLE_FRAC2_MAX = 0.30
TITLE_BIG_MAX = 6000
TIMESTAMP = re.compile(r"^<[^>]+>\s*")


def run_arm(arm, binary, driver, scene, output):
    out_dir = output / arm / scene.strip("-")
    out_dir.mkdir(parents=True, exist_ok=True)
    env = audit.scene_env(clean=True)
    env.update({"GX_FIXED_DT": "0.0166666667"})
    if driver:
        env["SDL_VIDEODRIVER"] = driver
    result = audit.run_scene(
        scene, env, binary=str(binary), width=WIDTH, height=HEIGHT, fps=60,
        frames=FRAMES, shot_frame=SHOT, screenshot_dir=out_dir, timeout=120,
        check=False, extra_args=("--seed", "1"))
    log = audit.strip_ansi(result.stdout or "")
    (out_dir / "run.log").write_text(log, encoding="utf-8", errors="replace")
    shots = sorted(out_dir.glob("*.png"))
    caps = next((TIMESTAMP.sub("", l) for l in log.splitlines()
                 if "RHI caps" in l), None)
    platform = next((TIMESTAMP.sub("", l) for l in log.splitlines()
                     if "PLATFORM: DESKTOP" in l), None)
    return {"exit": result.returncode, "shots": [str(s) for s in shots],
            "caps": caps, "platform": platform, "log": str(out_dir / "run.log")}


def compare(a_path, b_path):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(a_path).convert("RGB"), dtype=np.int32)
    b = np.asarray(Image.open(b_path).convert("RGB"), dtype=np.int32)
    if a.shape != b.shape:
        return {"shape": f"{a.shape} vs {b.shape}"}
    d = np.abs(a - b).max(axis=2)
    return {"frac_gt2": float((d > 2).mean()), "big_gt40": int((d > 40).sum()),
            "mean_abs": float(d.mean()), "max_abs": int(d.max()),
            "frac_gt0": float((d > 0).mean())}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--glfw", default="build/glfw/gX_build")
    parser.add_argument("--sdl", default="build/sdl/gX_build")
    parser.add_argument("--drivers", default="wayland,x11")
    parser.add_argument("--output", default="/tmp/opencode/backend_sdl/check")
    args = parser.parse_args()

    glfw_bin = (ROOT / args.glfw).resolve()
    sdl_bin = (ROOT / args.sdl).resolve()
    for binary in (glfw_bin, sdl_bin):
        if not binary.is_file():
            print(f"BACKEND_SDL_CHECK: missing binary {binary}", file=sys.stderr)
            return 1

    output = Path(args.output)
    report = {"glfw": str(glfw_bin), "sdl": str(sdl_bin), "scenes": {}}
    failures = []

    for scene in ("--gfx", "--title"):
        glfw_run = run_arm("glfw", glfw_bin, None, scene, output)
        if glfw_run["exit"] != 0 or not glfw_run["shots"] or \
                "GLFW" not in (glfw_run["platform"] or ""):
            failures.append(f"{scene}: glfw arm broken ({glfw_run['exit']}, "
                            f"{glfw_run['platform']})")
        entry = {"glfw": glfw_run, "sdl": {}}
        for driver in args.drivers.split(","):
            driver = driver.strip()
            if not driver:
                continue
            sdl_run = run_arm("sdl-" + driver, sdl_bin, driver, scene, output)
            if sdl_run["exit"] != 0 or not sdl_run["shots"] or \
                    "SDL3" not in (sdl_run["platform"] or ""):
                failures.append(f"{scene} [{driver}]: sdl arm broken "
                                f"({sdl_run['exit']}, {sdl_run['platform']})")
            if glfw_run["caps"] and sdl_run["caps"] and \
                    glfw_run["caps"] != sdl_run["caps"]:
                failures.append(f"{scene} [{driver}]: RHI caps differ")
            if glfw_run["shots"] and sdl_run["shots"]:
                diff = compare(glfw_run["shots"][-1], sdl_run["shots"][-1])
                entry["sdl"][driver] = {"run": sdl_run, "diff": diff}
                if scene == "--gfx" and diff.get("frac_gt2", 1.0) > GFX_FRAC2_MAX:
                    failures.append(f"{scene} [{driver}]: divergence above the "
                                    f"same-batch band (frac>2 "
                                    f"{diff['frac_gt2']}): {diff}")
                if scene == "--title" and (
                        diff.get("frac_gt2", 1.0) > TITLE_FRAC2_MAX
                        or diff.get("big_gt40", 10**9) > TITLE_BIG_MAX):
                    failures.append(f"{scene} [{driver}]: title divergence "
                                    f"beyond the documented band: {diff}")
        report["scenes"][scene] = entry

    output.mkdir(parents=True, exist_ok=True)
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    for scene, entry in report["scenes"].items():
        for driver, data in entry["sdl"].items():
            d = data["diff"]
            print(f"{scene:8s} [{driver:7s}] gfx-diff frac>0 "
                  f"{d.get('frac_gt0', -1):.6f} frac>2 {d.get('frac_gt2', -1):.6f} "
                  f">40 {d.get('big_gt40', -1)}")
    if failures:
        print("BACKEND_SDL_CHECK: FAIL")
        for failure in failures:
            print("  " + failure)
        return 1
    print("BACKEND_SDL_CHECK: PASS (platforms + caps match; divergence inside "
          "the documented same-batch bands)")
    print(f"report: {output / 'report.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
