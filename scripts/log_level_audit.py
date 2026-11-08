#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: logging-level guard — routine entry/exit traces must stay TRACE and the
#   current threshold must never be reused as a message severity.
import argparse
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
ALLOWED = {
    "src/common/log.c",
    "src/common/log.h",
    "src/main.c",
}
THRESHOLD_USE = re.compile(r"gX_LogState\.globalLevel|gX_TraceLog_logtypeLevel")
MACROS = ("gX_TRACELOG_PROLOGUE", "gX_TRACELOG_EPILOGUE")


def source_files(root):
    for pattern in ("*.c", "*.h", "*.cpp"):
        for path in sorted((Path(root) / "src").rglob(pattern)):
            if any(part in ("external", "build") for part in path.parts):
                continue
            yield path


def macro_block(text, name):
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if re.match(rf"#define\s+{name}\(", line):
            block = [line]
            for following in lines[index + 1:index + 3]:
                block.append(following)
                if not block[-2].rstrip().endswith("\\"):
                    break
            return "\n".join(block)
    return ""


def audit(root=ROOT):
    root = Path(root)
    problems = []
    for path in source_files(root):
        relative = path.relative_to(root).as_posix()
        if relative in ALLOWED:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError as error:
            problems.append({"path": relative, "line": 0,
                             "reason": f"unreadable: {error}"})
            continue
        for number, line in enumerate(text.splitlines(), 1):
            if THRESHOLD_USE.search(line):
                problems.append({
                    "path": relative,
                    "line": number,
                    "reason": "threshold used as a message severity",
                    "text": line.strip(),
                })
    header = (root / "src/common/log.h").read_text(encoding="utf-8",
                                                   errors="replace")
    for name in MACROS:
        block = macro_block(header, name)
        if not block:
            problems.append({"path": "src/common/log.h", "line": 0,
                             "reason": f"{name} definition missing"})
        elif "gX_LOG_TRACE" not in block or "globalLevel" in block:
            problems.append({
                "path": "src/common/log.h",
                "line": 0,
                "reason": f"{name} must use the fixed TRACE level",
                "text": block,
            })
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Audit routine log levels and threshold-as-severity misuse.")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        problems = audit(args.root)
    except OSError as error:
        print(f"LOG_LEVEL_AUDIT: FAIL — {error}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps({"problems": problems}, indent=2))
    if problems:
        for item in problems:
            location = f"{item['path']}:{item['line']}"
            detail = f" ({item.get('text', '')})" if item.get("text") else ""
            print(f"LOG_LEVEL_AUDIT: FAIL — {location}: {item['reason']}{detail}",
                  file=sys.stderr)
        return 1
    if not args.json:
        print("LOG_LEVEL_AUDIT: PASS — routine prologues stay TRACE and the "
              "current threshold is not reused as a severity")
    return 0


if __name__ == "__main__":
    sys.exit(main())
