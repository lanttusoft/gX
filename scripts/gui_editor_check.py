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


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


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
