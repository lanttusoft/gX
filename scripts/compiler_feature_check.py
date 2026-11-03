#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: compiler capability gate — the build probes flags/features in
#   cmake/CompilerFeatures.cmake; this asserts the required set is present
#   instead of silently falling back, and independently compiles the alias
#   size/bool-identity contract.
"""Check the configured compiler's capabilities and type contract.

Reads `build/generated/compiler_features.txt` (written by CMake) and compiles
a small probe that includes `src/common/types/primary.h`. Exits non-zero when
a required feature is missing, the compiler is unsupported, or a type-size /
`gX_I1` bool identity assertion fails.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REPORT = ROOT / "build" / "generated" / "compiler_features.txt"
SUPPORTED_IDS = {"GNU", "Clang", "AppleClang"}
REQUIRED = (
    "c11_static_assert",
    "c11_thread_local",
    "c11_generic",
    "c11_atomics",
    "gnuc_atomic",
    "stdatomic",
    "builtin_expect",
    "werror_bool_compare",
    "werror_bool_operation",
    "werror_pointer_to_int",
    "werror_int_to_pointer",
)

PROBE = r"""
#include "common/types/primary.h"
_Static_assert(sizeof(gX_I1) == 1, "gX_I1 size");
_Static_assert(sizeof(gX_I8) == 1, "gX_I8 size");
_Static_assert(sizeof(gX_I16) == 2, "gX_I16 size");
_Static_assert(sizeof(gX_I32) == 4, "gX_I32 size");
_Static_assert(sizeof(gX_I64) == 8, "gX_I64 size");
_Static_assert(sizeof(gX_F32) == 4, "gX_F32 size");
_Static_assert(sizeof(gX_F64) == 8, "gX_F64 size");
_Static_assert(_Generic((gX_I1)0, _Bool: 1, default: 0), "gX_I1 is not bool");
int main(void) { return 0; }
"""


def parse_report(path):
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    return values


def run_probe(cc):
    with tempfile.TemporaryDirectory(prefix="gx_type_probe_", dir=os.environ.get(
            "GX_SCRATCH", "/tmp/opencode")) as directory:
        source = Path(directory) / "probe.c"
        source.write_text(PROBE, encoding="utf-8")
        result = subprocess.run(
            [cc, "-std=gnu11", "-I", str(ROOT / "src"), "-fsyntax-only",
             str(source)],
            capture_output=True, text=True, check=False)
        return result.returncode == 0, result.stderr.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    args = parser.parse_args()

    if not args.report.is_file():
        print("COMPILER_CHECK: FAIL — %s missing; configure the build first"
              % args.report)
        return 2
    values = parse_report(args.report)
    compiler = values.get("compiler_id", "")
    version = values.get("compiler_version", "")
    failures = []
    if compiler not in SUPPORTED_IDS:
        failures.append("unsupported compiler id '%s' (supported: %s)" % (
            compiler, ", ".join(sorted(SUPPORTED_IDS))))
    for feature in REQUIRED:
        if values.get(feature) != "1":
            failures.append("missing compiler feature: %s" % feature)
    probe_ok, probe_error = run_probe(os.environ.get("CC", "cc"))
    if not probe_ok:
        failures.append("type probe failed: %s" % probe_error)

    print("COMPILER_CHECK: compiler=%s %s (%s)" % (
        compiler or "?", version or "?", args.report))
    if failures:
        for failure in failures:
            print("  FAIL %s" % failure)
        print("COMPILER_CHECK: FAIL")
        return 1
    print("COMPILER_CHECK: PASS — C11 atomics/thread-local/_Generic, GNU atomics, "
          "and bool/pointer warning errors are enabled")
    return 0


if __name__ == "__main__":
    sys.exit(main())
