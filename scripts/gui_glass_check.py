#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: GUI Aero-glass gate — the glass surface must sample and blur the
#   accumulated backdrop, tint toward its color, add a silhouette rim, keep a
#   deterministic frame, and fall back pixel-identically to the plain
#   translucent fill when the tokens are zeroed or GX_GUI_GLASS=0. GPU
#   display required (DISPLAY, default :1).

import argparse
import json
import os
import sys
import tempfile
from pathlib import Path

import numpy as np

import audit_common as audit


DEFAULT_OUT = "/tmp/opencode/gui_glass_check"
THEME_PATH = audit.ROOT / "resources" / "style" / "theme.json"
TEMPLATE = "GUITESTER"
# GuiTester glass panel: grid (6.7, 6.9, 1.2, 0.6) in the 1024x768 window.
GLASS_ROI = (668, 715, 863, 1006)   # y0, y1, x0, x1 (inside the panel)
GLASS_ON = {"blur": 7.0, "tint": 0.30, "rim": 0.30, "refraction": 2.5, "gloss": 0.22}
GLASS_OFF = {"blur": 0.0, "tint": 0.0, "rim": 0.0, "refraction": 0.0, "gloss": 0.0}
TIMEOUT = 120


def themed(directory, settings, name):
    theme = json.loads(THEME_PATH.read_text(encoding="utf-8"))
    theme["glass"] = dict(settings)
    path = directory / f"glass-{name}.json"
    path.write_text(json.dumps(theme), encoding="utf-8")
    return path


def capture(theme_path, directory, extra=None):
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_SKIP_TESTS": "1",
        "GX_FIXED_DT": "0.0166666667",
        "GX_GUI_TEMPLATE_FORCE": TEMPLATE,
        "GX_GUI_THEME": str(theme_path),
    }, clean=True)
    for key, value in (extra or {}).items():
        environment[key] = str(value)
    result = audit.run_scene("--title", environment, width=None, height=None,
                             fps=None, frames=90, shot_frame=80,
                             screenshot_dir=directory, timeout=TIMEOUT,
                             check=False)
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError(f"capture missing (exit {result.returncode})")
    return audit.load_rgb(shot)


class Rows:
    def __init__(self, only):
        self.only = only
        self.results = []
        self.tempdir = None
        self.images = {}

    def run(self, name, function):
        if self.only and name not in self.only:
            return
        try:
            ok, note = function(self)
        except (OSError, RuntimeError, ValueError) as error:
            ok, note = False, str(error)
        self.results.append((name, ok, note))
        print(f"{name:<18} {'PASS' if ok else 'FAIL'}  {note}", flush=True)

    def image(self, name):
        if name not in self.images:
            on = themed(self.tempdir, GLASS_ON, "on")
            off = themed(self.tempdir, GLASS_OFF, "off")
            self.images["on"] = capture(on, self.tempdir / "on")
            self.images["off"] = capture(off, self.tempdir / "off")
        return self.images[name]


def row_glass_applied(rows):
    on = rows.image("on")
    off = rows.image("off")
    y0, y1, x0, x1 = GLASS_ROI
    delta = audit.mean_abs_diff(on[y0:y1, x0:x1], off[y0:y1, x0:x1])
    return delta > 8.0, f"glass surface differs from the flat fallback (delta {delta:.1f})"


def row_glass_blur(rows):
    on = rows.image("on")
    off = rows.image("off")
    y0, y1, x0, x1 = GLASS_ROI
    sharp = float(off[y0:y1, x0:x1].std())
    blurred = float(on[y0:y1, x0:x1].std())
    return (sharp - blurred) > 8.0, (f"backdrop variance drops under glass "
                                     f"({sharp:.1f} -> {blurred:.1f})")


def row_glass_fallback(rows):
    on = rows.image("on")
    off = rows.image("off")
    forced = capture(themed(rows.tempdir, GLASS_ON, "on"),
                     rows.tempdir / "forced-off", extra={"GX_GUI_GLASS": "0"})
    y0, y1, x0, x1 = GLASS_ROI
    forced_delta = audit.mean_abs_diff(off[y0:y1, x0:x1], forced[y0:y1, x0:x1])
    on_delta = audit.mean_abs_diff(on[y0:y1, x0:x1], forced[y0:y1, x0:x1])
    ok = forced_delta <= 0.01 and on_delta > 1.0
    return ok, (f"GX_GUI_GLASS=0 matches the flat fill "
                f"(delta {forced_delta:.4f}, glass delta {on_delta:.1f})")


def row_glass_rim(rows):
    on = rows.image("on")
    y0, y1, x0, x1 = GLASS_ROI
    panel = on[y0:y1, x0:x1].mean(axis=2)
    ring = np.concatenate([panel[:4, :].ravel(), panel[-4:, :].ravel(),
                           panel[:, :4].ravel(), panel[:, -4:].ravel()])
    inner = panel[10:-10, 10:-10]
    lift = float(ring.mean() - inner.mean())
    return lift > 2.0, f"inner rim lifts the silhouette (+{lift:.1f} luma)"


def row_determinism(rows):
    first = rows.image("on")
    second = capture(themed(rows.tempdir, GLASS_ON, "on"), rows.tempdir / "det")
    delta = audit.mean_abs_diff(first, second)
    return delta == 0.0, f"repeat run identical (mean {delta:.4f})"


ROWS = [
    ("glass-applied", row_glass_applied),
    ("glass-blur", row_glass_blur),
    ("glass-rim", row_glass_rim),
    ("glass-fallback", row_glass_fallback),
    ("determinism", row_determinism),
]


def main():
    parser = argparse.ArgumentParser(
        description="Validate the Aero-glass surface (blur, tint, rim, fallback).")
    parser.add_argument("--only", metavar="NAMES",
                        help="comma-separated row names")
    args = parser.parse_args()
    if not audit.BIN.is_file():
        print(f"GUI_GLASS_CHECK: FAIL — missing binary: {audit.BIN}",
              file=sys.stderr)
        return 1
    if not THEME_PATH.is_file():
        print(f"GUI_GLASS_CHECK: FAIL — missing theme: {THEME_PATH}",
              file=sys.stderr)
        return 1
    names = {item.strip() for item in args.only.split(",") if item.strip()} \
        if args.only else set()
    unknown = sorted(names - {name for name, _ in ROWS})
    if unknown:
        parser.error("unknown row(s): " + ", ".join(unknown))
    Path(DEFAULT_OUT).mkdir(parents=True, exist_ok=True)
    rows = Rows(names)
    with tempfile.TemporaryDirectory(prefix="glass_themes_", dir="/tmp/opencode") as temp:
        rows.tempdir = Path(temp)
        for name, function in ROWS:
            rows.run(name, function)
    failures = [name for name, ok, _ in rows.results if not ok]
    print(f"\nGUI_GLASS_CHECK: {'PASS' if not failures else 'FAIL'} "
          f"({len(rows.results) - len(failures)}/{len(rows.results)} rows)")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"GUI_GLASS_CHECK: FAIL — {exc}", file=sys.stderr)
        sys.exit(1)
