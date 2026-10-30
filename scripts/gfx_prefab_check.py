#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: prefab gate — capture the fixture's anchor model + nearby lights
#   into a .prefab.json, place a copy 2m along +X, and verify the file shape,
#   the radius bound, and both placed positions. Hook: GX_GFX_PREFAB_CHECK.

import json
import re
import sys
import tempfile
from pathlib import Path

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"
CAPTURE_RE = re.compile(
    r"GfxEd: prefab captured models=(\d+) lights=(\d+) decals=(\d+) "
    r"emitters=(\d+) radius=([\d.]+)")
MODEL_PLACE_RE = re.compile(
    r"GfxEd: prefab model placed at=([-\d.]+),([-\d.]+),([-\d.]+)")
LIGHT_PLACE_RE = re.compile(
    r"GfxEd: prefab light placed at=([-\d.]+),([-\d.]+),([-\d.]+)")
HOOK_RE = re.compile(r"GFX_PREFAB_CHECK: (PASS|FAIL) captured=(\d+) placed=(\d+)")


def close(first, second, tolerance=0.01):
    return abs(float(first) - float(second)) <= tolerance


def main():
    if not BIN.is_file() or not PRESET.is_file():
        print("GFX_PREFAB_CHECK: missing binary or preset", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_prefab_",
                                         dir="/tmp/opencode") as temporary:
            output = Path(temporary) / "probe.prefab.json"
            with audit.settings_preserved():
                environment = audit.scene_env({
                    "GX_FIXED_DT": "0.0166666667",
                    "GX_FRAMES": "30",
                    "GX_GFX_PRESET_LOAD": str(PRESET),
                    "GX_GFX_PREFAB_CHECK": str(output),
                }, clean=True)
                result = audit.run_scene("--gfx", environment, width=None,
                                         height=None, fps=None, timeout=120,
                                         check=True,
                                         extra_args=("--gfx-editor",))
            log = audit.strip_ansi(result.stdout or "")
            hook = HOOK_RE.findall(log)
            if not hook or hook[-1][0] != "PASS":
                raise RuntimeError(f"prefab hook did not pass: {hook}")
            captured_ok = int(hook[-1][1])
            placed_total = int(hook[-1][2])

            capture = CAPTURE_RE.findall(log)
            if not capture:
                raise RuntimeError("capture trace missing")
            model_count, light_count, decal_count, emitter_count, radius = (
                int(capture[-1][0]), int(capture[-1][1]),
                int(capture[-1][2]), int(capture[-1][3]),
                float(capture[-1][4]))
            if model_count != 1 or light_count < 1:
                raise RuntimeError(
                    f"expected 1 anchor model + >=1 light, got "
                    f"{model_count}/{light_count}")
            if decal_count != 1 or emitter_count != 1:
                raise RuntimeError(
                    f"expected 1 decal + 1 emitter, got "
                    f"{decal_count}/{emitter_count}")
            expected_placed = 1 + light_count + decal_count + emitter_count
            if captured_ok != 1 or placed_total != expected_placed:
                raise RuntimeError(
                    f"hook reports captured={captured_ok} placed={placed_total}, "
                    f"expected 1/{expected_placed}")

            if not output.is_file():
                raise RuntimeError("prefab file missing")
            document = json.loads(output.read_text())
            entries = document.get("entries") or []
            models = [entry for entry in entries if entry.get("kind") == "model"]
            lights = [entry for entry in entries if entry.get("kind") == "light"]
            decals = [entry for entry in entries if entry.get("kind") == "decal"]
            emitters = [entry for entry in entries if entry.get("kind") == "emitter"]
            if len(models) != 1 or len(lights) != light_count:
                raise RuntimeError(
                    f"file entries models={len(models)} lights={len(lights)}, "
                    f"expected 1/{light_count}")
            if len(decals) != decal_count or len(emitters) != emitter_count:
                raise RuntimeError(
                    f"file entries decals={len(decals)} emitters={len(emitters)}, "
                    f"expected {decal_count}/{emitter_count}")
            if abs(float(decals[0].get("size", 0.0)) - 1.5) > 0.01:
                raise RuntimeError(f"decal size {decals[0].get('size')} != 1.5")
            if float(emitters[0].get("emitRate", 0.0)) <= 0.0:
                raise RuntimeError("emitter emitRate missing")
            if not models[0].get("path"):
                raise RuntimeError("anchor entry has no path")
            for light in lights:
                distance = sum(float(value) ** 2
                               for value in light["pos"]) ** 0.5
                if distance > radius * 1.001:
                    raise RuntimeError(
                        f"light at {distance:.2f} exceeds radius {radius:.2f}")

            fixture = json.loads(PRESET.read_text())
            anchor = fixture["models"][0]["params"]["pos"]
            expected_model = (anchor[0] + 2.0, anchor[1], anchor[2])
            placed = MODEL_PLACE_RE.findall(log)
            if not placed:
                raise RuntimeError("placed model trace missing")
            if any(not close(got, want) for got, want in
                   zip(placed[-1], expected_model)):
                raise RuntimeError(
                    f"model placed at {placed[-1]}, expected {expected_model}")
            placed_light = LIGHT_PLACE_RE.findall(log)
            if not placed_light:
                raise RuntimeError("placed light trace missing")
            relative = lights[0]["pos"]
            expected_light = (expected_model[0] + float(relative[0]),
                              expected_model[1] + float(relative[1]),
                              expected_model[2] + float(relative[2]))
            if any(not close(got, want) for got, want in
                   zip(placed_light[-1], expected_light)):
                raise RuntimeError(
                    f"light placed at {placed_light[-1]}, expected "
                    f"{expected_light}")
            tagged_output = Path(temporary) / "tagged.prefab.json"
            with audit.settings_preserved():
                environment = audit.scene_env({
                    "GX_FIXED_DT": "0.0166666667",
                    "GX_FRAMES": "30",
                    "GX_GFX_PRESET_LOAD": str(PRESET),
                    "GX_GFX_PREFAB_CHECK": str(tagged_output),
                    "GX_GFX_PREFAB_TAGGED": "1",
                }, clean=True)
                result = audit.run_scene("--gfx", environment, width=None,
                                         height=None, fps=None, timeout=120,
                                         check=True,
                                         extra_args=("--gfx-editor",))
            tagged_log = audit.strip_ansi(result.stdout or "")
            tagged_hook = HOOK_RE.findall(tagged_log)
            if not tagged_hook or tagged_hook[-1][0] != "PASS":
                raise RuntimeError(
                    f"tagged hook did not pass: {tagged_hook}")
            tagged_capture = CAPTURE_RE.findall(tagged_log)
            if not tagged_capture:
                raise RuntimeError("tagged capture trace missing")
            tagged_counts = [int(value) for value in tagged_capture[-1][:4]]
            if tagged_counts != [1, 2, 1, 1]:
                raise RuntimeError(
                    f"tagged capture counts {tagged_counts}, expected "
                    f"[1, 2, 1, 1]")
            if int(tagged_hook[-1][2]) != 5:
                raise RuntimeError(
                    f"tagged placed total {tagged_hook[-1][2]}, expected 5")
            tagged_document = json.loads(tagged_output.read_text())
            tagged_kinds = [entry.get("kind")
                            for entry in tagged_document.get("entries") or []]
            for kind, expected in (("model", 1), ("light", 2), ("decal", 1),
                                   ("emitter", 1)):
                if tagged_kinds.count(kind) != expected:
                    raise RuntimeError(
                        f"tagged file {kind} count "
                        f"{tagged_kinds.count(kind)}, expected {expected}")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_PREFAB_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"GFX_PREFAB_CHECK: PASS 1 model + {light_count} lights + "
          f"{decal_count} decal + {emitter_count} emitter captured "
          f"(radius {radius:.1f}), placed at the expected offsets; tagged "
          f"group capture 1/2/1/1")
    return 0


if __name__ == "__main__":
    sys.exit(main())
