#!/usr/bin/env python3
#opencode: GUI template capture matrix — paired baseline/forced-template
#   captures with explicit expected-blank rows and failing gate semantics.

import argparse
import os
import re
import shutil
import sys

import audit_common as audit


ENUM = audit.ROOT / "src" / "gui" / "enum.h"
DEFAULT_OUT = "/tmp/opencode/gui_audit"
EXPECTED_BLANK = {"SCENE"}
#ai: blank rows must stay near the baseline. The title scene owns an animated
#   palette backdrop now, so a capture phase offset leaves a small residual
#   (measured 0.02..0.16 against the 0.1 rendered threshold); a real template
#   render lands well above this ceiling (lowest observed rendered row: 1.59).
BLANK_DELTA_CEILING = 0.5
QUICK_TEMPLATES = {"ESCAPEMENU", "GUITESTER", "SETTINGS"}


def template_names():
    source = ENUM.read_text(encoding="utf-8")
    match = re.search(r"#define gX_GUI_TEMPLATE \\\n(.*?)\n\n", source, re.S)
    if not match:
        raise RuntimeError("could not find gX_GUI_TEMPLATE registry")
    return re.findall(r"X\(([A-Z0-9_]+)\)", match.group(1))


#opencode: ambient template hooks must not leak into a capture row; the headless
#   suite stays off so the capture measures rendering only.
def capture_env(extra=None):
    environment = audit.scene_env(clean=True)
    environment.pop("GX_GUI_TEMPLATE_FORCE", None)
    environment.pop("GX_GUI_SHOW_MENU", None)
    environment["GX_SKIP_TESTS"] = "1"
    #opencode: pin the frame clock — the title backdrop palette and the
    #   centerpiece animate from maindata.timeElapsed, which is wall-clock
    #   without this hook, so a loaded matrix could drift the phase enough to
    #   break the expected-blank SCENE row (observed 17/18, retried green).
    environment["GX_FIXED_DT"] = "0.0166666667"
    for key, value in (extra or {}).items():
        environment[key] = str(value)
    return environment


def parse_names(value, known, parser, option):
    names = {item.strip().upper() for item in value.split(",") if item.strip()}
    unknown = sorted(names - known)
    if unknown:
        parser.error(f"{option}: unknown template(s): {', '.join(unknown)}")
    return names


def main():
    parser = argparse.ArgumentParser(
        description="Capture and verify the registered gX GUI templates.")
    parser.add_argument("--output", default=DEFAULT_OUT,
                        help=f"capture directory (default: {DEFAULT_OUT})")
    parser.add_argument("--quick", action="store_true",
                        help="run the EscapeMenu/GuiTester/Settings smoke set")
    parser.add_argument("--only", metavar="NAMES",
                        help="comma-separated template names to include")
    parser.add_argument("--skip", metavar="NAMES",
                        help="comma-separated template names to omit")
    parser.add_argument("--threshold", type=float, default=0.1,
                        help="mean absolute RGB delta for a rendered template")
    parser.add_argument("--timeout", type=int, default=60,
                        help="seconds allowed for each capture")
    args = parser.parse_args()
    if args.threshold < 0.0:
        parser.error("--threshold must be non-negative")
    if args.timeout < 1:
        parser.error("--timeout must be positive")
    if not audit.BIN.is_file():
        print(f"GUI-CHECK: FAIL — missing binary: {audit.BIN}", file=sys.stderr)
        return 1

    try:
        templates = template_names()
    except (OSError, RuntimeError) as exc:
        print(f"GUI-CHECK: FAIL — {exc}", file=sys.stderr)
        return 1
    known = set(templates)
    only = parse_names(args.only, known, parser, "--only") if args.only else known
    skip = parse_names(args.skip, known, parser, "--skip") if args.skip else set()
    selected = only & known
    if args.quick:
        selected &= QUICK_TEMPLATES
    selected -= skip
    if not selected:
        parser.error("template selection is empty")

    out_dir = os.path.abspath(args.output)
    raw_dir = os.path.join(out_dir, "raw")
    os.makedirs(raw_dir, exist_ok=True)

    try:
        with audit.settings_preserved():
            pool = audit.CapturePool(jobs=4, raw_dir=raw_dir,
                                     timeout=args.timeout)
            requests = {"baseline": ("--title", capture_env())}
            for name in templates:
                if name in selected:
                    requests[name.lower()] = (
                        "--title",
                        capture_env({"GX_GUI_TEMPLATE_FORCE": name}))
            captures = pool.capture_all(requests, width=None, height=None,
                                        fps=None)
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"GUI-CHECK: FAIL — {exc}", file=sys.stderr)
        return 1

    baseline_path = captures.get("baseline")
    if baseline_path is None:
        print("BASELINE FAIL: capture unavailable", file=sys.stderr)
        print("GUI-CHECK: FAIL — baseline capture unavailable", file=sys.stderr)
        return 1
    baseline = audit.load_rgb(baseline_path)

    rows = []
    failures = 0
    for name in templates:
        if name not in selected:
            continue
        tag = name.lower()
        shot = captures.get(tag)
        destination = os.path.join(out_dir, f"{tag}.png")
        status = "CRASH/TIMEOUT"
        delta = None
        if shot is None:
            failures += 1
            print(f"{name:<20} FAIL — capture unavailable")
        else:
            shutil.copyfile(shot, destination)
            try:
                image = audit.load_rgb(shot)
                if image.shape != baseline.shape:
                    raise ValueError("capture size mismatch")
                delta = audit.mean_abs_diff(image, baseline)
            except (OSError, ValueError) as exc:
                status = "INVALID IMAGE"
                failures += 1
                print(f"{name:<20} FAIL — {exc}")
            else:
                rendered = delta >= args.threshold
                expected_rendered = name not in EXPECTED_BLANK
                if expected_rendered:
                    passed = rendered
                else:
                    passed = delta < BLANK_DELTA_CEILING
                if passed and rendered:
                    status = "PASS rendered"
                elif passed:
                    status = "PASS expected blank"
                else:
                    status = ("FAIL unexpected blank" if not rendered
                              else "FAIL unexpected render")
                    failures += 1
                print(f"{name:<20} {status:<24} delta={delta:.4f}")
        rows.append((name, status, delta))

    report_path = os.path.join(out_dir, "gui_matrix.md")
    with open(report_path, "w", encoding="utf-8") as handle:
        handle.write("# gX GUI Template Matrix\n\n")
        handle.write(f"Pixel threshold: mean absolute RGB delta >= {args.threshold}"
                     f" (blank rows < {BLANK_DELTA_CEILING}).\n\n")
        handle.write("| Template | Result | Mean RGB delta |\n|---|---|---:|\n")
        for name, status, delta in rows:
            value = "—" if delta is None else f"{delta:.4f}"
            handle.write(f"| {name} | {status} | {value} |\n")
        handle.write(f"\n{len(rows) - failures}/{len(rows)} rows passed.\n")
    #opencode: name the failing rows in the summary — the matrix retries a
    #   failed gate and overwrites this report, so the recorded first verdict
    #   must carry the diagnosis (this is how a one-off flake stays visible).
    failed_rows = [name for name, status, _ in rows
                   if status.startswith("FAIL")
                   or status in ("CRASH/TIMEOUT", "INVALID IMAGE")]
    diagnosis = f"; failures: {', '.join(failed_rows)}" if failed_rows else ""
    print(f"\nGUI-CHECK: {'PASS' if failures == 0 else 'FAIL'} "
          f"({len(rows) - failures}/{len(rows)} rows{diagnosis}; "
          f"report: {report_path})")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"GUI-CHECK: FAIL — {exc}", file=sys.stderr)
        sys.exit(1)
