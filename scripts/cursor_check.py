#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: cursor state machine gate. The cursor is either UI (visible/free)
#   or CAPTURED (hidden/locked); owners push with a priority and the scene
#   reset restores the scene default. The gate asserts:
#   - title = UI/scene
#   - GFX editor = UI/tool (the editor owns the pointer)
#   - host over a captured scene = UI/editor-host, then CAPTURED/scene on close
#   - physics free camera = CAPTURED/physics-freecam, UI/tool after Esc
#   - host over free camera = UI/editor-host wins, CAPTURED again on close
#   - automated runs never lock the OS pointer (applied hidden=0)
#   Traces are enabled with GX_CURSOR_TRACE=1.

import re
import sys

import audit_common as audit


STATE_RE = re.compile(r"CURSOR: state=(UI|CAPTURED) owner=(\S+) depth=(\d+)")
APPLY_RE = re.compile(r"CURSOR: applied state=(UI|CAPTURED) hidden=(\d)")


def run(tag, scene, extra, frames=200):
    overrides = {
        "GX_CURSOR_TRACE": "1",
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(frames),
        "GX_NO_SETTINGS_SAVE": "1",
    }
    overrides.update(extra)
    environment = audit.scene_env(overrides, clean=True)
    result = audit.run_scene(scene, environment, width=1024, height=768,
        fps=None, frames=frames, timeout=180, check=False)
    return result.returncode, audit.strip_ansi(result.stdout or "")


def states(log):
    return [(state, owner, int(depth))
            for state, owner, depth in STATE_RE.findall(log)]


def last_state(log):
    rows = states(log)
    if not rows:
        raise RuntimeError("no cursor state trace")
    return rows[-1][:2]


def has_state(log, state, owner):
    return any(s == state and o == owner for s, o, _ in states(log))


def assert_automation_safe(log):
    for state, hidden in APPLY_RE.findall(log):
        if hidden != "0":
            raise RuntimeError(
                f"automated run locked the OS cursor ({state} hidden={hidden})")


def main():
    if not audit.BIN.is_file():
        print("CURSOR_CHECK: FAIL — missing binary", file=sys.stderr)
        return 1
    try:
        code, log = run("title", "--title", {}, frames=40)
        if code != 0:
            raise RuntimeError(f"title run failed ({code})")
        if last_state(log) != ("UI", "scene"):
            raise RuntimeError(f"title cursor state {last_state(log)}")
        assert_automation_safe(log)

        code, log = run("gfx-editor", "--gfx-editor", {}, frames=90)
        if code != 0:
            raise RuntimeError(f"gfx editor run failed ({code})")
        if last_state(log) != ("UI", "tool"):
            raise RuntimeError(f"gfx editor cursor state {last_state(log)}")
        assert_automation_safe(log)

        code, log = run("host", "--gfx", {
            "GX_EDITOR_HOST": "1",
            "GX_EDITOR_WORKSPACE": "Default",
            "GX_AI_INPUT": "2.0 key 298",
        }, frames=220)
        if code != 0:
            raise RuntimeError(f"host run failed ({code})")
        if not has_state(log, "UI", "editor-host"):
            raise RuntimeError("host never claimed the cursor")
        if last_state(log) != ("CAPTURED", "scene"):
            raise RuntimeError(f"host close cursor state {last_state(log)}")
        assert_automation_safe(log)

        code, log = run("physics-freecam", "--title", {
            "GX_TOOLS_OPEN": "physics",
            "GX_PHYSICS_FREECAM": "1",
            "GX_AI_INPUT": "2.0 key 256",
        }, frames=260)
        if code != 0:
            raise RuntimeError(f"physics freecam run failed ({code})")
        if not has_state(log, "CAPTURED", "physics-freecam"):
            raise RuntimeError("free camera never captured the cursor")
        if last_state(log) != ("UI", "tool"):
            raise RuntimeError(f"freecam exit cursor state {last_state(log)}")
        assert_automation_safe(log)

        code, log = run("host-over-freecam", "--title", {
            "GX_EDITOR_HOST": "1",
            "GX_TOOLS_OPEN": "physics",
            "GX_PHYSICS_FREECAM": "1",
            "GX_AI_INPUT": "2.0 key 298",
        }, frames=240)
        if code != 0:
            raise RuntimeError(f"host/freecam run failed ({code})")
        if not has_state(log, "UI", "editor-host"):
            raise RuntimeError("host did not outrank the free camera")
        if not has_state(log, "CAPTURED", "physics-freecam"):
            raise RuntimeError("free camera did not reclaim the cursor")
        if last_state(log) != ("CAPTURED", "physics-freecam"):
            raise RuntimeError(
                f"host over freecam exit state {last_state(log)}")
        assert_automation_safe(log)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"CURSOR_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print("CURSOR_CHECK: PASS title/editor/host/freecam states, priorities "
          "and automated pointer safety")
    return 0


if __name__ == "__main__":
    sys.exit(main())
