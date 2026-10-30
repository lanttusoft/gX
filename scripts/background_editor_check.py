#!/usr/bin/env python3
#opencode: background editor gate — the panel opens through both entry paths
#   (GX_TOOLS_OPEN + the CLI flag), the opener routes to the background scene,
#   the panel traces its draw, the shipped sample preset stays loadable, and
#   the unknown-value hint stays honest. The preset save/load data path is
#   covered by the background module's own tests.
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
    #   calls (the opener routes to the background scene itself).
    result = run(["--screenWidth", "1024", "--screenHeight", "768"],
                 clean_env(GX_TOOLS_OPEN="background"))
    log = audit.strip_ansi(result.stdout + result.stderr)
    if result.returncode != 0 or "Tools: background editor opened at runtime" not in log:
        failures.append(f"GX_TOOLS_OPEN path failed (exit {result.returncode})")
    if "RENDERER_BACKGROUND" not in log:
        failures.append("GX_TOOLS_OPEN path did not route to the background scene")
    if "BG_EDITOR_UI: panel=1" not in log:
        failures.append("GX_TOOLS_OPEN path did not draw the panel")

    #opencode: CLI flag path (--background-editor), same routing.
    result = run(["--background-editor", "--screenWidth", "1024",
                  "--screenHeight", "768"], clean_env())
    log = audit.strip_ansi(result.stdout + result.stderr)
    if result.returncode != 0 or "Tools: background editor opened at runtime" not in log:
        failures.append(f"--background-editor path failed (exit {result.returncode})")

    #opencode: the shipped sample preset must stay loadable (the editor's
    #   fixture list ships it; the background scene load path exercises it).
    result = run(["--background", "--bg-preset",
                  "resources/tools/background/palette_showcase.json",
                  "--screenWidth", "1024", "--screenHeight", "768"],
                 clean_env(GX_FRAMES="150"))
    log = audit.strip_ansi(result.stdout + result.stderr)
    if (result.returncode != 0
            or ("Loading preset (new schema) from "
                "resources/tools/background/palette_showcase.json") not in log):
        failures.append(f"sample preset did not load (exit {result.returncode})")

    #opencode: the unknown-value hint names the accepted set (keeps the env
    #   hook self-documenting when a typo sneaks into a gate).
    result = run([], clean_env(GX_TOOLS_OPEN="background_typo"))
    log = audit.strip_ansi(result.stdout + result.stderr)
    if "expects editor|theme|themlab|materials|gfx|script|wavetable|textwriter|physics|rail|background" not in log:
        failures.append("unknown GX_TOOLS_OPEN value missing the hint")

    if failures:
        for failure in failures:
            print(f"BACKGROUND_EDITOR_CHECK: FAIL — {failure}")
        return 1
    print("BACKGROUND_EDITOR_CHECK: PASS (runtime + CLI open with routing, sample preset, hint)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
