#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: replay a private bug report's diagnostics as a temporary preset.
#   The merged preset and screenshot stay outside the repository by default;
#   reports and captures are never committed.

import argparse
import copy
import json
from pathlib import Path
import shutil
import sys
import tempfile

import audit_common as audit


MERGE_SECTIONS = (
    "state", "ssao", "ssr", "csm", "reflection", "godray", "fogVolume",
    "lensflare", "bloom", "fog", "editor",
)


def deep_merge(base, extra):
    for key, value in extra.items():
        if isinstance(value, dict) and isinstance(base.get(key), dict):
            deep_merge(base[key], value)
        else:
            base[key] = copy.deepcopy(value)
    return base


def resolve_preset(report, explicit=None):
    preset = explicit or report.get("preset")
    if not preset:
        raise ValueError("report has no preset path; pass --preset")
    path = Path(preset)
    if not path.is_absolute():
        path = audit.ROOT / path
    if not path.is_file():
        raise FileNotFoundError(f"base preset not found: {path}")
    return path


def merge_report(base, report):
    diagnostics = report.get("diagnostics")
    if not isinstance(diagnostics, dict):
        raise ValueError("report has no diagnostics snapshot to replay")
    merged = copy.deepcopy(base)
    for section in MERGE_SECTIONS:
        value = diagnostics.get(section)
        if not isinstance(value, dict):
            continue
        if isinstance(merged.get(section), dict):
            deep_merge(merged[section], value)
        else:
            merged[section] = copy.deepcopy(value)
    camera = diagnostics.get("camera")
    if isinstance(camera, dict) and camera:
        merged["camera"] = copy.deepcopy(camera)
        cameras = merged.get("cameras")
        if isinstance(cameras, list) and cameras and isinstance(cameras[0], dict):
            cameras[0]["view"] = copy.deepcopy(camera)
    return merged


def replay(report_path, preset_path, out_path, width, height, frame,
           preset_out=None):
    report = json.loads(Path(report_path).read_text())
    base = json.loads(preset_path.read_text())
    merged = merge_report(base, report)
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="gx_replay_",
                                     dir="/tmp/opencode") as temporary:
        root = Path(temporary)
        temporary_preset = root / "replay.json"
        text = json.dumps(merged, indent=1) + "\n"
        temporary_preset.write_text(text)
        if preset_out:
            destination = Path(preset_out)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(text)
        environment = audit.scene_env({
            "GX_FIXED_DT": "0.0166666667",
            "GX_GFX_PRESET_LOAD": str(temporary_preset),
        }, clean=True)
        with audit.settings_preserved():
            result = audit.run_scene(
                "--gfx", environment, width=width, height=height, fps=None,
                shot_frame=frame, screenshot_dir=root, timeout=120,
                extra_args=("--gfx-editor",), check=False)
        if result.returncode != 0:
            raise RuntimeError(
                f"renderer exited {result.returncode}\n"
                f"{(result.stdout or '')[-3000:]}")
        shot = audit.newest_shot(root)
        if shot is None:
            raise RuntimeError("renderer produced no screenshot")
        shutil.copyfile(shot, out_path)
    return {
        "report": str(report_path),
        "preset": str(preset_path),
        "out": str(out_path),
        "width": width,
        "height": height,
        "frame": frame,
        "title": report.get("title", ""),
    }


def main():
    parser = argparse.ArgumentParser(
        description="Render a private bug report's diagnostics as a preset.")
    parser.add_argument("report")
    parser.add_argument("--preset",
                        help="base preset; defaults to the report's preset")
    parser.add_argument("--out",
                        help="screenshot path (default: /tmp/opencode/replay-<report>.png)")
    parser.add_argument("--preset-out",
                        help="optionally save the merged preset for inspection")
    parser.add_argument("--width", type=int, default=1024)
    parser.add_argument("--height", type=int, default=768)
    parser.add_argument("--frame", type=int, default=120)
    parser.add_argument("--json", action="store_true")
    arguments = parser.parse_args()

    report_path = Path(arguments.report)
    try:
        report = json.loads(report_path.read_text())
        preset_path = resolve_preset(report, arguments.preset)
        out_path = arguments.out or (
            "/tmp/opencode/replay-" + report_path.stem + ".png")
        summary = replay(report_path, preset_path, out_path,
                         arguments.width, arguments.height, arguments.frame,
                         arguments.preset_out)
    except (OSError, ValueError, RuntimeError) as error:
        print(f"BUG_REPLAY: FAIL — {error}", file=sys.stderr)
        return 1
    if arguments.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        print(f"BUG_REPLAY: {summary['title']}")
        print(f"  report: {summary['report']}")
        print(f"  preset: {summary['preset']}")
        print(f"  camera: {arguments.width}x{arguments.height} frame {arguments.frame}")
        print(f"  out: {summary['out']}")
        print("  private scene content — inspect before sharing")
    return 0


if __name__ == "__main__":
    sys.exit(main())
