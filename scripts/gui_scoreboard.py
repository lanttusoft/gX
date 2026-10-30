#!/usr/bin/env python3
#opencode: GUI system scoreboard — verification coverage/correctness,
#   advisory visual grades, and median per-template runtime cost.

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import time

import audit_common as audit


ROOT = str(audit.ROOT)
BIN = str(audit.BIN)
OUT = "/tmp/opencode/gui_scoreboard"
VISUAL_MANIFEST = "/tmp/opencode/gui-visual-review/visual_review.json"
FRAMES = 180
TIMEOUT = 180
SAMPLES = 3

GUI_AREAS = {
    "canvas/render": (90, "stable element slots, draw order, transforms",
                      ("GUI core audit tests: all passed",
                       "CANVAS TRANSFORM tests: all passed")),
    "input/modal": (90, "focus, pointer capture, modal and dialog behavior",
                    ("GUI INTERACT tests: all passed",
                     "GUI DIALOG tests: all passed",
                     "GUI MODAL REMOVE tests: all passed")),
    "widgets/text": (90, "widget callbacks, wrapping, bounded text input, fit analysis",
                     ("GUI WRAP tests: all passed",
                      "GUI TEXT LAYOUT tests: all passed",
                      "textinput done — 0 errors")),
    "layout/css": (90, "box layout, cascade precedence, overflow behavior",
                   ("CSS CASCADE tests: all passed",
                    "CSS OVERFLOW tests: all passed",
                    "LAYOUT tests: all passed")),
    "transitions": (85, "phase evaluation and canvas transition invariants",
                    ("GUI TRANS tests: all passed",)),
    "themes/a11y": (80, "theme reload, contrast tokens, and the save/load fixed point",
                    ("STYLE RELOAD tests: all passed",
                     "STYLE ROUNDTRIP tests: all passed")),
    "markup": (80, "callback ownership (nested), inherited visibility, and recursive cleanup are tested",
               ("GUI MARKUP regressions tests: all passed",
                "GUI MARKUP CLEAR tests: all passed",
                "GUI MARKUP OWNERSHIP tests: all passed")),
    "compositing/resize": (85, "dock target resize and the WM resize sync (state, viewport re-fit, RT re-target, cramped fallback) are tested",
                           ("GUI DOCK RESIZE tests: all passed",
                            "GUI WM RESIZE tests: all passed")),
    "tools": (85, "template matrix, snapshot round-trip, and synthetic ImGui press capture",
              ("GUI IMGUI input tests: all passed",
               "GUI SNAPSHOT roundtrip tests: all passed")),
}


def template_names():
    enum_path = os.path.join(ROOT, "src", "gui", "enum.h")
    with open(enum_path, encoding="utf-8") as handle:
        source = handle.read()
    match = re.search(r"#define gX_GUI_TEMPLATE \\\n(.*?)\n\n", source, re.S)
    if not match:
        raise RuntimeError("could not find gX_GUI_TEMPLATE registry")
    return re.findall(r"X\(([A-Z0-9_]+)\)", match.group(1))


def run_timed(template, frames, timeout):
    #opencode: timing rows keep the window mode the caller exported (the
    #   Makefile clears GX_AUDIT_WINDOW for the scoreboard).
    env = audit.scene_env({
        "GX_SKIP_TESTS": "1",
        "GX_FIXED_DT": "0.016666667",
        "GX_GUI_TEMPLATE_FORCE": template if template else None,
    }, clean=True)
    if not template:
        env.pop("GX_GUI_TEMPLATE_FORCE", None)
    start = time.perf_counter()
    try:
        audit.run_scene("--title", env, width=None, height=None, fps=None,
                        frames=frames, timeout=timeout, check=True,
                        extra_args=("--seed", "1337"))
    except subprocess.SubprocessError as exc:
        raise RuntimeError(f"{template or 'baseline'} timing run failed: {exc}") from exc
    return time.perf_counter() - start


def hardness(templates, samples, frames, timeout):
    off_samples = [run_timed(None, frames, timeout) for _ in range(samples)]
    off = statistics.median(off_samples)
    results = {}
    for template in templates:
        on_samples = [run_timed(template, frames, timeout) for _ in range(samples)]
        on = statistics.median(on_samples)
        cost = (on - off) / off * 100.0 if off > 0 else 0.0
        results[template] = {
            "off_s": round(off, 3),
            "on_s": round(on, 3),
            "cost_pct": round(cost, 1),
            "off_samples_s": [round(value, 3) for value in off_samples],
            "on_samples_s": [round(value, 3) for value in on_samples],
        }
        print(f"hardness {template:14s} off {off:6.2f}s on {on:6.2f}s "
              f"cost {cost:+6.1f}% (median/{samples})", flush=True)
    return results


def run_test_only(timeout):
    testEnv = dict(os.environ)
    testEnv["GX_NO_SETTINGS_SAVE"] = "1"
    result = subprocess.run([BIN, "--test-only"], cwd=ROOT,
                            env=testEnv, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=timeout)
    return result.returncode, result.stdout


def run_template_matrix(out_dir, timeout):
    matrix_dir = os.path.join(out_dir, "template_matrix")
    result = subprocess.run(
        ["python3", os.path.join(ROOT, "scripts", "gui_audit.py"),
         "--output", matrix_dir, "--timeout", str(timeout)],
        cwd=ROOT, env=dict(os.environ), text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1200)
    matrix = {}
    report = os.path.join(matrix_dir, "gui_matrix.md")
    if os.path.isfile(report):
        with open(report, "r", encoding="utf-8") as handle:
            for line in handle:
                match = re.match(r"\| ([A-Z0-9_]+) \| ([^|]+) \|", line)
                if match:
                    matrix[match.group(1)] = match.group(2).strip()
    return result.returncode, matrix, result.stdout


def read_matrix_report(matrix_dir, templates):
    report = os.path.join(matrix_dir, "gui_matrix.md")
    matrix = {}
    if not os.path.isfile(report):
        return None
    with open(report, "r", encoding="utf-8") as handle:
        for line in handle:
            match = re.match(r"\| ([A-Z0-9_]+) \| ([^|]+) \|", line)
            if match:
                matrix[match.group(1)] = match.group(2).strip()
    if not set(templates).issubset(matrix):
        return None
    return matrix


def reusable_matrix(matrix_dir, templates):
    audit_script = os.path.join(ROOT, "scripts", "gui_audit.py")
    freshness = max(os.path.getmtime(BIN), os.path.getmtime(audit_script),
                    os.path.getmtime(os.path.join(ROOT, "src", "gui", "enum.h")))
    required = [os.path.join(matrix_dir, "baseline.png")]
    required.extend(os.path.join(matrix_dir, name.lower() + ".png")
                    for name in templates)
    report = os.path.join(matrix_dir, "gui_matrix.md")
    required.append(report)
    if any(not os.path.isfile(path) or os.path.getmtime(path) < freshness
           for path in required):
        return None
    return read_matrix_report(matrix_dir, templates)


def obtain_template_matrix(out_dir, timeout, templates, matrix_dir, refresh):
    if not refresh:
        matrix = reusable_matrix(matrix_dir, templates)
        if matrix is not None:
            code = 0 if all(value.startswith("PASS") for value in matrix.values()) else 1
            print(f"GUI matrix: reusing fresh captures from {matrix_dir}")
            return code, matrix, ""
    code, matrix, output = run_template_matrix(out_dir, timeout)
    if not matrix:
        return code, matrix, output
    return code, matrix, output


def load_visual_grades(path):
    if not os.path.isfile(path):
        return {}
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, json.JSONDecodeError):
        return {}
    return {item.get("id"): item.get("grade", "UNREVIEWED")
            for item in data.get("cards", [])
            if isinstance(item, dict) and item.get("id")}


def main():
    templates = template_names()
    parser = argparse.ArgumentParser(
        description="Score GUI verification, template rendering, visuals, and runtime cost.")
    parser.add_argument("--only", nargs="+", metavar="TEMPLATE",
                        help="time only these templates; skips correctness gates")
    parser.add_argument("--skip-gates", action="store_true",
                        help="skip headless tests and the full GUI capture matrix")
    parser.add_argument("--samples", type=int, default=SAMPLES,
                        help=f"timing samples per arm (default: {SAMPLES})")
    parser.add_argument("--frames", type=int, default=FRAMES,
                        help=f"frames per timing run (default: {FRAMES})")
    parser.add_argument("--timeout", type=int, default=TIMEOUT,
                        help=f"seconds per process (default: {TIMEOUT})")
    parser.add_argument("--json", default=None,
                        help="optional JSON scoreboard output path")
    parser.add_argument("--visual-manifest", default=VISUAL_MANIFEST,
                        help="advisory GUI visual-review JSON manifest")
    parser.add_argument("--matrix-dir", default="/tmp/opencode/gui_audit",
                        help="reuse fresh captures from this gui-check output")
    parser.add_argument("--refresh-matrix", action="store_true",
                        help="recapture the full template matrix even if fresh captures exist")
    args = parser.parse_args()
    if args.samples < 1 or args.frames < 1 or args.timeout < 1:
        parser.error("samples, frames, and timeout must be positive")
    if args.only:
        requested = {name.upper() for name in args.only}
        unknown = sorted(requested - set(templates))
        if unknown:
            parser.error("unknown GUI template(s): " + ", ".join(unknown))
        selected = [name for name in templates if name in requested]
    else:
        selected = templates
    os.makedirs(OUT, exist_ok=True)

    timing = hardness(selected, args.samples, args.frames, args.timeout)
    visual_grades = load_visual_grades(args.visual_manifest)

    test_code = 0
    test_log = ""
    matrix_code = 0
    matrix = {}
    matrix_log = ""
    gates_run = not args.skip_gates and not args.only
    if gates_run:
        test_code, test_log = run_test_only(args.timeout)
        matrix_code, matrix, matrix_log = obtain_template_matrix(
            OUT, args.timeout, templates, args.matrix_dir, args.refresh_matrix)
    expected_markers = {marker for _, _, markers in GUI_AREAS.values()
                        for marker in markers}
    test_ok = (gates_run and test_code == 0
               and all(marker in test_log for marker in expected_markers))
    matrix_ok = (gates_run and matrix_code == 0
                 and set(templates).issubset(matrix)
                 and all(value.startswith("PASS") for value in matrix.values()))

    print(f"\n{'GUI system':20s} {'cmpl%':>6s} {'correctness':14s} coverage note")
    system_summary = {}
    for name, (completeness, rationale, markers) in GUI_AREAS.items():
        if not gates_run:
            correctness = "n/a"
        elif name == "tools":
            correctness = ("pass" if matrix_ok and test_ok
                           and all(marker in test_log for marker in markers)
                           else "fail")
        elif not markers:
            correctness = "untested"
        else:
            correctness = ("pass" if test_ok
                           and all(marker in test_log for marker in markers)
                           else "fail")
        print(f"{name:20s} {completeness:5d}% {correctness:14s} {rationale}")
        system_summary[name] = {
            "completeness": completeness,
            "correctness": correctness,
            "rationale": rationale,
        }

    print(f"\n{'template':16s} {'correct':12s} {'visual':>10s} {'hard%':>8s}")
    template_summary = {}
    for template in selected:
        if not gates_run:
            correct = "n/a"
        else:
            correct = "pass" if matrix.get(template, "").startswith("PASS") else "fail"
        grade = visual_grades.get("template:" + template, "n/a")
        if grade == "UNREVIEWED":
            grade = "unrev"
        cost = timing[template]["cost_pct"]
        print(f"{template:16s} {correct:12s} {grade:>10s} {cost:+7.1f}%")
        template_summary[template] = {
            "correctness": correct,
            "visual": grade,
            "hardness": timing[template],
        }

    summary = {
        "systems": system_summary,
        "templates": template_summary,
        "headless_test_exit": test_code if gates_run else None,
        "headless_test_ok": test_ok if gates_run else None,
        "template_matrix_exit": matrix_code if gates_run else None,
        "template_matrix_ok": matrix_ok if gates_run else None,
        "template_matrix": matrix,
        "visual_manifest": os.path.abspath(args.visual_manifest),
        "frames": args.frames,
        "samples": args.samples,
    }
    json_path = args.json or os.path.join(OUT, "gui_scoreboard.json")
    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2)
        handle.write("\n")

    if not gates_run or (test_ok and matrix_ok):
        print(f"\nGUI SCOREBOARD: PASS (report: {json_path})")
        return 0
    print("\nGUI SCOREBOARD: FAIL — correctness gate failed")
    if not test_ok:
        print(test_log[-3000:])
    if matrix_code != 0:
        print(matrix_log[-3000:])
    return 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError,
            ValueError, json.JSONDecodeError) as exc:
        print(f"GUI SCOREBOARD: FAIL — {exc}", file=sys.stderr)
        sys.exit(1)
