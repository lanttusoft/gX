#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: global editor host pin — the toolbar is always summonable (F9) and
#   renders the application menu bar in any scene, not only while an editor is
#   open. The sky editor is the pilot panel. F9 is raylib KEY_F9 = 298.

import re
import sys
from pathlib import Path

import numpy as np

import audit_common as audit


OUT = audit.SCRATCH / "editor_host_check"
WIDTH = 1024
HEIGHT = 768
TOP = 46  # menu bar + workspace tab row
TAB_RE = re.compile(r'EDITOR_HOST: tab=Sky x=(\d+) y=(\d+) w=(\d+) h=(\d+)')
TAB_ALL_RE = re.compile(r'EDITOR_HOST: tab=(\S+) x=(\d+) y=(\d+) w=(\d+) h=(\d+)')
DOCK_RE = re.compile(r'GFX_DOCK:.*viewport=\(([-0-9.]+),([-0-9.]+),([-0-9.]+),([-0-9.]+)\)')


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
            {"GX_EDITOR_HOST": "1", "GX_EDITOR_WORKSPACE": "Sky"},
            frames=90, shot=70)
        if sky_code != 0:
            raise RuntimeError(f"sky host run failed ({sky_code})")
        if "EDITOR_HOST: open=1" not in sky_log:
            raise RuntimeError("host missing in the sky run")
        if "SKY_EDITOR_UI: panel=1" not in sky_log:
            raise RuntimeError("sky panel did not open")
        if "EDITOR_HOST: active=Sky" not in sky_log:
            raise RuntimeError("editor registry did not report Sky as active")
        if "EDITOR_HOST: workspace=Sky" not in sky_log:
            raise RuntimeError("Sky workspace was not applied")
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
            {"GX_EDITOR_HOST": "1", "GX_EDITOR_WORKSPACE": "Background"},
            frames=60, shot=40)
        if bg_code != 0:
            raise RuntimeError(f"background host run failed ({bg_code})")
        if "EDITOR_HOST: active=Background" not in bg_log:
            raise RuntimeError("editor registry did not report Background")
        if "EDITOR_HOST: workspace=Background" not in bg_log:
            raise RuntimeError("Background workspace was not applied")
        if "BG_EDITOR_UI: panel=1" not in bg_log:
            raise RuntimeError("background panel did not open")

        #opencode: the GFX editor is hosted now; its panels/viewport must start
        #   below the host menu bar + workspace tab strip.
        base_gfx_code, base_gfx_log, _ = run("gfx-base", "--gfx",
            {"GX_TOOLS_OPEN": "gfx", "GX_DOCK_TRACE": "1"},
            frames=80, shot=60)
        host_gfx_code, host_gfx_log, _ = run("gfx-host", "--gfx",
            {"GX_EDITOR_HOST": "1", "GX_TOOLS_OPEN": "gfx",
             "GX_DOCK_TRACE": "1"}, frames=80, shot=60)
        if base_gfx_code != 0 or host_gfx_code != 0:
            raise RuntimeError("hosted GFX editor run failed")
        if "Tools: GFX editor opened" not in host_gfx_log:
            raise RuntimeError("GFX editor did not open under the host")
        if "EDITOR_HOST: active=GFX" not in host_gfx_log:
            raise RuntimeError("host registry did not report GFX active")
        base_dock = DOCK_RE.findall(base_gfx_log)
        host_dock = DOCK_RE.findall(host_gfx_log)
        if not base_dock or not host_dock:
            raise RuntimeError("GFX dock trace missing")
        base_y = float(base_dock[-1][1])
        host_y = float(host_dock[-1][1])
        if host_y < 40.0:
            raise RuntimeError(
                f"GFX viewport does not clear the host bars (y={host_y:.0f})")
        if host_y <= base_y:
            raise RuntimeError(
                f"host inset did not move the GFX viewport (base {base_y:.0f}, "
                f"host {host_y:.0f})")

        default_code, default_log, _ = run("default", "--title",
            {"GX_EDITOR_HOST": "1", "GX_EDITOR_WORKSPACE": "Default"},
            frames=40, shot=30)
        if default_code != 0:
            raise RuntimeError(f"default workspace run failed ({default_code})")
        if "EDITOR_HOST: workspace=Default" not in default_log:
            raise RuntimeError("Default workspace was not applied")
        if "EDITOR_HOST: active=none" not in default_log:
            raise RuntimeError("Default workspace unexpectedly opened an editor")

        trace_code, trace_log, _ = run("tabs", "--title",
            {"GX_EDITOR_HOST": "1", "GX_EDITOR_WORKSPACE": "Default",
             "GX_EDITOR_HOST_TAB_TRACE": "1"}, frames=60, shot=40)
        match = TAB_RE.search(trace_log)
        if trace_code != 0 or not match:
            raise RuntimeError("workspace tab rect trace missing")
        #opencode: every tab owns only its own hit box. A full-row Selectable
        #   (the v0.30.1 host bug) made the first tab swallow every click.
        tabs = [(name, int(x), int(y), int(w), int(h))
                for name, x, y, w, h in TAB_ALL_RE.findall(trace_log)]
        if len(tabs) < 3:
            raise RuntimeError(f"workspace tab rects missing ({len(tabs)})")
        for name, x, y, w, h in tabs:
            if w > 200:
                raise RuntimeError(
                    f"workspace tab '{name}' is too wide ({w}px)")
        for index in range(len(tabs) - 1):
            name, x, y, w, h = tabs[index]
            next_x = tabs[index + 1][1]
            if x + w > next_x:
                raise RuntimeError(
                    f"workspace tab '{name}' overlaps the next tab")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"EDITOR_HOST_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print("EDITOR_HOST_CHECK: PASS toolbar in title/gfx, F9 open+close, "
          "Sky/Background/Default workspaces, tab hit boxes sane, GFX hosted "
          "below the bars")
    return 0


if __name__ == "__main__":
    sys.exit(main())
