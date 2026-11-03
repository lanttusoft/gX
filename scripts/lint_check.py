#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: clang-tidy diff gate — only new/changed lines are graded, so legacy
#   findings do not block the branch. The check set is deliberately narrow:
#   the clang static analyzer plus high-signal bugprone checks.
"""Run the clang-tidy gate over source changes.

The compile database is `build/lint/compile_commands.json` (configured with
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`). The diff is taken against the latest
tag (`GX_LINT_BASE`/`--base` overrides it), so a release tree is clean by
construction and every change since the release is graded.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DB_DIR = ROOT / "build" / "lint"
CHECKS = ",".join([
    "-*",
    "clang-analyzer-core.*",
    "clang-analyzer-deadcode.*",
    "clang-analyzer-unix.*",
    "bugprone-signed-char-misuse",
    "bugprone-misplaced-widening-cast",
    "bugprone-sizeof-expression",
    "bugprone-suspicious-memset-usage",
    "bugprone-incorrect-roundings",
    "bugprone-too-small-loop-variable",
    "bugprone-narrowing-conversions",
    "misc-redundant-expression",
    "misc-static-assert",
    "portability-*",
])
IGNORE = ("external/", "src/external/", "src/tools/imgui/", "raylib")
KEEP = ("clang-analyzer-", "bugprone-", "misc-redundant-expression",
        "misc-static-assert", "portability-")


def find_diff_tool():
    for path in sorted(Path("/usr/lib").glob("llvm*/share/clang/clang-tidy-diff.py"),
                       reverse=True):
        if path.is_file():
            return path
    for candidate in ("/usr/share/clang/clang-tidy-diff.py",
                      "/usr/local/share/clang/clang-tidy-diff.py"):
        if Path(candidate).is_file():
            return Path(candidate)
    return None


def latest_tag():
    result = subprocess.run(
        ["git", "-C", str(ROOT), "describe", "--tags", "--abbrev=0"],
        capture_output=True, text=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else "HEAD~1"


def ensure_database():
    if (DB_DIR / "compile_commands.json").is_file():
        return True
    result = subprocess.run(
        ["cmake", "-B", str(DB_DIR), "-DCMAKE_BUILD_TYPE=Debug",
         "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", str(ROOT)],
        capture_output=True, text=True, check=False)
    return result.returncode == 0 and (DB_DIR / "compile_commands.json").is_file()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", help="git base (default: latest tag)")
    parser.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 2))
    parser.add_argument("--all", action="store_true",
                        help="scan every source in the database (slow, advisory)")
    parser.add_argument("--advisory", action="store_true",
                        help="report findings without a non-zero exit")
    args = parser.parse_args()

    tidy = shutil.which("clang-tidy")
    diff_tool = find_diff_tool()
    if not tidy or not diff_tool:
        print("LINT_CHECK: SKIP — clang-tidy or clang-tidy-diff.py not installed")
        return 0
    if not ensure_database():
        print("LINT_CHECK: FAIL — compile database unavailable (%s)" % DB_DIR)
        return 2

    base = args.base or os.environ.get("GX_LINT_BASE") or latest_tag()
    if args.all:
        files = [str(path.relative_to(ROOT))
                 for path in sorted((ROOT / "src").rglob("*"))
                 if path.suffix in (".c", ".cpp")
                 and not str(path.relative_to(ROOT)).startswith(IGNORE)]
        command = [tidy, "-p", str(DB_DIR), "--use-color=false",
                   "--checks=" + CHECKS, "--header-filter=^.*/src/.*", *files]
        result = subprocess.run(command, capture_output=True, text=True)
        output = result.stdout + result.stderr
    else:
        diff = subprocess.run(
            ["git", "-C", str(ROOT), "diff", "-U0", base, "--", "src"],
            capture_output=True, text=True, check=False)
        if diff.returncode != 0:
            print("LINT_CHECK: FAIL — git diff against %s failed" % base)
            return 2
        if not diff.stdout.strip():
            print("LINT_CHECK: PASS — no source changes against %s" % base)
            return 0
        command = [sys.executable, str(diff_tool), "-p", "1",
                   "-path", str(DB_DIR), "-j", str(args.jobs),
                   "-checks=" + CHECKS, "-quiet"]
        result = subprocess.run(command, input=diff.stdout,
                                capture_output=True, text=True)
        output = result.stdout + result.stderr

    findings = []
    for line in output.splitlines():
        if "warning:" not in line and "error:" not in line:
            continue
        if line.startswith(IGNORE) or any(token in line for token in IGNORE):
            continue
        if not any(tag in line for tag in KEEP):
            continue
        findings.append(line)
    findings = list(dict.fromkeys(findings))

    print("LINT_CHECK: %d finding(s) against %s" % (len(findings), base))
    for line in findings[:80]:
        print("  " + line)
    if len(findings) > 80:
        print("  ... %d more" % (len(findings) - 80))

    if findings and not args.advisory:
        print("LINT_CHECK: FAIL")
        return 1
    print("LINT_CHECK: PASS" if not findings else "LINT_CHECK: advisory findings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
