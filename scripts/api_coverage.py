#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

"""
gX API-Reference Coverage Checker
Verifies every public gX_* function family declared in the source headers
is documented in docs/api_reference.md (by prefix wildcard, e.g.
gX_Heap_*). A family is the 2-part prefix (gX_First_Second_); deep
namespaces (gX_GUI_Element_*) fall back to the 3-part prefix.

Skips: backend/ (raylib-specific), external/, game/ (gated).
This checks documentation families only; declaration-vs-definition presence is
audited by api_definition_check.py.

Usage:
    python3 scripts/api_coverage.py             # report
    python3 scripts/api_coverage.py --strict    # exit 1 on uncovered family
"""

import os
import re
import sys
from collections import Counter

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(PROJECT, "src")
DOC = os.path.join(PROJECT, "docs", "api_reference.md")

SKIP_DIRS = {"backend", "external", "game"}
DECL_RE = re.compile(
    r'(?:^|\s)(gX_I8|gX_I16|gX_I32|gX_I64|gX_U8|gX_U16|gX_U32|gX_U64|'
    r'gX_F32|gX_F64|gX_I1|gX_I0|gX_[A-Za-z0-9_]+)\s+(gX_[A-Za-z0-9_]+)\s*\(')
DOC_PREFIX_RE = re.compile(r'\bgX_[A-Za-z0-9_]+_\*')


def collect_names():
    names = []
    for root, dirs, files in os.walk(SRC):
        top = os.path.relpath(root, SRC).split(os.sep)[0]
        if top in SKIP_DIRS:
            dirs[:] = []
            continue
        for f in files:
            if not f.endswith(".h"):
                continue
            for line in open(os.path.join(root, f), errors="ignore").read().splitlines():
                if line.lstrip().startswith("#") or "define" in line[:12]:
                    continue
                for m in DECL_RE.finditer(line):
                    names.append(m.group(2))
    return names


def main():
    strict = "--strict" in sys.argv
    with open(DOC) as f:
        doc = f.read()
    doc_p = [p[:-1] for p in DOC_PREFIX_RE.findall(doc)]

    names = collect_names()
    fam2 = Counter()
    for n in names:
        fam2[" ".join(n.split("_")[:2])] += 1

    uncovered = []
    for fam, count in sorted(fam2.items()):
        cand = "_".join(fam.split()) + "_"
        if any(cand.startswith(d) or d.startswith(cand) for d in doc_p):
            continue
        # deep namespace: check the 3-part sub-families against the doc
        sub = Counter()
        for n in names:
            if n.startswith(cand):
                sub["_".join(n.split("_")[:3]) + "_"] += 1
        if sub and all(any(s.startswith(d) or d.startswith(s) for d in doc_p) for s in sub):
            continue
        uncovered.append((cand, count))

    if uncovered:
        print(f"api coverage: {len(uncovered)} undocumented function family(ies) "
              f"({len(doc_p)} prefixes in doc)")
        for fam, count in sorted(uncovered):
            print(f"  [API] {fam}* ({count} funcs)")
    else:
        print(f"api coverage: OK ({len(fam2)} families, all documented)")
    return 1 if (strict and uncovered) else 0


if __name__ == "__main__":
    sys.exit(main())
