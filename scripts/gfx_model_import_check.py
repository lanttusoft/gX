#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: model-import gate — converts the owned COLLADA fixture with assimp,
#   validates the GLB/texture copy, then renders it through the real GFX
#   editor. Source checks pin the editor's .dae discovery and conversion hint.

import json
import shutil
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit
import model_import


ROOT = audit.ROOT
BIN = audit.BIN
FIXTURE = ROOT / "scripts" / "fixtures" / "model_import" / "quad.dae"
EDITOR = ROOT / "src" / "tools" / "gfx" / "gfx_editor.cpp"


def preset(model_path):
    document = json.loads((ROOT / "resources" / "tools" / "empty.json").read_text())
    view = {"position": [0.0, 0.9, 2.6], "target": [0.0, 0.9, 0.0],
            "up": [0.0, 1.0, 0.0], "fovy": 45.0, "projection": 0}
    document["name"] = "Model import fixture"
    document["camera"] = view
    document["cameras"] = [{"name": "Camera 1", "description": "",
                            "view": view}]
    document["models"] = []
    if model_path:
        document["models"].append({
            "path": str(model_path),
            "params": {"pos": [0.0, 0.9, 0.0], "scale": [0.9, 0.9, 0.9],
                       "rotation": [0, 0, 0],
                       "albedoTint": [255, 255, 255, 255]},
            "textureID": [-1] * 8,
            "texturePurpose": [-1] * 8,
        })
    return document


def render(directory, tag, path, extra=None):
    output = directory / (tag + ".png")
    overrides = {
        "GX_EDITOR_FOCUS_VIEWPORT": "1",
        "GX_FRAMES": "40",
        "GX_FIXED_DT": "0.0166666667",
        "GX_GFX_PRESET_LOAD": str(path),
        "GX_GFX_RENDER_CAMERA": str(output),
        "GX_GFX_RENDER_CAMERA_INDEX": "0",
    }
    overrides.update(extra or {})
    environment = audit.scene_env(overrides, clean=True)
    result = audit.run_scene("--gfx", environment, width=1024, height=768,
                             timeout=120, check=True,
                             extra_args=("--gfx-editor",))
    if not output.is_file():
        raise RuntimeError(f"render camera did not write {output}")
    log = audit.strip_ansi(result.stdout or "")
    pixels = np.asarray(Image.open(output).convert("RGB"), dtype=np.float32)
    return pixels, log


def source_checks():
    text = EDITOR.read_text(encoding="utf-8")
    if '".dae"' not in text:
        raise RuntimeError("asset browser does not list .dae sources")
    if "gfx_model_import_command" not in text:
        raise RuntimeError("editor conversion command helper is missing")
    if "Copy convert command" not in text:
        raise RuntimeError("editor conversion hint is missing")


def main():
    if not BIN.is_file():
        print("GFX_MODEL_IMPORT_CHECK: missing build/gX_build", file=sys.stderr)
        return 1
    assimp = shutil.which("assimp")
    if not assimp:
        print("GFX_MODEL_IMPORT_CHECK: SKIP — assimp is not installed")
        return 0
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_model_import_",
                                         dir="/tmp/opencode") as temporary:
            directory = Path(temporary)
            converted = directory / "quad.glb"
            result = model_import.import_model(
                FIXTURE, converted, assimp, force=True)
            if result["meshes"] != 1 or result["primitives"] != 1:
                raise RuntimeError(f"suspicious converted mesh: {result}")
            if not (directory / "quad.png").is_file():
                raise RuntimeError("fixture texture was not copied")
            source_checks()
            baseline_preset = directory / "baseline.json"
            model_preset = directory / "model.json"
            baseline_preset.write_text(json.dumps(preset(None)))
            model_preset.write_text(json.dumps(preset(converted)))
            baseline, _ = render(directory, "baseline", baseline_preset)
            frame, _ = render(directory, "model", model_preset)
            delta = float(np.abs(baseline - frame).mean())
            if delta < 1.0:
                raise RuntimeError(
                    f"converted model does not change the frame (mean {delta:.3f})")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_MODEL_IMPORT_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"GFX_MODEL_IMPORT_CHECK: PASS — converted quad rendered, "
          f"frame delta {delta:.2f}, textures copied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
