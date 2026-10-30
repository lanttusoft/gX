#!/usr/bin/env python3
#opencode: original-art regression for full-opacity transparent model routing.

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
PRESET = ROOT / "resources" / "tools" / "art.json"
SCREEN_WIDTH = 1536
SCREEN_HEIGHT = 1016
FRAME = 180


def capture(temp_root, name, blending, opacity):
    fixture = json.loads(PRESET.read_text())
    if len(fixture.get("models", [])) < 2:
        raise RuntimeError("original-art preset needs model slot 1 for blend coverage")
    fixture["models"][1]["params"].update({
        "enableBlending": blending,
        "blendOpacity": opacity,
    })
    preset = temp_root / f"{name}.json"
    preset.write_text(json.dumps(fixture, indent=2) + "\n")
    directory = temp_root / name
    directory.mkdir(parents=True, exist_ok=True)

    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_GFX_PRESET_LOAD": str(preset),
    }, clean=True)
    try:
        with audit.settings_preserved():
            result = audit.run_scene(
                "--gfx", environment, width=SCREEN_WIDTH, height=SCREEN_HEIGHT,
                fps=None, shot_frame=FRAME, screenshot_dir=directory,
                timeout=120, check=False, extra_args=("--gfx-editor",))
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        raise RuntimeError(f"{name}: timed out\n{output[-3000:]}") from error
    if result.returncode != 0:
        raise RuntimeError(
            f"{name}: process failed ({result.returncode})\n{result.stdout[-3000:]}"
        )
    screenshots = list(directory.glob("gXss_*.png"))
    if not screenshots:
        raise RuntimeError(f"{name}: screenshot missing\n{result.stdout[-3000:]}")
    screenshot = max(screenshots, key=lambda path: path.stat().st_mtime)
    return np.asarray(Image.open(screenshot).convert("RGB"), dtype=np.int16)


def difference(first, second):
    if first.shape != second.shape:
        raise RuntimeError(f"capture sizes differ: {first.shape} vs {second.shape}")
    height, width = first.shape[:2]
    region = (slice(int(height * 0.025), height),
              slice(int(width * 0.177), int(width * 0.79)))
    delta = np.abs(first[region] - second[region])
    return float(delta.mean()), float((delta > 3).any(axis=2).mean())


def main():
    if not BIN.is_file():
        print(f"GFX_ART_BLENDING_CHECK: binary missing: {BIN}", file=sys.stderr)
        return 1
    if not PRESET.is_file():
        print(f"GFX_ART_BLENDING_CHECK: original art preset missing: {PRESET}", file=sys.stderr)
        return 1
    original_bytes = PRESET.read_bytes()
    os.makedirs("/tmp/opencode", exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_art_blending_",
                                         dir="/tmp/opencode") as temporary:
            temp_root = Path(temporary)
            opaque = capture(temp_root, "opaque", False, 0.5)
            fullOpacityBlend = capture(temp_root, "full_opacity_blend", True, 1.0)
            partialBlend = capture(temp_root, "partial_blend", True, 0.5)
            opaque_mean, opaque_changed = difference(opaque, fullOpacityBlend)
            partial_mean, partial_changed = difference(opaque, partialBlend)
            if opaque_mean > 0.05 or opaque_changed > 0.005:
                raise RuntimeError(
                    "blend enabled at opacity 1 must match opaque rendering; "
                    f"mean={opaque_mean:.4f}, pixels={opaque_changed:.3%}"
                )
            if partial_mean < 0.25 or partial_changed < 0.005:
                raise RuntimeError(
                    "partial opacity no longer changes the art scene; "
                    f"mean={partial_mean:.4f}, pixels={partial_changed:.3%}"
                )
            if PRESET.read_bytes() != original_bytes:
                raise RuntimeError("preserved original art preset changed during the check")
            print("GFX_ART_BLENDING_CHECK: original-art full-opacity blend routing")
            print(
                f"  opacity 1 vs opaque: mean={opaque_mean:.4f}, "
                f"changed={opaque_changed:.3%}"
            )
            print(
                f"  opacity 0.5 remains translucent: mean={partial_mean:.3f}, "
                f"changed={partial_changed:.2%}"
            )
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"GFX_ART_BLENDING_CHECK: FAIL: {error}", file=sys.stderr)
        return 1
    print("GFX_ART_BLENDING_CHECK: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
