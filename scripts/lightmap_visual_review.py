#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: advisory static-lightmap A/B visual lane. Captures the checked-in
#   fixture with the additive RGBA8 map enabled and disabled, reports image
#   deltas, and writes a human-review contact sheet. It never grades or gates
#   visual quality.

import json
import os
import shutil
import sys
import tempfile
from datetime import datetime, timezone

import numpy as np
from PIL import Image, ImageDraw, ImageFont

import audit_common as audit


PRESET = audit.ROOT / "resources/tools/gfx_lightmap.json"
OUT = "/tmp/opencode/lightmap_visual_review"
FRAME = 60


def font():
    try:
        return ImageFont.truetype("DejaVuSans.ttf", 18)
    except OSError:
        return ImageFont.load_default()


def capture(label, preset_path, root):
    output = os.path.join(root, label)
    os.makedirs(output, exist_ok=True)
    #opencode: no auto-exposure — the A/B must measure the lightmap alone.
    environment = audit.scene_env({
        "GX_GFX_PRESET_LOAD": str(preset_path),
        "GX_NO_AE": "1",
    }, clean=True)
    audit.run_scene("--gfx", environment, width=None, height=None, fps=None,
                    screenshot_dir=output, shot_frame=FRAME,
                    screenshot_after=True, timeout=120,
                    extra_args=("--gfx-editor",))
    shot = audit.newest_shot(output)
    if shot is None:
        raise RuntimeError("%s: no screenshot produced" % label)
    return shot


def roi_metrics(on, off):
    if on.shape != off.shape:
        raise RuntimeError("capture size mismatch: %s vs %s" % (on.shape, off.shape))
    if not np.isfinite(on).all() or not np.isfinite(off).all():
        raise RuntimeError("capture contains non-finite pixels")
    height, width = on.shape[:2]
    #opencode: normalized viewport ROI around the fixture cube; the editor
    #   chrome occupies the outer left/right bands at the fixed capture size.
    rows = (int(height * 0.35), int(height * 0.68))
    cols = (int(width * 0.49), int(width * 0.69))
    diff = np.abs(on - off)
    region = diff[rows[0]:rows[1], cols[0]:cols[1]]
    return {
        "image_width": int(width),
        "image_height": int(height),
        "roi": {"rows": list(rows), "cols": list(cols)},
        "whole_mean_abs_channel_delta": float(diff.mean()),
        "roi_mean_abs_channel_delta": float(region.mean()),
        "roi_fraction_pixels_changed": float((region.max(axis=2) > 2.0).mean()),
        "on_roi_mean_rgb": [float(value) for value in on[rows[0]:rows[1], cols[0]:cols[1]].mean(axis=(0, 1))],
        "off_roi_mean_rgb": [float(value) for value in off[rows[0]:rows[1], cols[0]:cols[1]].mean(axis=(0, 1))],
    }


def render_contact_sheet(on_path, off_path, metrics, path):
    on = Image.open(on_path).convert("RGB")
    off = Image.open(off_path).convert("RGB")
    width = min(on.width, 900)
    height = int(on.height * width / on.width)
    on = on.resize((width, height), Image.Resampling.LANCZOS)
    off = off.resize((width, height), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (width * 2, height + 52), (18, 20, 24))
    canvas.paste(off, (0, 52))
    canvas.paste(on, (width, 52))
    draw = ImageDraw.Draw(canvas)
    text_font = font()
    draw.text((12, 15), "lightmap OFF", fill=(235, 238, 245), font=text_font)
    draw.text((width + 12, 15), "lightmap ON", fill=(235, 238, 245), font=text_font)
    draw.rectangle((0, 52, width - 1, height + 51), outline=(90, 96, 110))
    draw.rectangle((width, 52, width * 2 - 1, height + 51), outline=(90, 96, 110))
    canvas.save(path)


def main():
    if not audit.BIN.is_file():
        raise RuntimeError("missing build binary: %s" % audit.BIN)
    os.makedirs(OUT, exist_ok=True)
    for name in ("on", "off", "lightmap_ab.png", "lightmap_metrics.json"):
        path = os.path.join(OUT, name)
        if os.path.isdir(path):
            shutil.rmtree(path)
        elif os.path.isfile(path):
            os.remove(path)

    with open(PRESET, encoding="utf-8") as stream:
        off_preset = json.load(stream)
    off_preset["name"] = "gX Static Lightmap Fixture Off"
    off_preset["models"][0]["params"]["hasLightmap"] = False
    off_preset["models"][0]["params"]["lightmapID"] = -1

    with audit.settings_preserved():
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json",
                                         encoding="utf-8") as stream:
            json.dump(off_preset, stream, indent=2)
            stream.flush()
            on_path = capture("on", PRESET, OUT)
            off_path = capture("off", stream.name, OUT)

    on = audit.load_rgb(on_path)
    off = audit.load_rgb(off_path)
    metrics = roi_metrics(on, off)
    metrics.update({
        "schema": 1,
        "advisory": True,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "on_capture": os.path.abspath(on_path),
        "off_capture": os.path.abspath(off_path),
        "contact_sheet": os.path.abspath(os.path.join(OUT, "lightmap_ab.png")),
    })
    render_contact_sheet(on_path, off_path, metrics, metrics["contact_sheet"])
    with open(os.path.join(OUT, "lightmap_metrics.json"), "w", encoding="utf-8") as stream:
        json.dump(metrics, stream, indent=2)
        stream.write("\n")
    print("LIGHTMAP-VISUAL-REVIEW: ADVISORY")
    print("  ROI mean abs channel delta: %.3f" % metrics["roi_mean_abs_channel_delta"])
    print("  ROI pixels changed >2:      %.1f%%" % (metrics["roi_fraction_pixels_changed"] * 100.0))
    print("  Contact sheet:              %s" % metrics["contact_sheet"])
    print("  Metrics:                    %s" % os.path.join(OUT, "lightmap_metrics.json"))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print("LIGHTMAP-VISUAL-REVIEW: FAIL — %s" % exc)
        sys.exit(1)
