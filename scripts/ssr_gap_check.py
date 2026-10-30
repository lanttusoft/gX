#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: engine-owned fixed-camera reflection coverage fixture; controls are not a renderer fidelity certificate.
"""Separate planar, direct cube and SSR sources without author assets or presets."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import tempfile

import numpy as np
from PIL import Image
from scipy.ndimage import binary_dilation, binary_erosion

import audit_common as audit
from gfx_buffer_audit import read_dump

WIDTH, HEIGHT, FRAME = 800, 600, 35
CAMERA = {"position": [0, 3, 5], "target": [0, 0, 0],
          "up": [0, 1, 0], "fovy": 45.0, "projection": 0}
SOURCE_LO = np.array([-.7, 3.3, -2.7])
SOURCE_HI = np.array([.7, 4.7, -1.3])
BLOCKER_LO = np.array([-.8, 1.8, -.25])
BLOCKER_HI = np.array([.8, 2.6, .25])
MODES = ("baseline", "planar", "cubemap", "cubemap_candidate",
         "ssr_only", "ssr_env", "ssr_classes", "ssr_classes_env")
DEPTH_ALLOWANCE = 2e-6
INTERIOR_PIXELS = 3


#ai: outward triangles with positions baked into owned meshes; no node transforms or imported assets.
def box_mesh(lower, upper):
    vertices, normals = [], []
    for face in range(6):
        axis = face // 2
        normal = np.eye(3)[axis] * (1 if face % 2 == 0 else -1)
        tangent = np.eye(3)[(axis + 1) % 3]
        bitangent = np.cross(normal, tangent)
        corners = np.array([normal - tangent - bitangent,
                            normal + tangent - bitangent,
                            normal + tangent + bitangent,
                            normal - tangent + bitangent])
        vertices.extend((lower + upper) / 2
                        + corners[[0, 1, 2, 0, 2, 3]] * (upper - lower) / 2)
        normals.extend(np.tile(normal, (6, 1)))
    return vertices, normals


def write_glb(path, vertices, normals, emission):
    path = Path(path)
    binary, views, accessors = bytearray(), [], []
    for values in (vertices, normals):
        array = np.asarray(values, dtype="<f4")
        binary.extend(b"\0" * (-len(binary) % 4))
        views.append({"buffer": 0, "byteOffset": len(binary),
                      "byteLength": array.nbytes})
        binary.extend(array.tobytes())
        accessors.append({"bufferView": len(views) - 1, "componentType": 5126,
                          "count": len(array), "type": "VEC3",
                          "min": array.min(axis=0).tolist(),
                          "max": array.max(axis=0).tolist()})
    document = {"asset": {"version": "2.0", "generator": "gX owned SSR gap"},
                "buffers": [{"byteLength": len(binary)}], "bufferViews": views,
                "accessors": accessors, "materials": [{"name": path.stem,
                    "emissiveFactor": emission, "pbrMetallicRoughness": {
                        "baseColorFactor": [0, 0, 0, 1],
                        "metallicFactor": 0, "roughnessFactor": 0}}],
                "meshes": [{"primitives": [{"attributes": {
                    "POSITION": 0, "NORMAL": 1}, "material": 0}]}],
                "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0}
    text = json.dumps(document).encode()
    text += b" " * (-len(text) % 4)
    binary.extend(b"\0" * (-len(binary) % 4))
    with path.open("xb") as file:
        file.write(struct.pack("<III", 0x46546c67, 2, 28 + len(text) + len(binary)))
        file.write(struct.pack("<II", len(text), 0x4e4f534a) + text)
        file.write(struct.pack("<II", len(binary), 0x004e4942) + binary)


#ai: CPU slab intersection handles parallel rays exactly, not by substituting epsilon directions.
def box_time(origin, direction, lower, upper):
    origin = np.broadcast_to(origin, direction.shape)
    nonzero = direction != 0
    a = np.divide(lower - origin, direction,
                  out=np.zeros_like(direction), where=nonzero)
    b = np.divide(upper - origin, direction,
                  out=np.zeros_like(direction), where=nonzero)
    near = np.where(nonzero, np.minimum(a, b), -np.inf).max(axis=-1)
    far = np.where(nonzero, np.maximum(a, b), np.inf).min(axis=-1)
    parallel_outside = np.any(~nonzero & ((origin < lower) | (origin > upper)), axis=-1)
    valid = (far >= np.maximum(near, 0)) & ~parallel_outside & np.any(nonzero, axis=-1)
    return np.where(valid, np.maximum(near, 0), np.inf)


def camera_rays():
    eye = np.array(CAMERA["position"], dtype=float)
    forward = np.array(CAMERA["target"], dtype=float) - eye
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, CAMERA["up"])
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    y, x = np.indices((HEIGHT, WIDTH))
    scale = np.tan(np.deg2rad(CAMERA["fovy"]) / 2)
    direction = (forward + right * ((2 * (x + .5) / WIDTH - 1)
                 * WIDTH / HEIGHT * scale)[..., None]
                 + up * ((1 - 2 * (y + .5) / HEIGHT) * scale)[..., None])
    return eye, direction, forward


#ai: ideal first-hit masks for a flat reflector, with independent direct-view exclusion.
def oracle(blocked):
    eye, direction, forward = camera_rays()
    time = np.divide(-eye[1], direction[..., 1],
                     out=np.full((HEIGHT, WIDTH), np.inf), where=direction[..., 1] < 0)
    point = eye + direction * time[..., None]
    ground = ((time > 0) & np.isfinite(time)
              & (np.abs(point[..., 0]) < 5) & (np.abs(point[..., 2]) < 5))
    primary_source = box_time(eye, direction, SOURCE_LO, SOURCE_HI)
    primary_block = (box_time(eye, direction, BLOCKER_LO, BLOCKER_HI)
                     if blocked else np.full_like(time, np.inf))
    ground &= (time < primary_source) & (time < primary_block)
    outgoing = direction.copy()
    outgoing[..., 1] *= -1
    source_time = box_time(point, outgoing, SOURCE_LO, SOURCE_HI)
    block_time = (box_time(point, outgoing, BLOCKER_LO, BLOCKER_HI)
                  if blocked else np.full_like(time, np.inf))
    red = ground & np.isfinite(source_time) & (source_time < block_time)
    green = ground & np.isfinite(block_time) & (block_time < source_time)
    distance = np.einsum("ijk,k->ij", point - eye, forward)
    return {"ground": ground, "red": red, "green": green,
            "interior_red": binary_erosion(red, iterations=INTERIOR_PIXELS),
            "interior_green": binary_erosion(green, iterations=INTERIOR_PIXELS),
            "view_distance": distance,
            "primary_source_pixels": int(np.isfinite(primary_source).sum())}


def model_parameters():
    return {"pos": [0, 0, 0], "scale": [1, 1, 1], "rotation": [0, 0, 0],
            "textVisible": False, "usePBR": False, "albedo": 0,
            "albedoTint": [0, 0, 0, 255], "emissiveFactor": 0,
            "emissiveColor": [0, 0, 0, 255], "roughness": 0, "shininess": 32,
            "enableBlending": False, "blendOpacity": 1,
            "enableRefraction": False, "enableReflections": False,
            "receiveSSR": False, "planarReflection": False,
            "nonPlanarReflection": False, "reflectionNormal": [0, 1, 0],
            "reflectionPlaneHeight": 0, "hasLightmap": False}


#ai: only supported schema fields; no author preset or game-model dependency.
def make_preset(directory, mode, blocked):
    if mode not in MODES:
        raise ValueError(f"unknown fixture source: {mode}")
    off = ("LIGHTING_AMBIENT LIGHTING_DIFFUSE LIGHTING_SPECULAR ENV_AMBIENT "
           "MAP_METALNESS MAP_NORMAL MAP_PARALLAX DETAIL FOG VOLUME_FOG SHADOWS "
           "POINT_SHADOWS COOKIES SKYBOX PROCEDURAL_SKY BLOOM TAA FXAA WATER "
           "MOTION_BLUR DOF SSAO PARTICLES SNOWFALL RAIN FOGSPRITE SPLAT GODRAYS "
           "LENS_FLARE CLUSTERED REFLECTIONS PLANAR_REFLECTIONS SSR SSR_AUXVIEW "
           "ACES_TONEMAP AGX_TONEMAP GAMMA_CORRECTION").split()
    state = {"ENABLE_" + key: False for key in off}
    state.update({"SHOW_" + key: False for key in (
        "GRID", "GIZMO", "LIGHT_MARKERS", "BOUNDING_BOX")})
    state.update(ENABLE_PHONG=True, ENABLE_MAP_ALBEDO=True, shaderQuality=1,
                 msaaLevel=0, exposure=1, envAmbientFactor=0, textureFilter=0)
    floor = model_parameters()
    floor.update(usePBR=True, metalness=0, enableReflections=True, receiveSSR=True)
    models = [{"path": str(directory / "floor.glb"), "params": floor},
              {"path": str(directory / "emitter.glb"), "params": model_parameters()}]
    if blocked:
        models.append({"path": str(directory / "occluder.glb"), "params": model_parameters()})
    if mode == "planar":
        state["ENABLE_PLANAR_REFLECTIONS"] = True
        floor["planarReflection"] = True
    if mode.startswith("cubemap"):
        state["ENABLE_REFLECTIONS"] = True
    if mode.startswith("ssr"):
        state["ENABLE_SSR"] = True
        state["ENABLE_REFLECTIONS"] = mode.endswith("_env")
        state["ssrDebugMode"] = 14 if mode.startswith("ssr_classes") else 0
    return {"name": "Owned SSR gap, fixed camera", "description": "Generated owned geometry",
            "state": state, "bloom": {"autoExposure": False},
            "reflection": {"bias": 1 if mode.startswith("cubemap") else .55 if mode == "planar" else 0},
            "sky": {"color": [0, 0, 0, 255]},
            "sun": {"brightness": 0, "color": [0, 0, 0, 255], "castShadows": False},
            "ssr": {"opacity": 1, "roughness": 0, "normalBufferEnabled": True, "maxBounces": 1},
            "camera": CAMERA, "models": models, "lights": [], "textures": [], "decals": []}


def read_hdr(path):
    data = Path(path).read_bytes()
    if len(data) < 20 or data[:8] != b"GXHDR1\0\0":
        raise ValueError("invalid owned HDR header")
    width, height, channels = struct.unpack_from("<III", data, 8)
    if (width, height, channels) != (WIDTH, HEIGHT, 4) or len(data) != 20 + width * height * 16:
        raise ValueError("invalid owned HDR extent/payload")
    image = np.frombuffer(data, dtype="<f4", offset=20).reshape(height, width, channels)[::-1]
    if not np.isfinite(image).all():
        raise ValueError("nonfinite owned HDR pixels")
    return image


def color_mask(image, channel):
    others = [index for index in range(3) if index != channel]
    return ((image[..., channel] > 1e-4)
            & (image[..., channel] > image[..., others].max(axis=-1) * 3))


def measure(image, prediction):
    red, green = color_mask(image, 0), color_mask(image, 1)
    region = prediction["interior_red"]
    green_region = prediction["interior_green"]
    outside = prediction["ground"] & ~prediction["red"]
    return {"red_expected_pixels": int(prediction["red"].sum()),
            "red_interior_recall": float(red[region].mean()),
            "red_false_positive_ground_pixels": int((red & outside).sum()),
            "red_false_positive_beyond_3px": int((red & prediction["ground"]
                & ~binary_dilation(prediction["red"], iterations=INTERIOR_PIXELS)).sum()),
            "green_expected_pixels": int(prediction["green"].sum()),
            "green_interior_recall": float(green[green_region].mean()) if green_region.any() else None,
            "red_on_occluded_interior_pixels": int((red & green_region).sum()),
            "mean_expected_red_rgba": image[region].mean(axis=0).tolist()}


#ai: retain raw color metrics and separately isolate weak direct reflection from neutral material light.
def material_delta(image, baseline, prediction):
    if image.shape != baseline.shape or not np.isfinite(image).all() or not np.isfinite(baseline).all():
        raise ValueError("matching finite material buffers required for baseline subtraction")
    delta = image - baseline
    return {"positive_rgb_metrics": measure(np.maximum(delta, 0), prediction),
            "min_rgb_delta": float(delta[..., :3].min()),
            "max_rgb_delta": float(delta[..., :3].max()),
            "mean_expected_red_rgb_delta": delta[prediction["interior_red"], :3].mean(axis=0).tolist(),
            "visibility_certified": False}


#ai: pass/fail applies to fixture controls; cube visibility metrics remain observations, not approved routing.
def controls(mode, image, prediction):
    metrics = measure(image, prediction)
    checks = {}
    if mode == "baseline":
        checks["no_direct_red_on_ground"] = metrics["red_false_positive_ground_pixels"] == 0 and not color_mask(image, 0)[prediction["ground"]].any()
    elif mode == "planar":
        checks["planar_red_interior"] = metrics["red_interior_recall"] >= .99
        checks["planar_no_invalid_red"] = metrics["red_false_positive_beyond_3px"] == 0
        if prediction["interior_green"].any():
            checks["planar_occlusion"] = metrics["green_interior_recall"] >= .99 and metrics["red_on_occluded_interior_pixels"] == 0
    elif mode == "ssr_only":
        checks["offscreen_source_not_claimed_by_screen_color"] = metrics["red_interior_recall"] == 0
    elif mode.startswith("ssr_classes"):
        region = prediction["interior_red"]
        checks["offscreen_source_region_has_no_march_geometry_hit"] = not color_mask(image, 0)[region].any()
        checks["diagnostic_pass_executed"] = bool(np.all(image[region, 3] > .99))
    return metrics, checks


def depth_control(image, prediction, log):
    match = re.search(r"ORACLEDUMP f=\d+ .*?near=([0-9.]+) far=([0-9.]+) size=(\d+)x(\d+)", log)
    if not match or tuple(map(int, match.group(3, 4))) != (WIDTH, HEIGHT):
        raise ValueError("missing native planar frustum/extent witness")
    near, far = map(float, match.group(1, 2))
    if (near, far) != (.1, 1000.0):
        raise ValueError("native frustum differs from frozen fixture")
    distance = prediction["view_distance"]
    expected = far / (far - near) - far * near / ((far - near) * distance)
    interior = binary_erosion(prediction["ground"], iterations=INTERIOR_PIXELS)
    delta = np.abs(image[..., 0][interior] - expected[interior])
    return {"pixels": int(interior.sum()), "max_abs_error": float(delta.max()),
            "allowance": DEPTH_ALLOWANCE, "projection_fitted": False,
            "pass": bool(delta.max() <= DEPTH_ALLOWANCE)}


def file_hash(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def protected_hashes():
    files = list((audit.ROOT / "resources/tools").rglob("*.json"))
    files.append(audit.ROOT / "resources/settings.json")
    return {str(path): file_hash(path) if path.is_file() else None for path in files}


#ai: owned artifacts stay external; refuse overwrites and do not restore/overwrite concurrent author work.
def capture(directory, binary):
    directory = Path(directory).resolve()
    if directory == audit.ROOT or audit.ROOT in directory.parents:
        raise ValueError("fixture output must be outside the repository")
    directory.mkdir(parents=True, exist_ok=True)
    if any(directory.iterdir()):
        raise ValueError("fixture output must be empty")
    before = protected_hashes()
    sources = ("resources/shader/gfx_ssr.frag", "resources/shader/standard.frag",
               "src/renderer/gfx/draw.c", "src/renderer/gfx/model.c")
    hashes = {name: file_hash(audit.ROOT / name) for name in sources}
    binary_hash = file_hash(binary)
    contract = {"camera": CAMERA, "size": [WIDTH, HEIGHT], "frame": FRAME,
                "source_bounds": [SOURCE_LO.tolist(), SOURCE_HI.tolist()],
                "occluder_bounds": [BLOCKER_LO.tolist(), BLOCKER_HI.tolist()],
                "plane": {"y": 0, "half_extent": 5}, "modes": MODES,
                "depth_allowance": DEPTH_ALLOWANCE, "interior_pixels": INTERIOR_PIXELS,
                "color_test": "channel>1e-4 and channel>3*other_channels",
                "controls_only_not_renderer_admission": True}
    (directory / "contract.json").write_text(json.dumps(contract, indent=2) + "\n")
    write_glb(directory / "floor.glb", [[-5, 0, -5], [-5, 0, 5], [5, 0, 5],
              [-5, 0, -5], [5, 0, 5], [5, 0, -5]], [[0, 1, 0]] * 6, [0, 0, 0])
    write_glb(directory / "emitter.glb", *box_mesh(SOURCE_LO, SOURCE_HI), [1, 0, 0])
    write_glb(directory / "occluder.glb", *box_mesh(BLOCKER_LO, BLOCKER_HI), [0, 1, 0])
    rows, depths = [], []
    print(f"SSR_GAP output={directory}", flush=True)
    try:
        for blocked in (False, True):
            prediction = oracle(blocked)
            if prediction["primary_source_pixels"] or prediction["interior_red"].sum() < 1000:
                raise ValueError("source must be offscreen with a nonempty reflected interior")
            np.savez_compressed(directory / f"oracle_{int(blocked)}.npz", **prediction)
            for mode in MODES:
                label = f"{mode}_occluder{int(blocked)}"
                case = directory / label
                case.mkdir()
                preset = case / "input.json"
                preset.write_text(json.dumps(make_preset(directory, mode, blocked), indent=2) + "\n")
                view = "ssr" if mode.startswith("ssr") else "depth"
                hooks = {"GX_GFX_PRESET_LOAD": str(preset), "GX_FIXED_DT": "0.0166666667",
                    "GX_T_HDRDUMP": "1", "GX_HDRDUMP_FRAME": FRAME,
                    "GX_HDRDUMP_PATH": str(case / "main.hdr"), "GX_RT_VIEW": view,
                    "GX_RT_DUMP_DIR": str(case), "GX_RT_DUMP_FRAME": FRAME,
                    "GX_RT_DUMP_COUNT": "1", "GX_T_SSRENV": int(mode.endswith("_env")),
                    "GX_PROBE_GRID": "0", "GX_PROBE_BOX_CORRECT": int(mode == "cubemap_candidate"),
                    "GX_LAYOUT_NORESEAT": "1", "GX_VALIDATE": "1"}
                result = audit.run_scene("--gfx", audit.scene_env(hooks, clean=True),
                    binary=binary, width=WIDTH, height=HEIGHT, fps=None, frames=FRAME + 2,
                    extra_args=("--gfx-editor", "--seed", "424242"), timeout=180, check=False)
                log = audit.strip_ansi(result.stdout)
                (case / "renderer.log").write_text(log)
                if result.returncode:
                    raise RuntimeError(f"{label} exited {result.returncode}; see renderer.log")
                main_image = read_hdr(case / "main.hdr")
                raw = read_dump(case / f"{view}_{FRAME:06d}.gxrt")[::-1]
                if raw.shape[:2] != (HEIGHT, WIDTH) or not np.isfinite(raw).all():
                    raise ValueError("invalid native RT extent/pixels")
                image = raw if view == "ssr" else main_image
                metrics, checks = controls(mode, image, prediction)
                row = {"label": label, "metrics": metrics, "checks": checks,
                       "startup_error_lines": [line for line in log.splitlines() if "ERROR!" in line]}
                if mode.startswith("cubemap"):
                    row["direct_material_delta"] = material_delta(main_image, baseline_material, prediction)
                rows.append(row)
                if mode == "baseline":
                    baseline_material = main_image
                    planar_log_path = directory / f"planar_occluder{int(blocked)}/renderer.log"
                    baseline_depth = raw
                if mode == "planar":
                    depth = depth_control(baseline_depth, prediction, planar_log_path.read_text())
                    depth["occluder"] = blocked
                    depths.append(depth)
                Image.fromarray((np.clip(image[..., :3], 0, 1) * 255).astype("uint8")).save(case / "raw_preview.png")
                (directory / "partial_results.json").write_text(json.dumps(rows, indent=2) + "\n")
                print(f"SSR_GAP {label} {json.dumps(metrics)}", flush=True)
    finally:
        unchanged = protected_hashes() == before
        (directory / "protected_settings.json").write_text(json.dumps({"before": before,
            "unchanged": unchanged}, indent=2) + "\n")
    stable = (file_hash(binary) == binary_hash and hashes == {
        name: file_hash(audit.ROOT / name) for name in sources})
    passed = unchanged and stable and all(row["pass"] for row in depths) and all(
        all(row["checks"].values()) for row in rows)
    report = {"rows": rows, "depth_controls": depths, "fixture_controls_pass": passed,
              "protected_settings_unchanged": unchanged, "inputs_stable": stable,
              "binary_sha256": binary_hash, "source_sha256": hashes,
              "fixture_does_not_modify_renderer": True, "no_second_camera": True,
              "cube_metrics_are_observations_not_visibility_validation": True}
    (directory / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"SSR_GAP fixture_controls={'PASS' if passed else 'FAIL'} report={directory / 'report.json'}")
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="empty directory outside the repository")
    parser.add_argument("--binary", type=Path, default=audit.BIN)
    args = parser.parse_args()
    output = args.output
    if output is None:
        parent = Path("/tmp/opencode/ssr_gap_check")
        parent.mkdir(parents=True, exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix="run_", dir=parent))
    return 0 if capture(output, args.binary.resolve()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
