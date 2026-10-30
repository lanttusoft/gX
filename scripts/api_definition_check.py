#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: declared-versus-defined public API audit (AUD-03). Declarations come from
#   src/**.h (backend/external/game skipped, like api_coverage.py); definitions
#   come from every normal-build object (T/W symbols), so linker-dropped
#   declarations are still visible. module-only gx_shell families are excluded
#   from the default-engine baseline and listed separately in the report.
#
#   Usage:
#     python3 scripts/api_definition_check.py
#     python3 scripts/api_definition_check.py --strict
#     python3 scripts/api_definition_check.py --json
#     python3 scripts/api_definition_check.py --report docs/api_definition_audit_2026-10.md

import argparse
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
OBJDIR = ROOT / "build" / "CMakeFiles" / "gX_build.dir"
SKIP_DIRS = {"backend", "external", "game"}
#ai: gx_shell-only public families — definitions live in the module build, not
#   in the default engine objects. The report lists them as profile-provided.
PROFILE_PREFIXES = ("gX_Console_", "gX_Script_", "gX_Scripting_", "gX_Tour")

DECL_RE = re.compile(
    r'^\s*(?:extern\s+)?(?:const\s+)?[A-Za-z_][A-Za-z0-9_]*\s*\*?\s*'
    r'(gX_[A-Za-z0-9_]+)\s*\(')

#ai: checked baseline of default-engine declarations with no object definition.
#   The gate fails only on names outside this set; --strict fails on the full
#   known list too. Remove an entry when its definition lands.
KNOWN_MISSING = frozenset({
    "gX_Audio_Music_LoadResourceName",
    "gX_BoundingBox_Draw",
    "gX_Camera2D_ConvertFromBackend",
    "gX_Camera2D_ConvertToBackend",
    "gX_Camera_GetScreenToWorld2D",
    "gX_Camera_GetWorldToScreen2D",
    "gX_Camera_GiveTarget",
    "gX_Camera_RemoveTarget",
    "gX_Capsule_Draw",
    "gX_DrawBillboard_CharacterTextures",
    "gX_Ellipse_Draw",
    "gX_GUI_Ephemeris_SetInstance",
    "gX_Material_IsReady",
    "gX_Material_SetTexture",
    "gX_Material_Unload",
    "gX_MeshPtr_ConvertToBackend",
    "gX_Mesh_GenCone",
    "gX_Mesh_GenCubicMap",
    "gX_Mesh_GenHeightMap",
    "gX_Mesh_GenHemiSphere",
    "gX_Mesh_GenPoly",
    "gX_Mesh_Upload",
    "gX_Model_AttachTexture",
    "gX_Model_GetBoundingBox",
    "gX_Model_IsReady",
    "gX_Model_SetMeshMaterial",
    "gX_Msg_Show",
    "gX_Music_IsPlaying",
    "gX_Music_Update",
    "gX_Plane_Draw",
    "gX_Polygon_Draw",
    "gX_RayCollision_Box",
    "gX_RayCollision_Triangle",
    "gX_Ray_Draw",
    "gX_RendererBackground_LoadClear",
    "gX_RendererBackground_LoadRandom",
    "gX_RendererBackground_Misc_Reset",
    "gX_RendererBackground_State_Reset",
    "gX_RendererGFX_SSR_CacheLocs",
    "gX_RendererGFX_Update_Bloom",
    "gX_RendererGFX_Update_DOF",
    "gX_RendererGFX_Update_MotionBlur",
    "gX_RendererGFX_Update_TAA",
    "gX_RendererPostProcess_Preset_Load",
    "gX_RendererPostProcess_Preset_Save",
    "gX_Ring_Draw",
    "gX_Shader_Load_Prototype",
    "gX_Sound_PlayPtr",
    "gX_Sound_PlayPtr_Parameters",
    "gX_Sound_Ready",
    "gX_TextGetter_AddLine",
    "gX_TextGetter_ClearLines",
    "gX_TextGetter_Load",
    "gX_TextGetter_Unload",
    "gX_TextHandler_AddLine",
    "gX_TextHandler_ClearLines",
    "gX_TextHandler_Load",
    "gX_TextHandler_Unload",
    "gX_Texture_GenMipmaps",
    "gX_Triangle3D_Draw",
    "gX_Wave_Copy",
    "gX_Wave_Crop",
    "gX_Wave_Ready",
    "gX_asm_dumpRegisters",
    "gX_asm_helloworld",
})

IMPLEMENT_PREFIXES = (
    "gX_BoundingBox_", "gX_Camera_", "gX_Camera2D_", "gX_Capsule_",
    "gX_Ellipse_", "gX_Material_", "gX_Mesh_", "gX_MeshPtr_",
    "gX_Model_", "gX_Music_", "gX_Plane_", "gX_Polygon_", "gX_Ray_",
    "gX_RayCollision_", "gX_Ring_", "gX_Sound_", "gX_TextGetter_",
    "gX_TextHandler_", "gX_Texture_", "gX_Triangle3D_", "gX_Wave_",
)
PRUNE_PREFIXES = (
    "gX_asm_", "gX_DrawBillboard_", "gX_GUI_Ephemeris_", "gX_Msg_",
    "gX_RendererBackground_", "gX_RendererGFX_Update_",
    "gX_RendererGFX_SSR_CacheLocs", "gX_RendererPostProcess_Preset_",
    "gX_Shader_Load_Prototype", "gX_Audio_Music_LoadResourceName",
    "gX_Model_AttachTexture",
)


def mask_comments(text):
    out = []
    i = 0
    block = False
    string = False
    while i < len(text):
        char = text[i]
        if block:
            if text.startswith("*/", i):
                block = False
                i += 2
            else:
                i += 1
        elif string:
            if char == "\\":
                i += 2
            elif char == '"':
                string = False
                out.append(char)
                i += 1
            else:
                out.append(" ")
                i += 1
        elif text.startswith("//", i):
            newline = text.find("\n", i)
            i = len(text) if newline < 0 else newline
        elif text.startswith("/*", i):
            block = True
            i += 2
        elif char == '"':
            string = True
            out.append(char)
            i += 1
        else:
            out.append(char)
            i += 1
    return "".join(out)


def _condition_false(condition):
    condition = condition.strip()
    return condition in {"0", "GX_GUI_USE_POOLS"} or condition.startswith("0 ")


def collect_declarations(src=SRC):
    declarations = {}
    for path in sorted(Path(src).rglob("*.h")):
        relative = path.relative_to(ROOT) if path.is_relative_to(ROOT) else path
        if path.relative_to(src).parts[0] in SKIP_DIRS:
            continue
        lines = mask_comments(path.read_text(errors="ignore")).splitlines()
        active = []
        for index, line in enumerate(lines):
            stripped = line.strip()
            if stripped.startswith("#if "):
                active.append(not _condition_false(stripped[4:]))
            elif stripped.startswith("#ifdef ") or stripped.startswith("#ifndef "):
                active.append(True)
            elif stripped.startswith("#else"):
                if active:
                    active[-1] = not active[-1]
            elif stripped.startswith("#elif"):
                if active:
                    active[-1] = True
            elif stripped.startswith("#endif"):
                if active:
                    active.pop()
            if active and not all(active):
                continue
            if stripped.startswith("#") or "define" in line[:12]:
                continue
            if "typedef" in line or "(*" in line:
                continue
            if re.search(r"\bstatic\b", line):
                continue
            match = DECL_RE.match(line)
            if not match:
                continue
            name = match.group(1)
            if name.startswith(PROFILE_PREFIXES):
                continue
            # A definition in the header (inline body) needs no object symbol.
            if "{" in line[line.find("("):]:
                continue
            following = next((item for item in lines[index + 1:index + 4]
                              if item.strip()), "")
            if following.lstrip().startswith("{"):
                continue
            declarations.setdefault(name, (str(relative), index + 1))
    return declarations


def parse_nm_output(text):
    definitions = set()
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0].startswith("gX_") \
                and fields[1] in {"T", "W"}:
            definitions.add(fields[0])
    return definitions


def collect_definitions(objdir=OBJDIR):
    object_dir = Path(objdir)
    if not object_dir.is_dir():
        raise RuntimeError(f"object directory missing: {object_dir} (build first)")
    definitions = set()
    for obj in sorted(object_dir.rglob("*.o")):
        result = subprocess.run(["nm", "--defined-only", "--format=posix",
                                 str(obj)], capture_output=True, text=True)
        if result.returncode != 0:
            raise RuntimeError(f"nm failed for {obj}: {result.stderr.strip()}")
        definitions |= parse_nm_output(result.stdout)
    return definitions


def collect_callers(objdir=OBJDIR):
    object_dir = Path(objdir)
    callers = Counter()
    for obj in sorted(object_dir.rglob("*.o")):
        result = subprocess.run(["nm", "--undefined-only", "--format=posix",
                                 str(obj)], capture_output=True, text=True)
        if result.returncode != 0:
            continue
        for line in result.stdout.splitlines():
            fields = line.split()
            if fields and fields[0].startswith("gX_"):
                callers[fields[0]] += 1
    return callers


def recommendation(name):
    if name.startswith(IMPLEMENT_PREFIXES):
        return "implement"
    if name.startswith(PRUNE_PREFIXES):
        return "prune"
    return "review"


def build_report(declarations, missing, callers, objects):
    rows = []
    for index, name in enumerate(missing, 1):
        relative, line = declarations[name]
        rows.append(
            f"| API-{index:03d} | `{name}` | `{relative}:{line}` | "
            f"{callers.get(name, 0)} | {recommendation(name)} | baseline |")
    return "\n".join([
        "# Public API definition audit — 2026-10-21",
        "",
        "<!-- //ai: 2026-10-21 declared-vs-defined public surface; read-only audit. -->",
        "",
        "## Method",
        "",
        f"Declarations: every `gX_*` function signature in `src/**/*.h`, skipping",
        "`backend/`, `external/` and `game/` exactly like `api_coverage.py`.",
        "Definitions: `T`/`W` symbols from every normal-build object under",
        f"`build/CMakeFiles/gX_build.dir` ({objects} objects), so declarations",
        "dropped by the linker are still visible. Module-only `gx_shell` families",
        "(`gX_Console_*`, `gX_Script*`, `gX_Scripting_*`, `gX_Tour*`) are excluded",
        "from this default-engine list; they are provided by the shell profile.",
        "Callers: objects with an undefined `gX_*` reference (0 means no caller in",
        "the default build).",
        "",
        "This is a declaration audit only: no header or definition was edited.",
        "`make api-definition-check` is the live gate; `--strict` asks for a full",
        "clean surface and therefore fails on every row below.",
        "",
        f"## Known missing declarations ({len(missing)})",
        "",
        "| ID | Declaration | Evidence | Callers | Recommendation | Gate |",
        "|---|---|---|---:|---|---|",
        *rows,
        "",
        "## Recommendation policy",
        "",
        "- **implement** — public wrapper/geometry/audio/text family that is part",
        "  of the documented engine surface; add the definition (or a deliberate",
        "  inline/macro implementation) with its own gate.",
        "- **prune** — stale, experimental, renamed, or profile-specific",
        "  declaration; remove it from the public header rather than carrying a",
        "  dead declaration. This task does not prune anything.",
        "- **review** — no safe automatic call; main decides.",
        "",
        "Known list is a committed baseline: new names fail",
        "`make api-definition-check`; removing a definition from this list is a",
        "baseline cleanup, not an API change.",
        "",
    ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true",
                        help="fail on the full known missing list, not only new drift")
    parser.add_argument("--json", action="store_true", help="machine-readable output")
    parser.add_argument("--report", type=Path,
                        help="write the audit report to this path")
    parser.add_argument("--src", type=Path, default=SRC)
    parser.add_argument("--objdir", type=Path, default=OBJDIR)
    args = parser.parse_args()

    try:
        declarations = collect_declarations(args.src)
        definitions = collect_definitions(args.objdir)
    except RuntimeError as error:
        print(f"API_DEFINITION: FAIL — {error}", file=sys.stderr)
        return 1

    missing = sorted(set(declarations) - definitions)
    new = sorted(set(missing) - KNOWN_MISSING)
    stale = sorted(KNOWN_MISSING - set(missing))
    callers = collect_callers(args.objdir) if args.report else Counter()

    if args.report:
        args.report.write_text(
            build_report(declarations, missing, callers,
                         len(list(Path(args.objdir).rglob("*.o")))),
            encoding="utf-8")

    if args.json:
        print(json.dumps({
            "declarations": len(declarations),
            "definitions": len(definitions),
            "missing": missing,
            "new": new,
            "stale": stale,
        }, indent=2, sort_keys=True))
    else:
        print(f"API_DEFINITION: {len(declarations)} declarations, "
              f"{len(definitions)} definitions, {len(missing)} missing "
              f"({len(KNOWN_MISSING)} known baseline, {len(new)} new)")
        for name in new:
            print(f"  NEW  {name} ({declarations[name][0]}:{declarations[name][1]})")
        for name in stale:
            print(f"  NOTE baseline entry now defined: {name}")

    if new:
        return 1
    if args.strict and missing:
        print(f"API_DEFINITION: STRICT FAIL — {len(missing)} known missing remain")
        return 1
    print("API_DEFINITION: PASS"
          + (" (strict clean)" if args.strict else " (baseline)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
