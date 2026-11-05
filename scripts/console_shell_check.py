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


def capture(tag, actions, frames, shot_frame, out, extra=None,
        scene="--console-shell"):
    directory = out / tag
    directory.mkdir(parents=True, exist_ok=True)
    for old in directory.glob("gXss_*.png"):
        old.unlink()
    environment = {
        "GX_FIXED_DT": "%.12f" % (1.0 / 60.0),
    }
    if extra:
        environment.update(extra)
    if actions:
        environment["GX_AI_INPUT"] = actions
    result = audit.run_scene(
        scene, environment, width=1024, height=768,
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
            idle, idle_log = capture("idle", None, 460, 440, out,
                extra={"GX_CONSOLE_IDLE_SECONDS": "2"})
            woke, wake_log = capture("wake", "7.5 move 100 700 200",
                540, 520, out, extra={"GX_CONSOLE_IDLE_SECONDS": "2"})
            save_dir = out / "save_states"
            save_dir.mkdir(parents=True, exist_ok=True)
            for stale in save_dir.glob("console_slot_*.json"):
                stale.unlink()
            _saved, state_save_log = capture("save_state",
                "2.0 key 294;3.0 key 297", 260, 240, out,
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir),
                    "GX_NO_SETTINGS_SAVE": "0"},
                scene="--cartridge-y2k")
            _menu_saved, menu_save_log = capture("menu_save",
                "1.0 key 256;2.0 key 264;2.5 key 257", 220, 200, out,
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir),
                    "GX_NO_SETTINGS_SAVE": "0"},
                scene="--cartridge-y2k")
            _menu_resumed, menu_resume_log = capture("menu_resume",
                "1.0 key 256;2.0 key 257", 180, 160, out,
                scene="--cartridge-y2k")
            _menu_video, menu_video_log = capture("menu_video",
                "1.0 key 256;2.0 key 264;2.25 key 264;2.5 key 264;"
                "3.0 key 257", 240, 220, out,
                extra={"GX_NO_SETTINGS_SAVE": "1"},
                scene="--cartridge-y2k")
            _menu_loaded, menu_load_log = capture("menu_load",
                "1.0 key 256;2.0 key 264;2.25 key 264;3.0 key 257",
                240, 220, out,
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir),
                    "GX_NO_SETTINGS_SAVE": "1"},
                scene="--cartridge-y2k")
            menu_exit, menu_exit_log = capture("menu_exit",
                "1.0 key 256;2.0 key 264;2.25 key 264;2.5 key 264;"
                "2.75 key 264;3.25 key 257", 300, 280, out,
                extra={"GX_NO_SETTINGS_SAVE": "1"},
                scene="--cartridge-y2k")
            loaded, load_log = capture("load_state",
                "2.5 move 512 584 200;3.0 click 512 584;"
                "4.0 move 742 216 200;4.5 click 742 216", 320, 300, out,
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir)})

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
        require("CARTRIDGE_OSD: banner" in boot_log,
            "cartridge boot OSD missing")
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

        require("CONSOLE_IDLE: blank" in idle_log,
            "idle timeout did not blank the shell")
        idle_mean, idle_std = image_stats(idle)
        require(idle_mean < 6.0, f"blanked frame mean {idle_mean:.1f}")
        require(idle_std < 12.0, f"blanked frame std {idle_std:.1f}")
        require("CONSOLE_IDLE: blank" in wake_log
            and "CONSOLE_IDLE: wake" in wake_log,
            "idle wake trace missing")
        require("CONSOLE: boot=" not in wake_log,
            "wake input activated a hidden cartridge card")
        wake_mean, wake_std = image_stats(woke)
        require(wake_mean > 5.0 and wake_std > 8.0,
            f"wake did not restore the shell ({wake_mean:.1f}/{wake_std:.1f})")

        require("CONSOLE_STATE: save slot=1 cartridge=y2k" in state_save_log,
            "cartridge F5 save failed")
        require((save_dir / "console_slot_1.json").is_file(),
            "save slot file missing")
        require("CONSOLE_STATE: load slot=1 cartridge=y2k "
            "camera=-32.0/19.0/14.5" in state_save_log,
            "cartridge F8 load/camera restore failed")
        require("CONSOLE_MENU: active" in menu_save_log
            and "CONSOLE_MENU: save slot=1 result=0" in menu_save_log,
            "ESC system menu did not save slot 1")
        require("CONSOLE_MENU: resume" in menu_resume_log,
            "system menu Resume button did not close the overlay")
        require("CONSOLE: video=sharp" in menu_video_log,
            "system menu video route did not select Sharp")
        require("CONSOLE_MENU: load slot=1 result=0" in menu_load_log
            and "CONSOLE_STATE: load slot=1 cartridge=y2k" in menu_load_log,
            "system menu did not load slot 1")
        require("CONSOLE: exit=shell" in menu_exit_log
            and "CONSOLE: view=shell boot=1" in menu_exit_log,
            "system menu did not return to the shell")
        exit_mean, exit_std = image_stats(menu_exit)
        require(exit_mean > 5.0 and exit_std > 8.0,
            f"system menu exit did not show the shell ({exit_mean:.1f}/{exit_std:.1f})")
        require("CONSOLE: load_slot=1" in load_log
            and "CONSOLE_STATE: load slot=1 cartridge=y2k" in load_log,
            "shell LOAD slot route failed")
        require("CARTRIDGE: y2k" in load_log,
            "shell LOAD slot did not boot the cartridge")
        loaded_mean, loaded_std = image_stats(loaded)
        require(loaded_mean > 5.0 and loaded_std > 8.0,
            f"loaded state is not visible ({loaded_mean:.1f}/{loaded_std:.1f})")

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
          "CRT-safe idle blank/wake (input swallowed), cartridge OSD + cold "
          "boot at 320x240 Soft + 4x MSAA + diffraction, save/load slots, "
          "ESC system menu save/load/video/exit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
