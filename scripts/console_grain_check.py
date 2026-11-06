#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I013 film-grain gate — opt-in animated monochrome grain on the
#   320x240 console output, independent of PRISM. Covers product controls,
#   save-state persistence, SSAA composite, animation and a flat-field metric.

import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

import console_osd_check as osd
import console_shell_check as console


OUT = None
FLAT_CROP = (slice(30, 120), slice(200, 800))
MIN_HF_GAIN = 1.30
MIN_ANIM_DELTA = 0.5


def capture(tag, actions=None, frames=220, shot=200, extra=None, **kwargs):
    return console.capture(tag, actions, frames, shot, OUT, extra=extra,
        scene=kwargs.pop("scene", "--cartridge-y2k"), **kwargs)


def osd_has(log, **expected):
    parsed = osd.rows(log)
    console.require(parsed, "OSD trace missing")
    console.require(any(all(row[key] == value for key, value in expected.items())
        for row in parsed),
        f"OSD did not show {expected}: "
        f"{[({k: row[k] for k in expected}, row['mode']) for row in parsed]}")
    osd.assert_osd(log, 1024, 768, **expected)
    return parsed[-1]


def flat_noise(image):
    gray = image[FLAT_CROP].mean(axis=2).astype(np.float32)
    laplacian = np.abs(4.0 * gray[1:-1, 1:-1] - gray[:-2, 1:-1]
        - gray[2:, 1:-1] - gray[1:-1, :-2] - gray[1:-1, 2:])
    return float(laplacian.mean())


def saved_grain(path):
    state = json.loads(Path(path).read_text(encoding="utf-8"))
    return state.get("video", {}).get("grain")


def main():
    if not console.audit.BIN.is_file():
        print("CONSOLE_GRAIN: FAIL — missing binary", file=sys.stderr)
        return 1
    scratch = console.audit.SCRATCH / "console_grain_check"
    scratch.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run_", dir=scratch))
    global OUT
    OUT = out

    try:
        with console.audit.settings_preserved():
            visual_extra = {
                "GX_VIRTUAL_WIDTH": "320", "GX_VIRTUAL_HEIGHT": "240",
                "GX_PRESENT_FILTER": "1", "GX_CONSOLE_OSD": "0",
                "GX_T_CARTRIDGE_OSD": "0",
            }
            off, _ = capture("visual_off", frames=180, shot=160,
                scene="--showcase-minimal", extra=visual_extra)
            on, _ = capture("visual_on", frames=180, shot=160,
                scene="--showcase-minimal",
                extra={**visual_extra, "GX_CONSOLE_GRAIN": "1"})
            repeat, _ = capture("visual_repeat", frames=180, shot=160,
                scene="--showcase-minimal",
                extra={**visual_extra, "GX_CONSOLE_GRAIN": "1"})
            later, _ = capture("visual_later", frames=183, shot=163,
                scene="--showcase-minimal",
                extra={**visual_extra, "GX_CONSOLE_GRAIN": "1"})
            off_later, _ = capture("visual_off_later", frames=183, shot=163,
                scene="--showcase-minimal", extra=visual_extra)

            hf_off = flat_noise(off)
            hf_on = flat_noise(on)
            console.require(hf_on > hf_off * MIN_HF_GAIN,
                f"grain did not add flat-field texture ({hf_on:.3f} vs "
                f"{hf_off:.3f})")
            console.require(float(np.abs(on - off).mean()) > 0.5,
                "grain did not change the frame")
            console.require(float(np.abs(on - repeat).max()) == 0.0,
                "grain is not deterministic at a fixed frame")
            anim_on = float(np.abs(on - later).mean())
            anim_off = float(np.abs(off - off_later).mean())
            console.require(anim_on > max(MIN_ANIM_DELTA, anim_off * 2.0),
                f"grain did not animate ({anim_on:.3f} vs {anim_off:.3f})")

            # Shell toggle, Y2K boot and OSD identity.
            _, discovery = capture("shell_discovery", None, 80, 60,
                scene="--console-shell",
                extra={"GX_T_CARTRIDGE_OSD": "0",
                       "GX_VIRTUAL_WIDTH": "320", "GX_VIRTUAL_HEIGHT": "240",
                       "GX_PRESENT_FILTER": "1"})
            grain = console.target(console.elements(discovery), "GRAIN: OFF")
            boot = console.target(console.elements(discovery), "Y2K CGI DEMO")
            _, shell_log = capture("shell_boot",
                console.interaction.click_actions((1.0, grain), (2.0, boot)),
                frames=300, shot=280, scene="--console-shell",
                extra={"GX_T_CARTRIDGE_OSD": "0",
                       "GX_VIRTUAL_WIDTH": "320", "GX_VIRTUAL_HEIGHT": "240",
                       "GX_PRESENT_FILTER": "1"})
            console.require("CONSOLE: grain=1" in shell_log,
                "shell did not enable grain")
            osd_has(shell_log, cart="Y2K CGI", mode="SOFT", grain=True)

            # Pause toggle on and off, with the row label refreshed.
            toggle = ("1.0 key 256;2.0 key 264;2.25 key 264;2.5 key 264;"
                "2.75 key 264;3.0 key 264;3.5 key 257;4.25 key 257")
            _, pause_log = capture("pause_toggle", toggle, frames=360, shot=340)
            console.require(pause_log.count("CONSOLE: grain=1") == 1
                and pause_log.count("CONSOLE: grain=0") == 1,
                "pause grain did not toggle on and off")
            console.require("GRAIN: ON" in pause_log
                and "GRAIN: OFF" in pause_log,
                "pause grain label did not refresh")
            console.require("GUI_OVERFLOW" not in pause_log,
                "pause grain label overflowed")
            osd_has(pause_log, cart="Y2K CGI", mode="SOFT", grain=True)
            osd_has(pause_log, cart="Y2K CGI", mode="SOFT", grain=False)

            # Save state stores grain; F8 restores it.
            save_dir = out / "slots"
            save_script = ("1.0 key 256;2.0 key 264;2.25 key 264;2.5 key 264;"
                "2.75 key 264;3.0 key 264;3.5 key 257;4.5 key 256;"
                "5.5 key 294")
            _, save_log = capture("save", save_script, frames=440, shot=420,
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir),
                       "GX_NO_SETTINGS_SAVE": "0",
                       "GX_T_CARTRIDGE_OSD": "0"})
            state_path = save_dir / "console_slot_1.json"
            console.require(state_path.is_file(), "grain save state missing")
            console.require(saved_grain(state_path) == 1,
                "save state did not persist video.grain=1")
            _, load_log = capture("load", "2.0 key 297", frames=320, shot=300,
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir),
                       "GX_T_CARTRIDGE_OSD": "0"})
            console.require("CONSOLE_STATE: load slot=1" in load_log,
                "F8 did not load the grain slot")
            osd_has(load_log, cart="Y2K CGI", mode="SOFT", grain=True,
                state="LOADED")

            # Grain survives the particle composite/SSAA route.
            _, discovery = capture("ssaa_discovery", None, 80, 60,
                scene="--console-shell",
                extra={"GX_T_CARTRIDGE_OSD": "0",
                       "GX_VIRTUAL_WIDTH": "320", "GX_VIRTUAL_HEIGHT": "240",
                       "GX_PRESENT_FILTER": "1"})
            rows = console.elements(discovery)
            ssaa = console.target(rows, "320x240 SSAA 2x")
            grain = console.target(rows, "GRAIN: OFF")
            particle = console.target(rows, "PARTICLE SANDBOX")
            _, particle_log = capture("particle_ssaa_grain",
                console.interaction.click_actions((1.0, ssaa), (2.0, grain),
                    (3.0, particle)),
                frames=380, shot=360, scene="--console-shell",
                extra={"GX_T_CARTRIDGE_OSD": "0"})
            osd_has(particle_log, cart="PARTICLE SANDBOX", render=(640, 480),
                out=(320, 240), mode="SSAA2x", grain=True)

            # Native bypasses grain and disables its row.
            _, discovery = capture("native_discovery", None, 80, 60,
                scene="--console-shell", extra={"GX_T_CARTRIDGE_OSD": "0"})
            native = console.target(console.elements(discovery), "NATIVE")
            _, native_log = capture("native",
                console.interaction.click_actions((1.0, native)),
                frames=180, shot=160, scene="--console-shell")
            native_rows = console.elements(native_log)
            console.require(any(row["text"] == "GRAIN: N/A"
                and row["disabled"] for row in native_rows),
                "native mode did not disable the grain row")
            osd_has(native_log, mode="NATIVE", grain=False)
    except (OSError, ValueError, RuntimeError, KeyError,
            subprocess.SubprocessError) as error:
        print(f"CONSOLE_GRAIN: FAIL — {error}; evidence {out}", file=sys.stderr)
        return 1
    print("CONSOLE_GRAIN: PASS animated monochrome grain, deterministic fixed "
          "frame, flat-field gain, shell/pause/save, SSAA particle and native "
          "fallback")
    print(f"CONSOLE_GRAIN: evidence {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
