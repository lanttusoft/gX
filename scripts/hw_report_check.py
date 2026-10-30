#!/usr/bin/env python3
#ai: hardware report gate — runs the engine with GX_HW_REPORT=1 and validates
#   the capability block (GL strings, level, feature flags, limits, tier),
#   cross-checks the C classifier against the same tier rules, and asserts the
#   headless classifier tests.

import os
import re
import sys

import audit_common as audit

VENDOR_RE = re.compile(
    r'HW_REPORT: vendor="([^"]*)" renderer="([^"]*)" version="([^"]*)" '
    r'glsl="([^"]*)"')
LEVEL_RE = re.compile(r"HW_REPORT: gl=(\d+)\.(\d+) es=(\d) shader=(\d+)")
FLAGS_RE = re.compile(r"HW_REPORT: flags (.+)")
FLAG_ITEM_RE = re.compile(r"(\w+)=(\d)")
LIMITS_RE = re.compile(
    r"HW_REPORT: limits texture_units=(\d+) texture_size=(\d+) "
    r"color_attachments=(\d+) uniform_components=(\d+)")
TIER_RE = re.compile(r'HW_REPORT: tier=(\S+) reason="([^"]*)"')
TIER_NAMES = ("below-minimum", "minimum", "standard", "recommended")


def expected_tier(gl_major, gl_minor, is_es, shader, flags):
    if flags.get("fbo", 0) != 1:
        return "below-minimum"
    if is_es:
        if shader < 300:
            return "below-minimum"
    elif shader < 330:
        return "below-minimum"
    if (not is_es and flags.get("compute", 0) and flags.get("ssbo", 0)
            and flags.get("bindless", 0) and flags.get("cubemaparray", 0)
            and flags.get("tess", 0)):
        return "recommended"
    if flags.get("compute", 0) and flags.get("ssbo", 0):
        return "standard"
    if not is_es and gl_major == 4 and gl_minor >= 2:
        return "standard"
    return "minimum"


def run_report(temp_root):
    directory = temp_root / "report"
    directory.mkdir(exist_ok=True)
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "5",
        "GX_HW_REPORT": "1",
    }, clean=True)
    result = audit.run_scene("--title", environment, width=None, height=None,
                             fps=None, frames=5, shot_frame=3,
                             screenshot_dir=directory, timeout=120,
                             check=True)
    return audit.strip_ansi(result.stdout or "")


def run_unit_tests():
    import subprocess
    result = subprocess.run([str(audit.BIN), "--test-only"], cwd=str(audit.ROOT),
                            text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=300)
    return audit.strip_ansi(result.stdout or "")


def main():
    if not audit.BIN.is_file():
        print("HW_REPORT_CHECK: missing binary", file=sys.stderr)
        return 1
    try:
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory(prefix="hw_report_",
                                         dir=str(audit.SCRATCH.parent)) as temp:
            log = run_report(Path(temp))
        vendor = VENDOR_RE.search(log)
        level = LEVEL_RE.search(log)
        flags_line = FLAGS_RE.search(log)
        limits = LIMITS_RE.search(log)
        tier = TIER_RE.search(log)
        problems = []
        if not vendor or not vendor.group(1) or not vendor.group(3):
            problems.append("vendor/renderer/version strings missing")
        if not level:
            problems.append("GL level line missing")
        if not flags_line:
            problems.append("feature flag line missing")
        if not limits:
            problems.append("limit line missing")
        if not tier:
            problems.append("tier line missing")
        if problems:
            raise RuntimeError("; ".join(problems))
        flags = {key: int(value) for key, value in
                 FLAG_ITEM_RE.findall(flags_line.group(1))}
        for key in ("compute", "ssbo", "bindless", "tess", "cubemaparray",
                    "fbo"):
            if key not in flags:
                raise RuntimeError(f"flag missing: {key}")
        gl_major, gl_minor, is_es, shader = (int(level.group(1)),
            int(level.group(2)), int(level.group(3)), int(level.group(4)))
        texture_units = int(limits.group(1))
        texture_size = int(limits.group(2))
        if not (8 <= texture_units <= 4096):
            raise RuntimeError(f"texture units out of range: {texture_units}")
        if not (1024 <= texture_size <= 65536):
            raise RuntimeError(f"texture size out of range: {texture_size}")
        reported = tier.group(1)
        if reported not in TIER_NAMES:
            raise RuntimeError(f"unknown tier: {reported}")
        expected = expected_tier(gl_major, gl_minor, is_es, shader, flags)
        if reported != expected:
            raise RuntimeError(
                f"tier {reported} != classifier expectation {expected}")
        if not tier.group(2):
            raise RuntimeError("tier reason is empty")
        unit_log = run_unit_tests()
        if "HW TESTS: all passed" not in unit_log:
            raise RuntimeError("hardware classifier unit tests did not pass")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"HW_REPORT_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"HW_REPORT_CHECK: PASS (renderer \"{vendor.group(2)}\", "
          f"gl={gl_major}.{gl_minor}, tier={reported}, "
          f"texture units {texture_units})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
