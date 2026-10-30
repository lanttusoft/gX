#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: GUI layout pass — every template's text must fit its element box.
#   The GX_GUI_TRACE element map carries the draw-truth verdict (wraps,
#   truncated, overflow, shrink scale) computed by the engine's prepared text
#   path, so this gate fails on labels that clip, ellipsize, or spill unless
#   the finding is explicitly allowlisted as an intentional oversized demo.
#
# Usage:
#   DISPLAY=:1 python3 scripts/gui_layout_check.py
#   DISPLAY=:1 python3 scripts/gui_layout_check.py --quick
#   DISPLAY=:1 python3 scripts/gui_layout_check.py --only SETTINGS GUITESTER

import argparse
import os
import re
import sys

import audit_common as audit


ENUM = audit.ROOT / "src" / "gui" / "enum.h"
DEFAULT_OUT = "/tmp/opencode/gui_layout"
QUICK_TEMPLATES = {"ESCAPEMENU", "GUITESTER", "SETTINGS"}
ELEMENT_RE = re.compile(
    r'GUITRACE: element canvas=(\d+) element=(-?\d+) name="([^"]*)" text="([^"]*)" '
    r'rect=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+),(-?[\d.]+) visible=(\d+) disabled=(\d+) '
    r'clickable=(\d+) focusable=(\d+) textfit=(-?[\d.]+),(-?[\d.]+) wraps=(\d+) '
    r'truncated=(\d+) overflow=(\d+) shrink=([\d.]+)')
#opencode: intentional deviations — a finding here is a documented design
#   choice, not a defect. Keep the rationale on every entry.
INTENTIONAL = {
    ("GUITESTER", "Bitmap 96px"):
        "3x font-scale demo: the label intentionally exceeds its demo panel",
    ("GUITESTER", "SDF 96px"):
        "3x font-scale demo: the label intentionally exceeds its demo panel",
}


def template_names():
    source = ENUM.read_text(encoding="utf-8")
    match = re.search(r"#define gX_GUI_TEMPLATE \\\n(.*?)\n\n", source, re.S)
    if not match:
        raise RuntimeError("could not find gX_GUI_TEMPLATE registry")
    return re.findall(r"X\(([A-Z0-9_]+)\)", match.group(1))


def parse_names(value, known, parser, option):
    names = {item.strip().upper() for item in value.split(",") if item.strip()}
    unknown = sorted(names - known)
    if unknown:
        parser.error(f"{option}: unknown template(s): {', '.join(unknown)}")
    return names


def run_template(name, timeout):
    environment = audit.scene_env({
        "GX_SKIP_TESTS": "1",
        "GX_FIXED_DT": "0.0166666667",
        "GX_GUI_TRACE": "1",
        "GX_GUI_TEMPLATE_FORCE": name,
    }, clean=True)
    result = audit.run_scene("--title", environment, width=None, height=None,
                             fps=None, frames=60, timeout=timeout, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"{name} run exited {result.returncode}")
    return audit.strip_ansi(result.stdout or "")


def findings(log, template):
    """text-fit problems, deduped by (canvas, element, text)."""
    problems = []
    allowed = []
    shrunk = 0
    seen = set()
    for match in ELEMENT_RE.finditer(log):
        if not match.group(4):
            continue
        truncated = match.group(16) == "1"
        overflow = match.group(17) == "1"
        if float(match.group(18)) < 1.0:
            shrunk += 1
        if not (truncated or overflow):
            continue
        key = (match.group(1), match.group(2), match.group(4))
        if key in seen:
            continue
        seen.add(key)
        record = {
            "canvas": int(match.group(1)),
            "element": int(match.group(2)),
            "text": match.group(4),
            "rect": (float(match.group(5)), float(match.group(6)),
                     float(match.group(7)), float(match.group(8))),
            "measure": (float(match.group(13)), float(match.group(14))),
            "truncated": truncated,
            "overflow": overflow,
            "shrink": float(match.group(18)),
        }
        if (template, record["text"]) in INTENTIONAL:
            allowed.append(record)
        else:
            problems.append(record)
    return problems, allowed, shrunk


def main():
    parser = argparse.ArgumentParser(
        description="Fail when a template's text does not fit its element box.")
    parser.add_argument("--output", default=DEFAULT_OUT,
                        help=f"report directory (default: {DEFAULT_OUT})")
    parser.add_argument("--quick", action="store_true",
                        help="run the EscapeMenu/GuiTester/Settings smoke set")
    parser.add_argument("--only", metavar="NAMES",
                        help="comma-separated template names")
    parser.add_argument("--skip", metavar="NAMES",
                        help="comma-separated template names to omit")
    parser.add_argument("--timeout", type=int, default=90,
                        help="seconds allowed for each run")
    args = parser.parse_args()
    if args.timeout < 1:
        parser.error("--timeout must be positive")
    if not audit.BIN.is_file():
        print(f"GUI-LAYOUT: FAIL — missing binary: {audit.BIN}", file=sys.stderr)
        return 1

    try:
        templates = template_names()
    except (OSError, RuntimeError) as exc:
        print(f"GUI-LAYOUT: FAIL — {exc}", file=sys.stderr)
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
    os.makedirs(out_dir, exist_ok=True)
    rows = []
    failures = 0
    total_problems = 0
    total_allowed = 0
    try:
        for name in templates:
            if name not in selected:
                continue
            log = run_template(name, args.timeout)
            problems, allowed, shrunk = findings(log, name)
            total_problems += len(problems)
            total_allowed += len(allowed)
            status = "PASS" if not problems else "FAIL"
            if problems:
                failures += 1
            print(f"{name:<20} {status}  text-fit problems={len(problems)} "
                  f"intentional={len(allowed)} shrink-guarded={shrunk}",
                  flush=True)
            for record in problems:
                print(f"    canvas={record['canvas']} text=\"{record['text'][:44]}\" "
                      f"rect=({record['rect'][0]:.0f},{record['rect'][1]:.0f},"
                      f"{record['rect'][2]:.0f},{record['rect'][3]:.0f}) "
                      f"measure=({record['measure'][0]:.0f},{record['measure'][1]:.0f}) "
                      f"truncated={int(record['truncated'])} "
                      f"overflow={int(record['overflow'])}",
                      flush=True)
            rows.append((name, status, problems, allowed, shrunk))
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"GUI-LAYOUT: FAIL — {exc}", file=sys.stderr)
        return 1

    report = os.path.join(out_dir, "gui_layout.md")
    with open(report, "w", encoding="utf-8") as handle:
        handle.write("# gX GUI Layout Pass\n\n")
        handle.write("Text-fit verdict per template (engine draw-truth bounds).\n\n")
        handle.write("| Template | Result | Problems | Intentional | Shrink-guarded |\n")
        handle.write("|---|---|---:|---:|---:|\n")
        for name, status, problems, allowed, shrunk in rows:
            handle.write(f"| {name} | {status} | {len(problems)} | "
                         f"{len(allowed)} | {shrunk} |\n")
        handle.write("\n")
        for name, status, problems, allowed, shrunk in rows:
            for record in problems:
                handle.write(f"- {name}: \"{record['text']}\" at "
                             f"({record['rect'][0]:.0f},{record['rect'][1]:.0f},"
                             f"{record['rect'][2]:.0f},{record['rect'][3]:.0f}) "
                             f"measures ({record['measure'][0]:.0f},"
                             f"{record['measure'][1]:.0f})\n")
        handle.write(f"\n{len(rows) - failures}/{len(rows)} rows passed.\n")
    print(f"\nGUI-LAYOUT: {'PASS' if not failures else 'FAIL'} "
          f"({len(rows) - failures}/{len(rows)} rows; {total_problems} problems, "
          f"{total_allowed} intentional; report: {report})")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"GUI-LAYOUT: FAIL — {exc}", file=sys.stderr)
        sys.exit(1)
