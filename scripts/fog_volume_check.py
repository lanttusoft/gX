#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: GFX fog-volume visibility/tint gate. Pins the default density,
#   in-scatter color, and scene-fog matching against the shipped editor preset.

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
SOURCE_PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"


def make_fixture(path, *, density, color, match_scene, scene_fog=(255, 255, 255, 255)):
    fixture = json.loads(SOURCE_PRESET.read_text())
    fixture["state"].update({
        "ENABLE_VOLUME_FOG": True,
        "ENABLE_SSR": False,
        "ENABLE_SSAO": False,
        "ENABLE_BLOOM": False,
        "ENABLE_TAA": False,
        "ENABLE_FXAA": False,
        "ENABLE_GODRAYS": False,
        "ENABLE_PARTICLES": False,
        "ENABLE_LENS_FLARE": False,
        "ENABLE_SHADOWS": True,
    })
    fixture["fogVolume"].update({
        "density": density,
        "heightFalloff": 1.2,
        "scattering": 1.0,
        "hgAsymmetry": 0.85,
        "color": list(color),
        "matchSceneFog": match_scene,
    })
    fixture["fog"]["color"] = list(scene_fog)
    path.write_text(json.dumps(fixture, indent=2) + "\n")


def capture(temp_root, name, preset):
    directory = temp_root / name
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_GFX_PRESET_LOAD": str(preset),
    }, clean=True)
    with audit.settings_preserved():
        result = audit.run_scene(
            "--gfx", environment, width=1024, height=768, fps=None,
            shot_frame=120, screenshot_dir=directory, timeout=120,
            extra_args=("--gfx-editor",))
    if result.returncode != 0:
        raise RuntimeError(f"{name}: process failed ({result.returncode})\n"
                           f"{result.stdout[-3000:]}")
    screenshot = audit.newest_shot(directory)
    if screenshot is None:
        raise RuntimeError(f"{name}: screenshot missing\n{result.stdout[-3000:]}")
    return audit.load_rgb(screenshot)


def channel_means(image):
    return tuple(float(image[:, :, channel].mean()) for channel in range(3))


def main():
    if not BIN.is_file():
        print(f"FOG_VOLUME_CHECK: binary missing: {BIN}", file=sys.stderr)
        return 1
    try:
        os.makedirs("/tmp/opencode", exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="fog_volume_",
                                         dir="/tmp/opencode") as temporary:
            root = Path(temporary)
            off = root / "off.json"
            on = root / "on.json"
            red = root / "red.json"
            blue = root / "blue.json"
            match_red = root / "match_red.json"
            no_match_blue = root / "no_match_blue.json"
            make_fixture(off, density=0.0, color=(255, 255, 255, 255),
                         match_scene=False)
            make_fixture(on, density=0.003, color=(255, 255, 255, 255),
                         match_scene=False)
            make_fixture(red, density=0.02, color=(255, 64, 64, 255),
                         match_scene=False)
            make_fixture(blue, density=0.02, color=(64, 64, 255, 255),
                         match_scene=False)
            make_fixture(match_red, density=0.02, color=(64, 64, 255, 255),
                         match_scene=True, scene_fog=(255, 0, 0, 255))
            make_fixture(no_match_blue, density=0.02, color=(64, 64, 255, 255),
                         match_scene=False, scene_fog=(255, 0, 0, 255))

            off_image = capture(root, "off", off)
            on_image = capture(root, "on", on)
            red_image = capture(root, "red", red)
            blue_image = capture(root, "blue", blue)
            match_image = capture(root, "match_red", match_red)
            no_match_image = capture(root, "no_match_blue", no_match_blue)

            visibility = audit.mean_abs_diff(off_image, on_image)
            visible_fraction = audit.diff_frac(off_image, on_image, cut=4.0)
            if visibility < 0.5 or visible_fraction < 0.02:
                raise RuntimeError(
                    "default fog volume is not visible: "
                    f"mean={visibility:.3f}, changed={visible_fraction:.2%}")

            tint_delta = audit.mean_abs_diff(red_image, blue_image)
            red_arm = channel_means(red_image)
            blue_arm = channel_means(blue_image)
            if tint_delta < 0.2:
                raise RuntimeError(
                    f"fog color does not move pixels: mean={tint_delta:.3f}")
            if not (red_arm[0] > blue_arm[0] and blue_arm[2] > red_arm[2]):
                raise RuntimeError(
                    "fog tint direction wrong: "
                    f"red arm={red_arm}, blue arm={blue_arm}")

            match_arm = channel_means(match_image)
            no_match_arm = channel_means(no_match_image)
            if not (match_arm[0] > no_match_arm[0]
                    and match_arm[2] < no_match_arm[2]):
                raise RuntimeError(
                    "scene-fog matching did not override the volume tint: "
                    f"match={match_arm}, no_match={no_match_arm}")

            print("FOG_VOLUME_CHECK: PASS")
            print(f"  default visibility: mean={visibility:.3f}, "
                  f"changed={visible_fraction:.2%}")
            print(f"  tint delta: mean={tint_delta:.3f}")
            print(f"  match scene fog: match={match_arm}, no_match={no_match_arm}")
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"FOG_VOLUME_CHECK: FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
