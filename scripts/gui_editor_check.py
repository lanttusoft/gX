#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GUI editor usability gate — open, guide/tour, save, structural hooks,
#   undo/remove and the shipped-markup harness guard. Display hidden.
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

import audit_common as audit


ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "build" / "gX_build"
SCRATCH = audit.SCRATCH / "gui_editor_check"
TOOLS = "Tools: GUI editor"
HOOK_RE = re.compile(
    r"Tools: deferred hooks applied \(add=(-?\d+) rm=(-?\d+) "
    r"undo=(\d+) redo=(\d+)\) used (\d+) -> (\d+) \(amount (\d+)\)")
TOUR_RE = re.compile(
    r"TOOLS: editor tour step=(\d+) window=(\d+)x(\d+) "
    r"next_right=(\d+) skip_right=(\d+) window_right=(\d+)")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def check_tour(tag, log, scale):
    matches = TOUR_RE.findall(log)
    require(matches, f"{tag}: editor tour geometry trace missing")
    step, width, height, next_right, skip_right, window_right = (
        float(value) for value in matches[-1])
    require(step == 0, f"{tag}: tour did not stay on step 0")
    require(width >= 420.0 * scale * 0.95,
        f"{tag}: tour width {width:.0f} did not scale for {scale:.2f}x")
    require(height >= 180.0 * scale * 0.95,
        f"{tag}: tour height {height:.0f} did not scale for {scale:.2f}x")
    require(next_right > 0 and next_right <= window_right - 4,
        f"{tag}: Next button clipped ({next_right:.0f} > {window_right:.0f})")
    require(skip_right > 0 and skip_right <= window_right - 4,
        f"{tag}: Skip button clipped ({skip_right:.0f} > {window_right:.0f})")


def run(tag, extra, frames=90):
    environment = dict(os.environ)
    environment.update({
        "DISPLAY": audit.display(),
        "GX_AUDIT_WINDOW": "hidden",
        "GX_NO_SETTINGS_SAVE": "1",
        "GX_SKIP_TESTS": "1",
        "GX_FRAMES": str(frames),
        "GX_TOOLS_OPEN": "editor",
        "GX_TOUR": "0",
        "GX_SYNTH_MUTE": "1",
    })
    environment.update({key: str(value) for key, value in extra.items()})
    result = subprocess.run([str(BIN), "--title"], cwd=str(ROOT),
        env=environment, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, timeout=120)
    log = audit.strip_ansi(result.stdout or "")
    require(result.returncode == 0,
        f"{tag}: editor exited {result.returncode}: {log[-500:]}")
    return log


def run_tools(tag):
    environment = dict(os.environ)
    environment.update({
        "DISPLAY": audit.display(),
        "GX_AUDIT_WINDOW": "hidden",
        "GX_NO_SETTINGS_SAVE": "1",
        "GX_SKIP_TESTS": "1",
        "GX_FRAMES": "140",
        "GX_TOUR": "0",
        "GX_SYNTH_MUTE": "1",
    })
    environment.pop("GX_TOOLS_OPEN", None)
    result = subprocess.run(
        [str(BIN), "--tools", "resources/tools/tools_demo.json"],
        cwd=str(ROOT), env=environment, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, timeout=120)
    log = audit.strip_ansi(result.stdout or "")
    require(result.returncode == 0,
        f"{tag}: editor exited {result.returncode}: {log[-500:]}")
    return log


def hook_counts(log, tag):
    match = HOOK_RE.search(log)
    require(match is not None, f"{tag}: deferred-hook trace missing")
    return {
        "add": int(match.group(1)),
        "remove": int(match.group(2)),
        "undo": int(match.group(3)),
        "redo": int(match.group(4)),
        "before": int(match.group(5)),
        "after": int(match.group(6)),
        "amount": int(match.group(7)),
    }


#ai: GUI-H1/M1 — toolbar/menu actions on a disposable shipped markup. Used for
#   undo/save survival and for shortcuts while an ImGui pane has keyboard focus.
def run_tools_actions(target, shots, actions):
    environment = dict(os.environ)
    environment.update({
        "DISPLAY": audit.display(),
        "GX_AUDIT_WINDOW": "hidden",
        "GX_AUDIT_FULLSIZE": "1",
        "GX_NO_SETTINGS_SAVE": "1",
        "GX_SKIP_TESTS": "1",
        "GX_SYNTH_MUTE": "1",
        "GX_AUDIO_MUTE": "1",
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "400",
        "GX_SHOT_FRAME": "395",
        "GX_GUI_TRACE": "1",
        "GX_GFX_LAYOUT_TRACE": "1",
        "GX_TOUR": "0",
        "GX_UI_SCALE": "1.00",
        "GX_DOCK_DISABLE": "1",
        "GX_TOOLS_SELECT": "1",
        "GX_TOOLS_CANVAS_UNLOCK": "1",
        "GX_SCREENSHOT_DIR": str(shots),
        "GX_AI_INPUT": actions,
    })
    result = subprocess.run(
        [str(BIN), "--tools", str(target), "--screenWidth", "1024",
         "--screenHeight", "768", "--ai-screenshot-final"],
        cwd=str(ROOT), env=environment, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, timeout=180)
    log = audit.strip_ansi(result.stdout or "")
    require(result.returncode == 0,
        f"action probe exited {result.returncode}: {log[-500:]}")
    return log


def main():
    if not BIN.is_file():
        print("GUI_EDITOR_CHECK: FAIL — missing binary", file=sys.stderr)
        return 1
    SCRATCH.mkdir(parents=True, exist_ok=True)
    temporary = tempfile.TemporaryDirectory(prefix="run_", dir=SCRATCH)
    try:
        save_path = Path(temporary.name) / "snapshot.json"
        log = run("open-save", {
            "GX_TOOLS_SELECT": "1",
            "GX_TOOLS_FILTER": "button",
            "GX_TOOLS_SOLO": "1",
            "GX_TOOLS_SAVE": str(save_path),
        })
        require(TOOLS in log and "headless snapshot saved" in log,
            "open/save path is missing")
        require(save_path.is_file(), "editor snapshot file missing")
        snapshot = json.loads(save_path.read_text(encoding="utf-8"))
        require(snapshot.get("elements"), "editor snapshot has no elements")

        #ai: the shipped --tools route must open after the markup canvas is
        #   built and expose the named demo elements (not UNKNOWN rows).
        tree = re.search(
            r"GUI_EDITOR_TREE: canvas=(\d+) elements=(\d+) named=(\d+) "
            r"labeled=(\d+)", log)
        require(tree is not None and int(tree.group(4)) > 0,
            "title editor tree has no readable names or text labels")

        tools = run_tools("tools-markup")
        require("GUI editor deferred open applied (canvas 0, 4 elements)" in tools,
            "deferred --tools open did not see the markup canvas")
        require("GUI_EDITOR_TREE: canvas=0 elements=4 named=4 labeled=4" in tools,
            "GUI editor tree did not expose the four named demo elements")
        require("MAIN: Masterindex switch errors" not in tools
            and "MAIN: Renderer failed to respond" not in tools
            and "MAIN: Scene failed to respond" not in tools
            and "MAIN: Camera failed to respond" not in tools,
            "GUI-playground switch still reports expected module failures")

        tour = run("tour", {"GX_TOUR": "force"})
        require("Tools: editor tour opened step=0" in tour,
            "first-run editor tour did not open")
        check_tour("tour", tour, 1.0)
        tour175 = run("tour-175", {"GX_TOUR": "force", "GX_UI_SCALE": "1.75"})
        require("GUI_EDITOR_TITLE: left=Editor - Elements###editor_elements "
                "right=Props###editor_properties" in tour175,
                "175% inspector title did not shorten to Props")
        require("Tools: editor tour opened step=0" in tour175,
            "175% editor tour did not open")
        check_tour("tour-175", tour175, 1.75)

        added = run("add", {"GX_TOOLS_ADD": "0"})
        counts = hook_counts(added, "add")
        require(counts["after"] == counts["before"] + 1,
            f"add hook changed {counts['before']} -> {counts['after']}")

        undo = run("remove-undo", {
            "GX_TOOLS_REMOVE": "2",
            "GX_TOOLS_UNDO": "1",
        })
        require("Tools: removed element 2" in undo,
            "remove hook did not run")
        counts = hook_counts(undo, "remove-undo")
        require(counts["after"] == counts["before"],
            f"undo did not restore remove {counts['before']} -> {counts['after']}")

        #ai: GUI-H1 regression — Undo must not be wiped by the next GUI refresh
        #   before Save writes the restored markup.
        undo_path = Path(temporary.name) / "undo-save.json"
        undo_shots = Path(temporary.name) / "undo-shots"
        undo_shots.mkdir()
        shutil.copyfile(ROOT / "resources" / "tools" / "tools_demo.json",
            undo_path)
        run_tools_actions(undo_path, undo_shots,
            "1.0 move 73 143 200;1.5 click 73 143;"
            "2.5 move 172 10 200;2.9 click 172 10;"
            "3.4 move 190 32 200;3.8 click 190 32;"
            "4.5 move 150 143 200;4.9 click 150 143")
        restored = json.loads(undo_path.read_text(encoding="utf-8"))
        require(len(restored.get("elements", [])) == 4,
            "undo/save wrote an empty or truncated markup canvas")
        require({element.get("name") for element in restored["elements"]}
            == {"bg", "hello", "stripe", "gradient"},
            "undo/save did not preserve the shipped element names")

        #ai: GUI-M1 regression — Ctrl+Z must work while an ImGui pane has
        #   keyboard focus; without the fix the duplicate survives Save.
        shortcut_path = Path(temporary.name) / "shortcut-undo-save.json"
        shortcut_shots = Path(temporary.name) / "shortcut-shots"
        shortcut_shots.mkdir()
        shutil.copyfile(ROOT / "resources" / "tools" / "tools_demo.json",
            shortcut_path)
        run_tools_actions(shortcut_path, shortcut_shots,
            "1.0 move 73 143 200;1.5 click 73 143;"
            "2.5 key 90 0 1;3.5 move 150 143 200;4.0 click 150 143")
        shortcut_saved = json.loads(shortcut_path.read_text(encoding="utf-8"))
        require(len(shortcut_saved.get("elements", [])) == 4,
            "Ctrl+Z shortcut did not undo the duplicate before Save")

        #ai: GUI-M1 regression — arrow nudge must move the selected element and
        #   Save must keep all four roots (no refresh wipe).
        nudge_path = Path(temporary.name) / "nudge-save.json"
        nudge_shots = Path(temporary.name) / "nudge-shots"
        nudge_shots.mkdir()
        shutil.copyfile(ROOT / "resources" / "tools" / "tools_demo.json",
            nudge_path)
        shipped = json.loads(nudge_path.read_text(encoding="utf-8"))
        shipped_hello = next(element for element in shipped["elements"]
            if element.get("name") == "hello")
        run_tools_actions(nudge_path, nudge_shots,
            "1.0 move 500 450 200;2.0 key 262;"
            "3.0 move 150 143 200;3.4 click 150 143")
        nudged = json.loads(nudge_path.read_text(encoding="utf-8"))
        require(len(nudged.get("elements", [])) == 4,
            "arrow nudge/save wrote an empty or truncated canvas")
        nudged_hello = next(element for element in nudged["elements"]
            if element.get("name") == "hello")
        require(nudged_hello.get("gx") != shipped_hello.get("gx"),
            "arrow nudge did not move the selected element before Save")

        probe = ROOT / "resources/tools/gui_editor_guard_probe.json"
        probe.unlink(missing_ok=True)
        try:
            guard = run("guard", {
                "GX_TOOLS_SAVE": "resources/tools/gui_editor_guard_probe.json",
            })
            require("refusing to overwrite shipped GUI markup" in guard,
                "shipped-markup guard message missing")
            require(not probe.exists(),
                "shipped-markup guard wrote the probe file")
        finally:
            probe.unlink(missing_ok=True)

        print("GUI_EDITOR_CHECK: PASS open/save, tour, add, remove+undo, "
              "and shipped-markup harness guard")
        return 0
    except (OSError, subprocess.SubprocessError, RuntimeError, ValueError) as error:
        print(f"GUI_EDITOR_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    finally:
        temporary.cleanup()


if __name__ == "__main__":
    sys.exit(main())
