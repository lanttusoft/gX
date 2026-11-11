#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: title front-end flow pin — every advertised sub-view must open
#   without crashing and leave the frame visibly changed, and the ESC back
#   route must return to the main menu bit-identically (the fixed clock makes
#   that an exact assertion). The EXTRAS card loop once indexed past its array
#   (12 cards against `tools[10]`) and segfaulted the engine on every attempt
#   to open the tools grid, so this drives the menu exactly like a user would
#   and fails on a non-zero exit.

import argparse
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

import audit_common as audit


OUT = Path("/tmp/opencode/title_flow_check")
FRAMES = 340
SHOT_FRAME = 300
DELTA_MIN = 0.5   # a view switch changes a large part of the frame
RETURN_MAX = 2.5  # a return matches the menu frame; a stray focused-item
                    # delta can reach ~1.8, while a stuck view is >20, so the
                    # threshold leaves room for title polish without hiding a
                    # failed back route.
#opencode: tool-card labels end in " EDITOR" or " LAB"; every card must own a
#   distinct EXTRAS grid cell — a duplicate col/row once stacked the background
#   and sky cards on one slot, making the background editor unreachable and
#   double-printing the labels.
TOOL_LABEL = re.compile(r"( EDITOR| LAB)$")
TRACE_RE = re.compile(
    r'element=(\d+) name="([^"]*)" text="([^"]*)" rect=(\d+),(\d+),(\d+),(\d+)')
DESC_RE = re.compile(r'desc="([^"]*)"')

#ai: full menu order is OPEN GFX, SAVE SLOTS, SETTINGS, DEMOS, EXTRAS, QUIT;
#   key codes are gX_KEY_* (DOWN 264, ENTER 257, ESCAPE 256). ESC is the
#   shared back route on the title and on every sub-view.
#opencode: the title entrance cascade delays menu focus past 1s; start the
#   navigation after it settles so the first Down is not lost.
#opencode: the first Down focuses OPEN GFX (it does not move); include it as a
#   warm-up, then the visible menu navigation.
LOAD_GAME = "2.0 key 264;2.4 key 264;2.8 key 257"
DEMOS = "2.0 key 264;2.4 key 264;2.8 key 264;3.2 key 264;3.6 key 257"
#ai: drive EXTRAS with a mouse click on its fixed 1024x768 card. The keyboard
#   route depends on the title entrance focus lock and a fixed Down count;
#   a slow first frame dropped the early Downs and landed on QUIT, so the
#   "extras" view was really the quit dialog and extras_layout found no cards.
EXTRAS = "2.0 move 511 592 200;2.5 click 511 592"
#ai: the console shell sits in the DEMOS hub; drive both with fixed mouse
#   coordinates so the keyboard focus cascade cannot change the target.
DEMOS_CLICK = "2.0 move 511 545 200;2.5 click 511 545"
CONSOLE = DEMOS_CLICK + ";3.5 move 512 578 200;4.0 click 512 578"
HELP = "2.0 move 759 718 200;2.5 click 759 718"
VIEWS = (
    ("load_game", LOAD_GAME, "view"),
    ("demos", DEMOS, "view"),
    ("extras", EXTRAS, "view"),
    ("credits", "2.0 move 855 723 200;2.5 click 855 723", "view"),
    ("help", HELP, "view"),
    ("escape", "2.0 key 256", "view"),
    ("escape_back", "2.0 key 256;4.0 key 256", "return"),
    ("load_game_back", LOAD_GAME + ";4.2 key 256", "return"),
    ("extras_back", EXTRAS + ";4.5 key 256", "return"),
    ("help_back", HELP + ";4.2 key 256", "return"),
    ("console", CONSOLE, "view"),
    ("console_back", CONSOLE + ";4.6 key 256", "return"),
)


def capture(binary, tag, actions):
    directory = OUT / tag
    directory.mkdir(parents=True, exist_ok=True)
    for stale in directory.glob("gXss_*.png"):
        stale.unlink()
    overrides = {"GX_FIXED_DT": "0.0166666667", "GX_FRAMES": str(FRAMES)}
    if actions:
        overrides["GX_AI_INPUT"] = actions
    environment = audit.scene_env(overrides, clean=True)
    environment["GX_SKIP_TESTS"] = "1"
    result = audit.run_scene("--title", environment, width=1024, height=768,
                             fps=None, frames=FRAMES, shot_frame=SHOT_FRAME,
                             screenshot_dir=directory, timeout=180,
                             binary=binary, check=False)
    if result.returncode != 0:
        raise RuntimeError(
            f"TITLE_FLOW {tag}: engine exited {result.returncode} "
            f"(menu drive crashed)")
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError(f"TITLE_FLOW {tag}: no screenshot produced")
    return audit.load_rgb(shot)


def extras_layout(binary):
    """Drive EXTRAS with the element trace; tool cards must own distinct cells."""
    directory = OUT / "extras_layout"
    directory.mkdir(parents=True, exist_ok=True)
    overrides = {
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(FRAMES),
        "GX_AI_INPUT": EXTRAS,
        "GX_GUI_TRACE": "1",
    }
    environment = audit.scene_env(overrides, clean=True)
    environment["GX_SKIP_TESTS"] = "1"
    result = audit.run_scene("--title", environment, width=1024, height=768,
                             fps=None, frames=FRAMES, shot_frame=SHOT_FRAME,
                             screenshot_dir=directory, timeout=180,
                             binary=binary, check=False)
    if result.returncode != 0:
        raise RuntimeError(
            f"TITLE_FLOW extras_layout: engine exited {result.returncode}")
    log = audit.strip_ansi(result.stdout or "")
    cells = {}
    for line in log.splitlines():
        match = TRACE_RE.search(line)
        if not match:
            continue
        text = match.group(3)
        if not TOOL_LABEL.search(text):
            continue
        #opencode: the tool tier ships in the element description; every card
        #   must carry it so the internal classification cannot rot silently.
        description = DESC_RE.search(line)
        tier = description.group(1).lower() if description else ""
        if "alpha" not in tier and "beta" not in tier:
            raise RuntimeError(
                f"EXTRAS card '{text}' has no alpha/beta tier description")
        cells.setdefault((match.group(4), match.group(5)), []).append(text)
    duplicates = {cell: names for cell, names in cells.items() if len(names) > 1}
    if duplicates:
        detail = "; ".join(f"{cell}: {', '.join(names)}"
                           for cell, names in duplicates.items())
        raise RuntimeError(f"EXTRAS tool cards share a grid cell: {detail}")
    if len(cells) < 6:
        raise RuntimeError(f"EXTRAS tool grid incomplete: {len(cells)} cards")
    #ai: the full menu must keep one renderer-demo shortcut. RUN DEMO was
    #   removed so OPEN GFX is not duplicated; DEMOS remains the hub.
    menu_texts = set(re.findall(
        r'element canvas=0 [^\n]*text="([^"]*)"', log))
    if "OPEN GFX" not in menu_texts:
        raise RuntimeError("full menu lost the OPEN GFX shortcut")
    if "RUN DEMO" in menu_texts:
        raise RuntimeError("full menu reintroduced the duplicate RUN DEMO row")
    return len(cells)


def plain_hint(binary):
    """Fresh-install plain launch must draw the Esc/Back hint pixels."""
    directory = OUT / "plain_hint"
    directory.mkdir(parents=True, exist_ok=True)
    for stale in directory.glob("gXss_*.png"):
        stale.unlink()
    environment = audit.scene_env({
        "GX_SKIPTITLE": "1",
        "GX_SKIP_TESTS": "1",
        "GX_FRAMES": "60",
        "GX_SHOT_FRAME": "60",
        "GX_FIXED_DT": "0.0166666667",
        "GX_SCREENSHOT_DIR": str(directory),
    }, clean=True)
    result = subprocess.run(
        [str(binary or audit.BIN), "--screenWidth", "1024",
         "--screenHeight", "768", "--ai-screenshot-final"],
        cwd=str(audit.ROOT), env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    log = audit.strip_ansi(result.stdout or "")
    if result.returncode != 0:
        raise RuntimeError(
            f"plain launch exited {result.returncode}: {log[-400:]}")
    if "PLAIN_HINT: drawn" not in log:
        raise RuntimeError("plain launch did not draw the Esc/Back hint")
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError("plain launch produced no hint screenshot")
    image = audit.load_rgb(shot)
    if image.shape[:2] != (768, 1024):
        raise RuntimeError(
            f"plain hint capture is {image.shape[1]}x{image.shape[0]}")
    return assert_hint_pixels(image)


def assert_hint_pixels(image):
    """Require the dark hint panel and light glyphs in the screenshot."""
    hint = image[12:44, 12:262]
    background = image[12:44, 300:550]
    darkened = float(background.mean() - hint.mean())
    #ai: M5 — the trace alone is not evidence. Require the dark panel and the
    #   light glyphs in the screenshot, so removing the draw calls fails.
    if darkened < 40.0:
        raise RuntimeError(
            f"plain hint panel is not visible (darkened {darkened:.1f} luma)")
    bright = float((hint > 200.0).all(axis=2).mean())
    if bright < 0.02 or bright > 0.35:
        raise RuntimeError(
            f"plain hint text is not visible (bright fraction {bright:.3f})")
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Drive the title menu and pin every sub-view flow.")
    parser.add_argument("--binary", help="override the engine binary")
    arguments = parser.parse_args()
    if not audit.BIN.is_file():
        print(f"TITLE_FLOW_CHECK: FAIL — missing binary: {audit.BIN}",
              file=sys.stderr)
        return 1
    try:
        OUT.mkdir(parents=True, exist_ok=True)
        baseline = capture(arguments.binary, "baseline", None)
        print(f"{'view':<16} {'delta':>7}  gate")
        failures = []
        for tag, actions, mode in VIEWS:
            frame = capture(arguments.binary, tag, actions)
            delta = float(np.abs(frame - baseline).mean())
            if mode == "view":
                ok = delta >= DELTA_MIN
                note = "changed from the main menu"
            else:
                ok = delta <= RETURN_MAX
                note = "returned to the main menu"
            if not ok:
                failures.append(f"{tag}: delta {delta:.3f} ({mode} failed)")
            print(f"{tag:<16} {delta:7.3f}  {'ok' if ok else 'FAIL'} {note}")
        cards = extras_layout(arguments.binary)
        print(f"{'extras_layout':<16} {cards:>7}  ok {cards} tool cards, "
              f"distinct grid cells")
        plain_hint(arguments.binary)
        print(f"{'plain_hint':<16} {'ok':>7}  Esc/Back hint branch ran")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"TITLE_FLOW_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    if failures:
        for failure in failures:
            print(f"TITLE_FLOW_CHECK: FAIL — {failure}", file=sys.stderr)
        return 1
    print("TITLE_FLOW_CHECK: PASS (sub-views open changed and the ESC back "
          "routes return; the engine survives)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
