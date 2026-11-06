#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I008 2x SSAA gate — product video mode, 640x480->320x240 split,
#   native GUI, Y2K/RAY-1/particle scope, save/load and matched GPU frame-cost
#   budget (<=3x the 320x240 baseline).
import csv
import json
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

import console_osd_check as osd
import console_shell_check as console


OUT = None
WARMUP_FRAMES = 100
SAMPLE_FRAMES = 250
TOTAL_FRAMES = 400
MAX_RATIO = 3.0


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
    return parsed[-1]


def profile_total(path):
    with Path(path).open(encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    values = [float(row["total_ms"]) for row in rows
              if WARMUP_FRAMES <= int(row["frame"]) < WARMUP_FRAMES + SAMPLE_FRAMES]
    console.require(len(values) >= SAMPLE_FRAMES // 2,
        f"GPU profile has too few measured frames: {len(values)}")
    return statistics.median(values)


def budget_ratio(base_ms, ssaa_ms):
    console.require(base_ms > 0.0 and ssaa_ms > 0.0,
        f"invalid frame cost {base_ms}/{ssaa_ms}")
    ratio = ssaa_ms / base_ms
    console.require(ratio <= MAX_RATIO,
        f"2x SSAA frame cost {ratio:.3f}x exceeds {MAX_RATIO:.1f}x "
        f"(base {base_ms:.3f} ms, SSAA {ssaa_ms:.3f} ms)")
    return ratio


def run_profile(tag, ssaa):
    environment = console.audit.scene_env({
        "GX_FIXED_DT": "%.12f" % (1.0 / 60.0),
        "GX_BENCHMARK_UNCAPPED": "1",
        "GX_BENCHMARK_NO_FLUSH": "1",
        "GX_NO_SETTINGS_SAVE": "1",
    }, clean=True)
    if ssaa:
        environment["GX_CONSOLE_SSAA"] = "1"
    path = OUT / f"{tag}.csv"
    result = console.audit.run_scene("--cartridge-y2k", environment,
        width=1024, height=768, fps=None, frames=TOTAL_FRAMES, timeout=240,
        check=False, extra_args=("--gfx-profile=" + str(path), "--seed", "1337"))
    (OUT / f"{tag}.log").write_text(console.audit.strip_ansi(result.stdout or ""),
        encoding="utf-8")
    console.require(result.returncode == 0,
        f"{tag}: profile run exited {result.returncode}")
    console.require(path.is_file(), f"{tag}: GPU profile missing")
    return profile_total(path)


def scene_metrics(image):
    gray = image.mean(axis=2)
    crop = gray[140:520, 700:980]
    laplacian = np.abs(4.0 * crop[1:-1, 1:-1] - crop[:-2, 1:-1]
        - crop[2:, 1:-1] - crop[1:-1, :-2] - crop[1:-1, 2:])
    return float(laplacian.mean())


def scene_coverage(image):
    return float(np.mean(image.mean(axis=2) > 8.0))


def saved_ssaa(path):
    state = json.loads(Path(path).read_text(encoding="utf-8"))
    return state.get("video", {}).get("ssaa")


def main():
    if not console.audit.BIN.is_file():
        print("CONSOLE_SSAA: FAIL — missing binary", file=sys.stderr)
        return 1
    scratch = console.audit.SCRATCH / "console_ssaa_check"
    scratch.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run_", dir=scratch))
    global OUT
    OUT = out

    try:
        with console.audit.settings_preserved():
            # Matched 60 Hz GPU cost: baseline 320x240 vs 2x SSAA.
            base_ms = run_profile("base", False)
            ssaa_ms = run_profile("ssaa", True)
            ratio = budget_ratio(base_ms, ssaa_ms)

            # Product menu path: two Video activations reach SSAA 2x.
            menu_script = ("1.0 key 256;2.0 key 264;2.25 key 264;2.5 key 264;"
                "3.0 key 257;4.0 key 264;4.25 key 264;4.5 key 264;5.0 key 257")
            _, menu_log = capture("menu_ssaa", menu_script, frames=340, shot=320)
            console.require("CONSOLE: video=ssaa2x" in menu_log,
                "pause Video cycle did not select SSAA")
            osd_has(menu_log, render=(640, 480), out=(320, 240), mode="SSAA2x")
            menu_rows = console.elements(menu_log, 5)
            console.require(any(row["text"] == "Video: SSAA 2x"
                and row["visible"] and row["clickable"] for row in menu_rows),
                "pause menu did not refresh the SSAA video label")

            # Steady-state visual A/B; the native OSD stays outside the crop.
            base_image, base_log = capture("visual_base", frames=380, shot=360,
                extra={"GX_T_CARTRIDGE_OSD": "0"})
            ssaa_image, ssaa_log = capture("visual_ssaa", frames=380, shot=360,
                extra={"GX_CONSOLE_SSAA": "1", "GX_T_CARTRIDGE_OSD": "0"})
            osd_has(base_log, render=(320, 240), out=(320, 240), mode="SOFT")
            osd_has(ssaa_log, render=(640, 480), out=(320, 240), mode="SSAA2x")
            visual_delta = float(np.abs(base_image - ssaa_image).mean())
            base_hf = scene_metrics(base_image)
            ssaa_hf = scene_metrics(ssaa_image)
            base_cov = scene_coverage(base_image)
            ssaa_cov = scene_coverage(ssaa_image)
            console.require(visual_delta > 0.05,
                f"SSAA output did not change the frame ({visual_delta:.3f})")
            console.require(ssaa_cov > base_cov * 0.75,
                f"SSAA output did not fill the viewport "
                f"(coverage {ssaa_cov:.3f} vs {base_cov:.3f})")
            console.require(ssaa_hf <= base_hf * 1.25,
                f"SSAA added high-frequency aliasing ({ssaa_hf:.3f} vs {base_hf:.3f})")

            # RAY-1 shares the direct renderer-scene path; select SSAA in the
            # shell first so the product video route owns the mode.
            shell_extra = {"GX_T_CARTRIDGE_OSD": "0"}
            _, ray_discovery = capture("ray_discovery", None, frames=80, shot=60,
                scene="--console-shell", extra=shell_extra)
            ray_rows = console.elements(ray_discovery)
            ray = console.target(ray_rows, "RAY-1 TECH DEMO")
            ssaa_button = console.target(ray_rows, "320x240 SSAA 2x")
            _, ray_log = capture("ray_ssaa",
                console.interaction.click_actions((1.0, ssaa_button), (2.0, ray)),
                frames=240, shot=220, scene="--console-shell", extra=shell_extra)
            osd_has(ray_log, cart="RAY-1", render=(640, 480),
                out=(320, 240), mode="SSAA2x")

            # Particle sandbox consumes the same resolve through its
            # GUI-composite background capture.
            _, particle_discovery = capture("particle_discovery", None,
                frames=80, shot=60, scene="--console-shell", extra=shell_extra)
            particle_rows = console.elements(particle_discovery)
            particle = console.target(particle_rows, "PARTICLE SANDBOX")
            ssaa_button = console.target(particle_rows, "320x240 SSAA 2x")
            _, particle_log = capture("particle_ssaa",
                console.interaction.click_actions((1.0, ssaa_button),
                    (2.0, particle)),
                frames=280, shot=260, scene="--console-shell", extra=shell_extra)
            osd_has(particle_log, cart="PARTICLE SANDBOX", render=(640, 480),
                out=(320, 240), mode="SSAA2x")
            console.require("CONSOLE_COMPOSITE: present=320x240 "
                "source=640x480 prism=0" in particle_log,
                "particle composite did not consume the 320x240 resolve")

            # Save state persists the video factor, and a shell load applies it.
            save_dir = out / "slots"
            _, save_log = capture("save_ssaa", "2.0 key 294",
                frames=180, shot=160,
                extra={"GX_CONSOLE_SSAA": "1",
                       "GX_CONSOLE_SAVE_DIR": str(save_dir),
                       "GX_NO_SETTINGS_SAVE": "0"})
            osd_has(save_log, render=(640, 480), out=(320, 240), mode="SSAA2x")
            state_path = save_dir / "console_slot_1.json"
            console.require(state_path.is_file(), "SSAA save state missing")
            console.require(saved_ssaa(state_path) == 2,
                "save state did not persist ssaa=2")
            load_extra = {"GX_GUI_TITLE_VIEW": "1",
                          "GX_CONSOLE_SAVE_DIR": str(save_dir)}
            _, load_discovery = capture("load_discovery", None, frames=80, shot=60,
                scene="--console-shell", extra=load_extra)
            load = console.target(console.elements(load_discovery), "LOAD")
            _, load_log = capture("load_ssaa",
                console.interaction.click_actions((1.0, load)),
                frames=220, shot=200, scene="--console-shell", extra=load_extra)
            console.require("CONSOLE_STATE: load slot=1" in load_log,
                "shell did not load the SSAA slot")
            osd_has(load_log, cart="Y2K CGI", render=(640, 480),
                out=(320, 240), mode="SSAA2x")
    except (OSError, ValueError, RuntimeError, KeyError, subprocess.SubprocessError) as error:
        print(f"CONSOLE_SSAA: FAIL — {error}; evidence {out}", file=sys.stderr)
        return 1
    print(f"CONSOLE_SSAA: PASS 640x480->320x240, menu/shell/save/RAY-1/particle; "
          f"GPU cost {ratio:.3f}x of 320x240")
    print(f"CONSOLE_SSAA: evidence {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
