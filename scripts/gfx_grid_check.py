#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: seeded GUI x GFX viewport-grid smoke. It captures the opt-in
#   dashboard twice, requires four populated/different tile regions separated
#   by both grid gutters, and bounds run-to-run image drift. The grid disables
#   temporal history owners, so this is also its no-history contamination pin.
#
# Usage:
#   DISPLAY=:1 python3 scripts/gfx_grid_check.py
#   DISPLAY=:1 python3 scripts/gfx_grid_check.py /tmp/opencode/gfx_grid

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
DEFAULT_OUT = Path("/tmp/opencode/gfx_grid_check")
TIMEOUT = 180


#opencode: each run has an isolated screenshot directory so a stale capture
#   cannot pass a failed launch. The fixed shot frame follows other GFX gates.
def capture(label, environment, screenshots, output):
    capture_dir = screenshots / label
    capture_dir.mkdir()
    result = audit.run_scene(
        "--showcase", environment, width=None, height=None, fps=None,
        shot_frame=60, screenshot_dir=capture_dir, timeout=TIMEOUT,
        check=False, extra_args=("--seed", "1"))
    log = result.stdout
    (output / f"{label}.log").write_text(log, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        print(log, end="")
        raise RuntimeError(f"{label} capture failed")
    if not re.search(r"VIEWPORT_GRID: targets=4 size=\d+x\d+", log):
        raise RuntimeError(f"{label} viewport-grid allocation log missing")
    for pass_name in ("motionblur", "taa"):
        if not re.search(rf"PASS \d+ {pass_name}\s+off", log, re.IGNORECASE):
            raise RuntimeError(f"{label} did not suppress {pass_name} in grid mode")
    shots = sorted(capture_dir.glob("gXss_*.png"), key=lambda path: path.stat().st_mtime)
    if not shots:
        raise RuntimeError(f"{label} screenshot missing")
    return shots[-1]


#opencode: locate the dark central separators from the image itself so the
#   gate tolerates resolution/window fitting while still proving a real 2x2
#   composition rather than one fullscreen GFX blit.
def grid_metrics(path):
    pixels = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    height, width, _ = pixels.shape
    if width < 128 or height < 128:
        raise RuntimeError(f"grid screenshot is too small: {width}x{height}")
    luma = pixels.mean(axis=2)
    top = int(height * 0.04)
    bottom = max(top + 1, int(height * 0.98))
    left = int(width * 0.04)
    right = max(left + 1, int(width * 0.96))
    x_lo, x_hi = int(width * 0.43), int(width * 0.57)
    y_lo, y_hi = int(height * 0.45), int(height * 0.60)
    x_cut = x_lo + int(np.argmin(luma[top:bottom, x_lo:x_hi].mean(axis=0)))
    y_cut = y_lo + int(np.argmin(luma[y_lo:y_hi, left:right].mean(axis=1)))
    vertical_gutter = float(luma[top:bottom, x_cut].mean())
    horizontal_gutter = float(luma[y_cut, left:right].mean())
    if vertical_gutter > 32.0 or horizontal_gutter > 32.0:
        raise RuntimeError("2x2 separator gutters are not dark")

    pad_x = max(2, width // 400)
    pad_y = max(2, height // 400)
    tiles = (
        pixels[top:y_cut - pad_y, left:x_cut - pad_x],
        pixels[top:y_cut - pad_y, x_cut + pad_x:right],
        pixels[y_cut + pad_y:bottom, left:x_cut - pad_x],
        pixels[y_cut + pad_y:bottom, x_cut + pad_x:right],
    )
    for index, tile in enumerate(tiles):
        if tile.size == 0 or float(tile.mean()) < 20.0 or float(tile.std()) < 15.0:
            raise RuntimeError(f"tile {index + 1} is empty or lacks scene detail")

    pair_deltas = []
    for later in range(1, len(tiles)):
        for earlier in range(later):
            overlap_h = min(tiles[later].shape[0], tiles[earlier].shape[0])
            overlap_w = min(tiles[later].shape[1], tiles[earlier].shape[1])
            delta = np.abs(tiles[later][:overlap_h, :overlap_w]
                           - tiles[earlier][:overlap_h, :overlap_w]).mean()
            pair_deltas.append(float(delta))
    if min(pair_deltas) < 10.0:
        raise RuntimeError("viewport cameras did not produce distinct tile images")
    return {
        "width": width,
        "height": height,
        "vertical_gutter": vertical_gutter,
        "horizontal_gutter": horizontal_gutter,
        "min_tile_delta": min(pair_deltas),
    }


def main():
    parser = argparse.ArgumentParser(description="Validate the opt-in GFX viewport grid.")
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
        environment = audit.scene_env({
            "GX_GFX_GRID": "1",
            "GX_FIXED_DT": "0.0166666667",
            "GX_T_TAA": "1",
            "GX_T_MOTIONBLUR": "1",
            "GX_PASS_DUMP": "1",
        }, clean=True)
        first = capture("first", environment, screenshots, output)
        second = capture("second", environment, screenshots, output)
        first_metrics = grid_metrics(first)
        second_metrics = grid_metrics(second)
        first_pixels = np.asarray(Image.open(first).convert("RGB"), dtype=np.int16)
        second_pixels = np.asarray(Image.open(second).convert("RGB"), dtype=np.int16)
        if first_pixels.shape != second_pixels.shape:
            raise RuntimeError("seeded grid captures have different dimensions")
        delta = np.abs(first_pixels - second_pixels)
        mean_delta = float(delta.mean())
        changed_fraction = float(np.mean(delta.max(axis=2) > 4))
        if mean_delta > 4.0 or changed_fraction > 0.25:
            raise RuntimeError("seeded grid capture drift exceeds temporal-free bound")
        print("GFX_GRID_CHECK: "
              f"size={first_metrics['width']}x{first_metrics['height']} "
              f"gutters={first_metrics['vertical_gutter']:.1f}/"
              f"{first_metrics['horizontal_gutter']:.1f} "
              f"min-tile-delta={first_metrics['min_tile_delta']:.1f} "
              f"repeat-mean={mean_delta:.2f} changed={changed_fraction:.3f} "
              f"second-min-tile-delta={second_metrics['min_tile_delta']:.1f} "
              "verdict=PASS")
        return 0
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"GFX_GRID_CHECK: {error} verdict=FAIL")
        return 1


if __name__ == "__main__":
    sys.exit(main())
