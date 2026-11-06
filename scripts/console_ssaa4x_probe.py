#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I008 4x SSAA probe — hidden GX_CONSOLE_SSAA=4 path only. Measures
#   the 1280x960 render cost and visual difference against 2x; it is not a
#   product mode and is not wired into shell/pause/save-state controls.

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
PROBE_MAX_RATIO = 8.0


def capture(tag, frames=380, shot=360, extra=None):
    return console.capture(tag, None, frames, shot, OUT, extra=extra,
        scene="--cartridge-y2k")


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


def budget_ratio(base_ms, probe_ms, label):
    console.require(base_ms > 0.0 and probe_ms > 0.0,
        f"invalid frame cost {base_ms}/{probe_ms}")
    ratio = probe_ms / base_ms
    console.require(ratio <= PROBE_MAX_RATIO,
        f"{label} frame cost {ratio:.3f}x exceeds probe ceiling "
        f"{PROBE_MAX_RATIO:.1f}x (base {base_ms:.3f} ms, {label} {probe_ms:.3f} ms)")
    return ratio


def run_profile(tag, ssaa):
    environment = console.audit.scene_env({
        "GX_FIXED_DT": "%.12f" % (1.0 / 60.0),
        "GX_BENCHMARK_UNCAPPED": "1",
        "GX_BENCHMARK_NO_FLUSH": "1",
        "GX_NO_SETTINGS_SAVE": "1",
        "GX_CONSOLE_SSAA": str(ssaa),
    }, clean=True)
    path = OUT / f"{tag}.csv"
    result = console.audit.run_scene("--cartridge-y2k", environment,
        width=1024, height=768, fps=None, frames=TOTAL_FRAMES, timeout=300,
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


def main():
    if not console.audit.BIN.is_file():
        print("CONSOLE_SSAA4X: FAIL — missing binary", file=sys.stderr)
        return 1
    scratch = console.audit.SCRATCH / "console_ssaa4x_probe"
    scratch.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run_", dir=scratch))
    global OUT
    OUT = out

    try:
        with console.audit.settings_preserved():
            base_ms = run_profile("base", 0)
            two_ms = run_profile("two", 1)
            four_ms = run_profile("four", 4)
            ratio4 = budget_ratio(base_ms, four_ms, "4x")
            ratio_vs2 = budget_ratio(two_ms, four_ms, "4x-vs-2x")

            _, log4 = capture("osd4",
                extra={"GX_CONSOLE_SSAA": "4", "GX_T_CARTRIDGE_OSD": "0"})
            osd_has(log4, render=(1280, 960), out=(320, 240), mode="SSAA4x")
            _, log2 = capture("osd2",
                extra={"GX_CONSOLE_SSAA": "1", "GX_T_CARTRIDGE_OSD": "0"})
            osd_has(log2, render=(640, 480), out=(320, 240), mode="SSAA2x")

            shot2, _ = capture("visual2", extra={
                "GX_CONSOLE_SSAA": "1", "GX_CONSOLE_OSD": "0",
                "GX_T_CARTRIDGE_OSD": "0"})
            shot4, _ = capture("visual4", extra={
                "GX_CONSOLE_SSAA": "4", "GX_CONSOLE_OSD": "0",
                "GX_T_CARTRIDGE_OSD": "0"})
            shot4r, _ = capture("visual4_repeat", extra={
                "GX_CONSOLE_SSAA": "4", "GX_CONSOLE_OSD": "0",
                "GX_T_CARTRIDGE_OSD": "0"})
            delta = float(np.abs(shot4 - shot2).mean())
            console.require(delta > 0.05,
                f"4x did not change the frame ({delta:.3f})")
            #ai: the crop's Laplacian is report-only; supersampling can retain
            #   more authored detail while changing the metric either way.
            hf2 = scene_metrics(shot2)
            hf4 = scene_metrics(shot4)
            repeat = float(np.abs(shot4 - shot4r).max())
            console.require(repeat == 0.0,
                f"4x is not deterministic ({repeat:.3f})")
            coverage = float(np.mean(shot4.mean(axis=2) > 8.0))
            console.require(coverage > 0.5,
                f"4x output did not fill the viewport ({coverage:.3f})")

            report = {
                "base_ms": base_ms,
                "two_ms": two_ms,
                "four_ms": four_ms,
                "ratio4": ratio4,
                "ratio_vs2": ratio_vs2,
                "hf2": hf2,
                "hf4": hf4,
                "hf_ratio": hf4 / hf2 if hf2 > 0.0 else 0.0,
                "delta": delta,
                "repeat": repeat,
                "coverage": coverage,
            }
            (OUT / "report.json").write_text(json.dumps(report, indent=2) + "\n",
                encoding="utf-8")
    except (OSError, ValueError, RuntimeError, KeyError,
            subprocess.SubprocessError) as error:
        print(f"CONSOLE_SSAA4X: FAIL — {error}; evidence {out}", file=sys.stderr)
        return 1
    print("CONSOLE_SSAA4X: PASS probe 1280x960->320x240; GPU cost %.3fx "
          "vs 320x240, %.3fx vs 2x; HF %.3f vs %.3f; repeat 0"
          % (ratio4, ratio_vs2, hf4, hf2))
    print(f"CONSOLE_SSAA4X: evidence {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
