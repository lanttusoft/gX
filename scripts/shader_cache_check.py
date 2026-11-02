#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GL program-binary cache contract — a warm run must hit the cache, render
#   the same frame and spend less time in shader load than a cold run.

import re
import shutil
import sys
from pathlib import Path

import numpy as np

import audit_common as audit


OUT = audit.SCRATCH / "shader_cache_check"
CACHE_DIR = audit.ROOT / "gx_shader_cache"
PRESET = audit.ROOT / "resources" / "tools" / "gfx_editor.json"

TIME_RE = re.compile(
    r"SHADER_TIME: (.+?) ([0-9.]+) ms \((compiled|binary cache)\)")
DIFF_LIMIT = 2.0


def run(tag, frames=90, shot=60):
    directory = OUT / tag
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_GFX_PRESET_LOAD": str(PRESET),
        "GX_SHADER_TIME": "1",
    }, clean=True)
    result = audit.run_scene(
        "--gfx", environment, width=None, height=None, fps=None,
        frames=frames, shot_frame=shot, screenshot_dir=directory,
        timeout=180, check=True, extra_args=("--gfx-editor",))
    log = audit.strip_ansi(result.stdout or "")
    rows = [(name, float(ms), kind) for name, ms, kind in TIME_RE.findall(log)]
    capture = audit.newest_shot(directory)
    image = None
    if capture is not None:
        image = np.asarray(audit.Image.open(capture).convert("RGB"),
            dtype=np.float32)
    return rows, image


def main():
    if not audit.BIN.is_file() or not PRESET.is_file():
        print("SHADER_CACHE_CHECK: missing binary or preset", file=sys.stderr)
        return 1
    try:
        shutil.rmtree(CACHE_DIR, ignore_errors=True)
        cold_rows, cold_image = run("cold")
        warm_rows, warm_image = run("warm")
        if not cold_rows:
            raise RuntimeError("cold run logged no shader timings")
        if not warm_rows:
            raise RuntimeError("warm run logged no shader timings")
        #ai: two shaders can share one processed source, so a cold run may
        #   hit an entry it just wrote; the warm run is the real contract.
        warm_hits = [row for row in warm_rows if row[2] == "binary cache"]
        if not warm_hits:
            raise RuntimeError("warm run did not hit the shader cache")
        if cold_image is None or warm_image is None:
            raise RuntimeError("cold/warm captures missing")
        if cold_image.shape != warm_image.shape:
            raise RuntimeError(
                f"capture size changed: {cold_image.shape} vs {warm_image.shape}")
        difference = audit.mean_abs_diff(cold_image, warm_image)
        if difference > DIFF_LIMIT:
            raise RuntimeError(
                f"cached frame differs from the compiled frame ({difference:.2f})")
        cold_total = sum(row[1] for row in cold_rows)
        warm_total = sum(row[1] for row in warm_rows)
        if warm_total >= cold_total:
            raise RuntimeError(
                f"warm shader load is not faster ({warm_total:.1f} ms vs "
                f"{cold_total:.1f} ms)")
        print("SHADER_CACHE_CHECK: PASS "
              f"cold {cold_total:.1f} ms ({len(cold_rows)} shaders), "
              f"warm {warm_total:.1f} ms ({len(warm_hits)} cached), "
              f"frame diff {difference:.2f}")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"SHADER_CACHE_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
