#!/usr/bin/env python3
#opencode: GUI cursor-light gate — the SDF surface highlight must follow the
#   synthesized cursor, stay absent when the strength token is 0, and render
#   deterministically. Uses GX_AI_INPUT move-only parks so no click fires.
#   GPU display required (DISPLAY, default :1).

import argparse
import json
import os
import sys
import tempfile
from pathlib import Path

import numpy as np

import audit_common as audit


DEFAULT_OUT = "/tmp/opencode/gui_cursor_light_check"
THEME_PATH = audit.ROOT / "resources" / "style" / "theme.json"
TEMPLATE = "SETTINGS"
POSITION_A = (294, 237)   # Fullscreen toggle row
POSITION_B = (294, 456)   # Volume slider row
ROI_RADIUS = 60
LIGHT_ON = {"strength": 0.8, "radius": 120.0, "color": [255, 235, 205, 255]}
LIGHT_OFF = {"strength": 0.0, "radius": 120.0, "color": [255, 235, 205, 255]}
FOLLOW_MIN = 50.0      # on: light asymmetry between the two ROIs
APPLIED_MIN = 40.0     # on vs off at the same cursor position
OFF_MAX = 40.0         # off: hover asymmetry only
TIMEOUT = 120


def themed(directory, settings, name):
    theme = json.loads(THEME_PATH.read_text(encoding="utf-8"))
    theme["cursorLight"] = dict(settings)
    path = directory / f"cursor-{name}.json"
    path.write_text(json.dumps(theme), encoding="utf-8")
    return path


GLINT_THEME = {"strength": 0.35, "radius": 120.0, "glint": 0.45,
               "color": [255, 235, 205, 255]}
GLINT_POSITION = (512, 259)   # escape-menu Resume button at 1024x768
GLINT_MIN = 2.0


def capture_glint(theme_path, directory, glint_on):
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_SKIP_TESTS": "1",
        "GX_FIXED_DT": "0.0166666667",
        "GX_GUI_SHOW_MENU": "1",
        "GX_GUI_THEME": str(theme_path),
        "GX_AI_INPUT": f"1.0 move {GLINT_POSITION[0]} {GLINT_POSITION[1]} 300",
    }, clean=True)
    if not glint_on:
        environment["GX_GUI_CURSOR_GLINT"] = "0"
    result = audit.run_scene("--gfx", environment, width=None, height=None,
                             fps=None, frames=110, shot_frame=100,
                             screenshot_dir=directory, timeout=TIMEOUT,
                             check=False)
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError(f"capture missing (exit {result.returncode})")
    return audit.load_rgb(shot)


def capture(theme_path, position, directory):
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_SKIP_TESTS": "1",
        "GX_FIXED_DT": "0.0166666667",
        "GX_GUI_TEMPLATE_FORCE": TEMPLATE,
        "GX_GUI_THEME": str(theme_path),
        "GX_AI_INPUT": f"1.0 move {position[0]} {position[1]} 300",
    }, clean=True)
    result = audit.run_scene("--title", environment, width=None, height=None,
                             fps=None, frames=110, shot_frame=100,
                             screenshot_dir=directory, timeout=TIMEOUT,
                             check=False)
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError(f"capture missing (exit {result.returncode})")
    return audit.load_rgb(shot)


def roi(image, position, radius=ROI_RADIUS):
    x, y = position
    return image[y - radius:y + radius, x - radius:x + radius]


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
        print(f"{name:<16} {'PASS' if ok else 'FAIL'}  {note}", flush=True)


def row_light_follows(rows):
    on_a = capture(themed(rows.tempdir, LIGHT_ON, "on"), POSITION_A,
                   rows.tempdir / "on-a")
    on_b = capture(themed(rows.tempdir, LIGHT_ON, "on"), POSITION_B,
                   rows.tempdir / "on-b")
    a_lift = float(roi(on_a, POSITION_A).mean() - roi(on_a, POSITION_B).mean())
    b_lift = float(roi(on_b, POSITION_B).mean() - roi(on_b, POSITION_A).mean())
    rows.images["on_a"] = on_a
    rows.images["on_b"] = on_b
    ok = a_lift > FOLLOW_MIN and b_lift > FOLLOW_MIN
    return ok, f"highlight follows the cursor (A +{a_lift:.0f}, B +{b_lift:.0f} luma)"


def row_light_applied(rows):
    on_a = rows.images["on_a"]
    off_a = capture(themed(rows.tempdir, LIGHT_OFF, "off"), POSITION_A,
                    rows.tempdir / "off-a")
    on_b = rows.images["on_b"]
    off_b = capture(themed(rows.tempdir, LIGHT_OFF, "off"), POSITION_B,
                    rows.tempdir / "off-b")
    delta_a = audit.mean_abs_diff(roi(on_a, POSITION_A), roi(off_a, POSITION_A))
    delta_b = audit.mean_abs_diff(roi(on_b, POSITION_B), roi(off_b, POSITION_B))
    ok = delta_a > APPLIED_MIN and delta_b > APPLIED_MIN
    return ok, f"light changes the surface (A {delta_a:.0f}, B {delta_b:.0f})"


def row_light_off(rows):
    off_a = capture(themed(rows.tempdir, LIGHT_OFF, "off"), POSITION_A,
                    rows.tempdir / "off-a2")
    off_b = capture(themed(rows.tempdir, LIGHT_OFF, "off"), POSITION_B,
                    rows.tempdir / "off-b2")
    a_bias = float(roi(off_a, POSITION_A).mean() - roi(off_a, POSITION_B).mean())
    b_bias = float(roi(off_b, POSITION_B).mean() - roi(off_b, POSITION_A).mean())
    ok = abs(a_bias) < OFF_MAX and abs(b_bias) < OFF_MAX
    return ok, f"strength 0 stays off (A {a_bias:+.1f}, B {b_bias:+.1f} hover bias)"


def row_hover_glint(rows):
    theme = themed(rows.tempdir, GLINT_THEME, "glint")
    on = capture_glint(theme, rows.tempdir / "glint-on", True)
    off = capture_glint(theme, rows.tempdir / "glint-off", False)
    delta = audit.mean_abs_diff(roi(on, GLINT_POSITION, 70),
                                roi(off, GLINT_POSITION, 70))
    far = audit.mean_abs_diff(roi(on, (200, 600), 40),
                              roi(off, (200, 600), 40))
    ok = delta > GLINT_MIN and far == 0.0
    return ok, (f"hovered button tightens/brightens (delta {delta:.2f}, "
                f"far {far:.2f})")


def row_determinism(rows):
    first = rows.images["on_a"]
    second = capture(themed(rows.tempdir, LIGHT_ON, "on"), POSITION_A,
                     rows.tempdir / "on-a2")
    delta = audit.mean_abs_diff(first, second)
    return delta == 0.0, f"repeat run identical (mean {delta:.4f})"


ROWS = [
    ("light-follows", row_light_follows),
    ("light-applied", row_light_applied),
    ("light-off", row_light_off),
    ("hover-glint", row_hover_glint),
    ("determinism", row_determinism),
]


def main():
    parser = argparse.ArgumentParser(
        description="Validate the cursor light on SDF surfaces.")
    parser.add_argument("--only", metavar="NAMES",
                        help="comma-separated row names")
    args = parser.parse_args()
    if not audit.BIN.is_file():
        print(f"GUI_CURSOR_LIGHT_CHECK: FAIL — missing binary: {audit.BIN}",
              file=sys.stderr)
        return 1
    if not THEME_PATH.is_file():
        print(f"GUI_CURSOR_LIGHT_CHECK: FAIL — missing theme: {THEME_PATH}",
              file=sys.stderr)
        return 1
    names = {item.strip() for item in args.only.split(",") if item.strip()} \
        if args.only else set()
    unknown = sorted(names - {name for name, _ in ROWS})
    if unknown:
        parser.error("unknown row(s): " + ", ".join(unknown))
    Path(DEFAULT_OUT).mkdir(parents=True, exist_ok=True)
    rows = Rows(names)
    with tempfile.TemporaryDirectory(prefix="cursor_themes_", dir="/tmp/opencode") as temp:
        rows.tempdir = Path(temp)
        for name, function in ROWS:
            rows.run(name, function)
    failures = [name for name, ok, _ in rows.results if not ok]
    print(f"\nGUI_CURSOR_LIGHT_CHECK: {'PASS' if not failures else 'FAIL'} "
          f"({len(rows.results) - len(failures)}/{len(rows.results)} rows)")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"GUI_CURSOR_LIGHT_CHECK: FAIL — {exc}", file=sys.stderr)
        sys.exit(1)
