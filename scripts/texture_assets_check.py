#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: E6 asset gate — the two committed DDS sidecars must load, stay inside
#   the image budget against the source PNG path, and actually reduce storage.
#   No encoder is needed here; this runs in the quick matrix.
"""Gate the committed compressed texture sidecars."""

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


DEFAULT_OUT = audit.SCRATCH / "texture_assets_check"
SCENE = "--gfx-editor"
PRESET = "resources/tools/gfx_materials.json"
FRAME = 60
WIDTH = 1024
HEIGHT = 768
PSNR_MIN = 55.0
MEAN_MAX = 0.05
MAX_DELTA = 32
CHANGED_MAX = 0.02
ASSETS = (
    ("orb.dds", "resources/texture/orb.png"),
    ("testcard.dds", "resources/texture/misc/testcard.png"),
)


def latest(directory):
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError("%s produced no screenshot" % directory.name)
    return shot


def capture(out, tag, no_dds):
    directory = out / tag
    if directory.exists():
        for old in directory.glob("*.png"):
            old.unlink()
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_GFX_PRESET_LOAD": PRESET,
        "GX_NO_TEXTURE_DDS": "1" if no_dds else "0",
        "GX_FIXED_DT": "%.12f" % (1.0 / 60.0),
    }, clean=True)
    result = audit.run_scene(
        SCENE, environment, width=WIDTH, height=HEIGHT, frames=FRAME + 20,
        shot_frame=FRAME, screenshot_dir=directory, timeout=120, check=True,
        extra_args=("--showcase-minimal",))
    with Image.open(latest(directory)) as image:
        pixels = np.asarray(image.convert("RGB"), dtype=np.int16)
    return pixels, audit.strip_ansi(result.stdout or "")


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

    missing = [name for name, _ in ASSETS
               if not (audit.ROOT / "resources" / "texture" / name).is_file()
               and not (audit.ROOT / "resources" / "texture" / "misc" / name).is_file()]
    if missing:
        print("TEXTURE_ASSETS: FAIL — missing sidecars: " + ", ".join(missing))
        return 1

    png, png_log = capture(out, "png", True)
    dds, dds_log = capture(out, "dds", False)
    metrics = compare(png, dds)
    loaded = [name for name, _ in ASSETS if name in dds_log]
    leaked = [name for name, _ in ASSETS if name in png_log]
    sizes = {}
    for name, source in ASSETS:
        sidecar = audit.ROOT / "resources" / "texture"
        sidecar = sidecar / ("misc" if source.endswith("misc/testcard.png") else "")
        sidecar = sidecar / name
        with Image.open(audit.ROOT / source) as image:
            raw = image.width * image.height * 4
        sizes[name] = {
            "file_bytes": sidecar.stat().st_size,
            "raw_rgba8_bytes": raw,
            "ratio": sidecar.stat().st_size / raw if raw else 0.0,
        }

    failures = []
    if metrics["psnr"] < PSNR_MIN:
        failures.append("PSNR %.2f < %.2f" % (metrics["psnr"], PSNR_MIN))
    if metrics["mean"] > MEAN_MAX:
        failures.append("mean %.4f > %.4f" % (metrics["mean"], MEAN_MAX))
    if metrics["max"] > MAX_DELTA:
        failures.append("max %d > %d" % (metrics["max"], MAX_DELTA))
    if metrics["changed"] > CHANGED_MAX:
        failures.append("changed %.4f > %.4f"
                        % (metrics["changed"], CHANGED_MAX))
    if len(loaded) != len(ASSETS):
        failures.append("sidecars not loaded: " + ", ".join(
            name for name, _ in ASSETS if name not in loaded))
    if leaked:
        failures.append("PNG arm loaded sidecars: " + ", ".join(leaked))
    for name, size in sizes.items():
        if size["ratio"] >= 0.5:
            failures.append("%s is not at least 2x smaller than RGBA8" % name)

    report = {"frame": FRAME, "size": [WIDTH, HEIGHT], "metrics": metrics,
              "loaded": loaded, "leaked": leaked, "sizes": sizes}
    (out / "report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("TEXTURE_ASSETS: frame=%d %dx%d" % (FRAME, WIDTH, HEIGHT))
    print("  image: PSNR %.2f dB mean %.4f max %d changed %.4f%%"
          % (metrics["psnr"], metrics["mean"], metrics["max"],
             metrics["changed"] * 100.0))
    for name, size in sizes.items():
        print("  %s: %d B vs RGBA8 %d B (%.2fx)"
              % (name, size["file_bytes"], size["raw_rgba8_bytes"],
                 size["ratio"]))
    if failures:
        for failure in failures:
            print("  FAIL " + failure)
        print("TEXTURE_ASSETS: FAIL")
        return 1
    print("TEXTURE_ASSETS: PASS — sidecars load, image budget and VRAM hold")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print("TEXTURE_ASSETS: FAIL — %s" % error)
        sys.exit(1)
