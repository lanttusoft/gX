#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I011 RAY-1 cartridge gate — fixed Full-quality raymarch profile,
#   authored camera, direct/shell boot, pause menu, and save-state camera
#   restore. The raw --raymarch scene remains the comparison baseline.

import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import audit_common as audit

import console_osd_check as osd
import console_shell_check as console


OUT = None
PROFILE_QUALITY = ("CARTRIDGE: ray1 quality=3 renderScale=1.00 "
    "steps=256 bounces=4")
PROFILE_CAMERA = ("CARTRIDGE: ray1 camera=6.0/2.0/14.0 "
    "target=0.0/-0.5/6.0 fovy=55")
LOAD_CAMERA = ("CONSOLE_STATE: load slot=1 cartridge=ray-1 "
    "camera=1.0/2.0/3.0 target=0.0/0.5/4.0 fovy=60")
RESTORE_CAMERA = ("CONSOLE_CAMERA: restore=1.0/2.0/3.0 "
    "target=0.0/0.5/4.0 fovy=60")


def capture(tag, actions=None, frames=220, shot=200, extra=None, **kwargs):
    return console.capture(tag, actions, frames, shot, OUT, extra=extra,
        scene=kwargs.pop("scene", "--cartridge-ray1"), **kwargs)


def osd_has(log, **expected):
    parsed = osd.rows(log)
    console.require(parsed, "OSD trace missing")
    console.require(any(all(row[key] == value for key, value in expected.items())
        for row in parsed),
        f"OSD did not show {expected}: "
        f"{[({k: row[k] for k in expected}, row['mode']) for row in parsed]}")
    osd.assert_osd(log, 1024, 768, **expected)
    return parsed[-1]


def require_profile(log):
    console.require(PROFILE_QUALITY in log,
        "RAY-1 profile did not apply Full quality")
    console.require(PROFILE_CAMERA in log,
        "RAY-1 profile did not apply the authored camera")


def validate_save(state):
    console.require(state.get("cartridge") == "ray-1",
        f"save cartridge is {state.get('cartridge')}")
    video = state.get("video", {})
    console.require((video.get("width"), video.get("height")) == (320, 240),
        f"save video size {video.get('width')}x{video.get('height')}")
    console.require(video.get("filter") == 1,
        f"save video filter {video.get('filter')}")
    camera = state.get("camera3d", {})
    console.require(camera.get("position") == [6.0, 2.0, 14.0],
        f"save camera position {camera.get('position')}")
    console.require(camera.get("target") == [0.0, -0.5, 6.0],
        f"save camera target {camera.get('target')}")
    console.require(camera.get("fovy") == 55.0,
        f"save camera fovy {camera.get('fovy')}")


def require_load_camera(log):
    console.require(LOAD_CAMERA in log,
        "save load did not restore the RAY-1 camera")
    console.require(RESTORE_CAMERA in log,
        "camera response did not apply the saved RAY-1 view")


def frame_stats(image):
    return float(image.mean()), float(image.std())


def main():
    if not audit.BIN.is_file():
        print("CONSOLE_RAY1: FAIL — missing binary", file=sys.stderr)
        return 1
    scratch = audit.SCRATCH / "console_ray1_check"
    scratch.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run_", dir=scratch))
    global OUT
    OUT = out

    try:
        with audit.console_settings():
            # Direct profile boot, plus a raw raymarch reference for the
            # authored camera/quality difference.
            direct, direct_log = capture("direct", None, frames=180, shot=160,
                extra={"GX_T_CARTRIDGE_OSD": "0"})
            require_profile(direct_log)
            osd_has(direct_log, cart="RAY-1", render=(320, 240),
                out=(320, 240), mode="SOFT", prism=False)
            mean, std = frame_stats(direct)
            console.require(mean > 5.0 and std > 8.0,
                f"RAY-1 frame is flat ({mean:.1f}/{std:.1f})")

            profile_pixels, _ = capture("profile_visual", None,
                frames=180, shot=160,
                extra={"GX_CONSOLE_OSD": "0", "GX_T_CARTRIDGE_OSD": "0"})
            profile_repeat, _ = capture("profile_repeat", None,
                frames=180, shot=160,
                extra={"GX_CONSOLE_OSD": "0", "GX_T_CARTRIDGE_OSD": "0"})
            raw_pixels, _ = capture("raw_visual", None, frames=180, shot=160,
                scene="--raymarch",
                extra={"GX_CONSOLE_OSD": "0", "GX_T_CARTRIDGE_OSD": "0",
                       "GX_VIRTUAL_WIDTH": "320", "GX_VIRTUAL_HEIGHT": "240",
                       "GX_PRESENT_FILTER": "1"})
            delta = float(np.abs(profile_pixels - raw_pixels).mean())
            console.require(delta > 1.0,
                f"RAY-1 profile camera/quality did not change the frame "
                f"({delta:.3f})")
            repeat_delta = float(np.abs(profile_pixels - profile_repeat).max())
            console.require(repeat_delta == 0.0,
                f"RAY-1 profile is not deterministic ({repeat_delta:.3f})")

            # Shell boot uses the same profile and selected 240p output.
            _, discovery = capture("shell_discovery", None, 80, 60,
                scene="--console-shell",
                extra={"GX_T_CARTRIDGE_OSD": "0",
                       "GX_VIRTUAL_WIDTH": "320", "GX_VIRTUAL_HEIGHT": "240",
                       "GX_PRESENT_FILTER": "1"})
            ray = console.target(console.elements(discovery), "RAY-1 TECH DEMO")
            shell_extra = {"GX_T_CARTRIDGE_OSD": "0",
                           "GX_VIRTUAL_WIDTH": "320", "GX_VIRTUAL_HEIGHT": "240",
                           "GX_PRESENT_FILTER": "1"}
            _, shell_log = capture("shell_boot",
                console.interaction.click_actions((1.0, ray)),
                frames=260, shot=240, scene="--console-shell",
                extra=shell_extra)
            require_profile(shell_log)
            console.require("CONSOLE: boot=1 scene=18" in shell_log,
                "shell did not boot the RAY-1 profile")
            osd_has(shell_log, cart="RAY-1", render=(320, 240),
                out=(320, 240), mode="SOFT")

            # The cartridge pause menu owns the RAY-1 scene.
            _, pause_log = capture("pause", "1.0 key 256", frames=220, shot=200,
                extra={"GX_T_CARTRIDGE_OSD": "0"})
            console.require("CONSOLE_MENU: active" in pause_log
                and "CONSOLE_PLAYBACK: pause" in pause_log,
                "RAY-1 did not open the console pause menu")
            osd_has(pause_log, cart="RAY-1", mode="SOFT", play="PAUSED")

            # F5/F8 save and load, with camera3d persistence and restore.
            save_dir = out / "slots"
            _, save_log = capture("save", "2.0 key 294", frames=260, shot=240,
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir),
                       "GX_NO_SETTINGS_SAVE": "0",
                       "GX_T_CARTRIDGE_OSD": "0"})
            console.require("CONSOLE_STATE: save slot=1 cartridge=ray-1"
                in save_log, "RAY-1 F5 save failed")
            state_path = save_dir / "console_slot_1.json"
            console.require(state_path.is_file(), "RAY-1 save state missing")
            state = json.loads(state_path.read_text(encoding="utf-8"))
            validate_save(state)
            state["camera3d"] = {
                "position": [1.0, 2.0, 3.0],
                "target": [0.0, 0.5, 4.0],
                "up": [0.0, 1.0, 0.0],
                "fovy": 60.0,
            }
            state_path.write_text(json.dumps(state), encoding="utf-8")

            _, load_log = capture("load", "2.0 key 297", frames=280, shot=260,
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir),
                       "GX_T_CARTRIDGE_OSD": "0"})
            console.require("CONSOLE_STATE: load slot=1" in load_log,
                "RAY-1 F8 load failed")
            require_profile(load_log)
            require_load_camera(load_log)
            osd_has(load_log, cart="RAY-1", mode="SOFT", state="LOADED")
    except (OSError, ValueError, RuntimeError, KeyError,
            subprocess.SubprocessError) as error:
        print(f"CONSOLE_RAY1: FAIL — {error}; evidence {out}", file=sys.stderr)
        return 1
    print("CONSOLE_RAY1: PASS Full quality, authored camera, direct/shell boot, "
          "pause menu, deterministic frame and save-state camera restore")
    print(f"CONSOLE_RAY1: evidence {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
