#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: headless layout report over gX --layout-dump JSON. Per-scene canvas and
#   element counts, off-screen rects, same-canvas overlaps and text elements;
#   findings are diagnostic, not failures. No AI probe or engine changes.
"""Report structured layout dumps for the gX scene matrix.

Each scene is launched at one fixed resolution/frame with --layout-dump. The
dump schema is {"screenW","screenH","canvases":[{"name","elements":[
{"name","type","text","x","y","w","h"}]}]}. Overlaps are reported with
context (full-screen/containment) instead of being treated as defects.

Examples:
    python3 scripts/layout_report.py --only title,themlab
    python3 scripts/layout_report.py --format json --out /tmp/opencode/layout.json
"""

import argparse
import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCENES = (
    "title", "gfx", "battle", "showcase", "showcase-y2k", "themlab",
)
SCENE_FLAGS = {
    "title": "--title",
    "gfx": "--gfx",
    "battle": "--battle",
    "showcase": "--showcase",
    "showcase-y2k": "--showcase-y2k",
    "themlab": "--themlab",
}
DEFAULT_WIDTH = 1024
DEFAULT_HEIGHT = 768
DEFAULT_FRAME = 8
OFFSCREEN_EPS = 0.5
OVERLAP_EPS = 0.5
NOTES = (
    "Findings are diagnostic; off-screen rects and overlaps do not fail the report.",
    "The dump's text field is a boolean (element has text), not the string content.",
    "Full-screen backgrounds and containment overlaps are context, not defects.",
    "Renderer-only scenes can legitimately produce zero GUI canvases.",
)


def scene_names():
    return list(DEFAULT_SCENES)


def select_scenes(selected=None):
    if not selected:
        return scene_names()
    names = []
    for value in selected:
        for part in str(value).split(","):
            name = part.strip().lstrip("-")
            if not name:
                continue
            if name not in SCENE_FLAGS:
                raise ValueError(
                    f"unknown scene '{name}' (choose from {', '.join(SCENE_FLAGS)})")
            if name not in names:
                names.append(name)
    if not names:
        raise ValueError("--only selected no scenes")
    return names


def _number(value, field):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{field} must be numeric")
    if not math.isfinite(float(value)):
        raise ValueError(f"{field} must be finite")
    return float(value)


def load_dump(path):
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValueError(f"cannot read {path}: {error}") from error
    if not isinstance(data, dict):
        raise ValueError(f"{path}: root is not an object")
    if "screenW" not in data or "screenH" not in data:
        raise ValueError(f"{path}: missing screenW/screenH")
    _number(data["screenW"], "screenW")
    _number(data["screenH"], "screenH")
    canvases = data.get("canvases")
    if not isinstance(canvases, list):
        raise ValueError(f"{path}: missing canvases array")
    for canvas_index, canvas in enumerate(canvases):
        if not isinstance(canvas, dict):
            raise ValueError(f"{path}: canvas {canvas_index} is not an object")
        elements = canvas.get("elements")
        if not isinstance(elements, list):
            raise ValueError(f"{path}: canvas {canvas_index} missing elements")
        for element_index, element in enumerate(elements):
            if not isinstance(element, dict):
                raise ValueError(
                    f"{path}: canvas {canvas_index} element {element_index} "
                    "is not an object")
            for key in ("x", "y", "w", "h"):
                _number(element.get(key), f"canvas {canvas_index} element "
                                          f"{element_index} {key}")
    return data


def _rect(element):
    return tuple(float(element[key]) for key in ("x", "y", "w", "h"))


def _element_label(canvas_name, index, name):
    return f"{canvas_name}:{name or f'elem_{index}'}#{index}"


def _full_screen(rect, width, height):
    x, y, w, h = rect
    return (x <= OFFSCREEN_EPS and y <= OFFSCREEN_EPS
            and x + w >= width - OFFSCREEN_EPS
            and y + h >= height - OFFSCREEN_EPS)


def _contains(outer, inner):
    ox, oy, ow, oh = outer
    ix, iy, iw, ih = inner
    return (ox <= ix + OVERLAP_EPS and oy <= iy + OVERLAP_EPS
            and ox + ow >= ix + iw - OVERLAP_EPS
            and oy + oh >= iy + ih - OVERLAP_EPS)


def _intersection(first, second):
    ax, ay, aw, ah = first
    bx, by, bw, bh = second
    left = max(ax, bx)
    top = max(ay, by)
    right = min(ax + aw, bx + bw)
    bottom = min(ay + ah, by + bh)
    if right - left <= OVERLAP_EPS or bottom - top <= OVERLAP_EPS:
        return 0.0
    return (right - left) * (bottom - top)


def analyze_dump(data, scene):
    width = float(data["screenW"])
    height = float(data["screenH"])
    canvases = []
    offscreen = []
    overlaps = []
    text_elements = []
    for canvas_index, canvas in enumerate(data["canvases"]):
        name = str(canvas.get("name") or f"canvas_{canvas_index}")
        elements = []
        labels = []
        for element_index, element in enumerate(canvas.get("elements", [])):
            element_name = str(element.get("name") or
                               f"elem_{element_index}")
            rect = _rect(element)
            entry = {
                "index": element_index,
                "name": element_name,
                "type": str(element.get("type") or "UNKNOWN"),
                "text": bool(element.get("text")),
                "x": rect[0],
                "y": rect[1],
                "w": rect[2],
                "h": rect[3],
            }
            elements.append(entry)
            labels.append(_element_label(name, element_index, element_name))
            x, y, w, h = rect
            overflow = {
                "left": max(0.0, -x),
                "top": max(0.0, -y),
                "right": max(0.0, x + w - width),
                "bottom": max(0.0, y + h - height),
            }
            if any(value > OFFSCREEN_EPS for value in overflow.values()):
                offscreen.append({
                    "canvas": name,
                    "element": element_name,
                    "index": element_index,
                    "type": entry["type"],
                    "rect": [x, y, w, h],
                    "overflow": {key: round(value, 2)
                                 for key, value in overflow.items()},
                })
            if entry["text"]:
                text_elements.append({
                    "canvas": name,
                    "element": element_name,
                    "index": element_index,
                    "type": entry["type"],
                    "rect": [x, y, w, h],
                })
        for left in range(len(elements)):
            for right in range(left + 1, len(elements)):
                first = elements[left]
                second = elements[right]
                area = _intersection(_rect(first), _rect(second))
                if area <= OVERLAP_EPS:
                    continue
                first_rect = _rect(first)
                second_rect = _rect(second)
                context = []
                if _full_screen(first_rect, width, height) or _full_screen(
                        second_rect, width, height):
                    context.append("full-screen")
                if _contains(first_rect, second_rect) or _contains(
                        second_rect, first_rect):
                    context.append("contains")
                if "UNKNOWN" in (first["type"], second["type"]):
                    context.append("untyped")
                smaller = min(first_rect[2] * first_rect[3],
                              second_rect[2] * second_rect[3])
                overlaps.append({
                    "canvas": name,
                    "first": labels[left],
                    "second": labels[right],
                    "intersection": round(area, 2),
                    "ratio": round(area / smaller, 4) if smaller > 0 else 0.0,
                    "context": context,
                })
        canvases.append({
            "name": name,
            "element_count": len(elements),
            "text_count": sum(1 for element in elements if element["text"]),
            "elements": elements,
        })
    background = [item for item in overlaps
                  if "full-screen" in item["context"]]
    foreground = [item for item in overlaps
                  if "full-screen" not in item["context"]]
    return {
        "scene": scene,
        "flag": SCENE_FLAGS.get(scene, ""),
        "screenW": width,
        "screenH": height,
        "canvas_count": len(canvases),
        "element_count": sum(canvas["element_count"] for canvas in canvases),
        "text_count": len(text_elements),
        "canvases": canvases,
        "offscreen": offscreen,
        "overlaps": overlaps,
        "foreground_overlaps": foreground,
        "background_overlaps": background,
        "text_elements": text_elements,
    }


def capture_scene(scene, directory, frame, width, height, timeout=180):
    import audit_common as audit

    dump = Path(directory) / f"{scene}.json"
    result = audit.run_scene(
        SCENE_FLAGS[scene],
        audit.scene_env(clean=True),
        width=width,
        height=height,
        fps=60,
        frames=max(frame + 4, 12),
        timeout=timeout,
        check=False,
        extra_args=("--layout-dump", str(dump),
                    "--layout-dump-frame", str(frame)),
    )
    if result.returncode != 0:
        raise RuntimeError(f"{scene} exited {result.returncode}")
    if not dump.is_file():
        raise RuntimeError(f"{scene} produced no layout dump")
    return load_dump(dump)


def build_report(scenes, frame, width, height, timeout=180):
    reports = []
    with tempfile.TemporaryDirectory(prefix="gx-layout-report-") as directory:
        for scene in scenes:
            reports.append(analyze_dump(
                capture_scene(scene, directory, frame, width, height,
                              timeout=timeout),
                scene))
    return {
        "width": width,
        "height": height,
        "frame": frame,
        "scenes": reports,
        "notes": list(NOTES),
        "totals": {
            "scenes": len(reports),
            "canvases": sum(scene["canvas_count"] for scene in reports),
            "elements": sum(scene["element_count"] for scene in reports),
            "offscreen": sum(len(scene["offscreen"]) for scene in reports),
            "overlaps": sum(len(scene["overlaps"]) for scene in reports),
            "foreground_overlaps": sum(
                len(scene["foreground_overlaps"]) for scene in reports),
            "background_overlaps": sum(
                len(scene["background_overlaps"]) for scene in reports),
            "text": sum(scene["text_count"] for scene in reports),
        },
    }


def _md_cell(value):
    return str(value).replace("|", "\\|")


def render_markdown(report):
    totals = report["totals"]
    lines = [
        "# Layout report",
        "",
        f"{totals['scenes']} scenes at {report['width']}x{report['height']}, "
        f"frame {report['frame']}.",
        "",
        "Findings are diagnostic, not pass/fail assertions.",
        "",
        "| Scene | Canvases | Elements | Off-screen | Overlaps | "
        "Background | Text |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for scene in report["scenes"]:
        lines.append(
            f"| {_md_cell(scene['scene'])} | {scene['canvas_count']} | "
            f"{scene['element_count']} | {len(scene['offscreen'])} | "
            f"{len(scene['foreground_overlaps'])} | "
            f"{len(scene['background_overlaps'])} | {scene['text_count']} |")
    lines.append("")
    for scene in report["scenes"]:
        lines.extend([
            f"## {scene['scene']} (`{scene['flag']}`)",
            "",
            f"Screen: {scene['screenW']:.0f}x{scene['screenH']:.0f}; "
            f"canvases: {scene['canvas_count']}; "
            f"elements: {scene['element_count']}; "
            f"text: {scene['text_count']}.",
            "",
        ])
        if not scene["canvases"]:
            lines.extend([
                "No GUI canvases were dumped at this frame "
                "(renderer-only scene).",
                "",
            ])
            continue
        lines.append("Canvases: " + ", ".join(
            f"`{_md_cell(canvas['name'])}` ({canvas['element_count']})"
            for canvas in scene["canvases"]))
        lines.append("")
        lines.append(f"### Off-screen ({len(scene['offscreen'])})")
        lines.append("")
        if scene["offscreen"]:
            lines.extend([
                "| Canvas | Element | Type | Rect | Overflow |",
                "|---|---|---|---|---|",
            ])
            for item in scene["offscreen"]:
                rect = item["rect"]
                overflow = ", ".join(
                    f"{key}={value:g}" for key, value in item["overflow"].items()
                    if value > 0)
                lines.append(
                    f"| {_md_cell(item['canvas'])} | {_md_cell(item['element'])} "
                    f"| {_md_cell(item['type'])} | "
                    f"{rect[0]:.1f},{rect[1]:.1f} {rect[2]:.1f}x{rect[3]:.1f} "
                    f"| {overflow} |")
        else:
            lines.append("None.")
        lines.extend([
            "",
            f"### Overlaps ({len(scene['foreground_overlaps'])} foreground; "
            f"{len(scene['background_overlaps'])} full-screen background)",
            "",
        ])
        if scene["foreground_overlaps"]:
            lines.extend([
                "| Canvas | First | Second | Intersection | Ratio | Context |",
                "|---|---|---|---:|---:|---|",
            ])
            for item in scene["foreground_overlaps"]:
                context = ", ".join(item["context"]) or "-"
                lines.append(
                    f"| {_md_cell(item['canvas'])} | {_md_cell(item['first'])} "
                    f"| {_md_cell(item['second'])} | {item['intersection']:g} "
                    f"| {item['ratio']:.2f} | {_md_cell(context)} |")
        else:
            lines.append("None.")
        if scene["background_overlaps"]:
            lines.append("")
            lines.append(
                "Full-screen background overlaps are intentional context and "
                "are listed in JSON, not the table above.")
        lines.extend(["", f"### Text elements ({scene['text_count']})", ""])
        if scene["text_elements"]:
            lines.extend([
                "| Canvas | Element | Type | Rect |",
                "|---|---|---|---|",
            ])
            for item in scene["text_elements"]:
                rect = item["rect"]
                lines.append(
                    f"| {_md_cell(item['canvas'])} | {_md_cell(item['element'])} "
                    f"| {_md_cell(item['type'])} | "
                    f"{rect[0]:.1f},{rect[1]:.1f} {rect[2]:.1f}x{rect[3]:.1f} |")
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
    parser.add_argument("--only", action="append", metavar="SCENE",
                        help="comma-separated scene names (repeatable)")
    parser.add_argument("--out", type=Path, help="write the report to this path")
    parser.add_argument("--format", choices=("markdown", "json"),
                        default="markdown")
    parser.add_argument("--frame", type=int, default=DEFAULT_FRAME)
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
    parser.add_argument("--timeout", type=int, default=180,
                        help="per-scene timeout in seconds")
    args = parser.parse_args()
    if args.frame < 1:
        parser.error("--frame must be >= 1")
    if args.width < 1 or args.height < 1:
        parser.error("--width/--height must be >= 1")
    try:
        scenes = select_scenes(args.only)
        report = build_report(scenes, args.frame, args.width, args.height,
                              timeout=args.timeout)
    except (RuntimeError, ValueError, OSError,
            subprocess.TimeoutExpired) as error:
        print(f"LAYOUT_REPORT: FAIL — {error}", file=sys.stderr)
        return 1

    output = (render_json(report) if args.format == "json"
              else render_markdown(report))
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output, encoding="utf-8")
        print(f"LAYOUT_REPORT: {len(scenes)} scenes -> {args.out}",
              file=sys.stderr)
    else:
        sys.stdout.write(output)
    totals = report["totals"]
    print(f"LAYOUT_REPORT: {totals['canvases']} canvases, "
          f"{totals['elements']} elements, {totals['offscreen']} off-screen, "
          f"{totals['overlaps']} overlaps, {totals['text']} text elements",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
