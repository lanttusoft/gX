#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

"""
gX Codebase Map — include dependency visualizer
Scans all .c/.h files in src/, builds a dependency graph of #include
directives, and outputs a DOT graph.

Usage:
    python3 scripts/codebase_map.py [--dot] [--cluster]
    
    --dot      Output DOT format (for Graphviz)
    --cluster  Group by subdirectory clusters
    --list     Just list files and their dependencies (default)
"""

import os
import re
import sys
from collections import defaultdict

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(PROJECT, "src")

# Directories to include (subsystems)
SUBSYSTEMS = [
    "common", "gxmath", "wrappers", "renderer", "gui", "scene",
    "resource", "camera", "audio", "main", "backend", "rhi", "settings",
]

def find_files():
    """Find all .c and .h files in src/ organized by subsystem."""
    files = defaultdict(list)
    for root, dirs, fnames in os.walk(SRC):
        for f in fnames:
            if f.endswith(('.c', '.h')):
                rel = os.path.relpath(root, SRC)
                parts = rel.split(os.sep)
                sub = parts[0] if parts else "root"
                if sub in SUBSYSTEMS:
                    files[sub].append(os.path.join(root, f))
    return files

def parse_includes(path):
    """Extract #include "..." directives from a file."""
    includes = []
    try:
        with open(path, 'r', errors='ignore') as f:
            for line in f:
                m = re.match(r'#\s*include\s+"([^"]+)"', line)
                if m:
                    includes.append(m.group(1))
    except:
        pass
    return includes

def resolve_subsystem(include_path, file_map):
    """Try to determine which subsystem an included file belongs to."""
    # Check if the include path contains a known subsystem dir
    for sub in SUBSYSTEMS:
        if f"/{sub}/" in include_path or include_path.startswith(f"{sub}/"):
            return sub
    # Check by searching file_map for matching basename
    basename = os.path.basename(include_path)
    for sub, files in file_map.items():
        for f in files:
            if f.endswith(basename):
                return sub
    return "unknown"

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Generate codebase dependency map")
    parser.add_argument("--dot", action="store_true", help="Output DOT format")
    parser.add_argument("--list", action="store_true", default=True, help="List dependencies")
    args = parser.parse_args()
    
    file_map = find_files()
    
    # Build dependency matrix
    deps = defaultdict(set)  # subsystem -> set of subsystems it depends on
    all_subs = set(file_map.keys())
    
    for sub, files in file_map.items():
        for f in files:
            includes = parse_includes(f)
            for inc in includes:
                target = resolve_subsystem(inc, file_map)
                if target and target != sub and target in all_subs:
                    deps[sub].add(target)
    
    if args.dot:
        print("digraph gX {")
        print("  rankdir=LR;")
        print("  node [shape=box, style=filled, fillcolor=lightyellow];")
        for sub in sorted(all_subs):
            print(f'  "{sub}";')
        for sub, targets in sorted(deps.items()):
            for t in sorted(targets):
                print(f'  "{sub}" -> "{t}";')
        print("}")
    else:
        print("gX dependency matrix (subsystem → depends on):")
        print()
        for sub in sorted(all_subs):
            targets = deps.get(sub, set())
            if targets:
                print(f"  {sub:15s} → {', '.join(sorted(targets))}")
            else:
                print(f"  {sub:15s} → (no internal deps)")
        print()
        print(f"Total subsystems: {len(all_subs)}")
        print(f"Total deps: {sum(len(v) for v in deps.values())}")
        
        # Find circular deps
        print()
        for sub in sorted(all_subs):
            for t in deps.get(sub, set()):
                if sub in deps.get(t, set()):
                    print(f"  CIRCULAR: {sub} ↔ {t}")

if __name__ == '__main__':
    sys.exit(main())
