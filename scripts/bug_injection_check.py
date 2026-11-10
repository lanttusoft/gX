#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I035 mutation/fault-injection pilot. Each entry is applied to a
#   disposable HEAD archive, never the working tree. Historical entries reverse
#   a fix commit; targeted mutations replace one exact source line/block. The
#   named gate/test must pass on the unmutated baseline and fail on the mutant.
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

try:
    import resource
except ImportError:  # pragma: no cover - POSIX is the supported host
    resource = None

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "scripts" / "bug_injection_manifest.json"
DEFAULT_REPORT = ROOT / "build" / "bug-injection"
ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
FIX_RE = re.compile(r"^[0-9a-fA-F]{4,64}$")
ALLOWED = {"./build.sh", "./build/gX_build", "python", "python3"}
LOG_LIMIT = 1_000_000


def child_setup():
    if resource is not None:
        try:
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        except (OSError, ValueError):
            pass


def run(command, cwd, timeout, input_text=None, env=None):
    process = subprocess.Popen(
        command, cwd=str(cwd), stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        env=env, start_new_session=True, preexec_fn=child_setup)
    try:
        output, _ = process.communicate(input=input_text, timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except OSError:
            pass
        try:
            process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
        raise
    return subprocess.CompletedProcess(command, process.returncode,
                                       output or "", "")


def git(root, *args):
    result = run(["git", "-C", str(root), *args], root, 60)
    if result.returncode:
        raise RuntimeError(result.stdout.strip()
                           or "git %s failed" % " ".join(args))
    return result.stdout


def is_crash(returncode):
    return returncode < 0 or returncode >= 128


def classify(returncode, output, pattern, expect_crash=False):
    matched = re.search(pattern, output or "", re.M) is not None
    if expect_crash and is_crash(returncode):
        return "killed", matched
    if returncode != 0 and matched:
        return "killed", matched
    if returncode == 0 and not matched:
        return "survived", matched
    return "unexpected", matched


def baseline_ok(returncode, output, pattern):
    return returncode == 0 and re.search(pattern, output or "", re.M) is None


def safe_relative(value, label):
    if not isinstance(value, str) or not value:
        raise ValueError("%s must be a non-empty string" % label)
    path = Path(value)
    if path.is_absolute() or any(part == ".." for part in path.parts):
        raise ValueError("%s must stay inside the tree: %s" % (label, value))
    return value


def validate_command(entry):
    command = entry["command"]
    if not all(isinstance(item, str) for item in command):
        raise ValueError("%s: command items must be strings" % entry["id"])
    first = command[0]
    if first not in ALLOWED:
        raise ValueError("%s: command not allowed: %s" % (entry["id"], first))
    for item in command:
        if item.startswith("/") or ".." in Path(item).parts:
            raise ValueError("%s: command path escapes the tree: %s"
                             % (entry["id"], item))
    if first in ("python", "python3"):
        if len(command) < 2 or command[1].startswith("-"):
            raise ValueError("%s: python needs a relative script" % entry["id"])
        script = safe_relative(command[1], "%s script" % entry["id"])
        if not script.startswith("scripts/") or not script.endswith(".py"):
            raise ValueError("%s: python script must be scripts/*.py"
                             % entry["id"])


def validate_manifest(data):
    if not isinstance(data, dict) or data.get("schema") != 1:
        raise ValueError("manifest schema must be 1")
    entries = data.get("entries")
    if not isinstance(entries, list) or not entries:
        raise ValueError("manifest needs a non-empty entries list")
    seen = set()
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError("entry must be an object")
        for key in ("id", "command", "expect_regex"):
            if key not in entry:
                raise ValueError("entry missing %s" % key)
        if not isinstance(entry["id"], str) or not ID_RE.match(entry["id"]):
            raise ValueError("invalid entry id: %r" % entry["id"])
        if entry["id"] in seen:
            raise ValueError("duplicate entry id: %s" % entry["id"])
        seen.add(entry["id"])
        if (not isinstance(entry["command"], list) or not entry["command"]):
            raise ValueError("%s: command must be a non-empty list"
                             % entry["id"])
        validate_command(entry)
        try:
            re.compile(entry["expect_regex"])
        except re.error as error:
            raise ValueError("%s: invalid expect_regex: %s"
                             % (entry["id"], error))
        if ("expect_crash" in entry
                and not isinstance(entry["expect_crash"], bool)):
            raise ValueError("%s: expect_crash must be a boolean" % entry["id"])
        if ("intended" in entry and not isinstance(entry["intended"], bool)):
            raise ValueError("%s: intended must be a boolean" % entry["id"])
        if "timeout" in entry:
            timeout = entry["timeout"]
            if (not isinstance(timeout, (int, float))
                    or isinstance(timeout, bool) or timeout <= 0):
                raise ValueError("%s: timeout must be positive" % entry["id"])
        kind = entry.get("type", "replace")
        if kind == "reverse_commit":
            fix = entry.get("fix_commit")
            if not isinstance(fix, str) or not FIX_RE.match(fix):
                raise ValueError("%s: invalid fix_commit" % entry["id"])
            paths = entry.get("paths")
            if not isinstance(paths, list) or not paths:
                raise ValueError("%s: paths must be a non-empty list"
                                 % entry["id"])
            for path in paths:
                safe_relative(path, "%s paths" % entry["id"])
        elif kind == "replace":
            safe_relative(entry.get("path"), "%s path" % entry["id"])
            if not isinstance(entry.get("old"), str) or not entry["old"]:
                raise ValueError("%s: old must be a non-empty string"
                                 % entry["id"])
            if not isinstance(entry.get("new"), str):
                raise ValueError("%s: new must be a string" % entry["id"])
        else:
            raise ValueError("%s: unknown type %s" % (entry["id"], kind))
    return data


def load_manifest(path):
    return validate_manifest(
        json.loads(Path(path).read_text(encoding="utf-8")))


def extract_base(base, destination):
    archive = subprocess.run(
        ["git", "-C", str(ROOT), "archive", base],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120, check=False)
    if archive.returncode:
        raise RuntimeError(archive.stderr.decode(errors="replace").strip())
    tar = subprocess.run(
        ["tar", "-x", "-C", str(destination)], input=archive.stdout,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120, check=False)
    if tar.returncode:
        raise RuntimeError(tar.stderr.decode(errors="replace").strip())


def resolve_under(root, relative, label):
    root = root.resolve()
    target = (root / safe_relative(relative, label)).resolve()
    if target != root and root not in target.parents:
        raise ValueError("%s escapes the disposable tree: %s"
                         % (label, relative))
    return target


def inject(entry, tree, base):
    kind = entry.get("type", "replace")
    if kind == "reverse_commit":
        fix = entry["fix_commit"]
        if run(["git", "-C", str(ROOT), "merge-base", "--is-ancestor",
                fix, base], ROOT, 60).returncode:
            raise RuntimeError("%s is not an ancestor of %s" % (fix, base))
        patch = git(ROOT, "diff", fix + "^", fix, "--", *entry["paths"])
        if not patch.strip():
            raise RuntimeError("fix commit %s has no diff for %s"
                               % (fix, ", ".join(entry["paths"])))
        result = run(["patch", "-p1", "-R", "--batch", "--forward",
                      "--fuzz=0"], tree, 120, input_text=patch)
        if result.returncode:
            raise RuntimeError("reverse patch failed: %s"
                               % result.stdout[-800:])
        return "reversed %s (%s)" % (fix, ", ".join(entry["paths"]))
    path = resolve_under(tree, entry["path"], "%s path" % entry["id"])
    if not path.is_file():
        raise RuntimeError("missing source file: %s" % entry["path"])
    text = path.read_text(encoding="utf-8")
    count = text.count(entry["old"])
    if count != 1:
        raise RuntimeError("%s: old text matched %d times (need 1)"
                           % (entry["id"], count))
    path.write_text(text.replace(entry["old"], entry["new"], 1),
                    encoding="utf-8")
    return "replaced one block in %s" % entry["path"]


def write_log(path, text):
    text = text or ""
    path.write_text(text[-LOG_LIMIT:], encoding="utf-8")


def execute(tree, command, timeout, env):
    build = run(["./build.sh", "Debug"], tree,
                timeout.get("build", 1800), env=env)
    if build.returncode:
        return "build-failed", build, None
    gate = run(command, tree, timeout.get("gate", 900), env=env)
    return None, build, gate


def cleanup(path):
    try:
        shutil.rmtree(path)
        return ""
    except OSError as error:
        return str(error)


def main():
    parser = argparse.ArgumentParser(
        description="Apply historical/targeted bugs to a disposable tree and "
                    "verify the expected gate/test passes on the baseline and "
                    "fails on the mutant.")
    parser.add_argument("--manifest", default=str(DEFAULT_MANIFEST))
    parser.add_argument("--only", help="comma-separated entry ids")
    parser.add_argument("--base", default="HEAD",
                        help="commit to archive (default HEAD)")
    parser.add_argument("--report", default=str(DEFAULT_REPORT))
    parser.add_argument("--keep", action="store_true",
                        help="keep both baseline and mutant trees")
    parser.add_argument("--skip-baseline", action="store_true",
                        help="skip the unmutated control run (faster, weaker)")
    parser.add_argument("--allow-dirty", action="store_true",
                        help="allow tracked working-tree changes (the test "
                             "still runs against --base)")
    parser.add_argument("--list", action="store_true")
    arguments = parser.parse_args()

    try:
        manifest = load_manifest(arguments.manifest)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print("BUG_INJECTION: manifest error: %s" % error, file=sys.stderr)
        return 2
    entries = manifest["entries"]
    if arguments.only:
        wanted = {item.strip() for item in arguments.only.split(",")
                  if item.strip()}
        unknown = wanted - {entry["id"] for entry in entries}
        if unknown:
            print("BUG_INJECTION: unknown ids: %s" % ", ".join(sorted(unknown)),
                  file=sys.stderr)
            return 2
        entries = [entry for entry in entries if entry["id"] in wanted]
    if arguments.list:
        for entry in entries:
            print("%s\t%s\t%s" % (
                entry["id"],
                "intended" if entry.get("intended", True) else "indirect",
                entry.get("description", "")))
        return 0 if entries else 2

    if not arguments.allow_dirty:
        dirty = git(ROOT, "status", "--porcelain", "--untracked-files=no")
        if dirty.strip():
            print("BUG_INJECTION: tracked changes present; commit/stash them "
                  "or pass --allow-dirty", file=sys.stderr)
            return 2
    base = git(ROOT, "rev-parse", arguments.base).strip()
    report_dir = Path(arguments.report).resolve()
    report_dir.mkdir(parents=True, exist_ok=True)
    results = []
    for entry in entries:
        entry_id = entry["id"]
        tree = Path(tempfile.mkdtemp(prefix="gx-bug-%s-" % entry_id,
                                     dir="/tmp/opencode"))
        baseline_tree = Path(tempfile.mkdtemp(
            prefix="gx-bug-%s-base-" % entry_id, dir="/tmp/opencode"))
        build_log = report_dir / (entry_id + "-build.log")
        gate_log = report_dir / (entry_id + ".log")
        base_build_log = report_dir / (entry_id + "-baseline-build.log")
        base_gate_log = report_dir / (entry_id + "-baseline.log")
        started = time.monotonic()
        status = "error"
        detail = ""
        returncode = None
        baseline_returncode = None
        matched = False
        crash = False
        cleanup_note = ""
        env = dict(os.environ)
        env["GX_NO_SETTINGS_SAVE"] = "1"
        env["GX_AUDIT_WINDOW"] = "hidden"
        timeouts = {
            "build": entry.get("build_timeout", 1800),
            "gate": entry.get("timeout", 900),
        }
        try:
            if not arguments.skip_baseline:
                extract_base(base, baseline_tree)
                failure, build, gate = execute(
                    baseline_tree, entry["command"], timeouts, env)
                write_log(base_build_log, build.stdout if build else "")
                write_log(base_gate_log, gate.stdout if gate else "")
                baseline_returncode = gate.returncode if gate else None
                if failure:
                    status = "baseline-build-failed"
                    detail = "baseline build failed"
                elif not baseline_ok(baseline_returncode, gate.stdout,
                                     entry["expect_regex"]):
                    status = "baseline-failed"
                    detail = ("baseline command did not pass cleanly "
                              "(rc=%s)" % baseline_returncode)
            if status == "error":
                extract_base(base, tree)
                detail = inject(entry, tree, base)
                failure, build, gate = execute(
                    tree, entry["command"], timeouts, env)
                write_log(build_log, build.stdout if build else "")
                write_log(gate_log, gate.stdout if gate else "")
                if failure:
                    status = "build-failed"
                    detail += "; build failed"
                else:
                    returncode = gate.returncode
                    crash = is_crash(returncode)
                    status, matched = classify(
                        returncode, gate.stdout, entry["expect_regex"],
                        entry.get("expect_crash", False))
                    if crash:
                        detail += "; exit/signal %d" % returncode
                    elif returncode is not None:
                        detail += "; rc=%d" % returncode
                    if status == "killed" and not entry.get("intended", True):
                        status = "killed-indirect"
        except (OSError, RuntimeError, ValueError,
                subprocess.TimeoutExpired) as error:
            status = "error"
            detail = str(error)
        elapsed = time.monotonic() - started
        result = {
            "id": entry_id,
            "description": entry.get("description", ""),
            "type": entry.get("type", "replace"),
            "source": entry.get("source", "gX"),
            "note": entry.get("note", ""),
            "intended": entry.get("intended", True),
            "status": status,
            "returncode": returncode,
            "baseline_returncode": baseline_returncode,
            "matched": matched,
            "crash": crash,
            "detail": detail,
            "seconds": round(elapsed, 1),
            "build_log": str(build_log),
            "gate_log": str(gate_log),
            "baseline_build_log": str(base_build_log),
            "baseline_gate_log": str(base_gate_log),
        }
        if arguments.keep:
            result["tree"] = str(tree)
            result["baseline_tree"] = str(baseline_tree)
            print("BUG_INJECTION: kept baseline=%s mutant=%s"
                  % (baseline_tree, tree))
        else:
            cleanup_note = "; ".join(filter(None, [
                cleanup(tree), cleanup(baseline_tree)]))
            if cleanup_note:
                result["detail"] += "; cleanup failed: " + cleanup_note
        results.append(result)
        print("BUG_INJECTION: %-28s %-18s %5.1fs %s"
              % (entry_id, status, elapsed, result["detail"]))

    summary = {
        "schema": 1,
        "base": base,
        "manifest": str(Path(arguments.manifest)),
        "results": results,
        "killed": sum(1 for item in results if item["status"] == "killed"),
        "killed_indirect": sum(1 for item in results
                               if item["status"] == "killed-indirect"),
        "survived": sum(1 for item in results if item["status"] == "survived"),
        "baseline_failed": sum(1 for item in results
                               if item["status"].startswith("baseline-")),
        "unexpected": sum(1 for item in results
                          if item["status"] in
                          ("unexpected", "error", "build-failed")),
    }
    (report_dir / "report.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    lines = ["# Bug-injection report", "",
             "Base: `%s`" % base, "",
             "| Entry | Source | Intended | Status | RC | Crash | Baseline | "
             "Seconds | Detail |",
             "| --- | --- | --- | --- | ---: | --- | --- | ---: | --- |"]
    for item in results:
        lines.append("| `%s` | %s | %s | **%s** | %s | %s | %s | %.1f | %s |"
                     % (item["id"], item["source"],
                        "yes" if item["intended"] else "no",
                        item["status"],
                        "null" if item["returncode"] is None
                        else item["returncode"],
                        "yes" if item["crash"] else "no",
                        "null" if item["baseline_returncode"] is None
                        else item["baseline_returncode"],
                        item["seconds"],
                        item["detail"].replace("|", "\\|")))
    lines += ["", "Killed: **%d**, killed-indirect: **%d**, survived: **%d**, "
              "baseline-failed: **%d**, unexpected: **%d**."
              % (summary["killed"], summary["killed_indirect"],
                 summary["survived"], summary["baseline_failed"],
                 summary["unexpected"]), ""]
    (report_dir / "report.md").write_text("\n".join(lines), encoding="utf-8")
    print("BUG_INJECTION: %s" % (report_dir / "report.md"))
    if summary["survived"]:
        return 1
    if summary["unexpected"] or summary["baseline_failed"]:
        return 2
    return 0 if results else 2


if __name__ == "__main__":
    sys.exit(main())
