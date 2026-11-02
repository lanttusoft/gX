#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: global editor host pin — the toolbar is always summonable (F9) and
#   renders the application menu bar in any scene, not only while an editor is
#   open. The sky editor is the pilot panel. F9 is raylib KEY_F9 = 298.

import sys
from pathlib import Path

import numpy as np

import audit_common as audit


OUT = audit.SCRATCH / "editor_host_check"
WIDTH = 1024
HEIGHT = 768
TOP = 22  # menu-bar band


def run(tag, scene, extra, frames=30, shot=20):
    directory = OUT / tag
    directory.mkdir(parents=True, exist_ok=True)
    overrides = {
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(frames),
        "GX_NO_SETTINGS_SAVE": "1",
    }
    overrides.update(extra)
    environment = audit.scene_env(overrides, clean=True)
    result = audit.run_scene(scene, environment, width=WIDTH, height=HEIGHT,
        fps=None, frames=frames, shot_frame=shot, screenshot_dir=directory,
        timeout=150, check=False)
    log = audit.strip_ansi(result.stdout or "")
    image = None
    shot_file = audit.newest_shot(directory)
    if shot_file is not None:
        image = np.asarray(audit.Image.open(shot_file).convert("RGB"),
                           dtype=np.float32)
    return result.returncode, log, image


def top_band(image):
    return image[:TOP, :, :]


def main():
    if not audit.BIN.is_file():
        print("EDITOR_HOST_CHECK: FAIL — missing binary", file=sys.stderr)
        return 1
    try:
        base_code, base_log, base = run("base", "--title", {})
        host_code, host_log, host = run("host", "--title",
            {"GX_EDITOR_HOST": "1"})
        if base_code != 0 or host_code != 0:
            raise RuntimeError(f"title runs failed ({base_code}/{host_code})")
        if "EDITOR_HOST: open=1" not in host_log:
            raise RuntimeError("host env did not open the toolbar")
        if base is None or host is None:
            raise RuntimeError("title captures missing")
        delta = float(np.abs(top_band(base) - top_band(host)).mean())
        if delta < 5.0:
            raise RuntimeError(
                f"toolbar bar not visible in the title (top delta {delta:.2f})")

        toggle_code, toggle_log, _ = run("toggle", "--title", {
            "GX_AI_INPUT": "1.0 key 298;2.0 key 298",
        }, frames=200, shot=150)
        if toggle_code != 0:
            raise RuntimeError(f"F9 toggle run failed ({toggle_code})")
        if toggle_log.count("EDITOR_HOST: open=1") < 1:
            raise RuntimeError("F9 did not open the toolbar")
        if toggle_log.count("EDITOR_HOST: open=0") < 1:
            raise RuntimeError("F9 did not close the toolbar")
        closed = audit.newest_shot(OUT / "toggle")
        if closed is None:
            raise RuntimeError("F9 toggle capture missing")
        closed_img = np.asarray(audit.Image.open(closed).convert("RGB"),
                                dtype=np.float32)
        closed_delta = float(np.abs(top_band(base) - top_band(closed_img)).mean())
        if closed_delta > 3.0:
            raise RuntimeError(
                f"toolbar did not hide on the second F9 (delta {closed_delta:.2f})")

        gfx_code, gfx_log, _ = run("gfx", "--gfx", {"GX_EDITOR_HOST": "1"},
                                   frames=30, shot=20)
        if gfx_code != 0 or "EDITOR_HOST: open=1" not in gfx_log:
            raise RuntimeError("host did not open in the gfx scene")

        sky_code, sky_log, sky = run("sky", "--title",
            {"GX_EDITOR_HOST": "1", "GX_TOOLS_OPEN": "sky"},
            frames=90, shot=70)
        if sky_code != 0:
            raise RuntimeError(f"sky host run failed ({sky_code})")
        if "EDITOR_HOST: open=1" not in sky_log:
            raise RuntimeError("host missing in the sky run")
        if "SKY_EDITOR_UI: panel=1" not in sky_log:
            raise RuntimeError("sky panel did not open")
        if "EDITOR_HOST: active=Sky" not in sky_log:
            raise RuntimeError("editor registry did not report Sky as active")
        if sky is None:
            raise RuntimeError("sky capture missing")
        #opencode: the sky scene opens its own (wider) window; compare the
        #   toolbar band on the common left edge.
        if sky.shape[1] < WIDTH:
            raise RuntimeError(f"sky capture too narrow ({sky.shape[1]})")
        sky_delta = float(np.abs(top_band(base) - sky[:TOP, :WIDTH, :]).mean())
        if sky_delta < 5.0:
            raise RuntimeError("toolbar bar missing in the sky capture")

        bg_code, bg_log, _ = run("background", "--title",
            {"GX_EDITOR_HOST": "1", "GX_TOOLS_OPEN": "background"},
            frames=60, shot=40)
        if bg_code != 0:
            raise RuntimeError(f"background host run failed ({bg_code})")
        if "EDITOR_HOST: active=Background" not in bg_log:
            raise RuntimeError("editor registry did not report Background")
        if "BG_EDITOR_UI: panel=1" not in bg_log:
            raise RuntimeError("background panel did not open")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"EDITOR_HOST_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print("EDITOR_HOST_CHECK: PASS toolbar in title/gfx, F9 open+close, "
          "sky + background panels via the editor registry")
    return 0


if __name__ == "__main__":
    sys.exit(main())
