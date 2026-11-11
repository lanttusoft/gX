#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I009 PRISM-1 gate — RGB565 + fixed Bayer, horizontal scanlines and a
#   subtle RGB triad, layered after the 320x240 resolve/standard 240p canvas.
#   Covers shell/pause controls, save/load, Soft and SSAA, Y2K/RAY-1/particle
#   output; also enforces a small full-frame CPU guard.

import csv
import json
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import console_osd_check as osd
import console_shell_check as console


OUT = None
WARMUP_FRAMES = 100
SAMPLE_FRAMES = 250
TOTAL_FRAMES = 400
MAX_CPU_RATIO = 1.25
OUTPUT_SIZE = (320, 240)
FIXTURE_SIZE = (1024, 768)
FIXTURE_FRAME = 180
FIXTURE_FRAMES = 200


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


def output_pixels(image):
    console.require(image.shape[:2] == (FIXTURE_SIZE[1], FIXTURE_SIZE[0]),
        f"fixture capture size {image.shape[:2]}, expected {FIXTURE_SIZE[::-1]}")
    pixels = np.clip(np.rint(image), 0.0, 255.0).astype(np.uint8)
    return np.asarray(Image.fromarray(pixels).resize(OUTPUT_SIZE, Image.NEAREST),
        dtype=np.float64)


def row_means(pixels):
    return pixels.mean(axis=(1, 2))


def bright_parity(pixels):
    means = row_means(pixels)
    return 0 if means[0::2].mean() >= means[1::2].mean() else 1


def scanline_ratio(pixels):
    means = row_means(pixels)
    even = float(means[0::2].mean())
    odd = float(means[1::2].mean())
    console.require(even > 0.0 and odd > 0.0, f"empty frame {even}/{odd}")
    return max(even, odd) / min(even, odd)


def smooth_x(pixels, radius=12):
    pad = np.pad(pixels, ((0, 0), (radius, radius), (0, 0)), mode="edge")
    windows = np.lib.stride_tricks.sliding_window_view(pad, 2 * radius + 1,
        axis=1)
    return windows.mean(axis=-1)


def triad_residuals(pixels):
    bright = pixels[bright_parity(pixels)::2]
    smooth = smooth_x(bright)
    residual = bright / np.maximum(smooth, 1.0)
    return [[float(residual[:, phase::3, channel].mean())
             for channel in range(3)] for phase in range(3)]


def channel_levels(pixels):
    bright = pixels[bright_parity(pixels)::2]
    band = bright[8:60, 20:300]
    levels = []
    for channel in range(3):
        counts = []
        for phase in range(3):
            values = np.rint(band[:, phase::3, channel]).astype(int)
            counts.append(len(np.unique(values)))
        levels.append(max(counts))
    return levels


def budget_ratio(base_ms, prism_ms):
    console.require(base_ms > 0.0 and prism_ms > 0.0,
        f"invalid frame cost {base_ms}/{prism_ms}")
    ratio = prism_ms / base_ms
    console.require(ratio <= MAX_CPU_RATIO,
        f"PRISM-1 frame cost {ratio:.3f}x exceeds {MAX_CPU_RATIO:.2f}x "
        f"(base {base_ms:.3f} ms, PRISM {prism_ms:.3f} ms)")
    return ratio


def validate_prism(off, on, repeat):
    off_pixels = output_pixels(off)
    on_pixels = output_pixels(on)
    repeat_pixels = output_pixels(repeat)

    off_scan = scanline_ratio(off_pixels)
    on_scan = scanline_ratio(on_pixels)
    console.require(abs(off_scan - 1.0) <= 0.05,
        f"baseline already has scanlines ({off_scan:.3f}x)")
    console.require(1.15 <= on_scan <= 1.9,
        f"PRISM-1 scanline ratio {on_scan:.3f} outside 1.15..1.9")

    off_levels = channel_levels(off_pixels)
    on_levels = channel_levels(on_pixels)
    console.require(on_levels[0] <= 40 and on_levels[1] <= 72
        and on_levels[2] <= 40,
        f"RGB565 level counts too high: {on_levels}")
    console.require(on_levels[0] >= 8 and on_levels[1] >= 12
        and on_levels[2] >= 8,
        f"quantizer fixture too flat: {on_levels}")
    console.require(off_levels[0] >= on_levels[0] * 2
        and off_levels[1] >= on_levels[1] * 2
        and off_levels[2] >= on_levels[2] * 2,
        f"PRISM-1 did not quantize enough: off {off_levels} on {on_levels}")

    residuals = triad_residuals(on_pixels)
    for channel, phase in ((0, 0), (1, 1), (2, 2)):
        expected = residuals[phase][channel]
        others = [residuals[p][channel] for p in range(3) if p != phase]
        console.require(expected > max(others) * 1.03,
            f"triad phase {phase} does not boost channel {channel}: "
            f"{residuals}")

    delta = float(np.abs(on_pixels - off_pixels).mean())
    console.require(delta > 1.0,
        f"PRISM-1 did not change the frame ({delta:.3f})")
    repeat_delta = float(np.abs(on_pixels - repeat_pixels).max())
    console.require(repeat_delta == 0.0,
        f"PRISM-1 is not deterministic across runs ({repeat_delta:.3f})")
    coverage = float(np.mean(on_pixels.mean(axis=2) > 8.0))
    console.require(coverage > 0.5,
        f"PRISM-1 output did not fill the viewport ({coverage:.3f})")
    return {
        "scanline": on_scan,
        "levels": on_levels,
        "baseline_levels": off_levels,
        "triad": residuals,
        "delta": delta,
    }


def capture_fixture(tag, prism):
    directory = OUT / tag
    directory.mkdir(parents=True, exist_ok=True)
    environment = console.audit.scene_env({
        "GX_CONSOLE_PRISM": str(prism),
        "GX_VIRTUAL_WIDTH": "320",
        "GX_VIRTUAL_HEIGHT": "240",
        "GX_PRESENT_FILTER": "0",
        "GX_T_DITHER_MODE": "0",
        "GX_T_PALETTE_MODE": "0",
        "GX_T_INTERLACE_MODE": "0",
        "GX_FIXED_DT": "%.12f" % (1.0 / 60.0),
    }, clean=True)
    result = console.audit.run_scene("--showcase-minimal", environment,
        width=FIXTURE_SIZE[0], height=FIXTURE_SIZE[1], frames=FIXTURE_FRAMES,
        shot_frame=FIXTURE_FRAME, screenshot_dir=directory, timeout=180,
        check=False, extra_args=("--seed", "424242"))
    (directory / "run.log").write_text(
        console.audit.strip_ansi(result.stdout or ""), encoding="utf-8")
    console.require(result.returncode == 0,
        f"{tag}: fixture exited {result.returncode}; {directory}/run.log")
    shot = console.audit.newest_shot(directory)
    console.require(shot is not None, f"{tag}: fixture screenshot missing")
    return console.audit.load_rgb(shot)


def cpu_median(path):
    with Path(path).open(encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    values = [float(row["cpu_ms"]) for row in rows
              if WARMUP_FRAMES <= int(row["frame"])
              < WARMUP_FRAMES + SAMPLE_FRAMES]
    console.require(len(values) >= SAMPLE_FRAMES // 2,
        f"CPU profile has too few measured frames: {len(values)}")
    return statistics.median(values)


def run_profile(tag, prism):
    path = OUT / f"{tag}.csv"
    environment = console.audit.scene_env({
        "GX_CONSOLE_PRISM": str(prism),
        "GX_CONSOLE_SSAA": "0",
        "GX_FIXED_DT": "%.12f" % (1.0 / 60.0),
        "GX_BENCHMARK_UNCAPPED": "1",
        "GX_BENCHMARK_NO_FLUSH": "1",
        "GX_BENCHMARK_CPU_CSV": str(path),
    }, clean=True)
    result = console.audit.run_scene("--cartridge-y2k", environment,
        width=1024, height=768, fps=60, frames=TOTAL_FRAMES, timeout=240,
        check=False, extra_args=("--seed", "1337"))
    (OUT / f"{tag}.log").write_text(
        console.audit.strip_ansi(result.stdout or ""), encoding="utf-8")
    console.require(result.returncode == 0,
        f"{tag}: profile exited {result.returncode}")
    console.require(path.is_file(), f"{tag}: CPU profile missing")
    return cpu_median(path)


def save_video(path):
    return json.loads(Path(path).read_text(encoding="utf-8")).get("video", {})


def main():
    if not console.audit.BIN.is_file():
        print("CONSOLE_PRISM: FAIL — missing binary", file=sys.stderr)
        return 1
    scratch = console.audit.SCRATCH / "console_prism_check"
    scratch.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run_", dir=scratch))
    global OUT
    OUT = out

    try:
        with console.audit.console_settings():
            # Precise RGB565/scanline/triad A/B on the smooth showcase fixture.
            off = capture_fixture("fixture_off", 0)
            on = capture_fixture("fixture_on", 1)
            repeat = capture_fixture("fixture_on_repeat", 1)
            metrics = validate_prism(off, on, repeat)

            # Matched full-frame CPU guard; GPU work is a single 320x240 pass.
            off_costs = [run_profile("perf_off1", 0), run_profile("perf_off2", 0)]
            on_costs = [run_profile("perf_on1", 1), run_profile("perf_on2", 1)]
            ratio = budget_ratio(statistics.median(off_costs),
                statistics.median(on_costs))

            # Shell: select 240p, enable PRISM-1, boot Y2K.
            _, discovery = capture("shell_discovery", None, 80, 60,
                scene="--console-shell")
            rows = console.elements(discovery)
            soft = console.target(rows, "320x240 SOFT")
            prism = console.target(rows, "PRISM-1: OFF")
            boot = console.target(rows, "Y2K CGI DEMO")
            _, shell_log = capture("shell_y2k_prism",
                console.interaction.click_actions((1.0, soft), (2.0, prism),
                    (3.0, boot)),
                frames=340, shot=320, scene="--console-shell")
            console.require("CONSOLE: video=soft" in shell_log,
                "shell did not select the 240p Soft profile")
            console.require("CONSOLE: prism=1" in shell_log,
                "shell did not enable PRISM-1")
            osd_has(shell_log, cart="Y2K CGI", render=(320, 240),
                out=(320, 240), mode="SOFT", prism=True)

            # Shell: PRISM-1 combines with the 2x SSAA resolve.
            _, discovery = capture("shell_ssaa_discovery", None, 80, 60,
                scene="--console-shell")
            rows = console.elements(discovery)
            ssaa = console.target(rows, "320x240 SSAA 2x")
            prism = console.target(rows, "PRISM-1: OFF")
            boot = console.target(rows, "Y2K CGI DEMO")
            _, ssaa_log = capture("shell_y2k_ssaa_prism",
                console.interaction.click_actions((1.0, ssaa), (2.0, prism),
                    (3.0, boot)),
                frames=360, shot=340, scene="--console-shell")
            osd_has(ssaa_log, cart="Y2K CGI", render=(640, 480),
                out=(320, 240), mode="SSAA2x", prism=True)

            # Pause menu: toggle on, then off again; the label and focus stay put.
            _, discovery = capture("pause_discovery", "1.0 key 256", 100, 80)
            pause_prism = console.target(console.elements(discovery, 5),
                "PRISM-1: OFF")
            console.require(pause_prism["truncated"] == 0
                and pause_prism["overflow"] == 0,
                "pause PRISM-1 row starts clipped")
            toggle_script = ("1.0 key 256;2.0 key 264;2.25 key 264;"
                "2.5 key 264;2.75 key 264;3.25 key 257;4.0 key 257")
            _, pause_log = capture("pause_prism_toggle", toggle_script,
                frames=340, shot=320)
            console.require(pause_log.count("CONSOLE: prism=1") == 1
                and pause_log.count("CONSOLE: prism=0") == 1,
                "pause PRISM-1 did not toggle on and off")
            console.require("PRISM-1: ON" in pause_log
                and "PRISM-1: OFF" in pause_log,
                "pause PRISM-1 label did not refresh")
            console.require("GUI_OVERFLOW" not in pause_log,
                "pause PRISM-1 label overflowed")
            osd_has(pause_log, cart="Y2K CGI", mode="SOFT", prism=True)
            osd_has(pause_log, cart="Y2K CGI", mode="SOFT", prism=False)

            #ai: a live first enable must change scene pixels, not only the
            #   label; the output pass must exist before the toggle.
            off_script = ("1.0 key 256;2.0 key 264;2.25 key 264;"
                "2.5 key 264;2.75 key 264")
            on_script = off_script + ";3.25 key 257"
            pause_off, _ = capture("pause_live_off", off_script, 280, 260)
            pause_on, pause_on_log = capture("pause_live_on", on_script, 280, 260)
            console.require("CONSOLE: prism=1" in pause_on_log,
                "pause PRISM-1 live toggle did not enable the pass")
            live_off = pause_off[80:600, 0:200]
            live_on = pause_on[80:600, 0:200]
            live_delta = float(np.abs(live_on - live_off).mean())
            console.require(live_delta > 1.0,
                f"pause PRISM-1 live toggle changed no scene pixels "
                f"({live_delta:.3f})")

            # Native mode keeps the preference but disables the output row.
            _, discovery = capture("shell_native_discovery", None, 80, 60,
                scene="--console-shell")
            native = console.target(console.elements(discovery), "NATIVE")
            _, native_log = capture("shell_native",
                console.interaction.click_actions((1.0, native)),
                frames=180, shot=160, scene="--console-shell")
            native_rows = console.elements(native_log)
            console.require(any(row["text"] == "PRISM-1: N/A"
                and row["disabled"] for row in native_rows),
                "native mode did not disable the PRISM-1 row")
            osd_has(native_log, mode="NATIVE", prism=False)

            # RAY-1 uses the direct console path; particle uses the composite
            # background capture, which now reads the same PRISM present target.
            _, discovery = capture("ray_discovery", None, 80, 60,
                scene="--console-shell")
            rows = console.elements(discovery)
            soft = console.target(rows, "320x240 SOFT")
            prism = console.target(rows, "PRISM-1: OFF")
            ray = console.target(rows, "RAY-1 TECH DEMO")
            _, ray_log = capture("ray_prism",
                console.interaction.click_actions((1.0, soft), (2.0, prism),
                    (3.0, ray)),
                frames=360, shot=340, scene="--console-shell")
            osd_has(ray_log, cart="RAY-1", mode="SOFT", prism=True)

            _, discovery = capture("particle_discovery", None, 80, 60,
                scene="--console-shell")
            rows = console.elements(discovery)
            soft = console.target(rows, "320x240 SOFT")
            prism = console.target(rows, "PRISM-1: OFF")
            particle = console.target(rows, "PARTICLE SANDBOX")
            _, particle_log = capture("particle_prism",
                console.interaction.click_actions((1.0, soft), (2.0, prism),
                    (3.0, particle)),
                frames=380, shot=360, scene="--console-shell")
            osd_has(particle_log, cart="PARTICLE SANDBOX", mode="SOFT",
                prism=True)
            console.require("CONSOLE_COMPOSITE: present=320x240 "
                "source=320x240 prism=1" in particle_log,
                "particle composite did not consume the PRISM target")

            # Save state stores the toggle; a shell load restores it.
            save_dir = out / "slots"
            save_script = ("1.0 key 256;2.0 key 264;2.25 key 264;"
                "2.5 key 264;2.75 key 264;3.25 key 257;4.0 key 256;"
                "5.0 key 294")
            _, save_log = capture("save_prism", save_script,
                frames=420, shot=400,
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir),
                       "GX_NO_SETTINGS_SAVE": "0"})
            state_path = save_dir / "console_slot_1.json"
            console.require(state_path.is_file(), "PRISM-1 save state missing")
            console.require(save_video(state_path).get("prism") == 1,
                "save state did not persist video.prism=1")
            load_extra = {"GX_GUI_TITLE_VIEW": "1",
                          "GX_CONSOLE_SAVE_DIR": str(save_dir)}
            _, discovery = capture("load_discovery", None, 80, 60,
                scene="--console-shell", extra=load_extra)
            load = console.target(console.elements(discovery), "LOAD")
            _, load_log = capture("load_prism",
                console.interaction.click_actions((1.0, load)),
                frames=240, shot=220, scene="--console-shell",
                extra=load_extra)
            console.require("CONSOLE_STATE: load slot=1" in load_log,
                "shell did not load the PRISM-1 slot")
            osd_has(load_log, cart="Y2K CGI", mode="SOFT", prism=True)
    except (OSError, ValueError, RuntimeError, KeyError,
            subprocess.SubprocessError) as error:
        print(f"CONSOLE_PRISM: FAIL — {error}; evidence {out}", file=sys.stderr)
        return 1
    print("CONSOLE_PRISM: PASS RGB565 levels r/g/b %d/%d/%d, scanline %.3fx, "
          "triad phases, deterministic repeat; shell/pause/save, Soft+SSAA, "
          "Y2K/RAY-1/particle; CPU cost %.3fx"
          % (metrics["levels"][0], metrics["levels"][1], metrics["levels"][2],
             metrics["scanline"], ratio))
    print(f"CONSOLE_PRISM: evidence {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
