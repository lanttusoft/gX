#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: source-lineage audit. gX is distilled from the author's private gameX
#   archive; this tool finds high token-shingle overlap against an optional
#   archive/third-party corpus and requires a lineage banner or an explicit
#   credit marker on the gX side. It is a heuristic review aid, not a legal
#   originality proof, and it never publishes the compared paths.
"""Audit source lineage/citation for ported or adapted code."""

import argparse
import json
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXTENSIONS = {".c", ".h", ".cpp", ".hpp", ".cc", ".glsl", ".vert", ".frag",
              ".geom", ".s"}
TOKEN_RE = re.compile(
    r'[A-Za-z_][A-Za-z0-9_]*|[0-9]+(?:\.[0-9]+)?|"(?:\\.|[^"\\])*"|\S')
SHINGLE = 10
SKIP_PARTS = {"imgui", "external", "external/raylib"}

CREDIT_WORDS = (
    "raylib", "rlgl", "raymath", "imgui", "angelscript", "gamex", "game3",
    "game2", "three.js", "fxaa", "emmir", "ntsc", "mathlib", "stb_image",
    "cgltf", "miniaudio", "par_shapes", "glad", "glfw", "open dynamics",
)
CREDIT_PHRASES = (
    "ported from", "adapted from", "borrowed code", "based on",
    "derived from", "remake of", "uses borrowed",
)


def tokenise(path):
    try:
        text = path.read_text(errors="ignore")
    except OSError:
        return None
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return TOKEN_RE.findall(text)


def shingles(tokens):
    return {hash(tuple(tokens[i:i + SHINGLE]))
            for i in range(max(0, len(tokens) - SHINGLE + 1))}


def source_files(root, skip=False):
    files = []
    for path in root.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in EXTENSIONS:
            continue
        if ".git" in path.parts:
            continue
        if skip and any(part in SKIP_PARTS for part in path.parts):
            continue
        try:
            if path.stat().st_size < 2_000_000:
                files.append(path)
        except OSError:
            continue
    return files


def credited(path):
    head = path.read_text(errors="ignore")[:12000].lower()
    if "[gx:" in head:
        return True
    if any(word in head for word in CREDIT_WORDS):
        return True
    return any(phrase in head for phrase in CREDIT_PHRASES)


def compare(gx_files, corpus_root, threshold):
    corpus = {}
    for path in source_files(corpus_root):
        tokens = tokenise(path)
        if tokens:
            value = shingles(tokens)
            if len(value) >= 20:
                corpus[path] = value
    index = defaultdict(set)
    for path, value in corpus.items():
        for item in value:
            index[item].add(path)
    matches = []
    for path in gx_files:
        tokens = tokenise(path)
        if not tokens:
            continue
        value = shingles(tokens)
        if len(value) < 20:
            continue
        counts = defaultdict(int)
        for item in value:
            for candidate in index.get(item, ()):
                counts[candidate] += 1
        if not counts:
            continue
        candidate, shared = max(counts.items(), key=lambda item: item[1])
        containment = shared / len(value)
        if containment >= threshold:
            matches.append({
                "gx": str(path.relative_to(ROOT)),
                "corpus": str(candidate.relative_to(corpus_root)),
                "containment": containment,
                "credited": credited(path),
            })
    matches.sort(key=lambda item: item["containment"], reverse=True)
    return matches, len(corpus)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path,
                        help="private source archive to compare (e.g. gameX)")
    parser.add_argument("--compare", type=Path, action="append", default=[],
                        help="additional third-party source tree (repeatable)")
    parser.add_argument("--threshold", type=float, default=0.25)
    parser.add_argument("--strict", action="store_true",
                        help="exit 1 when an uncited match is found")
    parser.add_argument("--json", type=Path, help="write the full report")
    args = parser.parse_args()

    gx_files = source_files(ROOT / "src", skip=True)
    report = {"threshold": args.threshold, "gx_files": len(gx_files),
              "comparisons": []}
    uncited = []
    for label, corpus_root in [("archive", args.archive)] + [
            ("compare", path) for path in args.compare]:
        if corpus_root is None:
            continue
        if not corpus_root.is_dir():
            print("LINEAGE_AUDIT: corpus missing: %s" % corpus_root)
            return 2
        matches, corpus_count = compare(gx_files, corpus_root, args.threshold)
        missing = [item for item in matches if not item["credited"]]
        uncited.extend(missing)
        report["comparisons"].append({
            "label": label,
            "corpus": str(corpus_root),
            "corpus_files": corpus_count,
            "matches": len(matches),
            "uncited": len(missing),
            "top": matches[:20],
        })
        print("LINEAGE_AUDIT: %s — %d files, %d matches >= %.2f, %d uncited"
              % (label, corpus_count, len(matches), args.threshold,
                 len(missing)))
        for item in missing[:10]:
            print("  UNCITED %s <= %s (%.3f)"
                  % (item["gx"], item["corpus"], item["containment"]))
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                             encoding="utf-8")
    if not report["comparisons"]:
        print("LINEAGE_AUDIT: PASS — %d source files; no corpus requested"
              % len(gx_files))
        return 0
    if uncited:
        print("LINEAGE_AUDIT: FAIL — %d uncited high-similarity file(s)"
              % len(uncited))
        return 1 if args.strict else 0
    print("LINEAGE_AUDIT: PASS — every high-similarity file carries a lineage "
          "banner or explicit credit")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print("LINEAGE_AUDIT: FAIL — %s" % error)
        sys.exit(1)
