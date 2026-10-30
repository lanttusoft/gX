# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#!/usr/bin/env python3
#opencode: shipped-preset persistence pin — all LOAD+SAVE pairs share one
#   three-frame renderer process. The schema load/save path is exercised while
#   avoiding repeated window, GL, and renderer startup per fixture.

import json
import math
import os
#ai: failure reporting must not mask the primary guard error with an unresolved exception type.
import subprocess
import sys

import audit_common as audit


ROOT = audit.ROOT
OUT = "/tmp/opencode/preset_check"
PRESETS = (
    "resources/tools/gfx_editor.json",
    "resources/tools/gfx_y2k.json",
    #opencode: opt-in secondary-UV lightmap fixture round-trip.
    "resources/tools/gfx_lightmap.json",
    #opencode: per-material override entries (sidecar-format data in a preset).
    "resources/tools/gfx_materials.json",
    #opencode: corrected user-art mirror preset remains schema-roundtrip safe.
    "resources/tools/art_mirror_fixed.json",
)


#opencode: run persistence fixtures sequentially after one renderer startup.
def roundtrip_batch(presets):
    jobs = []
    for preset in presets:
        name = os.path.splitext(os.path.basename(preset))[0]
        saved = os.path.join(OUT, f"rt_{name}.json")
        if os.path.isfile(saved):
            os.remove(saved)
        jobs.append((name, preset, saved))

    manifest = os.path.join(OUT, "preset_batch.tsv")
    with open(manifest, "w", encoding="utf-8") as handle:
        for _, preset, saved in jobs:
            handle.write(f"{preset}\t{saved}\n")

    env = audit.scene_env({"GX_FRAMES": "3", "GX_GFX_PRESET_BATCH": manifest},
                          clean=True)
    #opencode: a single-load hook must not shadow the batch manifest.
    env.pop("GX_GFX_PRESET_LOAD", None)
    env.pop("GX_GFX_PRESET_SAVE", None)
    result = audit.run_scene("--gfx", env, width=None, height=None, fps=None,
                             timeout=120, check=False,
                             extra_args=("--gfx-editor",))
    log = audit.strip_ansi(result.stdout or "")
    if result.returncode != 0:
        raise RuntimeError(f"batch: process failed ({result.returncode})")
    for token, count in (("[GFX] Loading preset (new schema) from", len(jobs)),
                         ("[GFX] Load done", len(jobs)),
                         ("[GFX] Saving preset (new schema) to", len(jobs)),
                         ("[GFX] Save done", len(jobs))):
        if log.count(token) != count:
            raise RuntimeError(
                f"batch: expected {count} '{token}' log entries, "
                f"got {log.count(token)}")

    results = []
    for name, preset, saved in jobs:
        if not os.path.isfile(saved):
            raise RuntimeError(f"{name}: no saved file produced")
        with open(os.path.join(ROOT, preset), encoding="utf-8") as handle:
            before = json.load(handle)
        with open(saved, encoding="utf-8") as handle:
            after = json.load(handle)
        results.append((name, before, after))
    #opencode: harness guard — a batch entry targeting a shipped-preset path
    #   must be refused (scene_env sets GX_NO_SETTINGS_SAVE), so the negative
    #   probe destination never appears.
    guardTarget = os.path.join(ROOT, "resources", "tools",
                               "__harness_guard_probe__.json")
    if os.path.isfile(guardTarget):
        os.remove(guardTarget)
    guardManifest = os.path.join(OUT, "guard_batch.tsv")
    with open(guardManifest, "w", encoding="utf-8") as handle:
        handle.write(f"{presets[0]}\t{guardTarget}\n")
    guardEnv = audit.scene_env({"GX_FRAMES": "3",
                                "GX_GFX_PRESET_BATCH": guardManifest},
                               clean=True)
    guardEnv.pop("GX_GFX_PRESET_LOAD", None)
    guardEnv.pop("GX_GFX_PRESET_SAVE", None)
    guardResult = audit.run_scene("--gfx", guardEnv, width=None, height=None,
                                  fps=None, timeout=120, check=False,
                                  extra_args=("--gfx-editor",))
    guardLog = audit.strip_ansi(guardResult.stdout or "")
    if os.path.isfile(guardTarget):
        os.remove(guardTarget)
        raise RuntimeError("harness guard wrote a shipped-preset path")
    if "Refusing to save a shipped preset" not in guardLog:
        raise RuntimeError("harness guard did not refuse the shipped path")
    return results, log


#opencode: compare every field authored in the source preset, not only the
#   historic model-slot signature. New schema fields are added to the probe
#   below with non-default values so nested heaps and renderer subobjects get
#   exercised too.
def check_authored_values(name, before, after):
    differences = []

    def compare(expected, actual, path):
        if isinstance(expected, dict):
            if not isinstance(actual, dict):
                differences.append((path, expected, actual))
                return
            for key, value in expected.items():
                if key in ("name", "description", "created_date", "created_time"):
                    continue
                if key not in actual:
                    differences.append((f"{path}.{key}", value, "<missing>"))
                else:
                    compare(value, actual[key], f"{path}.{key}")
        elif isinstance(expected, list):
            if not isinstance(actual, list) or len(expected) != len(actual):
                actual_size = len(actual) if isinstance(actual, list) else actual
                differences.append((path, len(expected), actual_size))
                return
            for index, (old, new) in enumerate(zip(expected, actual)):
                compare(old, new, f"{path}[{index}]")
        elif isinstance(expected, bool):
            if not isinstance(actual, bool) or expected != actual:
                differences.append((path, expected, actual))
        elif isinstance(expected, (int, float)):
            if not isinstance(actual, (int, float)) or not math.isclose(
                    float(expected), float(actual), rel_tol=0.0, abs_tol=1e-5):
                differences.append((path, expected, actual))
        elif expected != actual:
            differences.append((path, expected, actual))

    compare(before, after, name)
    if differences:
        detail = "; ".join(f"{path}: {old!r} -> {new!r}"
                            for path, old, new in differences[:8])
        raise RuntimeError(f"{name}: authored fields changed: {detail}")


#opencode: a temporary art-like fixture validates the added nested controls
#   without editing any shipped preset or user-authored resources.
def make_schema_probe():
    source = os.path.join(ROOT, "resources/tools/gfx_editor.json")
    with open(source, encoding="utf-8") as handle:
        probe = json.load(handle)

    probe["state"].update({
        "exposure": 1.37,
        "parallaxOffset": 0.0175,
        "normalYThreshold": 0.23,
        "ENABLE_WATER": False,
        "ENABLE_PLANAR_REFLECTIONS": False,
    })
    probe["ssr"].update({
        "normalBufferEnabled": True,
        "maxDepthDelta": 0.01234,
        "worldGapThreshold": 1.75,
        "finalDepthThreshold": 0.0045,
        "hitFacingThreshold": 0.17,
    })
    probe["bloom"].update({
        "knee": 0.27,
        "exposureCompensation": 1.4,
        "exposureMin": 0.18,
        "exposureMax": 7.5,
    })
    probe["models"][0]["useTextures"] = False
    probe["models"][0]["params"].update({
        "emissiveMaterial": "roundtrip_probe_material",
        "glossMapID": -1,
        "excludeFromPlanarCapture": True,
        "reflectionPlaneFollowsModel": True,
        #opencode: pin the custom label-font path round-trip.
        "textFontPath": "resources/font/EuropeanTeletext.ttf",
        #opencode: pin the model motion-path round-trip.
        "orbitEnabled": True,
        "orbitPaused": True,
        "orbitLoop": False,
        "orbitPlane": 0,
        "orbitCenter": [-0.75, 1.25, 2.5],
        "orbitRadius": 3.25,
        "orbitSpeed": 51.0,
        "orbitPhase": -33.0,
        "orbitElapsed": 7.125,
        #opencode: pin the waypoint path round-trip (flat xyz triples).
        "pathMode": 1,
        "pathWaypointCount": 3,
        "pathWaypoints": [0.5, 1.0, -1.5, 2.5, 1.0, -1.5, 2.5, 1.0, 1.0,
                          0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                          0.0, 0.0, 0.0, 0.0, 0.0, 0.0],
        "pathPingPong": True,
        "pathSmooth": True,
    })
    #opencode: round-trip configured circular playback and its current phase.
    probe["lights"][0].update({
        "orbitEnabled": True,
        "orbitPaused": True,
        "orbitLoop": False,
        "orbitPlane": 2,
        "orbitCenter": [1.25, -0.5, 3.75],
        "orbitRadius": 4.5,
        "orbitSpeed": -73.0,
        "orbitPhase": 112.0,
        "orbitElapsed": 12.375,
        #opencode: pin the light waypoint path round-trip (flat xyz triples).
        "pathMode": 1,
        "pathWaypointCount": 2,
        "pathWaypoints": [0.5, 2.0, -1.0, 3.5, 2.0, -1.0,
                          0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                          0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0],
        "pathPingPong": False,
        "pathSmooth": True,
    })
    #opencode: pin the newly exposed water color/normal-map preset fields.
    probe["water"] = {
        "planeHeight": 0.73,
        "tint": [81, 142, 219, 255],
        "normalMapStrength": 1.27,
        "normalMapScale": [2.25, 3.5],
        "normalMapPath": "resources/texture/normal/normal_mapping_normal_map.png",
        "waveCount": 2,
        "waveDirections": [0.7, 1.4, 0.0, 0.0, 0.0, 0.0],
        "waveAmplitudes": [0.14, 0.08, 0.0, 0.0, 0.0, 0.0],
        "waveFrequencies": [1.2, 2.3, 0.0, 0.0, 0.0, 0.0],
        "waveSpeeds": [0.9, 1.3, 0.0, 0.0, 0.0, 0.0],
        "waveSteepness": [0.4, 0.6, 0.0, 0.0, 0.0, 0.0],
    }
    #opencode: pin persistent fog-to-void matching.
    probe["fog"] = {
        "near": 27.0,
        "far": 390.0,
        "color": [58, 92, 144, 255],
        "heightDensity": 0.023,
        "heightBase": -1.25,
        "matchVoidColor": True,
    }
    probe["weather"] = {
        "targetCount": 31,
        "windX": 1.25,
        "windZ": -0.75,
        "fallSpeed": 4.5,
        "floorY": -3.0,
        "halfExtent": 48.0,
        "spawnTop": 25.0,
        "rainTarget": 27,
        "rainWindX": 2.0,
        "rainWindZ": -1.0,
        "rainFall": 16.0,
        "rainLength": 1.7,
        "fogTarget": 9,
        "fogWindX": -0.3,
        "fogWindZ": 0.6,
        "fogOpacity": 0.42,
        "fogScale": 2.4,
    }
    probe["decals"] = [{
        "pos": [1.0, 0.05, -2.0],
        "normal": [0.0, 1.0, 0.0],
        "size": 1.8,
        "rotation": 0.35,
        "textureIndex": -1,
        "alpha": 0.68,
        "snowLayer": False,
        "expireTime": -1.0,
    }]
    probe["particles"] = {"emitters": [{
        "active": True,
        "emitRate": 23.0,
        "pos": [1.0, 2.0, 3.0],
        "baseVel": [0.0, 4.0, 0.5],
        "spread": 17.0,
        "color": [0.3, 0.6, 0.9, 0.8],
        "life": 3.5,
        "speed": 4.25,
    }]}
    probe["camera"] = {
        "position": [4.0, 3.0, 8.0],
        "target": [0.5, 1.0, -0.25],
        "up": [0.0, 1.0, 0.0],
        "fovy": 58.0,
        "projection": 0,
    }
    probe["editor"] = {"usePP": True, "useCompositor": False}

    path = os.path.join(OUT, "gfx_editor_schema_probe.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(probe, handle, indent=2)
    return path


#opencode: intentionally incomplete JSON verifies tolerant loading and both
#   missing-field severities without changing a shipped or user-authored file.
def make_partial_probe():
    path = os.path.join(OUT, "gfx_partial_probe.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump({"models": [{
            "path": "resources/model/primitive/cube.glb",
            "params": {"materialOverrides": [{"roughness": 0.43}]},
        }]}, handle, indent=2)
    return path


#opencode: semantic signature — missing keys and F32 round-trip noise must
#   not fail the pin; entry order, names, colors, paths, and floats within
#   1e-5 do.
def override_signature(entries):
    signature = []
    for entry in entries:
        signature.append((
            entry.get("material", ""),
            tuple(entry.get("emissiveColor", [0, 0, 0, 0])),
            round(float(entry.get("emissive", 0.0)), 5),
            tuple(entry.get("albedo", [0, 0, 0, 0])),
            round(float(entry.get("roughness", -1.0)), 5),
            round(float(entry.get("metalness", -1.0)), 5),
            entry.get("albedoMap", ""),
            entry.get("emissiveMap", ""),
        ))
    return signature


def check_model_slots(name, before, after):
    overrides = 0
    before_models = before.get("models", [])
    after_models = after.get("models", [])
    if len(before_models) != len(after_models):
        raise RuntimeError(
            f"{name}: model count {len(before_models)} -> "
            f"{len(after_models)}")
    if len(before.get("lights", [])) != len(after.get("lights", [])):
        raise RuntimeError(f"{name}: light count changed")
    for index, (old, new) in enumerate(zip(before_models, after_models)):
        for key in ("path", "textureID", "texturePurpose"):
            if old.get(key) != new.get(key):
                raise RuntimeError(
                    f"{name}: model {index} field changed: {key}")
        #opencode: per-material override entries must round-trip (map paths
        #   included; runtime IDs are never serialized).
        old_overrides = old.get("params", {}).get("materialOverrides", [])
        new_overrides = new.get("params", {}).get("materialOverrides", [])
        if override_signature(old_overrides) != override_signature(new_overrides):
            raise RuntimeError(
                f"{name}: model {index} materialOverrides changed: "
                f"{len(old_overrides)} -> {len(new_overrides)}")
        overrides += len(new_overrides)
    return len(before_models), len(before.get("lights", [])), overrides


#opencode: combine shipped, schema, and partial-schema fixtures in one run.
def main():
    os.makedirs(OUT, exist_ok=True)
    print("                  models lights   ovr")
    probe = make_schema_probe()
    partial = make_partial_probe()
    with audit.settings_preserved():
        results, log = roundtrip_batch((*PRESETS, probe, partial))
    for name, before, after in results[:-1]:
        check_authored_values(name, before, after)
        models, lights, overrides = check_model_slots(name, before, after)
        print(f"{name:14s} {models:5d}  {lights:5d}  {overrides:5d}")
    name, before, after = results[-1]
    check_authored_values(name, before, after)
    models = after.get("models", [])
    if len(models) != 1 or models[0].get("path") != "resources/model/primitive/cube.glb":
        raise RuntimeError("partial schema: authored model path changed")
    if "schema_json: missing required field 'models[0].params.materialOverrides[0].material'" not in log:
        raise RuntimeError("partial schema: required-field error was not logged")
    if "schema_json: missing field 'state'" not in log:
        raise RuntimeError("partial schema: missing-field warning was not logged")
    print(f"{name:14s} partial load accepted; warning/error diagnostics verified")
    print(f"PRESET-CHECK: PASS ({len(results)} presets in one 3-frame run)")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError,
            ValueError) as exc:
        print(f"PRESET-CHECK: FAIL — {exc}")
        sys.exit(1)
