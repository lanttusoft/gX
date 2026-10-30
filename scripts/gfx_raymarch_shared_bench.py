#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: deterministic shared-depth budget probe. One SDF sphere against the
#   raster floor/cube (no terrain or water), so pass-level regressions are
#   visible without the treearena scene's content variance.
import argparse
import csv
import json
import os
import statistics
import sys
from pathlib import Path

import audit_common as audit

ROOT = Path(audit.ROOT)
DEFAULT_BIN = ROOT / "build" / "gX_build"
DEFAULT_OUT = Path("/tmp/opencode/raymarch_shared_bench")
WIDTH = 1600
HEIGHT = 900
WARMUP = 60
FRAMES = 120

#ai: rows isolate raster, hybrid color, and shared color+depth at two scales.
ROWS = {
    "raster": {"depth": "0", "scale": "1.0", "view": "0"},
    "hybrid-0.35": {"depth": "1", "scale": "0.35", "view": "1"},
    "shared-0.25": {"depth": "2", "scale": "0.25", "view": "1"},
    "shared-0.35": {"depth": "2", "scale": "0.35", "view": "1"},
    "shared-1.00": {"depth": "2", "scale": "1.0", "view": "1"},
}


def read_cpu(path):
    values = []
    with open(path, newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            try:
                values.append(float(row["cpu_ms"]))
            except (KeyError, TypeError, ValueError):
                continue
    return values


def run_row(row, output, binary, repeats):
    samples = []
    for repeat in range(1, repeats + 1):
        run_dir = output / row / f"repeat-{repeat:02d}"
        run_dir.mkdir(parents=True, exist_ok=True)
        cpu_path = run_dir / "cpu.csv"
        log_path = run_dir / "run.log"
        environment = audit.scene_env({
            "GX_FIXED_DT": "0.0166666667",
            "GX_BENCHMARK_UNCAPPED": "1",
            "GX_BENCHMARK_NO_FLUSH": "1",
            "GX_BENCHMARK_CPU_CSV": str(cpu_path),
            "GX_RAYMARCH_DEPTH_TEST": "front",
            "GX_GFX_RAYMARCH_VIEW": ROWS[row]["view"],
            "GX_GFX_RAYMARCH_COMPOSITE": ROWS[row]["view"],
            "GX_GFX_RAYMARCH_DEPTH": ROWS[row]["depth"],
            "GX_GFX_RAYMARCH_RENDERSCALE": ROWS[row]["scale"],
        }, clean=True)
        result = audit.run_scene(
            "--showcase-minimal", environment, binary=str(binary),
            width=WIDTH, height=HEIGHT, fps=None,
            frames=WARMUP + FRAMES, timeout=180, check=False)
        log_path.write_text(audit.strip_ansi(result.stdout or ""),
                            encoding="utf-8")
        if result.returncode != 0:
            raise RuntimeError(f"{row} repeat {repeat} exited {result.returncode}")
        values = read_cpu(cpu_path)[-FRAMES:]
        if len(values) != FRAMES:
            raise RuntimeError(f"{row} repeat {repeat}: {len(values)} CPU rows")
        samples.extend(values)
    ordered = sorted(samples)
    return {
        "row": row,
        "median_ms": statistics.median(ordered),
        "p95_ms": ordered[min(len(ordered) - 1, int(0.95 * len(ordered)))],
        "samples": len(ordered),
    }


def check(rows):
    problems = []
    for name in ROWS:
        if name not in rows:
            problems.append(f"{name}: missing row")
    if problems:
        return problems
    shared = rows["shared-0.35"]["median_ms"]
    shared_full = rows["shared-1.00"]["median_ms"]
    hybrid = rows["hybrid-0.35"]["median_ms"]
    #ai: the old regression rendered shared color at full resolution, so the
    #   scale row stopped separating from 1.00.
    if shared >= shared_full * 0.8:
        problems.append(
            f"shared scale not honored ({shared:.2f} ms at 0.35 vs "
            f"{shared_full:.2f} ms at 1.00)")
    #ai: the depth split should stay within a bounded multiple of hybrid color;
    #   a full-res shaded depth pass blows past this.
    if shared > hybrid * 2.2:
        problems.append(
            f"shared depth overhead too high ({shared:.2f} ms vs "
            f"hybrid {hybrid:.2f} ms)")
    if rows["shared-0.25"]["median_ms"] > shared:
        problems.append("shared 0.25 slower than shared 0.35")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default=str(DEFAULT_BIN))
    parser.add_argument("--output", default=str(DEFAULT_OUT))
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    rows = {}
    print("shared-depth sphere probe (1600x900, one SDF sphere + raster)")
    for name in ROWS:
        row = run_row(name, output, Path(args.binary), args.repeats)
        rows[name] = row
        print(f"  {name:14s} median {row['median_ms']:7.2f} ms  "
              f"p95 {row['p95_ms']:7.2f} ms", flush=True)

    (output / "shared_depth_bench.json").write_text(
        json.dumps({"width": WIDTH, "height": HEIGHT, "frames": FRAMES,
                    "warmup": WARMUP, "repeats": args.repeats, "rows": rows},
                   indent=2) + "\n", encoding="utf-8")
    report = ["# Shared-depth sphere probe", "",
              f"{WIDTH}x{HEIGHT}, {FRAMES} frames, {WARMUP} warmup, "
              f"{args.repeats} repeat(s)", "",
              "| row | median | p95 |", "| --- | ---: | ---: |"]
    for name, row in rows.items():
        report.append(f"| {name} | {row['median_ms']:.2f} | {row['p95_ms']:.2f} |")
    report.append("")
    (output / "shared_depth_bench.md").write_text("\n".join(report),
                                                  encoding="utf-8")

    problems = check(rows) if args.check else []
    if problems:
        for problem in problems:
            print(f"SHARED_DEPTH_BENCH: FAIL — {problem}", file=sys.stderr)
        return 1
    if args.check:
        print("SHARED_DEPTH_BENCH: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
