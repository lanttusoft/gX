#!/usr/bin/env python3
#ai: 2026-10-21 devcube lab-module gate — 60 Hz phase + native overlay capture.
"""Verify the devcube module without making it part of the default startup.

The phase assertion is shared with --test-only (main_tests.c).  The visual
probe loads the module explicitly, captures a live --gfx frame at native
resolution, and checks that the cube/Lissajous/RGB palette is present.  It is
included in check-fast because the module is built by default; loading it is
not.
"""

import subprocess
import sys
import tempfile

import audit_common

ROOT = audit_common.ROOT
BIN = audit_common.BIN
MODULE = ROOT / "build" / "labs" / "gx_devcube.so"
AUTO_MODULE = ROOT / "modules" / "gx_devcube.so"
PHASE_MARKER = "DEVCUBE TESTS: 60Hz phase passed"
ENTRY_MARKER = "DEVCUBE: entry"
LOAD_MARKER = "MODULE: loaded"


def fail(message):
    print(f"DEVCUBE_CHECK: FAIL — {message}", file=sys.stderr)
    return 1


def component_counts(image):
    red = image[..., 0]
    green = image[..., 1]
    blue = image[..., 2]
    return {
        "green": int(((green > 170) & (green > red * 1.35)
                      & (green > blue * 1.35)).sum()),
        "pink": int(((red > 170) & (blue > 100)
                     & (red > green * 1.35) & (blue > green * 1.10)).sum()),
        "red": int(((red > 170) & (red > green * 1.60)
                    & (red > blue * 1.60)).sum()),
        "blue": int(((blue > 150) & (blue > red * 1.30)
                     & (blue > green * 1.10)).sum()),
    }


def main():
    if not BIN.is_file():
        return fail(f"missing {BIN}; run make first")
    if not MODULE.is_file():
        return fail(f"missing {MODULE}; run make first")
    if AUTO_MODULE.exists():
        return fail(f"{AUTO_MODULE} would auto-load; keep labs out of modules/")

    tests = subprocess.run(
        [str(BIN), "--test-only"],
        cwd=ROOT, env=audit_common.scene_env(),
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=60)
    log = audit_common.strip_ansi(tests.stdout)
    if tests.returncode != 0 or PHASE_MARKER not in log:
        return fail("60 Hz phase test did not pass "
                    f"(exit {tests.returncode})")

    with tempfile.TemporaryDirectory(
            prefix="gx-devcube-", dir=audit_common.SCRATCH) as shot_dir:
        result = audit_common.run_scene(
            "--gfx",
            screenshot_dir=shot_dir,
            frames=90,
            shot_frame=60,
            screenshot_after=True,
            timeout=120,
            extra_args=("--module", str(MODULE)))
        log = audit_common.strip_ansi(result.stdout)
        for marker in (ENTRY_MARKER, LOAD_MARKER):
            if marker not in log:
                return fail(f"module log missing {marker!r}")
        shot = audit_common.newest_shot(shot_dir)
        if shot is None:
            return fail("no capture produced")
        image = audit_common.load_rgb(shot)

    height, width = image.shape[:2]
    if (width, height) != (1024, 768):
        return fail(f"capture is {width}x{height}, expected 1024x768")

    counts = component_counts(image)
    minimum = {"green": 200, "pink": 80, "red": 40, "blue": 40}
    missing = [name for name, count in counts.items()
               if count < minimum[name]]
    if missing:
        return fail("missing overlay palette: " + ", ".join(
            f"{name}={counts[name]}<{minimum[name]}" for name in missing))

    print("DEVCUBE_CHECK: PASS — cube={green} lissajous={pink} "
          "red={red} blue={blue} at {width}x{height}".format(
              width=width, height=height, **counts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
