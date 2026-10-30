#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: static font/fontSize inventory for GUI templates and UI-producing scenes.
#   Reports project font files, explicit references and per-screen fontSize
#   uses; no engine launch, display or asset changes.
"""Inventory project fonts and font sizes used by gX's front-end sources.

Scope matches color_inventory.py: GUI templates, UI-producing scenes and the
teletext renderer; test files are skipped. Font references are explicit
GX_PATH("font", ...) / gX_Font_Load(...) / *.ttf literals; an unqualified
gX_Font_GetDefault() is reported as "<default>". fontSize uses are assignment
expressions (including declarations); runtime-computed sizes stay symbolic.

Output is JSON (--json [PATH]) and a short text summary.
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FONT_DIR = ROOT / "resources" / "font"
STYLE_DIR = ROOT / "resources" / "style"
SOURCE_DIRS = (ROOT / "src" / "gui" / "templates", ROOT / "src" / "scene")
EXTRA_SOURCES = (ROOT / "src" / "renderer" / "teletext.c",
                 ROOT / "src" / "gui" / "gui.c")
DEFAULT_JSON = "/tmp/opencode/frontend_fonts.json"
FONT_SUFFIXES = {".ttf", ".otf", ".fnt"}

GX_PATH_RE = re.compile(
    r'GX_PATH\(\s*"font"\s*,\s*"([^"]+)"\s*\)')
LOAD_RE = re.compile(
    r'gX_Font_Load(?:Ex|SDF)?\s*\(\s*"([^"]+)"')
FONT_LITERAL_RE = re.compile(
    r'"([^"]+\.(?:ttf|otf|fnt))"', re.IGNORECASE)
DEFAULT_RE = re.compile(r"gX_Font_GetDefault\s*\(")
FONT_SIZE_RE = re.compile(r"\bfontSize\s*=\s*([^;\n]+)")


def mask_comments(text):
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
                output.extend((char, text[index + 1]))
                index += 2
            else:
                output.append(char)
                if char == '"':
                    string = False
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
        else:
            output.append(char)
            if char == '"':
                string = True
            index += 1
    return "".join(output)


def source_files():
    files = []
    for base in SOURCE_DIRS:
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in {".c", ".cpp"}:
                continue
            if path.stem == "tests" or path.stem.endswith("_tests"):
                continue
            files.append(path)
    for path in EXTRA_SOURCES:
        if path.is_file():
            files.append(path)
    return files


def normalize_font(value):
    value = value.replace("\\", "/")
    if value.startswith("resources/") or Path(value).is_absolute():
        return value
    if (ROOT / value).is_file():
        return value
    if (FONT_DIR / value).is_file():
        return f"resources/font/{value}"
    return value


def font_references_for_file(path):
    path = Path(path)
    if path.suffix.lower() not in {".c", ".cpp"}:
        return []
    text = mask_comments(path.read_text(encoding="utf-8", errors="ignore"))
    references = set()
    for match in GX_PATH_RE.finditer(text):
        references.add(normalize_font(match.group(1)))
    for match in LOAD_RE.finditer(text):
        references.add(normalize_font(match.group(1)))
    for match in FONT_LITERAL_RE.finditer(text):
        references.add(normalize_font(match.group(1)))
    if DEFAULT_RE.search(text):
        references.add("<default>")
    return sorted(references, key=lambda item: (item != "<default>", item))


def flatten_theme(value, prefix=()):
    if isinstance(value, dict):
        for key, child in value.items():
            yield from flatten_theme(child, prefix + (str(key),))
    elif isinstance(value, (int, float)) and not isinstance(value, bool):
        yield ".".join(prefix), value


def theme_font_sizes():
    sizes = {}
    for path in sorted(STYLE_DIR.glob("theme*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        for key, value in flatten_theme(data):
            leaf = key.rsplit(".", 1)[-1]
            if leaf in {"fontSize", "titleSize", "bodySize", "smallSize",
                        "creditsSize"}:
                sizes.setdefault(path.name, {})[key] = value
    return sizes


def collect_inventory():
    named_fonts = {}
    for path in sorted(FONT_DIR.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in FONT_SUFFIXES:
            continue
        data = path.read_bytes()
        named_fonts[str(path.relative_to(ROOT))] = {
            "name": path.name,
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "referenced_by": [],
        }

    screens = {}
    font_size_count = 0
    for path in source_files():
        relative = str(path.relative_to(ROOT))
        screen = path.stem
        text = mask_comments(path.read_text(encoding="utf-8", errors="ignore"))
        entry = screens.setdefault(screen, {
            "files": [], "fonts": set(), "fontSizes": []})
        entry["files"].append(relative)
        for reference in font_references_for_file(path):
            entry["fonts"].add(reference)
            if reference in named_fonts:
                named_fonts[reference]["referenced_by"].append(screen)
        for match in FONT_SIZE_RE.finditer(text):
            value = match.group(1).strip()
            entry["fontSizes"].append({
                "line": text.count("\n", 0, match.start()) + 1,
                "value": value[:120],
            })
            font_size_count += 1

    screen_rows = {}
    for screen, entry in sorted(screens.items()):
        if not entry["fonts"] and not entry["fontSizes"]:
            continue
        screen_rows[screen] = {
            "files": sorted(entry["files"]),
            "fonts": sorted(entry["fonts"], key=lambda item: (item != "<default>", item)),
            "fontSizes": entry["fontSizes"],
        }
    for font in named_fonts.values():
        font["referenced_by"] = sorted(set(font["referenced_by"]))
    unreferenced = sorted(path for path, font in named_fonts.items()
                          if not font["referenced_by"])
    referenced = len(named_fonts) - len(unreferenced)
    return {
        "assumptions": [
            "Scope: GUI templates + UI-producing scenes + teletext; test files skipped.",
            "Font references are explicit GX_PATH/gX_Font_Load/*.ttf uses; <default> is raylib's built-in font.",
            "fontSize uses are assignment expressions; computed values stay symbolic.",
            "Project fonts are files under resources/font; theme size tokens are listed separately.",
        ],
        "summary": {
            "project_fonts": len(named_fonts),
            "referenced_fonts": referenced,
            "unreferenced_fonts": len(unreferenced),
            "screens": len(screen_rows),
            "font_size_uses": font_size_count,
        },
        "fonts": named_fonts,
        "unreferenced": unreferenced,
        "theme_sizes": theme_font_sizes(),
        "screens": screen_rows,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", nargs="?", const=DEFAULT_JSON,
                        help=f"write JSON (default: {DEFAULT_JSON})")
    args = parser.parse_args()
    inventory = collect_inventory()
    summary = inventory["summary"]
    print(f"FONT_INVENTORY: {summary['project_fonts']} project fonts, "
          f"{summary['referenced_fonts']} referenced, "
          f"{summary['unreferenced_fonts']} unreferenced; "
          f"{summary['screens']} screens, "
          f"{summary['font_size_uses']} fontSize uses")
    if args.json:
        path = Path(args.json)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(inventory, indent=2, sort_keys=True),
                        encoding="utf-8")
        print(f"  json={path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
