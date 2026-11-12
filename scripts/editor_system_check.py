#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: v0.44.1 editor-system gate — lifecycle, toolbars and lab cleanup across
#   every editor, not just per-editor opening frames.
import argparse
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
    print("EDITOR_SYSTEM_CHECK: PASS — toolbars, lifecycle, labs and title return")
    return 0


if __name__ == "__main__":
    sys.exit(main())
