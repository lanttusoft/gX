#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: texture-utility gate. GX_GFX_TEXTURE_CHECK generates four
#   procedural textures, bakes a normal map from the checker and packs an ORM
#   map from checker/noise/solid into GX_GFX_TEXTURE_DIR; the writer in the
#   editor is the same one the Texture tools panel calls.

import re
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
HOOK_RE = re.compile(
    r"GFX_TEXTURE_CHECK: (PASS|FAIL) generated=(\d+) normal=(\d+) orm=(\d+) "
    r"preview=(\d+) material=(\d+)")
GENERATED_RE = re.compile(
    r"GfxEd: texture generated kind=(\w+) size=(\d+)x(\d+)")
NORMAL_RE = re.compile(r"GfxEd: texture normal strength=")
ORM_RE = re.compile(r"GfxEd: texture orm size=")
MATERIAL_RE = re.compile(
    r"GfxEd: material brushed name=(\w+) size=(\d+)x(\d+)")
PREVIEW_RE = re.compile(r"GfxEd: texture preview file=(.+)")


def load(path):
    return np.asarray(Image.open(path).convert("RGBA"), dtype=np.int16)


def channel_delta(image, axis):
    """Mean absolute difference between the wrapped seam and interior pairs."""
    data = image[..., 0].astype(np.float32)
    if axis == 0:
        seam = np.abs(data[-1, :] - data[0, :]).mean()
        interior = np.abs(np.diff(data, axis=0)).mean()
    else:
        seam = np.abs(data[:, -1] - data[:, 0]).mean()
        interior = np.abs(np.diff(data, axis=1)).mean()
    return float(seam), float(interior)


def main():
    if not BIN.is_file():
        print("GFX_TEXTURE_CHECK: missing binary", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_texture_",
                                         dir="/tmp/opencode") as temporary:
            folder = Path(temporary) / "textures"
            folder.mkdir()
            overrides = {
                "GX_FIXED_DT": "0.0166666667",
                "GX_FRAMES": "12",
                "GX_GFX_TEXTURE_CHECK": "1",
                "GX_GFX_TEXTURE_DIR": str(folder),
            }
            environment = audit.scene_env(overrides, clean=True)
            with audit.settings_preserved():
                result = audit.run_scene("--gfx", environment, width=None,
                    height=None, fps=None, frames=12, shot_frame=None,
                    screenshot_dir=None, timeout=120, check=True,
                    extra_args=("--gfx-editor",))
            log = audit.strip_ansi(result.stdout or "")
            hook = HOOK_RE.findall(log)
            if not hook or hook[-1][0] != "PASS":
                raise RuntimeError(f"texture hook did not pass: {hook}")
            if tuple(int(value) for value in hook[-1][1:]) != (5, 1, 1, 1, 1):
                raise RuntimeError(f"unexpected hook counts: {hook[-1]}")
            generated = GENERATED_RE.findall(log)
            if len(generated) != 5:
                raise RuntimeError(f"expected 5 generated traces: {generated}")
            if [entry[0] for entry in generated] != [
                    "solid", "checker", "gradient", "noise", "value"]:
                raise RuntimeError(f"unexpected generator order: {generated}")
            material = MATERIAL_RE.findall(log)
            if len(material) != 1 or material[0][0] != "brushed":
                raise RuntimeError(f"material set trace missing: {material}")
            if not NORMAL_RE.search(log) or not ORM_RE.search(log):
                raise RuntimeError("bake/pack traces missing")
            preview = PREVIEW_RE.findall(log)
            if not preview or not preview[-1].endswith("brushed_orm.png"):
                raise RuntimeError(f"material preview trace missing: {preview}")
            if not any(path.endswith("packed_orm.png") for path in preview):
                raise RuntimeError(f"ORM preview trace missing: {preview}")
            solid = load(folder / "solid.png")
            checker = load(folder / "checker.png")
            gradient = load(folder / "gradient.png")
            noise = load(folder / "noise.png")
            value = load(folder / "value.png")
            normal = load(folder / "checker_n.png")
            orm = load(folder / "packed_orm.png")
            for name, image in (("solid", solid), ("checker", checker),
                    ("gradient", gradient), ("noise", noise),
                    ("value", value), ("normal", normal), ("orm", orm)):
                if image.shape != (64, 64, 4):
                    raise RuntimeError(f"{name} shape {image.shape}")
            if int(solid[..., :3].min()) != int(solid[..., :3].max()):
                raise RuntimeError("solid is not constant")
            if set(np.unique(checker[..., 0]).tolist()) != {51, 255}:
                raise RuntimeError("checker colors unexpected")
            if len(np.unique(gradient[..., 0])) < 8:
                raise RuntimeError("gradient does not ramp")
            if (float(noise[..., 0].std()) < 5.0
                    or int(noise[..., 0].min()) == int(noise[..., 0].max())):
                raise RuntimeError("noise is flat")
            #opencode: value noise — smooth lattice noise: non-flat, many
            #   levels, grayscale, and deterministic across runs.
            value_luma = value[..., 0].astype(np.int32)
            if (float(value_luma.std()) < 20.0
                    or len(np.unique(value_luma)) < 16
                    or int(value_luma.min()) == int(value_luma.max())):
                raise RuntimeError("value noise is flat or quantized")
            if not np.array_equal(value[..., 0], value[..., 1]) \
                    or not np.array_equal(value[..., 0], value[..., 2]):
                raise RuntimeError("value noise is not grayscale")
            if (float(normal[..., 2].mean()) < 180.0
                    or float(normal[..., 0].std()) < 1.0):
                raise RuntimeError("normal map looks wrong")
            if not np.array_equal(orm[..., 0], checker[..., 0]):
                raise RuntimeError("ORM occlusion channel does not match")
            if not np.array_equal(orm[..., 1], noise[..., 0]):
                raise RuntimeError("ORM roughness channel does not match")
            if not np.array_equal(orm[..., 2], solid[..., 0]):
                raise RuntimeError("ORM metallic channel does not match")

            #ai: brushed-metal material set — every map tiles, streaks run
            #   along one axis, ORM channels match their sources exactly, the
            #   normal is unit length and the set is deterministic.
            brushed = {}
            for name in ("albedo", "height", "roughness", "metalness", "ao",
                    "normal", "orm"):
                image = load(folder / f"brushed_{name}.png")
                if image.shape != (64, 64, 4):
                    raise RuntimeError(f"brushed {name} shape {image.shape}")
                brushed[name] = image
            for name in ("albedo", "height"):
                for axis, axis_name in ((0, "vertical"), (1, "horizontal")):
                    seam, interior = channel_delta(brushed[name], axis)
                    if seam > max(3.0 * interior, 1.5):
                        raise RuntimeError(
                            f"brushed {name} {axis_name} seam {seam:.2f} "
                            f"vs interior {interior:.2f}")
            albedo_luma = brushed["albedo"][..., :3].astype(np.float32).mean(
                axis=2)
            along_y = float(np.abs(np.diff(albedo_luma, axis=0)).mean())
            along_x = float(np.abs(np.diff(albedo_luma, axis=1)).mean())
            if along_y < 1.5 * along_x:
                raise RuntimeError(
                    f"brushed streaks are not stretched (dy {along_y:.2f}, "
                    f"dx {along_x:.2f})")
            if float(brushed["height"][..., 0].std()) < 10.0:
                raise RuntimeError("brushed height is flat")
            if not np.array_equal(brushed["orm"][..., 0],
                    brushed["ao"][..., 0]):
                raise RuntimeError("brushed ORM occlusion does not match AO")
            if not np.array_equal(brushed["orm"][..., 1],
                    brushed["roughness"][..., 0]):
                raise RuntimeError("brushed ORM roughness does not match")
            if not np.array_equal(brushed["orm"][..., 2],
                    brushed["metalness"][..., 0]):
                raise RuntimeError("brushed ORM metallic does not match")
            rough_mean = float(brushed["orm"][..., 1].mean())
            metal_mean = float(brushed["orm"][..., 2].mean())
            if not 40.0 < rough_mean < 120.0 or metal_mean < 220.0:
                raise RuntimeError(
                    f"brushed ORM ranges rough {rough_mean:.1f} "
                    f"metal {metal_mean:.1f}")
            if float(brushed["albedo"][..., 2].mean()) \
                    <= float(brushed["albedo"][..., 0].mean()):
                raise RuntimeError("brushed albedo tint was not applied")
            normal_rgb = brushed["normal"][..., :3].astype(np.float32)
            if normal_rgb[..., 2].mean() < 180.0 \
                    or normal_rgb[..., 0].std() < 1.0:
                raise RuntimeError("brushed normal looks wrong")
            decoded = normal_rgb / 127.5 - 1.0
            error = float(np.abs(np.linalg.norm(decoded, axis=2) - 1.0).mean())
            if error > 0.02:
                raise RuntimeError(f"brushed normal is not unit length: {error}")

            #opencode: determinism — a second run must reproduce the value
            #   noise (and the checker) byte-for-byte.
            repeat = Path(temporary) / "repeat"
            repeat.mkdir()
            environment = audit.scene_env({
                "GX_FIXED_DT": "0.0166666667",
                "GX_FRAMES": "12",
                "GX_GFX_TEXTURE_CHECK": "1",
                "GX_GFX_TEXTURE_DIR": str(repeat),
            }, clean=True)
            with audit.settings_preserved():
                audit.run_scene("--gfx", environment, width=None, height=None,
                    fps=None, frames=12, shot_frame=None, screenshot_dir=None,
                    timeout=120, check=True, extra_args=("--gfx-editor",))
            for name in ("value.png", "checker.png", "brushed_albedo.png",
                    "brushed_normal.png", "brushed_orm.png"):
                first = (folder / name).read_bytes()
                second = (repeat / name).read_bytes()
                if first != second:
                    raise RuntimeError(f"{name} differs across runs")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_TEXTURE_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print("GFX_TEXTURE_CHECK: PASS 5 procedural textures (incl. value noise) "
          "+ normal bake + ORM pack (channels exact) + tileable brushed-metal "
          "set (seams, streak axis, ORM channels, unit normals) + preview + "
          "determinism")
    return 0


if __name__ == "__main__":
    sys.exit(main())
