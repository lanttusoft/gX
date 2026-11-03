#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: repository secret scan over tracked text files only. Build trees,
#   wine prefixes and other generated state are never scanned. The rules are
#   deliberately narrow (known credential shapes + private release paths) so a
#   clean tree stays green without a baseline file; an inline
#   `pragma: allowlist secret` marker is the documented escape hatch.
"""Scan tracked text files for credentials and private release paths."""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAX_BYTES = 4 * 1024 * 1024
ALLOW_MARKERS = ("pragma: allowlist secret", "gitleaks:allow",
                 "noqa: secret")

#ai: keep the pattern text from matching itself; the string is assembled from
#   fragments so this file participates in the scan like every other file.
RULES = (
    ("private-key",
     re.compile(r"-----BEGIN " + r"[A-Z0-9 ]*PRIVATE KEY-----")),
    ("github-token",
     re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9]{36,}"
                r"|github_pat_[A-Za-z0-9_]{20,})\b")),
    ("aws-access-key",
     re.compile(r"\b(?:AKIA|ASIA)[0-9A-Z]{16}\b")),
    ("google-api-key",
     re.compile(r"\bAIza[0-9A-Za-z_-]{35}\b")),
    ("slack-token",
     re.compile(r"\bxox[baprs]-[A-Za-z0-9-]{10,}\b")),
    ("stripe-key",
     re.compile(r"\b(?:sk|pk)_(?:live|test)_[A-Za-z0-9]{16,}\b")),
    ("jwt",
     re.compile(r"\beyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}"
                r"\.[A-Za-z0-9_-]{10,}\b")),
    ("generic-secret",
     re.compile(r"(?i)\b(?:api[_-]?key|secret[_-]?key|access[_-]?token"
                r"|auth[_-]?token|client[_-]?secret|password|passwd)\b"
                r"\s*[:=]\s*[\"']([^\"'\s]{8,})[\"']")),
    ("private-path",
     re.compile(r"(?:/mnt/" + r"Data4|/home/[A-Za-z0-9._-]+/"
                r"(?:git|projects)/|" + r"gX" + r"-release)")),
)

PLACEHOLDER_VALUES = (
    "example", "sample", "dummy", "placeholder", "changeme", "change-me",
    "your_", "your-", "<", ">", "${", "{{", "%s", "xxx", "test", "redacted",
    "password", "secret", "none", "null", "todo", "fixme",
)

SKIP_DIRS = {".git", "build", "node_modules", "__pycache__", ".cache"}


def _is_placeholder(value):
    lowered = value.lower()
    if any(marker in lowered for marker in PLACEHOLDER_VALUES):
        return True
    if len(set(lowered)) == 1:
        return True
    if lowered.startswith(("$", "{", "(", "[")):
        return True
    return False


def scan_text(text):
    """Return (line number, rule name) findings; values are never returned."""
    findings = []
    for number, line in enumerate(text.splitlines(), 1):
        if any(marker in line for marker in ALLOW_MARKERS):
            continue
        for name, pattern in RULES:
            match = pattern.search(line)
            if not match:
                continue
            if name == "generic-secret" and _is_placeholder(match.group(1)):
                continue
            findings.append((number, name))
    return findings


def scan_file(path):
    try:
        data = path.read_bytes()
    except OSError:
        return []
    if len(data) > MAX_BYTES or b"\0" in data[:8192]:
        return []
    return scan_text(data.decode("utf-8", errors="replace"))


def tracked_files(root):
    try:
        result = subprocess.run(
            ["git", "-C", str(root), "ls-files", "-z",
             "--cached", "--others", "--exclude-standard"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True)
    except (OSError, subprocess.CalledProcessError):
        return walk_files(root)
    names = [name for name in result.stdout.decode().split("\0") if name]
    return [root / name for name in names]


def walk_files(root):
    files = []
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        parts = set(path.relative_to(root).parts[:-1])
        if parts & SKIP_DIRS:
            continue
        if any(part.startswith("build-") for part in parts):
            continue
        files.append(path)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    files = tracked_files(root)
    findings = []
    skipped = 0
    for path in files:
        try:
            size = path.stat().st_size
        except OSError:
            skipped += 1
            continue
        if size > MAX_BYTES:
            skipped += 1
            continue
        for number, rule in scan_file(path):
            findings.append((path.relative_to(root), number, rule))
    if findings:
        print(f"SECRET_SCAN: {len(findings)} finding(s) in {len(files)} files")
        for path, number, rule in findings:
            print(f"  {path}:{number}: {rule}")
        return 1
    if not args.quiet:
        print("SECRET_SCAN: PASS — %d tracked files scanned, %d large/unreadable "
              "skipped" % (len(files), skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
