/**********************************************************************************************
*
*   [gX: src/rhi/rhi_caps.c]
*   gX — a game engine
*
*   Copyright (c) 2026 lanttu, opencode
*
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*   gX is free software: you can redistribute it and/or modify it under the
*   terms of the GNU General Public License as published by the Free Software
*   Foundation, either version 3 of the License, or (at your option) any later
*   version.
*   gX is distributed in the hope that it will be useful, but WITHOUT ANY
*   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
*   FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
*   details.
*   You should have received a copy of the GNU General Public License along
*   with gX. If not, see <https://www.gnu.org/licenses/>.
*
*
**********************************************************************************************/


//opencode: RHI capabilities — GL capability detection moved into the RHI
//   layer. Engine code queries gX_RHI_GetCaps() with GL-independent names
//   instead of reading GL version/extensions directly (see gfx/gl_level.h,
//   which now delegates its hardware detection here).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rhi_caps.h"
#include "rhi_gl.h"
#include "rhi.h"
#include "rhi_state.h"
#include "../common/log.h"

//opencode: extension check via glGetStringi — glGetString(GL_EXTENSIONS) is
//   GL_INVALID_ENUM in core profiles (8 GL errors per startup, fixed in the
//   earlier GetStringi wrapper).
static gX_I1 hasExt(const gX_I8 *name)
{
    gX_I32 numExt = 0;
    gX_RHI_GetIntegerv(GL_NUM_EXTENSIONS, &numExt);
    for (gX_I32 i = 0; i < numExt; i++) {
        const gX_I8 *ext = gX_RHI_GetStringi((gX_U32)i);
        if (ext && strcmp(ext, name) == 0) return 1;
    }
    return 0;
}

//opencode: parse "major.minor" from a version string that may have a textual
//   prefix — desktop GL_VERSION is "4.6.0 NVIDIA...", GLES is "OpenGL ES 3.2
//   Mesa...". sscanf("%d.%d") fails on the ES prefix because %d skips only
//   whitespace, not letters. Scan to the first digit first.
static gX_I0 parseVersion(const gX_I8 *str, gX_I32 *major, gX_I32 *minor)
{
    *major = 0; *minor = 0;
    if (!str) return;
    while (*str && (*str < '0' || *str > '9')) str++;
    if (*str == '\0') return;
    *major = (gX_I32)(*str - '0');
    str++;
    while (*str >= '0' && *str <= '9') {
        *major = *major * 10 + (gX_I32)(*str - '0');
        str++;
    }
    if (*str != '.') return;
    str++;
    *minor = 0;
    while (*str >= '0' && *str <= '9') {
        *minor = *minor * 10 + (gX_I32)(*str - '0');
        str++;
    }
}

static gX_RHI_Caps gX_Caps;
static gX_I1 gX_CapsDetected = 0;

const gX_RHI_Caps *gX_RHI_GetCaps(gX_I0)
{
    if (gX_CapsDetected) return &gX_Caps;
    gX_CapsDetected = 1;

    // --- GL version strings → major/minor + shaderModel ---
    const gX_I8 *verStr = gX_RHI_GetString(GL_VERSION);
    const gX_I8 *glslStr = gX_RHI_GetString(GL_SHADING_LANGUAGE_VERSION);

    //opencode: detect OpenGL ES — GL_VERSION on an ES context reads like
    //   "OpenGL ES 3.2 Mesa 24.x" (or "OpenGL ES-CM" on very old ES2), and
    //   the GLSL string "OpenGL ES GLSL ES 3.20". Desktop reads "4.6" /
    //   "4.60". The ES flag drives the entire feature-level mapping below.
    gX_I1 isGLES = 0;
    if (verStr && strstr(verStr, "OpenGL ES") != NULL) isGLES = 1;
    gX_Caps.isGLES = isGLES;

    gX_I32 major = 0, minor = 0;
    parseVersion(verStr, &major, &minor);
    gX_Caps.glMajor = major;
    gX_Caps.glMinor = minor;

    // GLSL "4.60" → 460, "3.30" → 330, "3.00 ES" → 300, "3.20 ES" → 320
    gX_I32 sMajor = 0, sMinor = 0;
    parseVersion(glslStr, &sMajor, &sMinor);
    gX_Caps.shaderModel = (sMajor > 0) ? (sMajor * 100 + sMinor) : 0;

    // --- numeric limits ---
    gX_RHI_GetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &gX_Caps.maxTextureUnits);
    gX_RHI_GetIntegerv(GL_MAX_TEXTURE_SIZE, &gX_Caps.maxTextureSize);
    gX_RHI_GetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &gX_Caps.maxUniformComponents);
    gX_RHI_GetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &gX_Caps.maxColorAttachments);

    // --- feature flags (hardware support, ignores user level gating) ---
    // Base FBO/transform-feedback features (GL 3.0+ / ES 3.0+):
    gX_Caps.hasFBO = 1;
    gX_Caps.hasTransformFeedback = 1;
    //opencode: program binary is core in ES 3.0+ (glGetProgramBinary); desktop
    //   needs GL 4.1+ or the ARB extension.
    gX_Caps.hasProgramBinary = isGLES || hasExt("GL_ARB_get_program_binary");
    //opencode: multisample textures — desktop GL 3.2+; ES 3.1+ only (ES 3.0
    //   has renderbuffer MSAA but no MS textures). glTexImage2DMultisample is
    //   desktop-only; ES uses glTexStorage2DMultisample.
    gX_Caps.hasMSTexture = isGLES ? (sMajor >= 3 && sMinor >= 1) : (major > 3 || (major == 3 && minor >= 2));
    //opencode: geometry shaders — desktop GL 3.2+; ES 3.2+ only (never in
    //   ES 3.0/3.1). The renderer gates .geom loads behind this.
    gX_Caps.hasGeometryShaders = isGLES ? (sMajor >= 3 && sMinor >= 2)
                                         : (major > 3 || (major == 3 && minor >= 2) || hasExt("GL_ARB_geometry_shader4"));
    //opencode: draw instancing is desktop GL 3.1+ (or ARB_draw_instanced)
    //   and GLES 3.0+. The renderer's selected-level policy gates this raw
    //   hardware capability separately in gfx/gl_level.c.
    gX_Caps.hasInstancing = isGLES ? (major >= 3)
                                    : (major > 3 || (major == 3 && minor >= 1)
                                       || hasExt("GL_ARB_draw_instanced"));
    //opencode: immutable texture storage (glTexStorage2D/3D) is desktop
    //   GL 4.2+ or GL_ARB_texture_storage; ES 3.0+ has it in core. The RHI
    //   texture layer falls back to mutable glTexImage allocation without it
    //   (native GL 3.0 has neither the core entry nor, on most Mesas, the
    //   extension — calling the NULL glad pointer would crash).
    gX_Caps.hasImmutableStorage = isGLES ? (major >= 3)
        : (major > 4 || (major == 4 && minor >= 2) || hasExt("GL_ARB_texture_storage"));

    if (isGLES) {
        //opencode: ES level ladder — ES 3.0 → LEVEL_300, ES 3.1 → compute+SSBO,
        //   ES 3.2 → geometry. Desktop-only features (tessellation, bindless,
        //   cube-map arrays) are never available on any ES version.
        gX_I32 lvl = GX_RHI_LEVEL_300;
        if (major > 3 || (major == 3 && minor >= 1)) lvl = GX_RHI_LEVEL_330;
        if (major > 3 || (major == 3 && minor >= 2)) lvl = GX_RHI_LEVEL_400;
        gX_Caps.hasTessellation      = 0;
        gX_Caps.hasShaderSubroutine  = 0;
        gX_Caps.hasBindless          = 0;
        gX_Caps.hasCubeMapArray      = 0;
        gX_Caps.hasMultiDrawIndirect = (lvl >= GX_RHI_LEVEL_330);   // ES 3.1+
        gX_Caps.hasCompute           = (lvl >= GX_RHI_LEVEL_330);   // ES 3.1+
        gX_Caps.hasSSBO              = (lvl >= GX_RHI_LEVEL_330);   // ES 3.1+
    } else {
        gX_I32 lvl = GX_RHI_LEVEL_330;
        if (major > 4 || (major == 4 && minor >= 3)) lvl = GX_RHI_LEVEL_430;
        else if (major > 4 || (major == 4 && minor >= 2)) lvl = GX_RHI_LEVEL_420;
        else if (major > 4 || (major == 4 && minor >= 1)) lvl = GX_RHI_LEVEL_410;
        else if (major >= 4) lvl = GX_RHI_LEVEL_400;

        gX_Caps.hasTessellation      = (lvl >= GX_RHI_LEVEL_400) || hasExt("GL_ARB_tessellation_shader");
        gX_Caps.hasShaderSubroutine  = (lvl >= GX_RHI_LEVEL_400) || hasExt("GL_ARB_shader_subroutine");
        gX_Caps.hasBindless          = (lvl >= GX_RHI_LEVEL_420) || hasExt("GL_ARB_bindless_texture");
        gX_Caps.hasMultiDrawIndirect = (lvl >= GX_RHI_LEVEL_420) || hasExt("GL_ARB_multi_draw_indirect");
        gX_Caps.hasCompute           = (lvl >= GX_RHI_LEVEL_430) || hasExt("GL_ARB_compute_shader");
        gX_Caps.hasSSBO              = (lvl >= GX_RHI_LEVEL_430) || hasExt("GL_ARB_shader_storage_buffer_object");
        gX_Caps.hasCubeMapArray      = (lvl >= GX_RHI_LEVEL_400) || hasExt("GL_ARB_texture_cube_map_array");
    }

    gX_TraceLog(gX_LOG_INFO, "RHI caps: %s %d.%d (GLSL %d) units=%d tex=%d uniforms=%d attachments=%d compute=%d ssbo=%d bindless=%d geom=%d ms=%d instancing=%d immStorage=%d",
        isGLES ? "GLES" : "GL", gX_Caps.glMajor, gX_Caps.glMinor, gX_Caps.shaderModel,
        gX_Caps.maxTextureUnits, gX_Caps.maxTextureSize, gX_Caps.maxUniformComponents,
        gX_Caps.maxColorAttachments, gX_Caps.hasCompute, gX_Caps.hasSSBO, gX_Caps.hasBindless,
        gX_Caps.hasGeometryShaders, gX_Caps.hasMSTexture, gX_Caps.hasInstancing,
        gX_Caps.hasImmutableStorage);

    return &gX_Caps;
}

gX_I0 gX_RHI_Caps_Invalidate(gX_I0)
{
    gX_CapsDetected = 0;
    gX_Caps = (gX_RHI_Caps){0};
}

//opencode: desktop legacy-GLSL selector — native GL 3.x reports its real
// GLSL (130/140/150) and the preprocessor matches that dialect; anything
// 330+ (or ES) stays on the production path. Returns 0 for modern contexts.
// The GX_GLSL130 hook deliberately stays opt-in: it lets the GL3.3
// development host compile the 130 dialect while native-context work proceeds.
gX_I32 gX_RHI_Shader_LegacyGLSL(gX_I0)
{
    const gX_RHI_Caps *caps = gX_RHI_GetCaps();
    const gX_I8 *force = getenv("GX_GLSL130");
    if (!caps || caps->isGLES || caps->shaderModel < 130) return 0;
    if (force && force[0] && force[0] != '0') return 130;
    if (caps->shaderModel < 140) return 130;
    if (caps->shaderModel < 150) return 140;
    if (caps->shaderModel < 330) return 150;
    return 0;
}

//opencode: GLSL #version header for internal hardcoded shaders (ES-aware).
const gX_I8 *gX_RHI_Shader_VersionHeader(gX_I1 isFragment)
{
    if (gX_Caps.isGLES) {
        return isFragment ? "#version 300 es\nprecision highp float;\n" : "#version 300 es\n";
    }
    switch (gX_RHI_Shader_LegacyGLSL()) {
        case 130: return "#version 130\n";
        case 140: return "#version 140\n";
        case 150: return "#version 150\n";
        default: break;
    }
    return "#version 330\n";
}
