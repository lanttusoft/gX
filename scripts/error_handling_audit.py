#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: error-handling audit — static scan for silent failures.
#   1. Silent stubs: a function whose whole body is unused-parameter casts and
#      a success return (the schema2 class of bug: "saved" while doing
#      nothing). Intentional no-ops must be allowlisted here with a reason.
#   2. Discarded status results (informational): calls to schema save/load
#      entry points whose return value is thrown away.

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SILENT_STUB_ALLOWLIST = {
    ("src/common/marshal/json.c", "gX_JSON_Init"):
        "nothing to initialize (self-contained JSON parser)",
    ("src/game/game.c", "gX_GameData_Unload"):
        "gX ships no game data; nothing to unload",
    ("src/rhi/rhi_buf.c", "gX_RHI_HasVAOs"):
        "capability query — the return is data (1 = VAOs available)",
    ("src/scene/title.c", "gX_SceneTitle_Update"):
        "GUI-driven title screen (documented no-op)",
    ("src/scene/title.c", "gX_SceneTitle_Draw"):
        "GUI-driven title screen (documented no-op)",
    ("src/wrappers/window.c", "gX_Monitor_GetCount"):
        "data query — the return is data (single backend monitor)",
}
FUNCTION_RE = re.compile(
    r'^[ \t]*(?:static[ \t]+)?(?:gX_[A-Za-z0-9_]+|void|int|bool)[ \t]+'
    r'(gX_[A-Za-z0-9_]+)[ \t]*\(', re.M)
STATUS_CALL_RE = re.compile(
    r'^[ \t]*(gX_[A-Za-z0-9_]*(?:_Save_New|_Load_New|_Preset_Save|_Preset_Load))'
    r'[ \t]*\(', re.M)


def function_body(text, match):
    brace = text.find('{', match.end())
    if brace < 0 or ';' in text[match.end():brace]:
        return None
    depth = 1
    index = brace + 1
    while index < len(text) and depth > 0:
        if text[index] == '{':
            depth += 1
        elif text[index] == '}':
            depth -= 1
        index += 1
    return text[brace + 1:index - 1]


def silent_stubs():
    hits = []
    for path in sorted((ROOT / "src").rglob("*.c")):
        text = path.read_text(errors="ignore")
        for match in FUNCTION_RE.finditer(text):
            body = function_body(text, match)
            if body is None:
                continue
            stripped = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
            stripped = re.sub(r'//[^\n]*', '', stripped)
            lines = [line.strip() for line in stripped.splitlines()
                     if line.strip()]
            statements = [line for line in lines
                          if not line.startswith('(void)')]
            if not statements:
                continue
            if all(re.fullmatch(r'return\s+[01];?', statement)
                   for statement in statements):
                relative = str(path.relative_to(ROOT))
                hits.append((relative, match.group(1)))
    return hits


def discarded_statuses():
    hits = []
    for path in sorted((ROOT / "src").rglob("*.c")):
        text = path.read_text(errors="ignore")
        for match in STATUS_CALL_RE.finditer(text):
            line = text[match.start():text.find('\n', match.start())]
            if '=' in line or 'if' in line or 'return' in line:
                continue
            hits.append((str(path.relative_to(ROOT)), match.group(1),
                         line.strip()))
    return hits


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--strict", action="store_true",
                        help="fail on unallowlisted silent stubs")
    arguments = parser.parse_args()

    stubs = silent_stubs()
    unknown = [hit for hit in stubs if hit not in SILENT_STUB_ALLOWLIST]
    print(f"error-handling audit: {len(stubs)} no-op success function(s), "
          f"{len(unknown)} unallowlisted")
    for path, name in stubs:
        reason = SILENT_STUB_ALLOWLIST.get((path, name))
        marker = "allow" if reason else "FLAG"
        print(f"  [{marker}] {path}: {name}"
              + (f" — {reason}" if reason else ""))
    statuses = discarded_statuses()
    print(f"discarded status results (informational): {len(statuses)}")
    for path, name, line in statuses:
        print(f"  {path}: {name} — {line}")
    if unknown and arguments.strict:
        print("ERROR_HANDLING_AUDIT: FAIL — unallowlisted silent stubs")
        return 1
    print("ERROR_HANDLING_AUDIT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
