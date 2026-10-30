#!/usr/bin/env python3
#opencode: virtual-input regression for stale planar-reflection alignment.

import copy
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN

#opencode: absolute geometry in this gate's assertions — keep the engine size.
audit.keep_engine_size()
ALIGN_RE = re.compile(
    r"GfxEd: planar plane aligned model=(\d+) offset=([-\d.]+)->([-\d.]+)"
)
PLANE_RE = re.compile(
    r"ORACLEDUMP .* plane=([-\d.]+) n=\(([-\d.]+),([-\d.]+),([-\d.]+)\)"
)


#opencode: create an intentionally stale mirror plane in a temporary preset.
def make_fixture(path, follows_model=False):
    fixture = json.loads((ROOT / "resources/tools/gfx_editor.json").read_text())
    model = copy.deepcopy(fixture["models"][1])
    if follows_model:
        model["params"].update({
            "pos": [0.0, 0.0, 0.4],
            "scale": [0.1, 2.0, 2.0],
            "rotation": [0.0, 90.0, 0.0],
            "planarReflection": True,
            "nonPlanarReflection": False,
            "reflectionNormal": [1.0, 0.0, 0.0],
            "reflectionPlaneHeight": 0.0,
            "reflectionPlaneFollowsModel": True,
        })
        fixture["camera"] = {
            "position": [4.0, 2.0, 6.0],
            "target": [0.0, 0.0, 0.4],
            "up": [0.0, 1.0, 0.0],
            "fovy": 45.0,
            "projection": 0,
        }
        fixture["state"].update({
            "ENABLE_REFLECTIONS": False,
            "ENABLE_PLANAR_REFLECTIONS": True,
        })
    else:
        model["params"].update({
            "pos": [0.0, 0.5, 0.0],
            "scale": [1.0, 0.1, 1.0],
            "rotation": [0.0, 0.0, 0.0],
            "planarReflection": True,
            "nonPlanarReflection": False,
            "reflectionNormal": [1.0, 0.0, 0.0],
            "reflectionPlaneHeight": 6.0,
        })
        fixture["camera"] = {
            "position": [0.0, 5.0, 8.0],
            "target": [0.0, 0.5, 0.0],
            "up": [0.0, 1.0, 0.0],
            "fovy": 45.0,
            "projection": 0,
        }
        fixture["state"].update({
            "ENABLE_REFLECTIONS": True,
            "ENABLE_PLANAR_REFLECTIONS": True,
        })
    fixture["models"] = [model]
    fixture["lights"] = []
    fixture["state"].update({
        "ENABLE_SSR": False,
        "ENABLE_SSR_AUXVIEW": False,
        "ENABLE_SSAO": False,
        "ENABLE_BLOOM": False,
        "ENABLE_FXAA": False,
        "ENABLE_TAA": False,
        "ENABLE_SKYBOX": False,
        "ENABLE_PROCEDURAL_SKY": False,
        "ENABLE_COOKIES": False,
        "ENABLE_PARTICLES": False,
    })
    path.write_text(json.dumps(fixture, indent=2) + "\n")


#opencode: drive selection and alignment only through GX_AI_INPUT, retaining
#   an image before the action and the actual UI action's trace afterward.
def run_capture(preset, shot_dir, frame, input_script):
    shot_dir.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_AI_INPUT": input_script,
        "GX_FIXED_DT": "0.0166666667",
        "GX_GFX_PRESET_LOAD": str(preset),
    }, clean=True)
    try:
        result = audit.run_scene(
            "--gfx", environment, width=None, height=None, fps=None,
            shot_frame=frame, screenshot_dir=shot_dir, timeout=120,
            check=False, extra_args=("--gfx-editor",))
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        raise RuntimeError(f"frame {frame} timed out\n{output[-2000:]}") from error
    if result.returncode != 0:
        raise RuntimeError(f"frame {frame} failed ({result.returncode})\n{result.stdout[-2000:]}")
    screenshots = list(shot_dir.glob("gXss_*.png"))
    if not screenshots:
        raise RuntimeError(f"frame {frame} did not produce a screenshot\n{result.stdout[-2000:]}")
    screenshot = max(screenshots, key=lambda path: path.stat().st_mtime)
    return result.stdout, screenshot


def warning_pixel_count(image_path):
    image = np.asarray(Image.open(image_path).convert("RGB"), dtype=np.uint8)
    height, width = image.shape[:2]
    region = image[int(height * 0.27):int(height * 0.41),
                   int(width * 0.77):int(width * 0.995)]
    red, green, blue = region.astype(np.int16).transpose(2, 0, 1)
    warning = ((red > 170) & (green > 65) & (green < 220)
               & (blue < 145) & (red > green * 1.15))
    return int(warning.sum())


def main():
    if not BIN.is_file():
        print(f"GFX_MIRROR_ALIGNMENT_CHECK: binary missing: {BIN}", file=sys.stderr)
        return 1
    os.makedirs("/tmp/opencode", exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_mirror_align_",
                                         dir="/tmp/opencode") as temporary:
            root = Path(temporary)
            preset = root / "fixture.json"
            make_fixture(preset)
            _, before_image = run_capture(
                preset, root / "before", 180, "1.5 click 100 212")
            warning_pixels = warning_pixel_count(before_image)
            if warning_pixels < 50:
                raise RuntimeError(
                    f"stale-plane warning not visible ({warning_pixels} orange pixels)")

            width = Image.open(before_image).width
            align_x = width - 300
            #opencode: the align button sits under the stale-plane warning; its
            #   row was re-measured 2026-09-26 (379 hit the top edge, 386 center).
            align_y = 386
            input_script = f"1.5 click 100 212;2.5 click {align_x} {align_y}"
            action_log, after_image = run_capture(
                preset, root / "after", 240, input_script)
            matches = ALIGN_RE.findall(action_log)
            if len(matches) != 1:
                raise RuntimeError(f"expected one align action log, got {matches}")
            model_index, before, after = matches[0]
            before_offset = float(before)
            after_offset = float(after)
            if (model_index != "0" or before_offset <= 0.25
                    or after_offset >= 0.01):
                raise RuntimeError(
                    f"alignment mismatch model={model_index} "
                    f"offset={before_offset:.3f}->{after_offset:.3f}")
            after_warning_pixels = warning_pixel_count(after_image)
            if after_warning_pixels >= 50:
                raise RuntimeError(
                    f"stale-plane warning remained visible after alignment "
                    f"({after_warning_pixels} orange pixels)")
            follow_preset = root / "follow_fixture.json"
            make_fixture(follow_preset, True)
            follow_log, _ = run_capture(
                follow_preset, root / "follow", 30, "")
            plane_matches = PLANE_RE.findall(follow_log)
            if not plane_matches:
                raise RuntimeError("model-follow plane trace missing")
            follow_distance, nx, ny, nz = map(float, plane_matches[-1])
            if (abs(follow_distance - 0.4) > 0.01 or abs(nx) > 0.01
                    or abs(ny) > 0.01 or abs(nz - 1.0) > 0.01):
                raise RuntimeError(
                    "model-follow plane transform mismatch: "
                    f"d={follow_distance:.3f}, n=({nx:.3f},{ny:.3f},{nz:.3f})")
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
        print(f"GFX_MIRROR_ALIGNMENT_CHECK: FAIL: {error}", file=sys.stderr)
        return 1
    print(f"GFX_MIRROR_ALIGNMENT_CHECK: PASS warning_pixels={warning_pixels} "
          f"cleared={after_warning_pixels} "
          f"offset={before_offset:.3f}->{after_offset:.3f} "
          f"follow_d={follow_distance:.3f} "
          f"follow_n=({nx:.2f},{ny:.2f},{nz:.2f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
