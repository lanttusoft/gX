#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: opt-in pre-bloom SSR resolve gate. The default post-bloom path must stay
#   pixel-identical when SSR is off; with SSR on, the pre-bloom path must
#   produce a valid frame and differ from the post-bloom composite. This is a
#   behavior/no-regression gate, not a visual-quality claim.

import sys

import numpy as np

import audit_common as audit


OUT = audit.SCRATCH / "ssr_prebloom_check"
SCENE = "--showcase-y2k"
BASE = {
    "GX_T_TAA": "0",
    "GX_T_HUD": "0",
    "GX_CONSOLE_SSAA": "0",
    "GX_CONSOLE_PRISM": "0",
    "GX_CONSOLE_GRAIN": "0",
    "GX_FIXED_DT": "%.12f" % (1.0 / 60.0),
}


def capture(tag, extra):
    directory = OUT / tag
    directory.mkdir(parents=True, exist_ok=True)
    for old in directory.glob("*.png"):
        old.unlink()
    result = audit.run_scene(SCENE, {**BASE, **extra}, width=1024, height=768,
        frames=200, shot_frame=180, screenshot_dir=directory, timeout=180,
        check=False)
    log = audit.strip_ansi(result.stdout or "")
    shot = audit.newest_shot(directory)
    if result.returncode != 0 or shot is None:
        raise RuntimeError(f"{tag}: exit {result.returncode}, shot={shot}")
    if "[ ERROR ]" in log or "GL_DEBUG [HIGH]" in log:
        raise RuntimeError(f"{tag}: engine/GL error in log")
    return audit.load_rgb(shot)


def main():
    if not audit.BIN.is_file():
        print("SSR_PREBLOOM: missing binary", file=sys.stderr)
        return 1
    try:
        post = capture("post", {"GX_SSR_PREBLOOM": "0"})
        pre = capture("pre", {"GX_SSR_PREBLOOM": "1"})
        for name, image in (("post", post), ("pre", pre)):
            if image.mean() < 5.0 or image.std() < 8.0:
                raise RuntimeError(f"{name}: blank/flat frame")
        delta = np.abs(pre - post)
        if delta.mean() <= 0.5:
            raise RuntimeError(
                f"pre-bloom did not change the SSR frame ({delta.mean():.3f})")

        off_post = capture("nossr_post", {"GX_T_SSR": "0",
            "GX_SSR_PREBLOOM": "0"})
        off_pre = capture("nossr_pre", {"GX_T_SSR": "0",
            "GX_SSR_PREBLOOM": "1"})
        off_delta = np.abs(off_pre - off_post)
        if off_delta.max() > 1:
            raise RuntimeError(
                "SSR-off frame changed with GX_SSR_PREBLOOM "
                f"(max {off_delta.max():.0f})")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"SSR_PREBLOOM: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"SSR_PREBLOOM: PASS post/pre valid, delta {delta.mean():.3f}; "
        "SSR-off frames identical")
    return 0


if __name__ == "__main__":
    sys.exit(main())
