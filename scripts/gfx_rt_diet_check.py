#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: render-target diet gate. A/B the optional aux-target formats on the
#   deterministic default fixture, require the full/off path to be
#   byte-identical, keep the image inside the agreed PSNR budget, and require
#   a real VRAM drop from both the env switch and the preset field.
"""Gate the optional render-target diet (VRAM mode)."""

import argparse
import json
import math
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit
import gfx_benchmark as benchmark


DEFAULT_OUT = audit.SCRATCH / "gfx_rt_diet_check"
FRAME = 300
WIDTH = 1024
HEIGHT = 768
PSNR_MIN = 48.0
MEAN_MAX = 0.20
MAX_DELTA = 96
VRAM_REDUCTION = 0.15


def capture(out, tag, extra_env=None, extra_args=(), screenshot=True,
            frames=FRAME + 120):
    directory = out / tag
    if directory.exists():
        for old in directory.glob("*.png"):
            old.unlink()
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        **benchmark.FEATURE_ROWS["default"],
        "GX_FIXED_DT": "%.12f" % (1.0 / 60.0),
        "GX_SHOT_FRAME": str(FRAME),
        "GX_RT_MEMORY": "1",
    }, clean=True)
    benchmark.clean_feature_environment(environment)
    environment.update(benchmark.FEATURE_ROWS["default"])
    environment.update(extra_env or {})
    result = audit.run_scene(
        "--showcase-minimal", environment, width=WIDTH, height=HEIGHT,
        fps=None, frames=frames, timeout=240, check=True,
        extra_args=("--seed", "424242") + tuple(extra_args),
        screenshot_dir=directory if screenshot else None,
        shot_frame=FRAME if screenshot else None)
    log = audit.strip_ansi(result.stdout or "")
    if not screenshot:
        return None, log
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError("%s produced no screenshot" % tag)
    with Image.open(shot) as image:
        return np.asarray(image.convert("RGB"), dtype=np.int16), log


def vram_total(log):
    totals = re.findall(r"RT_MEMORY: .* total ([0-9.]+) MB", log)
    if not totals:
        raise RuntimeError("missing RT_MEMORY total")
    return float(totals[-1])


def compare(reference, variant):
    delta = np.abs(reference - variant)
    mse = float((delta.astype(np.float64) ** 2).mean())
    return {
        "psnr": float("inf") if mse == 0 else 10.0 * math.log10((255.0 ** 2) / mse),
        "mean": float(delta.mean()),
        "max": int(delta.max()),
        "changed": float((delta.max(axis=2) > 0).mean()),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    off_a, off_log = capture(out, "off-a", {"GX_RT_DIET": "0"})
    off_b, _ = capture(out, "off-b", {"GX_RT_DIET": "0"})
    on, on_log = capture(out, "on", {"GX_RT_DIET": "1"})
    off_vram = vram_total(off_log)
    on_vram = vram_total(on_log)

    preset = json.loads(
        (audit.ROOT / "resources" / "tools" / "gfx_showcase.json").read_text())
    preset["state"]["ENABLE_RT_DIET"] = True
    preset_path = out / "diet-preset.json"
    preset_path.write_text(json.dumps(preset), encoding="utf-8")
    _, preset_log = capture(
        out, "preset", {"GX_GFX_PRESET_LOAD": str(preset_path)},
        extra_args=("--gfx-editor",), screenshot=False, frames=240)
    preset_vram = vram_total(preset_log)

    metrics = {
        "frame": FRAME,
        "size": [WIDTH, HEIGHT],
        "off_determinism": compare(off_a, off_b),
        "on": compare(off_a, on),
        "vram_mb": {"off": off_vram, "on": on_vram, "preset": preset_vram},
        "vram_reduction": (off_vram - on_vram) / off_vram if off_vram else 0.0,
        "preset_reduction": (off_vram - preset_vram) / off_vram if off_vram else 0.0,
    }
    failures = []
    if metrics["off_determinism"]["max"] != 0:
        failures.append("off path is not deterministic")
    if metrics["on"]["psnr"] < PSNR_MIN:
        failures.append("on PSNR %.2f < %.2f" % (metrics["on"]["psnr"], PSNR_MIN))
    if metrics["on"]["mean"] > MEAN_MAX:
        failures.append("on mean %.3f > %.3f" % (metrics["on"]["mean"], MEAN_MAX))
    if metrics["on"]["max"] > MAX_DELTA:
        failures.append("on max %d > %d" % (metrics["on"]["max"], MAX_DELTA))
    if metrics["vram_reduction"] < VRAM_REDUCTION:
        failures.append("VRAM reduction %.3f < %.3f" % (
            metrics["vram_reduction"], VRAM_REDUCTION))
    if metrics["preset_reduction"] < VRAM_REDUCTION:
        failures.append("preset VRAM reduction %.3f < %.3f" % (
            metrics["preset_reduction"], VRAM_REDUCTION))

    (out / "report.json").write_text(
        json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("GFX_RT_DIET: frame=%d %dx%d" % (FRAME, WIDTH, HEIGHT))
    print("  image: PSNR %.2f dB  mean %.3f  max %d  changed %.4f" % (
        metrics["on"]["psnr"], metrics["on"]["mean"], metrics["on"]["max"],
        metrics["on"]["changed"]))
    print("  VRAM: off %.3f MB -> env %.3f MB (-%.1f%%), preset %.3f MB (-%.1f%%)" % (
        off_vram, on_vram, metrics["vram_reduction"] * 100.0,
        preset_vram, metrics["preset_reduction"] * 100.0))
    if failures:
        for failure in failures:
            print("  FAIL " + failure)
        print("GFX_RT_DIET: FAIL")
        return 1
    print("GFX_RT_DIET: PASS (off deterministic; image and VRAM budgets hold)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        print("GFX_RT_DIET: FAIL — %s" % error)
        sys.exit(1)
