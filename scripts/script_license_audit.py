#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.
#ai: require SPDX/copyright metadata on project scripts; complements the C header audit.
"""Check that project scripts carry GPLv3 SPDX and copyright metadata."""

import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SPDX = "SPDX-License-Identifier: GPL-3.0-or-later"
COPYRIGHT = "Copyright (c)"


def script_files():
    files = [ROOT / "build.sh"]
    files += sorted((ROOT / "scripts").glob("*.py"))
    files += sorted((ROOT / "scripts").glob("*.sh"))
    return files


def audit():
    issues = []
    files = script_files()
    for path in files:
        head = "\n".join(path.read_text(errors="replace").splitlines()[:8])
        if SPDX not in head:
            issues.append(f"{path.relative_to(ROOT)}: missing {SPDX}")
        if COPYRIGHT not in head:
            issues.append(f"{path.relative_to(ROOT)}: missing copyright line")
    return files, issues


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true",
                        help="exit 1 when a script has no license metadata")
    args = parser.parse_args()
    files, issues = audit()
    if issues:
        print(f"SCRIPT_LICENSE: {len(issues)} issue(s) across {len(files)} scripts")
        for issue in issues:
            print(f"  [SCRIPT_LICENSE] {issue}")
        return 1 if args.strict else 0
    print(f"SCRIPT_LICENSE: PASS — {len(files)} scripts carry GPLv3 SPDX + copyright")
    return 0


if __name__ == "__main__":
    sys.exit(main())
