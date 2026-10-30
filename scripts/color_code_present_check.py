#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: standalone screen-code presentation smoke: flat palette bands and simulated TV.
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
WIDTH = 1024
HEIGHT = 768


def palette_counts(pixels):
    flat = pixels.reshape(-1, 3).astype(np.int32)
    colors = {
        "black": (0, 0, 0),
        "white": (255, 255, 255),
        "red": (220, 40, 40),
        "blue": (40, 80, 220),
    }
    counts = {}
    for name, color in colors.items():
        distance = np.abs(flat - np.asarray(color, dtype=np.int32)).max(axis=1)
        counts[name] = int((distance <= 2).sum())
    return counts


def run(mode, shot, temporary):
    environment = dict(os.environ)
    environment.pop("GX_AUDIT_WINDOW", None)
    environment.update({
        "GX_NO_SETTINGS_SAVE": "1",
        "GX_FRAMES": "20",
        "GX_COLOR_CODE_SHOT": str(shot),
    })
    command = [str(BIN), mode, "gX color code",
               "--screenWidth", str(WIDTH), "--screenHeight", str(HEIGHT)]
    result = subprocess.run(command, cwd=ROOT, env=environment,
                            text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=60)
    log = audit.strip_ansi(result.stdout or "")
    (temporary / (mode.lstrip('-') + ".log")).write_text(log)
    if result.returncode != 0:
        raise RuntimeError(f"{mode} exited {result.returncode}")
    if not shot.is_file():
        raise RuntimeError(f"{mode} did not write its screenshot")
    image = np.asarray(Image.open(shot).convert("RGB"))
    if image.shape != (HEIGHT, WIDTH, 3):
        raise RuntimeError(f"{mode} size {image.shape} != {(HEIGHT, WIDTH, 3)}")
    return image


def check_flat(image):
    counts = palette_counts(image)
    print("flat palette counts:", counts, flush=True)
    for name, minimum in (("black", 10000), ("white", 100),
                          ("red", 20), ("blue", 20)):
        if counts[name] < minimum:
            raise RuntimeError(f"flat {name} palette band too small: {counts[name]}")
    center = image[HEIGHT // 4:HEIGHT * 3 // 4,
                   WIDTH // 4:WIDTH * 3 // 4]
    colored = np.abs(center.astype(np.int32) - 24).max(axis=2) > 8
    if float(colored.mean()) < 0.05:
        raise RuntimeError("flat presentation code is not visible in the centre")


def check_tv(image):
    pixels = image.astype(np.int32)
    red = pixels[..., 0]
    green = pixels[..., 1]
    blue = pixels[..., 2]
    bright = int(((red + green + blue) > 300).sum())
    red_dominant = int(((red > green + 40) & (red > blue + 40)).sum())
    blue_dominant = int(((blue > red + 40) & (blue > green + 20)).sum())
    dark = int(((red + green + blue) < 90).sum())
    print("tv bands:", {"bright": bright, "red": red_dominant,
                        "blue": blue_dominant, "dark": dark}, flush=True)
    if bright < 1000:
        raise RuntimeError("TV presentation has too few bright pixels")
    if red_dominant < 20 or blue_dominant < 20:
        raise RuntimeError("TV presentation lost its red/blue palette bands")
    if dark < 10000:
        raise RuntimeError("TV presentation lost its dark background")


def main():
    with tempfile.TemporaryDirectory(prefix="color_code_present_",
                                     dir="/tmp/opencode") as temporary:
        out = Path(temporary)
        flat = run("--color-code", out / "flat.png", out)
        check_flat(flat)
        tv = run("--color-code-tv", out / "tv.png", out)
        check_tv(tv)
    print("COLOR_CODE_PRESENT_CHECK: PASS (flat palette bands + simulated TV)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"COLOR_CODE_PRESENT_CHECK: FAIL — {error}", file=sys.stderr)
        sys.exit(1)
