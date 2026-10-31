#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: advisory version classifier. It inspects the actual diff, commit
#   subjects and the current CHANGELOG section, then recommends a release
#   class. The declared Release-Class in CHANGELOG.md remains authoritative;
#   --check rejects an under-bumped release.

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


PROJECT = Path(__file__).resolve().parent.parent
CHANGELOG = PROJECT / "CHANGELOG.md"
SEVERITY = {"none": 0, "patch": 1, "minor": 2, "major": 3}
LEVELS = ("none", "patch", "minor", "major")


def git_output(arguments):
    result = subprocess.run(arguments, cwd=PROJECT, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "git command failed")
    return result.stdout


def in_git_repo():
    result = subprocess.run(["git", "rev-parse", "--is-inside-work-tree"],
                            cwd=PROJECT, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True)
    return result.returncode == 0 and result.stdout.strip() == "true"


def max_level(findings):
    level = "none"
    for finding in findings:
        if SEVERITY[finding["level"]] > SEVERITY[level]:
            level = finding["level"]
    return level


def parse_changelog(text):
    lines = text.splitlines()
    start = None
    for index, line in enumerate(lines):
        if re.match(r"^##\s+\[", line):
            start = index
            break
    if start is None:
        return {}
    end = len(lines)
    for index in range(start + 1, len(lines)):
        if re.match(r"^##\s+\[", lines[index]):
            end = index
            break
    categories = {}
    current = None
    for line in lines[start + 1:end]:
        header = re.match(r"^###\s+(\w+)", line)
        if header:
            current = header.group(1).lower()
            categories.setdefault(current, [])
            continue
        if current and line.strip().startswith("-"):
            categories[current].append(line.strip())
    return categories


def changelog_findings(categories):
    findings = []
    for category, entries in categories.items():
        text = " ".join(entries).lower()
        reason = f"CHANGELOG {category}: {text[:100]}"
        if category == "added":
            tooling = any(word in text for word in (
                "policy", "tooling", "script", "test", "documentation", "docs"))
            findings.append({
                "level": "patch" if tooling else "minor",
                "reason": reason,
            })
        elif category == "changed":
            feature = any(word in text for word in (
                "new", "opt-in", "cap", "capacity", "mode", "format", "api",
                "behavior", "palette", "payload"))
            findings.append({
                "level": "minor" if feature else "patch",
                "reason": reason,
            })
        elif category == "fixed":
            findings.append({"level": "patch", "reason": reason})
        elif category == "removed":
            findings.append({"level": "major", "reason": reason})
        elif category == "deprecated":
            findings.append({"level": "minor", "reason": reason})
        elif category == "security":
            findings.append({"level": "patch", "reason": reason})
    return findings


def diff_entries(from_ref, to_ref):
    text = git_output(["git", "diff", "--name-status", "-M",
                       f"{from_ref}..{to_ref}"])
    entries = []
    for line in text.splitlines():
        fields = line.split("\t")
        if len(fields) >= 2:
            entries.append((fields[0], fields[1:]))
    return entries


def file_findings(entries):
    findings = []
    for status, paths in entries:
        code = status[:1]
        if code == "A":
            path = paths[0]
            if path.startswith("src/") and path.endswith(".c") \
                    and "_tests" not in path:
                findings.append({
                    "level": "minor",
                    "reason": f"new source file {path}",
                })
            elif path.startswith("src/") and path.endswith(".h"):
                findings.append({
                    "level": "minor",
                    "reason": f"new header {path}",
                })
            elif path.startswith("resources/tools/"):
                findings.append({
                    "level": "patch",
                    "reason": f"new diagnostic resource {path}",
                })
            elif path.startswith(("docs/", "scripts/")) \
                    or path in ("README.md", "readme.txt"):
                findings.append({
                    "level": "patch",
                    "reason": f"new documentation/tooling {path}",
                })
        elif code == "D":
            path = paths[0]
            if path.startswith("src/") and path.endswith(".h"):
                findings.append({
                    "level": "major",
                    "reason": f"deleted header {path}",
                })
        elif code == "R":
            old, new = paths[0], paths[1]
            if old.startswith("src/") and old.endswith(".h"):
                findings.append({
                    "level": "major",
                    "reason": f"renamed header {old} -> {new}",
                })
    return findings


def declaration_findings(from_ref, to_ref):
    text = git_output(["git", "diff", "-U0", f"{from_ref}..{to_ref}",
                       "--", "src"])
    added = []
    removed = []
    current = None
    for line in text.splitlines():
        if line.startswith("+++ b/"):
            current = line[6:]
        elif line.startswith("+") and not line.startswith("+++"):
            added.append((current, line[1:]))
        elif line.startswith("-") and not line.startswith("---"):
            removed.append((current, line[1:]))

    def declarations(lines):
        found = set()
        for path, line in lines:
            if not path or not path.endswith(".h"):
                continue
            match = re.search(r"\b(gX_[A-Za-z0-9_]+)\s*\(", line)
            if match:
                found.add((path, match.group(1)))
        return found

    added_declarations = declarations(added)
    removed_declarations = declarations(removed)
    findings = []
    for path, name in sorted(removed_declarations):
        if (path, name) not in added_declarations:
            findings.append({
                "level": "major",
                "reason": f"removed declaration {name} ({path})",
            })
    for path, name in sorted(added_declarations):
        if (path, name) not in removed_declarations:
            findings.append({
                "level": "minor",
                "reason": f"new declaration {name} ({path})",
            })
    return findings


def cli_findings(from_ref, to_ref):
    text = git_output(["git", "diff", "-U0", f"{from_ref}..{to_ref}",
                       "--", "src/main_parse.c"])
    added = set()
    removed = set()
    for line in text.splitlines():
        if line.startswith("+") and not line.startswith("+++"):
            added.update(re.findall(r'"(--[A-Za-z0-9-]+)"', line))
        elif line.startswith("-") and not line.startswith("---"):
            removed.update(re.findall(r'"(--[A-Za-z0-9-]+)"', line))
    findings = []
    for flag in sorted(removed - added):
        findings.append({
            "level": "major",
            "reason": f"removed CLI flag {flag}",
        })
    for flag in sorted(added - removed):
        findings.append({
            "level": "minor",
            "reason": f"new CLI flag {flag}",
        })
    return findings


def commit_findings(from_ref, to_ref):
    text = git_output(["git", "log", "--format=%s",
                       f"{from_ref}..{to_ref}"])
    findings = []
    for subject in text.splitlines():
        lower = subject.lower().strip()
        if not lower:
            continue
        if "breaking change" in lower or lower.startswith("breaking:"):
            findings.append({
                "level": "major",
                "reason": f"commit: {subject}",
            })
        elif lower.startswith(("feat:", "feature:")):
            findings.append({
                "level": "minor",
                "reason": f"commit: {subject}",
            })
        elif lower.startswith(("fix:", "docs:", "test:", "chore:",
                               "refactor:", "perf:", "build:", "ci:")):
            findings.append({
                "level": "patch",
                "reason": f"commit: {subject}",
            })
    return findings


def changelog_text(to_ref, changelog_path):
    if changelog_path:
        return Path(changelog_path).read_text()
    if to_ref == "HEAD":
        return CHANGELOG.read_text()
    return git_output(["git", "show", f"{to_ref}:CHANGELOG.md"])


def collect(from_ref, to_ref, changelog_path=None):
    entries = diff_entries(from_ref, to_ref)
    findings = file_findings(entries)
    findings += declaration_findings(from_ref, to_ref)
    findings += cli_findings(from_ref, to_ref)
    findings += commit_findings(from_ref, to_ref)
    categories = parse_changelog(changelog_text(to_ref, changelog_path))
    findings += changelog_findings(categories)
    if not findings and entries:
        findings.append({
            "level": "patch",
            "reason": "source/build changes without a stronger signal",
        })
    return max_level(findings), findings


def check(level, declared, target_major=None):
    if declared == "variant":
        return True
    if declared not in SEVERITY:
        return False
    if SEVERITY[level] <= SEVERITY[declared]:
        return True
    if target_major == 0 and declared == "minor" and level == "major":
        return True
    return False


def main():
    parser = argparse.ArgumentParser(
        description="Recommend a gX release class from the actual change.")
    parser.add_argument("--from", dest="from_ref",
                        help="base ref (default: latest tag)")
    parser.add_argument("--to", dest="to_ref", default="HEAD",
                        help="target ref (default: HEAD)")
    parser.add_argument("--changelog", help="CHANGELOG path override")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--check", metavar="CLASS",
                        help="fail if the suggestion is higher than CLASS")
    parser.add_argument("--target-major", type=int,
                        help="target major version (default: readme.txt)")
    arguments = parser.parse_args()

    repo = in_git_repo()
    from_ref = arguments.from_ref
    if not from_ref:
        if repo:
            from_ref = git_output(["git", "describe", "--tags", "--abbrev=0"])
        else:
            from_ref = "(archive)"
    target_major = arguments.target_major
    if target_major is None:
        readme = (PROJECT / "readme.txt").read_text().splitlines()
        match = re.search(r"v?(\d+)\.", readme[3] if len(readme) > 3 else "")
        target_major = int(match.group(1)) if match else None

    try:
        if repo:
            level, findings = collect(from_ref, arguments.to_ref,
                                      arguments.changelog)
        else:
            categories = parse_changelog(
                changelog_text("HEAD", arguments.changelog))
            findings = changelog_findings(categories)
            level = max_level(findings)
            if not findings:
                findings = [{
                    "level": "none",
                    "reason": "no CHANGELOG entries in this archive",
                }]
    except RuntimeError as error:
        print(f"VERSION_SUGGEST: FAIL — {error}", file=sys.stderr)
        return 2

    if arguments.json:
        print(json.dumps({
            "suggestion": level,
            "from": from_ref,
            "to": arguments.to_ref,
            "git": repo,
            "findings": findings,
        }, indent=2))
    else:
        print(f"VERSION_SUGGEST: {level} ({from_ref} -> {arguments.to_ref})")
        for finding in sorted(findings, key=lambda item: (
                -SEVERITY[item["level"]], item["reason"])):
            print(f"  [{finding['level']}] {finding['reason']}")

    if arguments.check:
        declared = arguments.check.lower()
        if declared not in SEVERITY and declared != "variant":
            print(f"VERSION_CHECK: FAIL — unknown class {declared}",
                  file=sys.stderr)
            return 2
        consistent = check(level, declared, target_major)
        print(f"VERSION_CHECK: {'OK' if consistent else 'FAIL'} — "
              f"suggested {level}, declared {declared}")
        return 0 if consistent else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
