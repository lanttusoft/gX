#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: gate manifest CLI — list, plan, run, explain and cache.
#   The manifest lives in gates.toml; audit_summary.py uses the same loader.

import argparse
import json
import subprocess
import sys
from pathlib import Path

import audit_cache
import evidence_stamp
import gate_manifest


ROOT = Path(__file__).resolve().parent.parent
SUMMARY = ROOT / "scripts" / "audit_summary.py"


def load_or_error(path):
    try:
        manifest = gate_manifest.load(path)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GATES: manifest error: {error}", file=sys.stderr)
        return None
    if manifest is None:
        print(f"GATES: manifest missing: {path or gate_manifest.MANIFEST_PATH}",
              file=sys.stderr)
        return None
    return manifest


def command_list(manifest, arguments):
    gates = gate_manifest.select_tier(manifest, arguments.tier)
    if arguments.json:
        print(json.dumps(gates, indent=2))
        return 0
    for gate in gates:
        print(f"{gate['name']:22s} {','.join(gate['tiers']):10s} "
              f"{gate['cache']:7s} {gate['script']} "
              f"{' '.join(gate['args'])}")
    return 0


def resolve_changed(arguments):
    if arguments.files is not None:
        return [item.strip() for item in arguments.files.split(",")
                if item.strip()]
    if arguments.changed is not None:
        return gate_manifest.changed_files(arguments.changed)
    return None


def command_plan(manifest, arguments):
    try:
        changed = resolve_changed(arguments)
    except RuntimeError as error:
        print(f"GATES: {error}", file=sys.stderr)
        return 2
    selected = gate_manifest.plan(manifest, arguments.tier, changed)
    if arguments.json:
        print(json.dumps({"tier": arguments.tier,
                          "changed": arguments.changed,
                          "files": arguments.files,
                          "gates": selected}, indent=2))
        return 0
    for item in selected:
        print(f"{item['gate']:22s} {item['reason']}")
    if not selected:
        print("GATES: no affected gates")
    return 0


def command_run(manifest, arguments):
    try:
        changed = resolve_changed(arguments)
    except RuntimeError as error:
        print(f"GATES: {error}", file=sys.stderr)
        return 2
    selected = gate_manifest.plan(manifest, arguments.tier, changed)
    names = [item["gate"] for item in selected]
    if not names:
        print("GATES: no affected gates")
        return 0
    command = [sys.executable, str(SUMMARY)]
    #ai: M1 — execute against the same manifest that selected the names.
    if arguments.manifest:
        command += ["--manifest", str(Path(arguments.manifest).resolve())]
    command += ["--only", ",".join(names)]
    if arguments.jobs > 1:
        command += ["--jobs", str(arguments.jobs)]
    if arguments.no_cache:
        command += ["--no-cache"]
    print(f"GATES: running {len(names)} gate(s)")
    result = subprocess.call(command, cwd=ROOT)
    if arguments.evidence:
        stamp = evidence_stamp.make_stamp("c",
            "PASS" if result == 0 else "FAIL",
            {"tier": arguments.tier, "jobs": arguments.jobs, "gates": names})
        evidence_stamp.write_stamp(arguments.evidence, stamp)
        print(f"EVIDENCE_STAMP: wrote {arguments.evidence}")
    return result


def command_explain(manifest, arguments):
    gate = gate_manifest.explain(manifest, arguments.gate)
    if gate is None:
        print(f"GATES: unknown gate {arguments.gate}", file=sys.stderr)
        return 1
    if arguments.json:
        print(json.dumps(gate, indent=2))
        return 0
    print(f"gate:        {gate['name']}")
    print(f"description: {gate['description']}")
    print(f"tier:        {','.join(gate['tiers'])}")
    print(f"group:       {gate['group']}")
    print(f"cache:       {gate['cache']}")
    print(f"timeout:     {gate['timeout']}")
    print(f"retries:     {gate['retries']}")
    print("command:     " + " ".join(gate["command"]))
    print("inputs:")
    for pattern in gate["inputs"]:
        print(f"  {pattern}")
    return 0


def command_cache(arguments):
    if arguments.show:
        return subprocess.call([sys.executable, str(ROOT / "scripts" /
                                                   "audit_cache.py"), "--show"],
                               cwd=ROOT)
    if arguments.clear:
        return subprocess.call([sys.executable, str(ROOT / "scripts" /
                                                   "audit_cache.py"), "--clear"],
                               cwd=ROOT)
    print("GATES: cache expects --show or --clear", file=sys.stderr)
    return 2


def main():
    parser = argparse.ArgumentParser(
        description="List, plan and run gX audit gates from gates.toml.")
    parser.add_argument("--manifest", help="manifest path (default: gates.toml)")
    sub = parser.add_subparsers(dest="command", required=True)

    list_parser = sub.add_parser("list", help="list manifest gates")
    list_parser.add_argument("--tier", default="all",
                             choices=gate_manifest.TIERS)
    list_parser.add_argument("--json", action="store_true")

    plan_parser = sub.add_parser("plan", help="plan gates for a tier/diff")
    plan_parser.add_argument("--tier", default="quick",
                             choices=gate_manifest.TIERS)
    plan_parser.add_argument("--changed", metavar="REF",
                             help="Git ref to diff against; omit for all")
    plan_parser.add_argument("--files", metavar="PATHS",
                             help="comma-separated changed paths")
    plan_parser.add_argument("--json", action="store_true")

    run_parser = sub.add_parser("run", help="run planned gates")
    run_parser.add_argument("--tier", default="quick",
                            choices=gate_manifest.TIERS)
    run_parser.add_argument("--changed", metavar="REF")
    run_parser.add_argument("--files", metavar="PATHS",
                            help="comma-separated changed paths")
    run_parser.add_argument("--jobs", type=int, default=1)
    run_parser.add_argument("--no-cache", action="store_true")
    run_parser.add_argument("--evidence", type=Path,
        help="Write an observational C gate evidence stamp.")

    explain_parser = sub.add_parser("explain", help="explain one gate")
    explain_parser.add_argument("gate")
    explain_parser.add_argument("--json", action="store_true")

    cache_parser = sub.add_parser("cache", help="show or clear the gate cache")
    cache_parser.add_argument("--show", action="store_true")
    cache_parser.add_argument("--clear", action="store_true")

    arguments = parser.parse_args()
    if arguments.command == "cache":
        return command_cache(arguments)
    manifest = load_or_error(arguments.manifest)
    if manifest is None:
        return 2
    if arguments.command == "list":
        return command_list(manifest, arguments)
    if arguments.command == "plan":
        return command_plan(manifest, arguments)
    if arguments.command == "run":
        return command_run(manifest, arguments)
    if arguments.command == "explain":
        return command_explain(manifest, arguments)
    return 2


if __name__ == "__main__":
    sys.exit(main())
