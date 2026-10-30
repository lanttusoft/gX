#!/usr/bin/env python3
"""
gX Shader Compile Checker
Validates that all .frag/.vert shader files have the minimum required
structure: void main(), #version directive, uniform declarations, etc.

Usage:
    python3 scripts/shader_compile_check.py
    python3 scripts/shader_compile_check.py --verbose
"""

import os
import re
import sys

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHADER_DIR = os.path.join(PROJECT, "resources", "shader")

# gX: shaders that are #included by another shader (vhs_* fragments) or
# loaded via LoadShaderFromMemory (gfx_* pipeline passes — the engine's
# shader compiler prepends #version and assembles main) legitimately lack
# both. Skip them like the resource validator does.
MEMORY_LOADED_PREFIXES = ("gfx_", "postprocess/geometry_", "postprocess/blur_")
INCLUDE_FRAGMENT_NAMES = ("vhs_format", "vhs_machine", "vhs_tape")


def _is_include_or_memory_loaded(path):
    name = os.path.basename(path)
    rel = path.replace(os.sep, "/")
    if name.startswith(INCLUDE_FRAGMENT_NAMES):
        return True
    base = os.path.basename(os.path.dirname(rel))
    for p in MEMORY_LOADED_PREFIXES:
        if name.startswith(p):
            return True
    return False


def check_shader(path):
    """Check a single shader file for basic validity."""
    issues = []
    try:
        with open(path, 'r') as f:
            content = f.read()
    except Exception as e:
        return [f"can't read: {e}"]

    if _is_include_or_memory_loaded(path):
        return issues

    lines = content.split('\n')
    
    # Check file size
    if len(content) < 10:
        issues.append("file too small (empty/stub)")
        return issues
    
    # Check #version
    has_version = False
    for line in lines[:5]:
        if re.match(r'\s*#\s*version', line):
            has_version = True
            break
    if not has_version:
        issues.append("missing #version directive")
    
    # Check void main
    if 'void main' not in content:
        issues.append("missing void main()")
    
    # Check for unbalanced braces (strip comments first — block comments
    # containing braces are not code)
    code_only = re.sub(r'/\*.*?\*/', '', content, flags=re.S)
    code_only = re.sub(r'//[^\n]*', '', code_only)
    open_braces = code_only.count('{')
    close_braces = code_only.count('}')
    if open_braces != close_braces:
        issues.append(f"unbalanced braces: {{={open_braces} }}={close_braces}")
    
    # Check uniform declarations have both type and name
    uniform_lines = re.findall(r'uniform\s+\w+\s+\w+', content)
    for ul in re.findall(r'uniform\s+([^;]+);', content):
        parts = ul.strip().split()
        if len(parts) < 2:
            issues.append(f"malformed uniform: '{ul}'")
    
    return issues

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Check shader files for basic validity")
    parser.add_argument("--verbose", "-v", action="store_true", help="Show all checks")
    parser.add_argument("--spirv", action="store_true",
                        help="Vulkan SPIR-V corpus gate (glslangValidator, classifies failures)")
    args = parser.parse_args()
    
    if args.spirv:
        return run_spirv_gate(SHADER_DIR, verbose=args.verbose)
    
    if not os.path.isdir(SHADER_DIR):
        print(f"ERROR: shader directory not found: {SHADER_DIR}")
        return 1
    
    # Collect all shader files
    shaders = []
    for root, dirs, files in os.walk(SHADER_DIR):
        for f in files:
            if f.endswith(('.frag', '.vert', '.geom')):
                shaders.append(os.path.join(root, f))
    
    total = len(shaders)
    passed = 0
    failed = 0
    
    print(f"Shader Compile Check: {total} files")
    print(f"{'='*50}")
    
    for path in sorted(shaders):
        rel = os.path.relpath(path, PROJECT)
        issues = check_shader(path)
        
        if issues:
            failed += 1
            print(f"  ❌ {rel}")
            for issue in issues[:3]:
                print(f"     {issue}")
        else:
            passed += 1
            if args.verbose:
                print(f"  ✅ {rel}")
    
    print(f"\n{'='*50}")
    print(f"Passed: {passed}, Failed: {failed}, Total: {total}")
    
    return 1 if failed > 0 else 0


# ---------------------------------------------------------------------------
# SPIR-V corpus gate (Vulkan feasibility, Stage 0)
#
# Answers the rhi_backends.md "hard part": can the existing GLSL corpus be
# compiled to SPIR-V for a future Vulkan backend? Runs glslangValidator with
# a Vulkan target on every .frag/.vert/.geom and CLASSIFIES failures by error
# class. Raw GLSL is expected to mostly fail — the point is to enumerate the
# rewrite work a Vulkan shader dialect must do (locations, uniform blocks,
# entry points, GL-only built-ins), not to gate on pass/fail.
# ---------------------------------------------------------------------------

SPIRV_ERROR_CLASSES = [
    ("location", "SPIR-V requires location for user input/output"),
    ("uniform-block", "non-opaque uniforms outside a block"),
    ("binding", "sampler/texture/image requires layout\\(binding|explicit 'binding'"),
    ("reserved-word", "Reserved word"),
    ("include", "#include"),
    ("entry-point", "Missing entry point"),
    ("version", "version"),
    ("GLSL-fp", "gl_FragColor|gl_FragData|gl_FragDepth"),
    ("tex-builtins", "texture2D|textureLod|gl_Texture"),
    ("other", None),
]

def classify_spirv_error(log):
    """Map a glslangValidator log line to a failure class name."""
    low = log
    for name, pattern in SPIRV_ERROR_CLASSES:
        if pattern is None:
            continue
        if re.search(pattern, low):
            return name
    return "other"

def run_spirv_gate(shader_dir, verbose=False):
    """Compile every shader to SPIR-V (Vulkan target), classify failures."""
    import shutil
    import subprocess
    import tempfile

    glslang = shutil.which("glslangValidator")
    if not glslang:
        print("ERROR: glslangValidator not found — install glslang-tools")
        return 1

    shaders = []
    for root, dirs, files in os.walk(shader_dir):
        for f in files:
            if f.endswith(('.frag', '.vert', '.geom')):
                shaders.append(os.path.join(root, f))

    total = len(shaders)
    passed = 0
    failed = 0
    by_class = {}

    print(f"SPIR-V Corpus Gate (Vulkan): {total} files")
    print(f"tool: {glslang}")
    print(f"{'='*60}")

    for path in sorted(shaders):
        rel = os.path.relpath(path, PROJECT)
        # --target-env vulkan1.1 forces the Vulkan GLSL dialect rules.
        # -o with a temp output: glslangValidator defaults to writing
        # ./frag.spv in the CWD, which pollutes the repo — redirect it.
        tmpdir = tempfile.mkdtemp(prefix="gx_spirv_")
        tmpout = os.path.join(tmpdir, "out.spv")
        r = subprocess.run(
            [glslang, "--target-env", "vulkan1.1", "-o", tmpout, path],
            capture_output=True, text=True)
        shutil.rmtree(tmpdir, ignore_errors=True)
        if r.returncode == 0:
            passed += 1
            if verbose:
                print(f"  [OK] {rel}")
            continue

        failed += 1
        # first ERROR line classifies the failure (glslang prints to stdout)
        combined = (r.stdout or "") + (r.stderr or "")
        cls = "other"
        for line in combined.splitlines():
            if "ERROR" in line and "No code generated" not in line:
                cls = classify_spirv_error(line)
                break
        by_class[cls] = by_class.get(cls, 0) + 1
        if verbose:
            first_err = next((l for l in combined.splitlines() if "ERROR" in l), "")
            print(f"  [FAIL] {rel}  -> {cls}: {first_err.strip()[:100]}")

    print(f"\n{'='*60}")
    print(f"Passed (portable as-is): {passed}")
    print(f"Failed (need Vulkan rewrite): {failed}")
    print(f"\nFailure classes (what a Vulkan shader dialect must rewrite):")
    for cls, count in sorted(by_class.items(), key=lambda kv: -kv[1]):
        print(f"  {cls:<18} {count:>4}")
    print(f"\nTotal: {total}")

    # the gate is informational at Stage 0 — report, don't fail the build
    return 0

if __name__ == '__main__':
    sys.exit(main())
