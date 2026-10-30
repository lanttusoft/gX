#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: conservative static checks for high-confidence GFX integration mistakes.
"""Find a few mechanical GFX hazards without requiring a build or GL context."""

import argparse
import re
import sys
from pathlib import Path

import gl_state_audit


SOURCE_EXTENSIONS = {".c", ".cpp", ".h", ".hpp"}
#ai: caller-managed helpers can inherit an active program, so order checks require a local activation.
EXPLICIT_SHADER_ACTIVATION = re.compile(
    r"\b(?:gX_RHI_Shader_Use|gX_Drawing_BeginShader|"
    r"gX_RendererGFX_Fullscreen_Begin)\s*\("
)
SHADER_SETTER = re.compile(r"\bgX_Shader_SetValue\w*\s*\(")
RAW_UNIFORM_UPLOAD = re.compile(r"\bgX_RHI_Uniform\w*\s*\(")
TEXTURE_UNIT_DEFINE = re.compile(
    r"(?m)^[ \t]*#define\s+(gX_GFX_TEXUNIT_\w+)\s+([^\s/]+)"
)
TEXTURE_SLOT_CALLS = (
    (re.compile(r"\bgX_RHI_Tex_Bind(?:Cubemap)?OnUnit\s*\("), 1, "RHI bind"),
    (re.compile(r"\bgX_Shader_SetValue_(?:Texture|Cubemap)Slot\s*\("),
     3, "shader texture-slot setter"),
)
INTEGER_LITERAL = re.compile(r"^([+-]?(?:0[xX][0-9a-fA-F]+|[0-9]+))([uUlL]*)$")
CMAKE_BRACKET_COMMENT = re.compile(r"#\[(=*)\[(.*?)\]\1\]", re.DOTALL)
MAX_TEXTURE_UNIT = 31
SKIN_ATTRIBUTES = (
    ("vertexBoneIndices", 7),
    ("vertexBoneWeights", 8),
)
SKIN_SHADER_FILES = (
    "resources/shader/standard.vert",
    "resources/shader/gfx_depth.vert",
    "resources/shader/gfx_pointdepth.vert",
)
#ai: the renderer cap aliases this shared C constant; shader arrays must match it.
SKIN_CAP_SHARED_HEADER = "src/common/types/3d.h"
#ai: protect canonical material uploads from hardcoded feature, factor, or map-presence values.
MATERIAL_UNIFORM_SOURCES = (
    #ai: emission controls must retain authored per-model sources, including an explicit zero.
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_Float", "rshader->locations.emissiveFactorLoc",
     "model->params.emissiveFactor", "emissiveFactor"),
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_Color", "rshader->locations.emissiveColorLoc",
     "model->params.emissiveColor", "emissiveColor"),
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_Float", "rshader->locations.emissiveMapFactorLoc",
     "model->params.emissiveMapFactor", "emissiveMapFactor"),
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_V2", "rshader->locations.emissiveMapScaleLoc",
     "model->params.emissiveMapScale", "emissiveMapScale"),
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_V2", "rshader->locations.emissiveMapOffsetLoc",
     "model->params.emissiveMapOffset", "emissiveMapOffset"),
    ("src/renderer/gfx/shader.c", "gX_RendererGFX_Shader_SetValues",
     "gX_Shader_SetValue_Int", "shader->locations.enableMapAlbedoLoc",
     "gfx->state.ENABLE_MAP_ALBEDO", "enableMapAlbedo"),
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_Float", "rshader->locations.albedoFactorLoc",
     "model->params.albedo", "albedoFactor"),
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_Float", "rshader->locations.albedoMapFactorLoc",
     "model->params.albedoMapFactor", "albedoMapFactor"),
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_Int", "rshader->locations.hasAlbedoMapLoc",
     "model->params.hasAlbedoMap", "hasAlbedoMap"),
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_Int", "rshader->locations.hasMetalnessMapLoc",
     "model->params.hasMetalnessMap", "hasMetalnessMap"),
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_Int", "rshader->locations.hasNormalMapLoc",
     "model->params.hasNormalMap", "hasNormalMap"),
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_Int", "rshader->locations.hasHeightMapLoc",
     "model->params.hasHeightMap", "hasHeightMap"),
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_Int", "rshader->locations.hasEmissiveMapLoc",
     "model->params.hasEmissiveMap", "hasEmissiveMap"),
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_Int", "rshader->locations.hasRoughnessMapLoc",
     "model->params.hasRoughnessMap", "hasRoughnessMap"),
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_Int", "rshader->locations.hasAoMapLoc",
     "model->params.hasAoMap", "hasAoMap"),
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_Int", "rshader->locations.hasGlossMapLoc",
     "model->params.hasGlossMap", "hasGlossMap"),
    ("src/renderer/gfx/model.c", "gX_RendererGFX_Model_Draw",
     "gX_Shader_SetValue_Int", "rshader->locations.hasLightmapLoc",
     "(!depthMode && lightmapEnabled) ? 1 : 0", "hasLightmap"),
)
MESH_MAP_FLAG_PATTERNS = (
    ("reset", re.compile(r"\bgX_I32\s+mapFlags\s*=\s*0\s*;"),
     "reset map-presence bits for each mesh"),
    ("texture", re.compile(r"\bgX_Texture\s+texture\s*=\s*\{\s*0\s*\}\s*;"),
     "start each selected map empty"),
    ("mesh", re.compile(
        r"if\s*\(\s*meshMat->maps\s*\)\s*texture\s*=\s*"
        r"meshMat->maps\s*\[\s*mapIndex\s*\]\.texture\s*;"),
     "read the current mesh's map"),
    ("injected", re.compile(
        r"if\s*\(\s*injectedMaps\s*\[\s*mapIndex\s*\]\s*>\s*0\s*\)\s*"
        r"texture\.id\s*=\s*injectedMaps\s*\[\s*mapIndex\s*\]\s*;"),
     "apply injected map overrides before presence testing"),
    ("albedo_override", re.compile(
        r"if\s*\(\s*mapIndex\s*==\s*gX_MATERIAL_MAP_ALBEDO\s*"
        r"&&\s*\(\s*mask\s*&\s*gX_GFX_MMASK_ALBEDO_MAP\s*\)\s*"
        r"&&\s*model->params\.effectiveAlbedoMap\s*\[\s*meshIndex\s*\]\s*>\s*0\s*\)\s*"
        r"\{\s*texture\.id\s*=\s*\(\s*gX_U32\s*\)\s*"
        r"model->params\.effectiveAlbedoMap\s*\[\s*meshIndex\s*\]\s*;\s*\}"),
     "apply the effective albedo override before presence testing"),
    ("emissive_override", re.compile(
        r"if\s*\(\s*mapIndex\s*==\s*gX_MATERIAL_MAP_EMISSION\s*"
        r"&&\s*\(\s*mask\s*&\s*gX_GFX_MMASK_EMISSIVE_MAP\s*\)\s*"
        r"&&\s*model->params\.effectiveEmissiveMap\s*\[\s*meshIndex\s*\]\s*>\s*0\s*\)\s*"
        r"\{\s*texture\.id\s*=\s*\(\s*gX_U32\s*\)\s*"
        r"model->params\.effectiveEmissiveMap\s*\[\s*meshIndex\s*\]\s*;\s*\}"),
     "apply the effective emissive override before presence testing"),
    ("presence", re.compile(
        r"if\s*\(\s*texture\.id\s*>\s*0\s*\)\s*"
        r"mapFlags\s*\|=\s*1\s*<<\s*mapIndex\s*;"),
     "mark a map present only when its selected texture ID is valid"),
    ("fallback", re.compile(
        r"if\s*\(\s*texture\.id\s*==\s*0\s*\)\s*"
        r"texture\.id\s*=\s*gX_RHI_Tex_GetDefault\s*\(\s*\)\s*;"),
     "bind the white fallback only after map presence is determined"),
    ("cache_guard", re.compile(
        r"if\s*\(\s*boundFlags\s*&&\s*\*\s*boundFlags\s*!=\s*mapFlags\s*\)"),
     "refresh flags when the current mesh differs from the cached mesh"),
    ("upload", re.compile(
        r"gX_Shader_SetValue_Int\s*\(\s*shaderProg\s*,\s*"
        r"flagLocations\s*\[\s*mapIndex\s*\]\s*,\s*"
        r"\(\s*mapFlags\s*>>\s*mapIndex\s*\)\s*&\s*1\s*\)\s*;"),
     "upload each has-map flag from the corresponding presence bit"),
    ("cache_update", re.compile(r"\*\s*boundFlags\s*=\s*mapFlags\s*;"),
     "cache the just-uploaded map flags"),
)
#ai: effect-resource flags require a resource-present upload and an explicit absent reset.
EFFECT_RESOURCE_FLAG_PATHS = (
    {
        "relative": "src/renderer/gfx/motionblur.c",
        "function": "gX_RendererGFX_MotionBlur_Draw",
        "label": "motion-blur hasVelocityBuffer",
        "present_condition": "!noVelocity && gfx->velocity.map.wrapper.loadingState.loaded && gfx->velocity.hasPrevVP",
        "present_calls": (
            ("gX_Shader_SetValue_TextureSlot", ("mbS", "gfx->motionBlur.velocityMapLoc", "gfx->velocity.map.wrapper.rt.texture", "gX_GFX_TEXUNIT_VELOCITY")),
            ("gX_Shader_SetValue_Int", ("mbS", "gfx->motionBlur.hasVelocityLoc", "1")),
        ),
        "present_guards": (
            ("gfx->motionBlur.velocityMapLoc >= 0", "gX_Shader_SetValue_TextureSlot", ("mbS", "gfx->motionBlur.velocityMapLoc", "gfx->velocity.map.wrapper.rt.texture", "gX_GFX_TEXUNIT_VELOCITY")),
            ("gfx->motionBlur.hasVelocityLoc >= 0", "gX_Shader_SetValue_Int", ("mbS", "gfx->motionBlur.hasVelocityLoc", "1")),
        ),
        "absent_condition": "gfx->motionBlur.hasVelocityLoc >= 0",
        "absent_calls": (("gX_Shader_SetValue_Int", ("mbS", "gfx->motionBlur.hasVelocityLoc", "0")),),
    },
    {
        "relative": "src/renderer/gfx/taa.c",
        "function": "gX_RendererGFX_TAA_Resolve",
        "label": "TAA hasVelocityBuffer",
        "present_condition": "gfx->velocity.map.wrapper.loadingState.loaded && gfx->velocity.hasPrevVP",
        "present_calls": (
            ("gX_Shader_SetValue_TextureSlot", ("ts", "gfx->taa.velocityMapLoc", "gfx->velocity.map.wrapper.rt.texture", "gX_GFX_TEXUNIT_VELOCITY")),
            ("gX_Shader_SetValue_Int", ("ts", "gfx->taa.hasVelocityLoc", "noVelocity ? 2 : 1")),
        ),
        "present_guards": (
            ("gfx->taa.velocityMapLoc >= 0", "gX_Shader_SetValue_TextureSlot", ("ts", "gfx->taa.velocityMapLoc", "gfx->velocity.map.wrapper.rt.texture", "gX_GFX_TEXUNIT_VELOCITY")),
            ("gfx->taa.hasVelocityLoc >= 0", "gX_Shader_SetValue_Int", ("ts", "gfx->taa.hasVelocityLoc", "noVelocity ? 2 : 1")),
        ),
        "absent_condition": "gfx->taa.hasVelocityLoc >= 0",
        "absent_calls": (("gX_Shader_SetValue_Int", ("ts", "gfx->taa.hasVelocityLoc", "0")),),
    },
    {
        "relative": "src/renderer/gfx/draw.c",
        "function": "gX_RendererGFX_SSGI_Draw",
        "label": "SSGI accumulation hasVelocity",
        "present_condition": "gfx->velocity.map.wrapper.loadingState.loaded && gfx->velocity.hasPrevVP",
        "present_calls": (
            ("gX_Shader_SetValue_TextureSlot", ("acS", "gfx->ssao.ssgiAccVelLoc", "gfx->velocity.map.wrapper.rt.texture", "gX_GFX_TEXUNIT_VELOCITY")),
            ("gX_Shader_SetValue_Int", ("acS", "gfx->ssao.ssgiAccHasVelLoc", "1")),
        ),
        "present_guards": (),
        "absent_condition": None,
        "absent_calls": (("gX_Shader_SetValue_Int", ("acS", "gfx->ssao.ssgiAccHasVelLoc", "0")),),
    },
    {
        "relative": "src/renderer/gfx/draw.c",
        "function": "gX_RendererGFX_SSAO_Draw",
        "label": "GTAO accumulation hasVelocity",
        "present_condition": "gfx->velocity.map.wrapper.loadingState.loaded && gfx->velocity.hasPrevVP",
        "present_calls": (
            ("gX_Shader_SetValue_TextureSlot", ("acS", "gfx->ssao.gtaoAccVelLoc", "gfx->velocity.map.wrapper.rt.texture", "gX_GFX_TEXUNIT_VELOCITY")),
            ("gX_Shader_SetValue_Int", ("acS", "gfx->ssao.gtaoAccHasVelLoc", "1")),
        ),
        "present_guards": (),
        "absent_condition": None,
        "absent_calls": (("gX_Shader_SetValue_Int", ("acS", "gfx->ssao.gtaoAccHasVelLoc", "0")),),
    },
    {
        "relative": "src/renderer/gfx/shader.c",
        "function": "gX_RendererGFX_Shader_SetValues",
        "label": "forward hasFogLut",
        "present_condition": "shader->locations.fogLutMapLoc >= 0 && gfx->ssr.fogLut.wrapper.loadingState.loaded && gfx->state.ENABLE_VOLUME_FOG",
        "present_calls": (
            ("gX_Shader_SetValue_TextureSlot", ("shader->shader", "shader->locations.fogLutMapLoc", "gfx->ssr.fogLut.wrapper.rt.texture", "gX_GFX_TEXUNIT_FOG_LUT")),
            ("gX_RHI_UniformInt", ("shader->locations.hasFogLutLoc", "1")),
        ),
        "present_guards": (("shader->locations.hasFogLutLoc >= 0", "gX_RHI_UniformInt", ("shader->locations.hasFogLutLoc", "1")),),
        "absent_condition": "shader->locations.hasFogLutLoc >= 0",
        "absent_calls": (("gX_RHI_UniformInt", ("shader->locations.hasFogLutLoc", "0")),),
    },
    {
        "relative": "src/renderer/gfx/draw.c",
        "function": "gX_RendererGFX_Draw_SSR",
        "label": "SSR hasFogLut",
        "present_condition": "gfx->state.ENABLE_VOLUME_FOG && gfx->ssr.fogLutMapLoc >= 0 && gfx->ssr.fogLut.wrapper.loadingState.loaded",
        "present_calls": (
            ("gX_Shader_SetValue_TextureSlot", ("sha", "gfx->ssr.fogLutMapLoc", "gfx->ssr.fogLut.wrapper.rt.texture", "gX_GFX_TEXUNIT_FOG_LUT")),
            ("gX_RHI_UniformInt", ("gfx->ssr.fogLutMapLoc", "gX_GFX_TEXUNIT_FOG_LUT")),
            ("gX_RHI_UniformInt", ("gfx->ssr.hasFogLutLoc", "1")),
        ),
        "present_guards": (("gfx->ssr.hasFogLutLoc >= 0", "gX_RHI_UniformInt", ("gfx->ssr.hasFogLutLoc", "1")),),
        "absent_condition": "gfx->ssr.hasFogLutLoc >= 0",
        "absent_calls": (("gX_Shader_SetValue_Int", ("sha", "gfx->ssr.hasFogLutLoc", "0")),),
    },
    {
        "relative": "src/renderer/gfx/draw.c",
        "function": "gX_RendererGFX_Draw_SSR",
        "label": "SSR hasVelocityMap",
        "present_condition": "gfx->velocity.map.wrapper.loadingState.loaded",
        "present_calls": (
            ("gX_Shader_SetValue_TextureSlot", ("sha", "gfx->ssr.velMapLoc", "gfx->velocity.map.wrapper.rt.texture", "gX_GFX_TEXUNIT_SSR_VELOCITY")),
            ("gX_Shader_SetValue_Int", ("sha", "gfx->ssr.hasVelMapLoc", "1")),
        ),
        "present_guards": (("gfx->ssr.hasVelMapLoc >= 0", "gX_Shader_SetValue_Int", ("sha", "gfx->ssr.hasVelMapLoc", "1")),),
        "absent_condition": "gfx->ssr.hasVelMapLoc >= 0",
        "absent_calls": (("gX_Shader_SetValue_Int", ("sha", "gfx->ssr.hasVelMapLoc", "0")),),
    },
)


def renderer_source_files(root):
    source_root = root / "src" / "renderer"
    if not source_root.is_dir():
        return
    for path in sorted(source_root.rglob("*")):
        if path.is_file() and path.suffix in SOURCE_EXTENSIONS:
            yield path


def audit_uniform_activation_order(root, findings):
    for path in renderer_source_files(root):
        text = path.read_text(encoding="utf-8", errors="replace")
        masked = gl_state_audit.mask_c_source(text)
        relative = str(path.relative_to(root))
        for function, _, brace, end in gl_state_audit.function_regions(masked):
            body = masked[brace:end]
            explicit_activations = list(EXPLICIT_SHADER_ACTIVATION.finditer(body))
            uploads = list(RAW_UNIFORM_UPLOAD.finditer(body))
            if not explicit_activations or not uploads:
                continue
            activations = explicit_activations + list(SHADER_SETTER.finditer(body))
            first_activation = min(match.start() for match in activations)
            preceding = [match for match in uploads if match.start() < first_activation]
            if not preceding:
                continue
            first_upload = min(preceding, key=lambda match: match.start())
            line = gl_state_audit.line_number(text, brace + first_upload.start())
            findings.append((
                "WARN", relative, line, "GFX001",
                f"raw uniform upload precedes shader activation in {function}(); "
                "activate the intended program before uploading uniforms",
            ))


def audit_skinning_capacity(root, findings):
    constants_path = root / "src" / "renderer" / "gfx" / "constants.h"
    if not constants_path.is_file():
        findings.append(("ERROR", str(constants_path.relative_to(root)), 0,
                         "GFX002", "missing GFX constants header"))
        return
    constants = constants_path.read_text(encoding="utf-8", errors="replace")
    cap_match = re.search(
        r"(?m)^\s*#define\s+gX_GFX_SKIN_MAX_BONES\s+"
        r"(\d+|[A-Za-z_][A-Za-z0-9_]*)\b",
        constants,
    )
    if not cap_match:
        findings.append(("ERROR", str(constants_path.relative_to(root)), 0,
                         "GFX002", "missing numeric or aliased gX_GFX_SKIN_MAX_BONES definition"))
        return
    cap_token = cap_match.group(1)
    constants_line = gl_state_audit.line_number(constants, cap_match.start())
    if cap_token.isdigit():
        cap = int(cap_token)
    else:
        shared_path = root / SKIN_CAP_SHARED_HEADER
        shared = shared_path.read_text(encoding="utf-8", errors="replace") \
            if shared_path.is_file() else ""
        shared_match = re.search(
            rf"(?m)^\s*#define\s+{re.escape(cap_token)}\s+(\d+)\b", shared
        )
        if not shared_match:
            findings.append((
                "ERROR", str(constants_path.relative_to(root)), constants_line,
                "GFX002",
                f"unresolved gX_GFX_SKIN_MAX_BONES alias '{cap_token}' "
                f"(expected a numeric #define in {SKIN_CAP_SHARED_HEADER})",
            ))
            return
        cap = int(shared_match.group(1))

    for relative in SKIN_SHADER_FILES:
        path = root / relative
        if not path.is_file():
            findings.append(("ERROR", relative, 0, "GFX002",
                             f"missing shader; expected boneMatrices[{cap}]"))
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        declaration = re.search(
            r"\buniform\s+mat4\s+boneMatrices\s*\[\s*(\d+)\s*\]\s*;",
            gl_state_audit.mask_c_source(text),
        )
        if not declaration:
            findings.append(("ERROR", relative, 0, "GFX002",
                             f"missing numeric mat4 boneMatrices array; upload cap is {cap}"))
            continue
        shader_cap = int(declaration.group(1))
        if shader_cap != cap:
            line = gl_state_audit.line_number(text, declaration.start())
            findings.append((
                "ERROR", relative, line, "GFX002",
                f"boneMatrices[{shader_cap}] does not match gX_GFX_SKIN_MAX_BONES={cap} "
                f"(defined at src/renderer/gfx/constants.h:{constants_line})",
            ))


def audit_skinning_attribute_locations(root, findings):
    for relative in SKIN_SHADER_FILES:
        path = root / relative
        if not path.is_file():
            findings.append(("ERROR", relative, 0, "GFX004",
                             "missing vertex shader for fixed skinning attributes"))
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        masked = gl_state_audit.mask_c_source(text)
        for attribute, expected_location in SKIN_ATTRIBUTES:
            declaration = re.compile(
                rf"\b(?:layout\s*\(\s*location\s*=\s*(\d+)\s*\)\s*)?"
                rf"in\s+[A-Za-z_]\w*\s+{re.escape(attribute)}\s*;"
            )
            matches = list(declaration.finditer(masked))
            if not matches:
                findings.append((
                    "ERROR", relative, 0, "GFX004",
                    f"missing input declaration for {attribute} at location {expected_location}",
                ))
                continue
            if len(matches) > 1:
                line = gl_state_audit.line_number(text, matches[1].start())
                findings.append((
                    "ERROR", relative, line, "GFX004",
                    f"duplicate input declaration for {attribute}",
                ))
                continue
            match = matches[0]
            location = match.group(1)
            line = gl_state_audit.line_number(text, match.start())
            if location is None:
                findings.append((
                    "ERROR", relative, line, "GFX004",
                    f"{attribute} must declare explicit location {expected_location}",
                ))
            elif int(location) != expected_location:
                findings.append((
                    "ERROR", relative, line, "GFX004",
                    f"{attribute} uses location {location}; expected {expected_location}",
                ))


def mask_cmake_comments(text):
    def blank_bracket_comment(match):
        return "".join("\n" if char == "\n" else " " for char in match.group(0))

    text = CMAKE_BRACKET_COMMENT.sub(blank_bracket_comment, text)
    masked = list(text)
    quoted = False
    escaped = False
    comment = False
    for position, char in enumerate(text):
        if comment:
            if char == "\n":
                comment = False
            else:
                masked[position] = " "
        elif quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
        elif char == "#":
            masked[position] = " "
            comment = True
    return "".join(masked)


def audit_raylib_skinning_build(root, findings):
    path = root / "CMakeLists.txt"
    relative = str(path.relative_to(root))
    if not path.is_file():
        findings.append(("ERROR", relative, 0, "GFX005", "missing CMakeLists.txt"))
        return
    text = path.read_text(encoding="utf-8", errors="replace")
    masked = mask_cmake_comments(text)
    compile_definitions = re.compile(r"(?m)^[ \t]*target_compile_definitions\s*\(")
    for match in compile_definitions.finditer(masked):
        open_paren = masked.find("(", match.start(), match.end())
        arguments = call_arguments(masked, open_paren)
        if len(arguments) != 1:
            continue
        tokens = re.findall(r'"(?:\\.|[^"])*"|[^\s;]+', arguments[0])
        tokens = [token[1:-1] if token.startswith('"') and token.endswith('"')
                  else token for token in tokens]
        if tokens and tokens[0] == "raylib" and "SUPPORT_GPU_SKINNING=1" in tokens[1:]:
            return
    findings.append((
        "ERROR", relative, 0, "GFX005",
        "raylib target must define SUPPORT_GPU_SKINNING=1 for bone VBO uploads",
    ))


def parse_integer_literal(expression):
    token = expression.strip()
    cast = re.fullmatch(
        r"\(\s*(?:gX_[IU]\d+|unsigned(?:\s+int)?|int)\s*\)\s*(.+)", token
    )
    if cast:
        token = cast.group(1).strip()
    match = INTEGER_LITERAL.fullmatch(token)
    if not match:
        return None
    token = match.group(1)
    signless = token.lstrip("+-")
    if signless.lower().startswith("0x"):
        base = 16
    elif len(signless) > 1 and signless.startswith("0"):
        base = 8
    else:
        base = 10
    try:
        return int(token, base)
    except ValueError:
        return None


def call_arguments(masked, open_paren):
    arguments = []
    argument_start = open_paren + 1
    paren_depth = 0
    bracket_depth = 0
    brace_depth = 0
    for position in range(open_paren + 1, len(masked)):
        char = masked[position]
        if char == "(":
            paren_depth += 1
        elif char == ")":
            if paren_depth == 0 and bracket_depth == 0 and brace_depth == 0:
                arguments.append(masked[argument_start:position].strip())
                return arguments
            paren_depth -= 1
        elif char == "[":
            bracket_depth += 1
        elif char == "]":
            bracket_depth -= 1
        elif char == "{":
            brace_depth += 1
        elif char == "}":
            brace_depth -= 1
        elif char == "," and paren_depth == 0 and bracket_depth == 0 and brace_depth == 0:
            arguments.append(masked[argument_start:position].strip())
            argument_start = position + 1
    return []


def normalize_expression(expression):
    return re.sub(r"\s+", "", expression)


def audit_material_uniform_sources(root, findings):
    source_cache = {}
    region_cache = {}
    for relative, function, setter, location, expected_source, label in MATERIAL_UNIFORM_SOURCES:
        path = root / relative
        if not path.is_file():
            findings.append(("ERROR", relative, 0, "GFX006",
                             f"missing upload source for {label}"))
            continue
        if relative not in source_cache:
            source_cache[relative] = path.read_text(encoding="utf-8", errors="replace")
            masked = gl_state_audit.mask_c_source(source_cache[relative])
            region_cache[relative] = gl_state_audit.function_regions(masked)
        text = source_cache[relative]
        masked = gl_state_audit.mask_c_source(text)
        regions = [region for region in region_cache[relative] if region[0] == function]
        if not regions:
            findings.append(("ERROR", relative, 0, "GFX006",
                             f"missing {function}() upload path for {label}"))
            continue
        expected_location = normalize_expression(location)
        expected_value = normalize_expression(expected_source)
        writes = []
        setter_pattern = re.compile(rf"\b{re.escape(setter)}\s*\(")
        for _, _, brace, end in regions:
            body = masked[brace:end]
            for call in setter_pattern.finditer(body):
                open_paren = body.find("(", call.start(), call.end())
                arguments = call_arguments(body, open_paren)
                if len(arguments) < 3:
                    continue
                if normalize_expression(arguments[1]) == expected_location:
                    writes.append((brace + call.start(), arguments[2]))
        if not writes:
            findings.append(("ERROR", relative, 0, "GFX006",
                             f"missing {label} upload to {location}"))
            continue
        for position, actual_source in writes:
            if normalize_expression(actual_source) != expected_value:
                line = gl_state_audit.line_number(text, position)
                findings.append((
                    "ERROR", relative, line, "GFX006",
                    f"{label} is hardcoded or sourced from `{actual_source.strip()}`; "
                    f"expected `{expected_source}`",
                ))


def audit_mesh_map_presence(root, findings):
    relative = "src/renderer/gfx/model.c"
    path = root / relative
    if not path.is_file():
        findings.append(("ERROR", relative, 0, "GFX007",
                         "missing per-mesh material map binding source"))
        return
    text = path.read_text(encoding="utf-8", errors="replace")
    masked = gl_state_audit.mask_c_source(text)
    regions = [region for region in gl_state_audit.function_regions(masked)
               if region[0] == "gX_RendererGFX_Model_MeshBindMaps"]
    if not regions:
        findings.append(("ERROR", relative, 0, "GFX007",
                         "missing gX_RendererGFX_Model_MeshBindMaps()"))
        return
    _, _, brace, end = regions[0]
    body = masked[brace:end]
    positions = {}
    for name, pattern, description in MESH_MAP_FLAG_PATTERNS:
        match = pattern.search(body)
        if not match:
            line = gl_state_audit.line_number(text, brace)
            findings.append(("ERROR", relative, line, "GFX007", description))
            continue
        positions[name] = match.start()
    ordered = (
        "reset", "texture", "mesh", "injected", "albedo_override",
        "emissive_override", "presence", "fallback", "cache_guard",
        "upload", "cache_update",
    )
    for before, after in zip(ordered, ordered[1:]):
        if before in positions and after in positions and positions[before] >= positions[after]:
            line = gl_state_audit.line_number(text, brace + positions[after])
            findings.append((
                "ERROR", relative, line, "GFX007",
                f"{after} must occur after {before} in per-mesh map selection",
            ))
    draw_regions = [region for region in gl_state_audit.function_regions(masked)
                    if region[0] == "gX_RendererGFX_Model_Draw"]
    if not draw_regions:
        findings.append(("ERROR", relative, 0, "GFX007",
                         "missing model draw path for per-mesh map flag cache"))
        return
    _, _, draw_brace, draw_end = draw_regions[0]
    draw_body = masked[draw_brace:draw_end]
    sentinel = re.search(r"\bgX_I32\s+boundMapFlags\s*=\s*-1\s*;", draw_body)
    mesh_loop = re.search(r"for\s*\(\s*gX_I32\s+mi\s*=", draw_body)
    bind_call = re.search(
        r"gX_RendererGFX_Model_MeshBindMaps\s*\([\s\S]*?&\s*boundMapFlags\s*\)",
        draw_body,
    )
    if not sentinel:
        findings.append(("ERROR", relative, 0, "GFX007",
                         "boundMapFlags must start at -1 to force the first mesh's flag upload"))
    if not mesh_loop or not bind_call:
        findings.append(("ERROR", relative, 0, "GFX007",
                         "mesh draw must pass the per-draw flag cache to each map bind"))
    elif sentinel and not (sentinel.start() < mesh_loop.start() < bind_call.start()):
        line = gl_state_audit.line_number(text, draw_brace + bind_call.start())
        findings.append(("ERROR", relative, line, "GFX007",
                         "initialize boundMapFlags before the mesh loop and map binding"))


#ai: parse only the small C if/else structure needed to pair resource guards with uploads.
def matching_delimiter(text, open_position, opening, closing):
    depth = 0
    for position in range(open_position, len(text)):
        if text[position] == opening:
            depth += 1
        elif text[position] == closing:
            depth -= 1
            if depth == 0:
                return position
    return None


def skip_space(text, position):
    while position < len(text) and text[position].isspace():
        position += 1
    return position


def parse_statement(text, position):
    position = skip_space(text, position)
    if position >= len(text):
        return "", position
    if text[position] == "{":
        close = matching_delimiter(text, position, "{", "}")
        if close is None:
            return text[position:], len(text)
        return text[position + 1:close], close + 1
    semicolon = text.find(";", position)
    if semicolon < 0:
        return text[position:], len(text)
    return text[position:semicolon + 1], semicolon + 1


def parse_if_chain(text, start):
    match = re.match(r"if\s*\(", text[start:])
    if not match:
        return None
    open_paren = start + match.end() - 1
    close_paren = matching_delimiter(text, open_paren, "(", ")")
    if close_paren is None:
        return None
    condition = text[open_paren + 1:close_paren]
    body, after = parse_statement(text, close_paren + 1)
    alternatives = []
    after = skip_space(text, after)
    if re.match(r"else\b", text[after:]):
        else_position = after + len("else")
        else_position = skip_space(text, else_position)
        nested_if = re.match(r"if\s*\(", text[else_position:])
        if nested_if:
            alternative = parse_if_chain(text, else_position)
            if alternative:
                alternatives.append(alternative)
                after = alternative["end"]
        else:
            alternative_body, after = parse_statement(text, else_position)
            alternatives.append({
                "condition": None,
                "body": alternative_body,
                "alternatives": [],
                "end": after,
            })
    return {
        "condition": condition,
        "body": body,
        "alternatives": alternatives,
        "end": after,
    }


def find_if_branch(text, expected_condition):
    expected = normalize_expression(expected_condition)
    for match in re.finditer(r"\bif\s*\(", text):
        branch = parse_if_chain(text, match.start())
        if branch and normalize_expression(branch["condition"]) == expected:
            return branch
    return None


def find_alternative(branch, expected_condition):
    expected = (None if expected_condition is None
                else normalize_expression(expected_condition))
    for alternative in branch["alternatives"]:
        condition = alternative["condition"]
        actual = None if condition is None else normalize_expression(condition)
        if actual == expected:
            return alternative
    return None


def matching_call_positions(masked, name, expected_arguments):
    expected = tuple(normalize_expression(argument) for argument in expected_arguments)
    positions = []
    pattern = re.compile(rf"\b{re.escape(name)}\s*\(")
    for match in pattern.finditer(masked):
        open_paren = masked.find("(", match.start(), match.end())
        arguments = call_arguments(masked, open_paren)
        if tuple(normalize_expression(argument) for argument in arguments) == expected:
            positions.append(match.start())
    return positions


def audit_ordered_calls(body, calls):
    cursor = 0
    for name, arguments in calls:
        positions = matching_call_positions(body, name, arguments)
        position = next((candidate for candidate in positions if candidate >= cursor), None)
        if position is None:
            return False
        cursor = position + 1
    return True


def audit_guarded_call(body, condition, name, arguments):
    guard = find_if_branch(body, condition)
    return guard is not None and bool(matching_call_positions(guard["body"], name, arguments))


def function_source_region(root, relative, function, rule, findings):
    path = root / relative
    if not path.is_file():
        findings.append(("ERROR", relative, 0, rule, f"missing {function}() source"))
        return None
    text = path.read_text(encoding="utf-8", errors="replace")
    masked = gl_state_audit.mask_c_source(text)
    regions = [region for region in gl_state_audit.function_regions(masked)
               if region[0] == function]
    if not regions:
        findings.append(("ERROR", relative, 0, rule, f"missing {function}()"))
        return None
    _, _, brace, end = regions[0]
    return text, masked[brace:end], brace


def audit_effect_resource_flag_paths(root, findings):
    rule = "GFX008"
    for spec in EFFECT_RESOURCE_FLAG_PATHS:
        region = function_source_region(
            root, spec["relative"], spec["function"], rule, findings
        )
        if region is None:
            continue
        text, body, brace = region
        line = gl_state_audit.line_number(text, brace)
        present = find_if_branch(body, spec["present_condition"])
        if present is None:
            findings.append((
                "ERROR", spec["relative"], line, rule,
                f"{spec['label']} is missing its resource-present condition",
            ))
            continue
        if not audit_ordered_calls(present["body"], spec["present_calls"]):
            findings.append((
                "ERROR", spec["relative"], line, rule,
                f"{spec['label']} must bind the available resource and set its flag",
            ))
        for condition, name, arguments in spec["present_guards"]:
            if not audit_guarded_call(present["body"], condition, name, arguments):
                findings.append((
                    "ERROR", spec["relative"], line, rule,
                    f"{spec['label']} must guard `{name}` with `{condition}`",
                ))
        absent = find_alternative(present, spec["absent_condition"])
        if absent is None:
            findings.append((
                "ERROR", spec["relative"], line, rule,
                f"{spec['label']} is missing its resource-absent reset path",
            ))
        elif not audit_ordered_calls(absent["body"], spec["absent_calls"]):
            findings.append((
                "ERROR", spec["relative"], line, rule,
                f"{spec['label']} must set its flag to zero when unavailable",
            ))

    relative = "src/renderer/gfx/draw.c"
    function = "gX_RendererGFX_Draw_SSR"
    region = function_source_region(root, relative, function, rule, findings)
    if region is None:
        return
    text, body, brace = region
    line = gl_state_audit.line_number(text, brace)
    checks = (
        (re.compile(
            r"\bgX_I1\s+ssrHasEnv\s*=\s*\(\s*"
            r"gfx->reflection\.cubeUpdatedThisFrame\s*&&\s*"
            r"gfx->reflection\.dynamicCubemap\.id\s*>\s*0\s*\)\s*;"
        ), "initialize hasEnv from a fresh, valid dynamic cubemap"),
        (re.compile(
            r"\bgX_Texture\s+ssrEnvTex\s*=\s*gfx->reflection\.dynamicCubemap\s*;"
        ), "select the dynamic cubemap as the initial SSR environment"),
    )
    for pattern, description in checks:
        if not pattern.search(body):
            findings.append(("ERROR", relative, line, rule, description))

    probes = find_if_branch(body, "gfx->reflection.probesReady && gfx->reflection.probeCount > 0")
    if probes is None:
        findings.append(("ERROR", relative, line, rule,
                         "hasEnvMap must include valid baked-probe fallback availability"))
    else:
        probe_body = normalize_expression(probes["body"])
        probe_requirements = (
            ("if(!ep->baked||ep->cube.id==0)continue;", "skip unavailable or empty probe cubemaps"),
            ("if(bestD>=0.0f)ssrHasEnv=true;", "mark a selected probe environment available"),
        )
        for required, description in probe_requirements:
            if required not in probe_body:
                findings.append(("ERROR", relative, line, rule, description))
        nearest = find_if_branch(probes["body"], "bestD < 0.0f || dist < bestD")
        if (nearest is None
                or "bestD=dist;" not in normalize_expression(nearest["body"])
                or "ssrEnvTex=ep->cube;" not in normalize_expression(nearest["body"])):
            findings.append(("ERROR", relative, line, rule,
                             "select a valid probe cubemap while updating the nearest distance"))

    env_bind = find_if_branch(body, "gfx->ssr.envMapLoc >= 0 && ssrHasEnv")
    env_call = (
        "gX_Shader_SetValue_CubemapSlot",
        ("sha", "gfx->ssr.envMapLoc", "ssrEnvTex", "gX_GFX_TEXUNIT_SSR_ENV"),
    )
    if env_bind is None or not matching_call_positions(env_bind["body"], *env_call):
        findings.append(("ERROR", relative, line, rule,
                         "bind the SSR environment cubemap only when one is available"))
    env_flag_call = (
        "gX_Shader_SetValue_Int",
        ("sha", "gfx->ssr.hasEnvMapLoc", "ssrHasEnv ? 1 : 0"),
    )
    if not audit_guarded_call(body, "gfx->ssr.hasEnvMapLoc >= 0", *env_flag_call):
        findings.append(("ERROR", relative, line, rule,
                         "upload the hasEnvMap availability value when its uniform exists"))


def audit_texture_unit_bounds(root, findings):
    constants_path = root / "src" / "renderer" / "gfx" / "constants.h"
    if not constants_path.is_file():
        findings.append(("ERROR", str(constants_path.relative_to(root)), 0,
                         "GFX003", "missing GFX constants header"))
        return 0, 0

    constants = constants_path.read_text(encoding="utf-8", errors="replace")
    numeric_constants = 0
    for match in TEXTURE_UNIT_DEFINE.finditer(gl_state_audit.mask_c_source(constants)):
        value = parse_integer_literal(match.group(2))
        if value is None:
            continue
        numeric_constants += 1
        if value < 0 or value > MAX_TEXTURE_UNIT:
            line = gl_state_audit.line_number(constants, match.start())
            findings.append((
                "ERROR", str(constants_path.relative_to(root)), line, "GFX003",
                f"{match.group(1)}={value} is outside texture-unit range [0,{MAX_TEXTURE_UNIT}]",
            ))

    literal_slots = 0
    for path in gl_state_audit.source_files(root):
        text = path.read_text(encoding="utf-8", errors="replace")
        masked = gl_state_audit.mask_c_source(text)
        relative = str(path.relative_to(root))
        for pattern, slot_index, call_kind in TEXTURE_SLOT_CALLS:
            for match in pattern.finditer(masked):
                open_paren = masked.find("(", match.start(), match.end())
                arguments = call_arguments(masked, open_paren)
                if len(arguments) <= slot_index:
                    continue
                value = parse_integer_literal(arguments[slot_index])
                if value is None:
                    continue
                literal_slots += 1
                if value < 0 or value > MAX_TEXTURE_UNIT:
                    line = gl_state_audit.line_number(text, match.start())
                    findings.append((
                        "ERROR", relative, line, "GFX003",
                        f"literal {call_kind} texture unit {value} is outside "
                        f"[0,{MAX_TEXTURE_UNIT}]",
                    ))
    return numeric_constants, literal_slots


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Audit high-confidence GFX integration hazards"
    )
    parser.add_argument(
        "--root", default=None,
        help="project root (default: repository containing this script)",
    )
    parser.add_argument(
        "--strict", action="store_true",
        help="also return failure when advisory GFX001 warnings are found",
    )
    args = parser.parse_args(argv)
    root = Path(args.root).resolve() if args.root else Path(__file__).resolve().parent.parent
    findings = []
    audit_uniform_activation_order(root, findings)
    audit_skinning_capacity(root, findings)
    numeric_constants, literal_slots = audit_texture_unit_bounds(root, findings)
    audit_skinning_attribute_locations(root, findings)
    audit_raylib_skinning_build(root, findings)
    audit_material_uniform_sources(root, findings)
    audit_mesh_map_presence(root, findings)
    audit_effect_resource_flag_paths(root, findings)

    for level, path, line, rule, message in findings:
        print(f"{level}: {path}:{line}: {rule}: {message}")
    errors = sum(level == "ERROR" for level, *_ in findings)
    warnings = sum(level == "WARN" for level, *_ in findings)
    if not any(rule == "GFX001" for _, _, _, rule, _ in findings):
        print("OK: GFX001 no same-function raw uniform uploads precede activation")
    if not any(rule == "GFX002" for _, _, _, rule, _ in findings):
        print("OK: GFX002 skin upload cap matches all three shader arrays")
    if not any(rule == "GFX003" for _, _, _, rule, _ in findings):
        print(
            f"OK: GFX003 {numeric_constants} GFX texture-unit constants and "
            f"{literal_slots} literal binding slots are within [0,{MAX_TEXTURE_UNIT}]"
        )
    if not any(rule == "GFX004" for _, _, _, rule, _ in findings):
        print("OK: GFX004 skinning attributes use locations 7/8 in all three vertex shaders")
    if not any(rule == "GFX005" for _, _, _, rule, _ in findings):
        print("OK: GFX005 raylib enables SUPPORT_GPU_SKINNING=1")
    if not any(rule == "GFX006" for _, _, _, rule, _ in findings):
        map_flags = sum(label.startswith("has") for *_, label in MATERIAL_UNIFORM_SOURCES)
        print(
            f"OK: GFX006 feature/albedo uploads are data-driven; "
            f"{map_flags} material has* uploads remain data-driven"
        )
    if not any(rule == "GFX007" for _, _, _, rule, _ in findings):
        print("OK: GFX007 per-mesh has* flags follow selected IDs, fallback and draw cache")
    if not any(rule == "GFX008" for _, _, _, rule, _ in findings):
        print(
            f"OK: GFX008 {len(EFFECT_RESOURCE_FLAG_PATHS) + 1} effect-resource flags "
            "bind when available and reset when absent"
        )
    print(f"GFX OBVIOUS AUDIT: errors={errors} warnings={warnings}")
    return 1 if errors or (args.strict and warnings) else 0


if __name__ == "__main__":
    sys.exit(main())
