#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: editor-input gate — ImGui owns the pointer/keys over its windows, while
#   canvas wheel zoom and middle-drag pan only run inside the free viewport.
import re
import sys
import tempfile
from pathlib import Path

import audit_common as audit


def run(environment, frames=200, shot_frame=180, screenshot=None):
    base = {
        "GX_SKIP_TESTS": "1",
        "GX_FIXED_DT": "0.0166666667",
        "GX_AUDIT_WINDOW": "hidden",
        "GX_TOOLS_LAYOUT_TRACE": "1",
        "GX_UI_SCALE": "1.00",
    }
    base.update(environment)
    with audit.engine_default_settings():
        result = audit.run_scene(
            "--title", base, width=1024, height=768, fps=None,
            frames=frames, shot_frame=shot_frame,
            screenshot_dir=screenshot, timeout=180)
    return audit.strip_ansi(result.stdout or "")


def after(log, marker):
    return log.split(marker, 1)[1] if marker in log else ""


def editor(script):
    return run({
        "GX_TOOLS_OPEN": "editor",
        "GX_TOUR": "0",
        "GX_DOCK_DISABLE": "1",
        "GX_AI_INPUT": script,
    })


def main():
    if not audit.BIN.is_file():
        print(f"EDITOR_INPUT_CHECK: FAIL — missing binary: {audit.BIN}",
              file=sys.stderr)
        return 1
    failures = []
    try:
        with tempfile.TemporaryDirectory(prefix="editor_input_",
                                         dir="/tmp/opencode") as root:
            root = Path(root)

            panel = editor("0.3 move 850 400 100; 0.5 wheel 1.0")
            wheel_event = "INPUT: ai wheel=1.00" in panel
            panel_wheel_ok = wheel_event and "EDITOR_SYSTEM: view zoom=" not in panel
            print(f"{'wheel':<12} {'PASS' if panel_wheel_ok else 'FAIL'}  "
                  f"panel event={int(wheel_event)} no-zoom="
                  f"{int('EDITOR_SYSTEM: view zoom=' not in panel)}")
            if not panel_wheel_ok:
                failures.append("wheel: a properties-panel scroll zoomed the canvas")

            canvas = editor("0.3 move 500 400 100; 0.5 wheel 1.0")
            canvas_wheel_ok = ("INPUT: ai wheel=1.00" in canvas
                               and "EDITOR_SYSTEM: view zoom=1.10" in canvas)
            print(f"{'wheel':<12} {'PASS' if canvas_wheel_ok else 'FAIL'}  "
                  f"canvas zoom={int(canvas_wheel_ok)}")
            if not canvas_wheel_ok:
                failures.append("wheel: the canvas wheel did not zoom 1.10x")

            panel = editor("0.3 move 850 400 100; "
                           "0.5 drag 850 400 910 440 300 MMB")
            panel_pan_ok = "EDITOR_SYSTEM: view pan=" not in panel
            print(f"{'pan':<12} {'PASS' if panel_pan_ok else 'FAIL'}  "
                  f"panel no-pan={int(panel_pan_ok)}")
            if not panel_pan_ok:
                failures.append("pan: a properties-panel middle-drag moved the canvas")

            canvas = editor("0.3 move 500 400 100; "
                            "0.5 drag 500 400 560 440 300 MMB")
            pan = re.search(r"EDITOR_SYSTEM: view pan=(-?\d+),(-?\d+)", canvas)
            canvas_pan_ok = bool(pan) and (pan.group(1), pan.group(2)) != ("0", "0")
            print(f"{'pan':<12} {'PASS' if canvas_pan_ok else 'FAIL'}  "
                  f"canvas pan={pan.group(1) + ',' + pan.group(2) if pan else 'missing'}")
            if not canvas_pan_ok:
                failures.append("pan: the canvas middle-drag did not pan")

            #ai: closing the GUI editor must not leave the host bar or an
            #   orphaned ImGui popup capturing native GUI clicks.
            editor_x = run({
                "GX_TOOLS_OPEN": "editor",
                "GX_TOUR": "0",
                "GX_DOCK_DISABLE": "1",
                "GX_GUI_TRACE": "1",
                "GX_AI_INPUT": "1.2 move 290 30 100; 1.5 click 290 30; "
                               "2.2 move 350 415 100; 2.5 click 350 415",
            })
            x_ok = 'GUITRACE: click canvas=0 element=51 detail="OPEN GFX"' in editor_x
            print(f"{'editor-x':<12} {'PASS' if x_ok else 'FAIL'}  "
                  f"title click after X close")
            if not x_ok:
                failures.append("editor-x: title input stayed inert after X close")

            host_close = run({
                "GX_TOOLS_OPEN": "editor",
                "GX_EDITOR_HOST": "1",
                "GX_TOUR": "0",
                "GX_DOCK_DISABLE": "1",
                "GX_GUI_TRACE": "1",
                "GX_AI_INPUT": "1.2 move 224 10 100; 1.5 click 224 10; "
                               "1.8 move 250 31 100; 2.1 click 250 31; "
                               "2.7 move 350 415 100; 3.0 click 350 415",
            })
            host_ok = (
                "EDITOR_HOST: open=0" in host_close
                and 'GUITRACE: click canvas=0 element=51 detail="OPEN GFX"'
                in host_close)
            print(f"{'host-close':<12} {'PASS' if host_ok else 'FAIL'}  "
                  f"title click after host menu close")
            if not host_ok:
                failures.append("host-close: host bar/UI stayed inert after close")

            click = run({
                "GX_GUI_TRACE": "1",
                "GX_AI_INPUT": "0.5 key 299 1; 1.0 move 350 425 100; "
                               "1.5 click 350 425",
            })
            tail = after(click, "BUG_REPORT: open global")
            mouse_ok = ("GUITRACE: input-captured mouse" in tail
                        and "GUITRACE: click" not in tail
                        and "GUITRACE: focus " not in tail)
            print(f"{'report-mouse':<12} {'PASS' if mouse_ok else 'FAIL'}  "
                  f"captured={int('GUITRACE: input-captured mouse' in tail)} "
                  f"no-title-click={int('GUITRACE: click' not in tail)}")
            if not mouse_ok:
                failures.append("report: a click passed through the ImGui form")

            keys = run({
                "GX_GUI_TRACE": "1",
                "GX_AI_INPUT": "0.5 key 299 1; 1.0 move 300 128 100; "
                               "1.5 click 300 128; 2.0 key 264",
            })
            tail = after(keys, "BUG_REPORT: open global")
            keys_ok = ("GUITRACE: input-captured keyboard" in tail
                       and "GUITRACE: focus " not in tail)
            print(f"{'report-keys':<12} {'PASS' if keys_ok else 'FAIL'}  "
                  f"captured={int('GUITRACE: input-captured keyboard' in tail)} "
                  f"no-title-focus={int('GUITRACE: focus ' not in tail)}")
            if not keys_ok:
                failures.append("report: an arrow key moved title focus while typing")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"EDITOR_INPUT_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    if failures:
        for failure in failures:
            print(f"EDITOR_INPUT_CHECK: FAIL — {failure}", file=sys.stderr)
        return 1
    print("EDITOR_INPUT_CHECK: PASS — ImGui/native mouse, wheel, pan and arrow arbitration")
    return 0


if __name__ == "__main__":
    sys.exit(main())
