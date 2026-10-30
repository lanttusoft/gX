#!/usr/bin/env python3
#ai: 2026-10-22 source/system attribution audit; assets remain provenance-gated.
"""Check that shipped code systems and adapted snippets are credited.

This complements copyright_audit.py (media/fonts) and provenance_audit.py
(resource assets): it checks code/service lineage recorded in NOTICE.md and
the license files for vendored dependencies.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NOTICE = ROOT / "license" / "NOTICE.md"
PROOFS = ROOT.parent / "proofs"

# Names a user of the source/binary would need to find in NOTICE.
REQUIRED = [
    "raylib", "raymath", "stb_image", "cgltf", "tinyobj_loader_c",
    "miniaudio", "par_shapes", "glad", "GLFW", "Dear ImGui", "rlImGui",
    "AngelScript", "Open Dynamics Engine", "EMMIR", "Mathlib", "three.js",
    "FXAA", "Inigo Quilez", "Ashima", "ACES", "AgX", "SSRT3",
]

# Source files carrying a visible upstream credit; the same credit must be in
# NOTICE. Matching is case-insensitive.
SOURCE_CREDITS = [
    ("src/external/crt_core.c", "EMMIR"),
    ("src/external/crt_ntsc.c", "EMMIR"),
    ("src/gxmath/vector3.h", "raymath v1.5"),
    ("resources/shader/gfx_gtao.frag", "three.js"),
    ("resources/shader/gfx_fxaa.frag", "Timothy Lottes"),
    ("resources/shader/common/sdf_basic.glsl", "Inigo Quilez"),
    ("resources/shader/common/noise.glsl", "Ashima"),
    ("resources/shader/gfx_ssgi.frag", "SSRT3"),
]

LICENSE_FILES = [
    "license/raylib/LICENSE",
    "license/imgui/LICENSE",
    "license/rlimgui/LICENSE",
    "license/angelscript/LICENSE",
    "license/three/LICENSE",
    "license/fxaa/LICENSE",
    "license/iq/LICENSE",
    "license/ashima/LICENSE",
    "license/agx/LICENSE",
    "license/ntsc-crt/LICENSE",
]


def has_word(text, word):
    return re.search(r"\b" + re.escape(word) + r"\b", text, re.I) is not None


def audit():
    issues = []
    if not NOTICE.is_file():
        return ["license/NOTICE.md is missing"]
    text = NOTICE.read_text(errors="replace")
    for name in REQUIRED:
        if not has_word(text, name):
            issues.append(f"NOTICE missing required system credit: {name}")
    for relative, credit in SOURCE_CREDITS:
        path = ROOT / relative
        if not path.is_file():
            issues.append(f"source credit file missing: {relative}")
            continue
        source = path.read_text(errors="replace")
        if credit.lower() not in source.lower():
            issues.append(f"{relative}: expected upstream credit {credit!r}")
    for relative in LICENSE_FILES:
        if not (ROOT / relative).is_file():
            issues.append(f"missing dependency license: {relative}")
    if PROOFS.is_dir():
        proof_text = "\n".join(
            path.read_text(errors="replace")
            for path in list(PROOFS.rglob("*.lean"))[:64])
        if "Mathlib" in proof_text and not has_word(text, "Mathlib"):
            issues.append("proofs import Mathlib but NOTICE has no Mathlib credit")
    return issues


def main():
    parser = argparse.ArgumentParser(description="gX code/system attribution audit")
    parser.add_argument("--strict", action="store_true",
                        help="exit 1 on any missing credit/license")
    args = parser.parse_args()
    issues = audit()
    if issues:
        print(f"ATTRIBUTION_AUDIT: {len(issues)} issue(s)")
        for issue in issues:
            print(f"  [ATTRIBUTION] {issue}")
        return 1 if args.strict else 0
    print(f"ATTRIBUTION_AUDIT: PASS — {len(REQUIRED)} system credits, "
          f"{len(LICENSE_FILES)} dependency licenses")
    return 0


if __name__ == "__main__":
    sys.exit(main())
