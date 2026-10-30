#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: normalized resolution sweep over gX --layout-dump JSON. Compares element
#   presence, normalized rects and foreground-overlap growth across sizes;
#   findings are diagnostic, not a pixel gate. No AI probe or engine changes.
"""Sweep layout dumps across resolutions and compare normalized layouts.

The baseline is the first --sizes entry. Element identity is the canvas name
plus element name and index from the dump. Rectangles are normalized by the
dump's actual screen size (x/W, y/H, w/W, h/H), so pure scaling does not
produce shifts. Foreground overlap area is normalized by the screen area and
pair ratio by the smaller element; full-screen background overlaps are
excluded from growth comparisons.

Examples:
    python3 scripts/layout_sweep.py --scenes title,themlab \
        --sizes 800x600,1280x720 --tol 0.02
    python3 scripts/layout_sweep.py --format json --out /tmp/opencode/sweep.json
"""

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import layout_report


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCENES = ("title", "themlab")
DEFAULT_SIZES = ("800x600", "1024x768", "1280x720", "1920x1080")
DEFAULT_FRAME = 8
DEFAULT_TOL = 0.02
SIZE_RE = re.compile(r"^(\d+)[xX](\d+)$")
NOTES = (
    "Findings are diagnostic; shifts and overlap growth do not fail the sweep.",
    "Rects are normalized by the dump's actual screen size; the first size is the baseline.",
    "Element identity is canvas:name#index from the dump; a reorder is reported as missing/added.",
    "Overlap growth uses foreground pairs only; full-screen backgrounds are excluded.",
    "Screen-size mismatches are reported because a clamped window invalidates normalization.",
)


def parse_sizes(values=None):
    raw = values or DEFAULT_SIZES
    sizes = []
    for value in raw:
        for part in str(value).replace(" ", "").split(","):
            if not part:
                continue
            match = SIZE_RE.match(part)
            if not match:
                raise ValueError(f"invalid size '{part}' (use WxH)")
            width, height = int(match.group(1)), int(match.group(2))
            if width < 1 or height < 1:
                raise ValueError(f"invalid size '{part}'")
            if (width, height) not in sizes:
                sizes.append((width, height))
    if len(sizes) < 2:
        raise ValueError("at least two distinct sizes are required")
    return sizes


def select_scenes(values=None):
    return (layout_report.select_scenes(values) if values
            else list(DEFAULT_SCENES))


def size_label(size):
    return f"{size[0]}x{size[1]}"


def flatten(analysis):
    result = {}
    for canvas in analysis["canvases"]:
        for element in canvas["elements"]:
            key = (f"{canvas['name']}:{element['name']}#{element['index']}")
            result[key] = element
    return result


def normalize(element, analysis):
    width = float(analysis["screenW"])
    height = float(analysis["screenH"])
    return [round(element[key] / width, 6)
            if key in ("x", "w") else round(element[key] / height, 6)
            for key in ("x", "y", "w", "h")]


def overlap_map(analysis):
    result = {}
    for overlap in analysis["foreground_overlaps"]:
        key = (overlap["canvas"], overlap["first"], overlap["second"])
        result[key] = overlap
    return result


def screen_mismatch(width, height, analysis):
    actual_width = int(analysis["screenW"])
    actual_height = int(analysis["screenH"])
    if actual_width == width and actual_height == height:
        return None
    return {
        "size": size_label((width, height)),
        "requested": [width, height],
        "actual": [actual_width, actual_height],
    }


def compare_dumps(scene, dumps, tolerance):
    baseline_label, baseline = dumps[0]
    baseline_elements = flatten(baseline)
    baseline_overlaps = overlap_map(baseline)
    result = {
        "scene": scene,
        "baseline": baseline_label,
        "sizes": [label for label, _ in dumps],
        "missing": [],
        "added": [],
        "shifts": [],
        "overlap_growth": [],
    }
    for label, analysis in dumps[1:]:
        elements = flatten(analysis)
        for key in sorted(set(baseline_elements) - set(elements)):
            result["missing"].append({
                "size": label,
                "element": key,
                "type": baseline_elements[key].get("type", "UNKNOWN"),
            })
        for key in sorted(set(elements) - set(baseline_elements)):
            result["added"].append({
                "size": label,
                "element": key,
                "type": elements[key].get("type", "UNKNOWN"),
            })
        for key in sorted(set(baseline_elements) & set(elements)):
            before = normalize(baseline_elements[key], baseline)
            after = normalize(elements[key], analysis)
            delta = [round(after[index] - before[index], 6)
                     for index in range(4)]
            if any(abs(value) > tolerance for value in delta):
                result["shifts"].append({
                    "size": label,
                    "element": key,
                    "before": before,
                    "after": after,
                    "delta": delta,
                    "kind": ("moved" if any(abs(delta[index]) > tolerance
                                            for index in (0, 1))
                             else "resized"),
                })

        current_overlaps = overlap_map(analysis)
        for key in sorted(set(baseline_overlaps) & set(current_overlaps)):
            before = baseline_overlaps[key]
            after = current_overlaps[key]
            before_area = before["intersection"] / (
                float(baseline["screenW"]) * float(baseline["screenH"]))
            after_area = after["intersection"] / (
                float(analysis["screenW"]) * float(analysis["screenH"]))
            delta_area = round(after_area - before_area, 6)
            delta_ratio = round(after["ratio"] - before["ratio"], 6)
            if (abs(delta_area) > tolerance
                    or abs(delta_ratio) > tolerance):
                result["overlap_growth"].append({
                    "size": label,
                    "canvas": key[0],
                    "first": key[1],
                    "second": key[2],
                    "before": {
                        "area": round(before_area, 6),
                        "ratio": before["ratio"],
                    },
                    "after": {
                        "area": round(after_area, 6),
                        "ratio": after["ratio"],
                    },
                    "delta_area": delta_area,
                    "delta_ratio": delta_ratio,
                })
    return result


def build_report(scenes, sizes, tolerance, frame, timeout):
    scene_reports = []
    with tempfile.TemporaryDirectory(prefix="gx-layout-sweep-") as directory:
        for scene in scenes:
            dumps = []
            mismatches = []
            dump_rows = []
            for width, height in sizes:
                label = size_label((width, height))
                data = layout_report.capture_scene(
                    scene, directory, frame, width, height, timeout=timeout)
                analysis = layout_report.analyze_dump(data, scene)
                dumps.append((label, analysis))
                mismatch = screen_mismatch(width, height, analysis)
                if mismatch:
                    mismatches.append(mismatch)
                dump_rows.append({
                    "size": label,
                    "screenW": analysis["screenW"],
                    "screenH": analysis["screenH"],
                    "canvas_count": analysis["canvas_count"],
                    "element_count": analysis["element_count"],
                    "offscreen": len(analysis["offscreen"]),
                })
            compared = compare_dumps(scene, dumps, tolerance)
            compared["dumps"] = dump_rows
            compared["screen_mismatch"] = mismatches
            scene_reports.append(compared)
    return {
        "frame": frame,
        "tolerance": tolerance,
        "sizes": [size_label(size) for size in sizes],
        "scenes": scene_reports,
        "notes": list(NOTES),
        "totals": {
            "scenes": len(scene_reports),
            "dumps": sum(len(scene["dumps"]) for scene in scene_reports),
            "missing": sum(len(scene["missing"]) for scene in scene_reports),
            "added": sum(len(scene["added"]) for scene in scene_reports),
            "shifts": sum(len(scene["shifts"]) for scene in scene_reports),
            "overlap_growth": sum(
                len(scene["overlap_growth"]) for scene in scene_reports),
            "screen_mismatch": sum(
                len(scene["screen_mismatch"]) for scene in scene_reports),
        },
    }


def _md_cell(value):
    return str(value).replace("|", "\\|")


def _rect(values):
    return f"{values[0]:.3f},{values[1]:.3f} {values[2]:.3f}x{values[3]:.3f}"


def render_markdown(report):
    totals = report["totals"]
    lines = [
        "# Layout resolution sweep",
        "",
        f"{totals['scenes']} scenes x {len(report['sizes'])} sizes at frame "
        f"{report['frame']}; baseline `{report['sizes'][0]}`; normalized "
        f"tolerance {report['tolerance']:g}.",
        "",
        "Findings are diagnostic, not pass/fail assertions.",
        "",
        "| Scene | Baseline | Sizes | Missing | Added | Shifts | "
        "Overlap growth | Screen mismatch |",
        "|---|---|---|---:|---:|---:|---:|---:|",
    ]
    for scene in report["scenes"]:
        lines.append(
            f"| {_md_cell(scene['scene'])} | {_md_cell(scene['baseline'])} | "
            f"{', '.join(_md_cell(size) for size in scene['sizes'])} | "
            f"{len(scene['missing'])} | {len(scene['added'])} | "
            f"{len(scene['shifts'])} | {len(scene['overlap_growth'])} | "
            f"{len(scene['screen_mismatch'])} |")
    lines.append("")
    for scene in report["scenes"]:
        lines.extend([f"## {scene['scene']}", ""])
        lines.append("Dumps: " + ", ".join(
            f"`{_md_cell(item['size'])}` ({item['canvas_count']} canvases, "
            f"{item['element_count']} elements, {item['offscreen']} off-screen)"
            for item in scene["dumps"]))
        lines.append("")
        lines.append(f"### Screen mismatch ({len(scene['screen_mismatch'])})")
        lines.append("")
        if scene["screen_mismatch"]:
            lines.extend([
                "| Size | Requested | Actual |",
                "|---|---|---|",
            ])
            for item in scene["screen_mismatch"]:
                lines.append(
                    f"| {_md_cell(item['size'])} | "
                    f"{item['requested'][0]}x{item['requested'][1]} | "
                    f"{item['actual'][0]}x{item['actual'][1]} |")
        else:
            lines.append("None.")
        for heading, key in (("Missing elements", "missing"),
                             ("Added elements", "added")):
            items = scene[key]
            lines.extend(["", f"### {heading} ({len(items)})", ""])
            if items:
                lines.extend(["| Size | Element | Type |", "|---|---|---|"])
                for item in items:
                    lines.append(
                        f"| {_md_cell(item['size'])} | "
                        f"{_md_cell(item['element'])} | "
                        f"{_md_cell(item['type'])} |")
            else:
                lines.append("None.")
        lines.extend([
            "", f"### Shifts ({len(scene['shifts'])})", "",
        ])
        if scene["shifts"]:
            lines.extend([
                "| Size | Element | Kind | Before | After | Delta |",
                "|---|---|---|---|---|---|",
            ])
            for item in scene["shifts"]:
                lines.append(
                    f"| {_md_cell(item['size'])} | {_md_cell(item['element'])} "
                    f"| {item['kind']} | {_rect(item['before'])} | "
                    f"{_rect(item['after'])} | "
                    f"{_rect(item['delta'])} |")
        else:
            lines.append("None.")
        lines.extend([
            "", f"### Overlap growth ({len(scene['overlap_growth'])})", "",
        ])
        if scene["overlap_growth"]:
            lines.extend([
                "| Size | Canvas | First | Second | Area before→after | "
                "Ratio before→after |",
                "|---|---|---|---|---:|---:|",
            ])
            for item in scene["overlap_growth"]:
                lines.append(
                    f"| {_md_cell(item['size'])} | {_md_cell(item['canvas'])} "
                    f"| {_md_cell(item['first'])} | {_md_cell(item['second'])} "
                    f"| {item['before']['area']:.4f}→{item['after']['area']:.4f} "
                    f"| {item['before']['ratio']:.2f}→{item['after']['ratio']:.2f} |")
        else:
            lines.append("None.")
        lines.append("")
    lines.extend(["## Notes", ""])
    lines.extend(f"- {note}" for note in report["notes"])
    return "\n".join(lines).rstrip() + "\n"


def render_json(report):
    return json.dumps(report, indent=2, sort_keys=True) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenes", action="append", metavar="SCENE",
                        help="comma-separated scene names (repeatable)")
    parser.add_argument("--sizes", action="append", metavar="WxH",
                        help="comma-separated sizes (repeatable; first is baseline)")
    parser.add_argument("--tol", type=float, default=DEFAULT_TOL,
                        help="normalized tolerance (default 0.02)")
    parser.add_argument("--out", type=Path, help="write the report to this path")
    parser.add_argument("--format", choices=("markdown", "json"),
                        default="markdown")
    parser.add_argument("--frame", type=int, default=DEFAULT_FRAME)
    parser.add_argument("--timeout", type=int, default=180,
                        help="per-scene timeout in seconds")
    args = parser.parse_args()
    if args.tol < 0 or args.tol > 1:
        parser.error("--tol must be between 0 and 1")
    if args.frame < 1:
        parser.error("--frame must be >= 1")
    try:
        scenes = select_scenes(args.scenes)
        sizes = parse_sizes(args.sizes)
        report = build_report(scenes, sizes, args.tol, args.frame,
                              args.timeout)
    except (RuntimeError, ValueError, OSError,
            subprocess.TimeoutExpired) as error:
        print(f"LAYOUT_SWEEP: FAIL — {error}", file=sys.stderr)
        return 1

    output = (render_json(report) if args.format == "json"
              else render_markdown(report))
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output, encoding="utf-8")
        print(f"LAYOUT_SWEEP: {len(scenes)} scenes x {len(sizes)} sizes "
              f"-> {args.out}", file=sys.stderr)
    else:
        sys.stdout.write(output)
    totals = report["totals"]
    print(f"LAYOUT_SWEEP: {totals['dumps']} dumps, "
          f"{totals['missing']} missing, {totals['added']} added, "
          f"{totals['shifts']} shifts, {totals['overlap_growth']} overlap "
          f"growth, {totals['screen_mismatch']} screen mismatches",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
