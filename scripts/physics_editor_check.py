#!/usr/bin/env python3
#ai: physics editor gate — the panel opens through both entry paths
#   (GX_TOOLS_OPEN and --physics-editor), routes to the physics sandbox and
#   renders frames cleanly, and the unknown-value hint stays honest. The data
#   path (tuning defaults, JSON round trip, shipped-path guard, live apply)
#   is pinned by the scene unit tests in --test-only; this gate covers the
#   editor surface and its wiring.
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
    #ai: the sandbox now plays procedural impact/tool SFX — keep gate runs
    #   silent on the dev machine (the synth's GX_SYNTH_MUTE precedent).
    environment["GX_AUDIO_MUTE"] = "1"
    environment["GX_FRAMES"] = "48"
    environment.update(extra)
    return environment


def run(args, env):
    return subprocess.run([str(BIN), *args], cwd=str(ROOT), env=env,
                          capture_output=True, text=True, timeout=TIMEOUT)


def main():
    failures = []

    #ai: GX_TOOLS_OPEN path — the same runtime opener the EXTRAS card calls
    #   (and the opener routes to the sandbox scene itself).
    result = run(["--screenWidth", "1024", "--screenHeight", "768"],
                 clean_env(GX_TOOLS_OPEN="physics"))
    log = audit.strip_ansi(result.stdout + result.stderr)
    if result.returncode != 0 or "Tools: physics editor opened at runtime" not in log:
        failures.append(f"GX_TOOLS_OPEN path failed (exit {result.returncode})")
    if "PHYSICS_SCENE: Loaded" not in log:
        failures.append("GX_TOOLS_OPEN path did not route to the sandbox scene")

    #ai: CLI flag path (--physics-editor), same routing.
    result = run(["--physics-editor", "--screenWidth", "1024",
                  "--screenHeight", "768"], clean_env())
    log = audit.strip_ansi(result.stdout + result.stderr)
    if result.returncode != 0 or "Tools: physics editor opened at runtime" not in log:
        failures.append(f"--physics-editor path failed (exit {result.returncode})")

    #ai: the unknown-value hint names the accepted set (keeps the env hook
    #   self-documenting when a typo sneaks into a gate).
    result = run([], clean_env(GX_TOOLS_OPEN="physics_typo"))
    log = audit.strip_ansi(result.stdout + result.stderr)
    if "expects editor|theme|themlab|materials|gfx|script|wavetable|textwriter|physics" not in log:
        failures.append("unknown GX_TOOLS_OPEN value missing the hint")

    #ai: toolgun leg — GX_TOOLS_TOOLGUN=1 opens the shared crosshair/tool
    #   mode over the sandbox's free camera, hands the sandbox's mouse
    #   interaction over and renders a final frame with the overlay.
    shot_dir = ROOT / "build" / "physics_editor_check_shot"
    shot_dir.mkdir(parents=True, exist_ok=True)
    for stale in shot_dir.glob("*.png"):
        stale.unlink()
    result = run(["--physics-editor", "--screenWidth", "1024",
                  "--screenHeight", "768", "--ai-screenshot-final"],
                 clean_env(GX_TOOLS_TOOLGUN="1", GX_FRAMES="120",
                           GX_SHOT_FRAME="90",
                           GX_SCREENSHOT_DIR=str(shot_dir)))
    log = audit.strip_ansi(result.stdout + result.stderr)
    if result.returncode != 0 or "TOOLGUN: on (6 tools)" not in log:
        failures.append(f"toolgun mode failed to open (exit {result.returncode})")
    if "PHYSICS_EDITOR: toolgun on" not in log:
        failures.append("toolgun open left no editor trace")
    if "PHYSICS_SCENE: tool mode on" not in log:
        failures.append("toolgun did not hand the sandbox mouse input over")
    if not list(shot_dir.glob("*.png")):
        failures.append("toolgun capture produced no screenshot")

    if failures:
        for failure in failures:
            print(f"PHYSICS_EDITOR_CHECK: FAIL — {failure}")
        return 1
    print("PHYSICS_EDITOR_CHECK: PASS (runtime + CLI open with routing, "
          "unknown-value hint, toolgun capture)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
