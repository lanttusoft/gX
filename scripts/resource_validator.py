#!/usr/bin/env python3
"""
gX Resource Validator
Scans resources/ directory for common issues:
  - Missing files referenced in code/schemas
  - Texture dimension issues (non-power-of-2)
  - Shader files that exist
  - Font files
  - Empty directories

Usage:
    python3 scripts/resource_validator.py [--verbose]
"""

import os
import sys
import struct

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESOURCES = os.path.join(PROJECT, "resources")

ISSUES = []

def report(category, path, msg):
    ISSUES.append((category, path, msg))
    print(f"  [{category}] {path}: {msg}")

def check_power_of_two(n):
    return n > 0 and (n & (n - 1)) == 0

# gX: accepted non-power-of-2 textures (render fine on GL 3.3+; NPOT is
# legal with CLAMP_TO_EDGE / no mipmaps, and raylib handles both).
NPOT_ALLOWLIST = {
    "resources/texture/skybox/skybox1.png",      # 4x3 cubemap face grid (512x512 faces)
    "resources/texture/misc/testcard.png",               # SMPTE testcard (1:1 sampled)
    "resources/texture/gx_title.png",                    # author title logo (1:1 sampled)
}

def check_texture(path):
    """Basic PNG/BMP header check."""
    rel = os.path.relpath(path, PROJECT).replace(os.sep, "/")
    if rel in NPOT_ALLOWLIST:
        return
    try:
        with open(path, 'rb') as f:
            sig = f.read(8)
            if sig[:4] == b'\x89PNG':
                f.seek(16)
                w, h = struct.unpack('>II', f.read(8))
                if not check_power_of_two(w):
                    report("TEX", path, f"Width {w} not power of 2")
                if not check_power_of_two(h):
                    report("TEX", path, f"Height {h} not power of 2")
            elif sig[:2] == b'BM':
                f.seek(18)
                w = struct.unpack('<I', f.read(4))[0]
                h = abs(struct.unpack('<i', f.read(4))[0])
                if not check_power_of_two(w):
                    report("TEX", path, f"Width {w} not power of 2")
                if not check_power_of_two(h):
                    report("TEX", path, f"Height {h} not power of 2")
    except Exception as e:
        report("TEX", path, f"Can't read: {e}")


def _is_include_fragment(path):
    """gX: shader files that are #included by another shader (e.g. vhs_format
    fragments) legitimately lack void main(). Detect by scanning for includers."""
    name = os.path.basename(path)
    shader_dir = os.path.dirname(path)
    for other in os.listdir(shader_dir):
        if other == name or not other.endswith(('.frag', '.vert', '.geom')):
            continue
        try:
            if name.encode() in open(os.path.join(shader_dir, other), 'rb').read():
                return True
        except OSError:
            pass
    return False

def check_shader(path):
    """Verify shader file has content."""
    try:
        size = os.path.getsize(path)
        if size < 10:
            report("SHD", path, f"Too small ({size} bytes)")
        with open(path, 'r') as f:
            content = f.read()
        if "void main" not in content and not _is_include_fragment(path):
            report("SHD", path, "Missing void main")
    except Exception as e:
        report("SHD", path, f"Can't read: {e}")

def check_model(path):
    """Basic GLB header check."""
    try:
        with open(path, 'rb') as f:
            magic = f.read(4)
            if magic != b'glTF':
                report("MOD", path, f"Bad magic (expected glTF, got {magic})")
    except Exception as e:
        report("MOD", path, f"Can't read: {e}")

def check_wavetable(path):
    #opencode: raw little-endian float32 wavetable for gX_Synth_WT_LoadFile —
    #   1..2048 samples; the engine maps values from 0..1 to -1..1.
    try:
        size = os.path.getsize(path)
        if size < 4 or size % 4 != 0:
            report("WT", path, f"Size {size} is not a positive multiple of 4")
            return
        if size // 4 > 2048:
            report("WT", path, f"{size // 4} samples exceeds the engine max (2048)")
    except Exception as e:
        report("WT", path, f"Can't read: {e}")

def walk():
    valid_extensions = {
        '.png': check_texture,
        '.bmp': check_texture,
        '.jpg': None,
        '.jpeg': None,
        '.glb': check_model,
        '.gltf': check_model,
        '.frag': check_shader,
        '.vert': check_shader,
        '.geom': check_shader,
        '.ttf': None,
        '.otf': None,
        '.yaml': None,
        '.yml': None,
        '.json': None,
        '.wav': None,
        '.ogg': None,
        '.as': None,
        '.gxdemo': None,
        '.css': None,  #opencode: v0.1.40 — GUI stylesheet (gui_audit/guitester)
        '.f32': check_wavetable,  #opencode: synth wavetable tables
    }
    
    for root, dirs, files in os.walk(RESOURCES):
        rel = os.path.relpath(root, RESOURCES)
        if rel == '.':
            rel = ''
        
        # Check for empty dirs
        if not files and not dirs and rel:
            report("DIR", rel, "Empty directory")
        
        for f in sorted(files):
            path = os.path.join(root, f)
            rel_path = os.path.join(rel, f)
            ext = os.path.splitext(f)[1].lower()
            
            if ext in valid_extensions:
                checker = valid_extensions[ext]
                if checker:
                    checker(path)
            elif ext not in ['.md', '.txt', '.zip', '.blend', '.exr', '.glsl', '.comp', '.fs']:
                report("EXT", rel_path, f"Unexpected extension '{ext}'")

def main():
    print(f"gX Resource Validator")
    print(f"Scanning: {RESOURCES}")
    print()
    
    walk()
    
    print()
    if not ISSUES:
        print("PASS: No issues found")
        return 0
    else:
        # Group by category
        cats = {}
        for cat, path, msg in ISSUES:
            cats.setdefault(cat, []).append((path, msg))
        print(f"FAIL: {len(ISSUES)} issue(s) found:")
        for cat in sorted(cats):
            items = cats[cat]
            print(f"\n  {cat} ({len(items)}):")
            for path, msg in items[:10]:
                print(f"    {path}: {msg}")
            if len(items) > 10:
                print(f"    ... and {len(items)-10} more")
        return 1

if __name__ == '__main__':
    sys.exit(main())
