#!/usr/bin/env python3
#opencode: GUI theme/a11y gate — every shipped theme must load through the
#   GX_GUI_THEME audit hook, keep its WCAG 2.2 AA On- pairs, and render.
#   The high-contrast theme is additionally proven to change pixels against
#   the default (a loaded-but-unapplied theme is the failure this catches).
#   GPU display required (DISPLAY, default :1); separate from headless check.

import argparse
import re
import sys
import tempfile
from pathlib import Path

import audit_common as audit


TIMEOUT = 120
THEMES = [
    ("default", "resources/style/theme.json"),
    ("high-contrast", "resources/style/theme_high_contrast.json"),
    ("color-vision", "resources/style/theme_color_vision.json"),
    ("blueprint", "resources/style/theme_blueprint.json"),
    ("aero", "resources/style/theme_aero.json"),
]
LOADED_RE = re.compile(r"Style: theme loaded from '([^']*)'")
CONTRAST_FAIL_RE = re.compile(r"a11y pair '(?P<pair>[^']*)' contrast "
                              r"(?P<ratio>[\d.]+):1 < 4\.5:1")
CAPTURE_DELTA_MIN = 2.0


def capture_theme(path, out_dir, shot_frame=60):
    environment = audit.scene_env({
        "GX_SKIP_TESTS": "1",
        "GX_GUI_THEME": path,
    }, clean=True)
    out_dir.mkdir(parents=True, exist_ok=True)
    #opencode: frame count stays implicit — the engine exits after the
    #   requested screenshot frame (GX_FRAMES would cut the capture short).
    result = audit.run_scene("--title", environment, width=None, height=None,
                             fps=None, frames=None, shot_frame=shot_frame,
                             screenshot_dir=out_dir, timeout=TIMEOUT,
                             check=False)
    log = audit.strip_ansi(result.stdout or "")
    shot = audit.newest_shot(out_dir)
    return result.returncode, log, shot


def main():
    parser = argparse.ArgumentParser(
        description="Validate shipped GUI themes (load, contrast, pixels).")
    parser.add_argument("--only", metavar="NAMES",
                        help="comma-separated theme names")
    args = parser.parse_args()
    if not audit.BIN.is_file():
        print(f"GUI_THEME_CHECK: FAIL — missing binary: {audit.BIN}",
              file=sys.stderr)
        return 1
    known = {name for name, _ in THEMES}
    selected = {item.strip() for item in args.only.split(",") if item.strip()} \
        if args.only else known
    unknown = sorted(selected - known)
    if unknown:
        parser.error("unknown theme(s): " + ", ".join(unknown))

    failures = 0
    images = {}
    with tempfile.TemporaryDirectory(prefix="gui_theme_check_",
                                     dir="/tmp/opencode") as temp:
        root = Path(temp)
        try:
            with audit.settings_preserved():
                for name, path in THEMES:
                    if name not in selected:
                        continue
                    code, log, shot = capture_theme(path, root / name)
                    problems = []
                    if code != 0:
                        problems.append(f"exit {code}")
                    loaded = LOADED_RE.findall(log)
                    if not loaded or loaded[-1] != path:
                        problems.append(
                            f"theme path not applied (loaded={loaded[-1:] or 'none'})")
                    contrast = CONTRAST_FAIL_RE.search(log)
                    if contrast:
                        problems.append(
                            f"contrast {contrast.group('ratio')}:1 on "
                            f"'{contrast.group('pair')}'")
                    if shot is None:
                        problems.append("capture missing")
                    else:
                        image = audit.load_rgb(shot)
                        images[name] = image
                        if float(image.mean()) < 5.0 or float(image.std()) < 5.0:
                            problems.append("capture blank")
                    if problems:
                        failures += 1
                        print(f"{name:<15} FAIL  {'; '.join(problems)}")
                    else:
                        red, green, blue = image.mean(axis=(0, 1))
                        print(f"{name:<15} PASS  loaded, contrast AA, "
                              f"mean=({red:.0f},{green:.0f},{blue:.0f})")
        except (OSError, RuntimeError, ValueError) as error:
            print(f"GUI_THEME_CHECK: FAIL — {error}", file=sys.stderr)
            return 1

    if "default" in images and "high-contrast" in images:
        default = images["default"]
        contrast = images["high-contrast"]
        delta = audit.mean_abs_diff(default, contrast)
        if delta < CAPTURE_DELTA_MIN:
            failures += 1
            print(f"{'applied':<15} FAIL  high-contrast delta {delta:.2f} "
                  f"< {CAPTURE_DELTA_MIN}")
        else:
            print(f"{'applied':<15} PASS  high-contrast changes pixels "
                  f"(delta {delta:.2f})")

    print(f"\nGUI_THEME_CHECK: {'PASS' if not failures else 'FAIL'}")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"GUI_THEME_CHECK: FAIL — {exc}", file=sys.stderr)
        sys.exit(1)
