#!/usr/bin/env python3
#opencode: GUI pause-blur gate — the EscapeMenu backdrop over a renderer scene
#   must be the current 3D frame, blurred with the shader's own kernel and
#   dimmed with the theme's overlay token (audit gate gap 3). A stale or
#   unseeded gui.ping (near-black backdrop), a silently unloaded blur program
#   (dim-only backdrop) and a missing menu all fail. GPU display required
#   (DISPLAY, default :1).

import argparse
import json
import sys
import tempfile
from pathlib import Path

import numpy as np

import audit_common as audit


SCENE = "--gfx"
FRAMES = 110
SHOT_FRAME = 100
TIMEOUT = 180
THEME_PATH = audit.ROOT / "resources" / "style" / "theme.json"
#opencode: the kernel mirrors resources/shader/postprocess/blur.frag — center
#   tap plus two linear-sampled rings at 2.0*offset[i] pixels, gamma 2.2.
BLUR_OFFSETS = (0.0, 1.3846153846, 3.2307692308)
BLUR_WEIGHTS = (0.2270270270, 0.3162162162, 0.0702702703)
BLUR_RADIUS_SCALE = 2.0
#opencode: EscapeMenu panel = RectGrid(1.5, 1.5, 5.0, 5.5) on an 8x8 grid.
PANEL_FRAC = (1.5 / 8.0, 1.5 / 8.0, 6.5 / 8.0, 7.0 / 8.0)
ROI_INSET = 12
PANEL_INSET = 6
MODEL_MAX = 1.0        # backdrop vs blur+dim model (correct ~0.3; dim-only 1.3)
BLUR_MARGIN = 0.5      # model must beat the dim-only fit by this margin
PANEL_MIN = 30.0       # panel region differs from the scene (correct ~72)


def capture(menu, directory):
    directory.mkdir(parents=True, exist_ok=True)
    over = {"GX_SKIP_TESTS": "1", "GX_FIXED_DT": "0.0166666667"}
    if menu:
        over["GX_GUI_SHOW_MENU"] = "1"
    environment = audit.scene_env(over, clean=True)
    result = audit.run_scene(SCENE, environment, width=None, height=None,
                             fps=None, frames=FRAMES, shot_frame=SHOT_FRAME,
                             screenshot_dir=directory, timeout=TIMEOUT,
                             check=False)
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError(f"capture missing (exit {result.returncode})")
    return audit.load_rgb(shot).astype(np.float64)


def blur_scene(image):
    #opencode: degamma, weighted taps (bilinear offsets rounded to pixels; the
    #   ROI inset keeps the roll wrap out of the measured area), regamma.
    linear = np.power(image / 255.0, 2.2)
    acc = linear * BLUR_WEIGHTS[0]
    directions = ((1, 1), (1, -1), (-1, 1), (-1, -1), (1, 0), (-1, 0), (0, 1), (0, -1))
    for ring in (1, 2):
        step = BLUR_RADIUS_SCALE * BLUR_OFFSETS[ring]
        for dx, dy in directions:
            ox = int(round(dx * step))
            oy = int(round(dy * step))
            acc += np.roll(np.roll(linear, oy, axis=0), ox, axis=1) \
                * (0.25 * BLUR_WEIGHTS[ring])
    return np.power(np.clip(acc, 0.0, 1.0), 1.0 / 2.2) * 255.0


def dim_overlay(image, color):
    alpha = color[3] / 255.0
    return image * (1.0 - alpha) + np.array(color[:3], dtype=np.float64) * alpha


def masks(shape):
    height, width = shape[:2]
    ys, xs = np.mgrid[0:height, 0:width]
    x0, y0, x1, y1 = (PANEL_FRAC[0] * width, PANEL_FRAC[1] * height,
                      PANEL_FRAC[2] * width, PANEL_FRAC[3] * height)
    panel = (xs >= x0 + PANEL_INSET) & (xs <= x1 - PANEL_INSET) \
        & (ys >= y0 + PANEL_INSET) & (ys <= y1 - PANEL_INSET)
    backdrop = ~((xs >= x0) & (xs <= x1) & (ys >= y0) & (ys <= y1)) \
        & (xs >= ROI_INSET) & (xs <= width - 1 - ROI_INSET) \
        & (ys >= ROI_INSET) & (ys <= height - 1 - ROI_INSET)
    return backdrop, panel


def mean_abs(a, b, mask):
    return float(np.abs(a - b)[mask].mean())


class Rows:
    def __init__(self, only):
        self.only = only
        self.results = []
        self.images = {}

    def run(self, name, function):
        if self.only and name not in self.only:
            return
        try:
            ok, note = function(self)
        except (OSError, RuntimeError, ValueError) as error:
            ok, note = False, str(error)
        self.results.append((name, ok, note))
        print(f"{name:<24} {'PASS' if ok else 'FAIL'}  {note}", flush=True)


def load_theme():
    theme = json.loads(THEME_PATH.read_text(encoding="utf-8"))
    return theme["escape"]["dimOverlay"]


def row_backdrop_current_frame(rows):
    scene = rows.images["scene"]
    menu = rows.images["menu"]
    overlay = load_theme()
    expected = dim_overlay(blur_scene(scene), overlay)
    backdrop, _ = masks(scene.shape)
    delta = mean_abs(menu, expected, backdrop)
    ok = delta < MODEL_MAX
    return ok, f"backdrop matches blur+dim of the current frame ({delta:.2f} luma)"


def row_blur_applied(rows):
    scene = rows.images["scene"]
    menu = rows.images["menu"]
    overlay = load_theme()
    expected = dim_overlay(blur_scene(scene), overlay)
    dim_only = dim_overlay(scene, overlay)
    backdrop, _ = masks(scene.shape)
    model_delta = mean_abs(menu, expected, backdrop)
    dim_delta = mean_abs(menu, dim_only, backdrop)
    ok = dim_delta > model_delta + BLUR_MARGIN
    return ok, f"blur kernel applied (model {model_delta:.2f} beats dim-only {dim_delta:.2f})"


def row_panel_present(rows):
    scene = rows.images["scene"]
    menu = rows.images["menu"]
    _, panel = masks(scene.shape)
    delta = mean_abs(menu, scene, panel)
    ok = delta > PANEL_MIN
    return ok, f"pause panel and buttons render over the scene ({delta:.0f} luma)"


def row_determinism(rows):
    with tempfile.TemporaryDirectory(prefix="pause_blur_repeat_", dir="/tmp/opencode") as temp:
        repeat = capture(True, Path(temp))
    delta = audit.mean_abs_diff(rows.images["menu"], repeat)
    return delta == 0.0, f"repeat run identical (mean {delta:.4f})"


ROWS = [
    ("backdrop-current-frame", row_backdrop_current_frame),
    ("blur-applied", row_blur_applied),
    ("panel-present", row_panel_present),
    ("determinism", row_determinism),
]


def main():
    parser = argparse.ArgumentParser(
        description="Validate the pause blur over a renderer scene.")
    parser.add_argument("--only", metavar="NAMES",
                        help="comma-separated row names")
    args = parser.parse_args()
    if not audit.BIN.is_file():
        print(f"GUI_PAUSE_BLUR_CHECK: FAIL — missing binary: {audit.BIN}",
              file=sys.stderr)
        return 1
    if not THEME_PATH.is_file():
        print(f"GUI_PAUSE_BLUR_CHECK: FAIL — missing theme: {THEME_PATH}",
              file=sys.stderr)
        return 1
    names = {item.strip() for item in args.only.split(",") if item.strip()} \
        if args.only else set()
    unknown = sorted(names - {name for name, _ in ROWS})
    if unknown:
        parser.error("unknown row(s): " + ", ".join(unknown))
    rows = Rows(names)
    with tempfile.TemporaryDirectory(prefix="pause_blur_", dir="/tmp/opencode") as temp:
        directory = Path(temp)
        rows.images["scene"] = capture(False, directory / "scene")
        rows.images["menu"] = capture(True, directory / "menu")
        for name, function in ROWS:
            rows.run(name, function)
    failures = [name for name, ok, _ in rows.results if not ok]
    print(f"\nGUI_PAUSE_BLUR_CHECK: {'PASS' if not failures else 'FAIL'} "
          f"({len(rows.results) - len(failures)}/{len(rows.results)} rows)")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"GUI_PAUSE_BLUR_CHECK: FAIL — {exc}", file=sys.stderr)
        sys.exit(1)
