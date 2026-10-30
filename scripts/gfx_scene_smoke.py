#!/usr/bin/env python3
#opencode: GFX scene boot smoke — every standalone scene flag must boot, render
#   a non-empty frame and log no ERROR!/FATAL! lines. The deep per-feature
#   gates cover the shared pipeline; this is the boot floor for the secondary
#   demo scenes (analog/clock/sky/teletext/textwriter/battle), with the gated
#   scenes riding along for uniform coverage. Quick tier. GPU display required
#   (DISPLAY, default :1).

import argparse
import re
import sys
import tempfile
from pathlib import Path

import audit_common as audit


SCENES = [
    ("analog", "--analog"),
    ("clock", "--clock"),
    ("sky", "--sky"),
    ("teletext", "--teletext"),
    ("textwriter", "--textwriter"),
    ("synthvis", "--synthvis"),
    ("water", "--water"),
    ("wireframe", "--wireframe"),
    ("raymarch", "--raymarch"),
    ("battle", "--battle"),
    ("physics", "--physics"),
]
FRAMES = 60
SHOT_FRAME = 50
TIMEOUT = 180
MIN_MEAN_LUMA = 0.5
MIN_PEAK_LUMA = 40.0
#ai: the textwriter scene is white text on a black canvas — legitimately dark
#   (mean ~0.15 with the first line revealed); the peak check plus a small
#   floor still catch a truly blank frame.
#opencode: the synthvis scene is a dark visualizer composite plus a bright
#   key bed (~0.12 mean idle; the scope fills only while notes sound) — the
#   peak check still catches a blank frame.
MIN_MEAN_OVERRIDES = {"textwriter": 0.05, "synthvis": 0.05}
ERROR_RE = re.compile(r"\[(?:ERROR!|FATAL!)\]")


def probe(flag, directory, min_mean=MIN_MEAN_LUMA):
    directory.mkdir(parents=True, exist_ok=True)
    environment = audit.scene_env(
        {"GX_SKIP_TESTS": "1", "GX_FIXED_DT": "0.0166666667"}, clean=True)
    result = audit.run_scene(flag, environment, width=None, height=None,
                             fps=None, frames=FRAMES, shot_frame=SHOT_FRAME,
                             screenshot_dir=directory, timeout=TIMEOUT,
                             check=False)
    log = audit.strip_ansi(result.stdout or "")
    error = ERROR_RE.search(log)
    if result.returncode != 0:
        return False, f"exit {result.returncode}"
    if error:
        line = log[error.start():].splitlines()[0][:88]
        return False, f"error log: {line}"
    shot = audit.newest_shot(directory)
    if shot is None:
        return False, "no screenshot"
    luma = audit.luma(audit.load_rgb(shot))
    mean = float(luma.mean())
    peak = float(luma.max())
    if mean < min_mean or peak < MIN_PEAK_LUMA:
        return False, f"blank frame (mean {mean:.2f}, peak {peak:.0f})"
    return True, f"mean {mean:.1f}, peak {peak:.0f}"


def main():
    parser = argparse.ArgumentParser(
        description="Boot every standalone scene and smoke its frame.")
    parser.add_argument("--only", metavar="NAMES",
                        help="comma-separated scene names")
    args = parser.parse_args()
    if not audit.BIN.is_file():
        print(f"GFX_SCENE_SMOKE: FAIL — missing binary: {audit.BIN}",
              file=sys.stderr)
        return 1
    names = {item.strip() for item in args.only.split(",") if item.strip()} \
        if args.only else set()
    unknown = sorted(names - {name for name, _ in SCENES})
    if unknown:
        parser.error("unknown scene(s): " + ", ".join(unknown))
    ran = 0
    failures = 0
    with tempfile.TemporaryDirectory(prefix="scene_smoke_", dir="/tmp/opencode") as temp:
        root = Path(temp)
        for name, flag in SCENES:
            if names and name not in names:
                continue
            ran += 1
            try:
                ok, note = probe(flag, root / name,
                                 min_mean=MIN_MEAN_OVERRIDES.get(name, MIN_MEAN_LUMA))
            except (OSError, RuntimeError, ValueError) as error:
                ok, note = False, str(error)
            print(f"{name:<12} {'PASS' if ok else 'FAIL'}  {note}", flush=True)
            if not ok:
                failures += 1
    print(f"\nGFX_SCENE_SMOKE: {'PASS' if not failures else 'FAIL'} "
          f"({ran - failures}/{ran} scenes)")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"GFX_SCENE_SMOKE: FAIL — {exc}", file=sys.stderr)
        sys.exit(1)
