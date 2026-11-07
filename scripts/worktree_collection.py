#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I022 safe collection workflow. Read-only observation plus explicit
#   archive output; prune-plan prints exact commands but never executes them.
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

import agent_bus
import worktree_inventory as inv


def git(root, *args):
    result = subprocess.run(
        ["git", "-c", "core.fsmonitor=false", "-C", str(root), *args],
        env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"},
        encoding="utf-8", errors="surrogateescape",
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or f"git {args[0]} failed")
    return result.stdout


def split_z(value):
    return [item for item in value.split("\0") if item]


def same_path(left, right):
    try:
        return Path(left).resolve() == Path(right).resolve()
    except OSError:
        return False


def load_claims(common):
    override = os.environ.get("GX_AGENT_BUS")
    root = Path(override).expanduser().resolve() if override else \
        (Path(common) / "gx-agent-bus").resolve()
    claims, _messages = agent_bus.read_state(root)
    return agent_bus.active_claims(claims, agent_bus.utc_now())


def claim_matches(claim, branch, worktrees):
    if branch and claim.get("branch") == branch:
        return True
    return any(same_path(claim.get("worktree", ""), path) for path in worktrees)


def annotate(data, claims):
    for branch in data["branches"]:
        branch["active"] = any(
            claim_matches(claim, branch["branch"], branch["worktrees"])
            for claim in claims)
        branch["integration"] = "integrated" if branch["unique_commits"] == 0 \
            else "unmerged"
    for worktree in data["worktrees"]:
        branch = worktree.get("branch", "").removeprefix("refs/heads/")
        worktree["active"] = any(
            claim_matches(claim, branch, [worktree["path"]])
            for claim in claims)
        worktree["detached"] = "branch" not in worktree
    groups = {
        "integrated": [row["branch"] for row in data["branches"]
                       if row["integration"] == "integrated"],
        "unmerged": [row["branch"] for row in data["branches"]
                     if row["integration"] == "unmerged"],
        "dirty": [row["path"] for row in data["worktrees"]
                  if row["state"] == "dirty"],
        "missing": [row["path"] for row in data["worktrees"]
                    if row["state"] in ("missing", "unavailable")],
        "locked": [row["path"] for row in data["worktrees"]
                   if row.get("locked")],
        "active": sorted({claim["session"] for claim in claims}),
        "detached": [row["path"] for row in data["worktrees"]
                     if row["detached"]],
    }
    data["groups"] = groups
    data["active_claims"] = claims
    return data


def worktree_files(path):
    path = Path(path)
    status = git(path, "status", "--short", "--untracked-files=all")
    untracked = sorted(split_z(git(
        path, "ls-files", "--others", "--exclude-standard", "-z")))
    ignored = sorted(split_z(git(
        path, "ls-files", "--others", "--ignored", "--exclude-standard", "-z")))
    return {"status": status, "untracked": untracked, "ignored": ignored}


def file_size(path, relative):
    try:
        return (Path(path) / relative).lstat().st_size
    except OSError:
        return None


def listing_text(path, relative_paths):
    lines = []
    for relative in sorted(relative_paths):
        size = file_size(path, relative)
        escaped = relative.replace("\\", "\\\\").replace("\t", "\\t") \
            .replace("\n", "\\n").replace("\r", "\\r")
        lines.append(f"{size if size is not None else '?'}\t{escaped}\n")
    return "".join(lines)


def safe_component(value):
    value = re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("._")
    return value or "detached"


def worktree_name(path):
    digest = hashlib.sha256(str(path).encode("utf-8", "surrogateescape")).hexdigest()[:8]
    return f"{safe_component(Path(path).name)}-{digest}"


def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", errors="backslashreplace")


def bundle_outputs(root, data, output):
    bundles = []
    branches = [row for row in data["branches"]]
    if branches:
        target = output / "repository.bundle"
        git(root, "bundle", "create", str(target), "--branches", "--tags")
        verify = git(root, "bundle", "verify", str(target))
        bundles.append({"file": target.name, "kind": "repository",
                        "heads": branches and [row["branch"] for row in branches],
                        "verify": verify.strip()})
    for worktree in data["worktrees"]:
        if not worktree["detached"] or worktree["state"] in ("missing", "unavailable"):
            continue
        if "HEAD" not in worktree:
            continue
        target = output / f"detached-{worktree_name(worktree['path'])}.bundle"
        git(worktree["path"], "bundle", "create", str(target), "HEAD")
        verify = git(worktree["path"], "bundle", "verify", str(target))
        bundles.append({"file": target.name, "kind": "detached",
                        "worktree": worktree["path"], "head": worktree["HEAD"],
                        "verify": verify.strip()})
    return bundles


def patch_outputs(data, output):
    patches = []
    for worktree in data["worktrees"]:
        if worktree["state"] in ("missing", "unavailable"):
            continue
        name = worktree_name(worktree["path"])
        for kind, args in (("tracked", ("diff", "--binary", "--no-ext-diff", "--no-color")),
                           ("cached", ("diff", "--cached", "--binary",
                                       "--no-ext-diff", "--no-color"))):
            text = git(worktree["path"], *args)
            if not text:
                continue
            target = output / f"{name}-{kind}.patch"
            write(target, text)
            patches.append({"file": target.name, "worktree": worktree["path"],
                            "kind": kind})
        files = worktree_files(worktree["path"])
        for kind in ("untracked", "ignored"):
            if not files[kind]:
                continue
            target = output / f"{name}-{kind}.txt"
            write(target, listing_text(worktree["path"], files[kind]))
            patches.append({"file": target.name, "worktree": worktree["path"],
                            "kind": kind})
        if files["status"]:
            target = output / f"{name}-status.txt"
            write(target, files["status"])
            patches.append({"file": target.name, "worktree": worktree["path"],
                            "kind": "status"})
    return patches


def checksums(output):
    lines = []
    for path in sorted(output.rglob("*")):
        if not path.is_file() or path.name == "SHA256SUMS":
            continue
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        lines.append(f"{digest}  {path.relative_to(output).as_posix()}\n")
    write(output / "SHA256SUMS", "".join(lines))
    return len(lines)


def manifest_markdown(manifest):
    lines = ["# Worktree collection archive", "",
             f"Observed: {manifest['observed_at']}",
             f"Base: {manifest['base']} ({manifest['base_commit'][:12]})", "",
             "Plan-only archive; no worktree, branch or ref was removed.", "",
             "## Groups", ""]
    for key in ("integrated", "unmerged", "dirty", "missing", "locked",
                "active", "detached"):
        lines.append(f"- **{key}**: {len(manifest['groups'][key])}")
    lines += ["", "## Bundles", ""]
    for bundle in manifest["bundles"]:
        lines.append(f"- `{bundle['file']}` — {bundle['kind']} — verify: "
                     f"{'ok' if 'okay' in bundle['verify'] else 'FAIL'}")
    lines += ["", "## Local files", ""]
    for item in manifest["files"]:
        lines.append(f"- `{item['file']}` — {item['kind']} — {item['worktree']}")
    lines += ["", "All paths and machine metadata are local-only. Review "
              "`inventory.json`, `notes.md` and the listed patches before any "
              "cleanup; this archive does not authorize deletion.", ""]
    return "\n".join(lines)


def archive(root, base, output):
    output = Path(output).expanduser().resolve()
    if output.exists() and any(output.iterdir()):
        raise RuntimeError(f"output directory is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    data = inv.inventory(root, base)
    claims = load_claims(Path(data["common_dir"]))
    annotate(data, claims)
    write(output / "inventory.json", json.dumps(data, indent=2) + "\n")
    write(output / "inventory.md", inv.markdown(data))
    write(output / "active-claims.json", json.dumps(claims, indent=2) + "\n")
    notes = Path(data["common_dir"]) / "gx-workstreams" / "notes.md"
    if notes.is_file():
        write(output / "notes.md", notes.read_text(encoding="utf-8",
                                                    errors="backslashreplace"))
    else:
        write(output / "notes.md", "# Local workstream notes\n\nNo shared notes found.\n")
    bundles = bundle_outputs(root, data, output)
    files = patch_outputs(data, output)
    manifest = {
        "schema": 1,
        "observed_at": datetime.now(timezone.utc).isoformat(),
        "base": base,
        "base_commit": data["base_commit"],
        "common_dir": data["common_dir"],
        "groups": data["groups"],
        "bundles": bundles,
        "files": [{"file": item["file"], "kind": "bundle", "worktree": ""}
                  for item in bundles] + files,
        "active_claims": claims,
        "worktrees": data["worktrees"],
        "branches": data["branches"],
    }
    write(output / "manifest.json", json.dumps(manifest, indent=2) + "\n")
    write(output / "manifest.md", manifest_markdown(manifest))
    count = checksums(output)
    print(f"WORKTREE_COLLECTION: archived {len(bundles)} bundle(s), "
          f"{len(files)} local file(s), {count} checksum(s) in {output}; "
          f"no cleanup performed")
    return 0


def safe_worktree(path):
    if path.get("current"):
        return False, "current worktree"
    if path.get("locked"):
        return False, "locked worktree"
    if path["state"] != "clean":
        return False, f"worktree state is {path['state']}"
    files = worktree_files(path["path"])
    if files["untracked"]:
        return False, f"{len(files['untracked'])} untracked file(s)"
    if files["ignored"]:
        return False, f"{len(files['ignored'])} ignored file(s)"
    return True, "clean"


def prune_plan(root, base, as_json=False):
    data = inv.inventory(root, base)
    claims = load_claims(Path(data["common_dir"]))
    annotate(data, claims)
    commands = []
    review = []
    branches = {row["branch"]: row for row in data["branches"]}
    for branch in sorted(data["branches"], key=lambda row: row["branch"]):
        name = branch["branch"]
        if name == base:
            review.append({"branch": name, "reason": "base branch"})
            continue
        if branch["active"]:
            review.append({"branch": name, "reason": "active agent claim"})
            continue
        if branch["unique_commits"] > 0:
            review.append({"branch": name,
                           "reason": f"{branch['unique_commits']} unique commit(s)"})
            continue
        worktrees = [row for row in data["worktrees"]
                     if row.get("branch") == branch["ref"]]
        reasons = []
        for worktree in worktrees:
            safe, reason = safe_worktree(worktree)
            if not safe:
                reasons.append(f"{worktree['path']}: {reason}")
        if reasons:
            review.append({"branch": name, "reason": "; ".join(reasons)})
            continue
        for worktree in worktrees:
            commands.append(["git", "worktree", "remove", worktree["path"]])
        commands.append(["git", "branch", "-d", name])
    for worktree in data["worktrees"]:
        if not worktree["detached"]:
            continue
        if worktree.get("unique_commits", 0) > 0:
            review.append({"worktree": worktree["path"],
                           "reason": f"{worktree['unique_commits']} unique commit(s)"})
            continue
        safe, reason = safe_worktree(worktree)
        if safe:
            commands.append(["git", "worktree", "remove", worktree["path"]])
        else:
            review.append({"worktree": worktree["path"], "reason": reason})
    if as_json:
        print(json.dumps({"commands": commands, "review": review,
                          "groups": data["groups"]}, indent=2))
        return 0
    print("PLAN ONLY — no worktree, branch or ref will be removed.")
    for command in commands:
        print("  " + " ".join(shlex.quote(part) for part in command))
    if review:
        print("REVIEW (no command emitted):")
        for item in review:
            target = item.get("branch") or item.get("worktree")
            print(f"  {target}: {item['reason']}")
    return 0


def status(root, base, as_json=False):
    data = inv.inventory(root, base)
    claims = load_claims(Path(data["common_dir"]))
    annotate(data, claims)
    if as_json:
        print(json.dumps({"groups": data["groups"], "branches": data["branches"],
                          "worktrees": data["worktrees"]}, indent=2))
        return 0
    print(f"Worktree collection status (base {base} {data['base_commit'][:12]})")
    for key in ("integrated", "unmerged", "dirty", "missing", "locked",
                "active", "detached"):
        print(f"{key}: {len(data['groups'][key])}")
        for item in data["groups"][key]:
            print(f"  {item}")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Archive and plan worktree/branch collection; never deletes.")
    parser.add_argument("--cwd", default=".", help="repository/worktree root")
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("status", "archive", "prune-plan"):
        command = sub.add_parser(name)
        command.add_argument("--base", required=True,
                             help="explicit local integration ref, e.g. main")
        command.add_argument("--json", action="store_true")
        if name == "archive":
            command.add_argument("--output", required=True,
                                 help="new or empty directory for the archive")
    args = parser.parse_args(argv)
    try:
        root = Path(args.cwd).resolve()
        if args.command == "archive":
            return archive(root, args.base, args.output)
        if args.command == "prune-plan":
            return prune_plan(root, args.base, args.json)
        return status(root, args.base, args.json)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"WORKTREE_COLLECTION: FAIL — {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
