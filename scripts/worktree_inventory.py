#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: read-only Git inventory for cleanup planning; writes only an explicit local
#   report under the common Git directory. Never removes worktrees or refs.
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def git(root, *args):
    result = subprocess.run(["git", "-c", "core.fsmonitor=false", "-C", str(root), *args],
        env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"}, encoding="utf-8",
        errors="surrogateescape", stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=60)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or f"git {args[0]} failed")
    return result.stdout


def parse_worktrees(text):
    rows = []
    current = None
    for field in text.split("\0"):
        if not field:
            continue
        key, _, value = field.partition(" ")
        if key == "worktree":
            current = {"path": value}
            rows.append(current)
        elif current is None:
            raise ValueError("worktree record has no path")
        else:
            current[key] = value
    return rows


def distance(root, base, tip):
    behind, unique = git(root, "rev-list", "--left-right", "--count",
        f"{base}...{tip}", "--").split()
    return {"behind_base": int(behind), "unique_commits": int(unique)}


def inventory(root, base):
    root = Path(git(root, "rev-parse", "--show-toplevel").strip())
    base_commit = git(root, "rev-parse", "--verify", f"{base}^{{commit}}").strip()
    common = Path(git(root, "rev-parse", "--git-common-dir").strip())
    if not common.is_absolute():
        common = root / common
    worktrees = parse_worktrees(git(root, "worktree", "list", "--porcelain", "-z"))
    branches = []
    refs = git(root, "for-each-ref",
        "--format=%(refname)%09%(objectname)%09%(upstream:short)", "refs/heads")
    for line in refs.splitlines():
        ref, head, upstream = line.split("\t")
        branches.append({"ref": ref, "branch": ref.removeprefix("refs/heads/"),
            "head": head, "upstream": upstream or None,
            "worktrees": [row["path"] for row in worktrees if row.get("branch") == ref],
            "cleanup_decision": "unreviewed", **distance(root, base_commit, head)})
    for row in worktrees:
        path = Path(row["path"])
        row["current"] = path.resolve() == root.resolve()
        row["cleanup_decision"] = "unreviewed"
        if "bare" in row:
            row["state"] = "bare"
        elif not path.is_dir():
            row["state"] = "missing"
        else:
            try:
                actual = Path(git(path, "rev-parse", "--show-toplevel").strip())
                if actual.resolve() != path.resolve():
                    raise ValueError("registered path is no longer its own worktree")
                actual_common = Path(git(path, "rev-parse", "--git-common-dir").strip())
                if not actual_common.is_absolute():
                    actual_common = path / actual_common
                if actual_common.resolve() != common.resolve():
                    raise ValueError("registered path belongs to another Git repository")
                row["state"] = "dirty" if git(path, "status", "--porcelain=v1", "-z",
                    "--untracked-files=normal") else "clean"
            except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
                row["state"] = "unavailable"
                row["error"] = str(error)
        if row.get("HEAD"):
            try:
                row.update(distance(root, base_commit, row["HEAD"]))
            except (RuntimeError, subprocess.SubprocessError) as error:
                row["distance_error"] = str(error)
    return {"schema": 1, "observed_at": datetime.now(timezone.utc).isoformat(),
        "base": base, "base_commit": base_commit, "common_dir": str(common.resolve()),
        "worktrees": worktrees, "branches": branches}


def cell(value):
    return str(value).replace("|", "\\|").replace("\n", " ").replace("\r", " ")


def markdown(data):
    lines = ["# Local worktree and branch inventory", "",
        f"Observed: {data['observed_at']}",
        f"Compared with: {data['base']} ({data['base_commit'][:12]})", "",
        "Snapshot only; no fetch and no cleanup authorization. All cleanup decisions are unreviewed.",
        "Clean excludes ignored files. Contained commits do not prove cherry-pick equivalence,",
        "publication, backups, inactive sessions or permission to delete. Check notes.md as well.", "",
        "## Worktrees", "",
        "| Path | Actual branch / HEAD | State / flags | Unique commits vs base | Behind base |",
        "| --- | --- | --- | ---: | ---: |"]
    for row in data["worktrees"]:
        branch = row.get("branch", "detached").removeprefix("refs/heads/")
        flags = [row["state"]]
        flags += [key for key in ("current", "locked", "prunable", "bare")
                  if key in row and (key != "current" or row[key])]
        values = [row["path"], f"{branch} @ {row.get('HEAD', '?')[:12]}",
            ", ".join(flags), row.get("unique_commits", "?"), row.get("behind_base", "?")]
        lines.append("| " + " | ".join(cell(value) for value in values) + " |")
    lines += ["", "## All local branches (including those without a worktree)", "",
        "| Branch | HEAD | Upstream | Worktree count | Unique commits vs base | Behind base |",
        "| --- | --- | --- | ---: | ---: | ---: |"]
    for row in data["branches"]:
        values = [row["branch"], row["head"][:12], row["upstream"] or "none",
            len(row["worktrees"]), row["unique_commits"], row["behind_base"]]
        lines.append("| " + " | ".join(cell(value) for value in values) + " |")
    return "\n".join(lines) + "\n"


def atomic_write(path, text):
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", errors="backslashreplace",
            dir=path.parent, delete=False) as handle:
        temporary = Path(handle.name)
        try:
            handle.write(text)
            handle.close()
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)


def write_inventory(data):
    directory = Path(data["common_dir"]) / "gx-workstreams"
    directory.mkdir(exist_ok=True)
    atomic_write(directory / "inventory.json", json.dumps(data, indent=2) + "\n")
    atomic_write(directory / "inventory.md", markdown(data))
    notes = directory / "notes.md"
    try:
        with notes.open("x", encoding="utf-8") as handle:
            handle.write("# Local workstream notes\n\n"
                "Human-maintained; inventory refresh never overwrites this file.\n"
                "Unlisted branches/worktrees have unknown purpose and unreviewed cleanup status.\n"
                "Retain entries after removal, with the date, explicit approval and disposition.\n\n"
                "| Branch / worktree | Purpose / related idea IDs | Lifecycle / verification | "
                "Integration evidence / remaining work | Cleanup decision / last review |\n"
                "| --- | --- | --- | --- | --- |\n")
    except FileExistsError:
        pass
    return directory


def main():
    parser = argparse.ArgumentParser(description="Inventory Git worktrees/branches; never clean them up.")
    parser.add_argument("--base", required=True, help="explicit local integration ref, e.g. main")
    parser.add_argument("--write", action="store_true", help="refresh shared local reports in the common Git directory")
    args = parser.parse_args()
    try:
        data = inventory(Path.cwd(), args.base)
        if args.write:
            directory = write_inventory(data)
            print(f"WORKTREE_INVENTORY: {len(data['worktrees'])} worktrees, "
                  f"{len(data['branches'])} branches; reports in {directory}; no cleanup performed")
        else:
            print(markdown(data), end="")
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"WORKTREE_INVENTORY: FAIL — {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
