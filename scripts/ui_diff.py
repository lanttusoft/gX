#!/usr/bin/env python3
#ai: 2026-10-22 textual UI diff over gX structured layout dumps.
"""Compare two layout dumps without a vision round-trip.

Reads the JSON produced by `--layout-dump`:
  {"screenW":..., "screenH":..., "canvases":[
    {"name":..., "elements":[{"name":..., "x":..., "y":..., "w":..., "h":...}]}
  ]}

Exit 0 = no meaningful changes within tolerance; 1 = changes; 2 = input error.
"""

import argparse
import json
import sys
from pathlib import Path


def load_dump(path):
    try:
        data = json.loads(Path(path).read_text())
    except (OSError, ValueError) as error:
        raise ValueError(f"cannot read {path}: {error}") from error
    if not isinstance(data, dict):
        raise ValueError(f"{path}: root is not an object")
    if not isinstance(data.get("screenW"), (int, float)) or not isinstance(
            data.get("screenH"), (int, float)):
        raise ValueError(f"{path}: missing numeric screenW/screenH")
    canvases = data.get("canvases")
    if not isinstance(canvases, list):
        raise ValueError(f"{path}: missing canvases array")
    return data


def flatten(data):
    elements = {}
    for canvas in data["canvases"]:
        if not isinstance(canvas, dict):
            raise ValueError("canvas entry is not an object")
        canvas_name = str(canvas.get("name", ""))
        if not canvas_name:
            raise ValueError("canvas has no name")
        values = canvas.get("elements")
        if not isinstance(values, list):
            raise ValueError(f"canvas {canvas_name}: missing elements array")
        for index, element in enumerate(values):
            if not isinstance(element, dict):
                raise ValueError(f"canvas {canvas_name}: element {index} is not an object")
            name = str(element.get("name", f"elem_{index}"))
            rect = {}
            for key in ("x", "y", "w", "h"):
                value = element.get(key)
                if not isinstance(value, (int, float)):
                    raise ValueError(
                        f"canvas {canvas_name}: {name} missing numeric {key}")
                rect[key] = float(value)
            elements[(canvas_name, name)] = rect
    return elements


def compare(before, after, tolerance):
    changes = []
    if (before["screenW"], before["screenH"]) != (after["screenW"], after["screenH"]):
        changes.append({
            "kind": "screen",
            "detail": f"{before['screenW']}x{before['screenH']} -> "
                      f"{after['screenW']}x{after['screenH']}",
        })

    left = flatten(before)
    right = flatten(after)
    for key in sorted(left.keys() - right.keys()):
        changes.append({"kind": "removed", "canvas": key[0], "element": key[1]})
    for key in sorted(right.keys() - left.keys()):
        changes.append({"kind": "added", "canvas": key[0], "element": key[1]})
    for key in sorted(left.keys() & right.keys()):
        old = left[key]
        new = right[key]
        moved = any(abs(old[axis] - new[axis]) > tolerance for axis in ("x", "y"))
        resized = any(abs(old[axis] - new[axis]) > tolerance for axis in ("w", "h"))
        if not moved and not resized:
            continue
        change = {
            "kind": "moved" if moved else "resized",
            "canvas": key[0],
            "element": key[1],
            "before": old,
            "after": new,
        }
        changes.append(change)
    return changes


def main():
    parser = argparse.ArgumentParser(
        description="Textual diff between two gX --layout-dump JSONs")
    parser.add_argument("before")
    parser.add_argument("after")
    parser.add_argument("--tol", type=float, default=2.0,
                        help="position/size tolerance in pixels (default 2.0)")
    parser.add_argument("--json", help="optional JSON report path")
    args = parser.parse_args()
    if args.tol < 0:
        parser.error("--tol must be non-negative")

    try:
        before = load_dump(args.before)
        after = load_dump(args.after)
        changes = compare(before, after, args.tol)
    except ValueError as error:
        print(f"UI_DIFF: FAIL — {error}", file=sys.stderr)
        return 2

    report = {"pass": not changes, "tolerance": args.tol, "changes": changes}
    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=2) + "\n")

    if not changes:
        print(f"UI_DIFF: PASS — no changes within {args.tol:g}px")
        return 0

    print(f"UI_DIFF: {len(changes)} change(s) at tolerance {args.tol:g}px")
    for change in changes:
        if change["kind"] in ("added", "removed"):
            print(f"  {change['kind']:<8} {change['canvas']}:{change['element']}")
        elif change["kind"] == "screen":
            print(f"  screen   {change['detail']}")
        else:
            old = change["before"]
            new = change["after"]
            print(f"  {change['kind']:<8} {change['canvas']}:{change['element']} "
                  f"({old['x']:.1f},{old['y']:.1f},{old['w']:.1f}x{old['h']:.1f})"
                  f" -> ({new['x']:.1f},{new['y']:.1f},{new['w']:.1f}x{new['h']:.1f})")
    return 1


if __name__ == "__main__":
    sys.exit(main())
