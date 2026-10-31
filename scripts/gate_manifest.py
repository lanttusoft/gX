#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: gate manifest loader and --changed planner. One gates.toml is the
#   source of truth for the audit gates; the planner maps a Git diff to the
#   gates whose declared inputs, group inputs or shared harness files changed.

import fnmatch
import subprocess
import sys
from pathlib import Path

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - Python < 3.11 fallback
    tomllib = None


ROOT = Path(__file__).resolve().parent.parent
MANIFEST_PATH = ROOT / "gates.toml"
TIERS = ("quick", "full", "compute", "all")
CACHE_POLICIES = ("content", "never", "ttl")


def load(path=None):
    path = Path(path) if path else MANIFEST_PATH
    if not path.is_file():
        return None
    if tomllib is None:
        raise RuntimeError("Python 3.11+ tomllib is required for gates.toml")
    data = tomllib.loads(path.read_text())
    defaults = data.get("defaults", {})
    groups = data.get("groups", {})
    shared = data.get("shared", {}).get("files", [])
    gates = []
    for name, entry in data.get("gate", {}).items():
        if "script" not in entry:
            raise ValueError(f"gate {name}: missing script")
        tiers = entry.get("tier", entry.get("tiers", ["quick"]))
        if isinstance(tiers, str):
            tiers = [tiers]
        group = entry.get("group", "render")
        if group not in groups:
            raise ValueError(f"gate {name}: unknown group {group}")
        cache = entry.get("cache", defaults.get("cache", "content"))
        if cache not in CACHE_POLICIES:
            raise ValueError(f"gate {name}: unknown cache policy {cache}")
        gates.append({
            "name": name,
            "script": entry["script"],
            "args": list(entry.get("args", [])),
            "tiers": tuple(tiers),
            "group": group,
            "cache": cache,
            "timeout": entry.get("timeout", defaults.get("timeout", 1200)),
            "retries": entry.get("retries", defaults.get("retries", 1)),
            "description": entry.get("description", ""),
            "inputs": list(entry.get("inputs", [])),
        })
    return {
        "path": path,
        "gates": gates,
        "groups": groups,
        "shared": list(shared),
        "defaults": defaults,
    }


def as_tuples(manifest):
    return [
        (gate["name"], gate["script"], gate["args"], gate["tiers"],
         gate["cache"])
        for gate in manifest["gates"]
    ]


def select_tier(manifest, tier):
    if tier == "all":
        return list(manifest["gates"])
    if tier == "full":
        return [gate for gate in manifest["gates"]
                if "quick" in gate["tiers"] or "full" in gate["tiers"]]
    return [gate for gate in manifest["gates"] if tier in gate["tiers"]]


def path_matches(path, pattern):
    if pattern.endswith("/"):
        return path == pattern[:-1] or path.startswith(pattern)
    if any(char in pattern for char in "*?["):
        return fnmatch.fnmatch(path, pattern)
    return path == pattern


def gate_inputs(manifest, gate):
    inputs = list(gate["inputs"])
    group = manifest["groups"].get(gate["group"], {})
    inputs.extend(group.get("inputs", []))
    inputs.append(f"scripts/{gate['script']}")
    return inputs


def changed_files(ref):
    result = subprocess.run(["git", "diff", "--name-only", ref],
                            cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "git diff failed")
    files = set(result.stdout.splitlines())
    untracked = subprocess.run(
        ["git", "ls-files", "--others", "--exclude-standard"],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if untracked.returncode == 0:
        files.update(untracked.stdout.splitlines())
    return sorted(path for path in files if path)


def plan(manifest, tier, changed=None):
    gates = select_tier(manifest, tier)
    if changed is None:
        return [{"gate": gate["name"], "reason": "tier selection"}
                for gate in gates]
    if not changed:
        return []
    shared_hits = [path for path in changed
                   if any(path_matches(path, pattern)
                          for pattern in manifest["shared"])]
    if shared_hits:
        return [{"gate": gate["name"],
                 "reason": f"shared harness: {shared_hits[0]}"}
                for gate in gates]
    selected = {}
    all_inputs = {}
    for gate in gates:
        inputs = gate_inputs(manifest, gate)
        all_inputs[gate["name"]] = inputs
        hits = [path for path in changed
                if any(path_matches(path, pattern) for pattern in inputs)]
        if hits:
            selected[gate["name"]] = f"input: {hits[0]}"
    build_inputs = manifest["defaults"].get("build_inputs", [])
    unmapped = [
        path for path in changed
        if any(path_matches(path, pattern) for pattern in build_inputs)
        and not any(any(path_matches(path, pattern) for pattern in inputs)
                    for inputs in all_inputs.values())
    ]
    if unmapped:
        for gate in gates:
            selected.setdefault(gate["name"],
                                f"unmapped build input: {unmapped[0]}")
    return [{"gate": name, "reason": reason}
            for name, reason in sorted(selected.items())]


def explain(manifest, name):
    for gate in manifest["gates"]:
        if gate["name"] == name:
            return {
                **gate,
                "inputs": gate_inputs(manifest, gate),
                "command": ["python3", f"scripts/{gate['script']}"]
                + gate["args"],
            }
    return None


def main():
    print("gate_manifest is a library; use scripts/gates.py", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
