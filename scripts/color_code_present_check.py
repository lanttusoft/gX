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


def run(mode, shot, temporary, payload="gX color code", frames=20,
        shot_frame=None, tag=None):
    environment = dict(os.environ)
    environment.pop("GX_AUDIT_WINDOW", None)
    environment.update({
        "GX_NO_SETTINGS_SAVE": "1",
        "GX_FRAMES": str(frames),
        "GX_COLOR_CODE_SHOT": str(shot),
    })
    if shot_frame is not None:
        environment["GX_COLOR_CODE_SHOT_FRAME"] = str(shot_frame)
    command = [str(BIN), mode, payload,
               "--screenWidth", str(WIDTH), "--screenHeight", str(HEIGHT)]
    result = subprocess.run(command, cwd=ROOT, env=environment,
                            text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=90)
    log = audit.strip_ansi(result.stdout or "")
    name = tag or mode.lstrip('-')
    (temporary / (name + ".log")).write_text(log)
    if result.returncode != 0:
        raise RuntimeError(f"{mode} exited {result.returncode}")
    if not shot.is_file():
        raise RuntimeError(f"{mode} did not write its screenshot")
    image = np.asarray(Image.open(shot).convert("RGB"))
    if image.shape != (HEIGHT, WIDTH, 3):
        raise RuntimeError(f"{mode} size {image.shape} != {(HEIGHT, WIDTH, 3)}")
    return image, log


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


def check_multiframe(out):
    payload = "".join(chr(65 + (index % 26)) for index in range(500))
    first, first_log = run("--color-code", out / "multi_a.png", out,
                           payload=payload, frames=130, shot_frame=5,
                           tag="multi_a")
    second, second_log = run("--color-code", out / "multi_b.png", out,
                             payload=payload, frames=130, shot_frame=65,
                             tag="multi_b")
    if "COLOR_CODE: 4 frame(s)" not in first_log:
        raise RuntimeError("multi-frame presentation did not encode four frames")
    if "COLOR_CODE: frame 2/4" not in second_log:
        raise RuntimeError("multi-frame presentation did not advance to frame 2")
    if float(np.abs(first.astype(np.int32) - second.astype(np.int32)).mean()) < 1.0:
        raise RuntimeError("multi-frame presentation frames did not change")


def main():
    with tempfile.TemporaryDirectory(prefix="color_code_present_",
                                     dir="/tmp/opencode") as temporary:
        out = Path(temporary)
        flat, _ = run("--color-code", out / "flat.png", out)
        check_flat(flat)
        tv, _ = run("--color-code-tv", out / "tv.png", out)
        check_tv(tv)
        check_multiframe(out)
    print("COLOR_CODE_PRESENT_CHECK: PASS (flat palette bands + simulated TV + multi-frame)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"COLOR_CODE_PRESENT_CHECK: FAIL — {error}", file=sys.stderr)
        sys.exit(1)
