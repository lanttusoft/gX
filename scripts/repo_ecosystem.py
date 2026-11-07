#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I023 local multi-repository discovery and ledger. Read-only Git and
#   filesystem observation; writes only an explicit local snapshot/ledger under
#   the configured ecosystem directory and never archives, prunes or deletes.
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit

import agent_bus
import worktree_inventory as inv


SCHEMA = 1
DEFAULT_MAX_DEPTH = 4
SKIP_NAMES = {
    ".git", ".hg", ".svn", "node_modules", "vendor", "third_party",
    "build", "builds", "out", "output", "dist", "target", ".cache",
    "__pycache__", ".venv", "venv", ".gradle", ".idea", ".vscode",
}
ENGINE_MARKERS = (
    "AGENTS.md", "gates.toml", "proofs.lock.json", "src",
    "resources/style/theme.json",
)
PROOF_MARKERS = ("GxProofs", "lakefile.toml", "lean-toolchain")
ENGINE_REMOTE_KEYS = {"github.com/lanttusoft/gx"}
PROOF_REMOTE_KEYS = {"github.com/lanttusoft/gx-proofs"}
GAME_COMPONENTS = {"gamex", "game2", "game3"}
UNRELATED = "unrelated"
KINDS = ("engine", "proofs", "lineage/reference", "candidate/unreviewed",
         UNRELATED)


def git(root, *args, check=True, timeout=120):
    env = {**os.environ, "GIT_OPTIONAL_LOCKS": "0", "GIT_TERMINAL_PROMPT": "0",
           "GIT_PAGER": "cat", "PAGER": "cat"}
    for key in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR",
                "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES"):
        env.pop(key, None)
    result = subprocess.run(
        ["git", "-c", "core.fsmonitor=false", "-C", str(root), *args],
        env=env, encoding="utf-8", errors="surrogateescape",
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    if check and result.returncode:
        raise RuntimeError(result.stderr.strip() or f"git {args[0]} failed")
    return result


def utc_now():
    return datetime.now(timezone.utc).replace(microsecond=0)


def iso(value):
    return value.astimezone(timezone.utc).isoformat()


def mtime_iso(path):
    try:
        return iso(datetime.fromtimestamp(path.stat().st_mtime, timezone.utc))
    except OSError:
        return None


def safe_component(value):
    value = re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("._")
    return value or "repository"


def path_hash(path):
    return hashlib.sha256(str(path).encode("utf-8", "surrogateescape")).hexdigest()


def same_path(left, right):
    try:
        return Path(left).resolve() == Path(right).resolve()
    except OSError:
        return False


def is_within(root, path):
    try:
        Path(path).resolve().relative_to(Path(root).resolve())
        return True
    except (OSError, ValueError):
        return False


def path_in_any(path, roots):
    if not path:
        return False
    try:
        candidate = Path(path).expanduser().resolve()
    except OSError:
        return False
    for root in roots:
        try:
            candidate.relative_to(Path(root).resolve())
            return True
        except (OSError, ValueError):
            continue
    return False


def relative_parts(root, path):
    try:
        relative = Path(path).resolve().relative_to(Path(root).resolve())
    except (OSError, ValueError):
        relative = Path(path).name
    return [part.lower() for part in relative.parts]


def sanitize_remote(value):
    value = re.sub(r"[\x00-\x1f\x7f]", " ", str(value)).strip()
    if not value:
        return ""
    if "://" not in value:
        match = re.match(
            r"^(?:(?P<user>[^/@]+)@)?(?P<host>[^/:]+):(?P<path>.+)$", value)
        if match and not (len(match.group("host")) == 1
                          and match.group("host").isalpha()
                          and match.group("path").startswith("\\")):
            host = match.group("host").lower()
            path = match.group("path").strip().lstrip("/").rstrip("/")
            path = re.sub(r"\.git$", "", path, flags=re.IGNORECASE)
            return f"{host}/{path}" if path else host
        path = value.rstrip("/")
        path = re.sub(r"\.git$", "", path, flags=re.IGNORECASE)
        return f"file://{path}" if path.startswith("/") else f"file:{path}"
    try:
        parts = urlsplit(value)
    except ValueError:
        redacted = re.sub(r"//[^/@]*@", "//", value)
        return redacted.split("?", 1)[0].split("#", 1)[0]
    scheme = parts.scheme.lower()
    path = parts.path.rstrip("/")
    path = re.sub(r"\.git$", "", path, flags=re.IGNORECASE)
    if scheme == "file":
        host = (parts.hostname or "").lower()
        prefix = f"{host}/" if host else ""
        return f"file://{prefix}{path.lstrip('/')}" if host else \
            f"file://{path}" if path.startswith("/") else f"file:{path}"
    host = (parts.hostname or "").lower()
    port = ""
    try:
        if parts.port and parts.port not in (80, 443, 22):
            port = f":{parts.port}"
    except ValueError:
        port = ""
    if ":" in host and not host.startswith("["):
        host = f"[{host}]"
    path = re.sub(r"/+", "/", path.strip("/"))
    if not host:
        return f"file:{path}"
    return f"{host}{port}/{path}" if path else f"{host}{port}"


def remote_key(value):
    return sanitize_remote(value).lower()


def read_remotes(path):
    result = git(path, "config", "--local", "--get-regexp",
                 r"^remote\..*\.url$", check=False)
    remotes = {}
    keys = set()
    if result.returncode not in (0, 1):
        return remotes, keys
    for line in result.stdout.splitlines():
        match = re.match(r"^remote\.(.+)\.url[ \t]+(.*)$", line)
        if not match:
            continue
        name, url = match.groups()
        clean = sanitize_remote(url)
        if not clean:
            continue
        remotes.setdefault(name, set()).add(clean)
        keys.add(clean.lower())
    ordered = {name: sorted(urls) for name, urls in sorted(remotes.items())}
    return ordered, keys


def marker_state(path):
    engine = sorted(marker for marker in ENGINE_MARKERS
                    if (Path(path) / marker).exists())
    proof = sorted(marker for marker in PROOF_MARKERS
                   if (Path(path) / marker).exists())
    return engine, proof


def classify(path, root, remote_keys):
    engine_markers, proof_markers = marker_state(path)
    engine_count = len(engine_markers)
    parts = relative_parts(root, path)
    game_hint = any(part in GAME_COMPONENTS for part in parts)
    lanttusoft = [key for key in remote_keys
                  if "/lanttusoft/" in key or key.startswith("lanttusoft/")]
    game_remote = bool(remote_keys) and all(
        key.rsplit("/", 1)[-1] in GAME_COMPONENTS for key in remote_keys)
    evidence = {
        "engine_markers": engine_markers,
        "engine_marker_count": engine_count,
        "proof_markers": proof_markers,
        "remote_keys": sorted(remote_keys),
        "path_components": parts,
        "game_lineage_hint": game_hint,
    }
    if (remote_keys & ENGINE_REMOTE_KEYS
            or ("gates.toml" in engine_markers
                and "proofs.lock.json" in engine_markers
                and engine_count >= 2)
            or (engine_count >= 4 and not game_hint)):
        evidence["reason"] = "engine markers or engine remote identity"
        return "engine", evidence
    if (remote_keys & PROOF_REMOTE_KEYS
            or ("GxProofs" in proof_markers
                and ("lakefile.toml" in proof_markers
                     or "lean-toolchain" in proof_markers))):
        evidence["reason"] = "proof corpus markers or proof remote identity"
        return "proofs", evidence
    if game_hint and engine_count >= 1 and (not remote_keys or game_remote):
        evidence["reason"] = "gameX-family path with local source markers"
        return "lineage/reference", evidence
    partial_engine = any(marker in engine_markers for marker in
                         ("gates.toml", "proofs.lock.json",
                          "resources/style/theme.json"))
    if (engine_count >= 3 or partial_engine or lanttusoft
            or any(part == "gx" for part in parts)
            or path.name.lower() in {"gx", "gx-release", "gx-public-beta",
                                     "gx-public-sanitize"}):
        evidence["reason"] = "plausible gX hint without confirmed identity"
        return "candidate/unreviewed", evidence
    evidence["reason"] = "no gX markers, known remote or lineage hint"
    return UNRELATED, evidence


def repo_marker(path):
    dotgit = Path(path) / ".git"
    if dotgit.exists() or dotgit.is_symlink():
        result = git(path, "rev-parse", "--show-toplevel", check=False)
        if result.returncode == 0:
            try:
                if Path(result.stdout.strip()).resolve() == Path(path).resolve():
                    return "normal"
            except OSError:
                return None
        return None
    if ((Path(path) / "HEAD").is_file()
            and (Path(path) / "objects").is_dir()
            and (Path(path) / "refs").is_dir()):
        result = git(path, "rev-parse", "--is-bare-repository", check=False)
        if result.returncode == 0 and result.stdout.strip() == "true":
            return "bare"
    return None


def current_toplevel():
    try:
        return Path(git(Path.cwd(), "rev-parse", "--show-toplevel")
                    .stdout.strip()).resolve()
    except (OSError, RuntimeError, subprocess.SubprocessError):
        return None


def default_root():
    try:
        rows = inv.parse_worktrees(
            git(Path.cwd(), "worktree", "list", "--porcelain", "-z").stdout)
        if rows:
            return Path(rows[0]["path"]).resolve().parent
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError):
        pass
    top = current_toplevel()
    return top.parent if top else Path.cwd()


def discover(root, max_depth):
    root = Path(root).resolve()
    found = {}
    skipped = Counter()
    unavailable = []

    def add(path, hint):
        key = Path(path).resolve()
        found.setdefault(key, hint)
        return key

    def visit(path, depth):
        hint = repo_marker(path)
        if hint:
            add(path, hint)
            return
        if depth >= max_depth:
            skipped["depth"] += 1
            return
        try:
            entries = sorted(os.scandir(path), key=lambda entry: entry.name)
        except OSError as error:
            unavailable.append({"path": str(path), "reason": "inaccessible",
                                "error": str(error)})
            return
        for entry in entries:
            try:
                if entry.is_symlink():
                    skipped["symlink"] += 1
                    continue
                if not entry.is_dir(follow_symlinks=False):
                    continue
            except OSError as error:
                unavailable.append({"path": entry.path, "reason": "inaccessible",
                                    "error": str(error)})
                continue
            name = entry.name.lower()
            if name in SKIP_NAMES or name.startswith("cmake-build"):
                skipped[name if name in SKIP_NAMES else "cmake-build"] += 1
                continue
            visit(Path(entry.path), depth + 1)

    visit(root, 0)
    return found, {"counts": dict(sorted(skipped.items())),
                   "unavailable": unavailable}


def identify_checkout(path, hint="normal"):
    path = Path(path).resolve()
    if hint == "bare":
        return {"path": path, "common_dir": path, "git_dir": path,
                "bare": True}
    top = git(path, "rev-parse", "--show-toplevel").stdout.strip()
    common_text = git(path, "rev-parse", "--git-common-dir").stdout.strip()
    git_dir_text = git(path, "rev-parse", "--absolute-git-dir").stdout.strip()
    common = Path(common_text)
    if not common.is_absolute():
        common = path / common
    if Path(top).resolve() != path:
        raise RuntimeError("directory resolves to another worktree")
    return {"path": path, "common_dir": common.resolve(),
            "git_dir": Path(git_dir_text).resolve(), "bare": False}


def choose_base(path, explicit):
    candidates = []
    if explicit:
        candidates.append((explicit, "argument"))
    candidates += [("main", "main"), ("master", "master")]
    result = git(path, "symbolic-ref", "-q", "--short",
                 "refs/remotes/origin/HEAD", check=False)
    if result.returncode == 0 and result.stdout.strip():
        candidates.append((result.stdout.strip(), "origin/HEAD"))
    candidates.append(("HEAD", "HEAD"))
    for ref, source in candidates:
        result = git(path, "rev-parse", "--verify", "-q", f"{ref}^{{commit}}",
                     check=False)
        if result.returncode == 0 and result.stdout.strip():
            return ref, source, result.stdout.strip()
    return None, None, None


def branch_distance(path, base_commit, head):
    if not base_commit or not head:
        return None, None
    result = git(path, "rev-list", "--left-right", "--count",
                 f"{base_commit}...{head}", check=False)
    if result.returncode:
        return None, None
    try:
        behind, unique = result.stdout.split()
        return int(behind), int(unique)
    except ValueError:
        return None, None


def last_commit(path):
    result = git(path, "log", "-1", "--format=%cI", "HEAD", check=False)
    return result.stdout.strip() or None if result.returncode == 0 else None


def read_branches(path, base_commit):
    result = git(path, "for-each-ref", "--format=%(refname)%09%(objectname)"
                 "%09%(upstream:short)%09%(committerdate:iso-strict)",
                 "refs/heads", check=False)
    branches = []
    if result.returncode:
        return branches
    for line in result.stdout.splitlines():
        fields = line.split("\t")
        if len(fields) < 4:
            continue
        ref, head, upstream, committed = fields
        behind, unique = branch_distance(path, base_commit, head)
        branches.append({
            "branch": ref.removeprefix("refs/heads/"),
            "ref": ref,
            "head": head,
            "upstream": upstream or None,
            "committed": committed or None,
            "behind_base": behind,
            "unique_commits": unique,
            "worktrees": [],
            "lifecycle": "unknown",
        })
    branches.sort(key=lambda row: row["branch"])
    return branches


def read_tags(path):
    result = git(path, "for-each-ref", "--sort=-creatordate",
                 "--format=%(refname:short)%09%(objectname)%09%(creatordate:iso-strict)",
                 "refs/tags", check=False)
    rows = []
    if result.returncode:
        return 0, rows
    for line in result.stdout.splitlines():
        fields = line.split("\t")
        if len(fields) >= 2:
            rows.append({"tag": fields[0], "head": fields[1],
                         "created": fields[2] if len(fields) > 2 else None})
    return len(rows), rows[:20]


def read_proof_pin(path):
    lock = Path(path) / "proofs.lock.json"
    if not lock.is_file():
        return None
    try:
        data = json.loads(lock.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        return {"error": str(error)}
    if not isinstance(data, dict):
        return {"error": "proofs.lock.json is not an object"}
    return {key: data.get(key) for key in
            ("repo", "tag", "commit", "coreModules", "fullModules")
            if key in data}


def read_release(path):
    source = Path(path) / "src/common/data.c"
    if not source.is_file():
        return None
    try:
        text = source.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    match = re.search(r"gX_Data_String_gX_version\s*=\s*\"([^\"]+)\"", text)
    if not match:
        return None
    return {"version": match.group(1), "source": "src/common/data.c"}


def archive_slug(primary_path, common_dir):
    name = safe_component(Path(primary_path).name)
    return f"{name}-{path_hash(common_dir)[:12]}"


def archive_state(ecosystem_dir, slug):
    if not ecosystem_dir:
        return "not-configured", None
    path = Path(ecosystem_dir) / "archives" / slug
    if not path.exists():
        return "none-recorded", path
    if not path.is_dir():
        return "path-is-file", path
    try:
        entries = list(path.iterdir())
    except OSError:
        return "unavailable", path
    if not entries:
        return "empty", path
    if (path / "manifest.json").is_file() and (path / "SHA256SUMS").is_file():
        return "archive-present", path
    return "incomplete", path


def notes_activity(common_dir, checkout_paths):
    inventory = Path(common_dir) / "gx-workstreams" / "notes.md"
    sessions = []
    for path in checkout_paths:
        note = Path(path) / ".ai/session.md"
        if note.is_file():
            sessions.append({"path": str(note), "modified": mtime_iso(note)})
    latest = max((row["modified"] for row in sessions if row["modified"]),
                 default=None)
    return {
        "inventory_notes": inventory.is_file(),
        "inventory_notes_modified": mtime_iso(inventory) if inventory.is_file()
        else None,
        "session_note_count": len(sessions),
        "latest_session_note_modified": latest,
    }


def load_bus(common_dir, env, cache):
    override = env.get("GX_AGENT_BUS")
    root = Path(override).expanduser().resolve() if override else \
        (Path(common_dir) / "gx-agent-bus").resolve()
    if root in cache:
        return cache[root]
    state = {"claims": [], "messages": [], "error": None}
    if root.is_dir():
        try:
            claims, messages = agent_bus.read_state(root)
            state["claims"] = agent_bus.active_claims(claims, agent_bus.utc_now())
            state["messages"] = messages
        except (OSError, RuntimeError, ValueError) as error:
            state["error"] = str(error)
    cache[root] = state
    return state


def compact_claim(claim):
    return {
        "session": claim.get("session"),
        "branch": claim.get("branch"),
        "worktree": claim.get("worktree"),
        "expires": claim.get("expires"),
        "note": claim.get("note"),
    }


def claim_matches(claim, checkout_paths, branches):
    worktree = claim.get("worktree")
    if worktree:
        return path_in_any(worktree, checkout_paths)
    branch = claim.get("branch")
    return bool(branch and branch in branches)


def message_matches(message, checkout_paths):
    return path_in_any(message.get("worktree", ""), checkout_paths)


def group_claims(state, checkout_paths, branches):
    matched = [claim for claim in state["claims"]
               if claim_matches(claim, checkout_paths, branches)]
    sessions = sorted({claim.get("session") for claim in matched
                       if claim.get("session")})
    messages = [message for message in state["messages"]
                if message_matches(message, checkout_paths)]
    latest = max((message.get("utc", "") for message in messages), default=None)
    latest_message = next((message for message in messages
                           if message.get("utc") == latest), None)
    return {
        "claims": [compact_claim(claim) for claim in matched],
        "sessions": sessions,
        "message_count": len(messages),
        "last_message_utc": latest or None,
        "last_message_session": latest_message.get("session")
        if latest_message else None,
    }


def inspect_checkout(identity, group, root, current, claims):
    path = Path(identity["path"])
    row = identity.get("row", {})
    bare = identity.get("bare", False) or "bare" in row
    relation = "bare" if bare else \
        "primary" if same_path(path, group["primary_path"]) else "worktree"
    branch = row.get("branch", "").removeprefix("refs/heads/") or None
    head = row.get("HEAD")
    detached = "detached" in row or branch is None
    data = {
        "path": str(path),
        "in_root": is_within(root, path),
        "common_dir": str(identity["common_dir"]),
        "git_dir": str(identity["git_dir"]) if identity.get("git_dir") else None,
        "relation": relation,
        "kind": group["kind"],
        "group_common_dir": group["common_dir"],
        "branch": branch,
        "head": head,
        "detached": detached,
        "current": bool(current and same_path(path, current)),
        "locked": row.get("locked"),
        "prunable": row.get("prunable"),
        "bare": bare,
        "state": "bare" if bare else "unavailable",
        "dirty_entries": None,
        "untracked": None,
        "ignored": None,
        "base": group["base"],
        "base_commit": group["base_commit"],
        "unique_commits": None,
        "behind_base": None,
        "last_commit": None,
        "active": any(claim_matches(claim, [path], [branch] if branch else [])
                      for claim in claims),
        "error": None,
    }
    if not path.is_dir():
        data["state"] = "missing"
        return data
    if bare:
        result = git(path, "symbolic-ref", "-q", "--short", "HEAD", check=False)
        if result.returncode == 0 and result.stdout.strip():
            data["branch"] = result.stdout.strip()
            data["detached"] = False
        result = git(path, "rev-parse", "--verify", "-q", "HEAD", check=False)
        if result.returncode == 0 and result.stdout.strip():
            data["head"] = result.stdout.strip()
        data["last_commit"] = last_commit(path)
        data["behind_base"], data["unique_commits"] = branch_distance(
            path, group["base_commit"], data["head"])
        return data
    try:
        actual = identify_checkout(path)
        if actual["common_dir"] != Path(group["common_dir"]).resolve():
            raise RuntimeError("checkout belongs to another repository")
        data["git_dir"] = str(actual["git_dir"])
        branch_result = git(path, "symbolic-ref", "-q", "--short", "HEAD",
                            check=False)
        data["branch"] = branch_result.stdout.strip() or None \
            if branch_result.returncode == 0 else None
        data["detached"] = data["branch"] is None
        head_result = git(path, "rev-parse", "--verify", "-q", "HEAD", check=False)
        data["head"] = head_result.stdout.strip() or None \
            if head_result.returncode == 0 else None
        status = git(path, "status", "--porcelain=v1", "-z",
                     "--untracked-files=normal", check=False)
        if status.returncode:
            raise RuntimeError(status.stderr.strip() or "git status failed")
        dirty, untracked = parse_status(status.stdout)
        ignored = git(path, "ls-files", "-z", "--others", "--ignored",
                      "--exclude-standard", "--directory", check=False)
        data["dirty_entries"] = dirty
        data["untracked"] = untracked
        data["ignored"] = len([item for item in ignored.stdout.split("\0")
                               if item]) if ignored.returncode == 0 else None
        data["state"] = "dirty" if dirty or untracked else "clean"
        data["last_commit"] = last_commit(path)
        data["behind_base"], data["unique_commits"] = branch_distance(
            path, group["base_commit"], data["head"])
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        data["state"] = "unavailable"
        data["error"] = str(error)
    return data


def parse_status(text):
    records = [record for record in text.split("\0") if record]
    dirty = 0
    untracked = 0
    index = 0
    while index < len(records):
        code = records[index][:2]
        if code == "??":
            untracked += 1
        else:
            dirty += 1
        index += 2 if "R" in code or "C" in code else 1
    return dirty, untracked


def collect_group(common_dir, identities, root, explicit_base, ecosystem_dir):
    paths = {str(identity["path"]): identity for identity in identities}
    source = next((identity["path"] for identity in identities
                   if Path(identity["path"]).is_dir()
                   and not identity.get("bare")), None)
    if source is None:
        source = next((identity["path"] for identity in identities
                       if Path(identity["path"]).is_dir()), None)
    rows = []
    if source is not None:
        result = git(source, "worktree", "list", "--porcelain", "-z",
                     check=False)
        if result.returncode == 0:
            try:
                rows = inv.parse_worktrees(result.stdout)
            except ValueError:
                rows = []
    for row in rows:
        path = Path(row["path"])
        key = str(path)
        if key in paths:
            paths[key]["row"] = row
            continue
        if path.is_dir():
            try:
                identity = identify_checkout(
                    path, "bare" if "bare" in row else "normal")
            except (OSError, RuntimeError, subprocess.SubprocessError):
                continue
            if Path(identity["common_dir"]).resolve() != Path(common_dir).resolve():
                continue
            identity["row"] = row
            paths[key] = identity
        else:
            paths[key] = {
                "path": path,
                "common_dir": Path(common_dir).resolve(),
                "git_dir": None,
                "bare": "bare" in row,
                "row": row,
            }
    primary_path = Path(rows[0]["path"]) if rows else \
        Path(identities[0]["path"]) if identities else None
    if primary_path is None:
        raise RuntimeError("repository group has no checkout path")
    for identity in paths.values():
        if "row" not in identity:
            identity["row"] = {}
    ordered = sorted(paths.values(), key=lambda item: str(item["path"]))
    info_path = primary_path
    if not info_path.is_dir():
        info_path = next((Path(item["path"]) for item in ordered
                          if Path(item["path"]).is_dir()), primary_path)
    kind_path = info_path
    if source is not None and not kind_path.is_dir():
        kind_path = Path(source)
    markers = {}
    remotes = {}
    remote_keys = set()
    branches = []
    tags = []
    tag_count = 0
    base = None
    base_source = None
    base_commit = None
    proof_pin = None
    release = None
    if source is not None:
        remotes, remote_keys = read_remotes(source)
        base, base_source, base_commit = choose_base(source, explicit_base)
        branches = read_branches(source, base_commit)
        tag_count, tags = read_tags(source)
        proof_pin = read_proof_pin(info_path)
        release = read_release(info_path)
        markers = {"engine": [], "proof": []}
        engine_markers, proof_markers = marker_state(kind_path)
        markers = {"engine": engine_markers, "proof": proof_markers}
    kind, evidence = classify(kind_path, root, remote_keys)
    if not markers:
        evidence.setdefault("engine_markers", [])
        evidence.setdefault("proof_markers", [])
    branch_map = {branch["ref"]: branch for branch in branches}
    for row in rows:
        branch_ref = row.get("branch")
        if branch_ref in branch_map:
            branch_map[branch_ref]["worktrees"].append(row["path"])
    slug = archive_slug(primary_path, Path(common_dir).resolve())
    state, archive_path = archive_state(ecosystem_dir, slug)
    return {
        "common_dir": str(Path(common_dir).resolve()),
        "primary_path": str(primary_path),
        "primary_exists": primary_path.is_dir(),
        "kind": kind,
        "kind_evidence": evidence,
        "in_ecosystem": kind != UNRELATED,
        "checkout_count": len(ordered),
        "checkouts": [str(item["path"]) for item in ordered],
        "relation_counts": dict(Counter(
            "bare" if item.get("bare") or "bare" in item.get("row", {})
            else "primary" if same_path(item["path"], primary_path)
            else "worktree" for item in ordered)),
        "remotes": remotes,
        "remote_keys": sorted(remote_keys),
        "base": base,
        "base_source": base_source,
        "base_commit": base_commit,
        "branches": branches,
        "branch_count": len(branches),
        "tag_count": tag_count,
        "tags": tags,
        "last_commit": last_commit(info_path) if info_path.is_dir() else None,
        "proof_pin": proof_pin,
        "release": release,
        "archive_slug": slug,
        "archive_state": state,
        "archive_path": str(archive_path) if archive_path else None,
        "active": False,
        "active_sessions": [],
        "activity": {},
        "notes": notes_activity(common_dir, [item["path"] for item in ordered]),
        "_identities": ordered,
    }


def snapshot(root=None, max_depth=DEFAULT_MAX_DEPTH, base=None,
             ecosystem_dir=None, env=None):
    env = dict(os.environ if env is None else env)
    ecosystem_dir = ecosystem_dir or env.get("GX_ECOSYSTEM_DIR") \
        or ecosystem_directory(env=env)
    root = Path(root or env.get("GX_REPO_ROOT") or default_root())
    root = root.expanduser().resolve()
    if not root.is_dir():
        raise RuntimeError(f"root is not a directory: {root}")
    max_depth = int(max_depth)
    if max_depth < 0:
        raise RuntimeError("max depth must be zero or greater")
    current = current_toplevel()
    found, skipped = discover(root, max_depth)
    identities = []
    unavailable = list(skipped["unavailable"])
    for path, hint in sorted(found.items()):
        try:
            identities.append(identify_checkout(path, hint))
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            unavailable.append({"path": str(path), "reason": "unavailable",
                                "error": str(error)})
    by_common = {}
    for identity in identities:
        by_common.setdefault(str(identity["common_dir"]), []).append(identity)
    groups = []
    for common, members in sorted(by_common.items()):
        groups.append(collect_group(Path(common), members, root, base,
                                    ecosystem_dir))
    repositories = []
    claims_all = []
    bus_errors = []
    bus_cache = {}
    for group in groups:
        identities = group.pop("_identities")
        bus = load_bus(group["common_dir"], env, bus_cache)
        if bus["error"]:
            bus_errors.append({"common_dir": group["common_dir"],
                               "error": bus["error"]})
        branches = [branch["branch"] for branch in group["branches"]]
        checkout_paths = [identity["path"] for identity in identities]
        activity = group_claims(bus, checkout_paths, branches)
        group["activity"] = activity
        group["active_sessions"] = activity["sessions"]
        group["active"] = bool(activity["sessions"])
        claims_all.extend(activity["claims"])
        active_branches = set()
        for identity in identities:
            row = inspect_checkout(identity, group, root, current,
                                   bus["claims"])
            repositories.append(row)
            if row["active"] and row["branch"]:
                active_branches.add(row["branch"])
        for branch in group["branches"]:
            if branch["branch"] in active_branches:
                branch["lifecycle"] = "active"
        primary = next((row for row in repositories
                        if same_path(row["path"], group["primary_path"])), None)
        if primary is None:
            primary = next((row for row in repositories
                            if is_within(root, row["path"])), None)
        group["primary_branch"] = primary["branch"] if primary else None
        group["primary_head"] = primary["head"] if primary else None
        group["primary_state"] = primary["state"] if primary else None
        group["dirty_checkouts"] = sum(
            1 for row in repositories
            if row["group_common_dir"] == group["common_dir"]
            and row["state"] == "dirty")
        group["missing_checkouts"] = sum(
            1 for row in repositories
            if row["group_common_dir"] == group["common_dir"]
            and row["state"] in ("missing", "unavailable"))
        group["checkout_count"] = len(identities)
        group["checkouts"] = [str(identity["path"]) for identity in identities]
    repositories.sort(key=lambda row: row["path"])
    groups.sort(key=lambda row: row["common_dir"])
    remote_groups = {}
    local_only = []
    unrelated = []
    for group in groups:
        if not group["in_ecosystem"]:
            unrelated.append({
                "path": group["primary_path"],
                "common_dir": group["common_dir"],
                "remotes": [url for urls in group["remotes"].values()
                            for url in urls],
                "reason": group["kind_evidence"]["reason"],
            })
            continue
        if not group["remote_keys"]:
            local_only.append(group["common_dir"])
            continue
        for key in group["remote_keys"]:
            entry = remote_groups.setdefault(key, {
                "remote": key,
                "repository_groups": [],
                "kinds": set(),
                "remotes": set(),
            })
            entry["repository_groups"].append(group["common_dir"])
            entry["kinds"].add(group["kind"])
            for urls in group["remotes"].values():
                entry["remotes"].update(urls)
    remote_rows = []
    for key, value in sorted(remote_groups.items()):
        remote_rows.append({
            "remote": key,
            "remotes": sorted(value["remotes"]),
            "repository_groups": sorted(value["repository_groups"]),
            "kinds": sorted(value["kinds"]),
        })
    kind_counts = Counter(group["kind"] for group in groups)
    active_sessions = sorted({claim.get("session") for claim in claims_all
                              if claim.get("session")})
    summary = {
        "repository_groups": len(groups),
        "checkouts": len(repositories),
        "in_root_checkouts": sum(1 for row in repositories if row["in_root"]),
        "linked_worktrees": sum(1 for row in repositories
                                if row["relation"] == "worktree"),
        "primary_checkouts": sum(1 for row in repositories
                                 if row["relation"] == "primary"),
        "bare_repositories": sum(1 for row in repositories
                                 if row["relation"] == "bare"),
        "remote_groups": len(remote_rows),
        "local_only_groups": len(local_only),
        "unrelated_groups": len(unrelated),
        "active_sessions": len(active_sessions),
        "kinds": {kind: kind_counts.get(kind, 0) for kind in KINDS},
    }
    ecosystem = Path(ecosystem_dir).expanduser().resolve() \
        if ecosystem_dir is not None else None
    ledger = ecosystem / "ledger.md" if ecosystem else None
    return {
        "schema": SCHEMA,
        "observed_at": iso(utc_now()),
        "root": str(root),
        "max_depth": max_depth,
        "base_argument": base,
        "current_toplevel": str(current) if current else None,
        "ecosystem_dir": str(ecosystem) if ecosystem else None,
        "ledger": {
            "path": str(ledger) if ledger else None,
            "present": ledger.is_file() if ledger else False,
        },
        "summary": summary,
        "repository_groups": groups,
        "repositories": repositories,
        "remote_groups": remote_rows,
        "local_only": sorted(local_only),
        "unrelated": unrelated,
        "active_claims": claims_all,
        "active_sessions": active_sessions,
        "activity": {
            "claim_count": len(claims_all),
            "active_sessions": active_sessions,
            "message_count": sum(group["activity"].get("message_count", 0)
                                 for group in groups),
            "notes_groups": sum(1 for group in groups
                                if group["notes"]["inventory_notes"]
                                or group["notes"]["session_note_count"]),
        },
        "skipped": skipped,
        "unavailable": unavailable,
        "bus_errors": bus_errors,
    }


def previous_snapshot(ecosystem_dir):
    directory = Path(ecosystem_dir)
    candidates = []
    if (directory / "snapshot.json").is_file():
        candidates.append(directory / "snapshot.json")
    snapshots = directory / "snapshots"
    if snapshots.is_dir():
        candidates.extend(sorted(snapshots.glob("*.json")))
    best = None
    for path in candidates:
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            raise RuntimeError(f"cannot read snapshot {path}: {error}") from error
        observed = data.get("observed_at", "")
        key = (observed, str(path))
        if best is None or key > best[0]:
            best = (key, path, data)
    if best is None:
        raise RuntimeError(f"no previous snapshot in {directory}")
    return best[1], best[2]


def branch_signature(group):
    return {row["branch"]: {
        field: row.get(field) for field in
        ("head", "upstream", "worktrees", "unique_commits", "behind_base",
         "lifecycle")
    } for row in group.get("branches", [])}


def tag_signature(group):
    return {row["tag"]: row.get("head") for row in group.get("tags", [])}


def compare_snapshots(previous, current):
    old_rows = {row["path"]: row for row in previous.get("repositories", [])}
    new_rows = {row["path"]: row for row in current.get("repositories", [])}
    added = sorted(set(new_rows) - set(old_rows))
    removed = sorted(set(old_rows) - set(new_rows))
    fields = ("kind", "common_dir", "relation", "branch", "head", "state",
              "dirty_entries", "untracked", "ignored", "base", "base_commit",
              "unique_commits", "behind_base", "last_commit", "active",
              "locked", "prunable", "detached", "in_root")
    changed = []
    for path in sorted(set(old_rows) & set(new_rows)):
        differences = {}
        for field in fields:
            before = old_rows[path].get(field)
            after = new_rows[path].get(field)
            if before != after:
                differences[field] = {"from": before, "to": after}
        if differences:
            changed.append({"path": path, "fields": differences})
    old_groups = {row["common_dir"]: row
                  for row in previous.get("repository_groups", [])}
    new_groups = {row["common_dir"]: row
                  for row in current.get("repository_groups", [])}
    added_groups = sorted(set(new_groups) - set(old_groups))
    removed_groups = sorted(set(old_groups) - set(new_groups))
    group_fields = ("kind", "remote_keys", "base", "base_commit",
                    "checkout_count", "tag_count", "active_sessions",
                    "archive_state", "in_ecosystem", "proof_pin", "release",
                    "activity", "notes")
    changed_groups = []
    for common in sorted(set(old_groups) & set(new_groups)):
        differences = {}
        for field in group_fields:
            before = old_groups[common].get(field)
            after = new_groups[common].get(field)
            if before != after:
                differences[field] = {"from": before, "to": after}
        old_branches = branch_signature(old_groups[common])
        new_branches = branch_signature(new_groups[common])
        if old_branches != new_branches:
            differences["branches"] = {"from": old_branches,
                                       "to": new_branches}
        old_tags = tag_signature(old_groups[common])
        new_tags = tag_signature(new_groups[common])
        if old_tags != new_tags:
            differences["tags"] = {"from": old_tags, "to": new_tags}
        if differences:
            changed_groups.append({"common_dir": common, "fields": differences})
    old_skipped = previous.get("skipped", {}).get("counts", {})
    new_skipped = current.get("skipped", {}).get("counts", {})
    skipped = {"from": old_skipped, "to": new_skipped} \
        if old_skipped != new_skipped else None
    unchanged = not (added or removed or changed or added_groups
                     or removed_groups or changed_groups or skipped)
    return {
        "schema": SCHEMA,
        "previous_observed_at": previous.get("observed_at"),
        "current_observed_at": current.get("observed_at"),
        "added": added,
        "removed": removed,
        "changed": changed,
        "added_groups": added_groups,
        "removed_groups": removed_groups,
        "changed_groups": changed_groups,
        "skipped": skipped,
        "unchanged": unchanged,
    }


def drift(root=None, max_depth=None, base=None, ecosystem_dir=None,
          env=None):
    env = dict(os.environ if env is None else env)
    value = ecosystem_dir or env.get("GX_ECOSYSTEM_DIR")
    if not value:
        raise RuntimeError("ecosystem directory is required for drift")
    directory = Path(value).expanduser().resolve()
    previous_file, previous = previous_snapshot(directory)
    root = root or previous.get("root")
    max_depth = max_depth if max_depth is not None else previous.get(
        "max_depth", DEFAULT_MAX_DEPTH)
    base = base if base is not None else previous.get("base_argument")
    current = snapshot(root=root, max_depth=max_depth, base=base,
                       ecosystem_dir=directory, env=env)
    result = compare_snapshots(previous, current)
    result["previous_file"] = str(previous_file)
    result["root"] = str(root)
    return result


def markdown(data):
    lines = [
        "# Local gX repository ecosystem snapshot",
        "",
        "<!-- LOCAL ONLY: machine paths and private remotes; never commit or",
        "publish this file. Read-only observation; no cleanup authorization. -->",
        "",
        f"Observed: {data['observed_at']}",
        f"Root: `{inv.cell(data['root'])}`",
        f"Max discovery depth: {data['max_depth']}",
        f"Current checkout: `{inv.cell(data['current_toplevel'] or 'unknown')}`",
        f"Ecosystem ledger: `{inv.cell(data['ledger']['path'] or 'not configured')}`"
        f" ({'present' if data['ledger']['present'] else 'absent'})",
        "",
        "No fetch, no network and no cleanup were performed. Unknown or",
        "unreviewed trees stay unknown; this snapshot never authorizes deletion.",
        "",
        "## Summary",
        "",
        f"- Repository groups (common dirs): {data['summary']['repository_groups']}",
        f"- Checkouts: {data['summary']['checkouts']} "
        f"({data['summary']['linked_worktrees']} linked worktrees, "
        f"{data['summary']['primary_checkouts']} primary, "
        f"{data['summary']['bare_repositories']} bare)",
        f"- Remote groups: {data['summary']['remote_groups']}; "
        f"local-only groups: {data['summary']['local_only_groups']}; "
        f"unrelated groups: {data['summary']['unrelated_groups']}",
        f"- Active sessions: {data['summary']['active_sessions']}",
        "",
        "| Kind | Groups |",
        "| --- | ---: |",
    ]
    for kind in KINDS:
        lines.append(f"| {kind} | {data['summary']['kinds'].get(kind, 0)} |")
    lines += ["", "## Repository groups", "",
              "| Kind | Primary path | Checkouts | Remotes | Base | HEAD | "
              "Dirty | Missing | Tags | Active | Archive |",
              "| --- | --- | ---: | --- | --- | --- | ---: | ---: | ---: | "
              "--- | --- |"]
    for group in data["repository_groups"]:
        remotes = ", ".join(url for urls in group["remotes"].values()
                            for url in urls) or "none"
        active = ", ".join(group["active_sessions"]) or "no"
        values = [group["kind"], group["primary_path"],
                  group["checkout_count"], remotes,
                  group["base"] or "none",
                  f"{group['primary_branch'] or 'detached'} @ "
                  f"{(group['primary_head'] or '?')[:12]}",
                  group["dirty_checkouts"], group["missing_checkouts"],
                  group["tag_count"], active, group["archive_state"]]
        lines.append("| " + " | ".join(inv.cell(value) for value in values) + " |")
    lines += ["", "## Remote identity groups", "",
              "| Sanitized remote | Kinds | Repository groups |",
              "| --- | --- | --- |"]
    for remote in data["remote_groups"]:
        values = [remote["remote"], ", ".join(remote["kinds"]),
                  ", ".join(remote["repository_groups"])]
        lines.append("| " + " | ".join(inv.cell(value) for value in values) + " |")
    if data["local_only"]:
        lines += ["", "Local-only repository groups (no remote):", ""]
        lines += [f"- `{inv.cell(path)}`" for path in data["local_only"]]
    lines += ["", "## Checkouts", "",
              "| Path | Relation | Group | Branch / HEAD | State | "
              "Dirty / untracked / ignored | Unique / behind | Active | "
              "Last commit |",
              "| --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
    for row in data["repositories"]:
        values = [row["path"], row["relation"], row["common_dir"],
                  f"{row['branch'] or 'detached'} @ "
                  f"{(row['head'] or '?')[:12]}",
                  row["state"],
                  f"{row['dirty_entries']} / {row['untracked']} / "
                  f"{row['ignored']}",
                  f"{row['unique_commits']} / {row['behind_base']}",
                  "yes" if row["active"] else "no",
                  row["last_commit"] or "unknown"]
        lines.append("| " + " | ".join(inv.cell(value) for value in values) + " |")
    lines += ["", "## Branches by repository group", ""]
    for group in data["repository_groups"]:
        lines += [f"### {group['kind']} — `{inv.cell(group['primary_path'])}`",
                  "",
                  "| Branch | HEAD | Upstream | Worktrees | Unique / behind | "
                  "Lifecycle |",
                  "| --- | --- | --- | ---: | --- | --- |"]
        for branch in group["branches"]:
            values = [branch["branch"], branch["head"][:12],
                      branch["upstream"] or "none", len(branch["worktrees"]),
                      f"{branch['unique_commits']} / {branch['behind_base']}",
                      branch["lifecycle"]]
            lines.append("| " + " | ".join(inv.cell(value) for value in values) + " |")
        lines.append("")
    if data["unrelated"]:
        lines += ["## Unrelated repositories (excluded from the ecosystem)", "",
                  "| Path | Sanitized remotes | Reason |",
                  "| --- | --- | --- |"]
        for row in data["unrelated"]:
            values = [row["path"], ", ".join(row["remotes"]) or "none",
                      row["reason"]]
            lines.append("| " + " | ".join(inv.cell(value) for value in values) + " |")
        lines.append("")
    lines += ["## Discovery skips", "",
              "Counts only; symlinks are not followed and repositories are not",
              "scanned recursively. Inaccessible paths are listed in JSON.", ""]
    for reason, count in data["skipped"]["counts"].items():
        lines.append(f"- {reason}: {count}")
    if not data["skipped"]["counts"]:
        lines.append("- none")
    if data["unavailable"]:
        lines += ["", f"Inaccessible or unavailable paths: {len(data['unavailable'])}"
                  " (local JSON only)."]
    if data["bus_errors"]:
        lines += ["", f"Agent-bus read errors: {len(data['bus_errors'])}."]
    lines += ["", "Refresh writes `snapshot.json`/`snapshot.md`; `ledger.md` is",
              "human-maintained and is never overwritten by this tool.", ""]
    return "\n".join(lines)


def atomic_write(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + ".",
                                             dir=str(path.parent))
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8",
                       errors="backslashreplace") as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


LEDGER_TEMPLATE = """# Local gX ecosystem ledger

<!-- LOCAL ONLY: machine paths, private remotes and review notes; never commit
or publish this file. Refreshes never overwrite it. -->

Human/agent-maintained purpose, lineage, integration target and archive/prune
decisions. A row here is a review record, not cleanup authorization.

| Common dir / repository | Kind | Purpose / related ideas | Lineage / upstream | Lifecycle | Integration target | Archive / prune decision | Last review |
| --- | --- | --- | --- | --- | --- | --- | --- |
"""


def ensure_ledger(directory):
    path = Path(directory) / "ledger.md"
    try:
        with path.open("x", encoding="utf-8") as stream:
            stream.write(LEDGER_TEMPLATE)
        return True
    except FileExistsError:
        return False


def write_report(data, directory, history=False):
    directory = Path(directory).expanduser().resolve()
    directory.mkdir(parents=True, exist_ok=True)
    written = []
    atomic_write(directory / "snapshot.json", json.dumps(data, indent=2) + "\n")
    written.append(str(directory / "snapshot.json"))
    atomic_write(directory / "snapshot.md", markdown(data))
    written.append(str(directory / "snapshot.md"))
    if ensure_ledger(directory):
        written.append(str(directory / "ledger.md"))
    if history:
        stamp = utc_now().strftime("%Y%m%dT%H%M%SZ")
        target = directory / "snapshots" / f"{stamp}.json"
        counter = 2
        while target.exists():
            target = directory / "snapshots" / f"{stamp}-{counter}.json"
            counter += 1
        atomic_write(target, json.dumps(data, indent=2) + "\n")
        written.append(str(target))
    return written


def archive_plan(root=None, max_depth=DEFAULT_MAX_DEPTH, base=None,
                 ecosystem_dir=None, env=None):
    env = dict(os.environ if env is None else env)
    directory = ecosystem_dir or env.get("GX_ECOSYSTEM_DIR")
    if not directory:
        raise RuntimeError("ecosystem directory is required for archive-plan")
    directory = Path(directory).expanduser().resolve()
    data = snapshot(root=root, max_depth=max_depth, base=base,
                    ecosystem_dir=directory, env=env)
    script = Path(__file__).resolve().with_name("worktree_collection.py")
    plan = []
    excluded = []
    for group in data["repository_groups"]:
        if not group["in_ecosystem"]:
            excluded.append({
                "path": group["primary_path"],
                "common_dir": group["common_dir"],
                "reason": "unrelated to the gX ecosystem",
            })
            continue
        checkouts = [row for row in data["repositories"]
                     if row["common_dir"] == group["common_dir"]
                     and row["state"] not in ("missing", "unavailable")]
        primary = group["primary_path"]
        if not Path(primary).is_dir() and checkouts:
            primary = checkouts[0]["path"]
        archive_dir = directory / "archives" / group["archive_slug"]
        review = [
            "explicit maintainer review required; this tool never archives, "
            "prunes or deletes",
        ]
        if group["active_sessions"]:
            review.append("active agent claim: " +
                          ", ".join(group["active_sessions"]))
        if group["dirty_checkouts"]:
            review.append(f"{group['dirty_checkouts']} dirty checkout(s)")
        if group["missing_checkouts"]:
            review.append(f"{group['missing_checkouts']} missing/unavailable "
                          "checkout(s)")
        unique_checkouts = [row for row in checkouts
                            if (row["unique_commits"] or 0) > 0]
        if unique_checkouts:
            review.append(f"{len(unique_checkouts)} checkout(s) with unique "
                          f"commits vs base {group['base'] or 'none'}")
        if group["archive_state"] in ("archive-present", "incomplete", "empty"):
            review.append(f"archive output already {group['archive_state']}; "
                          "verify it and choose a new directory instead of "
                          "overwriting")
        if group["kind"] == "candidate/unreviewed":
            review.append("classification is candidate/unreviewed")
        available = Path(primary).is_dir()
        archive_command = None
        prune_command = None
        if available:
            archive_command = [
                sys.executable, str(script), "--cwd", primary, "archive",
                "--base", group["base"] or "HEAD", "--output", str(archive_dir),
            ]
            prune_command = [
                sys.executable, str(script), "--cwd", primary, "prune-plan",
                "--base", group["base"] or "HEAD",
            ]
        else:
            review.append("no available checkout for a plan command")
        plan.append({
            "kind": group["kind"],
            "common_dir": group["common_dir"],
            "primary_path": primary,
            "base": group["base"],
            "active_sessions": group["active_sessions"],
            "archive_state": group["archive_state"],
            "archive_path": str(archive_dir),
            "linked_idea": "GX-I022",
            "disposition": "review-required",
            "review": review,
            "archive_command": archive_command,
            "prune_command": prune_command,
        })
    return {
        "schema": SCHEMA,
        "observed_at": data["observed_at"],
        "root": data["root"],
        "ecosystem_dir": str(directory),
        "plan_only": True,
        "note": "PLAN ONLY: no archive, worktree, branch, ref or file was "
                "created or removed by archive-plan.",
        "plan": plan,
        "excluded": excluded,
    }


def ecosystem_directory(override=None, env=None, root=None):
    env = dict(os.environ if env is None else env)
    value = override or env.get("GX_ECOSYSTEM_DIR")
    if value:
        return Path(value).expanduser().resolve()
    candidates = [Path.cwd()]
    if root:
        candidates.append(Path(root))
    for candidate in candidates:
        try:
            common = git(candidate, "rev-parse", "--git-common-dir") \
                .stdout.strip()
        except (OSError, RuntimeError, subprocess.SubprocessError):
            continue
        path = Path(common)
        if not path.is_absolute():
            path = Path(candidate) / path
        return (path.resolve() / "gx-ecosystem")
    return None


def plan_text(result):
    lines = [
        "REPO_ECOSYSTEM_ARCHIVE_PLAN: PLAN ONLY — no archive, worktree, "
        "branch, ref or file was created or removed.",
        f"Observed: {result['observed_at']}",
        f"Root: {result['root']}",
        f"Ledger: {result['ecosystem_dir']}",
        "",
    ]
    for item in result["plan"]:
        lines += [f"Repository: {item['primary_path']}",
                  f"  kind: {item['kind']}",
                  f"  disposition: {item['disposition']}",
                  f"  archive state: {item['archive_state']}",
                  f"  linked idea: {item['linked_idea']}"]
        for reason in item["review"]:
            lines.append(f"  review: {reason}")
        if item["archive_command"]:
            lines.append("  archive: " + " ".join(
                shlex.quote(part) for part in item["archive_command"]))
        if item["prune_command"]:
            lines.append("  prune-plan: " + " ".join(
                shlex.quote(part) for part in item["prune_command"]))
        lines.append("")
    if result["excluded"]:
        lines.append("Excluded (unrelated, no command emitted):")
        for item in result["excluded"]:
            lines.append(f"  {item['path']}: {item['reason']}")
        lines.append("")
    lines.append("Review every archive and prune report before any explicit "
                 "maintainer action.")
    return "\n".join(lines)


def drift_text(result):
    if result["unchanged"]:
        return (f"REPO_ECOSYSTEM_DRIFT: unchanged since "
                f"{result['previous_observed_at']} "
                f"({result['previous_file']})")
    lines = [f"REPO_ECOSYSTEM_DRIFT: changes since "
             f"{result['previous_observed_at']} ({result['previous_file']})"]
    for path in result["added"]:
        lines.append(f"  + checkout {path}")
    for path in result["removed"]:
        lines.append(f"  - checkout {path}")
    for common in result["added_groups"]:
        lines.append(f"  + group {common}")
    for common in result["removed_groups"]:
        lines.append(f"  - group {common}")
    for item in result["changed"]:
        fields = ", ".join(f"{field}: {value['from']} -> {value['to']}"
                           for field, value in sorted(item["fields"].items()))
        lines.append(f"  ~ {item['path']}: {fields}")
    for item in result["changed_groups"]:
        fields = ", ".join(f"{field}: {value['from']} -> {value['to']}"
                           for field, value in sorted(item["fields"].items()))
        lines.append(f"  ~ group {item['common_dir']}: {fields}")
    if result["skipped"]:
        lines.append("  ~ discovery skip counts changed")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Read-only local gX repository ecosystem and ledger; "
                    "never fetches, archives or deletes.")
    parser.add_argument("--ecosystem-dir", default=None,
                        help="local ledger directory (default: "
                             "GX_ECOSYSTEM_DIR or <git-common-dir>/gx-ecosystem)")
    sub = parser.add_subparsers(dest="command", required=True)
    report_parser = sub.add_parser("report")
    report_parser.add_argument("--root", default=None,
                               help="local directory containing checkouts "
                                    "(default: GX_REPO_ROOT or current parent)")
    report_parser.add_argument("--max-depth", type=int,
                               default=DEFAULT_MAX_DEPTH)
    report_parser.add_argument("--base", default=None,
                               help="preferred local integration ref")
    report_parser.add_argument("--json", action="store_true")
    report_parser.add_argument("--write", action="store_true")
    report_parser.add_argument("--history", action="store_true",
                               help="also write snapshots/<UTC>.json")
    drift_parser = sub.add_parser("drift")
    drift_parser.add_argument("--root", default=None)
    drift_parser.add_argument("--max-depth", type=int, default=None)
    drift_parser.add_argument("--base", default=None)
    drift_parser.add_argument("--json", action="store_true")
    plan_parser = sub.add_parser("archive-plan")
    plan_parser.add_argument("--root", default=None)
    plan_parser.add_argument("--max-depth", type=int,
                             default=DEFAULT_MAX_DEPTH)
    plan_parser.add_argument("--base", default=None)
    plan_parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.command == "drift":
            directory = ecosystem_directory(args.ecosystem_dir, root=args.root)
            if directory is None:
                raise RuntimeError("ecosystem directory is required for drift")
            result = drift(root=args.root, max_depth=args.max_depth,
                           base=args.base, ecosystem_dir=directory)
            print(json.dumps(result, indent=2) if args.json
                  else drift_text(result))
            return 0
        if args.command == "archive-plan":
            directory = ecosystem_directory(args.ecosystem_dir, root=args.root)
            if directory is None:
                raise RuntimeError("ecosystem directory is required for "
                                   "archive-plan")
            result = archive_plan(root=args.root, max_depth=args.max_depth,
                                  base=args.base, ecosystem_dir=directory)
            print(json.dumps(result, indent=2) if args.json
                  else plan_text(result))
            return 0
        directory = ecosystem_directory(args.ecosystem_dir, root=args.root)
        if args.write and directory is None:
            raise RuntimeError("ecosystem directory is required for --write")
        data = snapshot(root=args.root, max_depth=args.max_depth,
                        base=args.base, ecosystem_dir=directory)
        if args.write:
            written = write_report(data, directory, history=args.history)
            print(f"REPO_ECOSYSTEM: {len(data['repository_groups'])} "
                  f"repository group(s), {len(data['repositories'])} "
                  f"checkout(s), {len(data['unrelated'])} unrelated; wrote "
                  f"{len(written)} local file(s) under {directory}; no cleanup "
                  f"performed", file=sys.stderr)
        if args.json:
            print(json.dumps(data, indent=2))
        elif not args.write:
            print(markdown(data), end="")
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"REPO_ECOSYSTEM: FAIL — {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
