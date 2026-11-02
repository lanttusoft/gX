#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: Windows scripting-shell smoke — load gx_shell.dll under Wine and run a
#   tiny AngelScript probe through the module handshake.

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import audit_common as audit


ROOT = audit.ROOT
BIN = ROOT / "build" / "mingw-scripting" / "gX_build.exe"
MODULE = ROOT / "build" / "mingw-scripting" / "gx_shell.dll"
PREFIX = ROOT / "build" / "mingw-scripting" / "wineprefix"
RUNTIME = ("libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll")


def fail(message):
    print(f"MINGW_SCRIPTING_CHECK: FAIL — {message}", file=sys.stderr)
    return 1


def copy_runtime():
    for name in RUNTIME:
        target = BIN.parent / name
        if target.is_file():
            continue
        source = Path("/usr/x86_64-w64-mingw32/bin") / name
        if source.is_file():
            shutil.copyfile(source, target)


def main():
    if not BIN.is_file() or not MODULE.is_file():
        return fail("build/mingw-scripting artifacts missing "
                    "(run ./build-mingw-scripting.sh)")
    if not shutil.which("wine"):
        return fail("wine not found")
    copy_runtime()

    environment = audit.scene_env({
        "DISPLAY": os.environ.get("DISPLAY", ":1"),
        "WINEDEBUG": "-all",
        "WINEPREFIX": str(PREFIX),
        "GX_FRAMES": "90",
        "GX_FIXED_DT": "0.0166666667",
        "GX_SKIP_TESTS": "1",
        "GX_NO_SETTINGS_SAVE": "1",
    }, clean=True)
    with tempfile.TemporaryDirectory(prefix="gx-mingw-script-",
                                     dir="/tmp/opencode") as directory:
        script = Path(directory) / "module_probe.as"
        script.write_text(
            "void main()\n"
            "{\n"
            "    Print(\"module probe ran\");\n"
            "    Quit(0);\n"
            "}\n")
        result = subprocess.run(
            ["wine", str(BIN), "--module", str(MODULE), "--script",
             str(script)],
            cwd=ROOT, env=environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        log = audit.strip_ansi(result.stdout or "")
    if result.returncode != 0:
        return fail(f"wine run failed ({result.returncode})\n{log[-2000:]}")
    for marker in ("GX_SHELL: entry", "MODULE: loaded", "module probe ran"):
        if marker not in log:
            return fail(f"missing {marker!r}")
    print("MINGW_SCRIPTING_CHECK: PASS — gx_shell.dll handshake and script run")
    return 0


if __name__ == "__main__":
    sys.exit(main())
