#!/usr/bin/env python3
#opencode: GUI effect-contrast gate — text over the production glass surface
#   (the Aero pause panel), the default flat panel and a horizon depth profile
#   must keep WCAG 2.2 AA contrast (>= 4.5:1) with the cursor light on and
#   off. ROIs are the escape menu's hint / title / button bands at the
#   1024x768 audit window; the glyph/background luminances are sampled from
#   the rendered frame (the brightest band vs the panel band behind it). The
#   depth row shapes the same buttons through the SDF surface program
#   (mode 1 horizon vs mode 0 flat) and asserts both the AA floor and that
#   the profile actually rendered. GPU display required.

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path

import numpy as np

import audit_common as audit


THEME_DEFAULT = audit.ROOT / "resources" / "style" / "theme.json"
THEME_AERO = audit.ROOT / "resources" / "style" / "theme_aero.json"
#opencode: escape menu bands at 1024x768 (the grid scales with the window).
ROIS = {
    "hint": (350, 635, 670, 660),
    "title": (405, 150, 610, 200),
    "button": (280, 230, 745, 280),
}
AA_MIN = 4.5
TIMEOUT = 120
CURSOR_AWAY = (200, 400)
CURSOR_ON_HINT = (512, 648)


def relative_luminance(rgb):
    linear = rgb / 255.0
    linear = np.where(linear <= 0.03928, linear / 12.92,
                      ((linear + 0.055) / 1.055) ** 2.4)
    return 0.2126 * linear[..., 0] + 0.7152 * linear[..., 1] \
        + 0.0722 * linear[..., 2]


def contrast_in(image, roi):
    x0, y0, x1, y1 = roi
    luminance = relative_luminance(image[y0:y1, x0:x1])
    glyph = luminance >= np.percentile(luminance, 92)
    background = luminance <= np.percentile(luminance, 60)
    return (float(luminance[glyph].mean()) + 0.05) \
        / (float(luminance[background].mean()) + 0.05)


def capture(theme_path, cursor, directory):
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_SKIP_TESTS": "1",
        "GX_FIXED_DT": "0.0166666667",
        "GX_GUI_SHOW_MENU": "1",
        "GX_GUI_THEME": str(theme_path),
        "GX_AI_INPUT": f"1.0 move {cursor[0]} {cursor[1]} 200",
    }, clean=True)
    result = audit.run_scene("--gfx", environment, width=None, height=None,
                             fps=None, frames=110, shot_frame=100,
                             screenshot_dir=directory, timeout=TIMEOUT,
                             check=False)
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError(f"capture missing (exit {result.returncode})")
    return audit.load_rgb(shot).astype(np.float64)


def write_theme(directory, name, data):
    path = directory / f"{name}.json"
    path.write_text(json.dumps(data), encoding="utf-8")
    return path


def outline_theme(width):
    theme = json.loads(THEME_AERO.read_text(encoding="utf-8"))
    theme["textOutline"] = {"width": width, "color": [0, 0, 0, 255]}
    return theme


class Rows:
    def __init__(self, only):
        self.only = only
        self.results = []
        self.images = {}
        self.theme_dir = None

    def run(self, name, function):
        if self.only and name not in self.only:
            return
        try:
            ok, note = function(self)
        except (OSError, RuntimeError, ValueError) as error:
            ok, note = False, str(error)
        self.results.append((name, ok, note))
        print(f"{name:<26} {'PASS' if ok else 'FAIL'}  {note}", flush=True)


def row_glass_text(rows):
    image = rows.images["aero"]
    ratios = {name: contrast_in(image, roi) for name, roi in ROIS.items()}
    worst = min(ratios.values())
    ok = worst >= AA_MIN
    return ok, (f"Aero glass text keeps AA ({', '.join(f'{k} {v:.2f}' for k, v in ratios.items())})")


def row_cursor_light(rows):
    image = rows.images["aero-cursor"]
    ratio = contrast_in(image, ROIS["hint"])
    ok = ratio >= AA_MIN
    return ok, f"cursor light on the hint keeps AA ({ratio:.2f}:1)"


def row_default_text(rows):
    image = rows.images["default"]
    ratios = {name: contrast_in(image, roi) for name, roi in ROIS.items()}
    worst = min(ratios.values())
    ok = worst >= AA_MIN
    return ok, (f"default flat panel keeps AA ({', '.join(f'{k} {v:.2f}' for k, v in ratios.items())})")


def row_outline_contrast(rows):
    on_theme = write_theme(rows.theme_dir, "aero-outline-on",
                           outline_theme(1.0))
    off_theme = write_theme(rows.theme_dir, "aero-outline-off",
                            outline_theme(0.0))
    with tempfile.TemporaryDirectory(prefix="gui_contrast_outline_",
                                     dir="/tmp/opencode") as temp:
        directory = Path(temp)
        on = capture(on_theme, CURSOR_AWAY, directory / "on")
        off = capture(off_theme, CURSOR_AWAY, directory / "off")
    on_ratios = {name: contrast_in(on, roi) for name, roi in ROIS.items()}
    off_ratios = {name: contrast_in(off, roi) for name, roi in ROIS.items()}
    worst = min(on_ratios.values())
    #ai: the hint has its own opaque backing chip, so a black outline is not
    #   expected to lift it; the outline must lift the title/button text on
    #   the glass panel without dropping any band below AA.
    improved = all(on_ratios[name] > off_ratios[name] + 0.1
                   for name in ("title", "button"))
    ok = worst >= AA_MIN and improved
    return ok, (f"outline lifts glass text contrast "
                f"({', '.join(f'{k} {off_ratios[k]:.2f}->{on_ratios[k]:.2f}' for k in ROIS)})")


def depth_theme(directory, mode, name):
    theme = json.loads(THEME_AERO.read_text(encoding="utf-8"))
    theme.setdefault("surfaceDepth", {})["mode"] = mode
    return write_theme(directory, name, theme)


def row_depth_profile(rows):
    #opencode: the horizon profile (mode 1) is unshipped but authorable; the
    #   SDF surface program must shape the same buttons without dropping the
    #   text band under AA, and the flat/profiled pair must actually differ.
    flat_theme = depth_theme(rows.theme_dir, 0, "aero-depth-flat")
    horizon_theme = depth_theme(rows.theme_dir, 1, "aero-depth-horizon")
    with tempfile.TemporaryDirectory(prefix="gui_contrast_depth_",
                                     dir="/tmp/opencode") as temp:
        directory = Path(temp)
        flat = capture(flat_theme, CURSOR_AWAY, directory / "flat")
        horizon = capture(horizon_theme, CURSOR_AWAY, directory / "horizon")
    ratios = {name: contrast_in(horizon, roi) for name, roi in ROIS.items()}
    worst = min(ratios.values())
    shaped = max(float(np.abs(horizon[y0:y1, x0:x1]
                              - flat[y0:y1, x0:x1]).mean())
                 for x0, y0, x1, y1 in ROIS.values())
    ok = worst >= AA_MIN and shaped >= 2.0
    return ok, (f"horizon depth keeps AA "
                f"({', '.join(f'{k} {v:.2f}' for k, v in ratios.items())}; "
                f"profile delta {shaped:.2f})")


def row_determinism(rows):
    with tempfile.TemporaryDirectory(prefix="gui_contrast_repeat_",
                                     dir="/tmp/opencode") as temp:
        repeat = capture(THEME_AERO, CURSOR_AWAY, Path(temp))
    delta = audit.mean_abs_diff(rows.images["aero"], repeat)
    return delta == 0.0, f"repeat run identical (mean {delta:.4f})"


ROWS = [
    ("glass-text", row_glass_text),
    ("cursor-light", row_cursor_light),
    ("default-text", row_default_text),
    ("depth-profile", row_depth_profile),
    ("outline-contrast", row_outline_contrast),
    ("determinism", row_determinism),
]


def main():
    parser = argparse.ArgumentParser(
        description="Validate text contrast over glass and flat panels.")
    parser.add_argument("--only", metavar="NAMES",
                        help="comma-separated row names")
    args = parser.parse_args()
    if not audit.BIN.is_file() or not THEME_AERO.is_file():
        print(f"GUI_EFFECT_CONTRAST_CHECK: FAIL — missing binary or theme",
              file=sys.stderr)
        return 1
    names = {item.strip() for item in args.only.split(",") if item.strip()} \
        if args.only else set()
    unknown = sorted(names - {name for name, _ in ROWS})
    if unknown:
        parser.error("unknown row(s): " + ", ".join(unknown))
    rows = Rows(names)
    with tempfile.TemporaryDirectory(prefix="gui_contrast_", dir="/tmp/opencode") as temp:
        directory = Path(temp)
        rows.theme_dir = directory
        rows.images["aero"] = capture(THEME_AERO, CURSOR_AWAY, directory / "aero")
        rows.images["aero-cursor"] = capture(THEME_AERO, CURSOR_ON_HINT,
                                             directory / "aero-cursor")
        rows.images["default"] = capture(THEME_DEFAULT, CURSOR_AWAY,
                                         directory / "default")
        for name, function in ROWS:
            rows.run(name, function)
    failures = [name for name, ok, _ in rows.results if not ok]
    print(f"\nGUI_EFFECT_CONTRAST_CHECK: {'PASS' if not failures else 'FAIL'} "
          f"({len(rows.results) - len(failures)}/{len(rows.results)} rows)")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"GUI_EFFECT_CONTRAST_CHECK: FAIL — {exc}", file=sys.stderr)
        sys.exit(1)
