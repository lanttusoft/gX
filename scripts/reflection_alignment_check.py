#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: reflection alignment gate — the CPU planar mirror oracle plus the Y2K
#   single-owner contract. Numeric, no eyeballing; the full reflection suite
#   and visual review remain separate.

import os
import subprocess
import sys
import tempfile

import audit_common as audit


def check_ownership(log):
    errors = []
    if "SHOWCASE-Y2K: mirror planar=1 receiveSSR=0" not in log:
        errors.append("Y2K mirror is not marked planar=1/receiveSSR=0")
    if "GFX_PLAN feature=planar-reflections" not in log:
        errors.append("Y2K planar-reflection feature is missing from the plan")
    if "GFX_PLAN feature=ssr" in log:
        errors.append("Y2K still enables SSR (double-reflection risk)")
    return errors


def run_planar_oracle():
    with tempfile.TemporaryDirectory(prefix="reflection_alignment_",
            dir=str(audit.SCRATCH)) as out:
        environment = dict(os.environ)
        environment["GX_ORACLE_TAA"] = "0"
        result = subprocess.run(
            [sys.executable, str(audit.ROOT / "scripts" / "oracle_planar.py"),
             out],
            cwd=audit.ROOT, env=environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
        return result.returncode, audit.strip_ansi(result.stdout or "")


def main():
    if not audit.BIN.is_file():
        print("REFLECTION_ALIGNMENT: missing binary", file=sys.stderr)
        return 1
    try:
        oracle_code, oracle_log = run_planar_oracle()
        if oracle_code != 0 or "ORACLE GATE: PASS" not in oracle_log:
            tail = "\n".join(oracle_log.splitlines()[-12:])
            print("REFLECTION_ALIGNMENT: FAIL — planar oracle\n" + tail,
                file=sys.stderr)
            return 1

        environment = {
            "GX_GFX_PLAN": "1",
            "GX_T_TAA": "0",
            "GX_CONSOLE_SSAA": "0",
            "GX_CONSOLE_PRISM": "0",
            "GX_CONSOLE_GRAIN": "0",
        }
        result = audit.run_scene("--cartridge-y2k", environment,
            width=320, height=240, frames=30, timeout=120, check=False)
        log = audit.strip_ansi(result.stdout or "")
        if result.returncode != 0:
            print(f"REFLECTION_ALIGNMENT: FAIL — Y2K exit {result.returncode}",
                file=sys.stderr)
            return 1
        errors = check_ownership(log)
        if errors:
            print("REFLECTION_ALIGNMENT: FAIL — " + "; ".join(errors),
                file=sys.stderr)
            return 1
    except (OSError, subprocess.SubprocessError, RuntimeError) as error:
        print(f"REFLECTION_ALIGNMENT: FAIL — {error}", file=sys.stderr)
        return 1
    print("REFLECTION_ALIGNMENT: PASS planar oracle (CPU mirror/UV chain) "
        "and Y2K planar=1/receiveSSR=0, SSR-free plan")
    return 0


if __name__ == "__main__":
    sys.exit(main())
