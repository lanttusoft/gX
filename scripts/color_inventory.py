#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: static colour census for GUI templates and UI-producing scenes. Reports
#   distinct colours, named/theme-token overlap, orphans and font references;
#   no engine launch, display or asset changes.
"""Inventory static colours used by gX's front-end sources.

Scope: src/gui/templates/*.c|cpp, src/scene/*.c|cpp, src/renderer/teletext.c
(UI-producing), plus resources/style/*.css. Test files are skipped. Theme
tokens come from every resources/style/theme*.json; named colours come from
src/common/macros.h. Matching is exact RGBA (CSS colors default to alpha 255);
runtime colour math is not evaluated.

Output is JSON (--json [PATH]) and a short text summary.
"""

import argparse
import json
import re
import sys
from pathlib import Path

import font_inventory


ROOT = Path(__file__).resolve().parents[1]
STYLE_DIR = ROOT / "resources" / "style"
MACROS = ROOT / "src" / "common" / "macros.h"
SOURCE_DIRS = (ROOT / "src" / "gui" / "templates", ROOT / "src" / "scene")
EXTRA_SOURCES = (ROOT / "src" / "renderer" / "teletext.c",)
DEFAULT_JSON = "/tmp/opencode/frontend_colors.json"

LITERAL_RE = re.compile(
    r"\(gX_Color\)\s*\{\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})"
    r"\s*(?:,\s*(\d{1,3})\s*)?\}", re.DOTALL)
HEX_RE = re.compile(r"#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?\b")
RGB_RE = re.compile(
    r"\brgba?\(\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})"
    r"(?:\s*,\s*([0-9.]+)\s*)?\)")


def mask_comments(text, mask_strings=False):
    """Replace comment/string contents with spaces while preserving newlines."""
    output = []
    index = 0
    block = False
    string = False
    while index < len(text):
        char = text[index]
        if block:
            if text.startswith("*/", index):
                output.extend((" ", " "))
                block = False
                index += 2
            else:
                output.append("\n" if char == "\n" else " ")
                index += 1
        elif string:
            if char == "\\" and index + 1 < len(text):
                output.extend((" ", " "))
                index += 2
            elif char == '"':
                output.append(char)
                string = False
                index += 1
            else:
                output.append("\n" if char == "\n" else " ")
                index += 1
        elif text.startswith("//", index):
            newline = text.find("\n", index)
            end = len(text) if newline < 0 else newline
            output.extend(" " * (end - index))
            index = end
        elif text.startswith("/*", index):
            output.extend((" ", " "))
            block = True
            index += 2
        elif mask_strings and char == '"':
            output.append(char)
            string = True
            index += 1
        else:
            output.append(char)
            index += 1
    return "".join(output)


def rgba_hex(rgba):
    return "#{:02X}{:02X}{:02X}{:02X}".format(*rgba)


def line_at(text, position):
    return text.count("\n", 0, position) + 1


def load_named_colors():
    text = MACROS.read_text(encoding="utf-8")
    direct = {}
    for match in re.finditer(
            r"#define\s+(gX_\w+)\s+\(gX_Color\)\s*\{\s*(\d{1,3})\s*,\s*"
            r"(\d{1,3})\s*,\s*(\d{1,3})\s*(?:,\s*(\d{1,3})\s*)?\}",
            text):
        values = tuple(int(value) if value is not None else 255
                       for value in match.groups()[1:])
        if all(0 <= value <= 255 for value in values):
            direct[match.group(1)] = values
    aliases = {}
    for match in re.finditer(
            r"#define\s+(gX_\w+)\s+(gX_\w+)\s*$", text, re.MULTILINE):
        aliases[match.group(1)] = match.group(2)
    names = dict(direct)
    for _ in range(4):
        changed = False
        for alias, target in aliases.items():
            if alias not in names and target in names:
                names[alias] = names[target]
                changed = True
        if not changed:
            break
    return names


def flatten_theme(value, prefix=()):
    if isinstance(value, dict):
        for key, child in value.items():
            yield from flatten_theme(child, prefix + (str(key),))
    elif isinstance(value, list) and len(value) in (3, 4):
        if all(isinstance(item, int) and 0 <= item <= 255 for item in value):
            rgba = tuple(value + ([255] if len(value) == 3 else []))
            yield ".".join(prefix), rgba


def load_theme_colors():
    colors = {}
    for path in sorted(STYLE_DIR.glob("theme*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        for token, rgba in flatten_theme(data):
            colors.setdefault(rgba_hex(rgba), set()).add(token)
    return {key: sorted(tokens) for key, tokens in colors.items()}


def iter_ui_files():
    for base in SOURCE_DIRS:
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in {".c", ".cpp"}:
                continue
            if path.stem == "tests" or path.stem.endswith("_tests"):
                continue
            yield path
    for path in EXTRA_SOURCES:
        if path.is_file():
            yield path
    for path in sorted(STYLE_DIR.glob("*.css")):
        yield path


def _occurrence(path, masked, original, start, end, rgba, kind):
    if rgba is None or not all(0 <= value <= 255 for value in rgba):
        return None
    return {
        "file": str(path.relative_to(ROOT)),
        "screen": path.stem,
        "line": line_at(masked, start),
        "hex": rgba_hex(rgba),
        "rgba": list(rgba),
        "kind": kind,
        "text": " ".join(original[start:end].split())[:80],
    }


def scan_file(path, named=None):
    named = named or {}
    path = Path(path)
    original = path.read_text(encoding="utf-8", errors="ignore")
    if path.suffix.lower() == ".css":
        masked = mask_comments(original)
        occurrences = []
        for match in HEX_RE.finditer(masked):
            rgb = tuple(int(match.group(1)[index:index + 2], 16)
                        for index in (0, 2, 4))
            alpha = int(match.group(2), 16) if match.group(2) else 255
            occurrences.append(_occurrence(
                path, masked, original, match.start(), match.end(),
                rgb + (alpha,), "css"))
        for match in RGB_RE.finditer(masked):
            alpha = 255 if match.group(4) is None else round(
                max(0.0, min(1.0, float(match.group(4)))) * 255)
            rgba = tuple(int(match.group(index)) for index in (1, 2, 3)) + (alpha,)
            occurrences.append(_occurrence(
                path, masked, original, match.start(), match.end(),
                rgba, "css"))
        return [item for item in occurrences if item]

    masked = mask_comments(original, mask_strings=True)
    occurrences = []
    for match in LITERAL_RE.finditer(masked):
        rgba = tuple(int(value) if value is not None else 255
                     for value in match.groups())
        occurrences.append(_occurrence(
            path, masked, original, match.start(), match.end(), rgba, "literal"))
    if named:
        names = sorted(named, key=len, reverse=True)
        pattern = re.compile(r"\b(" + "|".join(re.escape(name)
                                                for name in names) + r")\b")
        for match in pattern.finditer(masked):
            occurrences.append(_occurrence(
                path, masked, original, match.start(), match.end(),
                named[match.group(1)], "named"))
    return [item for item in occurrences if item]


def collect_inventory():
    named = load_named_colors()
    named_by_hex = {}
    for name, rgba in named.items():
        named_by_hex.setdefault(rgba_hex(rgba), []).append(name)
    theme_tokens = load_theme_colors()
    occurrences = []
    screens = {}
    files = list(iter_ui_files())
    for path in files:
        relative = str(path.relative_to(ROOT))
        screen = path.stem
        entry = screens.setdefault(screen, {
            "files": [], "colors": set(), "fonts": set()})
        entry["files"].append(relative)
        for reference in font_inventory.font_references_for_file(path):
            entry["fonts"].add(reference)
        for occurrence in scan_file(path, named):
            occurrence["named"] = sorted(named_by_hex.get(occurrence["hex"], []))
            occurrence["theme"] = theme_tokens.get(occurrence["hex"], [])
            occurrence["orphan"] = not occurrence["named"] and not occurrence["theme"]
            occurrences.append(occurrence)
            entry["colors"].add(occurrence["hex"])

    colors = {}
    for occurrence in occurrences:
        entry = colors.setdefault(occurrence["hex"], {
            "hex": occurrence["hex"],
            "rgba": occurrence["rgba"],
            "count": 0,
            "kinds": set(),
            "screens": set(),
            "named": occurrence["named"],
            "theme": occurrence["theme"],
            "orphan": occurrence["orphan"],
        })
        entry["count"] += 1
        entry["kinds"].add(occurrence["kind"])
        entry["screens"].add(occurrence["screen"])

    color_rows = []
    for entry in sorted(colors.values(), key=lambda item: item["hex"]):
        color_rows.append({
            "hex": entry["hex"],
            "rgba": entry["rgba"],
            "count": entry["count"],
            "kinds": sorted(entry["kinds"]),
            "screens": sorted(entry["screens"]),
            "named": entry["named"],
            "theme": entry["theme"],
            "orphan": entry["orphan"],
        })
    screen_rows = {}
    for screen, entry in sorted(screens.items()):
        if not entry["colors"] and not entry["fonts"]:
            continue
        screen_rows[screen] = {
            "files": sorted(entry["files"]),
            "colors": sorted(entry["colors"]),
            "fonts": sorted(entry["fonts"]),
        }
    orphan = [row["hex"] for row in color_rows if row["orphan"]]
    return {
        "assumptions": [
            "Scope: GUI templates + UI-producing scenes + teletext; test files skipped.",
            "Raw colours: numeric (gX_Color){r,g,b,a} literals and CSS hex/rgb(a).",
            "Named colours come from src/common/macros.h; theme tokens from resources/style/theme*.json.",
            "Matching is exact RGBA; runtime colour math and shader outputs are not evaluated.",
            "An orphan is a colour with no exact named/theme-token match, not a defect by itself.",
        ],
        "source_files": [str(path.relative_to(ROOT)) for path in files],
        "named_colors": {key: sorted(value)
                         for key, value in named_by_hex.items()},
        "theme_tokens": theme_tokens,
        "summary": {
            "source_files": len(files),
            "screens": len(screen_rows),
            "occurrences": len(occurrences),
            "distinct_colors": len(color_rows),
            "orphan_colors": len(orphan),
            "named_distinct": len(named_by_hex),
            "theme_distinct": len(theme_tokens),
        },
        "colors": color_rows,
        "orphan_colors": orphan,
        "screens": screen_rows,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", nargs="?", const=DEFAULT_JSON,
                        help=f"write JSON (default: {DEFAULT_JSON})")
    args = parser.parse_args()
    inventory = collect_inventory()
    summary = inventory["summary"]
    print(f"COLOR_INVENTORY: {summary['occurrences']} occurrences, "
          f"{summary['distinct_colors']} distinct, "
          f"{summary['orphan_colors']} orphan across "
          f"{summary['screens']} screens")
    print(f"  named colours={summary['named_distinct']} "
          f"theme tokens={summary['theme_distinct']} "
          f"source files={summary['source_files']}")
    if args.json:
        path = Path(args.json)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(inventory, indent=2, sort_keys=True),
                        encoding="utf-8")
        print(f"  json={path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
