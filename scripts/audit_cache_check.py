#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: audit-cache gate. Runs a two-gate summary three times: a clean run
#   (must execute), a repeat (must be cached), and --no-cache (must execute
#   again). Pins the memory contract end to end.

import os
import re
import subprocess
import sys
from pathlib import Path

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
SUMMARY = ROOT / "scripts" / "audit_summary.py"
ONLY = "texture,dither"


def run_summary(*extra):
    environment = dict(os.environ)
    environment["DISPLAY"] = audit.display()
    environment["AUDIT_CACHE"] = "1"
    result = subprocess.run(
        ["python3", str(SUMMARY), "--full", "--only", ONLY, "--jobs", "2"]
        + list(extra),
        cwd=ROOT, env=environment, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, timeout=600)
    return audit.strip_ansi(result.stdout or ""), result.returncode


def main():
    if not BIN.is_file() or not SUMMARY.is_file():
        print("CACHE_CHECK: missing binary or summary runner", file=sys.stderr)
        return 1
    try:
        first, code = run_summary("--cache-clear")
        if code != 0 or "2/2 gates passed" not in first:
            raise RuntimeError(f"clean run failed: {first[-400:]}")
        if "(cached)" in first:
            raise RuntimeError("clean run reported cached rows")
        second, code = run_summary()
        if code != 0 or "2/2 gates passed" not in second:
            raise RuntimeError(f"repeat run failed: {second[-400:]}")
        if not re.search(r"2/2 gates passed.*, 2 cached", second):
            raise RuntimeError(f"repeat run did not cache: {second[-400:]}")
        third, code = run_summary("--no-cache")
        if code != 0 or "2/2 gates passed" not in third:
            raise RuntimeError(f"no-cache run failed: {third[-400:]}")
        if "(cached)" in third:
            raise RuntimeError("--no-cache still reported cached rows")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"CACHE_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print("CACHE_CHECK: PASS clean run executes, repeat is cached, "
          "--no-cache re-runs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
