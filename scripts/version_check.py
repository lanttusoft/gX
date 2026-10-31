#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: version tooling — format validation, readme/CHANGELOG/tag
#   consistency, and the validation tier for a version change:
#     MAJOR/MINOR bump -> full   (make check + make audit-summary-full)
#     PATCH bump       -> fast   (make check-fast)
#     _W variant only  -> minimal (unit tests + provenance audit)
#   Format: vMAJOR.MINOR.PATCH[_W] with W any [0-9A-Za-z]+ string.

import argparse
import re
import subprocess
import sys
from pathlib import Path


PROJECT = Path(__file__).resolve().parent.parent
README = PROJECT / "readme.txt"
README_MD = PROJECT / "README.md"
CHANGELOG = PROJECT / "CHANGELOG.md"
DATA = PROJECT / "src/common/data.c"
TITLE = PROJECT / "src/gui/templates/titlegx.c"
SETTINGS = PROJECT / "src/gui/templates/settings.c"
VERSION_RE = re.compile(r"v?(\d+)\.(\d+)\.(\d+)(?:_([0-9A-Za-z]+))?")
HEADER_RE = re.compile(r"^## \[(\d+\.\d+\.\d+(?:_[0-9A-Za-z]+)?)\]", re.M)

TIERS = {
    "full": ("make check", "make audit-summary-full"),
    "fast": ("make check-fast",),
    "minimal": ("./build/gX_build --test-only",
                "python3 scripts/provenance_audit.py --strict"),
    "none": (),
}


def parse(text):
    match = VERSION_RE.search(text)
    if not match:
        return None
    major, minor, patch, variant = match.groups()
    return (int(major), int(minor), int(patch), variant or "")


def readme_version():
    return parse(README.read_text().split("\n")[3]) or parse(README.read_text())


def changelog_version():
    match = HEADER_RE.search(CHANGELOG.read_text())
    return parse(match.group(1)) if match else None


def runtime_version():
    match = re.search(r'gX_Data_String_gX_version\s*=\s*"([^"]+)"',
                      DATA.read_text())
    return parse(match.group(1)) if match else None


def readme_md_version():
    match = re.search(r"\(`?v(\d+\.\d+\.\d+(?:_[0-9A-Za-z]+)?)",
                      README_MD.read_text())
    return parse(match.group(1)) if match else None


def ui_version(path):
    match = re.search(r"v(\d+\.\d+\.\d+(?:_[0-9A-Za-z]+)?)\s+BETA",
                      path.read_text(), re.I)
    return parse(match.group(1)) if match else None


def tag_versions():
    result = subprocess.run(["git", "tag", "-l", "v*"], cwd=PROJECT,
                            stdout=subprocess.PIPE, text=True, check=True)
    parsed = [(parse(line), line) for line in result.stdout.split()]
    return [(version, tag) for version, tag in parsed if version]


def format_version(version):
    major, minor, patch, variant = version
    text = f"{major}.{minor}.{patch}"
    return f"{text}_{variant}" if variant else text


def tier_for(before, after):
    if before[:2] != after[:2]:
        return "full"
    if before[2] != after[2]:
        return "fast"
    if before[3] != after[3]:
        return "minimal"
    return "none"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--validate", metavar="VERSION")
    parser.add_argument("--consistency", action="store_true")
    parser.add_argument("--readme", action="store_true")
    parser.add_argument("--gate-from", metavar="VERSION")
    parser.add_argument("--gate-to", metavar="VERSION")
    parser.add_argument("--tier-only", action="store_true")
    arguments = parser.parse_args()

    if arguments.validate:
        version = parse(arguments.validate)
        if not version:
            print(f"version invalid: {arguments.validate}", file=sys.stderr)
            return 1
        print(f"version valid: {format_version(version)}")
        return 0

    if arguments.readme:
        version = readme_version()
        if not version:
            print("readme version missing", file=sys.stderr)
            return 1
        print(f"v{format_version(version)}")
        return 0

    if arguments.consistency:
        readme = readme_version()
        changelog = changelog_version()
        tags = tag_versions()
        issues = []
        if not readme:
            issues.append("readme.txt: no version on the version line")
        if not changelog:
            issues.append("CHANGELOG.md: no released version header")
        if not tags:
            issues.append("git: no v* tags")
        if readme and changelog and readme[:3] != changelog[:3]:
            issues.append(f"readme {format_version(readme)} != changelog "
                          f"{format_version(changelog)}")
        runtime = runtime_version()
        if not runtime:
            issues.append("src/common/data.c: no gX_Data_String_gX_version")
        elif readme and runtime[:3] != readme[:3]:
            issues.append(f"readme {format_version(readme)} != runtime "
                          f"{format_version(runtime)}")
        readme_md = readme_md_version()
        if not readme_md:
            issues.append("README.md: no version link")
        elif readme and readme_md[:3] != readme[:3]:
            issues.append(f"readme {format_version(readme)} != README.md "
                          f"{format_version(readme_md)}")
        for path in (TITLE, SETTINGS):
            version = ui_version(path)
            relative = path.relative_to(PROJECT)
            if not version:
                issues.append(f"{relative}: no vX.Y.Z BETA label")
            elif readme and version[:3] != readme[:3]:
                issues.append(f"readme {format_version(readme)} != {relative} "
                              f"{format_version(version)}")
        if readme and tags:
            latest = max(version for version, _ in tags)
            if readme[:3] != latest[:3]:
                issues.append(f"readme {format_version(readme)} != latest tag "
                              f"{format_version(latest)}")
        for issue in issues:
            print(f"  [VERSION] {issue}")
        print("version-check: " + ("OK" if not issues else "ISSUES"))
        return 1 if issues else 0

    if arguments.gate_from or arguments.gate_to:
        before = parse(arguments.gate_from or "") or readme_version()
        after = parse(arguments.gate_to or "") or readme_version()
        if not before or not after:
            print("gate: missing version", file=sys.stderr)
            return 1
        tier = tier_for(before, after)
        if arguments.tier_only:
            print(tier)
            return 0
        print(f"version gate: {format_version(before)} -> {format_version(after)} "
              f"= {tier}")
        for command in TIERS[tier]:
            print(f"  {command}")
        return 0

    parser.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
