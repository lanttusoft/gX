#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I035 mutation/fault-injection pilot. Each entry is applied to a
#   disposable HEAD archive, never the working tree. Historical entries reverse
#   a fix commit; targeted mutations replace one exact source line/block. The
#   expected gate/test must fail, otherwise the mutation survives.
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

FORBIDDEN = {
    "bash", "sh", "dash", "zsh", "sudo", "su", "rm", "rmdir", "dd",
    "mkfs", "shutdown", "reboot", "poweroff", "kill", "pkill", "killall",
    "curl", "wget", "ssh", "scp", "sftp", "nc", "ncat", "iptables",
    "mount", "umount", "chmod", "chown", "systemctl", "crontab",
}

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "scripts" / "bug_injection_manifest.json"
DEFAULT_REPORT = ROOT / "build" / "bug-injection"


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
        process.communicate()
        raise
    return subprocess.CompletedProcess(command, process.returncode, output, "")


def git(root, *args):
    result = run(["git", "-C", str(root), *args], root, 60)
    if result.returncode:
        raise RuntimeError(result.stdout.strip()
                           or "git %s failed" % " ".join(args))
    return result.stdout


def classify(returncode, output, pattern, expect_crash=False):
    matched = re.search(pattern, output or "", re.M) is not None
    if expect_crash and returncode < 0:
        return "killed", matched
    if returncode != 0 and matched:
        return "killed", matched
    if returncode == 0 and not matched:
        return "survived", matched
    return "unexpected", matched


def validate_command(entry):
    command = entry["command"]
    name = Path(command[0]).name
    if name in FORBIDDEN:
        raise ValueError("%s: forbidden command %s" % (entry["id"], name))
    if command[0].startswith("/") or command[0].startswith(".."):
        raise ValueError("%s: command must be relative" % entry["id"])
    if name in ("python", "python3"):
        if len(command) < 2 or command[1].startswith("-"):
            raise ValueError("%s: python needs a relative script, not %s"
                             % (entry["id"], command[1] if len(command) > 1
                                else "(none)"))
        script = Path(command[1])
        if script.is_absolute() or ".." in script.parts:
            raise ValueError("%s: python script must be inside the tree"
                             % entry["id"])


def load_manifest(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if data.get("schema") != 1:
        raise ValueError("manifest schema must be 1")
    entries = data.get("entries")
    if not isinstance(entries, list) or not entries:
        raise ValueError("manifest needs a non-empty entries list")
    seen = set()
    for entry in entries:
        for key in ("id", "command", "expect_regex"):
            if key not in entry:
                raise ValueError("entry missing %s" % key)
        try:
            re.compile(entry["expect_regex"])
        except re.error as error:
            raise ValueError("%s: invalid expect_regex: %s"
                             % (entry["id"], error))
        if "expect_crash" in entry and not isinstance(entry["expect_crash"], bool):
            raise ValueError("%s: expect_crash must be a boolean" % entry["id"])
        if entry["id"] in seen:
            raise ValueError("duplicate entry id: %s" % entry["id"])
        seen.add(entry["id"])
        if not isinstance(entry["command"], list) or not entry["command"]:
            raise ValueError("%s: command must be a non-empty list"
                             % entry["id"])
        validate_command(entry)
        kind = entry.get("type", "replace")
        if kind == "reverse_commit":
            if not entry.get("fix_commit") or not entry.get("paths"):
                raise ValueError("%s: reverse_commit needs fix_commit and "
                                 "paths" % entry["id"])
        elif kind == "replace":
            if not entry.get("path") or "old" not in entry or "new" not in entry:
                raise ValueError("%s: replace needs path, old and new"
                                 % entry["id"])
        else:
            raise ValueError("%s: unknown type %s" % (entry["id"], kind))
    return data


def extract_base(base, destination):
    archive = subprocess.run(
        ["git", "-C", str(ROOT), "archive", base],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if archive.returncode:
        raise RuntimeError(archive.stderr.decode(errors="replace").strip())
    tar = subprocess.run(
        ["tar", "-x", "-C", str(destination)], input=archive.stdout,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if tar.returncode:
        raise RuntimeError(tar.stderr.decode(errors="replace").strip())


def inject(entry, tree, base, build_timeout):
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
                      "--fuzz=0"],
                     tree, 120, input_text=patch)
        if result.returncode:
            raise RuntimeError("reverse patch failed: %s"
                               % result.stdout[-800:])
        return "reversed %s (%s)" % (fix, ", ".join(entry["paths"]))
    path = tree / entry["path"]
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


def main():
    parser = argparse.ArgumentParser(
        description="Apply historical/targeted bugs to a disposable tree and "
                    "verify the expected gate/test fails.")
    parser.add_argument("--manifest", default=str(DEFAULT_MANIFEST))
    parser.add_argument("--only", help="comma-separated entry ids")
    parser.add_argument("--base", default="HEAD",
                        help="commit to archive (default HEAD)")
    parser.add_argument("--report", default=str(DEFAULT_REPORT))
    parser.add_argument("--keep", action="store_true",
                        help="keep the mutated tree for inspection")
    parser.add_argument("--allow-dirty", action="store_true",
                        help="allow tracked working-tree changes (the test "
                             "still runs against --base)")
    parser.add_argument("--list", action="store_true")
    arguments = parser.parse_args()

    try:
        manifest = load_manifest(arguments.manifest)
    except (OSError, ValueError) as error:
        print("BUG_INJECTION: manifest error: %s" % error, file=sys.stderr)
        return 2
    entries = manifest["entries"]
    if arguments.only:
        wanted = {item.strip() for item in arguments.only.split(",") if item.strip()}
        unknown = wanted - {entry["id"] for entry in entries}
        if unknown:
            print("BUG_INJECTION: unknown ids: %s" % ", ".join(sorted(unknown)),
                  file=sys.stderr)
            return 2
        entries = [entry for entry in entries if entry["id"] in wanted]
    if arguments.list:
        for entry in entries:
            print("%s\t%s" % (entry["id"], entry.get("description", "")))
        return 0 if entries else 2

    if not arguments.allow_dirty:
        dirty = git(ROOT, "status", "--porcelain", "--untracked-files=no")
        if dirty.strip():
            print("BUG_INJECTION: tracked changes present; commit/stash them "
                  "or pass --allow-dirty", file=sys.stderr)
            return 2
    base = git(ROOT, "rev-parse", arguments.base).strip()
    report_dir = Path(arguments.report)
    report_dir.mkdir(parents=True, exist_ok=True)
    results = []
    try:
        for entry in entries:
            entry_id = entry["id"]
            tree = Path(tempfile.mkdtemp(prefix="gx-bug-%s-" % entry_id,
                                         dir="/tmp/opencode"))
            build_log = report_dir / (entry_id + "-build.log")
            gate_log = report_dir / (entry_id + ".log")
            started = time.monotonic()
            status = "error"
            detail = ""
            returncode = None
            matched = False
            try:
                extract_base(base, tree)
                detail = inject(entry, tree, base,
                                entry.get("build_timeout", 1800))
                env = dict(os.environ)
                env["GX_NO_SETTINGS_SAVE"] = "1"
                env["GX_AUDIT_WINDOW"] = "hidden"
                build = run(["./build.sh", "Debug"], tree,
                            entry.get("build_timeout", 1800), env=env)
                build_log.write_text(build.stdout or "", encoding="utf-8")
                if build.returncode:
                    status = "build-failed"
                    detail += "; build failed"
                else:
                    command = entry["command"]
                    gate = run(command, tree, entry.get("timeout", 900),
                               env=env)
                    gate_log.write_text(gate.stdout or "", encoding="utf-8")
                    returncode = gate.returncode
                    status, matched = classify(
                        returncode, gate.stdout, entry["expect_regex"],
                        entry.get("expect_crash", False))
                    if returncode < 0:
                        detail += "; crashed with signal %d" % -returncode
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                status = "error"
                detail = str(error)
            elapsed = time.monotonic() - started
            results.append({
                "id": entry_id,
                "description": entry.get("description", ""),
                "type": entry.get("type", "replace"),
                "source": entry.get("source", "gX"),
                "note": entry.get("note", ""),
                "status": status,
                "returncode": returncode,
                "matched": matched,
                "crash": returncode is not None and returncode < 0,
                "detail": detail,
                "seconds": round(elapsed, 1),
                "build_log": str(build_log),
                "gate_log": str(gate_log),
            })
            if arguments.keep:
                results[-1]["tree"] = str(tree)
            else:
                shutil.rmtree(tree, ignore_errors=True)
            print("BUG_INJECTION: %-28s %-12s %5.1fs %s"
                  % (entry_id, status, elapsed, detail))
    finally:
        pass

    summary = {
        "schema": 1,
        "base": base,
        "manifest": str(Path(arguments.manifest)),
        "results": results,
        "killed": sum(1 for item in results if item["status"] == "killed"),
        "survived": sum(1 for item in results if item["status"] == "survived"),
        "unexpected": sum(1 for item in results
                          if item["status"] not in ("killed", "survived")),
    }
    (report_dir / "report.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    lines = ["# Bug-injection report", "",
             "Base: `%s`" % base, "",
             "| Entry | Source | Status | Seconds | Detail |",
             "| --- | --- | --- | ---: | --- |"]
    for item in results:
        lines.append("| `%s` | %s | **%s** | %.1f | %s |"
                     % (item["id"], item["source"], item["status"],
                        item["seconds"],
                        item["detail"].replace("|", "\\|")))
    lines += ["", "Killed: **%d**, survived: **%d**, unexpected: **%d**."
              % (summary["killed"], summary["survived"],
                 summary["unexpected"]), ""]
    (report_dir / "report.md").write_text("\n".join(lines), encoding="utf-8")
    print("BUG_INJECTION: %s" % (report_dir / "report.md"))
    if summary["survived"]:
        return 1
    if summary["unexpected"]:
        return 2
    return 0 if results else 2


if __name__ == "__main__":
    sys.exit(main())
