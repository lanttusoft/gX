#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: reflection lab gate — a metal sphere above a planar checker floor exercises
#   the planar strength ladder and behind-plane culling with a visible marker.

import copy
import json
import sys
import tempfile
from pathlib import Path

import audit_common as audit


ROOT = audit.ROOT
PRESET = ROOT / "resources" / "tools" / "gfx_reflection_lab.json"


def variant(source, strength, marker=False):
    document = copy.deepcopy(source)
    floor = document["models"][5]["params"]
    floor["planarReflection"] = True
    floor["planarReflectionStrength"] = strength
    floor["reflectionPlaneHeight"] = 0.0
    floor["reflectionNormal"] = [0.0, 1.0, 0.0]
    if marker:
        cube = copy.deepcopy(document["models"][0])
        cube["path"] = "resources/model/primitive/cube.glb"
        cube["params"].update({
            "pos": [0.0, -1.4, 0.0],
            "scale": [0.5, 0.5, 0.5],
            "rotation": [0.0, 0.0, 0.0],
            "metalness": 0.0,
            "roughness": 1.0,
            "emissiveFactor": 8.0,
            "emissiveColor": [255, 0, 0, 255],
            "enableReflections": False,
            "planarReflection": False,
            "receiveSSR": False,
        })
        document["models"].append(cube)
    return document


def capture(document, root, tag):
    preset = root / f"{tag}.json"
    preset.write_text(json.dumps(document, indent=2) + "\n")
    directory = root / tag
    directory.mkdir(exist_ok=True)
    environment = audit.scene_env({
        "GX_GFX_PRESET_LOAD": str(preset),
        "GX_FIXED_DT": "0.0166666667",
    }, clean=True)
    result = audit.run_scene(
        "--gfx", environment, width=1024, height=768, frames=40,
        shot_frame=30, screenshot_dir=directory, timeout=180, check=True,
        extra_args=("--gfx-editor",))
    log = audit.strip_ansi(result.stdout or "")
    if "Load done" not in log:
        raise RuntimeError(f"{tag}: preset did not load")
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError(f"{tag}: capture missing")
    return audit.load_rgb(shot)


def main():
    if not PRESET.is_file():
        print(f"GFX_REFLECTION_LAB_CHECK: missing {PRESET}", file=sys.stderr)
        return 1
    source = json.loads(PRESET.read_text())
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_reflection_lab_",
                                         dir="/tmp/opencode") as temporary:
            root = Path(temporary)
            off = capture(variant(source, 0.0), root, "strength0")
            half = capture(variant(source, 0.5), root, "strength05")
            full = capture(variant(source, 1.0), root, "strength1")
            behind = capture(variant(source, 1.0, marker=True), root, "behind")
            d_off = audit.mean_abs_diff(off, full)
            d_half_off = audit.mean_abs_diff(half, off)
            d_half_full = audit.mean_abs_diff(half, full)
            d_behind = audit.mean_abs_diff(full, behind)
            if d_off < 1.0:
                raise RuntimeError(f"planar strength has no visible effect ({d_off:.2f})")
            if d_half_off < 0.25 or d_half_full < 0.25:
                raise RuntimeError(
                    f"strength 0.5 is not between off and full "
                    f"({d_half_off:.2f}/{d_half_full:.2f})")
            if d_behind > 0.75:
                raise RuntimeError(
                    f"behind-plane marker leaked into the mirror ({d_behind:.2f})")
            print("GFX_REFLECTION_LAB_CHECK: PASS "
                  f"strength off/full={d_off:.2f} half={d_half_off:.2f}/"
                  f"{d_half_full:.2f} behind={d_behind:.2f}")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_REFLECTION_LAB_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
