#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: pin the engine proof gate to the published gx-proofs tree. The archived
#   private staging tree has the same shape but fewer modules; a commit check
#   (git checkout) and module counts (source copy) both reject it.

import argparse
import json
from pathlib import Path
import subprocess
import sys


PROJECT = Path(__file__).resolve().parents[1]
DEFAULT_PIN = PROJECT / "proofs.lock.json"
DEFAULT_PROOFS = PROJECT.parent / "proofs"
FAMILIES = ("GxProofs", "GxProofsGfx", "GxMath")
KEYS = ("schema", "repo", "tag", "commit", "coreModules", "fullModules",
        "coreDeclarations", "fullDeclarations")


def load_pin(path):
    document = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(document, dict):
        raise ValueError("proof pin must be a JSON object")
    missing = [key for key in KEYS if key not in document]
    if missing:
        raise ValueError("proof pin is missing: " + ", ".join(missing))
    if document["schema"] != 1:
        raise ValueError(f"unsupported proof pin schema {document['schema']}")
    for key in ("coreModules", "fullModules", "coreDeclarations",
                "fullDeclarations"):
        if type(document[key]) is not int or document[key] <= 0:
            raise ValueError(f"proof pin {key} must be a positive integer")
    return document


def module_counts(root):
    counts = {}
    for family in FAMILIES:
        directory = Path(root) / family
        if not directory.is_dir():
            raise ValueError(f"proof tree is missing {family}/")
        counts[family] = len(list(directory.rglob("*.lean")))
    return counts


def git_head(root):
    result = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def check(root=DEFAULT_PROOFS, pin_path=DEFAULT_PIN):
    pin = load_pin(pin_path)
    root = Path(root)
    if not root.is_dir():
        raise FileNotFoundError(f"proof tree not found: {root}")
    head = git_head(root)
    if head and head != pin["commit"]:
        raise ValueError(
            f"proof tree HEAD {head} is not the pinned {pin['tag']} "
            f"({pin['commit']})")
    counts = module_counts(root)
    if counts["GxProofs"] != pin["coreModules"]:
        raise ValueError(
            f"core module count {counts['GxProofs']} != pinned "
            f"{pin['coreModules']}")
    total = sum(counts.values())
    if total != pin["fullModules"]:
        raise ValueError(
            f"full module count {total} != pinned {pin['fullModules']}")
    return {"pin": pin, "head": head, "modules": counts, "total": total}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--proofs", type=Path, default=DEFAULT_PROOFS,
                        help="proof tree checkout (default: sibling ../proofs)")
    parser.add_argument("--pin", type=Path, default=DEFAULT_PIN,
                        help="pin manifest (default: proofs.lock.json)")
    parser.add_argument("--json", action="store_true",
                        help="emit the result as JSON")
    args = parser.parse_args()
    try:
        result = check(args.proofs, args.pin)
    except (OSError, ValueError, KeyError) as error:
        print(f"PROOF_PIN: FAIL — {error}", file=sys.stderr)
        return 1
    pin = result["pin"]
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(f"PROOF_PIN: PASS — {pin['tag']} {pin['commit'][:7]} "
              f"({pin['coreModules']} core / {result['total']} total modules)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
