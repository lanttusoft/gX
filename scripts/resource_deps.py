#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

"""
gX Resource Dependency Checker
Scans C source code for resource file references (paths containing
"resources/"), then cross-references against actual files on disk.

Usage:
    python3 scripts/resource_deps.py              # full report
    python3 scripts/resource_deps.py --missing    # only missing files
    python3 scripts/resource_deps.py --unused     # only unused files
"""

import os
import re
import sys
from collections import defaultdict

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(PROJECT, "src")
RES = os.path.join(PROJECT, "resources")

def find_resource_refs():
    """Find all resource/ path references in source code."""
    refs = defaultdict(list)
    pattern = re.compile(r'(?:resources/[^\s"\'\);]+)')
    # gX: GX_PATH("type", "file") macro — compile-time path generator
    gxpath = re.compile(r'GX_PATH\(\s*"([a-z]+)"\s*,\s*"([^"]+)"\s*\)')
    # gX: GX_RM_ADD_<TYPE>("file") loaddefaults macros
    rmadd = re.compile(r'GX_RM_ADD_([A-Z]+)\(\s*"([^"]+)"\s*\)')
    rm_types = {"FONT": "font", "MODEL": "model", "SHADER": "shader",
                "SOUND": "sound", "MUSIC": "music", "TEXTURE": "texture"}
    # gX: adjacent-literal concat — "resources" SHADER_PATH "dir/file" (shader
    #   tables in wrappers/shader.c build paths from parts)
    concat = re.compile(r'"resources"\s*SHADER_PATH\s*"([^"]+)"')
    # gX: GX_RESOURCE_DIR "/shader/postprocess/" prefix — the PP loader
    #   (postprocess/shader.c) builds names via gX_GetShaderString; the dir
    #   prefix references every file in it (name table verified 1:1)
    ppdir = re.compile(r'GX_RESOURCE_DIR\s*"/shader/postprocess/"')
    # gX: GX_RM_ADD_SHADER_VF("vert", "frag")
    vfadd = re.compile(r'GX_RM_ADD_SHADER_VF\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\)')
    # gX: bare media names — resolved against resource subdirs at runtime
    #   (wireframe model arrays, shader #include "common/x.glsl")
    bare = re.compile(r'"([^"/][^"]*\.(?:glb|gltf|frag|vert|geom|glsl|ttf|otf|png|wav|ogg))"')
    bare_dirs = ["shader", "model", "texture", "font", "audio", "music"]

    def add_ref(rp, line, rel):
        rp = rp.strip('"').strip("'")
        if rp.endswith(',') or rp.endswith(')'):
            rp = rp[:-1]
        refs[rp].append(f"{rel}:{line}")

    roots = [SRC, os.path.join(PROJECT, 'resources', 'shader')]
    for root in roots:
        for root2, dirs, files in os.walk(root):
            if os.path.basename(root2) == 'postprocess':
                dirs[:] = []  # postprocess files are dir-referenced
            for f in files:
                if f.endswith(('.c', '.cpp', '.h', '.as', '.yaml',
                              '.frag', '.vert', '.geom', '.glsl')):
                    path = os.path.join(root2, f)
                    try:
                        with open(path, 'r', errors='ignore') as fh:
                            for i, line in enumerate(fh, 1):
                                rel = os.path.relpath(path, PROJECT)
                                for m in pattern.findall(line):
                                    add_ref(m, i, rel)
                                for m in gxpath.findall(line):
                                    add_ref(f"resources/{m[0]}/{m[1]}", i, rel)
                                for m in rmadd.findall(line):
                                    if m[0] in rm_types:
                                        add_ref(f"resources/{rm_types[m[0]]}/{m[1]}", i, rel)
                                for m in concat.findall(line):
                                    add_ref(f"resources/shader/{m}", i, rel)
                                if ppdir.search(line):
                                    pp = os.path.join(PROJECT, 'resources', 'shader', 'postprocess')
                                    for m in os.listdir(pp):
                                        if m.endswith((".frag", ".vert", ".geom")):
                                            add_ref(f"resources/shader/postprocess/{m}", i, rel)
                                for m in vfadd.findall(line):
                                    add_ref(f"resources/shader/{m[0]}", i, rel)
                                    add_ref(f"resources/shader/{m[1]}", i, rel)
                                for m in bare.findall(line):
                                    for d in bare_dirs:
                                        cand = os.path.join(PROJECT, 'resources', d, m)
                                        if os.path.exists(cand):
                                            add_ref(f"resources/{d}/{m}", i, rel)
                                            break
                    except:
                        pass
    return refs

def find_actual_files():
    """List all files in resources/ directory."""
    files = set()
    for root, dirs, fnames in os.walk(RES):
        for f in fnames:
            if f.startswith('.'):
                continue
            full = os.path.join(root, f)
            rel = os.path.relpath(full, PROJECT)
            files.add(rel)
    return files

# gX: known-accepted missing refs — format templates, runtime artifacts,
# generated defaults, and retained slot dirs (documented decisions).
MISSING_ALLOWLIST = {
    "resources/audio/",              # sound slot dir; UI sounds are generated (audio.c)
    "resources/music/",              # no music ships; music API fails gracefully
    "resources/markup/demo.json",    # markup subsystem kept, demo asset not shipped
    "resources/model/%s",            # glb loader path format template
    "resources/texture/%s",          # override map format template (scene export copy)
    "resources/presets/renderer_fractal",    # fractal preset dir; CLI-driven, no presets ship
    "resources/presets/scene/<name>.json",  # preset path format template
    "resources/settings.json",       # runtime-generated on exit
    "resources/tools/scratch.json",  # GUI editor save target; created on first save
    "resources/texture/title/bg2.png",      # documented default in path.h comment
    # Dormant legacy shader-table rows (wrappers/shader.c type table) — the
    # gfx pipeline loads its passes directly; these types are never instantiated
    "resources/shader/billboard.vert",
    "resources/shader/sky.vert",
    "resources/shader/wireframe.frag",
    "resources/shader/wireframe.vert",
    "resources/shader/postprocess/tonemap.frag",
    "resources/shader/postprocess/tonemap.vert",
    "resources/shader/particle.frag",
    "resources/shader/particle.vert",
    "resources/shader/postprocess/bloom.frag",
    "resources/shader/postprocess/bloom.vert",
    "resources/shader/postprocess/blur.vert",
    "resources/shader/postprocess/composite.frag",
    "resources/shader/postprocess/composite.vert",
    "resources/shader/postprocess/ssao.frag",
    "resources/shader/postprocess/ssao.vert",
    "resources/shader/postprocess/ssr.frag",
    "resources/shader/postprocess/ssr.vert",
}


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Cross-reference resource files vs code references")
    parser.add_argument("--missing", action="store_true", help="Only show missing files")
    parser.add_argument("--unused", action="store_true", help="Only show unused files")
    args = parser.parse_args()
    
    refs = find_resource_refs()
    actual = find_actual_files()
    
    # Check which referenced files exist
    missing = []
    code_refs_no_res = set()
    for rp, sources in refs.items():
        # Normalize path
        if not rp.startswith('resources/'):
            code_refs_no_res.add(rp)
            continue
        if not os.path.exists(os.path.join(PROJECT, rp)) and rp not in MISSING_ALLOWLIST:
            missing.append((rp, sources))
        code_refs_no_res.add(rp)
    
    # Check which actual files are never referenced (license meta-files excluded)
    #opencode: `.materials.json` sidecars are loaded by convention from the
    #   model stem (model.c), so they never appear as literal code refs.
    unused = {u for u in (actual - code_refs_no_res)
             if 'LICENSE' not in os.path.basename(u) and not u.endswith('.txt')
             and not u.endswith('.materials.json')}
    
    # Files referenced without resources/ prefix
    non_res = [(rp, sources) for rp, sources in refs.items() 
               if not rp.startswith('resources/')]
    
    if args.missing:
        print(f"Missing files ({len(missing)}):")
        for rp, sources in sorted(missing):
            print(f"  {rp}")
            for s in sources[:3]:
                print(f"    referenced in: {s}")
        return 1 if missing else 0
    
    if args.unused:
        print(f"Unused files ({len(unused)}):")
        for f in sorted(unused)[:50]:
            print(f"  {f}")
        if len(unused) > 50:
            print(f"  ... and {len(unused)-50} more")
        return 0
    
    print(f"Resource Dependency Report")
    print(f"{'='*50}")
    print(f"Code references:   {len(refs)}")
    print(f"Actual files:      {len(actual)}")
    print(f"Missing files:     {len(missing)}")
    print(f"Unused files:      {len(unused)}")
    print(f"Non-standard refs: {len(non_res)}")
    print()
    
    if missing:
        print(f"MISSING FILES ({len(missing)}):")
        for rp, sources in sorted(missing)[:15]:
            print(f"  ❌ {rp}")
            for s in sources[:2]:
                print(f"     from: {s}")
        if len(missing) > 15:
            print(f"  ... and {len(missing)-15} more")
        print()
    
    if unused:
        print(f"UNUSED FILES ({len(unused)} — first 20):")
        for f in sorted(unused)[:20]:
            print(f"  📄 {f}")
        print()
    
    return 1 if missing else 0

if __name__ == '__main__':
    sys.exit(main())
