#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: sky editor gate — the panel opens through both entry paths
#   (GX_TOOLS_OPEN + the CLI flag), the opener routes to the sky scene, the
#   panel traces its draw, and the unknown-value hint stays honest. The
#   preset round trip + the shipped sample load in --test-only (sky tests).
import os
import subprocess
import sys
from pathlib import Path

import audit_common as audit


ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "build" / "gX_build"
DISPLAY = os.environ.get("DISPLAY", ":1")
TIMEOUT = 240


def clean_env(**extra):
    environment = {k: v for k, v in os.environ.items() if not k.startswith("GX_")}
    environment["DISPLAY"] = DISPLAY
    environment["GX_NO_SETTINGS_SAVE"] = "1"
    environment["GX_FRAMES"] = "48"
    environment.update(extra)
    return environment


def run(args, env):
    return subprocess.run([str(BIN), *args], cwd=str(ROOT), env=env,
                          capture_output=True, text=True, timeout=TIMEOUT)


def main():
    failures = []

    #opencode: GX_TOOLS_OPEN path — the same runtime opener the EXTRAS card
    #   calls (the opener routes to the sky scene itself).
    result = run(["--screenWidth", "1024", "--screenHeight", "768"],
                 clean_env(GX_TOOLS_OPEN="sky"))
    log = audit.strip_ansi(result.stdout + result.stderr)
    if result.returncode != 0 or "Tools: sky editor opened at runtime" not in log:
        failures.append(f"GX_TOOLS_OPEN path failed (exit {result.returncode})")
    if "RENDERER_SKY" not in log:
        failures.append("GX_TOOLS_OPEN path did not route to the sky scene")
    if "SKY_EDITOR_UI: panel=1" not in log:
        failures.append("GX_TOOLS_OPEN path did not draw the panel")

    #opencode: CLI flag path (--sky-editor), same routing.
    result = run(["--sky-editor", "--screenWidth", "1024", "--screenHeight", "768"],
                 clean_env())
    log = audit.strip_ansi(result.stdout + result.stderr)
    if result.returncode != 0 or "Tools: sky editor opened at runtime" not in log:
        failures.append(f"--sky-editor path failed (exit {result.returncode})")

    #opencode: the unknown-value hint names the accepted set.
    result = run([], clean_env(GX_TOOLS_OPEN="sky_typo"))
    log = audit.strip_ansi(result.stdout + result.stderr)
    if "expects editor|theme|themlab|materials|gfx|script|wavetable|textwriter|physics|rail|background|sky" not in log:
        failures.append("unknown GX_TOOLS_OPEN value missing the hint")

    if failures:
        for failure in failures:
            print(f"SKY_EDITOR_CHECK: FAIL — {failure}")
        return 1
    print("SKY_EDITOR_CHECK: PASS (runtime + CLI open with routing, panel trace, hint)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
