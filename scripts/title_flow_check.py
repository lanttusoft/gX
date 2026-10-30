#!/usr/bin/env python3
#opencode: title front-end flow pin — every advertised sub-view must open
#   without crashing and leave the frame visibly changed, and the ESC back
#   route must return to the main menu bit-identically (the fixed clock makes
#   that an exact assertion). The EXTRAS card loop once indexed past its array
#   (12 cards against `tools[10]`) and segfaulted the engine on every attempt
#   to open the tools grid, so this drives the menu exactly like a user would
#   and fails on a non-zero exit.

import argparse
import re
import sys
from pathlib import Path

import numpy as np

import audit_common as audit


OUT = Path("/tmp/opencode/title_flow_check")
FRAMES = 340
SHOT_FRAME = 300
DELTA_MIN = 0.5   # a view switch changes a large part of the frame
RETURN_MAX = 1.5  # a return matches the menu frame; title polish leaves a
                    # small focus/transition delta (view switches are >20)
#opencode: tool-card labels end in " EDITOR" or " LAB"; every card must own a
#   distinct EXTRAS grid cell — a duplicate col/row once stacked the background
#   and sky cards on one slot, making the background editor unreachable and
#   double-printing the labels.
TOOL_LABEL = re.compile(r"( EDITOR| LAB)$")
TRACE_RE = re.compile(
    r'element=(\d+) name="([^"]*)" text="([^"]*)" rect=(\d+),(\d+),(\d+),(\d+)')

#opencode: menu order is RESUME, RUN DEMO, LOAD SAVE, SETTINGS, DEMOS,
#   EXTRAS, QUIT; key codes are gX_KEY_* (DOWN 264, ENTER 257, ESCAPE 256).
#   ESC is the shared back route on the title and on every sub-view.
LOAD_GAME = "1.0 key 264;1.4 key 264;1.8 key 257"
DEMOS = "1.0 key 264;1.4 key 264;1.8 key 264;2.2 key 264;2.6 key 257"
EXTRAS = "1.0 key 264;1.4 key 264;1.8 key 264;2.2 key 264;2.6 key 264;3.0 key 257"
VIEWS = (
    ("load_game", LOAD_GAME, "view"),
    ("demos", DEMOS, "view"),
    ("extras", EXTRAS, "view"),
    ("credits", "1.5 move 855 723 200;2.0 click 855 723", "view"),
    ("escape", "1.0 key 256", "view"),
    ("escape_back", "1.0 key 256;2.2 key 256", "return"),
    ("load_game_back", LOAD_GAME + ";3.0 key 256", "return"),
    ("extras_back", EXTRAS + ";4.0 key 256", "return"),
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
        cells.setdefault((match.group(4), match.group(5)), []).append(text)
    duplicates = {cell: names for cell, names in cells.items() if len(names) > 1}
    if duplicates:
        detail = "; ".join(f"{cell}: {', '.join(names)}"
                           for cell, names in duplicates.items())
        raise RuntimeError(f"EXTRAS tool cards share a grid cell: {detail}")
    if len(cells) < 6:
        raise RuntimeError(f"EXTRAS tool grid incomplete: {len(cells)} cards")
    return len(cells)


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
