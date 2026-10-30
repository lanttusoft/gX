#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: raymarch shader-feature gate. Three legs:
#   1. feature matrix — each enable flag is forced on/off (GX_GFX_RAYMARCH_FEATURE)
#      and the captured frames must differ (the shader feature is wired);
#   2. defines matrix — the shader is compiled with feature permutations
#      (GX_GFX_RAYMARCH_DEFINES) and must compile cleanly;
#   3. gfx integration — the raymarch module forced into a --gfx scene
#      (GX_GFX_RAYMARCH_VIEW) must visibly change the frame.

import argparse
import re
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
FEATURE_DELTA_MIN = 1.0
INTEGRATION_DELTA_MIN = 1.0
#opencode: per-feature metric overrides. `lights` toggles the light's SDF
#   marker dot (illumination is gated separately), which is far too small for
#   a whole-frame mean — use the changed-pixel fraction instead.
#ai: the hook uses brightness 8 in both captures so the SDF marker remains
#   resolved at the module's default 0.35 render scale after conservative tracing.
FEATURE_METRICS = {"lights": ("changed", 0.00002)}
FEATURE_RE = re.compile(r"GFX_RAYMARCH_FEATURE: (\w+)=(\d) (set|unknown)")
COMPILED_MARKER = ("compiled 'resources/shader/raymarch.vert' + "
                   "'resources/shader/raymarch_new.frag'")
#opencode: features with a visible effect in the default scene. `sky` is
#   composited behind the separate SKY module there, so `proceduralsky` is the
#   sky representative; the hook gives `fog` a visible range (the default
#   40..120 range is beyond the default camera path). objects/lights are empty
#   in the default scene. `objecttexture` is the per-object texturing pair
#   (tex0 triplanar vs off on a diffuse-biased hero torus). `clouds` is the
#   volumetric cloud deck (off by default; enableClouds is a module flag).
#   `proxy` swaps the SDF march for wireframe proxies (stop-render mode);
#   `volfog` is the height fog + sun shafts (enableVolumetricFog).
FEATURES = ("terrain", "water", "fog", "proceduralsky", "grid", "lights",
            "objecttexture", "terraintexture", "waterreflection", "waterwaves",
            "clouds", "proxy", "volfog",
            "terrainscalex", "terrainscaley", "terrainscalez", "time")
BASE_DEFINES = (
    "#define CF_FBMNOISE 0\n#define CF_WATER 1\n#define CF_OBJECTS 1\n"
    "#define CF_LIGHTS 1\n#define CF_FOG 1\n#define CF_GLOW 1\n"
    "#define CF_AO 1\n#define CF_SKYBOX 0\n#define CF_GRID 0\n"
    "#define CF_DEBUGVIS 0\n#define CF_SDF_REPEAT 0\n#define CF_SDF_SMOOTH 0\n"
    "#define CF_HSV 0\n#define CF_VEGETATION 1\n#define CF_FBMNOISE3D 0\n"
    "#define CF_NOISE_TEX 1\n#define CF_SKY_PROCEDURAL 1\n"
    "#define CF_CLOUDS 1\n#define CF_VOLFOG 1")
DEFINES_VARIANTS = {
    "skybox": BASE_DEFINES.replace("CF_SKYBOX 0", "CF_SKYBOX 1"),
    "grid": BASE_DEFINES.replace("CF_GRID 0", "CF_GRID 1"),
    "debugvis": BASE_DEFINES.replace("CF_DEBUGVIS 0", "CF_DEBUGVIS 1"),
    "sdf-repeat": BASE_DEFINES.replace("CF_SDF_REPEAT 0", "CF_SDF_REPEAT 1")
        .replace("CF_SDF_SMOOTH 0", "CF_SDF_SMOOTH 1"),
    "fbmnoise": BASE_DEFINES.replace("CF_FBMNOISE 0", "CF_FBMNOISE 1"),
    "hsv": BASE_DEFINES.replace("CF_HSV 0", "CF_HSV 1"),
    "clouds-off": BASE_DEFINES.replace("CF_CLOUDS 1", "CF_CLOUDS 0"),
    "volfog-off": BASE_DEFINES.replace("CF_VOLFOG 1", "CF_VOLFOG 0"),
}


def capture(temp_root, tag, scene, extra_env, frames=30, shot=20):
    directory = temp_root / tag
    directory.mkdir(exist_ok=True)
    overrides = {
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(frames),
    }
    overrides.update(extra_env)
    environment = audit.scene_env(overrides, clean=True)
    with audit.settings_preserved():
        result = audit.run_scene(scene, environment, width=None, height=None,
            fps=None, frames=frames, shot_frame=shot, screenshot_dir=directory,
            timeout=120, check=True)
    log = audit.strip_ansi(result.stdout or "")
    shot_file = audit.newest_shot(directory)
    if shot_file is None:
        raise RuntimeError(f"{tag}: capture missing")
    pixels = np.asarray(Image.open(shot_file).convert("RGB"),
                        dtype=np.float32)
    return pixels, log


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--calibrate", action="store_true")
    arguments = parser.parse_args()
    if not BIN.is_file():
        print("GFX_RAYMARCH_FEATURES: missing binary", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_rm_features_",
                                         dir="/tmp/opencode") as temporary:
            temp_root = Path(temporary)
            deltas = {}
            changed_values = {}
            for feature in FEATURES:
                frames = {}
                for state in ("0", "1"):
                    frame, log = capture(temp_root,
                        f"{feature}_{state}", "--raymarch", {
                            "GX_GFX_RAYMARCH_FEATURE": feature,
                            "GX_GFX_RAYMARCH_FEATURE_STATE": state,
                        })
                    rows = FEATURE_RE.findall(log)
                    if not rows or rows[-1][2] != "set":
                        raise RuntimeError(
                            f"{feature}: feature hook did not fire: {rows}")
                    frames[state] = frame
                delta = float(np.abs(frames["0"] - frames["1"]).mean())
                changed = audit.diff_frac(frames["0"], frames["1"], cut=10.0)
                deltas[feature] = delta
                changed_values[feature] = changed
                #ai: a sky-color delta alone is insufficient: water must mirror the red fixture above it.
                if feature == "waterreflection":
                    reflected = {}
                    for state, pixels in frames.items():
                        lower = pixels[pixels.shape[0] // 2:]
                        red = (lower[:, :, 0] > lower[:, :, 1] + 15) & (lower[:, :, 0] > lower[:, :, 2] + 15)
                        reflected[state] = float(red.mean())
                    if reflected["1"] < 0.003 or reflected["0"] > 0.0002:
                        raise RuntimeError(f"water does not mirror isolated geometry: {reflected}")
                metric, threshold = FEATURE_METRICS.get(feature,
                    ("delta", FEATURE_DELTA_MIN))
                value = changed if metric == "changed" else delta
                if arguments.calibrate:
                    print(f"  CAL feature {feature:10s} delta={delta:6.2f} "
                          f"changed={changed:8.6f}")
                if value < threshold and not arguments.calibrate:
                    raise RuntimeError(
                        f"{feature}: no visible effect "
                        f"({metric} {value:.6f} < {threshold})")
            for name, defines in DEFINES_VARIANTS.items():
                _, log = capture(temp_root, f"defines_{name}", "--raymarch", {
                    "GX_GFX_RAYMARCH_DEFINES": defines,
                }, frames=20, shot=15)
                if COMPILED_MARKER not in log:
                    raise RuntimeError(
                        f"defines variant '{name}' did not compile")
                if arguments.calibrate:
                    print(f"  CAL defines {name:12s} compiled")
            baseline, _ = capture(temp_root, "gfx_base", "--gfx", {})
            integrated, _ = capture(temp_root, "gfx_raymarch", "--gfx", {
                "GX_GFX_RAYMARCH_VIEW": "1",
            })
            integration_delta = float(np.abs(baseline - integrated).mean())
            if arguments.calibrate:
                print(f"  CAL integration delta={integration_delta:6.2f}")
            if integration_delta < INTEGRATION_DELTA_MIN:
                raise RuntimeError(
                    f"raymarch module does not change the gfx frame "
                    f"(delta {integration_delta:.2f})")
            composite, _ = capture(temp_root, "gfx_composite", "--gfx", {
                "GX_GFX_RAYMARCH_COMPOSITE": "1",
            })
            composite_base_delta = float(np.abs(baseline - composite).mean())
            composite_view_delta = float(np.abs(integrated - composite).mean())
            if arguments.calibrate:
                print(f"  CAL composite vs base={composite_base_delta:6.2f} "
                      f"vs view={composite_view_delta:6.2f}")
            if composite_base_delta < INTEGRATION_DELTA_MIN:
                raise RuntimeError(
                    f"composite does not change the gfx frame "
                    f"(delta {composite_base_delta:.2f})")
            if composite_view_delta < INTEGRATION_DELTA_MIN:
                raise RuntimeError(
                    f"composite hides one layer "
                    f"(delta {composite_view_delta:.2f})")
            #opencode: true-alpha compositing leg — the backdrop-only composite
            #   (GX_GFX_RAYMARCH_COMPOSITE=2) is the raymarch layer alone through
            #   the same pipeline; with GX_GFX_TRANSPARENT_BG=1 the gfx layer
            #   masks its sky alpha, so the sky strip must match the backdrop
            #   exactly and the geometry region must match the opaque gfx frame.
            backdrop, _ = capture(temp_root, "gfx_backdrop", "--gfx", {
                "GX_GFX_RAYMARCH_COMPOSITE": "2",
            })
            alpha_composite, _ = capture(temp_root, "gfx_alpha", "--gfx", {
                "GX_GFX_RAYMARCH_COMPOSITE": "1",
                "GX_GFX_TRANSPARENT_BG": "1",
            })
            sky = (slice(0, 60), slice(0, 1024))
            geom = (slice(400, 700), slice(150, 900))
            alpha_sky = float(np.abs(alpha_composite[sky] - backdrop[sky]).mean())
            screen_sky = float(np.abs(composite[sky] - backdrop[sky]).mean())
            alpha_geom = float(np.abs(alpha_composite[geom] - baseline[geom]).mean())
            alpha_geom_backdrop = float(np.abs(alpha_composite[geom] - backdrop[geom]).mean())
            if arguments.calibrate:
                print(f"  CAL alpha sky={alpha_sky:6.3f} screen_sky={screen_sky:6.2f} "
                      f"geom/base={alpha_geom:6.3f} geom/backdrop={alpha_geom_backdrop:6.2f}")
            if alpha_sky > 0.05:
                raise RuntimeError(
                    f"transparent composite sky does not show the backdrop "
                    f"(delta {alpha_sky:.3f})")
            if screen_sky < 50.0:
                raise RuntimeError(
                    f"screen composite sky matches the backdrop — mode is a "
                    f"no-op (delta {screen_sky:.2f})")
            if alpha_geom > 0.05:
                raise RuntimeError(
                    f"transparent composite geometry does not match the gfx "
                    f"frame (delta {alpha_geom:.3f})")
            if alpha_geom_backdrop < 20.0:
                raise RuntimeError(
                    f"transparent composite hides the gfx geometry "
                    f"(delta {alpha_geom_backdrop:.2f})")
            if arguments.calibrate:
                return 0
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        print(f"GFX_RAYMARCH_FEATURES: FAIL — {error}", file=sys.stderr)
        return 1
    def feature_label(name):
        metric, _ = FEATURE_METRICS.get(name, ("delta", 0.0))
        if metric == "changed":
            return f"{name}={changed_values[name]:.1e}ch"
        return f"{name}={deltas[name]:.1f}"

    feature_summary = " ".join(feature_label(name) for name in FEATURES)
    print(f"GFX_RAYMARCH_FEATURES: PASS features ({feature_summary}) + "
          f"{len(DEFINES_VARIANTS)} defines variants compiled + gfx "
          f"integration delta {integration_delta:.1f} + composite "
          f"(base {composite_base_delta:.1f} / view {composite_view_delta:.1f}) "
          f"+ true-alpha (sky {alpha_sky:.3f} vs screen {screen_sky:.1f}, "
          f"geom/base {alpha_geom:.3f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
