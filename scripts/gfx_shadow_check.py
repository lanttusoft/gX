#!/usr/bin/env python3
#opencode: isolated point-shadow face orientation and acne-sensitivity check.

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
WIDTH = 1024
HEIGHT = 768
LIGHT_POSITIONS = {
    "px": [4.0, 2.5, 0.0],
    "nx": [-4.0, 2.5, 0.0],
    "pz": [0.0, 2.5, 4.0],
    "nz": [0.0, 2.5, -4.0],
    #opencode: vertical probes isolate the two Y-facing cubemap array layers.
    "py": [4.0, 6.0, 1.5],
    "ny": [4.0, -4.0, 1.5],
    #opencode: exact X/Z face-boundary probes catch cubemap seam orientation.
    "pp": [2.8284271, 2.5, 2.8284271],
    "pn": [2.8284271, 2.5, -2.8284271],
    "np": [-2.8284271, 2.5, 2.8284271],
    "nn": [-2.8284271, 2.5, -2.8284271],
    #opencode: aim the predicted floor caster point directly at each X/Y and
    #   Z/Y cubemap boundary from both horizontal signs.
    "nx_ny": [1.8, 2.5, 0.0],
    "px_ny": [-1.8, 2.5, 0.0],
    "nz_ny": [0.0, 2.5, 1.8],
    "pz_ny": [0.0, 2.5, -1.8],
    #opencode: mirrored ceiling probes exercise the corresponding +Y seams.
    "nx_py": [6.2, -4.0, 0.0],
    "px_py": [-6.2, -4.0, 0.0],
    "nz_py": [0.0, -4.0, 6.2],
    "pz_py": [0.0, -4.0, -6.2],
}
Y_SEAM_AXES = (
    "nx_ny", "px_ny", "nz_ny", "pz_ny",
    "nx_py", "px_py", "nz_py", "pz_py",
)
CEILING_AXES = {"ny", "nx_py", "px_py", "nz_py", "pz_py"}
WALL_LIGHT_POSITIONS = {
    "wall_nx_ny": [1.5, 1.5, 0.0],
    "wall_px_ny": [-1.5, 1.5, 0.0],
    "wall_nx_nz": [3.0, 0.0, 2.0],
    "wall_ny_nz": [0.0, 1.0, 0.0],
}
EXPECTED_DIRECTIONS = {
    "px": (-1, 0),
    "nx": (1, 0),
    "pz": (0, -1),
    "nz": (0, 1),
}


#opencode: create temporary, schema-native floor/occluder/light presets so
#   face and acne checks never mutate shipped or user-authored scene files.
def make_fixture(template, path, axis, shadows, bias):
    scene = copy.deepcopy(template)
    cubeTemplate = next(
        model for model in scene["models"]
        if model.get("path", "").endswith("primitive/cube.glb"))
    floor = copy.deepcopy(cubeTemplate)
    box = copy.deepcopy(cubeTemplate)
    #opencode: mirror the receiver/caster vertically to expose the POS_Y layer
    #   from below, while the regular fixture exposes NEG_Y from above.
    if axis in CEILING_AXES:
        floorPosition = [0.0, 3.0, 0.0]
        boxPosition = [0.0, 2.2, 0.0]
    else:
        floorPosition = [0.0, -0.06, 0.0]
        boxPosition = [0.0, 0.7, 0.0]
    for model, position, scale, tint in (
            (floor, floorPosition, [10.0, 0.06, 10.0], [220, 220, 220, 255]),
            (box, boxPosition, [0.7, 0.7, 0.7], [255, 0, 255, 255])):
        params = model["params"]
        params.update({
            "pos": position,
            "scale": scale,
            "rotation": [0.0, 0.0, 0.0],
            "usePBR": False,
            "albedo": 1.0,
            "albedoTint": tint,
            "metalness": 0.0,
            "roughness": 0.9,
            "emissiveFactor": 0.0,
            "emissiveColor": [0, 0, 0, 255],
            "enableReflections": False,
            "planarReflection": False,
            "nonPlanarReflection": False,
            "receiveSSR": False,
            "enableBlending": False,
            "blendOpacity": 1.0,
            "enableRefraction": False,
            "hasAlbedoMap": False,
            "hasMetalnessMap": False,
            "hasNormalMap": False,
            "hasHeightMap": False,
            "hasRoughnessMap": False,
            "hasAoMap": False,
            "hasEmissiveMap": False,
            "hasGlossMap": False,
        })
    light = copy.deepcopy(scene["lights"][0])
    light.update({
        "pos": LIGHT_POSITIONS[axis],
        "dir": [0.0, -1.0, 0.0],
        "color": [255, 255, 255, 255],
        "diffuseTint": [255, 255, 255, 255],
        "specularTint": [255, 255, 255, 255],
        "brightness": 4.0,
        "type": 1,
        "diffuseFactor": 1.0,
        "specularFactor": 0.0,
        "attenConst": 1.0,
        "attenLinear": 0.04,
        "attenQuad": 0.004,
        "castShadows": True,
        "shadowSize": 512,
        "shadowFov": 90.0,
        "shadowFar": 30.0,
        "shadowRange": 0.0,
        "cookieEnabled": False,
    })
    scene["models"] = [floor, box]
    scene["lights"] = [light]
    #opencode: view the mirrored ceiling fixture from underneath its receiver.
    cameraHeight = -3.0 if axis in CEILING_AXES else 7.0
    targetHeight = 2.2 if axis in CEILING_AXES else 0.7
    if axis in Y_SEAM_AXES and axis.startswith(("nz", "pz")):
        cameraPosition = [10.0, cameraHeight, 0.0]
    else:
        cameraPosition = [0.0, cameraHeight, 10.0]
    cameraTarget = [0.0, targetHeight, 0.0]
    scene["camera"] = {
        "position": cameraPosition,
        "target": cameraTarget,
        "up": [0.0, 1.0, 0.0],
        "fovy": 45.0,
        "projection": 0,
    }
    scene["state"].update({
        "ENABLE_SHADOWS": False,
        "ENABLE_POINT_SHADOWS": bool(shadows),
        "ENABLE_REFLECTIONS": False,
        "ENABLE_SSR": False,
        "ENABLE_SSR_AUXVIEW": False,
        "ENABLE_SSAO": False,
        "ENABLE_BLOOM": False,
        "ENABLE_FXAA": False,
        "ENABLE_TAA": False,
        "ENABLE_AGX_TONEMAP": False,
        "ENABLE_SKYBOX": False,
        "ENABLE_PROCEDURAL_SKY": False,
        "ENABLE_COOKIES": False,
        "ENABLE_PARTICLES": False,
        "ENABLE_LIGHTING_AMBIENT": True,
        "ENABLE_LIGHTING_DIFFUSE": True,
        "ENABLE_LIGHTING_SPECULAR": False,
        "ENABLE_ENV_AMBIENT": False,
        "SHOW_LIGHT_MARKERS": False,
        "pointShadowBias": bias[0],
        "pointShadowExtra": bias[1],
        "pointShadowMinBias": bias[2],
    })
    path.write_text(json.dumps(scene, indent=2) + "\n")


#opencode: reuse the isolated fixture materials but receive the point shadow on
#   a vertical wall, where an angular cube-face seam visibly cuts the outline.
def make_wall_fixture(template, path, axis, shadows, bias):
    make_fixture(template, path, "px", shadows, bias)
    scene = json.loads(path.read_text())
    wall, box = scene["models"]
    wall["params"].update({"pos": [0.0, 0.0, -2.03], "scale": [8.0, 8.0, 0.06]})
    box["params"].update({"pos": [0.0, 0.0, -1.0], "scale": [0.7, 0.7, 0.7]})
    scene["lights"][0]["pos"] = WALL_LIGHT_POSITIONS[axis]
    scene["camera"] = {
        "position": [0.0, 0.0, 10.0],
        "target": [0.0, 0.0, -2.0],
        "up": [0.0, 1.0, 0.0],
        "fovy": 45.0,
        "projection": 0,
    }
    path.write_text(json.dumps(scene, indent=2) + "\n")


#opencode: -X-face mirror regression fixture. A Z-offset light over a caster
#   offset in Z puts the true shadow in the upper frame and a mirrored ghost in
#   the lower frame when the -X cube-face lookup flips its Z axis. The top-down
#   camera keeps both patches in the viewport window.
def make_mirror_fixture(template, path, shadows):
    scene = copy.deepcopy(template)
    cubeTemplate = next(
        model for model in scene["models"]
        if model.get("path", "").endswith("primitive/cube.glb"))
    floor = copy.deepcopy(cubeTemplate)
    box = copy.deepcopy(cubeTemplate)
    for model, position, scale, tint in (
            (floor, [0.0, -0.06, 0.0], [10.0, 0.06, 10.0],
             [220, 220, 220, 255]),
            (box, [-1.5, 1.0, -1.0], [0.6, 2.0, 0.6], [255, 0, 255, 255])):
        params = model["params"]
        params.update({
            "pos": position,
            "scale": scale,
            "rotation": [0.0, 0.0, 0.0],
            "usePBR": False,
            "albedo": 1.0,
            "albedoTint": tint,
            "metalness": 0.0,
            "roughness": 0.9,
            "emissiveFactor": 0.0,
            "emissiveColor": [0, 0, 0, 255],
            "enableReflections": False,
            "planarReflection": False,
            "nonPlanarReflection": False,
            "receiveSSR": False,
            "enableBlending": False,
            "blendOpacity": 1.0,
            "hasAlbedoMap": False,
            "hasMetalnessMap": False,
            "hasNormalMap": False,
            "hasHeightMap": False,
            "hasRoughnessMap": False,
            "hasAoMap": False,
            "hasEmissiveMap": False,
            "hasGlossMap": False,
        })
    light = copy.deepcopy(scene["lights"][0])
    light.update({
        "pos": [2.0, 4.0, 0.0],
        "dir": [0.0, -1.0, 0.0],
        "color": [255, 255, 255, 255],
        "diffuseTint": [255, 255, 255, 255],
        "specularTint": [255, 255, 255, 255],
        "brightness": 4.0,
        "type": 1,
        "diffuseFactor": 1.0,
        "specularFactor": 0.0,
        "attenConst": 1.0,
        "attenLinear": 0.04,
        "attenQuad": 0.004,
        "castShadows": True,
        "shadowSize": 512,
        "shadowFov": 90.0,
        "shadowFar": 30.0,
        "shadowRange": 0.0,
        "cookieEnabled": False,
    })
    scene["models"] = [floor, box]
    scene["lights"] = [light]
    scene["camera"] = {
        "position": [0.0, 12.0, 0.02],
        "target": [0.0, 0.0, 0.0],
        "up": [0.0, 1.0, 0.0],
        "fovy": 50.0,
        "projection": 0,
    }
    scene["state"].update({
        "ENABLE_SHADOWS": False,
        "ENABLE_POINT_SHADOWS": bool(shadows),
        "ENABLE_REFLECTIONS": False,
        "ENABLE_SSR": False,
        "ENABLE_SSAO": False,
        "ENABLE_BLOOM": False,
        "ENABLE_FXAA": False,
        "ENABLE_TAA": False,
        "ENABLE_AGX_TONEMAP": False,
        "ENABLE_SKYBOX": False,
        "ENABLE_PROCEDURAL_SKY": False,
        "ENABLE_COOKIES": False,
        "ENABLE_PARTICLES": False,
        "ENABLE_LIGHTING_AMBIENT": True,
        "ENABLE_LIGHTING_DIFFUSE": True,
        "ENABLE_LIGHTING_SPECULAR": False,
        "ENABLE_ENV_AMBIENT": False,
        "SHOW_LIGHT_MARKERS": False,
        "pointShadowBias": 0.05,
        "pointShadowExtra": 0.01,
        "pointShadowMinBias": 0.04,
    })
    path.write_text(json.dumps(scene, indent=2) + "\n")


#opencode: the true shadow sits above the ghost band; a mirrored -X lookup
#   lands a second wedge in the lower frame. Counting the lower-band mask is a
#   ~100x discriminator (fixed 87 px vs mirrored 9199 px on the calibration).
def mirror_shadow_response(offPath, onPath):
    off = np.asarray(Image.open(offPath).convert("RGB"), dtype=np.float32)
    on = np.asarray(Image.open(onPath).convert("RGB"), dtype=np.float32)
    if off.shape != on.shape:
        raise RuntimeError(f"mirror: capture size mismatch {off.shape} vs {on.shape}")
    height, width = off.shape[:2]
    yGrid, xGrid = np.indices(off.shape[:2])
    viewport = ((xGrid >= width * 0.235) & (xGrid < width * 0.72)
                & (yGrid >= height * 0.05) & (yGrid < height * 0.97))
    delta = off.mean(axis=2) - on.mean(axis=2)
    mask = (delta > 15.0) & viewport
    total = int(mask.sum())
    lower = int((mask & (yGrid > height * 0.55)).sum())
    return {
        "ok": total >= 10000 and lower <= 1500,
        "total": total,
        "lower": lower,
    }


#opencode: each capture owns its screenshot directory; one transient app
#   launch failure is retried before the fixture is reported as failed.
def capture(name, preset, output, frame):
    screenshotRoot = output / "raw"
    screenshotRoot.mkdir(parents=True, exist_ok=True)
    command = [
        str(BIN), "--gfx", "--gfx-editor",
        "--screenWidth", str(WIDTH),
        "--screenHeight", str(HEIGHT),
        "--targetFPS", "60",
        "--ai-screenshot-after", str(frame),
    ]
    errors = []
    #opencode: retry only a failed editor launch; each attempt owns a fresh
    #   capture path so a crash or stale PNG cannot satisfy a later attempt.
    for attempt in range(2):
        screenshotDir = Path(tempfile.mkdtemp(
            prefix=f"{name}_", dir=screenshotRoot))
        environment = audit.scene_env({
            "GX_FIXED_DT": "0.0166666667",
            "GX_GFX_PRESET_LOAD": str(preset),
            "GX_SHOT_FRAME": str(frame),
            "GX_SCREENSHOT_DIR": str(screenshotDir),
        }, clean=True)
        try:
            result = subprocess.run(
                command, cwd=ROOT, env=environment, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
        except subprocess.TimeoutExpired as error:
            outputText = error.stdout or ""
            if isinstance(outputText, bytes):
                outputText = outputText.decode(errors="replace")
            errors.append(f"attempt {attempt + 1} timed out\n{outputText[-2000:]}")
            shutil.rmtree(screenshotDir, ignore_errors=True)
            continue
        if result.returncode != 0:
            errors.append(
                f"attempt {attempt + 1} failed ({result.returncode})\n"
                f"{result.stdout[-2000:]}")
            shutil.rmtree(screenshotDir, ignore_errors=True)
            continue
        screenshots = list(screenshotDir.glob("gXss_*.png"))
        if screenshots:
            return max(screenshots, key=lambda path: path.stat().st_mtime)
        errors.append(f"attempt {attempt + 1} screenshot missing\n{result.stdout[-2000:]}")
        shutil.rmtree(screenshotDir, ignore_errors=True)
    raise RuntimeError(f"{name}: " + "\n".join(errors))


#opencode: locate the magenta occluder and check that each point-light
#   shadow's measured direction agrees with its world-space light axis.
def directional_response(axis, offPath, onPath):
    off = np.asarray(Image.open(offPath).convert("RGB"), dtype=np.float32)
    on = np.asarray(Image.open(onPath).convert("RGB"), dtype=np.float32)
    if off.shape != on.shape:
        raise RuntimeError(f"{axis}: capture size mismatch {off.shape} vs {on.shape}")
    height, width = off.shape[:2]
    yGrid, xGrid = np.indices(off.shape[:2])
    viewport = ((xGrid >= width * 0.235) & (xGrid < width * 0.72)
                & (yGrid >= height * 0.05) & (yGrid < height * 0.85))
    magenta = (viewport & (off[:, :, 0] > off[:, :, 1] * 1.35)
               & (off[:, :, 2] > off[:, :, 1] * 1.35))
    magenta |= (viewport & (on[:, :, 0] > on[:, :, 1] * 1.35)
                & (on[:, :, 2] > on[:, :, 1] * 1.35))
    ys, xs = np.where(magenta)
    if len(xs) < 500:
        raise RuntimeError(f"{axis}: magenta receiver model not visible ({len(xs)} px)")
    anchor = (float(np.median(xs)), float(np.median(ys)))
    delta = off.mean(axis=2) - on.mean(axis=2)
    inScene = ((xGrid >= width * 0.235) & (xGrid < width * 0.72)
               & (yGrid >= height * 0.40) & (yGrid < height * 0.97)
               & ~magenta)
    shadow = (delta > 15.0) & inScene
    shadowCount = int(shadow.sum())
    if shadowCount < 1000:
        raise RuntimeError(f"{axis}: point shadows changed only {shadowCount} pixels")
    weights = np.maximum(delta[shadow], 0.0)
    centerX = float((xGrid[shadow] * weights).sum() / weights.sum())
    centerY = float((yGrid[shadow] * weights).sum() / weights.sum())
    offsetX = centerX - anchor[0]
    offsetY = centerY - anchor[1]
    expectedX, expectedY = EXPECTED_DIRECTIONS[axis]
    aligned = ((expectedX == 0 or offsetX * expectedX > 10.0)
               and (expectedY == 0 or offsetY * expectedY > 10.0))
    return {
        "ok": aligned,
        "shadow_pixels": shadowCount,
        "anchor": anchor,
        "shadow_centroid": (centerX, centerY),
        "offset": (offsetX, offsetY),
    }


#opencode: second-difference energy measures the floor's shadow acne signal.
def high_frequency_energy(imagePath):
    image = np.asarray(Image.open(imagePath).convert("RGB"), dtype=np.float32).mean(axis=2)
    height, width = image.shape
    region = image[int(height * 0.68):int(height * 0.97),
                   int(width * 0.26):int(width * 0.72)]
    secondX = np.abs(region[:, 2:] - 2.0 * region[:, 1:-1] + region[:, :-2]).mean()
    secondY = np.abs(region[2:] - 2.0 * region[1:-1] + region[:-2]).mean()
    return float((secondX + secondY) * 0.5)


#opencode: diagonal lights project the box shadow onto four known floor patches.
#   Local probes avoid the broad acne pattern biasing a whole-floor centroid.
def diagonal_shadow_response(axis, offPath, onPath):
    off = np.asarray(Image.open(offPath).convert("RGB"), dtype=np.float32)
    on = np.asarray(Image.open(onPath).convert("RGB"), dtype=np.float32)
    if off.shape != on.shape:
        raise RuntimeError(f"{axis}: capture size mismatch {off.shape} vs {on.shape}")
    height, width = off.shape[:2]
    magenta = ((off[:, :, 0] > off[:, :, 1] * 1.35)
               & (off[:, :, 2] > off[:, :, 1] * 1.35))
    magenta |= ((on[:, :, 0] > on[:, :, 1] * 1.35)
                & (on[:, :, 2] > on[:, :, 1] * 1.35))
    ys, xs = np.where(magenta)
    if len(xs) < 500:
        raise RuntimeError(f"{axis}: magenta receiver model not visible ({len(xs)} px)")
    anchorX = float(np.median(xs))
    anchorY = float(np.median(ys))

    #opencode: project each vertical probe with the matching mirrored camera
    #   and receiver plane so the expected cast patch is independent of face.
    if axis in CEILING_AXES:
        camera = np.array([0.0, -3.0, 10.0], dtype=np.float32)
        target = np.array([0.0, 2.2, 0.0], dtype=np.float32)
        boxCenter = np.array([0.0, 2.2, 0.0], dtype=np.float32)
        receiverY = 2.94
    else:
        camera = np.array([0.0, 7.0, 10.0], dtype=np.float32)
        target = np.array([0.0, 0.7, 0.0], dtype=np.float32)
        boxCenter = np.array([0.0, 0.7, 0.0], dtype=np.float32)
        receiverY = 0.0
    if axis in Y_SEAM_AXES and axis.startswith(("nz", "pz")):
        camera[0], camera[2] = 10.0, 0.0
    forward = target - camera
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, np.array([0.0, 1.0, 0.0], dtype=np.float32))
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    focal = 1.0 / np.tan(np.deg2rad(45.0) * 0.5)

    def projected(point):
        relative = point - camera
        depth = float(np.dot(relative, forward))
        return (float(np.dot(relative, right)) / depth,
                float(np.dot(relative, up)) / depth)

    lightPosition = np.asarray(LIGHT_POSITIONS[axis], dtype=np.float32)
    floorScale = ((receiverY - lightPosition[1])
                  / (boxCenter[1] - lightPosition[1]))
    floorShadow = lightPosition + floorScale * (boxCenter - lightPosition)
    boxProjection = projected(boxCenter)
    shadowProjection = projected(floorShadow)
    pixelScale = height * focal * 0.5
    expectedX = anchorX + (shadowProjection[0] - boxProjection[0]) * pixelScale
    expectedY = anchorY - (shadowProjection[1] - boxProjection[1]) * pixelScale
    oppositeX = anchorX - (expectedX - anchorX)
    oppositeY = anchorY - (expectedY - anchorY)
    delta = off.mean(axis=2) - on.mean(axis=2)
    radius = max(24, int(round(height * 0.0586)))

    def local_response(centerX, centerY):
        left = max(0, int(round(centerX)) - radius)
        right = min(width, int(round(centerX)) + radius + 1)
        top = max(0, int(round(centerY)) - radius)
        bottom = min(height, int(round(centerY)) + radius + 1)
        region = delta[top:bottom, left:right]
        valid = ~magenta[top:bottom, left:right]
        values = region[valid]
        if values.size == 0:
            return 0.0, 0.0
        return float(values.mean()), float((values > 15.0).mean())

    expectedMean, expectedFraction = local_response(expectedX, expectedY)
    oppositeMean, _ = local_response(oppositeX, oppositeY)
    return {
        "ok": expectedMean >= 20.0 and expectedFraction >= 0.15
            and expectedMean >= oppositeMean + 10.0,
        "expected_center": (expectedX, expectedY),
        "expected_mean": expectedMean,
        "expected_fraction": expectedFraction,
        "opposite_mean": oppositeMean,
    }


#opencode: predict the caster projection onto the front wall and verify the
#   point-shadow response stays continuous across the selected cube-face seam.
def wall_shadow_response(axis, offPath, onPath):
    off = np.asarray(Image.open(offPath).convert("RGB"), dtype=np.float32)
    on = np.asarray(Image.open(onPath).convert("RGB"), dtype=np.float32)
    if off.shape != on.shape:
        raise RuntimeError(f"{axis}: capture size mismatch {off.shape} vs {on.shape}")
    height, width = off.shape[:2]
    yGrid, xGrid = np.indices(off.shape[:2])
    viewport = ((xGrid >= width * 0.235) & (xGrid < width * 0.72)
                & (yGrid >= height * 0.05) & (yGrid < height * 0.90))
    magenta = (viewport & (off[:, :, 0] > off[:, :, 1] * 1.35)
               & (off[:, :, 2] > off[:, :, 1] * 1.35))
    magenta |= (viewport & (on[:, :, 0] > on[:, :, 1] * 1.35)
                & (on[:, :, 2] > on[:, :, 1] * 1.35))
    ys, xs = np.where(magenta)
    if len(xs) < 500:
        raise RuntimeError(f"{axis}: magenta wall caster not visible ({len(xs)} px)")
    anchorX = float(np.median(xs))
    anchorY = float(np.median(ys))
    camera = np.array([0.0, 0.0, 10.0], dtype=np.float32)
    target = np.array([0.0, 0.0, -2.0], dtype=np.float32)
    forward = target - camera
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, np.array([0.0, 1.0, 0.0], dtype=np.float32))
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    focal = 1.0 / np.tan(np.deg2rad(45.0) * 0.5)

    def projected(point):
        relative = point - camera
        depth = float(np.dot(relative, forward))
        return (float(np.dot(relative, right)) / depth,
                float(np.dot(relative, up)) / depth)

    lightPosition = np.asarray(WALL_LIGHT_POSITIONS[axis], dtype=np.float32)
    boxCenter = np.array([0.0, 0.0, -1.0], dtype=np.float32)
    wallZ = -2.0
    distance = (wallZ - lightPosition[2]) / (boxCenter[2] - lightPosition[2])
    wallShadow = lightPosition + distance * (boxCenter - lightPosition)
    boxProjection = projected(boxCenter)
    shadowProjection = projected(wallShadow)
    pixelScale = height * focal * 0.5
    expectedX = anchorX + (shadowProjection[0] - boxProjection[0]) * pixelScale
    expectedY = anchorY - (shadowProjection[1] - boxProjection[1]) * pixelScale
    oppositeX = anchorX - (expectedX - anchorX)
    oppositeY = anchorY - (expectedY - anchorY)
    delta = off.mean(axis=2) - on.mean(axis=2)
    radius = max(24, int(round(height * 0.0586)))

    def local_response(centerX, centerY):
        left = max(0, int(round(centerX)) - radius)
        rightEdge = min(width, int(round(centerX)) + radius + 1)
        top = max(0, int(round(centerY)) - radius)
        bottom = min(height, int(round(centerY)) + radius + 1)
        region = delta[top:bottom, left:rightEdge]
        valid = ~magenta[top:bottom, left:rightEdge]
        values = region[valid]
        if values.size == 0:
            return 0.0, 0.0
        return float(values.mean()), float((values > 15.0).mean())

    expectedMean, expectedFraction = local_response(expectedX, expectedY)
    oppositeMean, _ = local_response(oppositeX, oppositeY)
    return {
        "ok": expectedMean >= 20.0 and expectedFraction >= 0.70
            and expectedMean >= oppositeMean + 10.0,
        "expected_center": (expectedX, expectedY),
        "expected_mean": expectedMean,
        "expected_fraction": expectedFraction,
        "opposite_mean": oppositeMean,
    }


#opencode: count broad floor darkening, explicitly including both cast shadows
#   and acne; this diagnostic is not a physical caster-coverage measurement.
def shadow_difference_mask(offPath, onPath):
    off = np.asarray(Image.open(offPath).convert("RGB"), dtype=np.float32)
    on = np.asarray(Image.open(onPath).convert("RGB"), dtype=np.float32)
    if off.shape != on.shape:
        raise RuntimeError(f"capture size mismatch {off.shape} vs {on.shape}")
    height, width = off.shape[:2]
    yGrid, xGrid = np.indices(off.shape[:2])
    magenta = ((off[:, :, 0] > off[:, :, 1] * 1.35)
               & (off[:, :, 2] > off[:, :, 1] * 1.35))
    magenta |= ((on[:, :, 0] > on[:, :, 1] * 1.35)
                & (on[:, :, 2] > on[:, :, 1] * 1.35))
    floorRegion = ((xGrid >= width * 0.235) & (xGrid < width * 0.72)
                   & (yGrid >= height * 0.40) & (yGrid < height * 0.97)
                   & ~magenta)
    delta = off.mean(axis=2) - on.mean(axis=2)
    return (delta > 15.0) & floorRegion


def main():
    parser = argparse.ArgumentParser(description="GFX point-light shadow checks")
    parser.add_argument("--jobs", type=int, default=2,
                        help="parallel isolated GPU captures (default: 2)")
    parser.add_argument("--frame", type=int, default=120,
                        help="capture frame (default: 120)")
    parser.add_argument("--output", default="/tmp/opencode/gfx_shadow_check",
                        help="capture output directory")
    args = parser.parse_args()
    if args.jobs < 1 or args.jobs > 8:
        parser.error("--jobs must be between 1 and 8")
    if args.frame < 1:
        parser.error("--frame must be positive")
    if not BIN.is_file():
        print(f"GFX_SHADOW_CHECK: binary missing: {BIN}", file=sys.stderr)
        return 1

    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    template = json.loads((ROOT / "resources/tools/gfx_editor.json").read_text())
    cases = {}
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_shadow_", dir="/tmp/opencode") as temp:
            fixtureDir = Path(temp)
            for axis in LIGHT_POSITIONS:
                for enabled in (False, True):
                    name = f"{axis}_{'on' if enabled else 'off'}"
                    preset = fixtureDir / f"{name}.json"
                    #opencode: face-boundary and Y-face probes use current defaults.
                    bias = (0.015, 0.005, 0.01) if axis in (
                        "pp", "pn", "np", "nn", "py", "ny", *Y_SEAM_AXES) \
                        else (0.04, 0.01, 0.04)
                    make_fixture(template, preset, axis, enabled, bias)
                    cases[name] = preset
            for axis in WALL_LIGHT_POSITIONS:
                for enabled in (False, True):
                    name = f"{axis}_{'on' if enabled else 'off'}"
                    preset = fixtureDir / f"{name}.json"
                    make_wall_fixture(
                        template, preset, axis, enabled, (0.015, 0.005, 0.01))
                    cases[name] = preset
            for enabled in (False, True):
                name = f"mirror_{'on' if enabled else 'off'}"
                preset = fixtureDir / f"{name}.json"
                make_mirror_fixture(template, preset, enabled)
                cases[name] = preset
            lowBiasPreset = fixtureDir / "px_low_bias.json"
            make_fixture(template, lowBiasPreset, "px", True, (0.015, 0.005, 0.01))
            cases["px_low_bias"] = lowBiasPreset
            #opencode: candidate A/B doubles only the base bias; production
            #   defaults remain untouched until the local caster check passes.
            baseBiasPreset = fixtureDir / "px_base_bias_2x.json"
            make_fixture(template, baseBiasPreset, "px", True, (0.03, 0.005, 0.01))
            cases["px_base_bias_2x"] = baseBiasPreset
            captures = {}
            print(f"captures: {len(cases)} shots, {args.jobs} workers, "
                  f"{WIDTH}x{HEIGHT}@60, frame={args.frame}, output={output}")
            with ThreadPoolExecutor(max_workers=args.jobs) as executor:
                futures = {
                    executor.submit(capture, name, preset, output, args.frame): name
                    for name, preset in cases.items()
                }
                for future in as_completed(futures):
                    captures[futures[future]] = future.result()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"GFX_SHADOW_CHECK: FAIL: {error}", file=sys.stderr)
        return 1

    failures = []
    print("GFX_SHADOW_CHECK: point-shadow face directions")
    for axis in ("px", "nx", "pz", "nz"):
        try:
            result = directional_response(axis, captures[f"{axis}_off"], captures[f"{axis}_on"])
            offsetX, offsetY = result["offset"]
            status = "PASS" if result["ok"] else "FAIL"
            print(f"  {axis.upper():>2}: shadow pixels={result['shadow_pixels']:>7} "
                  f"offset=({offsetX:>7.1f},{offsetY:>7.1f}) {status}")
            if not result["ok"]:
                failures.append(axis)
        except (OSError, RuntimeError, ValueError) as error:
            print(f"  {axis.upper():>2}: FAIL: {error}")
            failures.append(axis)

    print("GFX_SHADOW_CHECK: X/Z cubemap seams at current default bias")
    for axis in ("pp", "pn", "np", "nn"):
        try:
            result = diagonal_shadow_response(
                axis, captures[f"{axis}_off"], captures[f"{axis}_on"])
            centerX, centerY = result["expected_center"]
            status = "PASS" if result["ok"] else "FAIL"
            print(f"  {axis.upper():>2}: expected=({centerX:>7.1f},{centerY:>7.1f}) "
                  f"local={result['expected_mean']:>6.1f}/{result['expected_fraction']:.2f} "
                  f"opposite={result['opposite_mean']:>6.1f} {status}")
            if not result["ok"]:
                failures.append(axis)
        except (OSError, RuntimeError, ValueError) as error:
            print(f"  {axis.upper():>2}: FAIL: {error}")
            failures.append(axis)

    print("GFX_SHADOW_CHECK: point-shadow outlines on vertical walls at cube seams")
    for axis in WALL_LIGHT_POSITIONS:
        try:
            result = wall_shadow_response(
                axis, captures[f"{axis}_off"], captures[f"{axis}_on"])
            centerX, centerY = result["expected_center"]
            status = "PASS" if result["ok"] else "FAIL"
            print(f"  {axis.upper():>12}: expected=({centerX:>7.1f},{centerY:>7.1f}) "
                  f"local={result['expected_mean']:>6.1f}/{result['expected_fraction']:.2f} "
                  f"opposite={result['opposite_mean']:>6.1f} {status}")
            if not result["ok"]:
                failures.append(axis)
        except (OSError, RuntimeError, ValueError) as error:
            print(f"  {axis.upper():>12}: FAIL: {error}")
            failures.append(axis)

    print("GFX_SHADOW_CHECK: X/Y and Z/Y cubemap seams at current default bias")
    for axis in Y_SEAM_AXES:
        try:
            result = diagonal_shadow_response(
                axis, captures[f"{axis}_off"], captures[f"{axis}_on"])
            centerX, centerY = result["expected_center"]
            status = "PASS" if result["ok"] else "FAIL"
            print(f"  {axis.upper():>5}: expected=({centerX:>7.1f},{centerY:>7.1f}) "
                  f"local={result['expected_mean']:>6.1f}/{result['expected_fraction']:.2f} "
                  f"opposite={result['opposite_mean']:>6.1f} {status}")
            if not result["ok"]:
                failures.append(axis)
        except (OSError, RuntimeError, ValueError) as error:
            print(f"  {axis.upper():>5}: FAIL: {error}")
            failures.append(axis)

    #opencode: the overhead and under-ceiling fixtures isolate -Y and +Y.
    print("GFX_SHADOW_CHECK: isolated Y cubemap faces at current default bias")
    for axis in ("py", "ny"):
        try:
            result = diagonal_shadow_response(
                axis, captures[f"{axis}_off"], captures[f"{axis}_on"])
            centerX, centerY = result["expected_center"]
            face = "NEG_Y" if axis == "py" else "POS_Y"
            status = "PASS" if result["ok"] else "FAIL"
            print(f"  {face:>5}: expected=({centerX:>7.1f},{centerY:>7.1f}) "
                  f"local={result['expected_mean']:>6.1f}/{result['expected_fraction']:.2f} "
                  f"opposite={result['opposite_mean']:>6.1f} {status}")
            if not result["ok"]:
                failures.append(axis)
        except (OSError, RuntimeError, ValueError) as error:
            print(f"  {axis.upper():>5}: FAIL: {error}")
            failures.append(axis)

    print("GFX_SHADOW_CHECK: -X cube-face mirror regression (single light)")
    try:
        result = mirror_shadow_response(
            captures["mirror_off"], captures["mirror_on"])
        status = "PASS" if result["ok"] else "FAIL"
        print(f"  MIRROR: total={result['total']:>7} lower-band={result['lower']:>6} "
              f"{status}")
        if not result["ok"]:
            failures.append("mirror")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"  MIRROR: FAIL: {error}")
        failures.append("mirror")

    try:
        lowEnergy = high_frequency_energy(captures["px_low_bias"])
        baseEnergy = high_frequency_energy(captures["px_base_bias_2x"])
        highEnergy = high_frequency_energy(captures["px_on"])
        baseRatio = baseEnergy / max(lowEnergy, 0.000001)
        ratio = highEnergy / max(lowEnergy, 0.000001)
        defaultMask = shadow_difference_mask(captures["px_off"], captures["px_low_bias"])
        baseMask = shadow_difference_mask(captures["px_off"], captures["px_base_bias_2x"])
        highMask = shadow_difference_mask(captures["px_off"], captures["px_on"])
        baseIntersection = int((defaultMask & baseMask).sum())
        baseUnion = int((defaultMask | baseMask).sum())
        baseIoU = baseIntersection / max(baseUnion, 1)
        highIntersection = int((defaultMask & highMask).sum())
        highUnion = int((defaultMask | highMask).sum())
        highIoU = highIntersection / max(highUnion, 1)
        defaultCaster = diagonal_shadow_response(
            "px", captures["px_off"], captures["px_low_bias"])
        baseCaster = diagonal_shadow_response(
            "px", captures["px_off"], captures["px_base_bias_2x"])
        print(f"GFX_SHADOW_CHECK: bias sweep high-frequency energy "
              f"default={lowEnergy:.3f} base-2x={baseEnergy:.3f} "
              f"combined-high={highEnergy:.3f}")
        print(f"GFX_SHADOW_CHECK: floor difference mask (includes acne) "
              f"default={int(defaultMask.sum())} base-2x={int(baseMask.sum())} "
              f"IoU={baseIoU:.3f}; combined-high={int(highMask.sum())} "
              f"IoU={highIoU:.3f}")
        print(f"GFX_SHADOW_CHECK: predicted caster patch "
              f"default={defaultCaster['expected_mean']:.2f}/"
              f"{defaultCaster['expected_fraction']:.2f} "
              f"base-2x={baseCaster['expected_mean']:.2f}/"
              f"{baseCaster['expected_fraction']:.2f} "
              f"opposite={baseCaster['opposite_mean']:.2f}")
        if baseRatio >= 0.80:
            failures.append("base-bias")
            print("GFX_SHADOW_CHECK: doubled base bias did not reduce acne detail")
        if (not defaultCaster["ok"] or not baseCaster["ok"]
                or baseCaster["expected_fraction"]
                    < defaultCaster["expected_fraction"] * 0.90
                or baseCaster["expected_mean"] < defaultCaster["expected_mean"] * 0.80):
            failures.append("base-bias-caster")
            print("GFX_SHADOW_CHECK: doubled base bias lost the predicted cast shadow")
        if ratio >= 0.80:
            failures.append("bias")
            print("GFX_SHADOW_CHECK: bias sweep did not reduce shadow acne energy")
    except (OSError, RuntimeError, ValueError) as error:
        failures.append("bias")
        print(f"GFX_SHADOW_CHECK: bias sweep FAIL: {error}")

    if failures:
        print(f"GFX_SHADOW_CHECK: FAIL — {', '.join(failures)}")
        return 1
    print("GFX_SHADOW_CHECK: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
