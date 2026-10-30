#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: background gate — the polisher (runtime defaults drive the backdrop live,
#   explicit default renders identically), the palette systems (the image
#   decomposer's index + palette textures render through palette modes 2 and 7;
#   mode 7's SNES row cycling animates), and the title scene's TITLE preset
#   (saturated, palette-live, animated behind the GUI). Markers + editor panels
#   + headless polisher tests are asserted too.

import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

import audit_common as audit

SCENE = "--background"
FRAMES = 30
BASE_MIN_MEAN = 10.0
BASE_MIN_STD = 5.0
MIN_POLISH_DELTA = 10.0
DEFAULT_DELTA_MAX = 0.05
APPLY_RE = re.compile(r"POLISHER: (gX_BG_\w+)=([\d.]+) applied")
UI_MARKER = "GFX_POLISHER_UI: panel=1"
BG_UI_MARKER = "GFX_BG_UI: panel=1"
TITLE_PRESET_MARKER = "BG_PRESET: TITLE"

#ai: title leg thresholds — the TITLE preset paints a saturated, animated
#   palette backdrop behind the title GUI.
MIN_TITLE_SATURATION = 0.15
MIN_TITLE_MODE_DELTA = 0.3  # the title GUI covers most of the backdrop;
                               # a visible sliver still proves the CLI override
MIN_TITLE_ANIM_DELTA = 0.3  # measured through the title GUI; any
                               # nonzero drift proves the palette is live

#ai: palette leg thresholds — the decomposed render is heavily quantized and
#   saturated; mode 7 keeps cycling columns over time.
MIN_PALETTE_SATURATION = 0.20
MAX_PALETTE_UNIQUE_COLORS = 400
MIN_PALETTE_DELTA = 15.0
MIN_CYCLE_DELTA = 5.0
PALETTE_BOUND_MARKER = "BG_IMGPRO: Palette bound: size="
PALETTE_PROCESS_MARKER = "BG_IMGPRO: Processing done"
PALETTE_CLI_MARKER = "BG_PALETTE: mode=7 enabled=1 (cli)"


def frame_stats(pixels):
    """Mean saturation + unique 5-bit-quantized colors of a frame."""
    maxima = pixels.max(axis=2)
    minima = pixels.min(axis=2)
    saturation = float(np.mean(np.where(maxima > 0.0,
        (maxima - minima) / np.maximum(maxima, 1.0), 0.0)))
    unique = int(len(np.unique((pixels.astype(np.uint8) // 8).reshape(-1, 3),
        axis=0)))
    return saturation, unique


def capture(temp_root, tag, extra_env, args=(), frames=FRAMES, shot_frame=20,
            fixed_dt="0.0166666667", scene=SCENE):
    directory = temp_root / tag
    directory.mkdir(exist_ok=True)
    environment = audit.scene_env({
        "GX_SKIP_TESTS": "1",
        "GX_FIXED_DT": fixed_dt,
        "GX_FRAMES": str(frames),
    }, clean=True)
    environment.update(extra_env)
    with audit.settings_preserved():
        result = audit.run_scene(scene, environment, width=None, height=None,
                                 fps=None, frames=frames, shot_frame=shot_frame,
                                 screenshot_dir=directory, timeout=180,
                                 check=True, extra_args=args)
    log = audit.strip_ansi(result.stdout or "")
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError(f"{tag}: capture missing")
    pixels = np.asarray(Image.open(shot).convert("RGB"), dtype=np.float32)
    return pixels, log


def write_palette_source(temp_root):
    """A saturated 4-quadrant + gradient image for the decomposer."""
    path = temp_root / "palette_source.png"
    image = Image.new("RGB", (96, 96))
    draw = ImageDraw.Draw(image)
    draw.rectangle([0, 0, 47, 47], fill=(230, 30, 40))
    draw.rectangle([48, 0, 95, 47], fill=(30, 200, 60))
    draw.rectangle([0, 48, 47, 95], fill=(40, 60, 220))
    draw.rectangle([48, 48, 95, 95], fill=(240, 220, 40))
    for y in range(96):
        draw.line([(0, y), (95, y)],
                  fill=(int(255 * y / 95), 40, int(255 - 200 * y / 95)))
    image.save(path)
    return path


def check_polish(temp_root):
    base, _ = capture(temp_root, "bg_base", {})
    if float(base.mean()) < BASE_MIN_MEAN or float(base.std()) < BASE_MIN_STD:
        raise RuntimeError(
            f"background scene is blank (mean {base.mean():.1f}, "
            f"std {base.std():.1f})")
    polished, log = capture(temp_root, "bg_scale",
                            {"GX_POLISHER": "gX_BG_SCALE_DEFVAL=0.6"})
    marker = APPLY_RE.search(log)
    if not marker or marker.group(1) != "gX_BG_SCALE_DEFVAL":
        raise RuntimeError("GX_POLISHER was not applied to the background")
    delta = float(np.abs(base - polished).mean())
    if delta < MIN_POLISH_DELTA:
        raise RuntimeError(
            f"polished default did not change the backdrop (delta {delta:.2f})")
    same, _ = capture(temp_root, "bg_default",
                      {"GX_POLISHER": "gX_BG_SCALE_DEFVAL=1.0"})
    default_delta = float(np.abs(base - same).mean())
    if default_delta > DEFAULT_DELTA_MAX:
        raise RuntimeError(
            f"explicit default is not identical (delta {default_delta:.4f})")
    return base, delta, default_delta


def check_palette(temp_root, base):
    """Image decomposer -> palette modes 2 (imgpro default) and 7 (cycling)."""
    source = write_palette_source(temp_root)
    args = ("--bg-imgpro", str(source))

    pixels, log = capture(temp_root, "bg_imgpro", {}, args=args)
    if PALETTE_PROCESS_MARKER not in log:
        raise RuntimeError("imgpro did not process the source image")
    if PALETTE_BOUND_MARKER not in log or "enabled=1" not in log:
        raise RuntimeError("imgpro did not bind and enable the palette")
    saturation, unique = frame_stats(pixels)
    if saturation < MIN_PALETTE_SATURATION:
        raise RuntimeError(
            f"decomposed backdrop is not colorized (sat {saturation:.3f})")
    if unique > MAX_PALETTE_UNIQUE_COLORS:
        raise RuntimeError(
            f"decomposed backdrop is not quantized (unique colors {unique})")
    delta = float(np.abs(base - pixels).mean())
    if delta < MIN_PALETTE_DELTA:
        raise RuntimeError(
            f"decomposed backdrop did not change the frame (delta {delta:.2f})")

    cycle_args = args + ("--bg-palette-mode", "7")
    first, cycle_log = capture(temp_root, "bg_mode7_a", {}, args=cycle_args,
                               frames=160, shot_frame=20)
    second, _ = capture(temp_root, "bg_mode7_b", {}, args=cycle_args,
                        frames=160, shot_frame=140)
    if PALETTE_CLI_MARKER not in cycle_log:
        raise RuntimeError("--bg-palette-mode did not enable mode 7")
    cycle_saturation, cycle_unique = frame_stats(first)
    if cycle_saturation < MIN_PALETTE_SATURATION:
        raise RuntimeError(
            f"mode 7 is not colorized (sat {cycle_saturation:.3f})")
    if cycle_unique > MAX_PALETTE_UNIQUE_COLORS:
        raise RuntimeError(
            f"mode 7 is not quantized (unique colors {cycle_unique})")
    cycle_delta = float(np.abs(first - second).mean())
    if cycle_delta < MIN_CYCLE_DELTA:
        raise RuntimeError(
            f"mode 7 row cycling does not animate (delta {cycle_delta:.2f})")
    return saturation, unique, delta, cycle_saturation, cycle_delta


def check_panel(temp_root):
    directory = temp_root / "panel"
    directory.mkdir(exist_ok=True)
    environment = audit.scene_env({
        "GX_SKIP_TESTS": "1",
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "20",
    }, clean=True)
    with audit.settings_preserved():
        result = audit.run_scene("--gfx-editor", environment, width=None,
                                 height=None, fps=None, frames=20,
                                 shot_frame=15, screenshot_dir=directory,
                                 timeout=180, check=True,
                                 extra_args=("--background",))
    log = audit.strip_ansi(result.stdout or "")
    if UI_MARKER not in log:
        raise RuntimeError("polisher panel did not draw")
    if BG_UI_MARKER not in log:
        raise RuntimeError("background panel did not draw")


def check_title(temp_root):
    """The title scene carries the TITLE preset: saturated, palette-live and
    animated behind the GUI."""
    base, log = capture(temp_root, "title_base", {}, args=(), scene="--title")
    if TITLE_PRESET_MARKER not in log:
        raise RuntimeError("title scene did not load the TITLE preset")
    saturation, _ = frame_stats(base)
    if saturation < MIN_TITLE_SATURATION:
        raise RuntimeError(
            f"title backdrop is not colorful (sat {saturation:.3f})")
    alt, _ = capture(temp_root, "title_mode1", {},
                     args=("--bg-palette-mode", "1"), scene="--title")
    mode_delta = float(np.abs(base - alt).mean())
    if mode_delta < MIN_TITLE_MODE_DELTA:
        raise RuntimeError(
            f"title palette override did not change the backdrop "
            f"(delta {mode_delta:.2f})")
    later, _ = capture(temp_root, "title_later", {}, args=(), scene="--title",
                       frames=160, shot_frame=140)
    anim_delta = float(np.abs(base - later).mean())
    if anim_delta < MIN_TITLE_ANIM_DELTA:
        raise RuntimeError(
            f"title backdrop does not animate (delta {anim_delta:.2f})")
    return saturation, mode_delta, anim_delta


def check_unit_tests():
    result = subprocess.run([str(audit.BIN), "--test-only"],
                            cwd=str(audit.ROOT), text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=300)
    log = audit.strip_ansi(result.stdout or "")
    if "POLISHER_TESTS: all passed" not in log:
        raise RuntimeError("polisher unit tests did not pass")


def main():
    if not audit.BIN.is_file():
        print("GFX_BACKGROUND_CHECK: missing binary", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_bg_",
                                         dir=str(audit.SCRATCH.parent)) as temp:
            temp_root = Path(temp)
            base, delta, default_delta = check_polish(temp_root)
            saturation, unique, palette_delta, cycle_sat, cycle_delta = \
                check_palette(temp_root, base)
            check_panel(temp_root)
            title_sat, title_mode_delta, title_anim_delta = check_title(temp_root)
            check_unit_tests()
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_BACKGROUND_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"GFX_BACKGROUND_CHECK: PASS polished scale delta {delta:.1f} "
          f"(explicit default {default_delta:.4f}); palette sat {saturation:.2f} "
          f"unique {unique} delta {palette_delta:.1f}; mode7 sat "
          f"{cycle_sat:.2f} cycle delta {cycle_delta:.1f}; title sat "
          f"{title_sat:.2f} mode delta {title_mode_delta:.1f} anim delta "
          f"{title_anim_delta:.1f}; panels + tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())
