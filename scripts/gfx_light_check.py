#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: runtime verification of PBR/Phong light accumulation and heap holes.

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
LOG_RE = re.compile(
    r"LAYOUT_LIGHT_TEST: mode=(\w+) model=(\w+) quality=(\d+) used=(\d+) span=(\d+)"
)
USED = {"baseline": 2, "remove0": 1, "remove1": 1,
        "removeBoth": 0, "reuse0": 2, "reuse1": 2}
ROI = (slice(110, 540), slice(150, 810))


def capture(temp_root, model, mode, config="front"):
    expected = (mode, model, "2", str(USED[mode]), "2")
    errors = []
    #opencode: isolated retry handles transient renderer startup/teardown
    #   failures without allowing a stale capture to satisfy the fixture.
    for attempt in range(2):
        directory = temp_root / model / config / mode / f"attempt_{attempt}"
        directory.mkdir(parents=True, exist_ok=True)
        environment = audit.scene_env({
            "GX_LAYOUT_LIGHT_TEST": mode,
            "GX_LAYOUT_LIGHT_MODEL": model,
            "GX_LAYOUT_NORESEAT": "1",
            "GX_FIXED_DT": "0.0166666667",
        }, clean=True)
        if config == "backface":
            environment["GX_LAYOUT_LIGHT_CONFIG"] = config
        try:
            result = audit.run_scene(
                "--layout-test", environment, width=960, height=640, fps=None,
                shot_frame=120, screenshot_dir=directory, timeout=60,
                check=False, extra_args=("--preset", "3"))
        except subprocess.TimeoutExpired as error:
            output = error.stdout or ""
            if isinstance(output, bytes):
                output = output.decode(errors="replace")
            errors.append(f"attempt {attempt + 1} timed out\n{output[-2000:]}")
            continue
        if result.returncode != 0:
            errors.append(
                f"attempt {attempt + 1} process failed ({result.returncode})\n"
                f"{result.stdout[-2000:]}")
            continue
        matches = LOG_RE.findall(result.stdout)
        if expected not in matches:
            errors.append(
                f"attempt {attempt + 1} expected fixture log {expected}, got {matches}\n"
                f"{result.stdout[-2000:]}")
            continue
        screenshots = list(directory.glob("gXss_*.png"))
        if not screenshots:
            errors.append(f"attempt {attempt + 1} screenshot missing\n{result.stdout[-2000:]}")
            continue
        screenshot = max(screenshots, key=lambda path: path.stat().st_mtime)
        pixels = np.asarray(Image.open(screenshot).convert("RGB"), dtype=np.int16)
        if pixels.shape != (640, 960, 3):
            errors.append(f"attempt {attempt + 1} unexpected image shape {pixels.shape}")
            continue
        return pixels
    raise RuntimeError(f"{model}/{config}/{mode}: " + "\n".join(errors))


def difference(first, second):
    delta = np.abs(first[ROI].astype(np.float32) - second[ROI].astype(np.float32))
    return float(delta.mean()), float((delta > 2.0).any(axis=2).mean())


def require_response(label, first, second):
    mean, changed = difference(first, second)
    if mean < 0.25 or changed < 0.005:
        raise RuntimeError(f"{label}: expected a visible light contribution, mean={mean:.4f}, pixels={changed:.3%}")
    print(f"  {label}: mean={mean:.3f}, changed={changed:.2%}")


def require_same(label, first, second):
    mean, changed = difference(first, second)
    if mean > 0.05 or changed > 0.005:
        raise RuntimeError(f"{label}: expected equivalent captures, mean={mean:.4f}, pixels={changed:.3%}")
    print(f"  {label}: mean={mean:.4f}, changed={changed:.3%}")


def main():
    parser = argparse.ArgumentParser(description="GFX light accumulation check")
    parser.add_argument("--jobs", type=int, default=1,
                        help="parallel isolated GPU captures (default: 1)")
    args = parser.parse_args()
    if args.jobs < 1 or args.jobs > 8:
        parser.error("--jobs must be between 1 and 8")
    if not BIN.is_file():
        print(f"GFX_LIGHT_CHECK: binary missing: {BIN}", file=sys.stderr)
        return 1
    os.makedirs("/tmp/opencode", exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_light_", dir="/tmp/opencode") as temporary:
            temp_root = Path(temporary)
            captures = {}
            jobs = []
            for model in ("pbr", "phong"):
                for mode in USED:
                    jobs.append(((model, "front", mode), model, mode, "front"))
            for mode in ("baseline", "remove0", "remove1", "removeBoth"):
                jobs.append((("pbr", "backface", mode), "pbr", mode, "backface"))
            with ThreadPoolExecutor(max_workers=args.jobs) as executor:
                futures = {
                    executor.submit(capture, temp_root, model, mode, config): key
                    for key, model, mode, config in jobs
                }
                for future in as_completed(futures):
                    captures[futures[future]] = future.result()
            print("GFX_LIGHT_CHECK: front-facing independent contributions")
            for model in ("pbr", "phong"):
                images = {mode: captures[(model, "front", mode)] for mode in USED}
                require_response(f"{model} slot 0", images["remove1"], images["removeBoth"])
                require_response(f"{model} slot 1", images["remove0"], images["removeBoth"])
                require_same(f"{model} slot 0 reuse", images["baseline"], images["reuse0"])
                require_same(f"{model} slot 1 reuse", images["baseline"], images["reuse1"])
            backface = {}
            for mode in ("baseline", "remove0", "remove1", "removeBoth"):
                backface[mode] = captures[("pbr", "backface", mode)]
            print("GFX_LIGHT_CHECK: back-facing light isolation")
            require_same("back-facing slot 1", backface["baseline"], backface["remove1"])
            require_same("back-facing slot 1 after removing slot 0",
                         backface["remove0"], backface["removeBoth"])
            require_response("front-facing slot 0 survives", backface["baseline"], backface["remove0"])
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"GFX_LIGHT_CHECK: FAIL: {error}", file=sys.stderr)
        return 1
    print("GFX_LIGHT_CHECK: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
