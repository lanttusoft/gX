#!/usr/bin/env python3
"""
Model Format Validator — validates all .glb files for structural integrity.
Checks: glTF magic, JSON header, mesh/material counts, vertex format hints.

Usage:
    python3 scripts/model_format_validator.py
"""

import sys
import json
import struct
from pathlib import Path

MODEL_DIR = "resources/model"

def validate_glb(path):
    """Validate a .glb file structure"""
    data = path.read_bytes()
    
    if len(data) < 20:
        return "file too small for glTF header"
    
    # glTF 2.0 binary header
    magic, version, length = struct.unpack_from("<III", data, 0)
    
    if magic != 0x46546C67:  # "glTF" in little-endian
        return f"bad magic: 0x{magic:08X} (expected 0x46546C67)"
    
    if version != 2:
        return f"glTF version {version} (expected 2)"
    
    if length != len(data):
        return f"header length {length} != actual {len(data)}"
    
    # Parse JSON chunk
    pos = 12  # header is 12 bytes
    has_bin = False
    json_data = None
    
    while pos + 8 <= len(data):
        chunk_len, chunk_type = struct.unpack_from("<II", data, pos)
        chunk_start = pos + 8
        chunk_end = chunk_start + chunk_len
        
        if chunk_end > len(data):
            return f"chunk at {pos} overflows file"
        
        if chunk_type == 0x4E4F534A:  # "JSON" in little-endian
            try:
                json_data = json.loads(data[chunk_start:chunk_end].decode("utf-8"))
            except (json.JSONDecodeError, UnicodeDecodeError) as e:
                return f"JSON parse error: {e}"
        elif chunk_type == 0x004E4942:  # "BIN\0" in little-endian
            has_bin = True
            if json_data is None:
                return "BIN chunk before JSON chunk"
            expected_bin_len = json_data.get("buffers", [{}])[0].get("byteLength", 0)
            if chunk_len != expected_bin_len:
                return f"BIN length {chunk_len} != JSON says {expected_bin_len}"
        
        pos += 8 + chunk_len
    
    if json_data is None:
        return "no JSON chunk found"
    
    # Validate mesh/material structure
    meshes = json_data.get("meshes", [])
    materials = json_data.get("materials", [])
    accessors = json_data.get("accessors", [])
    
    issues = []
    if not meshes:
        issues.append("no meshes")
    if len(meshes) > 10:
        issues.append(f"{len(meshes)} meshes (unusually many)")
    if len(materials) == 0:
        issues.append("no materials defined")
    # Check for vertex color accessor
    has_vertex_colors = any(acc for acc in accessors if acc.get("type") == "VEC4")
    if has_vertex_colors:
        issues.append("has vertex colors (rare)")
    
    return issues if issues else None  # None = clean, list = warnings

def main():
    errors = 0
    total = 0
    
    model_path = Path(MODEL_DIR)
    if not model_path.exists():
        print(f"ERROR: {MODEL_DIR} not found")
        sys.exit(1)
    
    print(f"Scanning {MODEL_DIR}...")
    print()
    
    for path in sorted(model_path.rglob("*.glb")):
        total += 1
        result = validate_glb(path)
        if result is None:
            print(f"  PASS: {path.relative_to(model_path.parent)}")
        elif isinstance(result, str):
            print(f"  FAIL: {path.relative_to(model_path.parent)} — {result}")
            errors += 1
        else:
            warnings = ", ".join(result)
            print(f"  WARN: {path.relative_to(model_path.parent)} — {warnings}")
    
    print()
    print(f"Total: {total} models, {total - errors} good, {errors} failed")
    return 1 if errors > 0 else 0

if __name__ == "__main__":
    sys.exit(main())
