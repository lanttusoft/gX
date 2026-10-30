#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: wavetable editor gate — the panel opens through both entry paths
#   (GX_TOOLS_OPEN and --wavetable-editor), renders frames, exits clean, and
#   the unknown-value hint stays honest. The data path (save/load round trip,
#   unloaded/invalid slots, the shipped-fixture harness guard) is pinned by
#   the synth unit tests in --test-only; this gate covers the editor surface
#   and its wiring.
import os
import subprocess
import sys
from pathlib import Path

import audit_common as audit


ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "build" / "gX_build"
DISPLAY = os.environ.get("DISPLAY", ":1")
TIMEOUT = 180


def clean_env(**extra):
    environment = {k: v for k, v in os.environ.items() if not k.startswith("GX_")}
    environment["DISPLAY"] = DISPLAY
    environment["GX_NO_SETTINGS_SAVE"] = "1"
    environment["GX_SYNTH_MUTE"] = "1"
    environment["GX_FRAMES"] = "40"
    environment.update(extra)
    return environment


def run(args, env):
    return subprocess.run([str(BIN), *args], cwd=str(ROOT), env=env,
                          capture_output=True, text=True, timeout=TIMEOUT)


def main():
    failures = []

    #opencode: GX_TOOLS_OPEN path — the same runtime opener the EXTRAS cards
    #   call, so a green run covers the in-app path too.
    result = run(["--screenWidth", "1024", "--screenHeight", "768"],
                 clean_env(GX_TOOLS_OPEN="wavetable"))
    log = audit.strip_ansi(result.stdout + result.stderr)
    if result.returncode != 0 or "Tools: wavetable editor opened at runtime" not in log:
        failures.append(f"GX_TOOLS_OPEN path failed (exit {result.returncode})")

    #opencode: CLI flag path (--wavetable-editor), no routing required.
    result = run(["--wavetable-editor", "--screenWidth", "1024", "--screenHeight", "768"],
                 clean_env())
    log = audit.strip_ansi(result.stdout + result.stderr)
    if result.returncode != 0 or "Tools: wavetable editor opened at runtime" not in log:
        failures.append(f"--wavetable-editor path failed (exit {result.returncode})")

    #opencode: the unknown-value hint names the accepted set (keeps the env
    #   hook self-documenting when a typo sneaks into a gate).
    result = run([], clean_env(GX_TOOLS_OPEN="wavetable_typo"))
    log = audit.strip_ansi(result.stdout + result.stderr)
    if "expects editor|theme|themlab|materials|gfx|script|wavetable" not in log:
        failures.append("unknown GX_TOOLS_OPEN value missing the hint")

    if failures:
        for failure in failures:
            print(f"WAVETABLE_EDITOR_CHECK: FAIL — {failure}")
        return 1
    print("WAVETABLE_EDITOR_CHECK: PASS (runtime + CLI open, unknown-value hint)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
