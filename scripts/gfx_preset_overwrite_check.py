#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: verify existing GFX presets require an explicit save-over decision.

import hashlib
from pathlib import Path

import audit_common as audit
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

#opencode: share the screenshot-measured overwrite dialog button detector.
from gfx_ui_probe import overwrite_button_centers


ROOT = audit.ROOT
BIN = audit.BIN
SOURCE = ROOT / "resources" / "tools" / "art2.json"


#opencode: run the editor against a private existing file, with virtual clicks only.
def run_save(preset, output, actions, frame=180):
    output.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_GFX_PRESET_LOAD": str(preset),
        "GX_AI_INPUT": actions,
        "GX_SCREENSHOT_DIR": str(output),
        "GX_SHOT_FRAME": str(frame),
    })
    for key in ("GX_AI_INPUT", "GX_GFX_PRESET_SAVE", "GX_GFX_PRESET_BATCH"):
        environment.pop(key, None)
    if actions:
        environment["GX_AI_INPUT"] = actions
    result = subprocess.run(
        [str(BIN), "--gfx", "--gfx-editor", "--screenWidth", "1536",
         "--screenHeight", "1016", "--ai-screenshot-after", str(frame)],
        cwd=ROOT, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120,
    )
    if result.returncode != 0:
        raise RuntimeError(f"editor exited {result.returncode}\n{result.stdout[-3000:]}")
    return audit.strip_ansi(result.stdout)


#opencode: settings safety and window isolation live in audit_common.
with_settings_preserved = audit.with_settings_preserved


#opencode: ensure Cancel leaves bytes untouched and Overwrite performs the save.
def main():
    if not BIN.is_file() or not SOURCE.is_file():
        print("GFX_PRESET_OVERWRITE_CHECK: missing binary or fixture", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="go_",
                                         dir="/tmp/opencode") as temp:
            root = Path(temp)
            def exercise():
                preset = root / "preset.json"
                shutil.copyfile(SOURCE, preset)
                beforePrompt = hashlib.sha256(preset.read_bytes()).hexdigest()
                promptDir = root / "prompt"
                promptLog = run_save(preset, promptDir,
                    "0.8 click 1568 103", frame=90)
                promptShots = list(promptDir.glob("gXss_*.png"))
                if not promptShots:
                    raise RuntimeError("overwrite prompt screenshot missing")
                imagePath = max(promptShots, key=lambda path: path.stat().st_mtime)
                width, height = Image.open(imagePath).size
                overwriteX, cancelX, dialogY = overwrite_button_centers(imagePath)
                saveX, saveY = width - round(width * 0.132), round(height * 0.102)
                #ai: the PP unit test also logs "Saving preset (new schema) to";
                #   match the GFX module tag so the marker is unambiguous.
                if "[GFX] Saving preset (new schema) to" in promptLog:
                    raise RuntimeError("opening the overwrite prompt wrote the preset")
                if hashlib.sha256(preset.read_bytes()).hexdigest() != beforePrompt:
                    raise RuntimeError("opening the overwrite prompt changed file bytes")

                cancelLog = run_save(preset, root / "cancel", ";".join((
                    f"0.8 click {saveX} {saveY}",
                    f"1.8 click {cancelX} {dialogY}",
                )))
                if "[GFX] Saving preset (new schema) to" in cancelLog:
                    raise RuntimeError("Cancel still wrote the existing preset")
                if hashlib.sha256(preset.read_bytes()).hexdigest() != beforePrompt:
                    raise RuntimeError("Cancel changed the existing preset bytes")

                confirmLog = run_save(preset, root / "confirm", ";".join((
                    f"0.8 click {saveX} {saveY}",
                    f"1.8 click {overwriteX} {dialogY}",
                )))
                if "[GFX] Saving preset (new schema) to" not in confirmLog:
                    raise RuntimeError(f"Overwrite confirmation did not save\n{confirmLog[-3000:]}")
                if hashlib.sha256(preset.read_bytes()).hexdigest() == beforePrompt:
                    raise RuntimeError("confirmed overwrite left the preset unchanged")

            with_settings_preserved(exercise)
            print("GFX_PRESET_OVERWRITE_CHECK: Cancel preserved the file; explicit Overwrite saved it")
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"GFX_PRESET_OVERWRITE_CHECK: FAIL: {error}", file=sys.stderr)
        return 1
    print("GFX_PRESET_OVERWRITE_CHECK: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
