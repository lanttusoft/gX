#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I018 evidence stamps — observational UTC + source/input provenance for
#   Lean proof audits and C contract gate runs. Stamps are sidecars; nothing is
#   embedded in the binary, source archive or release artifact. Freshness is
#   decided by revision/input hashes, not by trusting the timestamp.

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = 1
REQUIRED_INPUTS = ("gates.toml", "proofs.lock.json", ".lean_baseline",
    ".lean_budget.json")
INPUTS = REQUIRED_INPUTS + ("scripts/proof_audit.py", "scripts/gates.py",
    "scripts/gate_manifest.py", "scripts/evidence_stamp.py")


def utc_now():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_utc(value):
    if not isinstance(value, str) or not value.endswith("Z"):
        raise ValueError(f"invalid UTC stamp: {value!r}")
    return datetime.fromisoformat(value[:-1] + "+00:00")


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def input_hashes(root=ROOT, paths=INPUTS):
    hashes = {}
    for name in paths:
        path = Path(root) / name
        if path.is_file():
            hashes[name] = sha256_file(path)
    return hashes


def _git(root, *args):
    try:
        return subprocess.run(["git", "-C", str(root), *args],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    except OSError:
        return None


def git_state(root=ROOT):
    revision = _git(root, "rev-parse", "HEAD")
    if revision is None or revision.returncode != 0:
        return {"revision": "unknown", "worktree": "unknown"}
    status = _git(root, "status", "--porcelain")
    diff = _git(root, "diff", "HEAD", "--binary")
    if status is None or diff is None:
        return {"revision": revision.stdout.decode("utf-8", "replace").strip(),
            "worktree": "unknown"}
    dirty = status.stdout + b"\0" + diff.stdout
    has_changes = bool(status.stdout.strip()) or bool(diff.stdout)
    worktree = hashlib.sha256(dirty).hexdigest() if has_changes else None
    return {"revision": revision.stdout.decode("utf-8", "replace").strip(),
        "worktree": worktree}


def make_stamp(kind, result, details=None, root=ROOT, utc=None, git=None,
        inputs=None):
    if kind not in ("lean", "c"):
        raise ValueError(f"invalid evidence kind: {kind!r}")
    stamp = {
        "schema": SCHEMA,
        "kind": kind,
        "utc": utc or utc_now(),
        "result": "PASS" if str(result).upper() == "PASS" else "FAIL",
        "git": git or git_state(root),
        "inputs": input_hashes(root) if inputs is None else dict(inputs),
        "details": dict(details or {}),
    }
    parse_utc(stamp["utc"])
    return stamp


def write_stamp(path, stamp):
    path = Path(path)
    if str(path) == "-":
        print(json.dumps(stamp, indent=2, sort_keys=True))
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(stamp, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")


def read_stamp(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def compare_stamps(lean, c):
    if lean.get("schema") != SCHEMA or c.get("schema") != SCHEMA:
        raise ValueError("unsupported evidence schema")
    if lean.get("kind") != "lean":
        raise ValueError("first stamp is not a Lean audit")
    if c.get("kind") != "c":
        raise ValueError("second stamp is not a C gate run")
    if lean.get("result") != "PASS" or c.get("result") != "PASS":
        raise ValueError("evidence check requires two PASS stamps")
    lean_git = lean.get("git", {})
    c_git = c.get("git", {})
    if not lean_git.get("revision") or not c_git.get("revision"):
        raise ValueError("stamp is missing a Git revision")
    if lean_git.get("revision") != c_git.get("revision"):
        raise ValueError(f"revision mismatch: {lean_git.get('revision')} "
            f"vs {c_git.get('revision')}")
    if lean_git.get("worktree") != c_git.get("worktree"):
        raise ValueError("worktree state mismatch")
    lean_inputs = lean.get("inputs", {})
    c_inputs = c.get("inputs", {})
    for name in REQUIRED_INPUTS:
        if name not in lean_inputs or name not in c_inputs:
            raise ValueError(f"required input missing from a stamp: {name}")
        if lean_inputs[name] != c_inputs[name]:
            raise ValueError(f"input mismatch: {name}")
    for name in sorted(set(lean_inputs) & set(c_inputs)):
        if lean_inputs[name] != c_inputs[name]:
            raise ValueError(f"input mismatch: {name}")
    lean_time = parse_utc(lean["utc"])
    c_time = parse_utc(c["utc"])
    return {
        "revision": lean_git["revision"],
        "worktree": lean_git.get("worktree"),
        "lean_utc": lean["utc"],
        "c_utc": c["utc"],
        "delta_seconds": (c_time - lean_time).total_seconds(),
        "inputs": {name: lean_inputs[name] for name in REQUIRED_INPUTS},
    }


def command_stamp(arguments):
    details = {}
    for item in arguments.detail or []:
        if "=" not in item:
            raise SystemExit(f"EVIDENCE_STAMP: invalid --detail {item!r}")
        key, value = item.split("=", 1)
        details[key] = value
    stamp = make_stamp(arguments.kind, arguments.result, details)
    write_stamp(arguments.output, stamp)
    if str(arguments.output) != "-":
        print(f"EVIDENCE_STAMP: wrote {arguments.output}")
    return 0


def command_check(arguments):
    result = compare_stamps(read_stamp(arguments.lean), read_stamp(arguments.c))
    if arguments.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(f"EVIDENCE_STAMP: PASS revision={result['revision']} "
            f"lean={result['lean_utc']} c={result['c_utc']} "
            f"delta={result['delta_seconds']:.0f}s")
    return 0


def command_show(arguments):
    print(json.dumps(read_stamp(arguments.path), indent=2, sort_keys=True))
    return 0


def self_test():
    with tempfile.TemporaryDirectory(prefix="gx_evidence_") as directory:
        root = Path(directory)
        inputs = {name: "a" * 64 for name in REQUIRED_INPUTS}
        git = {"revision": "test-revision", "worktree": None}
        lean = make_stamp("lean", "PASS", {"modules": 60}, root=root,
            utc="2026-01-01T00:00:00Z", git=git, inputs=inputs)
        c = make_stamp("c", "PASS", {"gates": 2}, root=root,
            utc="2026-01-01T00:00:10Z", git=git, inputs=inputs)
        result = compare_stamps(lean, c)
        if result["delta_seconds"] != 10.0:
            raise RuntimeError("self-test timestamp delta")
        mutations = (
            ("revision", lambda stamp: stamp["git"].update(
                revision="other-revision")),
            ("input", lambda stamp: stamp["inputs"].update(
                {"gates.toml": "b" * 64})),
            ("result", lambda stamp: stamp.update(result="FAIL")),
        )
        for label, mutate in mutations:
            broken = json.loads(json.dumps(c))
            mutate(broken)
            try:
                compare_stamps(lean, broken)
            except ValueError:
                continue
            raise RuntimeError(f"self-test accepted a {label} mismatch")
    print("EVIDENCE_STAMP: PASS self-test")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="Write and compare Lean/C audit evidence stamps.")
    sub = parser.add_subparsers(dest="command", required=True)
    stamp = sub.add_parser("stamp", help="write an evidence stamp")
    stamp.add_argument("--kind", required=True, choices=("lean", "c"))
    stamp.add_argument("--result", required=True, choices=("PASS", "FAIL"))
    stamp.add_argument("--output", required=True)
    stamp.add_argument("--detail", action="append")
    stamp.set_defaults(handler=command_stamp)
    check = sub.add_parser("check", help="compare Lean and C stamps")
    check.add_argument("--lean", required=True)
    check.add_argument("--c", dest="c_path", required=True)
    check.add_argument("--json", action="store_true")
    check.set_defaults(handler=command_check)
    show = sub.add_parser("show", help="print a stamp")
    show.add_argument("path")
    show.set_defaults(handler=command_show)
    test = sub.add_parser("self-test", help="exercise matching and rejection paths")
    test.set_defaults(handler=lambda _: self_test())
    arguments = parser.parse_args()
    if arguments.command == "check":
        arguments.c = arguments.c_path
    try:
        return arguments.handler(arguments)
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print(f"EVIDENCE_STAMP: FAIL — {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
