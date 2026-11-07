#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: advisory cross-session bus for parallel agents sharing one Git repository.
#   Messages/claims live under the Git common dir (or GX_AGENT_BUS), not in the
#   public tree. File locks make appends/updates atomic; claims are advisory,
#   not a security boundary or a hard lock.

import argparse
from datetime import datetime, timedelta, timezone
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid


SCHEMA = 1
DEFAULT_TTL_MINUTES = 240


def utc_now():
    return datetime.now(timezone.utc).replace(microsecond=0)


def iso(value):
    return value.astimezone(timezone.utc).isoformat()


def git(root, *args):
    result = subprocess.run(
        ["git", "-C", str(root), *args],
        env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"},
        encoding="utf-8", errors="surrogateescape",
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or f"git {args[0]} failed")
    return result.stdout.strip()


def default_root(cwd):
    override = os.environ.get("GX_AGENT_BUS")
    if override:
        return Path(override).expanduser().resolve()
    common = Path(git(cwd, "rev-parse", "--git-common-dir"))
    if not common.is_absolute():
        common = Path(cwd) / common
    return (common / "gx-agent-bus").resolve()


def prepare_root(root):
    root = Path(root).expanduser().resolve()
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    return root


def lock_file(root, exclusive):
    handle = open(Path(root) / "lock", "a+", encoding="utf-8")
    fcntl.flock(handle.fileno(), fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH)
    return handle


def read_json(path, fallback):
    try:
        with open(path, "r", encoding="utf-8") as stream:
            return json.load(stream)
    except FileNotFoundError:
        return fallback
    except (OSError, ValueError) as error:
        raise RuntimeError(f"cannot read {path}: {error}") from error


def write_json_atomic(path, value):
    path = Path(path)
    descriptor, temporary = tempfile.mkstemp(
        prefix=path.name + ".", dir=str(path.parent))
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=1, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def read_state(root):
    root = Path(root)
    claims = read_json(root / "claims.json", {"schema": SCHEMA, "claims": []})
    messages = []
    try:
        with open(root / "bus.jsonl", "r", encoding="utf-8") as stream:
            for line in stream:
                line = line.strip()
                if not line:
                    continue
                try:
                    messages.append(json.loads(line))
                except ValueError:
                    continue
    except FileNotFoundError:
        pass
    return claims, messages


def active_claims(claims, now):
    result = []
    for claim in claims.get("claims", []):
        try:
            expires = datetime.fromisoformat(claim["expires"])
        except (KeyError, ValueError):
            continue
        if expires > now:
            result.append(claim)
    return result


def normalize_path(value):
    value = value.replace("\\", "/").strip()
    while value.startswith("./"):
        value = value[2:]
    return value.rstrip("/") or "."


def scope_matches(scope, path):
    scope = normalize_path(scope)
    path = normalize_path(path)
    if scope == path:
        return True
    if scope.endswith("/**"):
        base = scope[:-3].rstrip("/")
        return path == base or path.startswith(base + "/")
    return False


def scopes_overlap(left, right):
    return scope_matches(left, right) or scope_matches(right, left)


def current_identity(cwd, session, worktree, branch):
    root = Path(cwd).resolve()
    try:
        top = Path(git(root, "rev-parse", "--show-toplevel")).resolve()
        branch_name = git(root, "rev-parse", "--abbrev-ref", "HEAD")
    except (OSError, RuntimeError, subprocess.SubprocessError):
        top = root
        branch_name = "unknown"
    return {
        "session": session or os.environ.get("GX_AGENT_SESSION")
            or os.environ.get("OPENCODE_SESSION_ID")
            or f"unknown-{os.getpid()}",
        "worktree": str(Path(worktree).expanduser().resolve()) if worktree
            else str(top),
        "branch": branch or branch_name,
    }


def post_message(root, identity, intent, paths, status):
    now = utc_now()
    message = {
        "schema": SCHEMA,
        "id": uuid.uuid4().hex,
        "utc": iso(now),
        "session": identity["session"],
        "worktree": identity["worktree"],
        "branch": identity["branch"],
        "intent": intent,
        "paths": [normalize_path(path) for path in paths],
        "status": status,
    }
    with lock_file(root, True):
        with open(Path(root) / "bus.jsonl", "a", encoding="utf-8") as stream:
            stream.write(json.dumps(message, sort_keys=True) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
    return message


def add_claim(root, identity, paths, note, ttl_minutes, allow_overlap):
    now = utc_now()
    expires = now + timedelta(minutes=ttl_minutes)
    with lock_file(root, True):
        claims = read_json(Path(root) / "claims.json",
            {"schema": SCHEMA, "claims": []})
        active = active_claims(claims, now)
        conflicts = []
        for path in paths:
            for claim in active:
                if claim["session"] == identity["session"]:
                    continue
                if scopes_overlap(path, claim["path"]):
                    conflicts.append(claim)
        if conflicts and not allow_overlap:
            return None, conflicts, now
        kept = []
        for claim in claims.get("claims", []):
            if (claim["session"] == identity["session"]
                    and any(scopes_overlap(path, claim["path"])
                            for path in paths)):
                continue
            kept.append(claim)
        for path in paths:
            kept.append({
                "schema": SCHEMA,
                "session": identity["session"],
                "worktree": identity["worktree"],
                "branch": identity["branch"],
                "path": normalize_path(path),
                "note": note or "",
                "created": iso(now),
                "expires": iso(expires),
            })
        write_json_atomic(Path(root) / "claims.json",
            {"schema": SCHEMA, "claims": kept})
    return {"count": len(paths), "expires": iso(expires)}, [], now


def release_claims(root, identity, paths, release_all):
    with lock_file(root, True):
        claims = read_json(Path(root) / "claims.json",
            {"schema": SCHEMA, "claims": []})
        kept = []
        removed = 0
        for claim in claims.get("claims", []):
            if claim["session"] != identity["session"]:
                kept.append(claim)
                continue
            if release_all or not paths or any(
                    scopes_overlap(path, claim["path"]) for path in paths):
                removed += 1
                continue
            kept.append(claim)
        write_json_atomic(Path(root) / "claims.json",
            {"schema": SCHEMA, "claims": kept})
    return removed


def prune_claims(root):
    now = utc_now()
    with lock_file(root, True):
        claims = read_json(Path(root) / "claims.json",
            {"schema": SCHEMA, "claims": []})
        kept = active_claims(claims, now)
        removed = len(claims.get("claims", [])) - len(kept)
        write_json_atomic(Path(root) / "claims.json",
            {"schema": SCHEMA, "claims": kept})
    return removed


def print_state(root, claims, messages, as_json, limit, only_session):
    now = utc_now()
    active = active_claims(claims, now)
    if only_session:
        active = [claim for claim in active
                  if claim["session"] == only_session]
    if as_json:
        print(json.dumps({
            "root": str(root),
            "now": iso(now),
            "claims": active,
            "messages": messages[-limit:] if limit else messages,
        }, indent=1, sort_keys=True))
        return
    print(f"root: {root}")
    print(f"now:  {iso(now)}")
    print("active claims:")
    if not active:
        print("  (none)")
    for claim in active:
        print(f"  {claim['session']}  {claim['path']}  "
            f"expires {claim['expires']}  {claim.get('note', '')}")
    print("recent messages:")
    recent = messages[-limit:] if limit else messages
    if not recent:
        print("  (none)")
    for message in recent:
        paths = ",".join(message.get("paths", []))
        print(f"  {message.get('utc', '?')}  {message.get('session', '?')}  "
            f"{message.get('status', '?')}  {message.get('intent', '')}"
            + (f"  [{paths}]" if paths else ""))


def build_parser():
    parser = argparse.ArgumentParser(
        description="Advisory cross-session bus for parallel gX agents.")
    parser.add_argument("--root", help="bus directory (default: Git common dir)")
    parser.add_argument("--cwd", default=".",
        help="repository/worktree used for Git identity (default: cwd)")
    parser.add_argument("--session", help="session identifier")
    parser.add_argument("--worktree", help="worktree path override")
    parser.add_argument("--branch", help="branch override")
    sub = parser.add_subparsers(dest="command", required=True)

    post = sub.add_parser("post", help="append an intent/status message")
    post.add_argument("--intent", required=True)
    post.add_argument("--path", action="append", default=[])
    post.add_argument("--status", default="working",
        choices=("working", "done", "blocked", "note"))

    listing = sub.add_parser("list", help="show active claims and messages")
    listing.add_argument("--json", action="store_true")
    listing.add_argument("--limit", type=int, default=30)
    listing.add_argument("--only-session")

    claim = sub.add_parser("claim", help="claim one or more paths")
    claim.add_argument("--path", action="append", required=True)
    claim.add_argument("--note", default="")
    claim.add_argument("--ttl", type=int, default=DEFAULT_TTL_MINUTES,
        help="minutes until the claim expires (default 240)")
    claim.add_argument("--allow-overlap", action="store_true")

    release = sub.add_parser("release", help="release this session's claims")
    release.add_argument("--path", action="append", default=[])
    release.add_argument("--all", action="store_true")

    check = sub.add_parser("check", help="check paths against active claims")
    check.add_argument("--path", action="append", required=True)

    sub.add_parser("prune", help="remove expired claims")
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        cwd = Path(args.cwd).resolve()
        root = prepare_root(args.root or default_root(cwd))
        identity = current_identity(cwd, args.session, args.worktree,
            args.branch)
        if args.command == "post":
            post_message(root, identity, args.intent, args.path, args.status)
            print(f"posted: {identity['session']} {args.intent}")
            return 0
        if args.command == "claim":
            result, conflicts, _now = add_claim(root, identity, args.path,
                args.note, args.ttl, args.allow_overlap)
            if conflicts:
                for claim in conflicts:
                    print(f"conflict: {claim['session']} owns "
                        f"{claim['path']} until {claim['expires']}",
                        file=sys.stderr)
                print("claim not written; use --allow-overlap deliberately",
                    file=sys.stderr)
                return 1
            print(f"claimed {result['count']} path(s) until "
                f"{result['expires']}")
            return 0
        if args.command == "release":
            removed = release_claims(root, identity, args.path, args.all)
            print(f"released {removed} claim(s)")
            return 0
        if args.command == "prune":
            removed = prune_claims(root)
            print(f"pruned {removed} expired claim(s)")
            return 0
        if args.command == "check":
            claims, _messages = read_state(root)
            active = [claim for claim in active_claims(claims, utc_now())
                      if claim["session"] != identity["session"]]
            blocked = []
            for path in args.path:
                matches = [claim for claim in active
                           if scopes_overlap(path, claim["path"])]
                if matches:
                    blocked.append((path, matches))
            for path, matches in blocked:
                for claim in matches:
                    print(f"claimed: {path} -> {claim['session']} "
                        f"{claim['path']} until {claim['expires']}",
                        file=sys.stderr)
            if blocked:
                return 1
            print("free: no other active claim matches")
            return 0
        if args.command == "list":
            claims, messages = read_state(root)
            print_state(root, claims, messages, args.json, args.limit,
                args.only_session)
            return 0
    except (OSError, RuntimeError, ValueError,
            subprocess.SubprocessError) as error:
        print(f"AGENT_BUS: FAIL — {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
