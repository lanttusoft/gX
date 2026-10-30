#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: docked-editor layout pin. The GFX editor docks the scene beside its
#   scene panel and inspector: the GUI dock reserves the inspector width (right
#   inset), so the scene must render between the panels with letterbox bands
#   above/below — never behind the inspector. This gate checks the resolved
#   geometry (GX_DOCK_TRACE) plus the captured bands, so a regression in the
#   inset math or panel sizing fails here instead of only in the calibrated
#   picker gates.
#
#   Usage: DISPLAY=:1 python3 scripts/gfx_dock_layout_check.py
#   GPU/display required — use `make dock-layout-check`.

import re
import sys
import tempfile
from pathlib import Path

import numpy as np

import audit_common as audit


ROOT = audit.ROOT

#opencode: absolute geometry in this gate's assertions — keep the engine size.
audit.keep_engine_size()
PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"
DOCK_RE = re.compile(
    r"GFX_DOCK: docked=(\d) window=(\d+)x(\d+) "
    r"viewport=\(([-0-9.]+),([-0-9.]+),([-0-9.]+),([-0-9.]+)\) "
    r"inset=([-0-9.]+) inspector=([-0-9.]+) scene=([-0-9.]+)"
)
SCENE_WIDTH = 1536
SCENE_HEIGHT = 1016
LEFT_MARGIN = 320.0
EDGE_TOLERANCE = 8.0


def main():
    if not audit.BIN.is_file() or not PRESET.is_file():
        print("GFX_DOCK_LAYOUT_CHECK: missing binary or preset", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_dock_layout_",
                                         dir="/tmp/opencode") as temporary:
            temp = Path(temporary)
            shot_dir = temp / "shot"
            with audit.settings_preserved():
                environment = audit.scene_env({
                    "GX_FIXED_DT": "0.0166666667",
                    "GX_DOCK_TRACE": "1",
                    "GX_GFX_PRESET_LOAD": str(PRESET),
                }, clean=True)
                result = audit.run_scene("--gfx", environment, width=None,
                                         height=None, fps=None, shot_frame=60,
                                         screenshot_dir=shot_dir, timeout=180,
                                         check=True,
                                         extra_args=("--gfx-editor",))
            log = audit.strip_ansi(result.stdout or "")
            rows = DOCK_RE.findall(log)
            if not rows:
                raise RuntimeError("dock trace missing")
            (docked, window_w, window_h, view_x, view_y, view_w, view_h,
             inset, inspector_w, scene_w) = (float(value) for value in rows[-1])
            window_w = int(window_w)
            window_h = int(window_h)
            if docked != 1:
                raise RuntimeError("editor did not engage the docked viewport")
            if abs(view_x - LEFT_MARGIN) > 1.0:
                raise RuntimeError(
                    f"scene does not start at the left margin: {view_x:.0f}")
            inspector_x = window_w - inspector_w
            if abs((view_x + view_w) - inspector_x) > 2.0:
                raise RuntimeError(
                    f"scene right edge {view_x + view_w:.0f} != inspector left "
                    f"edge {inspector_x:.0f}")
            if view_x + view_w + inset > window_w + 1.0:
                raise RuntimeError(
                    f"scene extends under the inspector: "
                    f"{view_x + view_w:.0f} + {inset:.0f} > {window_w}")
            if abs(inset - inspector_w) > 0.5:
                raise RuntimeError(
                    f"dock inset {inset:.0f} != inspector width "
                    f"{inspector_w:.0f}")
            if view_w <= 0.0 or view_h <= 0.0:
                raise RuntimeError(f"degenerate scene box: {view_w}x{view_h}")
            aspect = view_w / view_h
            expected_aspect = SCENE_WIDTH / SCENE_HEIGHT
            if abs(aspect - expected_aspect) > 0.01 * expected_aspect:
                raise RuntimeError(
                    f"scene aspect {aspect:.3f} drifted from {expected_aspect:.3f}")
            expected_y = (window_h - view_h) * 0.5
            if abs(view_y - expected_y) > 1.0:
                raise RuntimeError(
                    f"scene is not vertically centred: {view_y:.0f} vs "
                    f"{expected_y:.0f}")
            if abs(scene_w - LEFT_MARGIN) > 1.0:
                raise RuntimeError(
                    f"scene panel width {scene_w:.0f} != left margin "
                    f"{LEFT_MARGIN:.0f}")

            shot = audit.newest_shot(shot_dir)
            if shot is None:
                raise RuntimeError("layout screenshot missing")
            image = audit.load_rgb(shot)
            height, width = image.shape[0], image.shape[1]
            if width != window_w or height != window_h:
                raise RuntimeError(
                    f"capture {width}x{height} != window "
                    f"{window_w}x{window_h}")
            #opencode: the letterbox bands are flat background; scene rows carry
            #   content, so per-row variance inside the scene's x-range marks the
            #   scene's vertical extent.
            x0 = int(view_x + 80.0)
            x1 = int(min(view_x + view_w - 80.0, width))
            if x1 <= x0:
                raise RuntimeError("scene too narrow for band detection")
            row_variance = image[:, x0:x1, :].std(axis=(1, 2))
            row_mean = image[:, x0:x1, :].mean(axis=(1, 2))
            #opencode: the letterbox bands are flat background; the scene edge is
            #   where a row's mean colour leaves the band reference. Scan only a
            #   narrow window around the traced box so ImGui chrome cannot be
            #   mistaken for scene content.
            band_row = int(view_y) - 40
            if band_row < 40:
                band_row = 0
            band_reference = float(row_mean[band_row])
            if row_variance[band_row] > 3.0:
                raise RuntimeError(
                    f"letterbox band is not flat above the scene (row {band_row})")

            def first_content(y_from, y_to):
                for y in range(max(0, y_from), min(height, y_to)):
                    if abs(float(row_mean[y]) - band_reference) > 4.0:
                        return float(y)
                return -1.0

            top = first_content(int(view_y) - 60, int(view_y) + 60)
            if top < 0.0 or abs(top - view_y) > EDGE_TOLERANCE:
                raise RuntimeError(
                    f"scene top edge {top:.0f} != trace {view_y:.0f}")
            bottom_band = int(view_y + view_h) + 40
            if bottom_band >= height:
                bottom_band = height - 1
            if row_variance[bottom_band] > 3.0:
                raise RuntimeError(
                    f"letterbox band is not flat below the scene "
                    f"(row {bottom_band})")
            bottom_reference = float(row_mean[bottom_band])
            bottom = -1.0
            for y in range(min(height, int(view_y + view_h) + 60) - 1,
                           max(0, int(view_y + view_h) - 60) - 1, -1):
                if abs(float(row_mean[y]) - bottom_reference) > 4.0:
                    bottom = float(y + 1)
                    break
            if bottom < 0.0 or abs(bottom - (view_y + view_h)) > EDGE_TOLERANCE:
                raise RuntimeError(
                    f"scene bottom edge {bottom:.0f} != trace "
                    f"{view_y + view_h:.0f}")
            if bottom - top > window_h - 2.0 * view_y + 2.0 * EDGE_TOLERANCE:
                raise RuntimeError(
                    f"scene content spans {top:.0f}..{bottom:.0f} — taller than "
                    f"the docked box")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_DOCK_LAYOUT_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print("GFX_DOCK_LAYOUT_CHECK: PASS scene fits between the panels "
          f"({int(view_w)}x{int(view_h)} at {int(view_x)},{int(view_y)}; "
          f"inset {int(inset)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
