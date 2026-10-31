#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: public thin-axis SSR regression. A bright 0.05 x 1.2 x 0.05 pole
#   stands above a mirror floor with planar reflections off and SSR on; the
#   included/excluded pair must change the mirror region. This is the public
#   proxy for the private randomshit.json axis report.

import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
SOURCE_PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"
MIRROR_CHANGED_MIN = 0.01
MIRROR_GREEN_MIN = 1000


def make_fixture(path, include_source):
    fixture = json.loads(SOURCE_PRESET.read_text())
    plane = copy.deepcopy(fixture["models"][1])
    plane["params"].update({
        "pos": [0.0, 0.0, 0.0],
        "scale": [8.0, 0.1, 8.0],
        "rotation": [0.0, 0.0, 0.0],
        "usePBR": True,
        "metalness": 1.0,
        "roughness": 0.02,
        "albedo": 1.0,
        "albedoTint": [255, 255, 255, 255],
        "hasAlbedoMap": False,
        "hasMetalnessMap": False,
        "hasNormalMap": False,
        "hasHeightMap": False,
        "hasEmissiveMap": False,
        "emissiveFactor": 0.0,
        "enableReflections": False,
        "planarReflection": False,
        "receiveSSR": True,
        "reflectionSource": False,
        "nonPlanarReflection": False,
        "waveAmplitude": 0.0,
    })
    models = [plane]
    if include_source:
        source = copy.deepcopy(fixture["models"][1])
        source["params"].update({
            "pos": [0.0, 1.2, 0.0],
            "scale": [0.05, 1.2, 0.05],
            "rotation": [0.0, 20.0, 0.0],
            "usePBR": False,
            "metalness": 0.0,
            "roughness": 0.5,
            "albedo": 1.0,
            "albedoTint": [255, 255, 255, 255],
            "hasAlbedoMap": False,
            "hasMetalnessMap": False,
            "hasNormalMap": False,
            "hasHeightMap": False,
            "hasEmissiveMap": False,
            "emissiveFactor": 3.0,
            "emissiveColor": [40, 255, 80, 255],
            "enableReflections": False,
            "planarReflection": False,
            "receiveSSR": False,
            "reflectionSource": False,
            "nonPlanarReflection": False,
            "waveAmplitude": 0.0,
        })
        models.append(source)
    fixture["models"] = models
    fixture["lights"] = []
    fixture["cameras"] = []
    fixture["camera"] = {
        "position": [4.0, 4.0, 6.0],
        "target": [0.0, 0.2, 0.0],
        "up": [0.0, 1.0, 0.0],
        "fovy": 45.0,
        "projection": 0,
    }
    fixture["state"].update({
        "ENABLE_SSR": True,
        "ENABLE_PLANAR_REFLECTIONS": False,
        "ENABLE_REFLECTIONS": False,
        "ENABLE_SSR_AUXVIEW": False,
        "ENABLE_SSAO": False,
        "ENABLE_BLOOM": False,
        "ENABLE_FXAA": False,
        "ENABLE_TAA": False,
        "ENABLE_SKYBOX": False,
        "ENABLE_PROCEDURAL_SKY": False,
        "ENABLE_SHADOWS": False,
        "ENABLE_POINT_SHADOWS": False,
        "ENABLE_COOKIES": False,
        "ENABLE_PARTICLES": False,
        "ENABLE_VOLUME_FOG": False,
        "ENABLE_FOG": False,
        "ENABLE_MAP_ALBEDO": True,
        "ENABLE_LIGHTING_AMBIENT": True,
        "ENABLE_LIGHTING_DIFFUSE": True,
        "ENABLE_LIGHTING_SPECULAR": True,
    })
    fixture["editor"] = {"usePP": False, "useCompositor": False}
    path.write_text(json.dumps(fixture, indent=1) + "\n")


def capture(temp_root, name, preset):
    directory = temp_root / name
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.016666667",
        "GX_GFX_PRESET_LOAD": str(preset),
    }, clean=True)
    result = audit.run_scene("--gfx", environment, width=1536, height=1016,
        fps=None, shot_frame=120, screenshot_dir=directory, timeout=120,
        check=False, extra_args=("--gfx-editor",))
    if result.returncode != 0:
        raise RuntimeError(f"{name}: renderer exited {result.returncode}\n"
                           f"{audit.strip_ansi(result.stdout or '')[-2000:]}")
    shots = list(directory.glob("gXss_*.png"))
    if not shots:
        raise RuntimeError(f"{name}: screenshot missing\n"
                           f"{audit.strip_ansi(result.stdout or '')[-2000:]}")
    shot = max(shots, key=lambda path: path.stat().st_mtime)
    return np.asarray(Image.open(shot).convert("RGB"), dtype=np.int16)


def mirror_region(image):
    height, width = image.shape[:2]
    return (slice(int(height * 0.495), int(height * 0.84)),
            slice(int(width * 0.47), int(width * 0.73)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if not audit.BIN.is_file():
        print(f"SSR_THIN: binary missing: {audit.BIN}", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="ssr_thin_",
                                         dir="/tmp/opencode") as temporary:
            root = Path(temporary)
            included_preset = root / "included.json"
            excluded_preset = root / "excluded.json"
            make_fixture(included_preset, True)
            make_fixture(excluded_preset, False)
            with audit.settings_preserved():
                included = capture(root, "included", included_preset)
                excluded = capture(root, "excluded", excluded_preset)
            if included.shape != excluded.shape:
                raise RuntimeError(
                    f"capture size mismatch: {included.shape} vs {excluded.shape}")
            region = mirror_region(included)
            delta = np.abs(included[region].astype(np.int32)
                           - excluded[region].astype(np.int32))
            changed = float((delta.max(axis=2) > 10).mean())
            part = included[region]
            green = int(((part[:, :, 1] > 150)
                         & (part[:, :, 1] > part[:, :, 0] * 1.35)
                         & (part[:, :, 1] > part[:, :, 2] * 1.1)).sum())
            print(f"mirror changed={changed:.4f} green={green} "
                  f"delta_mean={delta.mean():.3f}")
            if changed < MIRROR_CHANGED_MIN or green < MIRROR_GREEN_MIN:
                raise RuntimeError(
                    "thin-source reflection missing from SSR mirror: "
                    f"changed={changed:.4f} (min {MIRROR_CHANGED_MIN}), "
                    f"green={green} (min {MIRROR_GREEN_MIN})")
            if args.report:
                args.report.write_text(json.dumps({
                    "changed": changed,
                    "green": green,
                    "delta_mean": float(delta.mean()),
                    "mirror_changed_min": MIRROR_CHANGED_MIN,
                    "mirror_green_min": MIRROR_GREEN_MIN,
                }, indent=2) + "\n")
        print("SSR_THIN: PASS — thin pole reflected by SSR")
        return 0
    except (OSError, ValueError, RuntimeError,
            subprocess.SubprocessError) as error:
        print(f"SSR_THIN: FAIL — {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
