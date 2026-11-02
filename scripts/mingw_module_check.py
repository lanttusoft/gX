#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: Windows module smoke — build/run the devcube DLL under Wine and verify
#   the host handshake plus the native overlay palette.

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

import audit_common as audit
from gfx_devcube_check import component_counts


ROOT = audit.ROOT
BIN = ROOT / "build" / "mingw" / "gX_build.exe"
MODULE = ROOT / "build" / "mingw" / "gx_devcube.dll"
PREFIX = ROOT / "build" / "mingw" / "wineprefix"
ENTRY_MARKER = "DEVCUBE: entry"
LOAD_MARKER = "MODULE: loaded"
MINIMUM = {"green": 200, "pink": 80, "red": 40, "blue": 40}


def fail(message):
    print(f"MINGW_MODULE_CHECK: FAIL — {message}", file=sys.stderr)
    return 1


def ensure_runtime_dll():
    target = BIN.parent / "libwinpthread-1.dll"
    if target.is_file():
        return
    compiler = shutil.which("x86_64-w64-mingw32-gcc")
    if not compiler:
        return
    result = subprocess.run(
        [compiler, "-print-file-name=libwinpthread-1.dll"],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    source = Path(result.stdout.strip())
    if not source.is_file():
        source = Path("/usr/x86_64-w64-mingw32/bin/libwinpthread-1.dll")
    if source.is_file():
        shutil.copyfile(source, target)


def main():
    if not BIN.is_file() or not MODULE.is_file():
        return fail("build/mingw/gX_build.exe or gx_devcube.dll missing "
                    "(run ./build-mingw.sh)")
    if not shutil.which("wine"):
        return fail("wine not found")
    ensure_runtime_dll()
    if not (BIN.parent / "libwinpthread-1.dll").is_file():
        return fail("libwinpthread-1.dll missing next to the exe")

    environment = audit.scene_env({
        "DISPLAY": os.environ.get("DISPLAY", ":1"),
        "WINEDEBUG": "-all",
        "WINEPREFIX": str(PREFIX),
        "GX_FRAMES": "30",
        "GX_SHOT_FRAME": "20",
        "GX_FIXED_DT": "0.0166666667",
        "GX_SKIP_TESTS": "1",
        "GX_NO_SETTINGS_SAVE": "1",
    }, clean=True)
    with tempfile.TemporaryDirectory(prefix="gx-mingw-module-",
                                     dir="/tmp/opencode") as directory:
        environment["GX_SCREENSHOT_DIR"] = directory
        result = subprocess.run(
            ["wine", str(BIN), "--gfx", "--module", str(MODULE),
             "--screenWidth", "1024", "--screenHeight", "768",
             "--ai-screenshot-final"],
            cwd=ROOT, env=environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        log = audit.strip_ansi(result.stdout or "")
        if result.returncode != 0:
            return fail(f"wine run failed ({result.returncode})\n"
                        f"{log[-2000:]}")
        for marker in (ENTRY_MARKER, LOAD_MARKER):
            if marker not in log:
                return fail(f"module log missing {marker!r}")
        shot = audit.newest_shot(Path(directory))
        if shot is None:
            return fail("module capture missing")
        image = np.asarray(audit.Image.open(shot).convert("RGB"),
                           dtype=np.uint8)
    height, width = image.shape[:2]
    if (width, height) != (1024, 768):
        return fail(f"capture is {width}x{height}, expected 1024x768")
    counts = component_counts(image)
    missing = [name for name, count in counts.items()
               if count < MINIMUM[name]]
    if missing:
        return fail("missing overlay palette: " + ", ".join(
            f"{name}={counts[name]}<{MINIMUM[name]}" for name in missing))
    print("MINGW_MODULE_CHECK: PASS — devcube DLL handshake and overlay "
          f"cube={counts['green']} lissajous={counts['pink']} "
          f"red={counts['red']} blue={counts['blue']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
