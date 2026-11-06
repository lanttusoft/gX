#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: native console OSD contract — live video, slot feedback, auto-hide,
#   disable, selected-slot load and renderer-path idle blank/wake. No CRT modes.
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

import console_shell_check as console


SHOW_RE = re.compile(
    r"CONSOLE_OSD: show render=(\d+)x(\d+) out=(\d+)x(\d+) "
    r"mode=(SOFT|SHARP|NATIVE|SSAA2x|SSAA4x) prism=(on|off) "
    r"cart=(.*?) slot=(-?\d+) state=(.*?) play=(.*?) "
    r"rect=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) textfit=([\d.]+),([\d.]+)")


def rows(log):
    return [{"render": (int(match[1]), int(match[2])),
             "out": (int(match[3]), int(match[4])), "mode": match[5],
             "prism": match[6] == "on",
             "cart": match[7], "slot": int(match[8]), "state": match[9],
             "play": match[10],
             "rect": tuple(float(match[i]) for i in range(11, 15)),
             "fit": (float(match[15]), float(match[16]))}
            for match in SHOW_RE.finditer(log)]


def assert_osd(log, width, height, **expected):
    parsed = rows(log)
    console.require(parsed, "OSD draw trace missing")
    console.require(len(parsed) == log.count("CONSOLE_OSD: show "), "incomplete OSD draw trace")
    scale = min(width / 1024, height / 768)
    for row in parsed:
        x, y, w, h = row["rect"]
        fw, fh = row["fit"]
        console.require(min(x, y) >= 0 and min(w, h) > 0
            and x + w <= width + 1 and y + h <= height + 1, f"off-screen OSD: {row}")
        console.require(0 < fw <= w - 24 * scale + 2 and 10 <= fh <= 22 * scale + 1,
            f"clipped or unreadable OSD: {row}")
    matches = [row for row in parsed if all(row[key] == value for key, value in expected.items())]
    console.require(matches, f"OSD did not show {expected}: {parsed}")
    return matches[-1]


def region(image, row):
    x, y, w, h = row["rect"]
    return image[int(y):int(np.ceil(y + h)), int(x):int(np.ceil(x + w))]


def main():
    if not console.audit.BIN.is_file():
        print("CONSOLE_OSD: FAIL — missing binary", file=sys.stderr)
        return 1
    scratch = console.audit.SCRATCH / "console_osd_check"
    scratch.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run_", dir=scratch))

    def capture(tag, actions=None, frames=220, shot=200, extra=None, **kwargs):
        return console.capture(tag, actions, frames, shot, out, extra=extra,
            scene=kwargs.pop("scene", "--cartridge-y2k"), **kwargs)

    try:
        with console.audit.settings_preserved():
            for width, height in ((640, 480), (1024, 768), (1280, 720)):
                on, log = capture(f"ready_{width}x{height}", frames=90, shot=70,
                    width=width, height=height)
                row = assert_osd(log, width, height, render=(320, 240), mode="SOFT",
                    cart="Y2K CGI", slot=1, state="EMPTY")
                off, off_log = capture(f"disabled_{width}x{height}", frames=90, shot=70,
                    extra={"GX_CONSOLE_OSD": "0"}, width=width, height=height)
                console.require(not rows(off_log), "disabled OSD still drew")
                delta = float(np.abs(region(on, row) - region(off, row)).mean())
                console.require(delta > 1.0, f"OSD trace had no visible pixels ({delta:.2f})")

            hidden, hidden_log = capture("hidden", frames=340, shot=320)
            control, _ = capture("hidden_control", frames=340, shot=320,
                extra={"GX_CONSOLE_OSD": "0"})
            console.require("CONSOLE_OSD: hidden=expired" in hidden_log,
                "OSD did not auto-hide")
            console.require(float(np.abs(hidden - control).mean()) < 0.1,
                "auto-hidden OSD left pixels behind")

            _, video_log = capture("video",
                "1.0 key 256;2.0 key 264;2.25 key 264;2.5 key 264;3.0 key 257;"
                "3.5 pad 0 start;4.0 pad 0 start;4.75 key 264;5.0 key 264;"
                "5.25 key 264;5.75 key 257;6.5 key 264;6.75 key 264;"
                "7.0 key 264;7.5 key 257", frames=520, shot=500)
            assert_osd(video_log, 1024, 768, render=(320, 240),
                out=(320, 240), mode="SHARP")
            assert_osd(video_log, 1024, 768, render=(640, 480),
                out=(320, 240), mode="SSAA2x")
            assert_osd(video_log, 1024, 768, render=(1024, 768),
                out=(1024, 768), mode="NATIVE")

            save_dir = out / "slots"
            _, saved_log = capture("saved", "2.0 key 294",
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir), "GX_NO_SETTINGS_SAVE": "0"})
            assert_osd(saved_log, 1024, 768, slot=1, state="SAVED")
            _, loaded_log = capture("loaded", "2.0 key 297",
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir)})
            assert_osd(loaded_log, 1024, 768, slot=1, state="LOADED")

            guard_error = r"CONSOLE_STATE: refusing save under the harness guard:"
            _, save_failed = capture("save_failed", "2.0 key 294",
                allowed_errors=(guard_error,))
            console.require(re.search(guard_error, save_failed), "save-failure fixture did not fire")
            assert_osd(save_failed, 1024, 768, slot=1, state="SAVE FAILED")
            missing_error = r"CONSOLE_STATE: missing slot 1 \("
            _, load_failed = capture("load_failed", "2.0 key 297",
                allowed_errors=(missing_error,))
            console.require(re.search(missing_error, load_failed), "load-failure fixture did not fire")
            assert_osd(load_failed, 1024, 768, slot=1, state="LOAD FAILED")

            idle, idle_log = capture("idle", frames=460, shot=440,
                extra={"GX_CONSOLE_IDLE_SECONDS": "2"})
            console.require("CONSOLE_IDLE: blank" in idle_log
                and "CONSOLE_OSD: hidden=idle" in idle_log, "cartridge OSD ignored idle blanking")
            mean, std = console.image_stats(idle)
            console.require(mean < 6 and std < 12, f"cartridge did not blank ({mean:.2f}/{std:.2f})")
            _, wake_log = capture("wake", "7.5 key 65", frames=540, shot=520,
                extra={"GX_CONSOLE_IDLE_SECONDS": "2"})
            console.require("CONSOLE_IDLE: wake" in wake_log, "cartridge did not wake")
            assert_osd(wake_log.split("CONSOLE_IDLE: wake", 1)[1], 1024, 768,
                cart="Y2K CGI", mode="SOFT", state="EMPTY")

            slot3_dir = out / "slot3"
            slot3_dir.mkdir()
            state = json.loads((save_dir / "console_slot_1.json").read_text())
            state["slot"] = 3
            (slot3_dir / "console_slot_3.json").write_text(json.dumps(state))
            extra = {"GX_CONSOLE_SAVE_DIR": str(slot3_dir), "GX_GUI_TITLE_VIEW": "1"}
            _, discovery = capture("slot3_discovery", frames=80, shot=60,
                scene="--console-shell", extra=extra)
            load = console.target(console.elements(discovery), "LOAD")
            _, slot3_log = capture("slot3_load", console.interaction.click_actions((1.0, load)),
                scene="--console-shell", extra=extra)
            console.require("CONSOLE_STATE: load slot=3 cartridge=y2k" in slot3_log,
                "shell did not load slot 3")
            assert_osd(slot3_log, 1024, 768, cart="Y2K CGI", slot=3, state="LOADED")
    except (OSError, ValueError, RuntimeError, KeyError, subprocess.SubprocessError) as error:
        print(f"CONSOLE_OSD: FAIL — {error}; evidence {out}", file=sys.stderr)
        return 1
    print("CONSOLE_OSD: PASS three-size draw/bounds, disable/auto-hide, live "
          "Soft/Sharp/SSAA 2x/Native, save/load/failure feedback, cartridge "
          "idle/wake and slot-3 shell load")
    print(f"CONSOLE_OSD: evidence {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
