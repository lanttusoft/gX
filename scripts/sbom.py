#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: deterministic SPDX 2.3 SBOM for the public source release. The package
#   table is the single inventory; versions are parsed from the vendored
#   headers where a header exposes one. `--check` keeps the committed document
#   from drifting, and the release archive helper ships the same file.
"""Generate or check the gX SPDX 2.3 software bill of materials."""

import argparse
import json
import re
import sys
from pathlib import Path

import version_check

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "sbom" / "gX.spdx.json"
DOCUMENT_NAMESPACE = "https://github.com/lanttusoft/gX/sbom/v%s"

LICENSE_FILES = (
    "license/raylib/LICENSE",
    "license/imgui/LICENSE",
    "license/rlimgui/LICENSE",
    "license/angelscript/LICENSE",
    "license/three/LICENSE",
    "license/fxaa/LICENSE",
    "license/iq/LICENSE",
    "license/ashima/LICENSE",
    "license/ntsc-crt/LICENSE",
)

# name, version, SPDX license, source path, purl, comment
COMPONENTS = (
    ("raylib", "6.1-dev", "Zlib", "external/raylib",
     "pkg:github/raysan5/raylib@6.1-dev", "vendored window/render backend"),
    ("Dear ImGui", "1.92.8 WIP", "MIT", "src/tools/imgui",
     "pkg:github/ocornut/imgui@v1.92.8", "vendored editor UI"),
    ("rlImGui", "vendored", "Zlib", "src/tools/imgui/rlImGui.h", None,
     "raylib backend for Dear ImGui"),
    ("AngelScript", "2.38.0", "Zlib", "external/angelscript", None,
     "vendored scripting subset"),
    ("raymath", "1.5", "Zlib", "src/gxmath", None,
     "gX math is based on/uses raymath v1.5"),
    ("stb_image", "raylib-bundled", "MIT OR Unlicense",
     "external/raylib/src/external/stb_image.h", None, ""),
    ("cgltf", "raylib-bundled", "MIT",
     "external/raylib/src/external/cgltf.h", None, ""),
    ("tinyobj_loader_c", "raylib-bundled", "MIT",
     "external/raylib/src/external/tinyobj_loader_c.h", None, ""),
    ("miniaudio", "raylib-bundled", "MIT OR Unlicense",
     "external/raylib/src/external/miniaudio.h", None, ""),
    ("par_shapes", "raylib-bundled", "MIT",
     "external/raylib/src/external/par_shapes.h", None, ""),
    ("glad", "raylib-bundled", "MIT",
     "external/raylib/src/external/glad.h", None, ""),
    ("dr_flac", "raylib-bundled", "Unlicense",
     "external/raylib/src/external/dr_flac.h", None, ""),
    ("dr_mp3", "raylib-bundled", "Unlicense",
     "external/raylib/src/external/dr_mp3.h", None, ""),
    ("dr_wav", "raylib-bundled", "Unlicense",
     "external/raylib/src/external/dr_wav.h", None, ""),
    ("jar_mod", "raylib-bundled", "Unlicense",
     "external/raylib/src/external/jar_mod.h", None, ""),
    ("jar_xm", "raylib-bundled", "Unlicense",
     "external/raylib/src/external/jar_xm.h", None, ""),
    ("GLFW", "raylib-bundled", "Zlib",
     "external/raylib/src/external/glfw", None, ""),
    ("three.js GTAOPass", "adapted", "MIT", "resources/shader/gfx_gtao.frag",
     "pkg:npm/three", "GTAO reference implementation (Jimenez et al. 2016)"),
    ("NVIDIA FXAA 3.11", "3.11", "BSD-3-Clause",
     "resources/shader/gfx_fxaa.frag", None, "Timothy Lottes formulation"),
    ("Inigo Quilez SDF", "adapted", "MIT",
     "resources/shader/common", None, "SDF/smooth-min/normal formulations"),
    ("Ashima simplex noise", "adapted", "MIT",
     "resources/shader/common/noise.glsl", None, ""),
    ("ACES filmic fit", "adapted", "NOASSERTION",
     "resources/shader/gfx_bloom_composite.frag", None,
     "Krzysztof Narkowicz tone-mapping fit; see NOTICE"),
    ("AgX display transform", "adapted", "NOASSERTION",
     "resources/shader/gfx_bloom_composite.frag", None,
     "Troy Sobotka reference lineage; see NOTICE"),
    ("NTSC/CRT", "adapted", "NOASSERTION", "src/external/crt_core.c", None,
     "EMMIR permissive upstream terms; see license/ntsc-crt/LICENSE"),
    ("Open Dynamics Engine", "system", "LGPL-2.1-only", None,
     "pkg:generic/ode", "optional system dependency, not vendored"),
    ("Lean 4 / Mathlib4", "proof-pin", "Apache-2.0", None, None,
     "separate gx-proofs build dependency; not linked into the executable"),
    ("Kenney Mini Square Mono", "1.0", "CC0-1.0",
     "resources/font/KenneyMiniSquareMono.ttf", None, ""),
    ("European Teletext", "1.0", "CC0-1.0",
     "resources/font/EuropeanTeletext.ttf", None, ""),
    ("Skybox1", "1.0", "CC-BY-4.0",
     "resources/texture/skybox/skybox1.png", None,
     "learnopengl.com (Joey de Vries)"),
)

ALLOWED_LICENSES = {
    "GPL-3.0-or-later", "Zlib", "MIT", "MIT OR Unlicense", "Unlicense",
    "BSD-3-Clause", "Apache-2.0", "LGPL-2.1-only", "CC0-1.0", "CC-BY-4.0",
    "NOASSERTION",
}


def version_string():
    parsed = version_check.readme_version()
    if not parsed:
        raise SystemExit("SBOM: cannot read the gX version")
    major, minor, patch, variant = parsed
    version = "%d.%d.%d" % (major, minor, patch)
    return version + ("_" + variant if variant else "")


def created_timestamp(version):
    text = version_check.CHANGELOG.read_text()
    match = re.search(r"^## \[" + re.escape(version) + r"\] - (\d{4}-\d{2}-\d{2})$",
                      text, re.M)
    return (match.group(1) + "T00:00:00Z") if match else "1970-01-01T00:00:00Z"


def spdx_id(name):
    cleaned = re.sub(r"[^A-Za-z0-9.-]+", "-", name).strip("-")
    return "SPDXRef-Package-" + cleaned


def gx_package(version):
    return {
        "SPDXID": "SPDXRef-Package-gX",
        "name": "gX",
        "versionInfo": version,
        "supplier": "Organization: gX project",
        "downloadLocation": "https://github.com/lanttusoft/gX",
        "filesAnalyzed": False,
        "licenseConcluded": "GPL-3.0-or-later",
        "licenseDeclared": "GPL-3.0-or-later",
        "copyrightText": "Copyright (c) 2025-2026 lanttu, opencode",
        "primaryPackagePurpose": "APPLICATION",
        "externalRefs": [{
            "referenceCategory": "PACKAGE-MANAGER",
            "referenceType": "purl",
            "referenceLocator": "pkg:github/lanttusoft/gX@v" + version,
        }],
    }


def component_package(component):
    name, version, license_id, path, purl, comment = component
    package = {
        "SPDXID": spdx_id(name),
        "name": name,
        "versionInfo": version,
        "supplier": "NOASSERTION",
        "downloadLocation": "NOASSERTION",
        "filesAnalyzed": False,
        "licenseConcluded": license_id,
        "licenseDeclared": license_id,
        "copyrightText": "NOASSERTION",
    }
    if purl:
        package["externalRefs"] = [{
            "referenceCategory": "PACKAGE-MANAGER",
            "referenceType": "purl",
            "referenceLocator": purl,
        }]
    if comment:
        package["comment"] = comment
    if path:
        package["sourceInfo"] = path
    return package


def document():
    version = version_string()
    packages = [gx_package(version)]
    packages += [component_package(component) for component in COMPONENTS]
    relationships = [{
        "spdxElementId": "SPDXRef-DOCUMENT",
        "relationshipType": "DESCRIBES",
        "relatedSpdxElement": "SPDXRef-Package-gX",
    }]
    relationships += [{
        "spdxElementId": "SPDXRef-Package-gX",
        "relationshipType": "DEPENDS_ON",
        "relatedSpdxElement": spdx_id(component[0]),
    } for component in COMPONENTS]
    return {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": "gX-" + version,
        "documentNamespace": DOCUMENT_NAMESPACE % version,
        "creationInfo": {
            "created": created_timestamp(version),
            "creators": ["Tool: gX-sbom-1.0", "Organization: gX project"],
        },
        "packages": packages,
        "relationships": relationships,
    }


def render():
    return json.dumps(document(), indent=2, sort_keys=True) + "\n"


def validate(text):
    issues = []
    try:
        data = json.loads(text)
    except json.JSONDecodeError as error:
        return ["invalid JSON: %s" % error]
    if data.get("spdxVersion") != "SPDX-2.3":
        issues.append("spdxVersion is not SPDX-2.3")
    if data.get("dataLicense") != "CC0-1.0":
        issues.append("dataLicense is not CC0-1.0")
    packages = data.get("packages", [])
    ids = [package.get("SPDXID") for package in packages]
    if len(ids) != len(set(ids)):
        issues.append("duplicate SPDXIDs")
    version = version_string()
    gx = packages[0] if packages else {}
    if gx.get("name") != "gX" or gx.get("versionInfo") != version:
        issues.append("gX package version does not match readme.txt")
    for package in packages:
        name = package.get("name", "?")
        license_id = package.get("licenseDeclared")
        if license_id not in ALLOWED_LICENSES:
            issues.append("%s: unexpected license %r" % (name, license_id))
        source = package.get("sourceInfo")
        if source and not (ROOT / source).exists():
            issues.append("%s: missing source path %s" % (name, source))
    for path in LICENSE_FILES:
        if not (ROOT / path).is_file():
            issues.append("missing license file " + path)
    return issues


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--write", action="store_true",
                       help="write sbom/gX.spdx.json")
    group.add_argument("--check", action="store_true",
                       help="fail when the committed SBOM is stale")
    args = parser.parse_args()

    text = render()
    issues = validate(text)
    if issues:
        for issue in issues:
            print("SBOM: " + issue)
        return 1
    if args.write:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT.write_text(text, encoding="utf-8")
        print("SBOM: wrote %s" % OUTPUT.relative_to(ROOT))
        return 0
    if args.check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.is_file() else ""
        if current != text:
            print("SBOM: %s is stale; run python3 scripts/sbom.py --write"
                  % OUTPUT.relative_to(ROOT))
            return 1
        print("SBOM: PASS — %d packages, SPDX 2.3, committed document is current"
              % (len(document()["packages"])))
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print("SBOM: FAIL — %s" % error)
        sys.exit(1)
