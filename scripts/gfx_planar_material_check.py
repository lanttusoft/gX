#!/usr/bin/env python3
#opencode: render a textured emitter into the planar capture and compare it with
#   the same object excluded from capture; this pins raw-path material bindings.

import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
SOURCE_PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"


#opencode: use the standard material map asset and a fixed horizontal mirror fixture.
def make_fixture(path, exclude_source):
    fixture = json.loads(SOURCE_PRESET.read_text())
    fixture["models"] = [copy.deepcopy(fixture["models"][1]),
                         copy.deepcopy(fixture["models"][1])]
    plane, source = fixture["models"]
    plane["params"].update({
        "pos": [0.0, 0.0, 0.0],
        "scale": [8.0, 0.1, 8.0],
        "rotation": [0.0, 0.0, 0.0],
        "planarReflection": True,
        "enableReflections": False,
        "receiveSSR": False,
        "reflectionNormal": [0.0, 1.0, 0.0],
        "reflectionPlaneHeight": 0.05,
        "albedo": 1.0,
        "albedoTint": [255, 255, 255, 255],
        "usePBR": True,
        "metalness": 1.0,
        "roughness": 0.02,
    })
    source["params"].update({
        "pos": [0.0, 1.2, 0.0],
        "scale": [0.8, 0.8, 0.8],
        "rotation": [0.0, 25.0, 0.0],
        "planarReflection": False,
        "enableReflections": False,
        "receiveSSR": False,
        "albedo": 1.0,
        "albedoTint": [255, 255, 255, 255],
        "usePBR": False,
        "emissiveFactor": 2.0,
        "emissiveColor": [255, 255, 255, 255],
        "excludeFromPlanarCapture": exclude_source,
        "materialOverrides": [{
            "material": "Material",
            "emissiveColor": [255, 255, 255, 255],
            "emissive": 2.0,
            "albedo": [255, 255, 255, 255],
            "roughness": 0.8,
            "metalness": 0.0,
            "albedoMap": "misc/testcard.png",
            "emissiveMap": "misc/testcard.png",
        }],
    })
    fixture["lights"] = []
    fixture["camera"] = {
        "position": [4.0, 4.0, 6.0],
        "target": [0.0, 0.2, 0.0],
        "up": [0.0, 1.0, 0.0],
        "fovy": 45.0,
        "projection": 0,
    }
    fixture["state"].update({
        "ENABLE_REFLECTIONS": False,
        "ENABLE_PLANAR_REFLECTIONS": True,
        "ENABLE_SSR": False,
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
    })
    path.write_text(json.dumps(fixture, indent=2) + "\n")


#opencode: capture one fixture with the editor's supported deterministic screenshot path.
def capture(temp_root, name, preset):
    directory = temp_root / name
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_GFX_PRESET_LOAD": str(preset),
    }, clean=True)
    try:
        with audit.settings_preserved():
            result = audit.run_scene(
                "--gfx", environment, width=1536, height=1016, fps=None,
                shot_frame=300, screenshot_dir=directory, timeout=120,
                check=False, extra_args=("--gfx-editor",))
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        raise RuntimeError(f"{name}: timed out\n{output[-3000:]}") from error
    if result.returncode != 0:
        raise RuntimeError(f"{name}: process failed ({result.returncode})\n{result.stdout[-3000:]}")
    if "ORACLEDUMP f=" not in result.stdout:
        raise RuntimeError(f"{name}: planar capture did not run\n{result.stdout[-3000:]}")
    screenshots = list(directory.glob("gXss_*.png"))
    if not screenshots:
        raise RuntimeError(f"{name}: screenshot missing\n{result.stdout[-3000:]}")
    screenshot = max(screenshots, key=lambda path: path.stat().st_mtime)
    return np.asarray(Image.open(screenshot).convert("RGB"), dtype=np.int16)


#opencode: confirm the capture contains visible per-material texture color that vanishes when excluded.
def main():
    if not BIN.is_file():
        print(f"GFX_PLANAR_MATERIAL_CHECK: binary missing: {BIN}", file=sys.stderr)
        return 1
    try:
        os.makedirs("/tmp/opencode", exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="gfx_planar_material_",
                                         dir="/tmp/opencode") as temporary:
            temp_root = Path(temporary)
            included_preset = temp_root / "included.json"
            excluded_preset = temp_root / "excluded.json"
            make_fixture(included_preset, False)
            make_fixture(excluded_preset, True)
            included = capture(temp_root, "included", included_preset)
            excluded = capture(temp_root, "excluded", excluded_preset)
            if (included.ndim != 3 or included.shape[2] != 3
                    or excluded.shape != included.shape):
                raise RuntimeError(
                    f"unexpected screenshot sizes: {included.shape} vs {excluded.shape}")
            height, width = included.shape[:2]
            region = (slice(int(height * 0.495), int(height * 0.84)),
                      slice(int(width * 0.47), int(width * 0.73)))
            included_region = included[region]
            excluded_region = excluded[region]
            green = included_region[:, :, 1]
            red = included_region[:, :, 0]
            blue = included_region[:, :, 2]
            included_green = int(((green > 150) & (green > red * 1.35)
                                  & (green > blue * 1.1)).sum())
            excluded_green = int(((excluded_region[:, :, 1] > 150)
                                  & (excluded_region[:, :, 1]
                                     > excluded_region[:, :, 0] * 1.35)
                                  & (excluded_region[:, :, 1]
                                     > excluded_region[:, :, 2] * 1.1)).sum())
            delta = np.abs(included_region - excluded_region)
            changed = float((delta > 10).any(axis=2).mean())
            if included_green < 5000 or included_green < excluded_green * 8:
                raise RuntimeError(
                    "textured planar capture color missing: "
                    f"green pixels={included_green}, excluded={excluded_green}")
            if float(delta.mean()) < 5.0 or changed < 0.05:
                raise RuntimeError(
                    f"excluding the source did not remove its reflection: "
                    f"mean={delta.mean():.3f}, changed={changed:.2%}")
            print("GFX_PLANAR_MATERIAL_CHECK: textured material preserved in planar capture")
            print(f"  green pixels: {included_green} included, {excluded_green} excluded")
            print(f"  capture difference: mean={delta.mean():.3f}, changed={changed:.2%}")
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"GFX_PLANAR_MATERIAL_CHECK: FAIL: {error}", file=sys.stderr)
        return 1
    print("GFX_PLANAR_MATERIAL_CHECK: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
