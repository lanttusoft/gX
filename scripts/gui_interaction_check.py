#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: GUI interaction gate — drives the real canvas GUI through
#   GX_AI_INPUT and asserts the GX_GUI_TRACE readout (clicks, title views,
#   modal/dialog scope, canvas rebuilds). Every row exercises a user-facing
#   path; pixel-only checks are never the sole assertion. GPU display
#   required (DISPLAY, default :1) and separate from headless `make check`.
#
# Usage:
#   DISPLAY=:1 python3 scripts/gui_interaction_check.py
#   DISPLAY=:1 python3 scripts/gui_interaction_check.py --only title-quit-cancel

import argparse
import re
import sys

import audit_common as audit


TIMEOUT = 120
FRAME_DT = 1.0 / 60.0
TITLE_SCENE_CANVAS = 1  # maindata.masterIndex for the title scene in traces
MENU_CANVAS = 5         # escape-menu canvas index
DIALOG_CANVAS = 8       # modal dialog canvas index

ELEMENT_RE = re.compile(
    r'GUITRACE: element canvas=(\d+) element=(-?\d+) name="([^"]*)" '
    r'text="([^"]*)" rect=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+),(-?[\d.]+) '
    r'visible=(\d+) disabled=(\d+) clickable=(\d+) focusable=(\d+)')
BUILD_RE = re.compile(
    r'GUITRACE: canvas-build index=(\d+) template=(\d+) name="([^"]*)" '
    r'elements=(\d+)')
EVENT_RE = re.compile(
    r'GUITRACE: ([\w-]+) canvas=(-?\d+) element=(-?\d+) detail="([^"]*)"')


def trace_env(overrides):
    environment = {
        "GX_SKIP_TESTS": "1",
        "GX_FIXED_DT": "0.0166666667",
        "GX_GUI_TRACE": "1",
    }
    environment.update({key: str(value) for key, value in overrides.items()})
    return audit.scene_env(environment, clean=True)


def trace_run(scene, overrides, frames):
    environment = trace_env(overrides)
    result = audit.run_scene(scene, environment, width=None, height=None,
                             fps=None, frames=frames, timeout=TIMEOUT,
                             check=False)
    return result.returncode, audit.strip_ansi(result.stdout or "")


def parse_elements(log):
    elements = []
    for match in ELEMENT_RE.finditer(log):
        elements.append({
            "canvas": int(match.group(1)),
            "element": int(match.group(2)),
            "name": match.group(3),
            "text": match.group(4),
            "rect": tuple(float(match.group(i)) for i in range(5, 9)),
            "visible": int(match.group(9)),
            "disabled": int(match.group(10)),
            "clickable": int(match.group(11)),
            "focusable": int(match.group(12)),
        })
    return elements


def parse_events(log):
    return [{
        "type": match.group(1),
        "canvas": int(match.group(2)),
        "element": int(match.group(3)),
        "detail": match.group(4),
    } for match in EVENT_RE.finditer(log)]


def parse_builds(log):
    return [{
        "index": int(match.group(1)),
        "template": int(match.group(2)),
        "name": match.group(3),
        "elements": int(match.group(4)),
    } for match in BUILD_RE.finditer(log)]


def find_element(elements, text, canvas=None, clickable=True):
    wanted = text.strip().lower()
    for element in elements:
        if canvas is not None and element["canvas"] != canvas:
            continue
        if clickable and not element["clickable"]:
            continue
        if not element["visible"] or element["disabled"]:
            continue
        if element["text"].strip().lower() == wanted:
            return element
    for element in elements:
        if canvas is not None and element["canvas"] != canvas:
            continue
        if clickable and not element["clickable"]:
            continue
        if not element["visible"] or element["disabled"]:
            continue
        if wanted in element["text"].strip().lower():
            return element
    return None


def find_prefix(elements, prefix, canvas=None):
    for element in elements:
        if canvas is not None and element["canvas"] != canvas:
            continue
        if not element["clickable"] or not element["visible"] or element["disabled"]:
            continue
        if element["text"].strip().startswith(prefix):
            return element
    return None


def center(element):
    x, y, width, height = element["rect"]
    return int(round(x + width * 0.5)), int(round(y + height * 0.5))


def click_actions(*actions):
    parts = []
    for when, element in actions:
        x, y = center(element)
        parts.append(f"{when:.2f} move {x} {y} 200")
        parts.append(f"{when + 0.30:.2f} click {x} {y}")
    return ";".join(parts)


def frames_for(last_click):
    return int((last_click + 1.1) / FRAME_DT)


def matches(event, spec):
    for key, expected in spec.items():
        if callable(expected):
            if not expected(event[key]):
                return False
        elif event[key] != expected:
            return False
    return True


def ordered(events, specs):
    index = 0
    for event in events:
        if index < len(specs) and matches(event, specs[index]):
            index += 1
    return index == len(specs)


class Rows:
    def __init__(self, only):
        self.only = only
        self.results = []
        self._menu = None
        self._views = {}

    def run(self, name, function):
        if self.only and name not in self.only:
            return
        try:
            ok, note = function(self)
        except (OSError, RuntimeError, ValueError) as error:
            ok, note = False, str(error)
        self.results.append((name, ok, note))
        print(f"{name:<22} {'PASS' if ok else 'FAIL'}  {note}", flush=True)

    def menu(self, scene="--title"):
        if self._menu is None:
            code, log = trace_run(scene, {}, 90)
            if code != 0:
                raise RuntimeError(f"{scene} discovery exited {code}")
            self._menu = parse_elements(log)
        return self._menu

    def view(self, view):
        if view not in self._views:
            code, log = trace_run("--title", {"GX_GUI_TITLE_VIEW": view}, 60)
            if code != 0:
                raise RuntimeError(f"title view {view} discovery exited {code}")
            self._views[view] = parse_elements(log)
        return self._views[view]


def row_title_saves_back(rows):
    load = find_element(rows.menu(), "LOAD SAVE", canvas=0)
    back = find_element(rows.view(1), "BACK", canvas=0)
    if not load or not back:
        return False, "LOAD SAVE/BACK target missing"
    script = click_actions((1.0, load), (2.2, back))
    code, log = trace_run("--title", {"GX_AI_INPUT": script}, frames_for(2.5))
    events = parse_events(log)
    ok = code == 0 and ordered(events, [
        {"type": "click", "detail": "LOAD SAVE"},
        {"type": "title-view", "canvas": 1},
        {"type": "click", "detail": "BACK"},
        {"type": "title-view", "canvas": 0},
    ])
    return ok, "LOAD SAVE opens the saves view, BACK returns to the menu"


def row_title_settings_back(rows):
    settings = find_element(rows.menu(), "SETTINGS", canvas=0)
    back = find_element(rows.view(4), "BACK", canvas=0)
    if not settings or not back:
        return False, "SETTINGS/BACK target missing"
    script = click_actions((1.0, settings), (2.2, back))
    code, log = trace_run("--title", {"GX_AI_INPUT": script}, frames_for(2.5))
    events = parse_events(log)
    ok = code == 0 and ordered(events, [
        {"type": "click", "detail": "SETTINGS"},
        {"type": "title-view", "canvas": 4},
        {"type": "click", "detail": "BACK"},
        {"type": "title-view", "canvas": 0},
    ])
    return ok, "SETTINGS opens the settings view, BACK returns to the menu"


#ai: Demos routes are user-clickable scene switches, not title mockups.
def row_title_demos_back(rows):
    demos = find_element(rows.menu(), "DEMOS", canvas=0)
    back = find_element(rows.view("5"), "BACK", canvas=0)
    if not demos or not back:
        return False, "DEMOS/BACK target missing"
    script = click_actions((1.0, demos), (2.0, back))
    code, log = trace_run("--title", {"GX_AI_INPUT": script}, frames_for(2.4))
    events = parse_events(log)
    ok = code == 0 and ordered(events, [
        {"type": "click", "detail": "DEMOS"},
        {"type": "title-view", "canvas": 5},
        {"type": "click", "detail": "BACK"},
        {"type": "title-view", "canvas": 0},
    ])
    return ok, "DEMOS opens its hub and BACK returns to the root menu"


def row_title_battle_launch(rows):
    demos = find_element(rows.menu(), "DEMOS", canvas=0)
    battle = find_element(rows.view("5"), "BATTLE ARENA", canvas=0)
    if not demos or not battle:
        return False, "DEMOS/BATTLE ARENA target missing"
    script = click_actions((1.0, demos), (2.0, battle))
    code, log = trace_run("--title", {"GX_AI_INPUT": script}, frames_for(2.4))
    events = parse_events(log)
    ok = code == 0 and ordered(events, [
        {"type": "click", "detail": "DEMOS"},
        {"type": "title-view", "canvas": 5},
        {"type": "click", "detail": "BATTLE ARENA"},
        {"type": "respond", "canvas": lambda value: value != TITLE_SCENE_CANVAS},
    ])
    return ok, "title Demos launches the Battle scene"


def row_title_gfx_launch(rows):
    demos = find_element(rows.menu(), "DEMOS", canvas=0)
    gfx = find_element(rows.view("5"), "GFX SHOWCASE", canvas=0)
    if not demos or not gfx:
        return False, "DEMOS/GFX SHOWCASE target missing"
    script = click_actions((1.0, demos), (2.0, gfx))
    code, log = trace_run("--title", {"GX_AI_INPUT": script}, frames_for(2.4))
    events = parse_events(log)
    ok = code == 0 and ordered(events, [
        {"type": "click", "detail": "DEMOS"},
        {"type": "title-view", "canvas": 5},
        {"type": "click", "detail": "GFX SHOWCASE"},
        {"type": "respond", "canvas": lambda value: value != TITLE_SCENE_CANVAS},
    ])
    return ok, "title Demos keeps the GFX showcase route available"


#ai: assert the Background entry reaches its configured renderer scene.
def row_title_background_launch(rows):
    demos = find_element(rows.menu(), "DEMOS", canvas=0)
    background = find_element(rows.view("5"), "BACKGROUND", canvas=0)
    if not demos or not background:
        return False, "DEMOS/BACKGROUND target missing"
    script = click_actions((1.0, demos), (2.0, background))
    code, log = trace_run("--title", {"GX_AI_INPUT": script}, frames_for(2.4))
    events = parse_events(log)
    ok = code == 0 and ordered(events, [
        {"type": "click", "detail": "DEMOS"},
        {"type": "title-view", "canvas": 5},
        {"type": "click", "detail": "BACKGROUND"},
        {"type": "respond", "canvas": lambda value: value != TITLE_SCENE_CANVAS},
    ])
    return ok, "title Demos launches the Background scene"


#ai: assert the Raymarch entry reaches its configured renderer scene.
def row_title_raymarch_launch(rows):
    demos = find_element(rows.menu(), "DEMOS", canvas=0)
    raymarch = find_element(rows.view("5"), "RAYMARCH", canvas=0)
    if not demos or not raymarch:
        return False, "DEMOS/RAYMARCH target missing"
    script = click_actions((1.0, demos), (2.0, raymarch))
    code, log = trace_run("--title", {"GX_AI_INPUT": script}, frames_for(2.4))
    events = parse_events(log)
    ok = code == 0 and ordered(events, [
        {"type": "click", "detail": "DEMOS"},
        {"type": "title-view", "canvas": 5},
        {"type": "click", "detail": "RAYMARCH"},
        {"type": "respond", "canvas": lambda value: value != TITLE_SCENE_CANVAS},
    ])
    return ok, "title Demos launches the Raymarch scene"


def row_title_quit_cancel(rows):
    quit_element = find_element(rows.menu(), "QUIT", canvas=0)
    new_game = find_element(rows.menu(), "RUN DEMO", canvas=0)
    if not quit_element or not new_game:
        return False, "QUIT/RUN DEMO target missing"
    #opencode: pass A opens the dialog so its buttons come from the real build
    #   (the layout stays template-owned instead of hardcoded).
    script = click_actions((1.0, quit_element))
    code, log = trace_run("--title", {"GX_AI_INPUT": script}, frames_for(1.3))
    if code != 0:
        return False, f"dialog open pass exited {code}"
    cancel = find_element(parse_elements(log), "Cancel", canvas=DIALOG_CANVAS)
    if not cancel:
        return False, "dialog Cancel target missing"
    script = click_actions((1.0, quit_element), (2.0, new_game), (3.2, cancel))
    code, log = trace_run("--title", {"GX_AI_INPUT": script}, frames_for(3.6))
    events = parse_events(log)
    background_clicks = [event for event in events
                         if event["type"] == "click" and event["detail"] == "RUN DEMO"]
    ok = code == 0 and not background_clicks and ordered(events, [
        {"type": "click", "detail": "QUIT"},
        {"type": "dialog-open"},
        {"type": "click", "detail": "Cancel"},
        {"type": "dialog-cancel"},
        {"type": "modal-close"},
    ])
    return ok, "QUIT opens the modal; background clicks stay blocked; Cancel closes"


def row_menu_continue(rows):
    continue_element = find_element(rows.menu(), "RESUME", canvas=0)
    if not continue_element:
        return False, "RESUME target missing"
    script = click_actions((1.0, continue_element))
    code, log = trace_run("--title", {"GX_AI_INPUT": script}, frames_for(1.4))
    events = parse_events(log)
    builds = parse_builds(log)
    ok = code == 0 and ordered(events, [
        {"type": "click", "detail": "RESUME"},
        {"type": "respond", "canvas": lambda value: value != TITLE_SCENE_CANVAS},
    ]) and any(build["index"] == 0 for build in builds)
    return ok, "RESUME switches the scene and rebuilds the GUI canvas"


def row_escape_resume(rows):
    code, log = trace_run("--gfx", {"GX_GUI_SHOW_MENU": "1"}, 60)
    if code != 0:
        return False, f"escape menu discovery exited {code}"
    resume = find_element(parse_elements(log), "Resume", canvas=MENU_CANVAS)
    if not resume:
        return False, "escape-menu Resume target missing"
    script = click_actions((1.0, resume))
    code, log = trace_run("--gfx", {"GX_GUI_SHOW_MENU": "1", "GX_AI_INPUT": script},
                          frames_for(1.4))
    events = parse_events(log)
    ok = code == 0 and ordered(events, [
        {"type": "click", "detail": "Resume"},
    ])
    return ok, "escape-menu Resume click dispatches through the canvas"


def row_guitester_checkbox(rows):
    code, log = trace_run("--title", {"GX_GUI_TEMPLATE_FORCE": "GUITESTER"}, 90)
    if code != 0:
        return False, f"guitester discovery exited {code}"
    checkbox = find_prefix(parse_elements(log), "[", canvas=2)
    if not checkbox:
        return False, "guitester checkbox target missing"
    before = checkbox["text"].strip()
    script = click_actions((1.0, checkbox))
    code, log = trace_run("--title",
                          {"GX_GUI_TEMPLATE_FORCE": "GUITESTER", "GX_AI_INPUT": script},
                          frames_for(1.4))
    events = parse_events(log)
    elements = parse_elements(log)
    toggled = any(element["canvas"] == 2 and element["text"].strip().startswith("[X]")
                  for element in elements)
    rebuilt = any(build["index"] == 2 for build in parse_builds(log))
    ok = code == 0 and ordered(events, [
        {"type": "click", "detail": before},
    ]) and rebuilt and toggled
    return ok, f"checkbox '{before}' click rebuilds as checked"


def row_settings_toggle(rows):
    code, log = trace_run("--title", {"GX_GUI_TEMPLATE_FORCE": "SETTINGS"}, 90)
    if code != 0:
        return False, f"settings discovery exited {code}"
    toggle = find_element(parse_elements(log), "Invert Y", canvas=2)
    if not toggle:
        return False, "settings toggle target missing"
    before = toggle["text"].strip()
    script = click_actions((1.0, toggle))
    code, log = trace_run("--title",
                          {"GX_GUI_TEMPLATE_FORCE": "SETTINGS", "GX_AI_INPUT": script},
                          frames_for(1.4))
    events = parse_events(log)
    elements = parse_elements(log)
    rebuilt = any(build["index"] == 2 for build in parse_builds(log))
    toggled = any(
        element["canvas"] == 2 and element["visible"] and not element["disabled"]
        and element["text"].strip() != before
        and element["text"].strip().lower().startswith("invert y")
        for element in elements)
    ok = code == 0 and ordered(events, [
        {"type": "click", "detail": before},
    ]) and rebuilt and toggled
    return ok, f"settings toggle '{before}' click rebuilds its state"


ROWS = [
    ("title-saves-back", row_title_saves_back),
    ("title-settings-back", row_title_settings_back),
    ("title-demos-back", row_title_demos_back),
    ("title-battle-launch", row_title_battle_launch),
    ("title-gfx-launch", row_title_gfx_launch),
    ("title-background-launch", row_title_background_launch),
    ("title-raymarch-launch", row_title_raymarch_launch),
    ("title-quit-cancel", row_title_quit_cancel),
    ("menu-continue", row_menu_continue),
    ("escape-resume", row_escape_resume),
    ("guitester-checkbox", row_guitester_checkbox),
    ("settings-toggle", row_settings_toggle),
]


def main():
    parser = argparse.ArgumentParser(
        description="Drive the canvas GUI through virtual input and assert the "
                    "GUITRACE interaction readout.")
    parser.add_argument("--only", metavar="NAMES",
                        help="comma-separated row names")
    args = parser.parse_args()
    if not audit.BIN.is_file():
        print(f"GUI_INTERACTION_CHECK: FAIL — missing binary: {audit.BIN}",
              file=sys.stderr)
        return 1
    names = {item.strip() for item in args.only.split(",") if item.strip()} \
        if args.only else set()
    known = {name for name, _ in ROWS}
    unknown = sorted(names - known)
    if unknown:
        parser.error("unknown row(s): " + ", ".join(unknown))
    rows = Rows(names)
    try:
        for name, function in ROWS:
            rows.run(name, function)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GUI_INTERACTION_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    failures = [name for name, ok, _ in rows.results if not ok]
    print(f"\nGUI_INTERACTION_CHECK: {'PASS' if not failures else 'FAIL'} "
          f"({len(rows.results) - len(failures)}/{len(rows.results)} rows)")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"GUI_INTERACTION_CHECK: FAIL — {exc}", file=sys.stderr)
        sys.exit(1)
