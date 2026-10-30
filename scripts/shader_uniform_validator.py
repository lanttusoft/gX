#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

"""
gX Shader Uniform Validator
Cross-references uniform declarations in .frag/.vert shader files
against the C code that sets them via gX_Shader_SetValue_* calls.

Usage:
    python3 scripts/shader_uniform_validator.py
    python3 scripts/shader_uniform_validator.py --shader gfx.frag
"""

import os
import re
import sys
from collections import defaultdict

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHADER_DIR = os.path.join(PROJECT, "resources", "shader")
SRC_DIR = os.path.join(PROJECT, "src")

def extract_shader_uniforms(path):
    """Extract uniform name, type from a shader file."""
    uniforms = []
    try:
        with open(path, 'r') as f:
            for line in f:
                line = line.strip()
                #opencode: tolerate default initializers (uniform float x = 1.0;),
                #   explicit layout qualifiers (layout(location = 0) uniform ...),
                #   and comma declarator lists (uniform vec3 cpos,ctarget,cup;).
                #   Initializers may contain commas (vec4(0.5, ...)) so the
                #   declarator list is split paren-aware, not by raw commas.
                #   The type is one word (two for 'unsigned int'); the greedy
                #   two-word alternative would swallow 'float uGlass' whole.
                m = re.match(r'(?:layout\s*\([^)]*\)\s*)?uniform\s+(unsigned\s+int|\w+)\s+(.+);\s*(?://.*)?$', line)
                if m:
                    utype, chunk = m.group(1), m.group(2)
                    depth = 0
                    current = ''
                    for char in chunk + ',':
                        if char == '(':
                            depth += 1
                        elif char == ')':
                            depth = max(0, depth - 1)
                        if char == ',' and depth == 0:
                            name = re.match(r'\s*(\w+)', current)
                            if name:
                                uniforms.append((utype, name.group(1)))
                            current = ''
                        else:
                            current += char
    except:
        pass
    return uniforms

def find_setvalue_calls():
    """Find all gX_Shader_SetValue_* calls in src/."""
    calls = defaultdict(list)
    pattern = r'gX_Shader_SetValue_\w+\s*\([^,]+,\s*([^,]+),'
    for root, dirs, files in os.walk(SRC_DIR):
        for f in files:
            if f.endswith(('.c', '.cpp', '.h')):
                path = os.path.join(root, f)
                try:
                    with open(path, 'r') as fh:
                        content = fh.read()
                        for m in re.finditer(pattern, content):
                            loc_arg = m.group(1).strip().split('[')[0]
                            calls[loc_arg].append(os.path.relpath(path, PROJECT))
                except:
                    pass
    return calls

def find_gluniform_calls():
    """Find raw uploads whose location is queried inline by uniform name."""
    refs = defaultdict(list)
    pattern = r'gX_RHI_Uniform\w*\s*\(\s*gX_RHI_GetUniformLocation\s*\([^,]+,\s*"([^"]+)"'
    for root, dirs, files in os.walk(SRC_DIR):
        for f in files:
            if f.endswith(('.c', '.cpp', '.h')):
                path = os.path.join(root, f)
                try:
                    with open(path, 'r') as fh:
                        content = fh.read()
                        for m in re.finditer(pattern, content):
                            refs[m.group(1).strip()].append(os.path.relpath(path, PROJECT))
                except:
                    pass
    return refs


def find_glgetlocation_calls():
    """Find gX_Shader_GetLocation calls that reference uniform names."""
    locs = defaultdict(list)
    pattern = r'gX_Shader_GetLocation\s*\([^,]+,\s*"([^"]+)"\s*\)'
    # gX: the gfx pipeline resolves locations via the RHI layer too
    rhi_pattern = r'gX_RHI_GetUniformLocation\s*\([^,]+,\s*"([^"]+)"\s*\)'
    # gX: raymarch resolves locations via the GX_RM_GET_LOC2 macro
    macro_pattern = r'GX_RM_GET_LOC\w*\s*\([^,]+,\s*"([^"]+)"'
    gfx_pattern = r'GFX_SHADER_GETLOC\s*\(([^)]+)\)'
    multi_pattern = r'gX_Shader_GetLocation_Multi\s*\([^,]+,\s*\d+,\s*"([^"]+)"'
    for root, dirs, files in os.walk(SRC_DIR):
        for f in files:
            if f.endswith(('.c', '.cpp', '.h')):
                path = os.path.join(root, f)
                try:
                    with open(path, 'r') as fh:
                        content = fh.read()
                        for pat in (pattern, rhi_pattern, macro_pattern, gfx_pattern, multi_pattern):
                            for m in re.finditer(pat, content):
                                #opencode: skip the GFX_SHADER_GETLOC macro
                                #   *definition* (#define ... (name)) — its
                                #   parameter is not a uniform query.
                                line_start = content.rfind('\n', 0, m.start()) + 1
                                if content[line_start:].lstrip().startswith('#'):
                                    continue
                                uniform_name = m.group(1).split('[')[0]
                                locs[uniform_name].append(os.path.relpath(path, PROJECT))
                except:
                    pass
    return locs

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Cross-reference shader uniforms vs C code")
    parser.add_argument("--shader", help="Check a specific shader file")
    args = parser.parse_args()
    
    # Find all shader files (.glsl includes and .geom stages declare
    #   uniforms too: oracle vars live in common/shadow.glsl, u_Explode in
    #   geo_demo.geom — both were false "queried but never declared").
    shader_exts = ('.frag', '.vert', '.glsl', '.geom', '.comp', '.fs')
    shader_files = []
    for f in sorted(os.listdir(SHADER_DIR)):
        if f.endswith(shader_exts):
            shader_files.append(f)
    for root, dirs, files in os.walk(SHADER_DIR):
        for f in files:
            if f.endswith(shader_exts):
                rel = os.path.relpath(os.path.join(root, f), SHADER_DIR)
                if rel not in shader_files:
                    shader_files.append(rel)
    
    if args.shader:
        shader_files = [f for f in shader_files if args.shader in f]
    
    # Find C-side uniform references
    getloc_calls = find_glgetlocation_calls()
    gluniform_calls = find_gluniform_calls()
    setvalue_calls = find_setvalue_calls()
    
    issues = 0
    
    for sf in sorted(shader_files):
        path = os.path.join(SHADER_DIR, sf)
        if not os.path.exists(path):
            continue
        uniforms = extract_shader_uniforms(path)
        if not uniforms:
            continue
        
        print(f"\n=== {sf} ===")
        
        for utype, uname in uniforms:
            # Check if this uniform is referenced in C code
            base = uname.split('[')[0]
            in_getloc = base in getloc_calls
            in_setvalue = base in setvalue_calls
            in_gluniform = base in gluniform_calls
            # gX allowances: array uniforms are uploaded by indexed member
            #   names (waveDirs[%i] via snprintf) and raylib auto-binds
            #   sampler texture0 — both are referenced by construction
            if uname.startswith(("texture0",)) or "[" in uname:
                continue
            # gX: water wave/ripple uniforms are queried via snprintf-built
            #   names (water.c: "waveDirs[%i]") — referenced by construction
            if base in ("waveDirs", "waveAmps", "waveFreqs", "waveSpeeds",
                        "waveSteeps", "ripplePos", "rippleAmp", "rippleRadius",
                        "ripplePhase"):
                continue
            # gX: samplers bound by raylib's sampler-unit convention
            #   (analog field_odd/even, sky texSky auto-assigned units)
            if base in ("field_odd", "field_even", "texSky"):
                continue
            # gX: gX_Std/gX_StdModel UBO struct members are uploaded
            #   structurally (rshader->uboModelData + Buffer_SubData)
            if base in ("gX_ModelMatrix", "instanceMatrices", "instanceCount",
                        "lightsType", "lightsPos", "lightsDir", "lightsColor",
                        "lightsAttenConst", "lightsAttenLinear", "lightsAttenQuad",
                        "lightsBrightness", "lightsDiffuseFactor",
                        "lightsSpecularFactor", "lightsFov", "modelShininess",
                        "metalnessFactor", "albedoFactor", "planetDir",
                        "planetColor", "planetSize", "planetBrightness",
                        "col", "viewProj", "reflMVP"):
                continue
            
            if not in_getloc and not in_setvalue and not in_gluniform:
                print(f"  ⚠️  {utype} {uname} — NOT referenced in C code")
                issues += 1
            elif in_getloc:
                files = getloc_calls[uname]
                print(f"  ✅ {utype} {uname} — GetLocation in {len(files)} file(s)")
            else:
                files = setvalue_calls.get(uname, [])
                print(f"  ❓ {utype} {uname} — SetValue in {len(files)} file(s) but no GetLocation")
                issues += 1
    
    #opencode: reverse direction — C-side GetLocation names with no shader
    #   declaration (typos, renames, stale queries). Same allowlists apply.
    declared = set()
    for sf in sorted(shader_files):
        path = os.path.join(SHADER_DIR, sf)
        if not os.path.exists(path):
            continue
        for _, uname in extract_shader_uniforms(path):
            declared.add(uname.split('[')[0])
    #opencode: verified-benign reverse hits. colDiffuse/texture1/texture2
    #   are raylib builtin programs (embedded sources, not scanned);
    #   feedback is the generic per-effect PP query (guarded use at
    #   postprocess.c:488); rect/tex are GetLocation_Multi first-tries
    #   falling back to declared box/texInput.
    allow = {"colDiffuse", "texture1", "texture2", "feedback", "rect",
             "tex",
             "waveDirs", "waveAmps", "waveFreqs", "waveSpeeds",
             "waveSteeps", "ripplePos", "rippleAmp", "rippleRadius",
             "ripplePhase", "field_odd", "field_even", "texSky",
             "gX_ModelMatrix", "instanceMatrices", "instanceCount",
             "lightsType", "lightsPos", "lightsDir", "lightsColor",
             "lightsAttenConst", "lightsAttenLinear", "lightsAttenQuad",
             "lightsBrightness", "lightsDiffuseFactor",
             "lightsSpecularFactor", "lightsFov", "modelShininess",
             "metalnessFactor", "albedoFactor", "planetDir",
             "planetColor", "planetSize", "planetBrightness",
             "col", "viewProj", "reflMVP"}
    print(f"\n=== reverse: C queries with no shader declaration ===")
    reverse_issues = 0
    for name in sorted(getloc_calls):
        base = name.split('[')[0]
        if base in declared or base in allow:
            continue
        # snprintf-built indexed names (foo[%i]) match by base
        if any(base == d.split('[')[0] for d in declared):
            continue
        files = sorted(set(getloc_calls[name]))[:4]
        print(f"  ⚠️  {name} — queried but never declared "
              f"({', '.join(files)})")
        reverse_issues += 1
    if reverse_issues == 0:
        print("  (none)")

    print(f"\n{'='*50}")
    print(f"Total shaders checked: {len(shader_files)}")
    print(f"Potential issues: {issues + reverse_issues}")

    return 0 if issues + reverse_issues == 0 else 1

if __name__ == '__main__':
    sys.exit(main())
