#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: drive Planar only and model-follow controls through GX_AI_INPUT,
#   then verify editor save and preset load/save round-trip.

import copy
import json
import os
from pathlib import Path

import audit_common as audit
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

#opencode: use rendered dialog geometry to target overwrite across variable widths.
from gfx_ui_probe import overwrite_button_centers


ROOT = audit.ROOT
BIN = audit.BIN
SOURCE_PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"


#opencode: keep the test scene small and start with both selected-model modes off.
def make_fixture(path, startPlanarWithSSR=False):
    fixture = json.loads(SOURCE_PRESET.read_text())
    fixture["models"] = [copy.deepcopy(fixture["models"][0])]
    params = fixture["models"][0]["params"]
    params.update({
        "planarReflection": False,
        "nonPlanarReflection": False,
        "enableReflections": False,
        "receiveSSR": True,
        "reflectionPlaneFollowsModel": False,
    })
    if startPlanarWithSSR:
        params.update({"planarReflection": True, "receiveSSR": True})
    fixture["state"].update({
        "ENABLE_REFLECTIONS": False,
        "ENABLE_PLANAR_REFLECTIONS": startPlanarWithSSR,
        "ENABLE_SSR": startPlanarWithSSR,
        "ENABLE_SSR_AUXVIEW": False,
        "ENABLE_SSAO": False,
        "ENABLE_BLOOM": False,
        "ENABLE_TAA": False,
        "ENABLE_SKYBOX": False,
        "ENABLE_PROCEDURAL_SKY": False,
    })
    path.write_text(json.dumps(fixture, indent=2) + "\n")


#opencode: capture a clean layout first so AI clicks scale with the active window.

#opencode: this check's click offsets assume the fitted visible-window client
#   height; hidden windows report the unfitted height (1016 vs 1010), so it
#   requests the unfocused off-screen mode instead — still no pointer grab.
def audit_window_env(environment):
    if os.environ.get("GX_AUDIT_ONSCREEN") != "1":
        environment["GX_AUDIT_WINDOW"] = "1"
    return environment

#opencode: the visible audit window (mode 1, required for the calibrated click
#   offsets) can stall on Xwayland when many windows are created back-to-back,
#   so each attempt is bounded and retried; every attempt gets its own dir.
def capture_layout(temp_root, preset, attempts=3):
    last_error = None
    for attempt in range(attempts):
        directory = temp_root / f"layout{attempt}"
        #opencode: the whole exercise can retry, so the per-attempt dir may
        #   already exist; stale shots are older and newest_shot ignores them.
        directory.mkdir(exist_ok=True)
        for stale in directory.glob("gXss_*.png"):
            stale.unlink()
        environment = dict(os.environ)
        #opencode: harness runs must not rewrite the user's settings file.
        environment["GX_NO_SETTINGS_SAVE"] = "1"
        for key in ("GX_AI_INPUT", "GX_GFX_PRESET_SAVE", "GX_GFX_PRESET_BATCH"):
            environment.pop(key, None)
        environment.update({
            "DISPLAY": os.environ.get("DISPLAY", ":1"),
            "GX_FIXED_DT": "0.0166666667",
            "GX_GFX_PRESET_LOAD": str(preset),
            "GX_SCREENSHOT_DIR": str(directory),
            "GX_SHOT_FRAME": "60",
        })
        audit_window_env(environment)
        try:
            result = subprocess.run(
                [str(BIN), "--gfx", "--gfx-editor", "--screenWidth", "1536",
                 "--screenHeight", "1016", "--ai-screenshot-after", "60"],
                cwd=ROOT, env=environment, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60,
            )
        except subprocess.TimeoutExpired as error:
            last_error = error
            continue
        if result.returncode != 0:
            last_error = RuntimeError(
                f"layout capture failed ({result.returncode})\n{result.stdout[-3000:]}")
            continue
        screenshots = list(directory.glob("gXss_*.png"))
        if not screenshots:
            last_error = RuntimeError(
                f"layout screenshot missing\n{result.stdout[-3000:]}")
            continue
        image = Image.open(max(screenshots, key=lambda path: path.stat().st_mtime))
        return image.width, image.height
    raise last_error if last_error else RuntimeError("layout capture failed")


#opencode: open the Surface subtab, choose a reflection mode, and optionally save it.
def edit_with_ui(preset, temp_root, width, height, confirm=None,
                 frame=600, expectSave=True, outputName="ui_edit",
                 targetMode="Planar only", toggleFollow=True):
    screenshot_dir = temp_root / outputName
    #opencode: the whole exercise can retry, so a previous attempt's capture
    #   directory may exist; drop its shots so the dialog reader only sees the
    #   current run.
    screenshot_dir.mkdir(exist_ok=True)
    for stale in screenshot_dir.glob("gXss_*.png"):
        stale.unlink()
    surfaceX = width - 301
    surfaceY = height - 782
    modeX = width - 296
    modeY = height - 690
    #opencode: combo rows sit on ~18px pitch; aim at measured row centers
    #   (Off 335-351 -> height-667, Planar only 388-405 -> height-614). The
    #   previous -660/-608 sat 1-3px from row boundaries, so a window-height
    #   rounding flip or a shifted popup selected the neighbouring mode.
    modeOptionY = height - (667 if targetMode == "Off" else 614)
    actions = [
        f"0.8 move {surfaceX} {surfaceY} 250",
        f"1.2 click {surfaceX} {surfaceY}",
        f"2.0 move {modeX} {modeY} 250",
        f"2.4 click {modeX} {modeY}",
        f"3.0 move {modeX} {modeOptionY} 250",
        f"3.4 click {modeX} {modeOptionY}",
    ]
    if toggleFollow:
        followX = width - 362
        #opencode: the follow checkbox square spans 585-603 at the calibrated
        #   height; height-416 is its center (the old -420 sat near the top).
        followY = height - 416
        actions.extend((f"4.0 move {followX} {followY} 250",
                        f"4.4 click {followX} {followY}"))
    saveX = width - 252
    actions.extend((f"5.0 move {saveX} 102 250", f"5.4 click {saveX} 102"))
    if confirm:
        actions.append(f"7.8 click {confirm[0]} {confirm[1]}")
    environment = dict(os.environ)
    #opencode: the editor persists settings on exit; restore-after-exit raced
    #   that write, so never let these runs save settings at all.
    environment["GX_NO_SETTINGS_SAVE"] = "1"
    for key in ("GX_GFX_PRESET_SAVE", "GX_GFX_PRESET_BATCH"):
        environment.pop(key, None)
    environment.update({
        "DISPLAY": os.environ.get("DISPLAY", ":1"),
        "GX_FIXED_DT": "0.0166666667",
        "GX_GFX_PRESET_LOAD": str(preset),
        "GX_AI_INPUT": ";".join(actions),
        "GX_SCREENSHOT_DIR": str(screenshot_dir),
        "GX_SHOT_FRAME": str(frame),
    })
    audit_window_env(environment)
    result = subprocess.run(
        [str(BIN), "--gfx", "--gfx-editor", "--screenWidth", "1536",
         "--screenHeight", "1016", "--ai-screenshot-after", str(frame)],
        cwd=ROOT, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120,
    )
    if result.returncode != 0:
        raise RuntimeError(f"UI edit failed ({result.returncode})\n{result.stdout[-3000:]}")
    #ai: GFX module tag — the PP unit test logs the same suffix.
    saved = "[GFX] Saving preset (new schema) to" in result.stdout
    if expectSave and not saved:
        screenshots = list(screenshot_dir.glob("gXss_*.png"))
        if screenshots:
            failed_capture = Path("/tmp/opencode/gfx_reflection_ui_failed.png")
            shutil.copyfile(max(screenshots, key=lambda path: path.stat().st_mtime),
                            failed_capture)
            capture_note = f"; screenshot retained at {failed_capture}"
        else:
            capture_note = "; screenshot missing"
        raise RuntimeError(
            f"editor Save preset action did not run{capture_note}\n{result.stdout[-3000:]}")
    if not expectSave and saved:
        raise RuntimeError("opening the overwrite prompt unexpectedly wrote the preset")
    return result.stdout


#opencode: run the production preset loader and serializer on the UI-edited file.
def reload_and_save(preset, output, temp_root):
    manifest = temp_root / "roundtrip.tsv"
    manifest.write_text(f"{preset}\t{output}\n")
    environment = dict(os.environ)
    for key in ("GX_AI_INPUT", "GX_GFX_PRESET_LOAD", "GX_GFX_PRESET_SAVE"):
        environment.pop(key, None)
    environment.update({
        "DISPLAY": os.environ.get("DISPLAY", ":1"),
        "GX_FRAMES": "3",
        "GX_GFX_PRESET_BATCH": str(manifest),
    })
    audit_window_env(environment)
    result = subprocess.run(
        [str(BIN), "--gfx", "--gfx-editor"], cwd=ROOT, env=environment,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120,
    )
    if result.returncode != 0:
        raise RuntimeError(f"preset reload failed ({result.returncode})\n{result.stdout[-3000:]}")
    if result.stdout.count("[GFX] Load done") != 1 or result.stdout.count("[GFX] Save done") != 1:
        raise RuntimeError(f"preset load/save round-trip missing\n{result.stdout[-3000:]}")


#opencode: interpret schema defaults and pin the selected model's serialized reflection contract.
def check_values(label, path):
    document = json.loads(path.read_text())
    if not document.get("models"):
        raise RuntimeError(f"{label}: model missing")
    params = document["models"][0]["params"]
    expected = {
        "planarReflection": True,
        "nonPlanarReflection": False,
        "enableReflections": False,
        "receiveSSR": False,
        "reflectionPlaneFollowsModel": True,
    }
    differences = {
        key: (value, params.get(key, "<missing>"))
        for key, value in expected.items() if params.get(key) is not value
    }
    if differences:
        raise RuntimeError(f"{label}: selected-model reflection fields differ: {differences}")
    planar_gate = document.get("state", {}).get("ENABLE_PLANAR_REFLECTIONS", True)
    if planar_gate is not True:
        raise RuntimeError(f"{label}: planar capture gate is off")
    if document.get("state", {}).get("ENABLE_SSR", False) is not False:
        raise RuntimeError(f"{label}: Planar only unexpectedly enabled global SSR")
    if document.get("state", {}).get("ENABLE_REFLECTIONS", True) is not False:
        raise RuntimeError(f"{label}: Planar only enabled environment reflections")


#opencode: ensure choosing Off removes all per-model reflection receivers but
#   leaves the scene-wide systems available to other mirror surfaces.
def check_plain_values(label, path):
    document = json.loads(path.read_text())
    params = document["models"][0]["params"]
    expected = {
        "planarReflection": False,
        "nonPlanarReflection": False,
        "enableReflections": False,
        "receiveSSR": False,
    }
    differences = {
        key: (value, params.get(key, "<missing>"))
        for key, value in expected.items() if params.get(key) is not value
    }
    if differences:
        raise RuntimeError(f"{label}: plain-object reflection fields differ: {differences}")
    state = document.get("state", {})
    if state.get("ENABLE_PLANAR_REFLECTIONS", True) is not True:
        raise RuntimeError(f"{label}: clearing one model disabled scene-wide planar captures")
    if state.get("ENABLE_SSR") is not True:
        raise RuntimeError(f"{label}: clearing one model disabled scene-wide SSR")


#opencode: settings safety and window isolation live in audit_common.
run_with_settings_preserved = audit.with_settings_preserved


#opencode: verify UI-driven edits and their loaded preset round-trip.
def main():
    if not BIN.is_file():
        print(f"GFX_REFLECTION_UI_CHECK: binary missing: {BIN}", file=sys.stderr)
        return 1
    os.makedirs("/tmp/opencode", exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_reflection_ui_",
                                         dir="/tmp/opencode") as directory:
            temp_root = Path(directory)
            preset = temp_root / "ui_edited.json"
            roundtrip = temp_root / "roundtrip.json"
            plainPreset = temp_root / "plain_object.json"
            plainRoundtrip = temp_root / "plain_object_roundtrip.json"
            def exercise():
                #opencode: recreate the fixtures per attempt — a flaked attempt
                #   may have partially saved the preset.
                make_fixture(preset)
                make_fixture(plainPreset, startPlanarWithSSR=True)
                width, height = capture_layout(temp_root, preset)
                edit_with_ui(preset, temp_root, width, height,
                    frame=450, expectSave=False, outputName="overwrite_prompt")
                promptScreens = list((temp_root / "overwrite_prompt").glob("gXss_*.png"))
                if not promptScreens:
                    raise RuntimeError("overwrite prompt screenshot missing")
                overwriteX, _, dialogY = overwrite_button_centers(max(
                    promptScreens, key=lambda path: path.stat().st_mtime))
                edit_with_ui(preset, temp_root, width, height,
                    confirm=(overwriteX, dialogY))
                check_values("editor save", preset)
                reload_and_save(preset, roundtrip, temp_root)
                check_values("editor reload/save", roundtrip)
                edit_with_ui(plainPreset, temp_root, width, height,
                    frame=450, expectSave=False, outputName="plain_overwrite_prompt",
                    targetMode="Off", toggleFollow=False)
                plainPromptScreens = list(
                    (temp_root / "plain_overwrite_prompt").glob("gXss_*.png"))
                if not plainPromptScreens:
                    raise RuntimeError("plain-object overwrite prompt screenshot missing")
                plainOverwriteX, _, plainDialogY = overwrite_button_centers(max(
                    plainPromptScreens, key=lambda path: path.stat().st_mtime))
                edit_with_ui(plainPreset, temp_root, width, height,
                    confirm=(plainOverwriteX, plainDialogY), targetMode="Off",
                    toggleFollow=False, outputName="plain_object_edit")
                check_plain_values("plain-object editor save", plainPreset)
                reload_and_save(plainPreset, plainRoundtrip, temp_root)
                check_plain_values("plain-object reload/save", plainRoundtrip)
            last_error = None
            #opencode: three attempts — the overwrite-dialog probe can miss a
            #   prompt on a slow start, and each retry recreates the fixtures.
            for attempt in range(3):
                try:
                    audit.with_settings_preserved(exercise)
                    break
                except (OSError, ValueError, RuntimeError,
                        subprocess.SubprocessError) as error:
                    last_error = error
                    print(f"GFX_REFLECTION_UI_CHECK: attempt {attempt + 1} "
                          f"flaked ({error}); retrying", file=sys.stderr)
            else:
                raise last_error if last_error else RuntimeError(
                    "reflection UI exercise failed")
            print("GFX_REFLECTION_UI_CHECK: GX_AI_INPUT selected Planar only, enabled model-follow, and saved")
            print("GFX_REFLECTION_UI_CHECK: loaded preset retained those fields")
            print("GFX_REFLECTION_UI_CHECK: Off cleared per-model mirror and SSR reception without disabling global reflection gates")
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"GFX_REFLECTION_UI_CHECK: FAIL: {error}", file=sys.stderr)
        return 1
    print("GFX_REFLECTION_UI_CHECK: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
