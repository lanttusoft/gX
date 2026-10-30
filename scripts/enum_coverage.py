#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

"""
gX Enum Coverage Checker
Verifies every enum String() declared anywhere under src/ has an
implementation anywhere under src/ (the original curated four-header scope
missed dangling declarations such as gX_Camera_Modes_String, whose return
type had also drifted to void, and gX_Binds_Common_String).

Usage:
    python3 scripts/enum_coverage.py        # report
    python3 scripts/enum_coverage.py --strict  # exit 1 on any gap
"""

import os
import re
import sys

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(PROJECT, "src")

DECL_RE = re.compile(r'const\s+gX_I8\s*\*\s*(gX_[A-Za-z0-9_]+)_String\s*\(')
IMPL_RE = re.compile(r'const\s+gX_I8\s*\*\s*(gX_[A-Za-z0-9_]+)_String\s*\(')

def scan(suffixes):
    for dirpath, _dirs, files in os.walk(SRC):
        for name in sorted(files):
            if name.endswith(suffixes):
                yield os.path.join(dirpath, name)


def main():
    strict = "--strict" in sys.argv
    declared = {}
    for path in scan((".h",)):
        with open(path, errors="ignore") as f:
            for m in DECL_RE.finditer(f.read()):
                declared[m.group(1)] = os.path.relpath(path, PROJECT)

    implemented = set()
    for path in scan((".c", ".cpp")):
        with open(path, errors="ignore") as f:
            for m in IMPL_RE.finditer(f.read()):
                implemented.add(m.group(1))

    missing = [fn for fn, h in declared.items() if fn not in implemented]
    if missing:
        print(f"enum coverage: {len(missing)} dangling String() declaration(s)")
        for fn in sorted(missing):
            print(f"  [ENUM] {fn} — declared in {declared[fn]}, no implementation")
    else:
        print(f"enum coverage: OK ({len(declared)} String() decls, all implemented)")
    return 1 if (strict and missing) else 0


if __name__ == "__main__":
    sys.exit(main())
