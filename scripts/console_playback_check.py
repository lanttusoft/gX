#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: focused gX-240 playback gate — real pause, one 1/60 s step, 2x/4x/8x
#   fast-forward, menu and controller controls, save/load reset, OSD state.
#   Uses the console helpers' fixed-dt capture path; no physical hardware.
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

import console_osd_check as osd
import console_shell_check as console


PLAY_RE = re.compile(
    r"CONSOLE_PLAYBACK: frame=(\d+) active=(\d+) paused=(\d+) ff=(-?\d+) "
    r"step=(\d+) delta=([\d.]+) sim=([\d.]+)")
STEP = 1.0 / 60.0
EPS = 1e-6
OUT = None


def rows(log):
    return [{"frame": int(match[1]), "active": int(match[2]),
             "paused": int(match[3]), "ff": int(match[4]),
             "step": int(match[5]), "delta": float(match[6]),
             "sim": float(match[7])}
            for match in PLAY_RE.finditer(log)]


def require_step(log, count):
    parsed = rows(log)
    console.require(parsed, "playback trace missing")
    stepping = [row for row in parsed if row["paused"] and row["delta"] > EPS]
    console.require(len(stepping) == count,
        f"expected {count} stepped frame(s), got {len(stepping)}: {stepping}")
    for row in stepping:
        console.require(abs(row["delta"] - STEP) < 1e-6,
            f"step delta {row['delta']} is not 1/60: {row}")
    console.require(all(row["ff"] == 0 for row in stepping),
        f"step frame carried an active fast-forward: {stepping}")
    return stepping


def require_ff(log, multiplier):
    parsed = rows(log)
    console.require(parsed, "playback trace missing")
    playing = [row for row in parsed
               if row["active"] and not row["paused"] and row["ff"] == multiplier
               and row["delta"] > EPS]
    console.require(playing, f"no {multiplier}x playback frame in trace")
    for row in playing:
        console.require(abs(row["delta"] - STEP * multiplier) < 1e-6,
            f"{multiplier}x delta {row['delta']} is not {multiplier}/60: {row}")
    return playing


def require_osd_play(log, expected):
    parsed = osd.rows(log)
    console.require(parsed, "OSD trace missing")
    console.require(any(row["play"] == expected for row in parsed),
        f"OSD never showed play={expected}: {[row['play'] for row in parsed]}")
    return parsed[-1]


def capture(tag, actions=None, frames=220, shot=200, extra=None, **kwargs):
    return console.capture(tag, actions, frames, shot, OUT, extra=extra,
        scene=kwargs.pop("scene", "--cartridge-y2k"), **kwargs)


def main():
    if not console.audit.BIN.is_file():
        print("CONSOLE_PLAYBACK: FAIL — missing binary", file=sys.stderr)
        return 1
    scratch = console.audit.SCRATCH / "console_playback_check"
    scratch.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run_", dir=scratch))
    global OUT
    OUT = out

    trace = {"GX_CONSOLE_PLAYBACK_TRACE": "1"}
    try:
        with console.audit.settings_preserved():
            # Real pause: the scene and status chip are frozen while the menu is open.
            pause_extra = {**trace, "GX_T_CARTRIDGE_OSD": "0"}
            paused_a, pause_log = capture("paused_a", "1.0 key 256",
                frames=180, shot=120, extra=pause_extra)
            paused_b, _ = capture("paused_b", "1.0 key 256",
                frames=180, shot=160, extra=pause_extra)
            control_a, _ = capture("freeze_control_a", None, frames=180, shot=120,
                extra=pause_extra)
            control_b, _ = capture("freeze_control_b", None, frames=180, shot=160,
                extra=pause_extra)
            console.require("CONSOLE_PLAYBACK: pause" in pause_log,
                "console menu did not report a pause")
            require_osd_play(pause_log, "PAUSED")
            pause_rows = [row for row in rows(pause_log) if row["paused"]]
            console.require(pause_rows and all(row["delta"] == 0 for row in pause_rows),
                "paused frames advanced the simulation")
            paused_diff = float(np.abs(paused_a - paused_b).mean())
            control_diff = float(np.abs(control_a - control_b).mean())
            console.require(paused_diff < 0.5 and paused_diff < control_diff * 0.25,
                f"paused cartridge was not visually frozen "
                f"({paused_diff:.3f} vs control {control_diff:.3f})")

            # Keyboard step: exactly one 1/60 s tick, menu stays open.
            _, step_log = capture("step_f6", "1.0 key 256;2.0 key 295",
                frames=210, shot=135, extra=trace)
            require_step(step_log, 1)
            require_osd_play(step_log, "STEP")

            # Menu Step button via the forced-open menu path.
            _, discovery = capture("step_discovery", None, frames=90, shot=70,
                extra={"GX_GUI_SHOW_MENU": "1"})
            step_button = console.target(console.elements(discovery, 5), "Step (F6)")
            _, menu_step = capture("step_menu",
                console.interaction.click_actions((1.0, step_button)),
                frames=150, shot=130,
                extra={"GX_GUI_SHOW_MENU": "1", **trace})
            require_step(menu_step, 1)

            # Fast-forward cycle while paused, then 4x playback after Resume.
            _, ff_log = capture("ff_cycle", "1.0 key 256;2.0 key 296;"
                "2.3 key 296;2.6 key 296;2.9 key 296",
                frames=230, shot=210, extra=trace)
            console.require("CONSOLE_PLAYBACK: fast-forward=2" in ff_log
                and "CONSOLE_PLAYBACK: fast-forward=4" in ff_log
                and "CONSOLE_PLAYBACK: fast-forward=8" in ff_log
                and "CONSOLE_PLAYBACK: fast-forward=0" in ff_log,
                "F7 did not cycle 2x/4x/8x/off")
            for label in ("PAUSED 2x", "PAUSED 4x", "PAUSED 8x", "PAUSED"):
                require_osd_play(ff_log, label)
            _, ff_play = capture("ff_play",
                "1.0 key 256;2.0 key 296;2.3 key 296;3.0 key 256",
                frames=280, shot=260, extra=trace)
            require_ff(ff_play, 4)
            require_osd_play(ff_play, "FF 4x")

            # Save/load reset: load slot 1 while 4x is armed and playing.
            save_dir = out / "slots"
            _, saved = capture("saved", "2.0 key 294",
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir),
                       "GX_NO_SETTINGS_SAVE": "0"})
            console.require("CONSOLE_STATE: save slot=1" in saved,
                "save fixture did not write slot 1")
            _, loaded = capture("loaded",
                "1.0 key 256;2.0 key 296;2.3 key 296;3.0 key 256;"
                "4.0 key 297", frames=300, shot=280,
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir), **trace})
            console.require("CONSOLE_STATE: load slot=1" in loaded,
                "F8 did not load slot 1")
            final = rows(loaded)[-1]
            console.require(final["active"] and not final["paused"]
                and final["ff"] == 0 and abs(final["delta"] - STEP) < 1e-6,
                f"load did not reset playback: {final}")

            # Controller: Start, D-pad to Step, South, D-pad to FF, South,
            # Start resumes at 2x.
            _, controller = capture("controller",
                "1.0 pad 0 start;2.0 pad 0 dpad-down;2.25 pad 0 dpad-down;"
                "2.5 pad 0 dpad-down;2.75 pad 0 dpad-down;3.0 pad 0 dpad-down;"
                "3.25 pad 0 dpad-down;3.5 pad 0 dpad-down;4.0 pad 0 south;"
                "4.4 pad 0 dpad-down;4.8 pad 0 south;5.6 pad 0 start",
                frames=410, shot=390, extra=trace)
            require_step(controller, 1)
            require_ff(controller, 2)
            require_osd_play(controller, "PAUSED 2x")
            require_osd_play(controller, "FF 2x")
    except (OSError, ValueError, RuntimeError, KeyError, subprocess.SubprocessError) as error:
        print(f"CONSOLE_PLAYBACK: FAIL — {error}; evidence {out}", file=sys.stderr)
        return 1
    print("CONSOLE_PLAYBACK: PASS pause freeze, 1/60 step (keyboard/menu/controller), "
          "F7 2x/4x/8x/off, 4x playback, load reset and OSD state")
    print(f"CONSOLE_PLAYBACK: evidence {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
