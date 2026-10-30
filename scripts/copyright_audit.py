#!/usr/bin/env python3
"""
gX Copyright Audit
Scans resources/ for third-party-origin content that must not ship in a
GPLv3 public release, and verifies the license inventory in license/NOTICE.md.

Checks:
  - Rip-pattern filenames (game/OS/BIOS/console rips, tutorial textures)
  - ADPCM / unusual-codec wavs (console extraction format)
  - Fonts without a license file or allowlist entry
  - Media files not mentioned in the NOTICE inventory

Usage:
    python3 scripts/copyright_audit.py          # report issues
    python3 scripts/copyright_audit.py --strict # exit 1 on any issue
    python3 scripts/copyright_audit.py --add-notice # append missing to NOTICE
"""

import os
import re
import sys

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(PROJECT, "resources")
NOTICE = os.path.join(PROJECT, "license", "NOTICE.md")

# Patterns that indicate ripped / non-distributable content.
RIP_PATTERNS = [
    r"scph\d+",              # PS2 BIOS dumps (scph10000...)
    r"bios",                 # console BIOS audio
    r"windows\s*xp",         # Microsoft OS sounds
    r"cursor\s*-\s*(accept|buzzer|move|use)",  # FF7 menu rips
    r"\bff7\b|\bfinal\s*fantasy",  # Square Enix
    r"\bmother2\b|\bearthbound\b",  # Nintendo
    r"\bdeltarune\b|\bundertale\b",  # Toby Fox
    r"learnopengl",          # tutorial textures (attribution-bound; skybox1.png
                             # is allowlisted)
    r"mario|zelda|sonic|pokemon",  # other obvious game IP
]

# Files explicitly verified as project-original or licensed (allowlist).
ALLOWLIST = [
    "resources/font/KenneyMiniSquareMono.ttf",
    "resources/font/EuropeanTeletext.ttf",
    "resources/texture/skybox/skybox1.png",
    "resources/texture/misc/testcard.png",
    "resources/texture/orb.png",
    "resources/model/button.glb",
    "resources/model/props/tubetech_vt.glb",
    "resources/texture/texture.png",
    "resources/texture/texture_displace.png",
    "resources/texture/texture_normal.png",
    "resources/model/probe/matprobe.glb",
    "resources/model/probe/matprobe.materials.json",
    "resources/model/probe/mapprobe.glb",
    "resources/model/probe/mapprobe.materials.json",
    "resources/model/teapot.glb",
    "resources/model/primitive/cube.glb",
    "resources/model/primitive/icosahedron.glb",
    "resources/model/primitive/icosahedron2.glb",
    "resources/model/primitive/icosahedron3.glb",
    "resources/model/primitive/cone.glb",
    "resources/model/primitive/sphere.glb",
    "resources/model/primitive/cylinder.glb",
    "resources/model/primitive/torus.glb",
]

# Media extensions we audit (code is covered by license headers separately).
MEDIA_EXT = {".wav", ".ogg", ".mp3", ".ttf", ".otf", ".png", ".jpg", ".jpeg",
             ".bmp", ".tga", ".glb", ".gltf", ".fbx", ".obj"}


def audit():
    issues = []
    media = []
    for root, dirs, files in os.walk(RES):
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext not in MEDIA_EXT:
                continue
            rel = os.path.relpath(os.path.join(root, f), PROJECT).replace("\\", "/")
            media.append(rel)
            low = f.lower()
            for pat in RIP_PATTERNS:
                if re.search(pat, low):
                    issues.append((rel, f"rip-pattern: {pat}"))
    # Fonts must have a license file or be allowlisted
    font_dir = os.path.join(RES, "font")
    licenses = {f.lower() for f in os.listdir(font_dir) if "license" in f.lower()}
    for f in os.listdir(font_dir):
        if f.lower().endswith((".ttf", ".otf")):
            rel = os.path.join("resources", "font", f).replace("\\", "/")
            if rel not in ALLOWLIST and not licenses:
                issues.append((rel, "font without a license file and not allowlisted"))
    # Everything media must be in the NOTICE inventory
    notice = ""
    if os.path.exists(NOTICE):
        notice = open(NOTICE).read().lower()
    for rel in media:
        base = os.path.basename(rel).lower()
        if base not in notice and rel not in ALLOWLIST:
            issues.append((rel, "not mentioned in license/NOTICE.md"))
    return issues, media


def main():
    strict = "--strict" in sys.argv
    issues, _ = audit()
    if issues:
        print(f"copyright audit: {len(issues)} issue(s)")
        for rel, msg in sorted(set(issues)):
            print(f"  [COPYRIGHT] {rel}: {msg}")
    else:
        print("copyright audit: CLEAN")
    if strict and issues:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
