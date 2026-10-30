#!/usr/bin/env python3
"""
gX dead-symbol analysis — finds exported gX_* functions never called
from within the codebase (potential strip candidates).

Usage:
    python3 scripts/dead_symbols.py [--binary build/gX_build]
    python3 scripts/dead_symbols.py --min-size 100

Method: every gX_* function symbol in the binary (nm 'T') is checked for
call sites by grepping the source tree for 'name('. Symbols with at most
one source mention (their own definition) are reported.

Note: symbols may be referenced via function pointers, template dispatch,
or public-API headers — verify before deleting. This is a hint generator,
not a verdict. Public gX_* API in headers should be kept per the
API-surface-complete pillar.
"""

import sys
import os
import re
import subprocess
import json
import argparse

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(PROJECT, "src")

def parse_args():
    p = argparse.ArgumentParser(description="gX dead-symbol analysis")
    p.add_argument("--binary", default=os.path.join(PROJECT, "build/gX_build"),
                   help="Path to gX binary (default: build/gX_build)")
    p.add_argument("--json", action="store_true", help="JSON output")
    p.add_argument("--min-size", type=int, default=0,
                   help="Only report symbols >= this many bytes of text")
    return p.parse_args()

def get_text_symbols(binary):
    out = subprocess.run(["nm", "-S", binary], capture_output=True, text=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 4:
            continue
        size, typ, name = parts[1], parts[2], parts[3]
        if typ == "T" and name.startswith("gX_"):
            try:
                syms[name] = int(size, 16)
            except ValueError:
                syms[name] = 0
    return syms

def count_call_sites(name):
    out = subprocess.run(
        ["grep", "-rn", re.escape(name) + "(", SRC, "--include=*.c"],
        capture_output=True, text=True).stdout
    return len(out.splitlines())

def main():
    args = parse_args()
    if not os.path.exists(args.binary):
        print(f"ERROR: binary not found: {args.binary}", file=sys.stderr)
        sys.exit(2)

    syms = get_text_symbols(args.binary)
    dead = []
    for name, size in sorted(syms.items(), key=lambda kv: -kv[1]):
        if size < args.min_size:
            continue
        # .c call sites; the definition itself is 1 match
        calls = count_call_sites(name)
        if calls <= 1:
            dead.append({"name": name, "bytes": size, "call_sites": calls})

    if args.json:
        print(json.dumps(dead, indent=2))
    else:
        print(f"gX dead-symbol candidates ({len(dead)} found, <=1 call site):")
        print(f"{'bytes':>8}  symbol")
        for d in dead:
            print(f"{d['bytes']:>8}  {d['name']}")
        print()
        print("Verify via grep before deleting — function pointers / scripting /")
        print("template dispatch can reference without a direct call. Public API")
        print("declared in headers should be kept (API-surface-complete pillar).")
    sys.exit(0)

if __name__ == "__main__":
    main()
