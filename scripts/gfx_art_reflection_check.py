#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: original-art regression for SSR and planar reflection independence.

import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
PRESET = ROOT / "resources" / "tools" / "art.json"
CASES = {
    "both": {"ENABLE_REFLECTIONS": True, "ENABLE_SSR": True,
             "ENABLE_PLANAR_REFLECTIONS": True,
             "ENABLE_SSR_AUXVIEW": True},
    "planar_only": {"ENABLE_REFLECTIONS": False,
                     "ENABLE_PLANAR_REFLECTIONS": True, "ENABLE_SSR": False,
                     "ENABLE_SSR_AUXVIEW": False},
    "ssr_only": {"ENABLE_REFLECTIONS": False,
                  "ENABLE_PLANAR_REFLECTIONS": False, "ENABLE_SSR": True,
                   "ENABLE_SSR_AUXVIEW": False},
    "neither": {"ENABLE_REFLECTIONS": False,
                 "ENABLE_PLANAR_REFLECTIONS": False, "ENABLE_SSR": False,
                  "ENABLE_SSR_AUXVIEW": False},
}


def capture(temp_root, name, preset):
    directory = temp_root / name
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_GFX_PRESET_LOAD": str(preset),
    }, clean=True)
    try:
        with audit.settings_preserved():
            result = audit.run_scene(
                "--gfx", environment, width=1536, height=1016,
                fps=None, shot_frame=300, screenshot_dir=directory,
                timeout=120, check=False, extra_args=("--gfx-editor",))
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        raise RuntimeError(f"{name}: timed out\n{output[-3000:]}") from error
    if result.returncode != 0:
        raise RuntimeError(f"{name}: process failed ({result.returncode})\n{result.stdout[-3000:]}")
    expected_planar = name in ("both", "planar_only")
    has_planar = "ORACLEDUMP f=" in result.stdout
    if has_planar != expected_planar:
        raise RuntimeError(f"{name}: planar capture state mismatch (has_planar={has_planar})")
    screenshots = list(directory.glob("gXss_*.png"))
    if not screenshots:
        raise RuntimeError(f"{name}: screenshot missing\n{result.stdout[-3000:]}")
    screenshot = max(screenshots, key=lambda path: path.stat().st_mtime)
    pixels = np.asarray(Image.open(screenshot).convert("RGB"), dtype=np.int16)
    return result.stdout, pixels


def require_visible_change(label, first, second):
    if first.shape != second.shape:
        raise RuntimeError(f"{label}: capture sizes differ: {first.shape} vs {second.shape}")
    height, width = first.shape[:2]
    region = (slice(int(height * 0.025), height),
              slice(int(width * 0.177), int(width * 0.79)))
    delta = np.abs(first[region] - second[region])
    mean = float(delta.mean())
    changed = float((delta > 2).any(axis=2).mean())
    if mean < 0.25 or changed < 0.005:
        raise RuntimeError(
            f"{label}: expected visible reflection change, mean={mean:.4f}, "
            f"pixels={changed:.3%}"
        )
    print(f"  {label}: mean={mean:.3f}, changed={changed:.2%}")


def main():
    if not BIN.is_file():
        print(f"GFX_ART_REFLECTION_CHECK: binary missing: {BIN}", file=sys.stderr)
        return 1
    try:
        original = json.loads(PRESET.read_text())
        os.makedirs("/tmp/opencode", exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="gfx_art_reflection_",
                                         dir="/tmp/opencode") as temporary:
            temp_root = Path(temporary)
            captures = {}
            for name, toggles in CASES.items():
                fixture = copy.deepcopy(original)
                fixture["state"].update(toggles)
                preset = temp_root / f"{name}.json"
                preset.write_text(json.dumps(fixture, indent=2) + "\n")
                _, captures[name] = capture(temp_root, name, preset)
            print("GFX_ART_REFLECTION_CHECK: original-art environment/SSR/planar independence")
            require_visible_change("planar with SSR off",
                                   captures["planar_only"], captures["neither"])
            require_visible_change("SSR with planar off",
                                   captures["ssr_only"], captures["neither"])
            require_visible_change("SSR added to planar",
                                   captures["both"], captures["planar_only"])
            require_visible_change("planar added to SSR",
                                   captures["both"], captures["ssr_only"])
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"GFX_ART_REFLECTION_CHECK: FAIL: {error}", file=sys.stderr)
        return 1
    print("GFX_ART_REFLECTION_CHECK: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
