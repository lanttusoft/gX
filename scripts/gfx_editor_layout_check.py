#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GFX editor narrow-layout gate — pin the scene/preset rows, renderer-lab
#   strip and shortcut footer at 1024x768 and 1600x900, including 150/175%
#   tool text scales. GFX_LAYOUT reports post-layout fit flags; display hidden.
import os
import re
import subprocess
import sys
from pathlib import Path

import audit_common as audit


ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "build" / "gX_build"
SETTINGS = ROOT / "resources" / "settings.json"
TRACE_RE = re.compile(
    r"GFX_LAYOUT: window=(\d+)x(\d+) scene=(\d+) inspector=(\d+) "
    r"stage=(\d+) viewport=(\d+) pick=(\d+) buttons=(\d+) preset=(\d+) "
    r"presetW=(\d+)/(\d+) labs=(\d+) rows=(\d+) footer=(\d+) (\d+)x(\d+)")
KEYS = ("windowW", "windowH", "scene", "inspector", "stage", "viewport",
        "pick", "buttons", "preset", "presetTextW", "presetFieldW", "labs",
        "rows", "footer", "footerW", "footerH")
GRID_RE = re.compile(
    r"GFX_ASSET_GRID path=(\S+) avail=(\d+) item=(\d+) "
    r"line=(\d+) height=(\d+)")
PICKER_GEOM_RE = re.compile(
    r"EDITOR_PICKER: geometry id=(\S+) window=(\d+),(\d+),(\d+),(\d+) "
    r"items=(\d+)")
PICKER_ITEM_RE = re.compile(
    r"EDITOR_PICKER: item id=(\S+) path=(\S+) avail=(\d+) item=(\d+)")


def run(name, width, height, scale, level):
    environment = dict(os.environ)
    environment.update({
        "DISPLAY": audit.display(),
        "GX_AUDIT_WINDOW": "hidden",
        "GX_NO_SETTINGS_SAVE": "1",
        "GX_SKIP_TESTS": "1",
        "GX_DOCK_DISABLE": "1",
        "GX_FRAMES": "80",
        "GX_UI_SCALE": scale,
        "GX_UI_LEVEL": level,
        "GX_GFX_LAYOUT_TRACE": "1",
    })
    try:
        result = subprocess.run(
            [str(BIN), "--gfx", "--gfx-editor",
             "--screenWidth", str(width), "--screenHeight", str(height)],
            cwd=str(ROOT), env=environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    except (OSError, subprocess.SubprocessError) as error:
        raise RuntimeError(f"{name}: {error}") from error
    log = audit.strip_ansi(result.stdout or "")
    if result.returncode != 0:
        raise RuntimeError(f"{name}: editor exited {result.returncode}: "
                           f"{log[-500:]}")
    matches = TRACE_RE.findall(log)
    if not matches:
        raise RuntimeError(f"{name}: GFX_LAYOUT trace missing")
    return dict(zip(KEYS, (int(value) for value in matches[-1])))


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def check_asset_grid(name, width, height, scale):
    environment = dict(os.environ)
    environment.update({
        "DISPLAY": audit.display(),
        "GX_AUDIT_WINDOW": "hidden",
        "GX_NO_SETTINGS_SAVE": "1",
        "GX_SKIP_TESTS": "1",
        "GX_DOCK_DISABLE": "1",
        "GX_FRAMES": "80",
        "GX_UI_SCALE": scale,
        "GX_GFX_LAYOUT_TRACE": "1",
        "GX_GFX_ASSET_BROWSER": "1",
    })
    try:
        result = subprocess.run(
            [str(BIN), "--gfx", "--gfx-editor",
             "--screenWidth", str(width), "--screenHeight", str(height)],
            cwd=str(ROOT), env=environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    except (OSError, subprocess.SubprocessError) as error:
        raise RuntimeError(f"{name}: {error}") from error
    log = audit.strip_ansi(result.stdout or "")
    if result.returncode != 0:
        raise RuntimeError(f"{name}: asset browser exited {result.returncode}: "
                           f"{log[-500:]}")
    require("GFX_ASSET_BROWSER: open" in log,
            f"{name}: asset browser did not open")
    rows = GRID_RE.findall(log)
    require(rows, f"{name}: asset grid trace missing")
    for path, avail, item, line, item_height in rows:
        require(int(item) <= int(avail) + 1,
                f"{name}: grid path '{path}' is {item}px in a {avail}px cell")
        require(int(item_height) <= int(line) * 3 // 2 + 1,
                f"{name}: grid path '{path}' wraps to {item_height}px "
                f"(line {line}px)")


def check_preset_picker(name, width, height, scale):
    environment = dict(os.environ)
    environment.update({
        "DISPLAY": audit.display(),
        "GX_AUDIT_WINDOW": "hidden",
        "GX_NO_SETTINGS_SAVE": "1",
        "GX_SKIP_TESTS": "1",
        "GX_DOCK_DISABLE": "1",
        "GX_FRAMES": "80",
        "GX_UI_SCALE": scale,
        "GX_GFX_LAYOUT_TRACE": "1",
        "GX_GFX_PRESET_PICKER": "1",
        "GX_EDITOR_PICKER_TRACE": "1",
    })
    try:
        result = subprocess.run(
            [str(BIN), "--gfx", "--gfx-editor",
             "--screenWidth", str(width), "--screenHeight", str(height)],
            cwd=str(ROOT), env=environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    except (OSError, subprocess.SubprocessError) as error:
        raise RuntimeError(f"{name}: {error}") from error
    log = audit.strip_ansi(result.stdout or "")
    if result.returncode != 0:
        raise RuntimeError(f"{name}: preset picker exited {result.returncode}: "
                           f"{log[-500:]}")
    for command in ("gfx.preset.load", "gfx.preset.reload", "gfx.preset.save"):
        require(f"EDITOR_COMMAND: register {command}" in log,
                f"{name}: command {command} was not registered")
    geometry = PICKER_GEOM_RE.search(log)
    require(geometry, f"{name}: picker geometry trace missing")
    _, x0, y0, x1, y1, items = geometry.groups()
    require(int(x0) >= 0 and int(y0) >= 0
            and int(x1) <= width and int(y1) <= height,
            f"{name}: picker window {x0},{y0},{x1},{y1} leaves the display")
    require(int(items) > 0, f"{name}: picker has no preset items")
    rows = PICKER_ITEM_RE.findall(log)
    require(rows, f"{name}: picker item trace missing")
    for _, path, avail, item in rows:
        require(int(item) <= int(avail) + 16,
                f"{name}: picker path '{path}' is {item}px in a {avail}px row")


def check_preset_document(name):
    environment = dict(os.environ)
    environment.update({
        "DISPLAY": audit.display(),
        "GX_AUDIT_WINDOW": "hidden",
        "GX_NO_SETTINGS_SAVE": "1",
        "GX_SKIP_TESTS": "1",
        "GX_DOCK_DISABLE": "1",
        "GX_FRAMES": "80",
        "GX_UI_SCALE": "1.00",
        "GX_GFX_LAYOUT_TRACE": "1",
        "GX_GFX_PRESET_TOUCH": "1",
        "GX_GFX_PRESET_LOAD": "resources/tools/gfx_showcase.json",
        "GX_GFX_PRESET_CLOSE": "1",
    })
    try:
        result = subprocess.run(
            [str(BIN), "--gfx", "--gfx-editor",
             "--screenWidth", "1024", "--screenHeight", "768"],
            cwd=str(ROOT), env=environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    except (OSError, subprocess.SubprocessError) as error:
        raise RuntimeError(f"{name}: {error}") from error
    log = audit.strip_ansi(result.stdout or "")
    if result.returncode != 0:
        raise RuntimeError(f"{name}: document run exited {result.returncode}: "
                           f"{log[-500:]}")
    require("EDITOR_DOCUMENT: dirty id=gfx.preset" in log,
            f"{name}: preset document did not become dirty")
    require("EDITOR_DOCUMENT: clean id=gfx.preset" in log,
            f"{name}: preset load did not clear dirty state")
    require("GFX_PRESET: close guard=clean" in log,
            f"{name}: clean preset did not allow close")


def check_preset_close(name):
    environment = dict(os.environ)
    environment.update({
        "DISPLAY": audit.display(),
        "GX_AUDIT_WINDOW": "hidden",
        "GX_NO_SETTINGS_SAVE": "1",
        "GX_SKIP_TESTS": "1",
        "GX_DOCK_DISABLE": "1",
        "GX_FRAMES": "80",
        "GX_UI_SCALE": "1.00",
        "GX_GFX_LAYOUT_TRACE": "1",
        "GX_GFX_PRESET_TOUCH": "1",
        "GX_GFX_PRESET_CLOSE": "1",
    })
    try:
        result = subprocess.run(
            [str(BIN), "--gfx", "--gfx-editor",
             "--screenWidth", "1024", "--screenHeight", "768"],
            cwd=str(ROOT), env=environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    except (OSError, subprocess.SubprocessError) as error:
        raise RuntimeError(f"{name}: {error}") from error
    log = audit.strip_ansi(result.stdout or "")
    if result.returncode != 0:
        raise RuntimeError(f"{name}: close guard run exited {result.returncode}: "
                           f"{log[-500:]}")
    require("GFX_PRESET: close blocked dirty" in log,
            f"{name}: dirty preset allowed close")
    require("GFX_PRESET: close guard=dirty" in log,
            f"{name}: dirty close guard trace missing")
    require("GFX_PRESET: close prompt" in log,
            f"{name}: dirty close did not open the prompt")


def check_common(name, row, width, height, minimum_stage):
    require(row["windowW"] == width and row["windowH"] == height,
            f"{name}: unexpected window {row['windowW']}x{row['windowH']}")
    require(row["scene"] > 0 and row["inspector"] > 0,
            f"{name}: side panels did not resolve")
    require(row["stage"] >= minimum_stage,
            f"{name}: stage is only {row['stage']}px")
    for key in ("pick", "buttons", "preset", "labs"):
        require(row[key] == 1, f"{name}: {key} row clips")
    require(row["presetTextW"] <= row["presetFieldW"],
            f"{name}: preset path {row['presetTextW']}px exceeds the "
            f"{row['presetFieldW']}px field")


def main():
    if not BIN.is_file():
        print("GFX_EDITOR_LAYOUT_CHECK: FAIL — missing binary", file=sys.stderr)
        return 1
    before = SETTINGS.read_bytes() if SETTINGS.exists() else None
    settings_ok = True
    failure = None
    try:
        narrow = run("1024-125", 1024, 768, "1.25", "advanced")
        check_common("1024-125", narrow, 1024, 768, 300)
        require(narrow["footer"] == 1,
                f"1024-125: footer mode {narrow['footer']} is not minimal")
        require(narrow["footerH"] <= 80,
                f"1024-125: minimal footer is {narrow['footerH']}px tall")
        require(narrow["footerW"] <= narrow["stage"],
                "1024-125: footer is wider than the stage")

        base = run("1024-100", 1024, 768, "1.00", "advanced")
        check_common("1024-100", base, 1024, 768, 300)
        require(base["footer"] == 1,
                f"1024-100: footer mode {base['footer']} is not minimal")
        require(base["footerH"] <= 60,
                f"1024-100: minimal footer is {base['footerH']}px tall")

        middle = run("1024-150", 1024, 768, "1.50", "advanced")
        check_common("1024-150", middle, 1024, 768, 200)
        require(middle["footer"] == 0,
                "1024-150: footer should yield the stage")

        large = run("1024-175-expert", 1024, 768, "1.75", "expert")
        check_common("1024-175-expert", large, 1024, 768, 150)
        require(large["rows"] >= 3,
                f"1024-175-expert: lab strip only used {large['rows']} rows")
        require(large["footer"] == 0,
                "1024-175-expert: footer should yield the stage")

        check_asset_grid("asset-1024-100", 1024, 768, "1.00")
        check_asset_grid("asset-1024-175", 1024, 768, "1.75")
        check_preset_picker("preset-1024-100", 1024, 768, "1.00")
        check_preset_picker("preset-1024-175", 1024, 768, "1.75")
        check_preset_document("preset-document")
        check_preset_close("preset-close-dirty")

        wide = run("1600-125", 1600, 900, "1.25", "advanced")
        check_common("1600-125", wide, 1600, 900, 600)
        require(wide["footer"] == 2,
                f"1600-125: footer mode {wide['footer']} is not compact")
        require(wide["footerH"] <= 100,
                f"1600-125: footer is {wide['footerH']}px tall")
        require(wide["footerW"] <= wide["stage"],
                "1600-125: footer is wider than the stage")
    except RuntimeError as error:
        failure = str(error)
    finally:
        after = SETTINGS.read_bytes() if SETTINGS.exists() else None
        if after != before:
            settings_ok = False
    if not settings_ok:
        print("GFX_EDITOR_LAYOUT_CHECK: FAIL — resources/settings.json changed",
              file=sys.stderr)
        return 1
    if failure:
        print(f"GFX_EDITOR_LAYOUT_CHECK: FAIL — {failure}", file=sys.stderr)
        return 1
    print("GFX_EDITOR_LAYOUT_CHECK: PASS scene/preset/lab rows fit at "
          "1024/1600 and 100/125/150/175% scale; the 1024 footer is condensed; "
          "asset paths ellipsize, preset picker rows fit, preset dirty state "
          "clears on load and dirty close prompts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
