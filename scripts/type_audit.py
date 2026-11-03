#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: type-aware audit for the gX alias contract. libclang supplies the real
#   declaration types, so `gX_I1 flag = -1` (bool sentinel), bool counters and
#   counters/returns are caught without regex false positives. Raw host
#   type declarations are ratcheted against a checked-in baseline.
"""Audit gX's custom-type contract.

Findings:
  1. `gX_I1`/`bool` initialized or assigned an integer outside 0/1.
  2. `gX_I1`/`bool` compared to an out-of-range constant.
  3. `gX_I1`/`bool` incremented, decremented or compound-assigned.
  4. `gX_I1` functions returning a negative/sentinel value.
  5. `#include <stdbool.h>` outside the central type headers.
  6. Raw `int`/`float`/`double`/... declarations must not grow in engine core
     (existing files are grandfathered by scripts/fixtures/type_audit_baseline.json).

The default scope is the source changes since the latest tag (or HEAD~1);
`--all` scans every compile-database entry. `--strict` makes findings fatal.

Usage:
    python3 scripts/type_audit.py                 # changed files, advisory
    python3 scripts/type_audit.py --all --strict  # full gate
    python3 scripts/type_audit.py --update-baseline
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

try:
    import clang.cindex as ci
except ImportError:
    ci = None


ROOT = Path(__file__).resolve().parents[1]
BASELINE = Path(__file__).resolve().parent / "fixtures" / "type_audit_baseline.json"
DEFAULT_DB = ROOT / "build" / "lint" / "compile_commands.json"

SCAN_SKIP_PREFIXES = ("src/external/", "src/tools/imgui/")
RAWTYPE_SKIP_PREFIXES = (
    "src/external/",
    "src/backend/",
    "src/wrappers/",
    "src/scripting",
    "src/tools/",
    "src/common/marshal/json_impl",
)
STDBOOL_ALLOW_PREFIXES = ("src/common/types",)

BOOL_KINDS = {ci.TypeKind.BOOL} if ci else set()
NARROW_KINDS = ({
    ci.TypeKind.CHAR_S, ci.TypeKind.CHAR_U, ci.TypeKind.SCHAR,
    ci.TypeKind.UCHAR, ci.TypeKind.SHORT, ci.TypeKind.USHORT,
    ci.TypeKind.INT, ci.TypeKind.UINT,
} if ci else set())
REF_KINDS = ({
    ci.CursorKind.TYPE_REF, ci.CursorKind.NAMESPACE_REF,
    ci.CursorKind.TEMPLATE_REF, ci.CursorKind.MEMBER_REF,
} if ci else set())

RAW_DECL_RE = re.compile(
    r"^\s*(?:static\s+|const\s+|volatile\s+|extern\s+|inline\s+|register\s+)*"
    r"(int|unsigned|float|double|bool|char|short|long)\b")


def is_bool(t):
    return t.get_canonical().kind in BOOL_KINDS


def is_bool_expr(c):
    if c is None:
        return False
    if is_bool(c.type):
        return True
    if c.kind in (ci.CursorKind.UNEXPOSED_EXPR, ci.CursorKind.PAREN_EXPR):
        child = expr_child(c)
        return is_bool_expr(child) if child is not None else False
    if c.kind == ci.CursorKind.DECL_REF_EXPR:
        if c.referenced is None:
            return False
        return is_bool(c.referenced.type)
    return False


def is_bool_lvalue(c):
    if c is None:
        return False
    if c.kind in (ci.CursorKind.DECL_REF_EXPR, ci.CursorKind.MEMBER_REF_EXPR):
        return is_bool_expr(c)
    if c.kind in (ci.CursorKind.UNEXPOSED_EXPR, ci.CursorKind.PAREN_EXPR):
        return is_bool_lvalue(expr_child(c))
    return False


def expr_child(c):
    for child in c.get_children():
        if child.kind not in REF_KINDS:
            return child
    return None


def const_int(c):
    if c is None:
        return None
    if c.kind == ci.CursorKind.INTEGER_LITERAL:
        try:
            return int(next(iter(c.get_tokens())).spelling, 0)
        except (StopIteration, ValueError):
            return None
    if c.kind in (ci.CursorKind.UNARY_OPERATOR, ci.CursorKind.CSTYLE_CAST_EXPR,
                  ci.CursorKind.PAREN_EXPR, ci.CursorKind.UNEXPOSED_EXPR):
        child = expr_child(c)
        value = const_int(child)
        if value is None:
            return None
        if c.kind == ci.CursorKind.UNARY_OPERATOR:
            tokens = list(c.get_tokens())
            if tokens and tokens[0].spelling == "-":
                return -value
        return value
    return None


def locate(c):
    if not c.location.file:
        return None
    return str(Path(c.location.file.name).resolve())


def add(findings, cursor, message, reason):
    path = locate(cursor)
    if path is None:
        return
    findings.append({
        "file": path,
        "line": cursor.location.line,
        "column": cursor.location.column,
        "message": message,
        "reason": reason,
    })


def collect(tu, findings):
    def visit(c, bool_return=False):
        kind = c.kind
        if kind == ci.CursorKind.FUNCTION_DECL:
            bool_return = is_bool(c.result_type)
        if kind in (ci.CursorKind.VAR_DECL, ci.CursorKind.FIELD_DECL,
                    ci.CursorKind.PARM_DECL) and is_bool(c.type):
            value = const_int(expr_child(c))
            if value not in (None, 0, 1):
                add(findings, c,
                    "gX_I1 sentinel: %s = %d" % (c.spelling, value),
                    "gX_I1 is bool; -1/2 become true. Use gX_I32 for tri-state.")
        elif kind == ci.CursorKind.BINARY_OPERATOR:
            tokens = [token.spelling for token in c.get_tokens()]
            if any(op in tokens for op in ("==", "!=", "<", "<=", ">", ">=")):
                children = [ch for ch in c.get_children() if ch.kind not in REF_KINDS]
                if len(children) == 2:
                    left, right = children
                    lv, rv = const_int(left), const_int(right)
                    if is_bool_expr(left) and rv is not None and rv not in (0, 1):
                        add(findings, c, "gX_I1 compared to %d" % rv,
                            "gX_I1 is bool; the comparison is constant.")
                    elif is_bool_expr(right) and lv is not None and lv not in (0, 1):
                        add(findings, c, "gX_I1 compared to %d" % lv,
                            "gX_I1 is bool; the comparison is constant.")
        elif kind == ci.CursorKind.UNARY_OPERATOR:
            tokens = [token.spelling for token in c.get_tokens()]
            #ai: only the operator's own edge token counts; a nested Text++
            #   inside *Text++ must not read as a bool counter.
            if tokens and tokens[0] in ("++", "--") or tokens and tokens[-1] in ("++", "--"):
                child = expr_child(c)
                if is_bool_lvalue(child):
                    add(findings, c, "gX_I1 counter: %s" % c.spelling,
                        "gX_I1 is a flag, not a counter.")
        elif kind == ci.CursorKind.COMPOUND_ASSIGNMENT_OPERATOR:
            child = expr_child(c)
            if child is not None and is_bool_expr(child):
                add(findings, c, "gX_I1 compound assignment",
                    "gX_I1 is a flag, not an arithmetic value.")
        elif kind == ci.CursorKind.RETURN_STMT and bool_return:
            value = const_int(expr_child(c))
            if value not in (None, 0, 1):
                add(findings, c, "gX_I1 function returns %d" % value,
                    "gX_I1 is bool; return 0/1 or change the return type.")
        elif kind == ci.CursorKind.CSTYLE_CAST_EXPR:
            if c.type.get_canonical().kind in BOOL_KINDS:
                value = const_int(expr_child(c))
                if value not in (None, 0, 1):
                    add(findings, c, "gX_I1 cast of %d" % value,
                        "gX_I1 is bool; negative values become true.")
        for child in c.get_children():
            visit(child, bool_return)
    visit(tu.cursor)


def load_compile_db(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    entries = {}
    for entry in data:
        file = str(Path(entry["file"]).resolve())
        args = entry.get("arguments")
        if args is None:
            args = shlex.split(entry.get("command", ""))
        else:
            args = list(args)
        cleaned = []
        skip_next = False
        for index, arg in enumerate(args):
            if skip_next:
                skip_next = False
                continue
            if index == 0 and not arg.startswith("-"):
                continue
            if arg in ("-c", "-o"):
                if arg == "-o":
                    skip_next = True
                continue
            if arg == file:
                continue
            #ai: libclang only needs language/include/define flags; warning
            #   flags from a GCC compile DB can be unknown to older libclang.
            if arg.startswith("-W"):
                continue
            cleaned.append(arg)
        entries[file] = cleaned
    return entries


def git_changed_sources(base):
    result = subprocess.run(
        ["git", "-C", str(ROOT), "diff", "--name-only", "--diff-filter=ACMR",
         base, "--", "src"],
        capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError("git diff against %s failed: %s" % (
            base, result.stderr.strip()))
    files = []
    for line in result.stdout.splitlines():
        if line.endswith((".c", ".cpp")):
            files.append(str((ROOT / line).resolve()))
    return files


def latest_tag():
    result = subprocess.run(
        ["git", "-C", str(ROOT), "describe", "--tags", "--abbrev=0"],
        capture_output=True, text=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else "HEAD~1"


def raw_type_counts():
    counts = {}
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in (".c", ".h"):
            continue
        relative = str(path.relative_to(ROOT))
        if relative.startswith(RAWTYPE_SKIP_PREFIXES):
            continue
        text = path.read_text(errors="ignore")
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        text = re.sub(r"//[^\n]*", "", text)
        count = sum(1 for line in text.splitlines() if RAW_DECL_RE.match(line))
        if count:
            counts[relative] = count
    return counts


def load_baseline():
    if not BASELINE.is_file():
        return {}
    try:
        return json.loads(BASELINE.read_text(encoding="utf-8")).get(
            "raw_type_counts", {})
    except (OSError, ValueError):
        return {}


def stdbool_findings():
    findings = []
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in (".c", ".h", ".cpp", ".hpp"):
            continue
        relative = str(path.relative_to(ROOT))
        if relative.startswith(SCAN_SKIP_PREFIXES + STDBOOL_ALLOW_PREFIXES):
            continue
        text = path.read_text(errors="ignore")
        for number, line in enumerate(text.splitlines(), 1):
            if re.search(r'#\s*include\s*[<"]stdbool\.h[>"]', line):
                findings.append({
                    "file": str(path.resolve()),
                    "line": number,
                    "column": 1,
                    "message": "stdbool.h include",
                    "reason": "Use gX_I1 (src/common/types.h); stdbool is only for the central type headers.",
                })
    return findings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--all", action="store_true",
                        help="scan every compile-database source, not just changes")
    parser.add_argument("--base",
                        help="git base for the changed-file scope (default: latest tag)")
    parser.add_argument("--strict", action="store_true",
                        help="exit 1 when findings remain")
    parser.add_argument("--json", type=Path, help="write findings as JSON")
    parser.add_argument("--update-baseline", action="store_true",
                        help="rewrite the raw-type baseline from this tree")
    parser.add_argument("--compile-db", type=Path, default=DEFAULT_DB,
                        help="compile_commands.json (default: build/lint)")
    args = parser.parse_args()

    if args.update_baseline:
        counts = raw_type_counts()
        BASELINE.parent.mkdir(parents=True, exist_ok=True)
        BASELINE.write_text(json.dumps({
            "description": "Grandfathered raw-type declaration counts (engine core).",
            "raw_type_counts": counts,
        }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print("TYPE_AUDIT: baseline updated (%d files)" % len(counts))
        return 0

    if ci is None:
        print("TYPE_AUDIT: FAIL — python clang bindings missing")
        return 2
    if not args.compile_db.is_file() and args.compile_db == DEFAULT_DB:
        subprocess.run(
            ["cmake", "-B", str(ROOT / "build" / "lint"),
             "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
             str(ROOT)],
            capture_output=True, text=True, check=False)
    if not args.compile_db.is_file():
        print("TYPE_AUDIT: FAIL — %s missing; run cmake with "
              "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON" % args.compile_db)
        return 2

    entries = load_compile_db(args.compile_db)
    if args.all:
        files = sorted(path for path in entries
                       if path.endswith((".c", ".cpp"))
                       and not str(Path(path).relative_to(ROOT)).startswith(SCAN_SKIP_PREFIXES))
    else:
        base = args.base or os.environ.get("GX_TYPE_BASE") or latest_tag()
        files = git_changed_sources(base)

    findings = stdbool_findings()
    parsed = 0
    for path in files:
        args_list = entries.get(path)
        if args_list is None:
            continue
        index = ci.Index.create()
        tu = index.parse(path, args=args_list)
        collect(tu, findings)
        parsed += 1

    unique = {}
    source_root = str((ROOT / "src").resolve()) + os.sep
    for item in findings:
        if not item["file"].startswith(source_root):
            continue
        relative = os.path.relpath(item["file"], ROOT)
        if relative.startswith(("external/", "src/external/", "src/tools/imgui/")):
            continue
        key = (item["file"], item["line"], item["column"], item["message"])
        unique[key] = item
    findings = sorted(unique.values(), key=lambda item: (
        item["file"], item["line"], item["message"]))

    raw_counts = raw_type_counts()
    baseline = load_baseline()
    for relative, count in sorted(raw_counts.items()):
        allowed = baseline.get(relative, 0)
        if count > allowed:
            findings.append({
                "file": str((ROOT / relative).resolve()),
                "line": 0,
                "column": 0,
                "message": "raw-type declarations grew: %d (baseline %d)" % (
                    count, allowed),
                "reason": "Engine core uses gX_I8/I32/F32/... aliases; replace the new raw type.",
            })

    print("TYPE_AUDIT: %d findings across %d translation units" % (
        len(findings), parsed))
    for item in findings[:80]:
        print("  %s:%s %s — %s" % (
            os.path.relpath(item["file"], ROOT), item["line"] or "-",
            item["message"], item["reason"]))
    if len(findings) > 80:
        print("  ... %d more" % (len(findings) - 80))

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({
            "findings": findings,
            "raw_type_counts": raw_counts,
            "baseline": baseline,
        }, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    if args.strict and findings:
        print("TYPE_AUDIT: FAIL")
        return 1
    print("TYPE_AUDIT: PASS" if not findings else "TYPE_AUDIT: advisory findings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
