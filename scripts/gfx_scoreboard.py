#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: GFX feature scoreboard — completeness (static, curated) /
#   correctness (owning-gate verdicts) / hardness (timed on/off wall cost).
#   Hardness runs GX_FRAMES frames per arm on the matrix row scenes; each arm
#   is sampled three times and reported as a median. Costs include process
#   startup, so bands are coarse by design. Slow gates
#   (matrix, reflect suite) run once and are parsed, not reimplemented.

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
OUT = "/tmp/opencode/scoreboard"
VISUAL_MANIFEST = "/tmp/opencode/visual-review/visual_review.json"
FRAMES = 480
TIMEOUT = 300
SAMPLES = 3

BASE_OFF = {
    "GX_T_BLOOM": "0", "GX_T_SSAO": "0", "GX_T_SHADOWS": "0",
    "GX_T_POINT_SHADOWS": "0",
    "GX_T_SSR": "0", "GX_T_TAA": "0", "GX_T_FXAA": "0",
    "GX_T_GODRAY": "0", "GX_T_COOKIES": "0",
    "GX_T_PARTICLES": "0", "GX_T_LENSFLARE": "0", "GX_T_DOF": "0",
    "GX_T_MOTIONBLUR": "0", "GX_T_ENVAMBIENT": "0", "GX_T_DECALS": "0",
    "GX_T_WIREFRAME": "0", "GX_T_SSRAUX": "0", "GX_T_INSTANCING": "0",
    "GX_T_FOG": "1", "GX_FOG_DENSITY": "0",
    #opencode: HUD glyphs jitter with frame timing; keep diff metrics stable.
    "GX_T_HUD": "0",
    "GX_CAM_YAW": "30", "GX_CAM_PITCH": "25", "GX_CAM_DIST": "10",
}

# (name, scene, on-extra-env)
HARDNESS_ROWS = [
    ("bloom", "--showcase", {"GX_T_BLOOM": "1", "GX_T_BLOOMINT": "6"}),
    ("bloomemis", "--showcase-minimal",
     {"GX_DIAG_CUBE": "1", "GX_T_EMISSIVEMAP": "1",
      "GX_T_BLOOM": "1", "GX_T_BLOOMINT": "6"}),
    ("ssao", "--showcase", {"GX_T_SSAO": "1"}),
    ("shadows", "--showcase", {"GX_T_SHADOWS": "1"}),
    ("ssr", "--showcase", {"GX_T_SSR": "1"}),
    ("ssraux", "--reflectrow", {"GX_T_SSR": "1", "GX_T_SSRAUX": "1"}),
    ("pointshadows", "--showcase",
     {"GX_T_POINT_SHADOWS": "1", "GX_EXTRA_SHADOW_LIGHTS": "1"}),
    ("instancing", "--showcase", {"GX_T_INSTANCING": "1"}),
    ("taa", "--showcase", {"GX_T_TAA": "1"}),
    ("fxaa", "--showcase", {"GX_T_FXAA": "1"}),
    ("fog", "--showcase", {"GX_T_SSR": "1", "GX_T_SHADOWS": "1",
                           "GX_T_FOG": "1", "GX_FOG_DENSITY": "0.01"}),
    ("godray", "--showcase", {"GX_T_GODRAY": "1"}),
    ("cookies", "--showcase", {"GX_T_COOKIES": "1"}),
    ("particles", "--showcase", {"GX_T_PARTICLES": "1"}),
    ("lensflare", "--showcase", {"GX_T_LENSFLARE": "1"}),
    ("dof", "--showcase", {"GX_T_DOF": "1", "GX_T_DOFFOCAL": "1.0"}),
    ("motionblur", "--showcase",
     {"GX_T_MOTIONBLUR": "1", "GX_CAM_SPIN": "3"}),
    ("envambient", "--showcase", {"GX_T_ENVAMBIENT": "1"}),
    ("decals", "--showcase", {"GX_T_DECALS": "1"}),
    ("wireframe", "--showcase", {"GX_T_WIREFRAME": "1"}),
    ("occlusion", "--showcase", {"GX_T_OCCL": "1"}),
    ("frustum", "--showcase", {"GX_T_FRUSTUM": "1"}),
]

# feature: (completeness %, rationale)
COMPLETENESS = {
    "bloom": (95, "stage audit: extract/blur/composite/AE/emissive/MSAA"),
    "bloomemis": (90, "emissive-map bloom extraction"),
    "ssao": (90, "GTAO kernel + SSGI bundle"),
    "shadows": (90, "CSM + point-cube shadows"),
    "ssr": (85, "march/HiZ/temporal/auxview; normals opt-in"),
    "ssraux": (90, "planar miss fallback"),
    "pointshadows": (85, "atlas + fog coupling"),
    "instancing": (80, "GL4.3 SSBO path only"),
    "taa": (90, "velocity + history resolve"),
    "fxaa": (95, "composite AA"),
    "fog": (85, "froxel march + LUT + planar fog"),
    "godray": (75, "screen-space shafts"),
    "cookies": (80, "atlas projection"),
    "particles": (85, "GL33 + compute paths"),
    "lensflare": (80, "ghosts/dirt"),
    "dof": (85, "provoked focal slab"),
    "motionblur": (85, "velocity taps"),
    "envambient": (80, "cubemap blend"),
    "decals": (70, "quads, no deferred clip"),
    "wireframe": (90, "overlay + placement pin"),
    "occlusion": (85, "query cull"),
    "frustum": (90, "CPU cull"),
    "planar": (90, "RT + deformable consumers"),
    "cubemap": (90, "6-face + IBL fallback"),
    "water": (85, "Gerstner + analytic SSR normals"),
    "nonplanar": (75, "opt-in analytic path"),
}


def run_timed(scene, extra):
    #opencode: timing rows keep the window mode the caller exported (the
    #   Makefile clears GX_AUDIT_WINDOW for the scoreboard so the measured
    #   wall cost matches a normal run).
    environment = audit.scene_env({
        **BASE_OFF,
        **extra,
        "GX_FIXED_DT": "0.0166666667",
    }, clean=True)
    start = time.perf_counter()
    audit.run_scene(scene, environment, width=None, height=None, fps=None,
                    frames=FRAMES, timeout=TIMEOUT, check=True)
    return time.perf_counter() - start


def hardness(only, samples):
    results = {}
    for name, scene, on_extra in HARDNESS_ROWS:
        if only and name not in only:
            continue
        off_samples = [run_timed(scene, {}) for _ in range(samples)]
        on_samples = [run_timed(scene, on_extra) for _ in range(samples)]
        off = statistics.median(off_samples)
        on = statistics.median(on_samples)
        pct = (on - off) / off * 100.0 if off > 0 else 0.0
        results[name] = {"off_s": round(off, 2), "on_s": round(on, 2),
                         "cost_pct": round(pct, 1),
                         "off_samples_s": [round(value, 2) for value in off_samples],
                         "on_samples_s": [round(value, 2) for value in on_samples]}
        print(f"hardness {name:14s} off {off:5.1f}s on {on:5.1f}s "
              f"cost {pct:+5.1f}% (median/{samples})", flush=True)
    return results


def run_gate(script, args):
    result = subprocess.run(
        ["python3", os.path.join(ROOT, "scripts", script)] + args,
        cwd=ROOT, env={**os.environ, "DISPLAY": ":1"},
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=1200)
    return result.returncode, result.stdout


def matrix_correctness():
    code, out = run_gate("gfx_feature_matrix.py", [])
    verdicts = {}
    for line in out.splitlines():
        match = re.match(r"\s*(\w+)\s+\|\s+[\d.]+\s+[\d.]+\s+[\d.]+\s+"
                         r"[\d.]+\s+[\d.]+\s+\|\s+(\w+)", line)
        if match:
            verdicts[match.group(1)] = match.group(2)
    return code, verdicts


BLOOM_AUDIT_JSON = os.path.join(OUT, "bloom_audit.json")


#opencode: the stage audit owns bloom correctness (the matrix row stays the
#   fast smoke). Capture runs stay hidden even when the scoreboard clears
#   GX_AUDIT_WINDOW for timing realism, and the JSON report is parsed instead
#   of reimplementing the audit's metrics.
def bloom_audit_correctness():
    environment = {**os.environ, "DISPLAY": ":1"}
    if environment.get("GX_AUDIT_ONSCREEN") != "1":
        environment["GX_AUDIT_WINDOW"] = "hidden"
    result = subprocess.run(
        ["python3", os.path.join(ROOT, "scripts", "bloom_audit.py"),
         "--json", BLOOM_AUDIT_JSON, "--jobs", "4"],
        cwd=ROOT, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1200)
    families, failed = [], []
    try:
        with open(BLOOM_AUDIT_JSON, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        families = data.get("families", [])
        failed = [f"{row.get('family')}.{row.get('probe')}"
                  for row in data.get("rows", []) if not row.get("ok")]
    except (OSError, json.JSONDecodeError):
        pass
    return result.returncode, families, failed, result.stdout


def suite_correctness():
    code, out = run_gate("reflect_suite.py", [])
    grades = {}
    for line in out.splitlines():
        match = re.match(r"\s*\S+\s+[\d.]+\s+[\d.]+\s+\|\s+([\d.]+)\s+(\S+)"
                         r"\s+\|\s+([\d.]+)\s+(\S+)", line)
        if match:
            grades.setdefault("ssr_rows", []).append(match.group(2))
            grades.setdefault("planar_rows", []).append(match.group(4))
    match = re.search(r"nonplanar\s+\|\s+planar\s+([\d.]+)\s+cube\s+"
                      r"([\d.]+)\s+(\S+)", out)
    nonplanar = match.group(3) if match else "?"
    match = re.search(r"^\s*water\b.*non-black\s+[\d.]+%\s+(\S+)\s*$",
                      out, re.MULTILINE)
    water = match.group(1) if match else "?"
    match = re.search(r"^\s*cubemap\b.*\s([SABCDF](?:\+|-)?)\s*$",
                      out, re.MULTILINE)
    cubemap = match.group(1) if match else "?"
    match = re.search(r"REFLECT-SUITE:\s+(\S+)", out)
    overall = match.group(1) if match else "?"
    return code, grades, nonplanar, water, cubemap, overall


RANK = {"S": 0, "A+": 1, "A": 2, "A-": 3, "B": 4, "C": 5, "D": 6,
        "F": 7, "ok": 2, "?": 7}


def worst(items):
    return max(items, key=lambda g: RANK.get(g, 7))


#opencode: matrix rows expose pass/fail rather than calibrated fidelity bands;
#   map a passing owning gate to A so the table remains grade-shaped without
#   pretending the binary matrix has more resolution than it does.
def matrix_grade(status):
    return "A" if status == "ok" else "F"


#opencode: visual grades are deliberately advisory and loaded from the
#   separate contact-sheet manifest only when a human review exists.
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


VISUAL_IDS = {
    "ssr": "reflect:ssr_default_30_25",
    "planar": "reflect:planar_default_30_25",
    "nonplanar": "reflect:nonplanar",
    "water": "reflect:water",
    "cubemap": "reflect:cubemap",
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--only", nargs="*",
                        help="hardness subset + skip slow gates")
    parser.add_argument("--skip-gates", action="store_true")
    parser.add_argument("--json", default=None)
    parser.add_argument("--samples", type=int, default=SAMPLES,
                        help="timing samples per arm (default: 3)")
    parser.add_argument("--visual-manifest", default=VISUAL_MANIFEST,
                        help="advisory visual-review JSON manifest")
    args = parser.parse_args()
    if args.samples < 1:
        parser.error("--samples must be at least 1")
    if args.only:
        known = {row[0] for row in HARDNESS_ROWS}
        unknown = sorted(set(args.only) - known)
        if unknown:
            parser.error("unknown hardness feature(s): " + ", ".join(unknown))
    os.makedirs(OUT, exist_ok=True)

    hard = hardness(set(args.only) if args.only else None, args.samples)
    visual_grades = load_visual_grades(args.visual_manifest)

    matrix_ok, matrix_verdicts, suite = True, {}, None
    bloom = None
    if not args.skip_gates and not args.only:
        matrix_code, matrix_verdicts = matrix_correctness()
        expected_matrix = {row[0] for row in HARDNESS_ROWS}
        #opencode: the full matrix can grow beyond the scoreboard's curated
        #   timing rows; require every curated row and accept extra passing rows.
        matrix_ok = (matrix_code == 0
                     and expected_matrix.issubset(matrix_verdicts)
                     and all(v == "ok" for v in matrix_verdicts.values()))
        suite_code, suite_grades, nonplanar, water, cubemap, overall = suite_correctness()
        suite = {"code": suite_code, "grades": suite_grades,
                 "nonplanar": nonplanar, "water": water,
                 "cubemap": cubemap, "overall": overall}
        bloom_code, bloom_families, bloom_failed, bloom_out = bloom_audit_correctness()
        bloom = {"code": bloom_code,
                 "verdict": "A" if bloom_code == 0 else "F",
                 "families": bloom_families, "failed": bloom_failed}
        if bloom_code != 0:
            print(bloom_out[-1500:])
        print(f"bloom audit: {len(bloom_families)} families, "
              f"{'all checks ok' if not bloom_failed else 'failed ' + ', '.join(bloom_failed)}"
              f" | {bloom['verdict']}")

    print(f"\n{'feature':14s} {'cmpl%':>5s} correct{' ':5s} visual{' ':5s} {'hard%':>6s}")
    summary = {"hardness": hard, "matrix": matrix_verdicts,
               "matrix_ok": matrix_ok, "suite": suite,
               "bloom_audit": bloom, "rows": {}}
    correct_grades = []
    for name in list(COMPLETENESS):
        cmpl, why = COMPLETENESS[name]
        if name in hard:
            cost = hard[name]["cost_pct"]
            hard_str = f"{cost:+5.1f}%"
        else:
            hard_str = "   n/a"
        visual_id = VISUAL_IDS.get(name, "matrix:" + name)
        visual = visual_grades.get(visual_id, "n/a")
        if visual == "UNREVIEWED":
            visual = "unrev"
        if suite and name in ("ssr", "planar"):
            key = "ssr_rows" if name == "ssr" else "planar_rows"
            letter = worst(suite["grades"].get(key, ["?"]))
            correct = f"suite:{letter}"
            correct_grades.append(letter)
        elif suite and name == "nonplanar":
            correct = f"suite:{suite['nonplanar']}"
            correct_grades.append(suite["nonplanar"])
        elif bloom is not None and name == "bloom":
            correct = f"bloom:{bloom['verdict']}"
            correct_grades.append(bloom["verdict"])
        elif name in matrix_verdicts:
            grade = matrix_grade(matrix_verdicts[name])
            correct = f"matrix:{grade}"
            correct_grades.append(grade)
        elif suite and name in ("cubemap", "water"):
            grade = suite[name]
            correct = f"suite:{grade}"
            correct_grades.append(grade)
        else:
            correct = "n/a"
        print(f"{name:14s} {cmpl:4d}% {correct:12s} {visual:>7s} {hard_str}")
        summary["rows"][name] = {"complete": cmpl, "why": why,
                                 "correct": correct, "visual": visual,
                                 "hard": hard_str}
    mean_cmpl = sum(v[0] for v in COMPLETENESS.values()) / len(COMPLETENESS)
    print(f"\ncompleteness mean {mean_cmpl:.1f}% | "
          f"correctness worst {worst(correct_grades) if correct_grades else '?'}")
    if args.json:
        with open(args.json, "w") as handle:
            json.dump(summary, handle, indent=2)
    gate_ok = args.skip_gates or bool(args.only) or (
        matrix_ok and suite is not None and suite["code"] == 0
        and (bloom is None or bloom["code"] == 0)
        and suite["overall"] != "F"
        and suite["nonplanar"] != "?"
        and suite["water"] != "?"
        and suite["cubemap"] != "?"
    )
    if gate_ok:
        print("SCOREBOARD: PASS")
    else:
        print("SCOREBOARD: FAIL — correctness gate failed")
        sys.exit(1)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"SCOREBOARD: FAIL — {exc}")
        sys.exit(1)
