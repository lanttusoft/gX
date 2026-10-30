#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: Frutiger Aero showcase gate. Asserts the authored preset carries
#   the Aero contract (bright stylization stack on, temporal effects off for
#   reproducibility, chrome + glass + emissive materials, cool tints, a hero
#   camera) and that the scene renders bright with a glow signature.

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
PRESET = ROOT / "resources" / "tools" / "aero.json"
CAPTURE_MEAN_MIN = 80.0
GLOW_FRACTION_MIN = 0.01
THEME_RE = re.compile(
    r"GX_UI_THEME: applied preset=(\d+) frameRound=([\d.]+) "
    r"windowBg=\(([\d.]+),([\d.]+),([\d.]+),([\d.]+)\)")
AERO_PRESET = 3
AERO_FRAME_ROUND = 5.0   # the Aero branch sets FrameRounding 5 / WindowRounding 7
UI_PANEL_MARKER = "GFX_AERO_UI: panel="
MENU_BAR_GRADIENT_MIN = 15.0


def check_contract(document):
    state = document["state"]
    issues = []
    for flag in ("ENABLE_BLOOM", "ENABLE_SSR", "ENABLE_REFLECTIONS", "ENABLE_RIM",
                 "ENABLE_PROCEDURAL_SKY", "ENABLE_SSAO", "ENABLE_SHADOWS"):
        if not state.get(flag):
            issues.append(f"state.{flag} is off")
    for flag in ("ENABLE_TAA", "ENABLE_MOTION_BLUR"):
        if state.get(flag):
            issues.append(f"state.{flag} must stay off for deterministic captures")
    models = document.get("models", [])
    chrome = [m for m in models if float(m["params"].get("metalness", 0.0)) >= 0.9
              and float(m["params"].get("roughness", 1.0)) <= 0.15]
    glass = [m for m in models if float(m["params"].get("metalness", 1.0)) <= 0.25
             and float(m["params"].get("roughness", 1.0)) <= 0.06]
    glow = [m for m in models if float(m["params"].get("emissiveFactor", 0.0)) > 0.5]
    if not chrome:
        issues.append("no chrome material (metalness >= 0.9, roughness <= 0.15)")
    if not glass:
        issues.append("no glass material (metalness <= 0.25, roughness <= 0.06)")
    if not glow:
        issues.append("no emissive accent (emissiveFactor > 0.5)")
    cool = 0
    tinted = 0
    for model in models:
        tint = model["params"].get("albedoTint")
        if not tint:
            continue
        tinted += 1
        if int(tint[2]) >= int(tint[0]):
            cool += 1
    if tinted and cool * 2 < tinted:
        issues.append(f"tints not cool-toned ({cool}/{tinted})")
    cameras = document.get("cameras", [])
    if not cameras:
        issues.append("no cameras (hero framing expected)")
    return issues


def check_theme(temp_root):
    """Run the editor with the Aero preset requested; parse the applied trace,
       assert the gradient helpers ran, and measure the menu bar gradient."""
    directory = temp_root / "ui"
    directory.mkdir(exist_ok=True)
    with audit.settings_preserved():
        environment = audit.scene_env({
            "GX_FIXED_DT": "0.0166666667",
            "GX_FRAMES": "30",
            "GX_GFX_PRESET_LOAD": str(PRESET),
            "GX_GFX_UI_THEME": str(AERO_PRESET),
        }, clean=True)
        result = audit.run_scene("--gfx", environment, width=None,
                                 height=None, fps=None, shot_frame=20,
                                 screenshot_dir=directory, timeout=120,
                                 check=True, extra_args=("--gfx-editor",))
    log = audit.strip_ansi(result.stdout or "")
    rows = THEME_RE.findall(log)
    if not rows:
        raise RuntimeError("theme applied trace missing")
    preset, frame_round = int(rows[-1][0]), float(rows[-1][1])
    window_bg = tuple(float(value) for value in rows[-1][2:])
    if preset != AERO_PRESET:
        raise RuntimeError(f"theme preset {preset} != {AERO_PRESET}")
    if abs(frame_round - AERO_FRAME_ROUND) > 0.2:
        raise RuntimeError(f"frame rounding {frame_round} != "
                           f"{AERO_FRAME_ROUND} (Aero branch did not run)")
    if UI_PANEL_MARKER not in log:
        raise RuntimeError("Aero UI panel gradient trace missing")
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError("theme capture missing")
    pixels = np.asarray(Image.open(shot).convert("RGB"), dtype=np.float32)
    strip = pixels[3:17, :, :]
    third = strip.shape[1] // 3
    left = float(strip[:, :third].mean())
    right = float(strip[:, -third:].mean())
    if right - left < MENU_BAR_GRADIENT_MIN:
        raise RuntimeError(f"menu bar gradient weak "
                           f"(left {left:.1f}, right {right:.1f})")
    return window_bg, left, right


def capture_aero(temp_root, tag):
    directory = temp_root / tag
    directory.mkdir(exist_ok=True)
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "90",
        "GX_GFX_PRESET_LOAD": str(PRESET),
        "GX_DOCK_TRACE": "1",
    }, clean=True)
    result = audit.run_scene("--gfx", environment, width=None, height=None,
                             fps=None, shot_frame=80, screenshot_dir=directory,
                             timeout=120, check=True,
                             extra_args=("--gfx-editor",))
    shot = audit.newest_shot(directory)
    pixels = np.asarray(Image.open(shot).convert("RGB"), dtype=np.float32)
    log = audit.strip_ansi(result.stdout or "")
    x, y, w, h = audit.dock_viewport(log, pixels.shape[1], pixels.shape[0])
    return pixels[y:y + h, x:x + w]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--calibrate", action="store_true")
    arguments = parser.parse_args()
    if not BIN.is_file() or not PRESET.is_file():
        print("GFX_AERO_CHECK: missing binary or preset", file=sys.stderr)
        return 1
    try:
        document = json.loads(PRESET.read_text())
        issues = check_contract(document)
        if issues:
            raise RuntimeError("preset contract: " + "; ".join(issues))
        with tempfile.TemporaryDirectory(prefix="gfx_aero_",
                                         dir="/tmp/opencode") as temporary:
            with audit.settings_preserved():
                frame = capture_aero(Path(temporary), "shot")
            mean = float(frame.mean())
            glow = float((frame.max(axis=2) > 235.0).mean())
            if arguments.calibrate:
                print(f"  CAL aero: mean={mean:6.2f} glow_fraction={glow:5.4f}")
                return 0
            if mean < CAPTURE_MEAN_MIN:
                raise RuntimeError(f"frame too dark (mean {mean:.1f})")
            if glow < GLOW_FRACTION_MIN:
                raise RuntimeError(f"no glow signature (fraction {glow:.4f})")
            window_bg, bar_left, bar_right = check_theme(Path(temporary))
            if arguments.calibrate:
                print(f"  CAL aero theme: windowBg={window_bg} "
                      f"bar_left={bar_left:.1f} bar_right={bar_right:.1f}")
                return 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_AERO_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"GFX_AERO_CHECK: PASS aero contract holds, frame mean {mean:.1f}, "
          f"glow {glow:.3f}, theme preset {AERO_PRESET} applied, "
          f"menu bar gradient {bar_right - bar_left:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
