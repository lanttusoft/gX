#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: gX-240 console shell gate. Boots the shell view, drives video mode and
#   save-slot routes, then launches the Y2K cartridge from the boot menu and
#   pins the cold-boot presentation/quality chain.

import json
import re
import subprocess
import sys
import tempfile
from itertools import combinations
from pathlib import Path

import numpy as np

import audit_common as audit
import gui_interaction_check as interaction
import gui_layout_check as layout


SCRATCH = audit.SCRATCH / "console_shell_check"
VIEW_RE = re.compile(r"CONSOLE: view=shell boot=(\d)")
VIDEO_RE = re.compile(
    r"CONSOLE: video=(\w+) window=(\d+)x(\d+) render=(\d+)x(\d+) "
    r"out=(\d+)x(\d+) filter=(\d)")
BOOT_RE = re.compile(r"CONSOLE: boot=(\d+) scene=(\d+)")
CARTRIDGE_RE = re.compile(
    r"CARTRIDGE: y2k diffraction=(\d+) blades=(\d+) quality=(\d+)")
SCENE_RE = re.compile(
    r"SHOWCASE-Y2K: models=(\d+) lights=(\d+) cartridge=(\d+) textured=(\d+)")
MSAA_RE = re.compile(r"MSAA FBO created: (\d+)x(\d+) msaa=(\d+)")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


#ai: inspect the latest build only, require complete draw-truth telemetry, and
#   resolve clicks from labels so a layout change cannot silently miscalibrate input.
def elements(log, canvas=0):
    builds = [match for match in interaction.BUILD_RE.finditer(log)
              if int(match.group(1)) == canvas]
    require(builds, f"canvas {canvas}: build trace missing")
    rows = []
    for match in layout.ELEMENT_RE.finditer(log, builds[-1].end()):
        if int(match.group(1)) != canvas:
            continue
        rows.append({
            "canvas": canvas, "element": int(match.group(2)),
            "name": match.group(3), "text": match.group(4),
            "rect": tuple(float(match.group(i)) for i in range(5, 9)),
            "visible": int(match.group(9)), "disabled": int(match.group(10)),
            "clickable": int(match.group(11)), "focusable": int(match.group(12)),
            "measure": (float(match.group(13)), float(match.group(14))),
            "truncated": int(match.group(16)), "overflow": int(match.group(17)),
            "shrink": float(match.group(18)),
        })
    raw_rows = [row for row in interaction.parse_elements(log[builds[-1].end():])
                if row["canvas"] == canvas]
    require(rows and len(rows) == len(raw_rows)
        and len({row["element"] for row in rows}) == len(rows)
        and all(0 <= row["element"] < int(builds[-1].group(4)) for row in rows),
        f"canvas {canvas}: incomplete element/text-fit trace")
    return rows


def target(rows, text):
    found = [row for row in rows if row["text"] == text and row["visible"]
             and not row["disabled"] and row["clickable"]]
    require(len(found) == 1, f"expected one active '{text}' target, got {len(found)}")
    return found[0]


def assert_layout(log, width, height, expected_focus, canvas=0):
    rows = elements(log, canvas)
    visible = [row for row in rows if row["visible"]]
    focus = [row for row in visible if row["focusable"] and not row["disabled"]]
    require([row["text"] for row in focus] == expected_focus,
        f"{width}x{height} canvas {canvas}: missing/extra focus targets: "
        f"{[row['text'] for row in focus]}")
    labels = [row for row in visible if row["text"]]
    for row in labels:
        x, y, w, h = row["rect"]
        require(w > 0 and h > 0 and min(x, y) >= -1
            and x + w <= width + 1 and y + h <= height + 1,
            f"{width}x{height}: off-screen '{row['text']}' {row['rect']}")
        require(not row["truncated"] and not row["overflow"],
            f"{width}x{height}: text clips in '{row['text']}'")
        require(row["measure"][0] > 0 and row["measure"][1] > 0,
            f"{width}x{height}: missing text bounds for '{row['text']}'")
        if row in focus:
            require(row["clickable"] and row["shrink"] >= 0.85
                and row["measure"][1] >= 12 and h >= 26,
                f"{width}x{height}: unreadable/inert control '{row['text']}'")
    for left, right in combinations(labels, 2):
        x, y, w, h = left["rect"]
        rx, ry, rw, rh = right["rect"]
        require(min(x + w, rx + rw) - max(x, rx) <= 1
            or min(y + h, ry + rh) - max(y, ry) <= 1,
            f"{width}x{height}: overlapping labels/controls "
            f"'{left['text']}' and '{right['text']}'")
    return rows


def focus_actions(count, start=1.0):
    directions = ["down"] * (count + 1) + ["up", "left", "right"]
    return ";".join(f"{start + index * 0.25:.2f} pad 0 dpad-{direction}"
                    for index, direction in enumerate(directions)), directions


def assert_focus_walk(log, rows, directions, canvas=0):
    ids = [row["element"] for row in rows if row["visible"]
           and row["focusable"] and not row["disabled"]]
    walked = [event["element"] for event in interaction.parse_events(log)
              if event["type"] == "focus" and event["canvas"] == canvas]
    require(len(walked) >= len(directions), "controller focus events missing")
    walked = walked[-len(directions):]
    require(set(walked) == set(ids), "controller skipped a target or focused an inert row")
    for previous, current, direction in zip(walked, walked[1:], directions[1:]):
        step = -1 if direction in ("up", "left") else 1
        require(current == ids[(ids.index(previous) + step) % len(ids)],
            f"controller {direction} focus order/wrap is wrong")


#ai: fault probes may allow only their explicit expected diagnostic, never unrelated errors.
def check_errors(log, allowed=()):
    for line in log.splitlines():
        if re.search(r"\[\s*ERROR\s*\]", line):
            require(any(re.search(pattern, line) for pattern in allowed),
                f"unexpected engine error: {line}")


def capture(tag, actions, frames, shot_frame, out, extra=None,
        scene="--console-shell", width=1024, height=768, allowed_errors=()):
    directory = out / tag
    directory.mkdir(parents=True, exist_ok=True)
    for old in directory.glob("gXss_*.png"):
        old.unlink()
    environment = {
        "GX_FIXED_DT": "%.12f" % (1.0 / 60.0),
        "GX_GUI_TRACE": "1", "GX_SKIP_TESTS": "1",
        "GX_EDITOR_DEFAULT_SCENE": "0", "GX_CARTRIDGE_DIFFRACTION": "1",
        "GX_AI_INPUT": actions or "",
        "GX_CONSOLE_SAVE_DIR": str(out / "empty_slots"),
        #ai: default gates exercise the untransformed path; PRISM/grain opt in.
        "GX_CONSOLE_PRISM": "0",
        "GX_CONSOLE_GRAIN": "0",
    }
    if extra:
        environment.update(extra)
    result = audit.run_scene(
        scene, environment, width=width, height=height,
        frames=frames, shot_frame=shot_frame, screenshot_dir=directory,
        timeout=180, check=False, clean=True)
    log = audit.strip_ansi(result.stdout or "")
    (directory / "run.log").write_text(log, encoding="utf-8")
    require(result.returncode == 0, f"{tag}: exited {result.returncode}; {directory}/run.log")
    check_errors(log, allowed_errors)
    shot = audit.newest_shot(directory)
    require(shot is not None, f"{tag}: no screenshot")
    image = audit.load_rgb(shot)
    require(image.shape[:2] == (height, width),
        f"{tag}: capture size {image.shape[:2]}, expected {height}x{width}")
    return image, log


def image_stats(image):
    luma = audit.luma(image.astype(np.float32))
    return float(luma.mean()), float(luma.std())


def main():
    if not audit.BIN.is_file():
        print("CONSOLE_SHELL: missing binary", file=sys.stderr)
        return 1
    SCRATCH.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run_", dir=SCRATCH))
    try:
        help_text = subprocess.run([str(audit.BIN), "--help"], text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout
        require("--console-shell" in help_text,
            "--console-shell missing from --help")

        with audit.settings_preserved():
            shell, shell_log = capture("shell", None, 80, 60, out)
            shell_rows = elements(shell_log)
            sharp_button = target(shell_rows, "320x240 SHARP")
            saves_button = target(shell_rows, "SAVE STATES")
            boot_button = target(shell_rows, "Y2K CGI DEMO")
            sharp, sharp_log = capture("sharp",
                interaction.click_actions((2.0, sharp_button)), 220, 200, out)
            saves, save_log = capture("saves",
                interaction.click_actions((2.0, saves_button)), 220, 200, out)
            boot, boot_log = capture("boot",
                interaction.click_actions((2.0, boot_button)), 300, 260, out)
            idle, idle_log = capture("idle", None, 460, 440, out,
                extra={"GX_CONSOLE_IDLE_SECONDS": "2"})
            wake_x, wake_y = interaction.center(boot_button)
            woke, wake_log = capture("wake", f"7.5 click {wake_x} {wake_y}",
                540, 520, out, extra={"GX_CONSOLE_IDLE_SECONDS": "2"})
            save_dir = out / "save_states"
            save_dir.mkdir(parents=True, exist_ok=True)
            for stale in save_dir.glob("console_slot_*.json"):
                stale.unlink()
            camera_env = {"GX_CAM_YAW": "-47", "GX_CAM_PITCH": "24", "GX_CAM_DIST": "12",
                "GX_CAM_CUT_FRAME": "150", "GX_CAM_CUT_YAW": "70",
                "GX_CAM_CUT_PITCH": "9", "GX_CAM_CUT_DIST": "6",
                "GX_NO_SETTINGS_SAVE": "0"}
            _saved, state_save_log = capture("save_state",
                "2.0 key 294;3.0 key 297;3.5 key 294", 280, 260, out,
                extra={**camera_env, "GX_CONSOLE_SAVE_DIR": str(save_dir)},
                scene="--cartridge-y2k")
            restored = json.loads((save_dir / "console_slot_1.json").read_text())["camera"]
            require([restored[key] for key in ("yaw", "pitch", "dist")]
                == [-47.0, 24.0, 12.0], "F8 did not restore the saved nondefault camera")
            control_dir = out / "camera_no_load"
            _control, _control_log = capture("camera_no_load",
                "2.0 key 294;3.5 key 294", 280, 260, out,
                extra={**camera_env, "GX_CONSOLE_SAVE_DIR": str(control_dir)},
                scene="--cartridge-y2k")
            changed = json.loads((control_dir / "console_slot_1.json").read_text())["camera"]
            require([changed[key] for key in ("yaw", "pitch", "dist")]
                == [23.0, 9.0, 6.0], "camera restore control did not change the orbit")
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
                "2.75 key 264;3.0 key 264;3.25 key 264;3.75 key 257",
                340, 320, out,
                extra={"GX_NO_SETTINGS_SAVE": "1"},
                scene="--cartridge-y2k")
            controller_save_dir = out / "controller_save_states"
            controller_save_dir.mkdir(parents=True, exist_ok=True)
            for stale in controller_save_dir.glob("console_slot_*.json"):
                stale.unlink()
            _controller_menu, controller_menu_log = capture(
                "controller_menu",
                "1.0 pad 0 start 650;2.0 pad 0 dpad-down;2.5 pad 0 south;"
                "3.5 pad 0 start;4.5 pad 0 start;5.0 pad 0 east",
                430, 400, out,
                extra={"GX_CONSOLE_SAVE_DIR": str(controller_save_dir),
                    "GX_NO_SETTINGS_SAVE": "0"},
                scene="--cartridge-y2k")
            _populated, populated_log = capture("populated_saves",
                interaction.click_actions((1.0, saves_button)), 150, 130, out,
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir)})
            load_button = target(elements(populated_log), "LOAD")
            loaded, load_log = capture("load_state",
                interaction.click_actions((1.0, saves_button), (2.5, load_button)),
                260, 240, out,
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir)})
            _wrong_pad, wrong_pad_log = capture("nonprimary_pad",
                "1.0 pad 1 start 650;2.0 pad 1 south;2.5 pad 1 east",
                210, 190, out, scene="--cartridge-y2k")
            require("CONSOLE_INPUT:" not in wrong_pad_log
                and "CONSOLE_MENU: resume" not in wrong_pad_log,
                "nonprimary gamepad activated the console menu")

            shell_focus = ["Y2K CGI DEMO", "RAY-1 TECH DEMO", "PARTICLE SANDBOX",
                "320x240 SOFT", "320x240 SHARP", "320x240 SSAA 2x", "NATIVE",
                "PRISM-1: OFF", "GRAIN: OFF", "SAVE STATES", "BACK"]
            assert_layout(shell_log, 1024, 768, shell_focus)
            assert_layout(save_log, 1024, 768, ["BACK"])
            for width, height in ((640, 480), (1024, 768), (1280, 720)):
                for view, focus, canvas, scene_flag, extra in (
                    ("shell", shell_focus, 0, "--console-shell", {}),
                    ("saves", ["LOAD", "BACK"], 0, "--console-shell",
                        {"GX_GUI_TITLE_VIEW": "1", "GX_CONSOLE_SAVE_DIR": str(save_dir)}),
                    ("pause", ["Resume", "Save Slot 1 (F5)", "Load Slot 1",
                        "Video: Soft", "PRISM-1: OFF", "GRAIN: OFF",
                        "Exit to Shell", "Step (F6)", "FF: Off", "X"], 5,
                        "--cartridge-y2k",
                        {"GX_CONSOLE_SAVE_DIR": str(save_dir)}),
                ):
                    script, directions = focus_actions(len(focus),
                        start=2.0 if view == "pause" else 1.0)
                    if view == "pause":
                        script = "1.0 pad 0 start;" + script
                    _image, log = capture(f"layout_{view}_{width}x{height}",
                        script, 360, 340, out, scene=scene_flag, extra=extra,
                        width=width, height=height)
                    rows = assert_layout(log, width, height, focus, canvas)
                    assert_focus_walk(log, rows, directions, canvas)
            back_script = interaction.click_actions((1.0, saves_button),
                (2.5, target(elements(populated_log), "BACK")))
            _back, back_log = capture("saves_back", back_script, 240, 220, out,
                extra={"GX_CONSOLE_SAVE_DIR": str(save_dir)})
            require(interaction.ordered(interaction.parse_events(back_log), [
                {"type": "title-view", "canvas": 1},
                {"type": "title-view", "canvas": 6},
            ]), "save browser BACK did not return to the console shell")
            _east, east_log = capture("saves_east",
                interaction.click_actions((1.0, saves_button)) + ";2.5 pad 0 east",
                240, 220, out)
            require(interaction.ordered(interaction.parse_events(east_log), [
                {"type": "title-view", "canvas": 1},
                {"type": "title-view", "canvas": 6},
            ]), "save browser East did not return to the console shell")

        require(VIEW_RE.search(shell_log), "shell boot trace missing")
        require("gX_Scene: switched to masterIndex 1 [TITLE]" in shell_log,
            "shell did not boot on the title scene")
        shell_mean, shell_std = image_stats(shell)
        require(5.0 < shell_mean < 80.0,
            f"shell mean luma {shell_mean:.1f}")
        require(shell_std > 12.0, f"shell image is flat ({shell_std:.1f})")

        video = VIDEO_RE.findall(sharp_log)
        require(video, "video profile trace missing")
        mode, ww, wh, rw, rh, ow, oh, filter_value = video[-1]
        require(mode == "sharp", f"video mode {mode}")
        require((ww, wh) == ("1024", "768") and (rw, rh) == ("320", "240")
            and (ow, oh) == ("320", "240") and filter_value == "0",
            f"sharp profile {rw}x{rh} out={ow}x{oh} filter={filter_value}")
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
            and video[-1][5:7] == ("320", "240")
            and video[-1][7] == "1", f"cartridge video profile: {video}")
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

        #ai: a cold boot re-applies the authored Y2K camera after a prior cut.
        reboot_script = (interaction.click_actions((1.0, boot_button))
            + ";5.5 key 256;5.9 key 264;6.2 key 264;6.5 key 264;"
              "6.8 key 264;7.1 key 264;7.4 key 264;7.9 key 257;"
              "10.0 key 257;12.0 move 10 10 200")
        reboot_extra = {"GX_CONSOLE_OSD": "0", "GX_T_CARTRIDGE_OSD": "0",
            "GX_CONSOLE_PRISM": "0", "GX_CONSOLE_GRAIN": "0"}
        plain_reboot, _ = capture("camera_reboot_plain", reboot_script,
            860, 840, out, extra=reboot_extra)
        cut_reboot, _ = capture("camera_reboot_cut", reboot_script,
            860, 840, out,
            extra={**reboot_extra, "GX_CAM_CUT_FRAME": "240",
                   "GX_CAM_CUT_YAW": "120"})
        camera_diff = np.abs(plain_reboot - cut_reboot)
        require(camera_diff.mean() < 3.0
            and (camera_diff.mean(axis=2) > 8).mean() == 0.0,
            "cold boot kept a prior camera cut (mean %.3f)"
            % camera_diff.mean())

        require("CONSOLE_STATE: save slot=1 cartridge=y2k" in state_save_log,
            "cartridge F5 save failed")
        require((save_dir / "console_slot_1.json").is_file(),
            "save slot file missing")
        require("CONSOLE_STATE: load slot=1 cartridge=y2k "
            "camera=-47.0/24.0/12.0" in state_save_log,
            "cartridge F8 load/camera restore failed")
        require("CONSOLE_MENU: active" in menu_save_log
            and "CONSOLE_MENU: save slot=1 result=0" in menu_save_log,
            "ESC system menu did not save slot 1")
        for label, log in (("save", menu_save_log), ("resume", menu_resume_log),
                           ("video", menu_video_log), ("controller", controller_menu_log)):
            require(not any(event["type"] == "respond" and event["canvas"] == 1
                for event in interaction.parse_events(log)),
                f"pause {label} unexpectedly switched the cartridge to the title scene")
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
        require(re.findall(r"CONSOLE_INPUT: start menu=(\d)", controller_menu_log)
            == ["1", "0", "1"],
            "gamepad Start did not toggle exactly once per press")
        require(controller_menu_log.count("CONSOLE_MENU: save slot=1 result=0") == 1
            and (controller_save_dir / "console_slot_1.json").is_file(),
            "gamepad D-pad/South did not navigate and activate Save Slot 1")
        require("CONSOLE_INPUT: east menu=0" in controller_menu_log,
            "gamepad East did not back out of the system menu")
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
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"CONSOLE_SHELL: FAIL — {error}", file=sys.stderr)
        return 1
    print("CONSOLE_SHELL: PASS boot view, sharp video route, save slots, "
          "CRT-safe idle blank/wake (input swallowed), cartridge OSD + cold "
          "boot at 320x240 Soft + 4x MSAA + diffraction, save/load slots, "
          "cold-boot camera reset, "
          "ESC system menu save/load/video/exit, gamepad Start/D-pad/South/East map; "
          "shell/saves/pause layout + focus at 640x480, 1024x768, 1280x720")
    print(f"CONSOLE_SHELL: evidence {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
