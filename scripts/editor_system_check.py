#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: v0.44.1 editor-system gate — lifecycle, toolbars and lab cleanup across
#   every editor, not just per-editor opening frames.
import argparse
import re
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


HOSTABLE = (
    ("theme", "Theme"),
    ("sky", "Sky"),
    ("background", "Background"),
    ("textwriter", "Textwriter"),
)
LABS = ("themlab", "materials", "shaderlab")
ALL_EDITORS = ("editor", "gfx", "physics", "wavetable", "rail",
               "script") + tuple(item[0] for item in HOSTABLE) + LABS
CROP_TOP = 60
CLOSE_MAX = 2.0
OPEN_MIN = 10.0


def run(editor, frames, *, close_at=None, reopen_at=None, screenshot=None,
        shot_frame=25, extra=None):
    environment = {
        "GX_SKIP_TESTS": "1",
        "GX_FIXED_DT": "0.0166666667",
        "GX_AUDIT_WINDOW": "hidden",
    }
    if editor:
        environment["GX_TOOLS_OPEN"] = editor
    if close_at is not None:
        environment["GX_TOOLS_CLOSE_AT"] = str(close_at)
    if reopen_at is not None:
        environment["GX_TOOLS_REOPEN_AT"] = str(reopen_at)
    if extra:
        environment.update({key: str(value) for key, value in extra.items()})
    with audit.engine_default_settings():
        result = audit.run_scene(
            "--title", environment, width=None, height=None, fps=None,
            frames=frames, shot_frame=shot_frame,
            screenshot_dir=screenshot, timeout=180)
    return audit.strip_ansi(result.stdout or "")


def image(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float64)


def compare(a, b):
    if a.shape != b.shape:
        b = np.asarray(Image.fromarray(b.astype(np.uint8)).resize(
            (a.shape[1], a.shape[0]))).astype(np.float64)
    return float(np.abs(a[CROP_TOP:] - b[CROP_TOP:]).mean())


def parity_delta(a, b):
    if a.shape != b.shape:
        b = np.asarray(Image.fromarray(b.astype(np.uint8)).resize(
            (a.shape[1], a.shape[0]))).astype(np.float64)
    height, width = a.shape[:2]
    # ai: stay inside both side panels at 1024x768 (300px left, 340px right).
    left, right = int(width * 0.32), int(width * 0.64)
    return float(np.abs(a[CROP_TOP:, left:right]
                        - b[CROP_TOP:, left:right]).mean())


def main():
    parser = argparse.ArgumentParser(
        description="Check editor lifecycle, toolbars and lab cleanup.")
    parser.add_argument("--binary", help="override the engine binary")
    args = parser.parse_args()
    if not audit.BIN.is_file():
        print(f"EDITOR_SYSTEM_CHECK: FAIL — missing binary: {audit.BIN}",
              file=sys.stderr)
        return 1
    failures = []
    try:
        with tempfile.TemporaryDirectory(prefix="editor_system_",
                                         dir="/tmp/opencode") as root:
            root = Path(root)
            baseline_dir = root / "baseline"
            baseline_dir.mkdir()
            run(None, 30, screenshot=baseline_dir)
            baseline_shot = audit.newest_shot(baseline_dir)
            if baseline_shot is None:
                raise RuntimeError("baseline title screenshot missing")
            baseline = image(baseline_shot)

            #ai: GX-I040 — Shift+F10 opens the internal report from the title,
            #   not just inside the GFX editor.
            report_log = run(None, 80, extra={
                "GX_AI_INPUT": "0.5 key 299 1"})
            report_ok = "BUG_REPORT: open global" in report_log
            print(f"{'report':<12} {'PASS' if report_ok else 'FAIL'}  "
                  f"Shift+F10 global open")
            if not report_ok:
                failures.append("report: Shift+F10 did not open")
            plain_log = run(None, 50, extra={"GX_AI_INPUT": "0.5 key 299 0"})
            plain_ok = "BUG_REPORT: open global" not in plain_log
            print(f"{'report':<12} {'PASS' if plain_ok else 'FAIL'}  "
                  f"plain F10 stays console")
            if not plain_ok:
                failures.append("report: plain F10 opened the report")

            locked_log = run("editor", 30)
            locked_ok = "EDITOR_SYSTEM: canvas locked=1 zoom=1.00" in locked_log
            unlocked_log = run("editor", 30, extra={
                "GX_TOOLS_CANVAS_UNLOCK": "1"})
            unlocked_ok = "EDITOR_SYSTEM: canvas locked=0 zoom=1.00" in unlocked_log
            zoom_log = run("editor", 30, extra={"GX_TOOLS_VIEW_ZOOM": "1.5"})
            zoom_ok = "EDITOR_SYSTEM: canvas locked=1 zoom=1.50" in zoom_log
            print(f"{'canvas':<12} {'PASS' if locked_ok and unlocked_ok and zoom_ok else 'FAIL'}  "
                  f"lock={int(locked_ok)} unlock={int(unlocked_ok)} zoom={int(zoom_ok)}")
            if not (locked_ok and unlocked_ok and zoom_ok):
                failures.append("canvas: lock/unlock/zoom state missing")
            view_dir = root / "editor-view-reset"
            view_dir.mkdir()
            run("editor", 35, close_at=15, screenshot=view_dir,
                shot_frame=30, extra={"GX_TOOLS_VIEW_ZOOM": "1.5"})
            view_shot = audit.newest_shot(view_dir)
            view_delta = (compare(baseline, image(view_shot))
                          if view_shot else 99.0)
            view_ok = view_shot is not None and view_delta < CLOSE_MAX
            print(f"{'canvas':<12} {'PASS' if view_ok else 'FAIL'}  "
                  f"view reset delta={view_delta:.2f}")
            if not view_ok:
                failures.append(f"canvas: view did not reset ({view_delta:.2f})")
            parity_base_dir = root / "parity-baseline"
            parity_base_dir.mkdir()
            run(None, 220, screenshot=parity_base_dir, shot_frame=200)
            parity_base = audit.newest_shot(parity_base_dir)
            parity_dir = root / "editor-parity"
            parity_dir.mkdir()
            run("editor", 220, screenshot=parity_dir, shot_frame=200, extra={
                "GX_TOOLS_GRID": "0", "GX_DOCK_DISABLE": "1", "GX_TOUR": "0"})
            parity_shot = audit.newest_shot(parity_dir)
            parity = (parity_delta(image(parity_base), image(parity_shot))
                      if parity_base and parity_shot else 99.0)
            parity_ok = parity_shot is not None and parity < 8.0
            print(f"{'canvas':<12} {'PASS' if parity_ok else 'FAIL'}  "
                  f"runtime parity delta={parity:.2f}")
            if not parity_ok:
                failures.append(f"canvas: preview parity failed ({parity:.2f})")

            #ai: overlap/visibility — the view controls must remain inside the
            #   left dock and the status text must stay inside its strip at
            #   100% and 175%.
            for scale, label in (("1.00", "100%"), ("1.75", "175%")):
                log = run("editor", 45, extra={
                    "GX_UI_SCALE": scale, "GX_DOCK_DISABLE": "1",
                    "GX_TOUR": "0", "GX_TOOLS_LAYOUT_TRACE": "1"})
                bar = re.search(
                    r"EDITOR_SYSTEM: toolbar unlock=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) "
                    r"fit=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) "
                    r"panel=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)", log)
                status = re.search(
                    r"EDITOR_SYSTEM: status text=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) "
                    r"window=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)", log)
                layout_ok = bool(bar) and bool(status)
                if layout_ok:
                    v = [float(value) for value in bar.groups()]
                    layout_ok = (
                        v[2] <= v[10] and v[6] <= v[10]
                        and v[3] >= v[9] and v[7] >= v[9]
                        and v[7] <= v[11])
                    t = [float(value) for value in status.groups()]
                    layout_ok = layout_ok and t[3] <= t[7] and t[0] >= t[4]
                print(f"{'toolbar':<12} {'PASS' if layout_ok else 'FAIL'}  "
                      f"{label} unlock/fit inside, status fits")
                if not layout_ok:
                    failures.append(f"toolbar: overlap/status failed at {label}")

            #ai: the floating theme editor must start below the host workspace
            #   tab strip, including at 175% text scale.
            for scale, label in (("1.00", "100%"), ("1.75", "175%")):
                log = run("theme", 35, extra={
                    "GX_UI_SCALE": scale, "GX_DOCK_DISABLE": "1",
                    "GX_TOOLS_LAYOUT_TRACE": "1"})
                panel = re.search(
                    r"THEME_EDITOR: panel=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) "
                    r"hostbottom=([-\d.]+)", log)
                overlap_ok = bool(panel) and float(panel.group(2)) >= float(panel.group(5))
                print(f"{'overlap':<12} {'PASS' if overlap_ok else 'FAIL'}  "
                      f"theme panel below host bar {label}")
                if not overlap_ok:
                    failures.append(f"overlap: theme panel covered the host bar at {label}")

            #ai: the report form keeps its action row on screen at 175%, and
            #   Reset clears the draft.
            for scale, label in (("1.00", "100%"), ("1.75", "175%")):
                log = run(None, 60, extra={
                    "GX_UI_SCALE": scale, "GX_AI_INPUT": "0.5 key 299 1",
                    "GX_TOOLS_LAYOUT_TRACE": "1"})
                form = re.search(
                    r"BUG_REPORT: form save=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) "
                    r"reset=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) "
                    r"window=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)", log)
                form_ok = bool(form)
                if form_ok:
                    v = [float(value) for value in form.groups()]
                    form_ok = (v[2] <= v[10] and v[3] <= v[11]
                               and v[6] <= v[10] and v[7] <= v[11]
                               and v[8] >= 0 and v[9] >= 0
                               and v[10] <= 1024 and v[11] <= 768)
                print(f"{'report':<12} {'PASS' if form_ok else 'FAIL'}  "
                      f"form actions inside {label}")
                if not form_ok:
                    failures.append(f"report: form actions clipped at {label}")
            reset_log = run(None, 120, extra={
                "GX_UI_SCALE": "1.75",
                "GX_AI_INPUT": "0.5 key 299 1; 1.0 move 340 620 100; 1.5 click 340 620"})
            reset_ok = "BUG_REPORT: reset" in reset_log
            print(f"{'report':<12} {'PASS' if reset_ok else 'FAIL'}  reset control")
            if not reset_ok:
                failures.append("report: Reset control did not fire")

            #ai: state-machine transitions — report close/reopen, host
            #   open/close and canvas lock toggle.
            state_log = run(None, 220, extra={
                "GX_UI_SCALE": "1.75",
                "GX_AI_INPUT": "0.5 key 299 1; 1.0 move 992 72 100; "
                               "1.5 click 992 72; 2.0 key 299 1"})
            report_state = (state_log.count("BUG_REPORT: open global") >= 2
                            and "BUG_REPORT: close" in state_log)
            print(f"{'state':<12} {'PASS' if report_state else 'FAIL'}  "
                  f"report open-close-reopen")
            if not report_state:
                failures.append("state: report open-close-reopen failed")
            host_log = run(None, 100, extra={
                "GX_AI_INPUT": "0.5 key 298 1; 1.5 key 298 1"})
            host_state = ("EDITOR_HOST: open=1" in host_log
                          and "EDITOR_HOST: open=0" in host_log)
            print(f"{'state':<12} {'PASS' if host_state else 'FAIL'}  "
                  f"host open-close")
            if not host_state:
                failures.append("state: host open-close failed")
            lock_log = run("editor", 140, extra={
                "GX_UI_SCALE": "1.00", "GX_DOCK_DISABLE": "1",
                "GX_TOUR": "0", "GX_TOOLS_LAYOUT_TRACE": "1",
                "GX_AI_INPUT": "0.5 move 200 143 100; 0.8 click 200 143; "
                               "1.2 click 200 143"})
            lock_state = ("EDITOR_SYSTEM: canvas unlocked=1" in lock_log
                          and "EDITOR_SYSTEM: canvas unlocked=0" in lock_log)
            print(f"{'state':<12} {'PASS' if lock_state else 'FAIL'}  "
                  f"canvas unlock-lock")
            if not lock_state:
                failures.append("state: canvas unlock-lock failed")

            #ai: the Theme Lab Back route lives on the panel and must close the
            #   paired preview at both text scales.
            for scale, label, x, y in (
                    ("1.00", "100%", 100, 112), ("1.75", "175%", 120, 192)):
                directory = root / f"back-{label}"
                directory.mkdir()
                log = run("themlab", 80, screenshot=directory, shot_frame=70,
                          extra={"GX_UI_SCALE": scale, "GX_DOCK_DISABLE": "1",
                                 "GX_TOUR": "0",
                                 "GX_AI_INPUT": f"0.3 move {x} {y} 100; "
                                                f"0.8 click {x} {y}"})
                shot = audit.newest_shot(directory)
                delta = compare(baseline, image(shot)) if shot else 99.0
                back_ok = "THEME_EDITOR: back" in log and delta < CLOSE_MAX
                print(f"{'themlab':<12} {'PASS' if back_ok else 'FAIL'}  "
                      f"Back {label} delta={delta:.2f}")
                if not back_ok:
                    failures.append(f"themlab: Back failed at {label} ({delta:.2f})")

            for editor, expected in HOSTABLE:
                log = run(editor, 25)
                if "needs the gx_shell module" in log:
                    print(f"{editor:<12} SKIP  module unavailable")
                    continue
                opened = "EDITOR_HOST: open=1" in log
                active = f"EDITOR_HOST: active={expected}" in log
                ok = opened and active
                print(f"{editor:<12} {'PASS' if ok else 'FAIL'}  "
                      f"toolbar={int(opened)} active={int(active)}")
                if not ok:
                    failures.append(f"{editor}: toolbar/active missing")

            for lab in LABS:
                directory = root / f"open-{lab}"
                directory.mkdir()
                log = run(lab, 30, screenshot=directory)
                shot = audit.newest_shot(directory)
                visible = shot is not None and compare(baseline, image(shot)) > OPEN_MIN
                print(f"{lab:<12} {'PASS' if visible else 'FAIL'}  open preview")
                if not visible:
                    failures.append(f"{lab}: open preview not visible")

            for editor in ALL_EDITORS:
                log = run(editor, 55, close_at=20, reopen_at=35)
                closed = "EDITOR_SYSTEM: closed frame=20" in log
                reopened = "EDITOR_SYSTEM: reopened frame=35" in log
                ok = closed and reopened
                print(f"{editor:<12} {'PASS' if ok else 'FAIL'}  "
                      f"close/reopen={int(closed)}/{int(reopened)}")
                if not ok:
                    failures.append(f"{editor}: close/reopen marker missing")

            for lab in LABS + ("theme",):
                directory = root / f"closed-{lab}"
                directory.mkdir()
                run(lab, 35, close_at=15, screenshot=directory,
                    shot_frame=30)
                shot = audit.newest_shot(directory)
                delta = compare(baseline, image(shot)) if shot else 99.0
                ok = shot is not None and delta < CLOSE_MAX
                print(f"{lab:<12} {'PASS' if ok else 'FAIL'}  "
                      f"title return delta={delta:.2f}")
                if not ok:
                    failures.append(f"{lab}: did not return to title ({delta:.2f})")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"EDITOR_SYSTEM_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    if failures:
        for failure in failures:
            print(f"EDITOR_SYSTEM_CHECK: FAIL — {failure}", file=sys.stderr)
        return 1
    print("EDITOR_SYSTEM_CHECK: PASS — toolbars, lifecycle, labs, overlap and state transitions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
