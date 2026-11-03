#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: build and run the libFuzzer JSON harness with ASan. Manual/tooling
#   target; time-boxed and never part of the deterministic gate matrix.
"""Run the JSON parser fuzz harness.

Example:
    python3 scripts/fuzz_json.py --seconds 60
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BIN = ROOT / "build" / "fuzz_json"
DEFAULT_CORPUS = ROOT / "build" / "fuzz-corpus"
DEFAULT_ARTIFACTS = ROOT / "build" / "fuzz-artifacts"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--runs", type=int, default=0)
    parser.add_argument("--binary", type=Path, default=DEFAULT_BIN)
    parser.add_argument("--corpus", type=Path, default=DEFAULT_CORPUS)
    parser.add_argument("--artifacts", type=Path, default=DEFAULT_ARTIFACTS)
    args = parser.parse_args()

    clang = shutil.which("clang")
    if not clang:
        print("FUZZ_JSON: FAIL — clang not found")
        return 2
    args.binary.parent.mkdir(parents=True, exist_ok=True)
    compile_command = [
        clang, "-std=gnu11", "-fsanitize=fuzzer,address", "-g",
        "-I", str(ROOT / "src"),
        "-I", str(ROOT / "src" / "common" / "marshal"),
        str(ROOT / "fuzz" / "fuzz_json.c"),
        "-o", str(args.binary),
    ]
    result = subprocess.run(compile_command, capture_output=True, text=True)
    if result.returncode != 0:
        print("FUZZ_JSON: FAIL — build failed")
        print(result.stderr[-2000:])
        return 2

    args.corpus.mkdir(parents=True, exist_ok=True)
    for seed in sorted((ROOT / "resources" / "tools").glob("*.json")):
        target = args.corpus / seed.name
        if not target.exists():
            shutil.copyfile(seed, target)
    args.artifacts.mkdir(parents=True, exist_ok=True)

    command = [str(args.binary),
               "-artifact_prefix=" + str(args.artifacts) + os.sep,
               "-rss_limit_mb=2048",
               "-max_len=1048576"]
    if args.runs > 0:
        command += ["-runs=" + str(args.runs)]
    else:
        command += ["-max_total_time=" + str(args.seconds)]
    command.append(str(args.corpus))
    result = subprocess.run(command, capture_output=True, text=True)
    print(result.stdout.splitlines()[-1] if result.stdout else "")
    if result.returncode != 0:
        print("FUZZ_JSON: FAIL — crash or timeout artifact")
        print(result.stderr[-2000:])
        return 1
    print("FUZZ_JSON: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
