#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: PBR admission gate. A public fixture must auto-raise a LOW preset
#   to MEDIUM, light from the procedural-sky IBL, keep material tints distinct,
#   and show a measurable cast-shadow change on a PBR floor.

import json
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"
SHOT_FRAME = 70
FRAMES = 80


def make_fixture(path, shadows, density=2.2):
    base = json.loads(PRESET.read_text())
    floor = json.loads(json.dumps(base["models"][0]))
    floor["path"] = "resources/model/primitive/cube.glb"
    floor["params"].update({
        "pos": [0, 0, 0], "scale": [16, 0.1, 16], "rotation": [0, 0, 0],
        "usePBR": True, "albedo": 0.85,
        "albedoTint": [190, 190, 185, 255],
        "metalness": 0.0, "roughness": 0.9,
        "hasAlbedoMap": False, "hasMetalnessMap": False,
        "hasNormalMap": False, "hasHeightMap": False,
        "hasEmissiveMap": False, "hasRoughnessMap": False, "hasAoMap": False,
        "enableReflections": False, "planarReflection": False,
        "receiveSSR": False, "specularColor": [1, 1, 1],
    })
    models = [floor]
    colors = ([220, 80, 60, 255], [80, 200, 100, 255],
              [80, 120, 230, 255], [230, 210, 80, 255],
              [220, 220, 220, 255])
    metalness = (0.0, 0.25, 0.5, 0.75, 1.0)
    roughness = (0.8, 0.6, 0.4, 0.2, 0.05)
    for index in range(5):
        sphere = json.loads(json.dumps(base["models"][0]))
        sphere["path"] = "resources/model/primitive/sphere.glb"
        sphere["params"].update({
            "pos": [-6 + index * 3, 1.0, 0], "scale": [1.4, 1.4, 1.4],
            "rotation": [0, 0, 0], "usePBR": True, "albedo": 0.9,
            "albedoTint": colors[index],
            "metalness": metalness[index], "roughness": roughness[index],
            "hasAlbedoMap": False, "hasMetalnessMap": False,
            "hasNormalMap": False, "hasHeightMap": False,
            "hasEmissiveMap": False, "hasRoughnessMap": False, "hasAoMap": False,
            "enableReflections": False, "planarReflection": False,
            "receiveSSR": False, "specularColor": [1, 1, 1],
        })
        models.append(sphere)
    caster = json.loads(json.dumps(base["models"][0]))
    caster["path"] = "resources/model/primitive/cube.glb"
    caster["params"].update({
        "pos": [0, 4.0, -1.5], "scale": [2.0, 0.4, 2.0],
        "rotation": [0, 0, 0], "usePBR": True, "albedo": 0.6,
        "albedoTint": [120, 120, 120, 255],
        "metalness": 0.0, "roughness": 0.7,
        "hasAlbedoMap": False, "hasMetalnessMap": False,
        "hasNormalMap": False, "hasHeightMap": False,
        "hasEmissiveMap": False, "hasRoughnessMap": False, "hasAoMap": False,
        "enableReflections": False, "planarReflection": False,
        "receiveSSR": False, "specularColor": [1, 1, 1],
    })
    models.append(caster)
    base["models"] = models
    base["lights"] = []
    base["camera"] = {"position": [9, 5, 14], "target": [0, 1.0, 0],
                      "up": [0, 1, 0], "fovy": 45.0, "projection": 0}
    base["cameras"] = [{"name": "Camera 1", "description": "",
                        "view": base["camera"]}]
    base["sky"]["density"] = density
    base["sun"] = {
        "dir": [0.35, -0.85, 0.40], "color": [255, 255, 255, 255],
        "brightness": 1.0, "castShadows": bool(shadows),
        "diffuseTint": [255, 255, 255, 255],
        "specularTint": [255, 255, 255, 255],
        "diffuseFactor": 1.0, "specularFactor": 1.0,
    }
    base["state"].update({
        "qualityPreset": 0, "shaderQuality": 0,
        "ENABLE_SHADOWS": bool(shadows), "ENABLE_POINT_SHADOWS": False,
        "ENABLE_SSAO": False, "ENABLE_GODRAYS": False,
        "ENABLE_BLOOM": False, "ENABLE_FXAA": False, "ENABLE_TAA": False,
        "ENABLE_SSR": False, "ENABLE_REFLECTIONS": False,
        "ENABLE_PLANAR_REFLECTIONS": False, "ENABLE_ENV_AMBIENT": True,
        "ENABLE_VOLUME_FOG": False, "ENABLE_FOG": False,
        "ENABLE_PROCEDURAL_SKY": True, "ENABLE_SKYBOX": False,
        "ENABLE_ACES_TONEMAP": True, "ENABLE_AGX_TONEMAP": False,
        "ENABLE_GAMMA_CORRECTION": True, "exposure": 1.0,
    })
    path.write_text(json.dumps(base, indent=1) + "\n")


def run_editor(fixture, shot_dir, extra=None):
    overrides = {
        "GX_FIXED_DT": "0.0166666667", "GX_FRAMES": str(FRAMES),
        "GX_GFX_PRESET_LOAD": str(fixture),
        "GX_EDITOR_FOCUS_VIEWPORT": "1",
    }
    overrides.update(extra or {})
    environment = audit.scene_env(overrides, clean=True)
    result = audit.run_scene("--gfx", environment, width=None, height=None,
        fps=None, shot_frame=SHOT_FRAME, screenshot_dir=shot_dir,
        timeout=120, check=False, extra_args=("--gfx-editor",))
    return audit.strip_ansi(result.stdout or ""), result.returncode


def main():
    if not BIN.is_file() or not PRESET.is_file():
        print("PBR_CHECK: missing binary or preset", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="pbr_check_",
                                         dir="/tmp/opencode") as temporary:
            root = Path(temporary)
            on_fixture = root / "pbr_on.json"
            off_fixture = root / "pbr_off.json"
            make_fixture(on_fixture, True)
            make_fixture(off_fixture, False)
            on_dir = root / "on"
            off_dir = root / "off"
            on_dir.mkdir()
            off_dir.mkdir()
            with audit.settings_preserved():
                on_log, on_code = run_editor(on_fixture, on_dir)
                off_log, off_code = run_editor(off_fixture, off_dir)
            if on_code != 0 or off_code != 0:
                raise RuntimeError(f"editor runs failed ({on_code}/{off_code})")
            if "PBR preset raised" not in on_log and \
                    "PBR model requires MEDIUM" not in on_log:
                raise RuntimeError("PBR preset did not raise LOW quality")
            if "IBL sky cube captured" not in on_log:
                raise RuntimeError("procedural-sky IBL capture missing")
            if "IBL ready" not in on_log:
                raise RuntimeError("IBL maps did not become ready")
            on_shots = sorted(on_dir.glob("gXss_*.png"))
            off_shots = sorted(off_dir.glob("gXss_*.png"))
            if not on_shots or not off_shots:
                raise RuntimeError("PBR screenshots missing")
            on = np.asarray(Image.open(on_shots[-1]).convert("RGB"),
                            dtype=np.float32)
            off = np.asarray(Image.open(off_shots[-1]).convert("RGB"),
                             dtype=np.float32)
            if float(on.mean()) < 20.0:
                raise RuntimeError(f"PBR scene too dark (mean {on.mean():.1f})")
            height, width = on.shape[:2]
            spheres = on[int(height * 0.34):int(height * 0.60),
                         int(width * 0.26):int(width * 0.84)]
            tint_spread = float(spheres.std(axis=(0, 1)).mean())
            if tint_spread < 5.0:
                raise RuntimeError(f"PBR material tints collapsed "
                                   f"(spread {tint_spread:.2f})")
            difference = np.abs(on - off).mean(axis=2)
            changed_fraction = float((difference > 8.0).mean())
            if changed_fraction < 0.005:
                raise RuntimeError(f"PBR cast shadows did not register "
                                   f"({changed_fraction:.4f} changed fraction)")
            live_fixture = root / "pbr_live.json"
            reference_fixture = root / "pbr_reference.json"
            make_fixture(live_fixture, True, density=0.2)
            make_fixture(reference_fixture, True, density=5.0)
            live_dir = root / "live"
            reference_dir = root / "reference"
            stale_dir = root / "stale"
            live_dir.mkdir()
            reference_dir.mkdir()
            stale_dir.mkdir()
            with audit.settings_preserved():
                live_log, live_code = run_editor(
                    live_fixture, live_dir,
                    {"GX_GFX_IBL_REFRESH_TEST": "5.0"})
                reference_log, reference_code = run_editor(
                    reference_fixture, reference_dir)
                stale_log, stale_code = run_editor(
                    live_fixture, stale_dir,
                    {"GX_GFX_IBL_REFRESH_TEST": "5.0",
                     "GX_GFX_IBL_REFRESH_DISABLE": "1"})
            if live_code != 0 or reference_code != 0 or stale_code != 0:
                raise RuntimeError(
                    f"IBL refresh runs failed "
                    f"({live_code}/{reference_code}/{stale_code})")
            if "IBL_REFRESH_TEST" not in live_log:
                raise RuntimeError("IBL refresh hook did not fire")
            if "IBL refresh invalidated" not in live_log:
                raise RuntimeError("IBL refresh was not requested")
            if live_log.count("IBL sky cube captured") < 2:
                raise RuntimeError("IBL sky cube was not recaptured")
            if "IBL refresh invalidated" in reference_log:
                raise RuntimeError("reference run unexpectedly refreshed IBL")
            if "IBL refresh invalidated" in stale_log:
                raise RuntimeError("disabled refresh was not disabled")
            live_shots = sorted(live_dir.glob("gXss_*.png"))
            reference_shots = sorted(reference_dir.glob("gXss_*.png"))
            stale_shots = sorted(stale_dir.glob("gXss_*.png"))
            if not live_shots or not reference_shots or not stale_shots:
                raise RuntimeError("IBL refresh screenshots missing")
            live = np.asarray(Image.open(live_shots[-1]).convert("RGB"),
                              dtype=np.float32)
            reference = np.asarray(
                Image.open(reference_shots[-1]).convert("RGB"),
                dtype=np.float32)
            live_region = live[int(height * 0.34):int(height * 0.60),
                               int(width * 0.26):int(width * 0.84)]
            reference_region = reference[
                int(height * 0.34):int(height * 0.60),
                int(width * 0.26):int(width * 0.84)]
            stale = np.asarray(Image.open(stale_shots[-1]).convert("RGB"),
                               dtype=np.float32)
            stale_region = stale[int(height * 0.34):int(height * 0.60),
                                 int(width * 0.26):int(width * 0.84)]
            refresh_delta = float(np.abs(live_region - reference_region).mean())
            stale_delta = float(np.abs(stale_region - reference_region).mean())
            if refresh_delta > 2.5:
                raise RuntimeError(
                    f"live IBL does not match a fresh capture "
                    f"(delta {refresh_delta:.2f})")
            if stale_delta < 2.5:
                raise RuntimeError(
                    f"disabled refresh was not detectable "
                    f"(delta {stale_delta:.2f})")
            print(f"PBR_CHECK: PASS mean={on.mean():.1f} "
                  f"tint-spread={tint_spread:.2f} shadow-fraction={changed_fraction:.4f} "
                  f"refresh-delta={refresh_delta:.2f} "
                  f"stale-delta={stale_delta:.2f}")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"PBR_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
