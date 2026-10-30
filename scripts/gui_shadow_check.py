#!/usr/bin/env python3
#opencode: GUI text-shadow gate — the themed shadow tokens must reach the
#   draw path (bitmap and SDF), stay deterministic, and disable cleanly when
#   the theme zeroes them. The forced GX_GUI_TEXT_SHADOW hook turns the
#   shadow on for every text so the gate does not depend on a template
#   opting in.
#
# Usage:
#   DISPLAY=:1 python3 scripts/gui_shadow_check.py
#   DISPLAY=:1 python3 scripts/gui_shadow_check.py --only bitmap-applied

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

import audit_common as audit


DEFAULT_OUT = "/tmp/opencode/gui_shadow_check"
THEME_PATH = audit.ROOT / "resources" / "style" / "theme.json"
BITMAP_TEMPLATE = "SETTINGS"
SDF_TEMPLATE = "GUITESTER"
SDF_ROI = (341, 447, 794, 1114)  # y0, y1, x0, x1 of the SDF 96px demo
BUTTON_ROI = (615, 700, 0, 1024)  # light action-button row
SHADOW_OFFSET = (3, 3)
TIMEOUT = 120


def shadow_theme(offset_x, offset_y, color, small_x=None, small_y=None):
    theme = json.loads(THEME_PATH.read_text(encoding="utf-8"))
    theme["textShadow"] = {
        "offsetX": offset_x,
        "offsetY": offset_y,
        "offsetXSmall": offset_x if small_x is None else small_x,
        "offsetYSmall": offset_y if small_y is None else small_y,
        "color": list(color),
    }
    return theme


def outline_theme(width, color):
    theme = json.loads(THEME_PATH.read_text(encoding="utf-8"))
    theme["textOutline"] = {"width": width, "color": list(color)}
    return theme


def write_theme(directory, name, data):
    path = directory / f"{name}.json"
    path.write_text(json.dumps(data), encoding="utf-8")
    return path


def capture(template, overrides, directory, force_shadow=True):
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_SKIP_TESTS": "1",
        "GX_FIXED_DT": "0.0166666667",
        "GX_GUI_TEMPLATE_FORCE": template,
    }, clean=True)
    if force_shadow:
        environment["GX_GUI_TEXT_SHADOW"] = "1"
    for key, value in overrides.items():
        environment[key] = str(value)
    result = audit.run_scene("--title", environment, width=None, height=None,
                             fps=None, frames=90, shot_frame=80,
                             screenshot_dir=directory, timeout=TIMEOUT,
                             check=False)
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError(f"{template} capture missing (exit {result.returncode})")
    return audit.load_rgb(shot)


def shifted(mask, dy, dx):
    out = np.zeros_like(mask)
    height, width = mask.shape
    y_start, y_end = max(dy, 0), height + min(dy, 0)
    x_start, x_end = max(dx, 0), width + min(dx, 0)
    out[y_start:y_end, x_start:x_end] = mask[y_start - dy:y_end - dy,
                                             x_start - dx:x_end - dx]
    return out


def direction_scores(off_image, on_image, roi=None, candidates=None,
                     threshold=220.0):
    y0, y1, x0, x1 = roi or BUTTON_ROI
    before = off_image[y0:y1, x0:x1].mean(axis=2)
    after = on_image[y0:y1, x0:x1].mean(axis=2)
    delta = np.clip(before - after, 0.0, None)
    glyphs = before > threshold
    added = delta > 25.0
    if added.sum() < 100:
        return None
    scores = {}
    for dy, dx in (candidates or ((3, 3), (0, 0), (-3, -3), (3, 0), (0, 3))):
        overlap = (shifted(glyphs, dy, dx) & added).sum()
        scores[(dy, dx)] = float(overlap) / float(added.sum())
    return scores


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
        print(f"{name:<20} {'PASS' if ok else 'FAIL'}  {note}", flush=True)


def row_determinism(rows):
    off_theme = write_theme(rows.tempdir, "shadow-off",
                            shadow_theme(0, 0, (0, 0, 0, 0)))
    base = capture(BITMAP_TEMPLATE, {"GX_GUI_THEME": off_theme}, rows.tempdir / "det-base")
    off = capture(BITMAP_TEMPLATE, {"GX_GUI_THEME": off_theme}, rows.tempdir / "det-off")
    delta = audit.mean_abs_diff(base, off)
    return delta <= 0.05, f"zeroed tokens render identically (mean {delta:.4f})"


def row_bitmap_applied(rows):
    off_theme = write_theme(rows.tempdir, "shadow-off",
                            shadow_theme(0, 0, (0, 0, 0, 0)))
    on_theme = write_theme(rows.tempdir, "shadow-on",
                           shadow_theme(3, 3, (0, 0, 0, 200)))
    off = capture(BITMAP_TEMPLATE, {"GX_GUI_THEME": off_theme}, rows.tempdir / "bmp-off")
    on = capture(BITMAP_TEMPLATE, {"GX_GUI_THEME": on_theme}, rows.tempdir / "bmp-on")
    mean = audit.mean_abs_diff(off, on)
    changed = audit.diff_frac(off, on, 30)
    scores = direction_scores(off, on)
    if mean < 0.5 or changed < 0.005:
        return False, f"shadow too weak (mean {mean:.3f}, changed {changed:.4f})"
    if not scores:
        return False, "no shadow pixels in the button row"
    best = max(scores, key=scores.get)
    if best != SHADOW_OFFSET or scores[(0, 0)] > 0.02:
        return False, f"shadow not offset down-right: {scores}"
    return True, (f"mean {mean:.3f}, changed {changed:.4f}, "
                  f"direction {scores[SHADOW_OFFSET]:.3f}")


def row_sdf_applied(rows):
    white_theme = write_theme(rows.tempdir, "shadow-white",
                              shadow_theme(3, 3, (255, 255, 255, 255)))
    base = capture(SDF_TEMPLATE, {}, rows.tempdir / "sdf-base")
    white = capture(SDF_TEMPLATE, {"GX_GUI_THEME": white_theme}, rows.tempdir / "sdf-white")
    y0, y1, x0, x1 = SDF_ROI
    mean = audit.mean_abs_diff(base[y0:y1, x0:x1], white[y0:y1, x0:x1])
    changed = audit.diff_frac(base[y0:y1, x0:x1], white[y0:y1, x0:x1], 30)
    ok = mean > 1.5 and changed > 0.005
    return ok, f"SDF demo ROI mean {mean:.3f}, changed {changed:.4f}"


#opencode: settings rows carry mostly 16px labels (below bodySize 18), so
#   the small-text shadow pair applies there.
SMALL_TEXT_ROI = (300, 700, 260, 760)


def row_small_tokens(rows):
    base_theme = write_theme(rows.tempdir, "shadow-small-base",
                             shadow_theme(3, 3, (0, 0, 0, 200)))
    small_theme = write_theme(rows.tempdir, "shadow-small-on",
                              shadow_theme(3, 3, (0, 0, 0, 200),
                                           small_x=6.0, small_y=0.0))
    base = capture(BITMAP_TEMPLATE, {"GX_GUI_THEME": base_theme},
                   rows.tempdir / "small-base")
    small = capture(BITMAP_TEMPLATE, {"GX_GUI_THEME": small_theme},
                    rows.tempdir / "small-on")
    scores = direction_scores(base, small, roi=SMALL_TEXT_ROI,
        candidates=((0, 6), (3, 3), (0, 3), (6, 6), (6, 0), (3, 0)))
    if not scores:
        return False, "no small-text shadow pixels"
    best = max(scores, key=scores.get)
    ok = best == (0, 6) and scores[(0, 6)] > 0.06 \
        and scores[(3, 3)] < 0.04 and scores[(6, 6)] < 0.04
    return ok, f"small-text pair applies (best {best}, scores {scores})"


def soft_shadow_theme(softness):
    theme = shadow_theme(3, 3, (0, 0, 0, 200), small_x=1.5, small_y=1.5)
    theme["textShadow"]["softness"] = softness
    return theme


def row_soft_applied(rows):
    off_theme = write_theme(rows.tempdir, "shadow-soft-off",
                            soft_shadow_theme(0.0))
    on_theme = write_theme(rows.tempdir, "shadow-soft-on",
                           soft_shadow_theme(3.0))
    off = capture(BITMAP_TEMPLATE, {"GX_GUI_THEME": off_theme},
                  rows.tempdir / "soft-off")
    on = capture(BITMAP_TEMPLATE, {"GX_GUI_THEME": on_theme},
                 rows.tempdir / "soft-on")
    y0, y1, x0, x1 = OUTLINE_TEXT_ROI
    mean = audit.mean_abs_diff(off[y0:y1, x0:x1], on[y0:y1, x0:x1])
    before = off[y0:y1, x0:x1].mean(axis=2)
    after = on[y0:y1, x0:x1].mean(axis=2)
    ring = int((np.clip(before - after, 0.0, None) > 15.0).sum())
    ok = mean > 1.5 and ring > 3000
    return ok, f"soft ring widens the shadow (mean {mean:.2f}, ring px {ring})"


def row_soft_sdf(rows):
    off_theme = write_theme(rows.tempdir, "soft-sdf-off",
                            soft_shadow_theme(0.0))
    on_theme = write_theme(rows.tempdir, "soft-sdf-on",
                           soft_shadow_theme(3.0))
    off = capture(SDF_TEMPLATE, {"GX_GUI_THEME": off_theme},
                  rows.tempdir / "soft-sdf-off")
    on = capture(SDF_TEMPLATE, {"GX_GUI_THEME": on_theme},
                 rows.tempdir / "soft-sdf-on")
    y0, y1, x0, x1 = SDF_ROI
    mean = audit.mean_abs_diff(off[y0:y1, x0:x1], on[y0:y1, x0:x1])
    widened = audit.diff_frac(off[y0:y1, x0:x1], on[y0:y1, x0:x1], 10)
    ok = mean > 1.0 and widened > 0.02
    return ok, (f"SDF shadow blurs through the distance field "
                f"(mean {mean:.3f}, widened {widened:.4f})")


def row_soft_token_off(rows):
    explicit = json.loads(THEME_PATH.read_text(encoding="utf-8"))
    explicit["textShadow"]["softness"] = 0.0
    explicit_theme = write_theme(rows.tempdir, "soft-explicit", explicit)
    missing = json.loads(THEME_PATH.read_text(encoding="utf-8"))
    missing["textShadow"].pop("softness", None)
    missing_theme = write_theme(rows.tempdir, "soft-missing", missing)
    a = capture(BITMAP_TEMPLATE, {"GX_GUI_THEME": explicit_theme},
                rows.tempdir / "soft-explicit-cap")
    b = capture(BITMAP_TEMPLATE, {"GX_GUI_THEME": missing_theme},
                rows.tempdir / "soft-missing-cap")
    delta = audit.mean_abs_diff(a, b)
    return delta <= 0.05, f"explicit and schema-default softness 0 agree (mean {delta:.4f})"


OUTLINE_TEXT_ROI = (300, 700, 260, 760)


def row_outline_applied(rows):
    off_theme = write_theme(rows.tempdir, "outline-off",
                            outline_theme(0.0, (0, 0, 0, 255)))
    on_theme = write_theme(rows.tempdir, "outline-on",
                           outline_theme(1.0, (0, 0, 0, 255)))
    off = capture(BITMAP_TEMPLATE, {"GX_GUI_THEME": off_theme},
                  rows.tempdir / "outline-off", force_shadow=False)
    on = capture(BITMAP_TEMPLATE, {"GX_GUI_THEME": on_theme},
                 rows.tempdir / "outline-on", force_shadow=False)
    y0, y1, x0, x1 = OUTLINE_TEXT_ROI
    mean = audit.mean_abs_diff(off[y0:y1, x0:x1], on[y0:y1, x0:x1])
    dark_off = int((off[y0:y1, x0:x1].mean(axis=2) < 40).sum())
    dark_on = int((on[y0:y1, x0:x1].mean(axis=2) < 40).sum())
    ok = mean > 3.0 and (dark_on - dark_off) > 3000
    return ok, (f"outline changes the text band (mean {mean:.2f}, dark "
                f"+{dark_on - dark_off})")


def row_outline_sdf(rows):
    off_theme = write_theme(rows.tempdir, "outline-off",
                            outline_theme(0.0, (0, 0, 0, 255)))
    on_theme = write_theme(rows.tempdir, "outline-on",
                           outline_theme(1.0, (0, 0, 0, 255)))
    off = capture(SDF_TEMPLATE, {"GX_GUI_THEME": off_theme},
                  rows.tempdir / "outline-sdf-off", force_shadow=False)
    on = capture(SDF_TEMPLATE, {"GX_GUI_THEME": on_theme},
                 rows.tempdir / "outline-sdf-on", force_shadow=False)
    y0, y1, x0, x1 = SDF_ROI
    mean = audit.mean_abs_diff(off[y0:y1, x0:x1], on[y0:y1, x0:x1])
    changed = audit.diff_frac(off[y0:y1, x0:x1], on[y0:y1, x0:x1], 30)
    ok = mean > 0.5 and changed > 0.0005
    return ok, f"SDF demo ROI mean {mean:.3f}, changed {changed:.5f}"


def row_outline_token_off(rows):
    default = capture(BITMAP_TEMPLATE, {}, rows.tempdir / "outline-default",
                      force_shadow=False)
    off_theme = write_theme(rows.tempdir, "outline-off",
                            outline_theme(0.0, (0, 0, 0, 255)))
    off = capture(BITMAP_TEMPLATE, {"GX_GUI_THEME": off_theme},
                  rows.tempdir / "outline-token-off", force_shadow=False)
    delta = audit.mean_abs_diff(default, off)
    return delta <= 0.05, f"width 0 renders identically (mean {delta:.4f})"


def row_layout_with_shadows(rows):
    environment = audit.scene_env({"GX_GUI_TEXT_SHADOW": "1"}, clean=True)
    environment["DISPLAY"] = audit.display()
    result = subprocess.run(
        ["python3", str(audit.ROOT / "scripts" / "gui_layout_check.py"), "--quick"],
        cwd=audit.ROOT, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    tail = audit.strip_ansi(result.stdout or "").splitlines()[-1:] or [""]
    ok = result.returncode == 0 and "GUI-LAYOUT: PASS" in (result.stdout or "")
    return ok, tail[0].strip() or "layout pass with shadows forced"


ROWS = [
    ("determinism", row_determinism),
    ("bitmap-applied", row_bitmap_applied),
    ("small-tokens", row_small_tokens),
    ("sdf-applied", row_sdf_applied),
    ("outline-applied", row_outline_applied),
    ("outline-sdf", row_outline_sdf),
    ("outline-token-off", row_outline_token_off),
    ("soft-applied", row_soft_applied),
    ("soft-sdf", row_soft_sdf),
    ("soft-token-off", row_soft_token_off),
    ("layout-with-shadows", row_layout_with_shadows),
]


def main():
    parser = argparse.ArgumentParser(
        description="Validate the themed GUI text-shadow path.")
    parser.add_argument("--only", metavar="NAMES",
                        help="comma-separated row names")
    args = parser.parse_args()
    if not audit.BIN.is_file():
        print(f"GUI_SHADOW_CHECK: FAIL — missing binary: {audit.BIN}",
              file=sys.stderr)
        return 1
    if not THEME_PATH.is_file():
        print(f"GUI_SHADOW_CHECK: FAIL — missing theme: {THEME_PATH}",
              file=sys.stderr)
        return 1
    names = {item.strip() for item in args.only.split(",") if item.strip()} \
        if args.only else set()
    unknown = sorted(names - {name for name, _ in ROWS})
    if unknown:
        parser.error("unknown row(s): " + ", ".join(unknown))
    out_dir = Path(DEFAULT_OUT)
    out_dir.mkdir(parents=True, exist_ok=True)
    rows = Rows(names)
    with tempfile.TemporaryDirectory(prefix="shadow_themes_", dir="/tmp/opencode") as temp:
        rows.tempdir = Path(temp)
        for name, function in ROWS:
            rows.run(name, function)
    failures = [name for name, ok, _ in rows.results if not ok]
    print(f"\nGUI_SHADOW_CHECK: {'PASS' if not failures else 'FAIL'} "
          f"({len(rows.results) - len(failures)}/{len(rows.results)} rows)")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"GUI_SHADOW_CHECK: FAIL — {exc}", file=sys.stderr)
        sys.exit(1)
