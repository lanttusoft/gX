#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: E5 gate — the opt-in 4.3 point-shadow path must be byte-identical to the
#   legacy six-pass path, the env override must activate it, and the preset
#   field must do the same through the editor load. On a context below 4.3 the
#   gate reports SKIP instead of failing matrix portability.
"""Gate the opt-in layered point-shadow path."""

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


DEFAULT_OUT = audit.SCRATCH / "gfx_layered_shadows_check"
FRAME = 60
WIDTH = 1024
HEIGHT = 768
EXTRA_LIGHTS = 2
ACTIVE_MARKER = "LAYERED_SHADOWS: active"


def latest(directory):
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError("%s produced no screenshot" % directory.name)
    return shot


def image(path):
    with Image.open(path) as image:
        return np.asarray(image.convert("RGB"), dtype=np.int16)


def compare(reference, variant):
    delta = np.abs(reference - variant)
    mse = float((delta.astype(np.float64) ** 2).mean())
    return {
        "psnr": float("inf") if mse == 0 else 10.0 * math.log10((255.0 ** 2) / mse),
        "mean": float(delta.mean()),
        "max": int(delta.max()),
        "changed": float((delta.max(axis=2) > 0).mean()),
    }


def capture(out, tag, environment, scene="--showcase", extra_args=()):
    directory = out / tag
    if directory.exists():
        for old in directory.glob("*.png"):
            old.unlink()
    directory.mkdir(parents=True, exist_ok=True)
    result = audit.run_scene(
        scene, environment, width=WIDTH, height=HEIGHT, frames=FRAME + 40,
        shot_frame=FRAME, screenshot_dir=directory, timeout=180, check=True,
        extra_args=extra_args)
    log = audit.strip_ansi(result.stdout or "")
    return image(latest(directory)), log


def showcase_environment(layered):
    environment = audit.scene_env({
        "GX_GL43_CONTEXT": "1",
        "GX_LAYERED_SHADOWS": "1" if layered else "0",
        "GX_T_POINT_SHADOWS": "1",
        "GX_EXTRA_SHADOW_LIGHTS": str(EXTRA_LIGHTS),
        "GX_FIXED_DT": "%.12f" % (1.0 / 60.0),
    }, clean=True)
    return environment


def preset(out, layered):
    source = audit.ROOT / "resources" / "tools" / "gfx_showcase.json"
    data = json.loads(source.read_text())
    data["state"]["ENABLE_SHADOWS"] = True
    data["state"]["ENABLE_POINT_SHADOWS"] = True
    data["state"]["ENABLE_LAYERED_SHADOWS"] = bool(layered)
    for light in data.get("lights", [])[:2]:
        light["castShadows"] = True
        light["shadowSize"] = 512
    path = out / ("layered-%s.json" % ("on" if layered else "off"))
    path.write_text(json.dumps(data), encoding="utf-8")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    probe, probe_log = capture(out, "probe", showcase_environment(False))
    if "layered shadows available=1" not in probe_log:
        print("GFX_LAYERED_SHADOWS: SKIP — 4.3 layered path unavailable")
        return 0

    legacy, legacy_log = capture(out, "legacy", showcase_environment(False))
    layered, layered_log = capture(out, "layered", showcase_environment(True))
    env_metrics = compare(legacy, layered)

    off_preset = preset(out, False)
    on_preset = preset(out, True)
    preset_base = audit.scene_env({
        "GX_GL43_CONTEXT": "1",
        "GX_FIXED_DT": "%.12f" % (1.0 / 60.0),
    }, clean=True)
    preset_off, preset_off_log = capture(
        out, "preset-off", {**preset_base, "GX_GFX_PRESET_LOAD": str(off_preset)},
        scene="--gfx-editor", extra_args=("--showcase-minimal",))
    preset_on, preset_on_log = capture(
        out, "preset-on", {**preset_base, "GX_GFX_PRESET_LOAD": str(on_preset)},
        scene="--gfx-editor", extra_args=("--showcase-minimal",))
    preset_metrics = compare(preset_off, preset_on)

    metrics = {
        "frame": FRAME,
        "size": [WIDTH, HEIGHT],
        "extra_lights": EXTRA_LIGHTS,
        "env": env_metrics,
        "preset": preset_metrics,
        "env_marker": ACTIVE_MARKER in layered_log,
        "preset_marker": ACTIVE_MARKER in preset_on_log,
        "legacy_marker": ACTIVE_MARKER in legacy_log,
        "preset_off_marker": ACTIVE_MARKER in preset_off_log,
    }
    failures = []
    if metrics["env"]["max"] != 0:
        failures.append("env layered image is not byte-identical to legacy")
    if metrics["preset"]["max"] != 0:
        failures.append("preset layered image is not byte-identical to preset legacy")
    if not metrics["env_marker"]:
        failures.append("env arm did not activate the layered pass")
    if not metrics["preset_marker"]:
        failures.append("preset arm did not activate the layered pass")
    if metrics["legacy_marker"] or metrics["preset_off_marker"]:
        failures.append("disabled arm activated the layered pass")

    (out / "report.json").write_text(
        json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("GFX_LAYERED_SHADOWS: frame=%d %dx%d lights=%d"
          % (FRAME, WIDTH, HEIGHT, EXTRA_LIGHTS))
    print("  env: max %d changed %.4f, marker %s"
          % (metrics["env"]["max"], metrics["env"]["changed"],
             metrics["env_marker"]))
    print("  preset: max %d changed %.4f, marker %s"
          % (metrics["preset"]["max"], metrics["preset"]["changed"],
             metrics["preset_marker"]))
    if failures:
        for failure in failures:
            print("  FAIL " + failure)
        print("GFX_LAYERED_SHADOWS: FAIL")
        return 1
    print("GFX_LAYERED_SHADOWS: PASS — env and preset paths match the legacy "
          "image exactly")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        print("GFX_LAYERED_SHADOWS: FAIL — %s" % error)
        sys.exit(1)
