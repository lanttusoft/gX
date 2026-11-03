#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: gcov line-coverage summary for a GX_COVERAGE build. Reads the .gcno /
#   .gcda pair per source and aggregates the executed-line percentages.
"""Summarize gcov coverage for a `-DGX_COVERAGE=ON` build tree."""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD = ROOT / "build" / "coverage"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", nargs="?", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--json", type=Path,
                        help="write per-file coverage JSON (default: <build>/coverage.json)")
    parser.add_argument("--min", type=float, default=0.0,
                        help="exit non-zero when total line coverage is below this percent")
    args = parser.parse_args()
    build = args.build.resolve()
    sources = sorted(build.glob("CMakeFiles/*.dir/src/**/*.gcno"))
    if not sources:
        print("COVERAGE: FAIL — no .gcno files under %s; build with GX_COVERAGE=ON" % build)
        return 2

    files = {}
    scratch = Path(os.environ.get("GX_SCRATCH", "/tmp/opencode"))
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="gx_coverage_", dir=scratch) as directory:
        for gcno in sources:
            if not gcno.with_suffix(".gcda").exists():
                continue
            result = subprocess.run(
                ["gcov", "-b", "-c", str(gcno)],
                cwd=directory, capture_output=True, text=True, check=False,
                env={**os.environ, "LC_ALL": "C", "LANG": "C"})
            current = None
            for line in result.stdout.splitlines():
                match = re.match(r"File '(.+)'", line)
                if match:
                    current = match.group(1)
                    continue
                match = re.match(r"Lines executed:([0-9.]+)% of ([0-9]+)", line)
                if match and current and "/src/" in current:
                    if not current.endswith((".c", ".cpp")):
                        continue
                    if "/external/" in current or "/tools/imgui/" in current:
                        continue
                    percent = float(match.group(1))
                    total = int(match.group(2))
                    files[current] = {
                        "percent": percent,
                        "total": total,
                        "executed": round(percent * total / 100.0),
                    }

    total = sum(item["total"] for item in files.values())
    executed = sum(item["executed"] for item in files.values())
    overall = (executed / total * 100.0) if total else 0.0
    lowest = sorted(files.items(), key=lambda item: item[1]["percent"])[:10]
    print("COVERAGE: %.2f%% lines (%d/%d) across %d files" % (
        overall, executed, total, len(files)))
    for path, item in lowest:
        print("  %5.1f%%  %s" % (item["percent"], path))

    output = args.json or (build / "coverage.json")
    output.write_text(json.dumps({
        "overall_percent": overall,
        "executed_lines": executed,
        "total_lines": total,
        "files": files,
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("  json=%s" % output)
    if overall < args.min:
        print("COVERAGE: FAIL — %.2f%% < %.2f%%" % (overall, args.min))
        return 1
    print("COVERAGE: PASS")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.SubprocessError, ValueError) as error:
        print("COVERAGE: FAIL — %s" % error)
        sys.exit(2)
