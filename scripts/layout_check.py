#!/usr/bin/env python3
# gX layout check — auto-seat verification + scene capture triptych.
#   1. Runs --layout-check (in-engine CPU verdict: before>0 / after==0).
#   2. Captures --showcase (default orbit, top-down, below-slab) and
#      --layout-test (fixed) for eyeball/archival analysis.
#   Needs a display (run under Xvfb or with DISPLAY set, like reflect-check).
#   Exit 0 iff the CPU verdict is PASS; captures/analysis never fail the gate.

import re
import shutil
import sys
from pathlib import Path

import audit_common as audit


OUT = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/opencode/layout")
CAPTURE_DIR = OUT / "raw"
TIMEOUT = 150


def capture(name, args, env):
    #opencode: one scene run into the shared raw dir; the newest shot is the
    #   capture for this row.
    audit.run_scene(args[0], env, width=None, height=None, fps=None,
                    screenshot_dir=CAPTURE_DIR, shot_frame=200,
                    screenshot_after=True, timeout=TIMEOUT, check=False,
                    extra_args=args[1:])
    shot = audit.newest_shot(CAPTURE_DIR)
    if shot is None:
        print(f"   {name}: NO SHOT")
        return None
    destination = OUT / f"{name}.png"
    shutil.copyfile(shot, destination)
    print(f"   {name}.png")
    return destination


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    CAPTURE_DIR.mkdir(parents=True, exist_ok=True)
    failures = []

    print("== [layout] --layout-check (in-engine CPU verdict) ==")
    with audit.settings_preserved():
        verdict_run = audit.run_scene("--layout-check", width=None, height=None,
                                      fps=None, timeout=TIMEOUT, check=False)
        log = audit.strip_ansi(verdict_run.stdout or "")
        match = re.search(
            r"LAYOUT_CHECK: before=(\d+) moves=(\d+) after=(\d+) verdict=(\w+)",
            log)
        if match:
            print(f"   before={match.group(1)} moves={match.group(2)} "
                  f"after={match.group(3)} verdict={match.group(4)}")
            if match.group(4) != "PASS":
                failures.append("CPU verdict FAIL")
        else:
            failures.append("no LAYOUT_CHECK line in log")
            print("   LAYOUT_CHECK line missing; log tail:")
            print("   " + "\n   ".join(log.strip().splitlines()[-8:]))

        print("== [layout] captures ==")
        shots = [
            ("showcase_def", ["--showcase"], {}),
            ("showcase_top", ["--showcase"],
             {"GX_CAM_PITCH": "65", "GX_CAM_YAW": "30", "GX_CAM_DIST": "9"}),
            ("showcase_under", ["--showcase"], {"GX_CAM_PITCH": "-20"}),
            ("layouttest_fixed", ["--layout-test"], {}),
        ]
        for name, args, env in shots:
            capture(name, args, env)

    print("== [layout] analysis ==")
    for name, _, _ in shots:
        path = OUT / f"{name}.png"
        if not path.is_file():
            continue
        image = audit.load_rgb(path)
        print(f"   {name}: {image.shape[1]}x{image.shape[0]} "
              f"mean={image.mean():.1f} "
              f"blown={audit.blown_fraction(image) * 100:.2f}%")

    if failures:
        print("LAYOUT GATE: FAIL — " + "; ".join(failures))
        return 1
    print("LAYOUT GATE: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
