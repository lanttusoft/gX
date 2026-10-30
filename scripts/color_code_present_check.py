#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: standalone screen-code presentation smoke: exact size and all four palette bands.
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


def main():
    with tempfile.TemporaryDirectory(prefix="color_code_present_",
                                     dir="/tmp/opencode") as temporary:
        out = Path(temporary)
        shot = out / "present.png"
        environment = dict(os.environ)
        environment.pop("GX_AUDIT_WINDOW", None)
        environment.update({
            "GX_NO_SETTINGS_SAVE": "1",
            "GX_FRAMES": "20",
            "GX_COLOR_CODE_SHOT": str(shot),
        })
        command = [str(BIN), "--color-code", "gX color code",
                   "--screenWidth", str(WIDTH), "--screenHeight", str(HEIGHT)]
        result = subprocess.run(command, cwd=ROOT, env=environment,
                                text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=60)
        log = audit.strip_ansi(result.stdout or "")
        (out / "present.log").write_text(log)
        if result.returncode != 0:
            raise RuntimeError(f"presentation exited {result.returncode}")
        if not shot.is_file():
            raise RuntimeError("presentation did not write its screenshot")
        image = np.asarray(Image.open(shot).convert("RGB"))
        if image.shape != (HEIGHT, WIDTH, 3):
            raise RuntimeError(f"presentation size {image.shape} != {(HEIGHT, WIDTH, 3)}")
        counts = palette_counts(image)
        print("palette counts:", counts, flush=True)
        for name, minimum in (("black", 10000), ("white", 100),
                              ("red", 20), ("blue", 20)):
            if counts[name] < minimum:
                raise RuntimeError(f"{name} palette band too small: {counts[name]}")
        center = image[HEIGHT // 4:HEIGHT * 3 // 4,
                       WIDTH // 4:WIDTH * 3 // 4]
        colored = np.abs(center.astype(np.int32) - 24).max(axis=2) > 8
        if float(colored.mean()) < 0.05:
            raise RuntimeError("presentation code is not visible in the centre")
    print("COLOR_CODE_PRESENT_CHECK: PASS (1024x768, four palette bands, centred)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"COLOR_CODE_PRESENT_CHECK: FAIL — {error}", file=sys.stderr)
        sys.exit(1)
