#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: GUI gradient-depth gate — authored gradients must survive the SDF
#   surface path, and the themed depth profiles (horizon/metallic) must shape
#   the surface profile at their token positions. Falls back to the flat
#   rectangle path when the SDF program is disabled. GPU display required.

import argparse
import json
import re
import os
import sys
import tempfile
from pathlib import Path

import numpy as np

import audit_common as audit


DEFAULT_OUT = "/tmp/opencode/gui_depth_check"
THEME_PATH = audit.ROOT / "resources" / "style" / "theme.json"
TEMPLATE = "GUITESTER"
# GuiTester fixture panels in the 1024x768 audit window (grid units x8):
#   vertical red->blue   (0.5, 1.4, 3.0, 1.0) -> x 64..448,  y 134..230
#   horizontal green->yellow (4.5, 1.4, 3.0, 1.0) -> x 576..960, y 134..230
#   the ROI insets the edges so the SDF hairline stroke and the AA rim do not
#   register as a profile peak (they sit at the silhouette, not in the fill).
VERTICAL_ROI = (142, 222, 72, 440)
HORIZONTAL_ROI = (142, 222, 584, 952)
#opencode: four-point panel grid (0.5, 4.1, 3.0, 1.0) -> x 64..448, y 394..490.
#   The title canvas draws its menu over the panel's middle band, so the corner
#   probes sit in the clean top/bottom strips (inset past the hairline/AA rim).
FOUR_POINT_CORNERS = {
    "tl": (400, 412, 76, 116),
    "tr": (400, 412, 396, 436),
    "bl": (472, 484, 76, 116),
    "br": (472, 484, 396, 436),
}
DEPTH_DEFAULTS = {
    "mode": 0,
    "horizonPos": 0.45,
    "horizonSoft": 0.16,
    "horizonGlow": 0.40,
    "metalBand": 0.38,
    "metalWidth": 0.10,
    "metalStrength": 0.35,
    "metalRim": 0.22,
}
TIMEOUT = 120


def themed(directory, mode, **overrides):
    theme = json.loads(THEME_PATH.read_text(encoding="utf-8"))
    settings = dict(DEPTH_DEFAULTS)
    settings["mode"] = mode
    settings.update(overrides)
    theme["surfaceDepth"] = settings
    path = directory / f"depth-mode{mode}.json"
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


DEPTH_TRACE_RE = re.compile(
    r"GX_DEPTH_TRACE: elem_depth=(\d+) chosen=(\d+) theme=(\d+)")


def capture_log(theme_path, directory, extra=None):
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
    return audit.strip_ansi(result.stdout or "")


def vertical_profile(image, channel=None):
    """Mean of one channel (default: all) per row inside the vertical panel."""
    y0, y1, x0, x1 = VERTICAL_ROI
    band = image[y0:y1, x0:x1]
    if channel is None:
        return band.mean(axis=(1, 2))
    return band[:, :, channel].mean(axis=1)


def horizontal_profile(image, channel=None):
    y0, y1, x0, x1 = HORIZONTAL_ROI
    band = image[y0:y1, x0:x1]
    if channel is None:
        return band.mean(axis=(0, 2))
    return band[:, :, channel].mean(axis=0)


class Rows:
    def __init__(self, only):
        self.only = only
        self.results = []
        self.tempdir = None

    def run(self, name, function):
        if self.only and name not in self.only:
            return
        try:
            ok, note = function(self)
        except (OSError, RuntimeError, ValueError) as error:
            ok, note = False, str(error)
        self.results.append((name, ok, note))
        print(f"{name:<22} {'PASS' if ok else 'FAIL'}  {note}", flush=True)


def row_explicit_vertical(rows):
    image = capture(themed(rows.tempdir, 0), rows.tempdir / "vertical")
    blue = vertical_profile(image, channel=2)
    quarter = len(blue) // 4
    steps = [blue[:quarter].mean(), blue[quarter:2 * quarter].mean(),
             blue[2 * quarter:3 * quarter].mean(), blue[3 * quarter:].mean()]
    ok = (steps[0] < 80.0 and steps[-1] > 170.0
          and steps[0] < steps[1] < steps[2] < steps[3])
    return ok, f"authored vertical ramp survives SDF (blue {steps[0]:.0f}->{steps[-1]:.0f})"


def row_explicit_four_point(rows):
    image = capture(themed(rows.tempdir, 0), rows.tempdir / "four-point")
    corners = {}
    for name, (y0, y1, x0, x1) in FOUR_POINT_CORNERS.items():
        patch = image[y0:y1, x0:x1].astype(np.float64)
        corners[name] = patch.mean(axis=(0, 1))
    tl, tr = corners["tl"], corners["tr"]
    bl, br = corners["bl"], corners["br"]
    #opencode: authored corners: TL red, TR green, BL blue, BR yellow.
    ok = (tl[0] > 150.0 and tl[0] > tl[1] * 1.5 and tl[2] < 100.0
          and tr[1] > 150.0 and tr[1] > tr[0] * 1.5 and tr[2] < 100.0
          and bl[2] > 150.0 and bl[2] > bl[0] * 1.5
          and br[0] > 150.0 and br[1] > 150.0 and br[2] < 100.0)
    return ok, (f"authored four-point corners survive SDF (tl {tl[0]:.0f}/{tl[1]:.0f}/{tl[2]:.0f}, "
                f"tr {tr[0]:.0f}/{tr[1]:.0f}/{tr[2]:.0f}, bl {bl[0]:.0f}/{bl[1]:.0f}/{bl[2]:.0f}, "
                f"br {br[0]:.0f}/{br[1]:.0f}/{br[2]:.0f})")


def row_explicit_horizontal(rows):
    image = capture(themed(rows.tempdir, 0), rows.tempdir / "horizontal")
    mean = horizontal_profile(image)
    left = mean[:64].mean()
    right = mean[-64:].mean()
    middle = mean[len(mean) // 2 - 32:len(mean) // 2 + 32].mean()
    ok = left < 110.0 and right > 150.0 and left < middle < right
    return ok, f"authored horizontal ramp survives SDF (mean {left:.0f}->{right:.0f})"


def row_horizon(rows):
    image = capture(themed(rows.tempdir, 1), rows.tempdir / "horizon")
    profile = vertical_profile(image)
    peak = int(np.argmax(profile))
    position = peak / max(len(profile) - 1, 1)
    gain = profile.max() - profile.min()
    ends = (profile[:6].mean() + profile[-6:].mean()) * 0.5
    #opencode: the hue-preserving lift is deliberately gentle — the band must
    #   stand out over the ramp noise but never saturate (gain ~34 on the
    #   fixture's red->blue panel).
    ok = (abs(position - DEPTH_DEFAULTS["horizonPos"]) <= 0.08
          and gain > 20.0 and profile.max() - ends > 15.0)
    return ok, (f"horizon band at t={position:.2f} (token "
                f"{DEPTH_DEFAULTS['horizonPos']:.2f}), gain {gain:.0f}")


def row_metallic(rows):
    image = capture(themed(rows.tempdir, 2), rows.tempdir / "metallic")
    profile = vertical_profile(image)
    peak = int(np.argmax(profile))
    position = peak / max(len(profile) - 1, 1)
    gain = profile.max() - profile.min()
    ok = (abs(position - DEPTH_DEFAULTS["metalBand"]) <= 0.10 and gain > 50.0)
    return ok, (f"metallic highlight at t={position:.2f} (token "
                f"{DEPTH_DEFAULTS['metalBand']:.2f}), gain {gain:.0f}")


def row_metallic_horizontal(rows):
    image = capture(themed(rows.tempdir, 2), rows.tempdir / "metallic-x")
    #opencode: the horizontal fixture (4.5, 1.4, 3.0, 1.0) -> x 576..960,
    #   y 134..230; the metallic band must follow X and leave Y flat.
    roi = image[144:220, 586:950].astype(np.float64)
    xprofile = roi.mean(axis=(0, 2))
    yprofile = roi.mean(axis=(1, 2))
    peak = int(np.argmax(xprofile))
    position = peak / max(len(xprofile) - 1, 1)
    gain = float(xprofile.max() - xprofile.min())
    flat_y = float(yprofile.max() - yprofile.min())
    ok = (abs(position - DEPTH_DEFAULTS["metalBand"]) <= 0.10
          and gain > 50.0 and flat_y < 5.0)
    return ok, (f"horizontal metallic band at t={position:.2f} along X "
                f"(gain {gain:.0f}, Y flat {flat_y:.1f})")


def row_per_element(rows):
    seen = set()
    for mode in (0, 1):
        log = capture_log(themed(rows.tempdir, mode),
                          rows.tempdir / f"per-element{mode}",
                          extra={"GX_GUI_DEPTH_TRACE": "1"})
        seen |= {(int(a), int(b), int(c))
                 for a, b, c in DEPTH_TRACE_RE.findall(log)}
    want = {(1, 0, 0), (3, 2, 0), (1, 0, 1), (3, 2, 1)}
    ok = want <= seen
    return ok, (f"per-element override beats the theme mode "
                f"(none {((1,0,0)) in seen and ((1,0,1)) in seen}, "
                f"metallic {((3,2,0)) in seen and ((3,2,1)) in seen})")


def row_fallback(rows):
    image = capture(themed(rows.tempdir, 1), rows.tempdir / "fallback",
                    extra={"GX_GUI_SDF": "0"})
    blue = vertical_profile(image, channel=2)
    profile = vertical_profile(image)
    peak = int(np.argmax(profile))
    position = peak / max(len(profile) - 1, 1)
    ramp = blue[:6].mean() < 80.0 and blue[-6:].mean() > 170.0
    band = profile.max() - profile.min() > 30.0 and 0.15 < position < 0.85
    ok = ramp and not band
    return ok, (f"SDF off falls back to the authored ramp "
                f"(blue {blue[:6].mean():.0f}->{blue[-6:].mean():.0f})")


def row_determinism(rows):
    first = capture(themed(rows.tempdir, 1), rows.tempdir / "det-a")
    second = capture(themed(rows.tempdir, 1), rows.tempdir / "det-b")
    delta = audit.mean_abs_diff(first, second)
    return delta == 0.0, f"repeat run identical (mean {delta:.4f})"


ROWS = [
    ("explicit-vertical", row_explicit_vertical),
    ("explicit-horizontal", row_explicit_horizontal),
    ("explicit-four-point", row_explicit_four_point),
    ("horizon", row_horizon),
    ("metallic", row_metallic),
    ("metallic-horizontal", row_metallic_horizontal),
    ("per-element", row_per_element),
    ("fallback", row_fallback),
    ("determinism", row_determinism),
]


def main():
    parser = argparse.ArgumentParser(
        description="Validate SDF gradient modes and the surface depth profiles.")
    parser.add_argument("--only", metavar="NAMES",
                        help="comma-separated row names")
    args = parser.parse_args()
    if not audit.BIN.is_file():
        print(f"GUI_DEPTH_CHECK: FAIL — missing binary: {audit.BIN}",
              file=sys.stderr)
        return 1
    if not THEME_PATH.is_file():
        print(f"GUI_DEPTH_CHECK: FAIL — missing theme: {THEME_PATH}",
              file=sys.stderr)
        return 1
    names = {item.strip() for item in args.only.split(",") if item.strip()} \
        if args.only else set()
    unknown = sorted(names - {name for name, _ in ROWS})
    if unknown:
        parser.error("unknown row(s): " + ", ".join(unknown))
    Path(DEFAULT_OUT).mkdir(parents=True, exist_ok=True)
    rows = Rows(names)
    with tempfile.TemporaryDirectory(prefix="depth_themes_", dir="/tmp/opencode") as temp:
        rows.tempdir = Path(temp)
        for name, function in ROWS:
            rows.run(name, function)
    failures = [name for name, ok, _ in rows.results if not ok]
    print(f"\nGUI_DEPTH_CHECK: {'PASS' if not failures else 'FAIL'} "
          f"({len(rows.results) - len(failures)}/{len(rows.results)} rows)")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"GUI_DEPTH_CHECK: FAIL — {exc}", file=sys.stderr)
        sys.exit(1)
