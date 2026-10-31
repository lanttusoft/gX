#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: opt-in uniform-type attribution gate. Runs scenes with
#   GX_UNIFORM_TRACE=1 and fails on any named UNIFORM_MISMATCH.

import sys
from pathlib import Path

import audit_common as audit


SCENES = ("--showcase", "--showcase-y2k")


def main():
    for scene in SCENES:
        environment = audit.scene_env({
            "GX_FRAMES": "4",
            "GX_UNIFORM_TRACE": "1",
        }, clean=True)
        result = audit.run_scene(scene, environment, width=1024, height=768,
                                 fps=None, timeout=120, check=False)
        output = audit.strip_ansi(result.stdout or "")
        if "UNIFORM_TRACE: enabled" not in output:
            print(f"UNIFORM_TRACE_CHECK: FAIL — trace did not run for {scene}",
                  file=sys.stderr)
            return 1
        mismatches = [line.strip() for line in output.splitlines()
                      if "UNIFORM_MISMATCH" in line]
        if mismatches:
            for line in mismatches:
                print(f"UNIFORM_TRACE_CHECK: {line}", file=sys.stderr)
            return 1
        if result.returncode != 0:
            print(f"UNIFORM_TRACE_CHECK: FAIL — {scene} exited "
                  f"{result.returncode}", file=sys.stderr)
            return 1
    print("UNIFORM_TRACE_CHECK: PASS — all sampled uploads match shader types")
    return 0


if __name__ == "__main__":
    sys.exit(main())
