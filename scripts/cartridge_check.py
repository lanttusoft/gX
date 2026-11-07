#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: Y2K cartridge admission gate. Boots the fixed console profile and pins
#   its presentation (320x240 soft), Max-quality cascade, textured low-poly
#   scene, diffraction contribution and half-res star-map target.

import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


SCRATCH = audit.SCRATCH / "cartridge_check"
VIRTUAL_RE = re.compile(
    r"MAIN: virtual=(\d+)x(\d+) window=(\d+)x(\d+) render=(\d+)x(\d+) "
    r"filter=(\d+)")
CARTRIDGE_RE = re.compile(
    r"CARTRIDGE: y2k diffraction=(\d+) blades=(\d+) quality=(\d+)")
SCENE_RE = re.compile(
    r"SHOWCASE-Y2K: models=(\d+) lights=(\d+) cartridge=(\d+) textured=(\d+)")
QUALITY_RE = re.compile(r"Quality preset (\d+) applied .*msaa=(\d+)")
MSAA_RE = re.compile(r"MSAA FBO created: (\d+)x(\d+) msaa=(\d+)")
DIFFRACTION_RE = re.compile(
    r"diffraction: threshold=([\d.]+) knee=([\d.]+) intensity=([\d.]+) "
    r"length=([\d.]+) chromatic=([\d.]+) blades=(\d+) taps=(\d+) "
    r"map=(\d+)x(\d+)")
BUFFER_CLEAR_RE = re.compile(
    r"pass=diffraction op=clear target=diffraction\.map .* size=(\d+)x(\d+)")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def capture(tag, environment, out):
    directory = out / tag
    directory.mkdir(parents=True, exist_ok=True)
    for old in directory.glob("*.png"):
        old.unlink()
    result = audit.run_scene(
        "--cartridge-y2k", environment, width=1024, height=768, frames=120,
        shot_frame=100, screenshot_dir=directory, timeout=180, check=True)
    log = audit.strip_ansi(result.stdout or "")
    shot = audit.newest_shot(directory)
    require(shot is not None, f"{tag}: no screenshot")
    image = audit.load_rgb(shot)
    return image, log


def main():
    if not audit.BIN.is_file():
        print("CARTRIDGE: missing binary", file=sys.stderr)
        return 1
    out = SCRATCH
    out.mkdir(parents=True, exist_ok=True)
    audit_file = out / "buffer_audit.txt"
    if audit_file.exists():
        audit_file.unlink()
    try:
        #ai: the cartridge ships a 128 px set; missing or oversized maps are
        #   an asset-profile regression even when the shader still samples.
        for name in ("albedo", "normal", "orm"):
            path = audit.ROOT / "resources/texture/y2k" / (
                f"brushed_{name}.png")
            require(path.is_file(), f"missing cartridge map {path.name}")
            with Image.open(path) as image:
                require(image.size == (128, 128),
                    f"{path.name} is {image.size[0]}x{image.size[1]}")
        base = {
            "GX_WINDOW_TRACE": "1",
            "GX_FIXED_DT": "%.12f" % (1.0 / 60.0),
            "GX_GFX_BUFFER_AUDIT": str(audit_file),
            #ai: the shipped console profile has no TAA; pin it so the
            #   diffraction A/B is measured on the product configuration.
            "GX_T_TAA": "0",
            "GX_GFX_PLAN": "1",
        }
        with audit.settings_preserved():
            image, log = capture("on", base, out)
            control_env = {**base, "GX_CARTRIDGE_DIFFRACTION": "0"}
            control_env.pop("GX_GFX_BUFFER_AUDIT")
            control, control_log = capture("off", control_env, out)

        virtual = VIRTUAL_RE.findall(log)
        require(virtual, "virtual-screen trace missing")
        vw, vh, _ww, _wh, rw, rh, filter_value = (
            int(value) for value in virtual[-1])
        require((vw, vh) == (320, 240), f"cartridge virtual {vw}x{vh}")
        require((rw, rh) == (320, 240), f"cartridge render {rw}x{rh}")
        require(filter_value == 1, f"cartridge filter {filter_value}")

        cartridge = CARTRIDGE_RE.findall(log)
        require(cartridge, "cartridge trace missing")
        enabled, blades, quality = (int(value) for value in cartridge[-1])
        require((enabled, blades, quality) == (1, 6, 3),
            f"cartridge profile {enabled}/{blades}/{quality}")

        scene = SCENE_RE.findall(log)
        require(scene, "Y2K scene trace missing")
        models, lights, cartridge_flag, textured = (
            int(value) for value in scene[-1])
        require(models >= 6 and lights >= 4,
            f"cartridge scene too small: {models}/{lights}")
        require((cartridge_flag, textured) == (1, 1),
            "cartridge did not bind the authored material set")
        require("SHOWCASE-Y2K: mirror planar=1 receiveSSR=0" in log,
            "Y2K mirror floor did not take the single planar reflection path")
        require("GFX_PLAN feature=planar-reflections" in log,
            "Y2K planar reflection pass missing")
        require("GFX_PLAN feature=ssr" not in log,
            "Y2K cartridge re-enabled SSR (post-bloom reflection/glow overwrite)")

        require("Console Max quality applied (scene features preserved)" in log,
            "Max quality did not apply")
        quality_rows = QUALITY_RE.findall(log)
        require(quality_rows, "quality-preset trace missing")
        require(any(preset == "3" and msaa == "4"
            for preset, msaa in quality_rows), "quality 3/4x MSAA missing")
        msaa = MSAA_RE.findall(log)
        require(any(width == "320" and height == "240" and samples == "4"
            for width, height, samples in msaa),
            f"320x240 4x MSAA missing: {msaa}")

        diffraction = DIFFRACTION_RE.findall(log)
        require(diffraction, "diffraction parameter trace missing")
        threshold, _knee, _intensity, _length, _chromatic, diff_blades, \
            taps, map_width, map_height = diffraction[-1]
        require(float(threshold) >= 1.0, f"cartridge threshold {threshold}")
        require(int(diff_blades) == 6 and int(taps) == 12,
            "cartridge diffraction shape drifted")
        require((int(map_width), int(map_height)) == (160, 120),
            f"star map {map_width}x{map_height}")

        require("CARTRIDGE: y2k diffraction=0" in control_log,
            "diffraction control did not disable the pass")
        require("[ ERROR ]" not in log and "[ ERROR ]" not in control_log,
            "cartridge log contains errors")

        control = control.astype(np.float32)
        image = image.astype(np.float32)
        luma = audit.luma(image)
        require(15.0 < float(luma.mean()) < 110.0,
            f"cartridge mean luma {luma.mean():.1f}")
        require(float(luma.std()) > 8.0,
            f"cartridge image is flat ({luma.std():.1f})")
        require(len(np.unique(image.astype(np.uint8))) > 32,
            "cartridge image lost its palette")
        delta = np.abs(image - control)
        #ai: TAA off (the cartridge default) no longer multiplies the small
        #   spikes; the measured mean is ~0.086 with max >32 and >0.002 of
        #   pixels changed, so 0.07 still separates the pass from a no-op.
        require(float(delta.mean()) > 0.07,
            f"diffraction did not change the frame ({delta.mean():.3f})")
        changed = float((delta.mean(axis=2) > 8.0).mean())
        require(changed > 0.002, "diffraction changed too few pixels")
        require(int(delta.max()) > 32,
            "diffraction only shifted sub-visible values")

        require(audit_file.is_file(), "buffer audit missing")
        audit_text = audit_file.read_text()
        clears = BUFFER_CLEAR_RE.findall(audit_text)
        require(any((int(width), int(height)) == (160, 120)
            for width, height in clears),
            "diffraction.map is not a 160x120 half-res target")
        require("pass=diffraction" in audit_text,
            "diffraction pass missing from the buffer audit")
        require(not any("pass=diffraction" in line and "feedback=1" in line
            for line in audit_text.splitlines()),
            "diffraction pass reads its own target")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"CARTRIDGE: FAIL — {error}", file=sys.stderr)
        return 1
    print("CARTRIDGE: PASS y2k boot, 320x240 soft, 4x MSAA, textured low-poly "
          "scene, Max quality, diffraction contribution "
          f"{changed * 100.0:.1f}% of pixels, 160x120 star map")
    return 0


if __name__ == "__main__":
    sys.exit(main())
