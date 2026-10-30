#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: custom label-font gate — loads a shipped font, falls back on a bad
#   path, and asserts the label quad reaches actual scene pixels. The
#   GX_LABEL_DUMP texture comparison still covers glyph rendering; the scene
#   diff (label on vs off) guards the target-restore path that used to drop the
#   quad onto the default framebuffer.

import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import audit_common as audit

import numpy as np
from PIL import Image


ROOT = audit.ROOT
BIN = audit.BIN
PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"
FONT = "resources/font/EuropeanTeletext.ttf"


#opencode: settings safety and window isolation live in audit_common.
with_settings_preserved = audit.with_settings_preserved


def run_editor(preset_path, dump_path, frames=60):
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(frames),
        "GX_GFX_PRESET_LOAD": str(preset_path),
        "GX_LABEL_DUMP": str(dump_path),
    })
    environment.pop("GX_AI_INPUT", None)
    result = subprocess.run(
        [str(BIN), "--gfx", "--gfx-editor"],
        cwd=ROOT, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120,
    )
    if result.returncode != 0:
        raise RuntimeError(f"editor exited {result.returncode}\n{result.stdout[-3000:]}")
    return audit.strip_ansi(result.stdout)


#opencode: label-on vs label-off scene crops; the quad must move real pixels.
def run_editor_shot(preset_path, shot_dir, frames=60):
    shot_dir.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(frames),
        "GX_GFX_PRESET_LOAD": str(preset_path),
        "GX_DOCK_TRACE": "1",
    }, clean=True)
    environment.pop("GX_AI_INPUT", None)
    result = audit.run_scene("--gfx", environment, width=None, height=None,
                             fps=None, shot_frame=frames, screenshot_dir=shot_dir,
                             timeout=120, check=True,
                             extra_args=("--gfx-editor",))
    shot = audit.newest_shot(shot_dir)
    pixels = np.asarray(Image.open(shot).convert("RGB"), dtype=np.float32)
    log = audit.strip_ansi(result.stdout or "")
    x, y, w, h = audit.dock_viewport(log, pixels.shape[1], pixels.shape[0])
    return pixels[y:y + h, x:x + w]


def load_dump(path):
    data = path.read_bytes()
    magic, width, height, channels = struct.unpack_from("<8sIII", data, 0)
    if magic != b"GXLBL1\x00\x00" or channels != 4:
        raise RuntimeError(f"invalid label dump header {magic!r}")
    pixels = np.frombuffer(data, dtype=np.uint8, offset=20)
    if pixels.size != width * height * 4:
        raise RuntimeError("invalid label dump payload")
    return pixels.reshape(height, width, 4)


def mean_delta(first, second):
    return float(np.abs(first.astype(np.int16) - second.astype(np.int16)).mean())


def main():
    if not BIN.is_file() or not PRESET.is_file() or not (ROOT / FONT).is_file():
        print("GFX_LABEL_FONT_CHECK: missing binary, preset, or font", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_label_font_", dir="/tmp/opencode") as temp:
            temp = Path(temp)
            probe = json.loads(PRESET.read_text())
            params = probe["models"][0]["params"]
            params.update({
                "textVisible": True,
                "textBillboard": True,
                "textString": "FONT",
                "textFontSize": 32.0,
                "textFontPath": "",
            })

            def exercise():
                default_dump = temp / "default.bin"
                default_preset = temp / "default.json"
                default_preset.write_text(json.dumps(probe))
                default_log = run_editor(default_preset, default_dump)
                if "GfxEd: label font" in default_log:
                    raise RuntimeError("default label should not load a custom font")
                default = load_dump(default_dump)
                if int((default[..., 3] > 32).sum()) < 50:
                    raise RuntimeError("default label rendered no glyph pixels")

                #opencode: the scene-level check — the label pass must reach
                #   the captured frame, not just the shared text RT.
                label_on = temp / "label_on"
                label_off = temp / "label_off"
                on_pixels = run_editor_shot(default_preset, label_on)
                probe["models"][0]["params"]["textVisible"] = False
                off_preset = temp / "label_off.json"
                off_preset.write_text(json.dumps(probe))
                off_pixels = run_editor_shot(off_preset, label_off)
                probe["models"][0]["params"]["textVisible"] = True
                if on_pixels.shape != off_pixels.shape:
                    raise RuntimeError("label scene captures differ in size")
                scene_delta = np.abs(on_pixels - off_pixels).mean(axis=2)
                visible = int((scene_delta > 40.0).sum())
                if visible < 30 or float(scene_delta.max()) < 60.0:
                    raise RuntimeError(
                        "label quad missing from the scene frame "
                        f"(pixels>40: {visible}, max {float(scene_delta.max()):.1f})")
                print(f"GFX_LABEL_FONT_CHECK: scene label pixels={visible} "
                      f"max={float(scene_delta.max()):.1f}")

                params["textFontPath"] = FONT
                custom_dump = temp / "custom.bin"
                custom_preset = temp / "custom.json"
                custom_preset.write_text(json.dumps(probe))
                custom_log = run_editor(custom_preset, custom_dump)
                if f"GfxEd: label font loaded '{FONT}'" not in custom_log:
                    raise RuntimeError(f"custom label font did not load\n{custom_log[-2000:]}")
                custom = load_dump(custom_dump)
                delta = mean_delta(default, custom)
                if delta < 1.0:
                    raise RuntimeError(
                        f"custom font changed glyph pixels by only {delta:.3f} mean")

                params["textFontPath"] = "resources/font/does_not_exist.ttf"
                bad_dump = temp / "bad.bin"
                bad_preset = temp / "bad.json"
                bad_preset.write_text(json.dumps(probe))
                bad_log = run_editor(bad_preset, bad_dump)
                if "GfxEd: label font failed" not in bad_log:
                    raise RuntimeError(f"bad label font did not warn\n{bad_log[-2000:]}")
                fallback = mean_delta(default, load_dump(bad_dump))
                if fallback > 0.5:
                    raise RuntimeError(
                        f"bad font fallback differs from default by {fallback:.3f} mean")

            with_settings_preserved(exercise)
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
        print(f"GFX_LABEL_FONT_CHECK: FAIL: {error}", file=sys.stderr)
        return 1
    print("GFX_LABEL_FONT_CHECK: PASS custom font renders, bad path falls "
          "back, and the label quad reaches scene pixels")
    return 0


if __name__ == "__main__":
    sys.exit(main())
