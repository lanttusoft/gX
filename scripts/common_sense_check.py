#!/usr/bin/env python3
#ai: common-sense regression guard — freezes the accepted findings of the
#   2026-09-29 coherence pass (docs/audit_backlog.md) and fails on NEW ones:
#     - public gX_* functions declared in headers but defined nowhere
#     - GX_* defines with no user in src/shaders/harness/scripts/CMake
#     - docs/*.md path references that do not exist
#     - gX_MainArguments fields parsed but never read outside main_parse.c
#   Accepted findings live in .sense_baseline.json; run --update to rewrite it
#   deliberately (a resolved finding is reported but does not fail).

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
SRC = PROJECT / "src"
BASELINE = PROJECT / ".sense_baseline.json"

USAGE_ROOTS = [
    SRC,
    PROJECT / "resources" / "shader",
    PROJECT / "harness",
    PROJECT / "scripts",
]
USAGE_FILES = [PROJECT / "CMakeLists.txt", PROJECT / "build.sh",
               PROJECT / "Makefile"]

DECL_RE = re.compile(
    r"^[ \t]*(?:extern[ \t]+)?(?:const[ \t]+)?[A-Za-z_][A-Za-z0-9_]*[ \t\*]+"
    r"(gX_[A-Za-z0-9_]+)[ \t]*\(", re.M)
DEFINE_RE = re.compile(r"^#define[ \t]+(GX_[A-Z0-9_]+)\b", re.M)
ARG_STRUCT_RE = re.compile(
    r"typedef struct gX_MainArguments \{(.*?)\} gX_MainArguments;", re.S)
ARG_FIELD_RE = re.compile(
    r"^[ \t]*(?:gX_[A-Za-z0-9_]+|gX_TextLine\w*)[ \t]+"
    r"((?:has)?[A-Za-z][A-Za-z0-9_]*)[ \t]*;", re.M)
DOC_PATH_RE = re.compile(
    r"(?<![\w/.-])(src|scripts|resources|docs|harness|proofs|license)/"
    r"[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.{}%-]+)*")


def text_of(paths, suffixes=None):
    parts = []
    for path in paths:
        if path.is_file():
            parts.append(path.read_text(errors="ignore"))
        elif path.is_dir():
            for item in sorted(path.rglob("*")):
                if item.is_file() and (suffixes is None
                                       or item.suffix in suffixes):
                    parts.append(item.read_text(errors="ignore"))
    return "\n".join(parts)


def scan_declared_undefined():
    source_text = text_of([SRC], {".c", ".cpp"})
    #ai: one pass — collect every called identifier, then membership-test the
    #   declarations (per-name regexes over ~3 MB took minutes).
    defined = set(re.findall(r"\b(gX_[A-Za-z0-9_]+)[ \t]*\(", source_text))
    declared = {}
    for header in sorted(SRC.rglob("*.h")):
        for match in DECL_RE.finditer(header.read_text(errors="ignore")):
            declared.setdefault(match.group(1), header.name)
    return sorted(name for name in declared if name not in defined)


def scan_unused_defines():
    usage_text = text_of(USAGE_ROOTS) + "\n" + text_of(USAGE_FILES)
    #ai: one pass — a Counter of every GX_ token; the definition itself
    #   contributes the single allowed occurrence.
    counts = Counter(re.findall(r"\b(GX_[A-Z0-9_]+)\b", usage_text))
    unused = []
    for header in sorted(SRC.rglob("*.h")):
        for match in DEFINE_RE.finditer(header.read_text(errors="ignore")):
            name = match.group(1)
            if counts.get(name, 0) <= 1:
                unused.append(name)
    return sorted(set(unused))


def scan_missing_doc_paths():
    docs = sorted((PROJECT / "docs").glob("*.md"))
    if (PROJECT / "readme.txt").is_file():
        docs.append(PROJECT / "readme.txt")
    missing = []
    for doc in docs:
        for match in DOC_PATH_RE.finditer(doc.read_text(errors="ignore")):
            path = match.group(0).rstrip(".,:;)`\"'")
            if "{" in path or "%" in path or path.endswith("/"):
                continue
            if not (PROJECT / path).exists():
                missing.append(f"{doc.relative_to(PROJECT)}:{path}")
    return sorted(set(missing))


def scan_unused_arguments():
    main_header = (SRC / "main.h").read_text(errors="ignore")
    match = ARG_STRUCT_RE.search(main_header)
    if not match:
        raise RuntimeError("gX_MainArguments struct not found in src/main.h")
    fields = {m.group(1) for m in ARG_FIELD_RE.finditer(match.group(1))}
    usage = text_of([SRC], {".c", ".cpp"})
    #ai: one pass — every field read through the arguments global.
    used = set(re.findall(r"\barguments\.([A-Za-z_][A-Za-z0-9_]*)\b", usage))
    return sorted(fields - used)


SCANS = {
    "declared_undefined": scan_declared_undefined,
    "unused_defines": scan_unused_defines,
    "missing_doc_paths": scan_missing_doc_paths,
    "unused_arguments": scan_unused_arguments,
}


def load_baseline():
    if not BASELINE.is_file():
        return {}
    return json.loads(BASELINE.read_text())


def write_baseline(findings):
    payload = {name: findings.get(name, []) for name in SCANS}
    payload["note"] = ("Accepted findings from the 2026-09-29 common-sense "
                       "pass; regenerate deliberately with --update.")
    BASELINE.write_text(json.dumps(payload, indent=1) + "\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--update", action="store_true",
                        help="rewrite .sense_baseline.json with current findings")
    args = parser.parse_args()

    findings = {name: scan() for name, scan in SCANS.items()}
    if args.update:
        write_baseline(findings)
        for name, items in findings.items():
            print(f"  [SENSE] {name}: {len(items)} accepted")
        print(f"SENSE_CHECK: baseline updated ({BASELINE.name})")
        return 0

    baseline = load_baseline()
    new_count = 0
    for name, items in findings.items():
        accepted = set(baseline.get(name, []))
        new = sorted(set(items) - accepted)
        gone = sorted(accepted - set(items))
        print(f"  [SENSE] {name}: {len(items)} findings, "
              f"{len(new)} new, {len(gone)} baseline-resolved")
        for item in new:
            print(f"    NEW: {item}")
        for item in gone:
            print(f"    resolved (update baseline): {item}")
        new_count += len(new)
    if new_count:
        print(f"SENSE_CHECK: FAIL — {new_count} new finding(s); "
              f"fix them or run --update deliberately", file=sys.stderr)
        return 1
    print("SENSE_CHECK: PASS — no new findings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
