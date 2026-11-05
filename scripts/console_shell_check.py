#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: gX-240 console shell gate. Boots the shell view, drives video mode and
#   save-slot routes, then launches the Y2K cartridge from the boot menu and
#   pins the cold-boot presentation/quality chain.

import re
import subprocess
import sys
from pathlib import Path

import numpy as np

import audit_common as audit


SCRATCH = audit.SCRATCH / "console_shell_check"
VIEW_RE = re.compile(r"CONSOLE: view=shell boot=(\d)")
VIDEO_RE = re.compile(
    r"CONSOLE: video=(\w+) window=(\d+)x(\d+) render=(\d+)x(\d+) filter=(\d)")
BOOT_RE = re.compile(r"CONSOLE: boot=(\d+) scene=(\d+)")
CARTRIDGE_RE = re.compile(
    r"CARTRIDGE: y2k diffraction=(\d+) blades=(\d+) quality=(\d+)")
SCENE_RE = re.compile(
    r"SHOWCASE-Y2K: models=(\d+) lights=(\d+) cartridge=(\d+) textured=(\d+)")
MSAA_RE = re.compile(r"MSAA FBO created: (\d+)x(\d+) msaa=(\d+)")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def capture(tag, actions, frames, shot_frame, out):
    directory = out / tag
    directory.mkdir(parents=True, exist_ok=True)
    for old in directory.glob("gXss_*.png"):
        old.unlink()
    environment = {
        "GX_FIXED_DT": "%.12f" % (1.0 / 60.0),
    }
    if actions:
        environment["GX_AI_INPUT"] = actions
    result = audit.run_scene(
        "--console-shell", environment, width=1024, height=768,
        frames=frames, shot_frame=shot_frame, screenshot_dir=directory,
        timeout=180, check=True)
    log = audit.strip_ansi(result.stdout or "")
    shot = audit.newest_shot(directory)
    require(shot is not None, f"{tag}: no screenshot")
    return audit.load_rgb(shot), log


def image_stats(image):
    luma = audit.luma(image.astype(np.float32))
    return float(luma.mean()), float(luma.std())


def main():
    if not audit.BIN.is_file():
        print("CONSOLE_SHELL: missing binary", file=sys.stderr)
        return 1
    out = SCRATCH
    out.mkdir(parents=True, exist_ok=True)
    try:
        help_text = subprocess.run([str(audit.BIN), "--help"], text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout
        require("--console-shell" in help_text,
            "--console-shell missing from --help")

        with audit.settings_preserved():
            shell, shell_log = capture("shell", None, 80, 60, out)
            sharp, sharp_log = capture("sharp",
                "2.0 move 512 518 200;2.5 click 512 518", 220, 200, out)
            saves, save_log = capture("saves",
                "2.0 move 512 584 200;2.5 click 512 584", 220, 200, out)
            boot, boot_log = capture("boot",
                "2.0 move 512 184 200;2.5 click 512 184", 300, 260, out)

        require(VIEW_RE.search(shell_log), "shell boot trace missing")
        require("gX_Scene: switched to masterIndex 1 [TITLE]" in shell_log,
            "shell did not boot on the title scene")
        shell_mean, shell_std = image_stats(shell)
        require(5.0 < shell_mean < 80.0,
            f"shell mean luma {shell_mean:.1f}")
        require(shell_std > 12.0, f"shell image is flat ({shell_std:.1f})")

        video = VIDEO_RE.findall(sharp_log)
        require(video, "video profile trace missing")
        mode, _ww, _wh, rw, rh, filter_value = video[-1]
        require(mode == "sharp", f"video mode {mode}")
        require((rw, rh) == ("320", "240") and filter_value == "0",
            f"sharp profile {rw}x{rh} filter={filter_value}")
        _sharp_mean, sharp_std = image_stats(sharp)
        require(sharp_std > 12.0, "video route lost the shell frame")

        require("CONSOLE: save=slots" in save_log,
            "save-state route did not fire")
        delta = float(np.abs(saves.astype(np.float32)
            - shell.astype(np.float32)).mean())
        require(delta > 0.5, f"save view did not replace the shell ({delta})")

        boot_rows = BOOT_RE.findall(boot_log)
        require(boot_rows and boot_rows[-1] == ("0", "13"),
            f"cartridge boot trace missing: {boot_rows}")
        video = VIDEO_RE.findall(boot_log)
        require(video and video[-1][0] == "soft"
            and video[-1][3:5] == ("320", "240")
            and video[-1][5] == "1", f"cartridge video profile: {video}")
        require("RENDERER: GFX cold boot" in boot_log,
            "cartridge did not cold boot the GFX renderer")
        cartridge = CARTRIDGE_RE.findall(boot_log)
        require(cartridge and cartridge[-1] == ("1", "6", "3"),
            f"cartridge profile: {cartridge}")
        scene = SCENE_RE.findall(boot_log)
        require(scene and scene[-1][2:] == ("1", "1"),
            f"cartridge scene: {scene}")
        msaa = MSAA_RE.findall(boot_log)
        require(any(width == "320" and height == "240" and samples == "4"
            for width, height, samples in msaa),
            f"cartridge 320x240 4x MSAA missing: {msaa}")
        require("[ ERROR ]" not in shell_log + sharp_log + save_log + boot_log,
            "console shell log contains errors")

        boot_mean, boot_std = image_stats(boot)
        require(5.0 < boot_mean < 100.0,
            f"cartridge mean luma {boot_mean:.1f}")
        require(boot_std > 8.0, f"cartridge image is flat ({boot_std:.1f})")
        delta = float(np.abs(boot.astype(np.float32)
            - shell.astype(np.float32)).mean())
        require(delta > 8.0,
            f"cartridge boot did not replace the shell ({delta:.2f})")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"CONSOLE_SHELL: FAIL — {error}", file=sys.stderr)
        return 1
    print("CONSOLE_SHELL: PASS boot view, sharp video route, save slots, "
          "cartridge cold boot at 320x240 Soft + 4x MSAA + diffraction")
    return 0


if __name__ == "__main__":
    sys.exit(main())
