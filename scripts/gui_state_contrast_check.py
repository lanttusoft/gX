#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: 2026-10-22 state/surface contrast matrix for shipped GUI themes.
"""WCAG contrast matrix for gX themes.

Checks text pairs at 4.5:1 and non-text/focus/status pairs at 3:1. Each theme
is merged over the default theme, then alpha colors are composited before
relative-luminance comparison.
"""

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
THEMES = [
    ("default", ROOT / "resources/style/theme.json"),
    ("high-contrast", ROOT / "resources/style/theme_high_contrast.json"),
    ("color-vision", ROOT / "resources/style/theme_color_vision.json"),
    ("aero", ROOT / "resources/style/theme_aero.json"),
]
TEXT_MIN = 4.5
NON_TEXT_MIN = 3.0

# (name, foreground path, background path, minimum)
PAIRS = [
    ("a11y.canvas", ("a11y", "textOnCanvas"), ("a11y", "surfaceCanvas"), TEXT_MIN),
    ("a11y.raised", ("a11y", "textOnRaised"), ("a11y", "surfaceRaised"), TEXT_MIN),
    ("a11y.overlay", ("a11y", "textOnOverlay"), ("a11y", "surfaceOverlay"), TEXT_MIN),
    ("a11y.sunken", ("a11y", "textOnSunken"), ("a11y", "surfaceSunken"), TEXT_MIN),
    ("a11y.inverse", ("a11y", "textOnInverse"), ("a11y", "surfaceInverse"), TEXT_MIN),
    ("text.panel", ("core", "textColor"), ("core", "panelBg"), TEXT_MIN),
    ("text.section", ("core", "sectionHeader"), ("core", "panelBg"), TEXT_MIN),
    ("button.base", ("core", "buttonText"), ("core", "buttonBase"), TEXT_MIN),
    ("button.hover", ("core", "buttonText"), ("core", "buttonHover"), TEXT_MIN),
    ("back.base", ("core", "backText"), ("core", "backBase"), TEXT_MIN),
    ("back.hover", ("core", "backText"), ("core", "backHover"), TEXT_MIN),
    ("selected", ("core", "selectedText"), ("core", "selectedBg"), TEXT_MIN),
    ("unselected", ("core", "unselectedText"), ("core", "unselectedBg"), TEXT_MIN),
    ("escape.title", ("escape", "titleText"), ("escape", "titleBarBg"), TEXT_MIN),
    ("escape.hint", ("escape", "hintText"), ("escape", "panelBg"), TEXT_MIN),
    ("escape.saved", ("escape", "savedText"), ("escape", "panelBg"), TEXT_MIN),
    ("title.wordmark", ("title2", "wordmarkText"), ("title2", "bgGradientTop"), TEXT_MIN),
    ("title.subtitle", ("title2", "subtitleText"), ("title2", "bgGradientBottom"), TEXT_MIN),
    ("title.menu1", ("title2", "menuText1"), ("title2", "menuBg1"), TEXT_MIN),
    ("title.menu2", ("title2", "menuText2"), ("title2", "menuBg2"), TEXT_MIN),
    ("title.menu3", ("title2", "menuText3"), ("title2", "menuBg3"), TEXT_MIN),
    ("title.menu4", ("title2", "menuText4"), ("title2", "menuBg4"), TEXT_MIN),
    ("title.menu5", ("title2", "menuText5"), ("title2", "menuBg5"), TEXT_MIN),
    ("title.menu6", ("title2", "menuText6"), ("title2", "menuBg6"), TEXT_MIN),
    ("title.footer", ("title2", "footerText"), ("title2", "bgGradientBottom"), TEXT_MIN),
    ("title.version", ("title2", "versionText"), ("title2", "bgGradientBottom"), TEXT_MIN),
    ("title.save-header", ("title2", "saveHeaderText"), ("title2", "savePanelBg"), TEXT_MIN),
    ("title.save-slot", ("title2", "saveSlotText"), ("title2", "saveSlotBg"), TEXT_MIN),
    ("title.quit", ("title2", "quitText"), ("title2", "quitBg"), TEXT_MIN),
    ("battle.player", ("core", "textColor"), ("battle", "panelBg"), TEXT_MIN),
    ("battle.name", ("core", "textColor"), ("battle", "nameBg"), TEXT_MIN),
    ("battle.menu", ("core", "textColor"), ("battle", "menuBg"), TEXT_MIN),
    ("battle.magic", ("core", "textColor"), ("battle", "magicBg"), TEXT_MIN),
    ("battle.skills", ("core", "textColor"), ("battle", "skillsBg"), TEXT_MIN),
    ("battle.items", ("core", "textColor"), ("battle", "itemsBg"), TEXT_MIN),
    ("battle.target", ("core", "textColor"), ("battle", "targetBg"), TEXT_MIN),
    ("shared.dim", ("shared", "textDim"), ("shared", "panelDark"), TEXT_MIN),
    ("shared.blue", ("shared", "textLightBlue"), ("shared", "panelDark"), TEXT_MIN),
    ("focus.ring", ("core", "focusRing"), ("core", "panelBg"), NON_TEXT_MIN),
    ("focus.ring-canvas", ("core", "focusRing"), ("a11y", "surfaceCanvas"), NON_TEXT_MIN),
    ("focus.ring-raised", ("core", "focusRing"), ("a11y", "surfaceRaised"), NON_TEXT_MIN),
    ("focus.ring-overlay", ("core", "focusRing"), ("a11y", "surfaceOverlay"), NON_TEXT_MIN),
    ("focus.ring-sunken", ("core", "focusRing"), ("a11y", "surfaceSunken"), NON_TEXT_MIN),
    ("focus.ring-button", ("core", "focusRing"), ("core", "buttonBase"), NON_TEXT_MIN),
    ("focus.ring-selected", ("core", "focusRing"), ("core", "selectedBg"), NON_TEXT_MIN),
    ("focus.ring-title-menu1", ("core", "focusRing"), ("title2", "menuBg1"), NON_TEXT_MIN),
    ("focus.ring-title-menu2", ("core", "focusRing"), ("title2", "menuBg2"), NON_TEXT_MIN),
    ("focus.ring-title-menu3", ("core", "focusRing"), ("title2", "menuBg3"), NON_TEXT_MIN),
    ("focus.ring-title-menu4", ("core", "focusRing"), ("title2", "menuBg4"), NON_TEXT_MIN),
    ("focus.ring-title-menu5", ("core", "focusRing"), ("title2", "menuBg5"), NON_TEXT_MIN),
    ("focus.ring-title-menu6", ("core", "focusRing"), ("title2", "menuBg6"), NON_TEXT_MIN),
    ("focus.ring-title-save-panel", ("core", "focusRing"), ("title2", "savePanelBg"), NON_TEXT_MIN),
    ("focus.ring-title-save-slot", ("core", "focusRing"), ("title2", "saveSlotBg"), NON_TEXT_MIN),
    ("focus.ring-battle-panel", ("core", "focusRing"), ("battle", "panelBg"), NON_TEXT_MIN),
    ("focus.ring-battle-menu", ("core", "focusRing"), ("battle", "menuBg"), NON_TEXT_MIN),
    ("focus.ring-battle-magic", ("core", "focusRing"), ("battle", "magicBg"), NON_TEXT_MIN),
    ("focus.ring-battle-skills", ("core", "focusRing"), ("battle", "skillsBg"), NON_TEXT_MIN),
    ("focus.ring-battle-items", ("core", "focusRing"), ("battle", "itemsBg"), NON_TEXT_MIN),
    ("focus.ring-battle-target", ("core", "focusRing"), ("battle", "targetBg"), NON_TEXT_MIN),
    ("focus.ring-escape", ("core", "focusRing"), ("escape", "panelBg"), NON_TEXT_MIN),
    ("scroll.arrow", ("core", "scrollbarArrow"), ("core", "scrollbarTrack"), NON_TEXT_MIN),
    ("scroll.thumb", ("core", "scrollbarThumb"), ("core", "scrollbarTrack"), NON_TEXT_MIN),
    ("status.hp", ("shared", "hpGreen"), ("shared", "panelDark"), NON_TEXT_MIN),
    ("status.alert", ("shared", "alertRed"), ("shared", "panelDark"), NON_TEXT_MIN),
    ("border", ("shared", "borderLight"), ("shared", "panelDark"), NON_TEXT_MIN),
    ("accent.gold", ("shared", "accentGold"), ("shared", "panelDark"), NON_TEXT_MIN),
    ("accent.cyan", ("shared", "accentCyan"), ("shared", "panelDark"), NON_TEXT_MIN),
]


def merge(base, overlay):
    result = dict(base)
    for key, value in overlay.items():
        if isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = merge(result[key], value)
        else:
            result[key] = value
    return result


def color(theme, path):
    value = theme
    for key in path:
        if not isinstance(value, dict) or key not in value:
            return None
        value = value[key]
    if not isinstance(value, list) or len(value) < 3:
        return None
    rgb = [max(0, min(255, int(round(v)))) for v in value[:3]]
    alpha = value[3] if len(value) > 3 else 255
    alpha = max(0, min(255, int(round(alpha))))
    return rgb + [alpha]


def composite(fg, bg):
    fa = fg[3] / 255.0
    ba = bg[3] / 255.0
    out_a = fa + ba * (1.0 - fa)
    if out_a <= 0.0:
        return [0, 0, 0, 0]
    out = []
    for index in range(3):
        out.append((fg[index] * fa + bg[index] * ba * (1.0 - fa)) / out_a)
    return [int(round(v)) for v in out] + [int(round(out_a * 255))]


def lum(rgb):
    def channel(value):
        value /= 255.0
        return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4
    r, g, b = (channel(v) for v in rgb[:3])
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def ratio(fg, bg):
    a = lum(fg)
    b = lum(bg)
    if a < b:
        a, b = b, a
    return (a + 0.05) / (b + 0.05)


def evaluate(theme, canvas):
    failures = []
    checked = 0
    for name, fg_path, bg_path, minimum in PAIRS:
        fg = color(theme, fg_path)
        bg = color(theme, bg_path)
        if fg is None or bg is None:
            continue
        bg_eff = composite(bg, canvas) if bg[3] < 255 else bg
        fg_eff = composite(fg, bg_eff) if fg[3] < 255 else fg
        value = ratio(fg_eff, bg_eff)
        checked += 1
        if value + 1e-6 < minimum:
            failures.append({
                "pair": name, "ratio": round(value, 3), "minimum": minimum,
                "fg": fg, "bg": bg,
            })
    return checked, failures


def main():
    parser = argparse.ArgumentParser(description="gX theme state/contrast matrix")
    parser.add_argument("--json", help="write a JSON report")
    parser.add_argument("--strict", action="store_true",
                        help="exit 1 on any below-threshold pair")
    parser.add_argument("--baseline",
                        help="JSON of known failures; fail only on new pairs")
    args = parser.parse_args()
    default_path = THEMES[0][1]
    default = json.loads(default_path.read_text())
    canvas = color(merge(default, {}), ("a11y", "surfaceCanvas")) or [0, 0, 0, 255]
    known = {}
    if args.baseline:
        known_data = json.loads(Path(args.baseline).read_text())
        known = known_data.get("known", {})
    report = {"themes": {}, "checked": 0, "failures": 0, "new_failures": 0}
    for name, path in THEMES:
        theme = merge(default, json.loads(path.read_text()))
        checked, failures = evaluate(theme, canvas)
        known_pairs = set(known.get(name, []))
        new = [failure for failure in failures if failure["pair"] not in known_pairs]
        report["themes"][name] = {
            "checked": checked, "failures": failures, "new": new,
            "known": len(failures) - len(new),
        }
        report["checked"] += checked
        report["failures"] += len(failures)
        report["new_failures"] += len(new)
        print(f"{name:<15} checked={checked:2d} failures={len(failures)} "
              f"new={len(new)}")
        for failure in failures:
            mark = "NEW " if failure in new else "known "
            print(f"  {mark}{failure['pair']:<20} {failure['ratio']:.2f}:1 "
                  f"< {failure['minimum']}:1")
    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=2) + "\n")
    if args.strict and report["failures"]:
        print(f"GUI_STATE_CONTRAST: STRICT FAIL — {report['failures']} failure(s)")
        return 1
    if report["new_failures"]:
        print(f"GUI_STATE_CONTRAST: FAIL — {report['new_failures']} new "
              f"failure(s), {report['failures']} total")
        return 1
    if report["failures"]:
        print(f"GUI_STATE_CONTRAST: PASS — {report['failures']} known baseline "
              f"failure(s) across {report['checked']} checks")
        return 0
    print(f"GUI_STATE_CONTRAST: PASS — {report['checked']} checks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
