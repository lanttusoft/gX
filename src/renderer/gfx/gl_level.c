/**********************************************************************************************
*
*   [gX: src/renderer/gfx/gl_level.c]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu
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


//opencode: GL version detection + feature flags — runtime GL capability checks
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gl_level.h"
#include "../../settings.h"
#include "../../common/log.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_state.h"
#include "../../rhi/rhi_caps.h"

//opencode: hardware detection now lives in the RHI layer (gX_RHI_GetCaps()).
//   This module keeps only the user-selectable educational level ladder:
//   level/useLevel, the has* flags GATED by useLevel, and the shader
//   #version/#define injection strings.

gX_GL_Features gX_GL = {0};

//opencode: desktop 3.x needs its own rungs: 3.1 brings draw instancing and
//   3.2 brings geometry/MSAA textures. GLES keeps its existing capability
//   mapping so ES 3.1 compute and ES 3.2 geometry retain their policy levels.
static gX_I32 gX_GL_Level_FromCaps(const gX_RHI_Caps *caps)
{
    if (caps->isGLES)
    {
        if (caps->glMajor > 3 || (caps->glMajor == 3 && caps->glMinor >= 2)) return GX_GL_LEVEL_400;
        if (caps->glMajor == 3 && caps->glMinor >= 1) return GX_GL_LEVEL_330;
        if (caps->glMajor == 3) return GX_GL_LEVEL_300;
        return GX_GL_LEVEL_200;
    }
    if (caps->glMajor > 4 || (caps->glMajor == 4 && caps->glMinor >= 3)) return GX_GL_LEVEL_430;
    if (caps->glMajor == 4 && caps->glMinor >= 2) return GX_GL_LEVEL_420;
    if (caps->glMajor == 4 && caps->glMinor >= 1) return GX_GL_LEVEL_410;
    if (caps->glMajor >= 4) return GX_GL_LEVEL_400;
    if (caps->glMajor == 3 && caps->glMinor >= 3) return GX_GL_LEVEL_330;
    if (caps->glMajor == 3 && caps->glMinor >= 2) return GX_GL_LEVEL_320;
    if (caps->glMajor == 3 && caps->glMinor >= 1) return GX_GL_LEVEL_310;
    if (caps->glMajor == 3) return GX_GL_LEVEL_300;
    return GX_GL_LEVEL_200;
}

//opencode: GX_GL_LEVEL=N is an opt-in startup policy override used by the
//   GL-level matrix and compatibility investigations. It runs before shader
//   loading, unlike a later interactive level switch that must reload shaders.
gX_I0 gX_GL_Detect(gX_I0)
{
    const gX_RHI_Caps *caps = gX_RHI_GetCaps();
    const gX_I8 *glslStr = gX_RHI_GetString(gX_RHI_SHADING_LANGUAGE_VERSION);
    //opencode: the request comes from the env (gates) or the persisted
    //   editor setting; 0 means auto (use the detected level).
    const gX_I8 *requestedLevel = getenv("GX_GL_LEVEL");
    gX_I32 requestedSetting = settings.toolGlLevel;

    gX_I32 glLevel = gX_GL_Level_FromCaps(caps);

    gX_GL.level = glLevel;
    gX_GL.useLevel = glLevel; // default to max

    // Set feature flags from detected hardware level
    gX_I32 levelRequest = 0;
    if (requestedLevel && requestedLevel[0]) levelRequest = (gX_I32)atoi(requestedLevel);
    else if (requestedSetting > 0) levelRequest = requestedSetting;
    gX_GL_SetLevel(levelRequest ? levelRequest : glLevel);

    gX_TraceLog(gX_LOG_INFO, "GL: detected version %d.%d (GLSL %s) — level %d, user=%d, tess=%d compute=%d ssbo=%d bindless=%d geom=%d msaaTex=%d instancing=%d",
        caps->glMajor, caps->glMinor, glslStr ? glslStr : "?", glLevel, gX_GL.useLevel,
        gX_GL.hasTessellation, gX_GL.hasCompute, gX_GL.hasSSBO, gX_GL.hasBindless,
        gX_GL.hasGeometryShaders, gX_GL.hasMSTexture, gX_GL.hasInstancing);
}

//opencode: compute feature flags from a given GL level. A feature requires
//   both hardware support and a selected profile that exposes its contract;
//   lowering useLevel must disable extension-backed features too.
static gX_I0 gX_GL_UpdateFlags(gX_I32 lvl)
{
    const gX_RHI_Caps *caps = gX_RHI_GetCaps();
    const gX_I32 computeLevel = caps->isGLES ? GX_GL_LEVEL_330 : GX_GL_LEVEL_430;
    const gX_I32 multiDrawLevel = caps->isGLES ? GX_GL_LEVEL_330 : GX_GL_LEVEL_420;
    const gX_I32 programBinaryLevel = caps->isGLES ? GX_GL_LEVEL_300 : GX_GL_LEVEL_410;
    const gX_I32 geometryLevel = caps->isGLES ? GX_GL_LEVEL_400 : GX_GL_LEVEL_320;
    const gX_I32 msaaTextureLevel = caps->isGLES ? GX_GL_LEVEL_330 : GX_GL_LEVEL_320;
    const gX_I32 instancingLevel = caps->isGLES ? GX_GL_LEVEL_300 : GX_GL_LEVEL_310;
    gX_GL.hasTessellation      = (lvl >= GX_GL_LEVEL_400) && caps->hasTessellation;
    gX_GL.hasCompute           = (lvl >= computeLevel) && caps->hasCompute;
    gX_GL.hasBindless          = (lvl >= GX_GL_LEVEL_420) && caps->hasBindless;
    gX_GL.hasShaderSubroutine  = (lvl >= GX_GL_LEVEL_400) && caps->hasShaderSubroutine;
    gX_GL.hasSSBO              = (lvl >= computeLevel) && caps->hasSSBO;
    gX_GL.hasMultiDrawIndirect = (lvl >= multiDrawLevel) && caps->hasMultiDrawIndirect;
    gX_GL.hasProgramBinary     = (lvl >= programBinaryLevel) && caps->hasProgramBinary;
    gX_GL.hasCubeMapArray      = (lvl >= GX_GL_LEVEL_400) && caps->hasCubeMapArray;
    gX_GL.hasGeometryShaders   = (lvl >= geometryLevel) && caps->hasGeometryShaders;
    gX_GL.hasMSTexture         = (lvl >= msaaTextureLevel) && caps->hasMSTexture;
    gX_GL.hasInstancing        = (lvl >= instancingLevel) && caps->hasInstancing;
}

gX_I32 gX_GL_SetLevel(gX_I32 newLevel)
{
    // Clamp to valid range
    if (newLevel < GX_GL_LEVEL_200) newLevel = GX_GL_LEVEL_200;
    if (newLevel > gX_GL.level)     newLevel = gX_GL.level;
    gX_GL.useLevel = newLevel;
    gX_GL_UpdateFlags(newLevel);
    gX_TraceLog(gX_LOG_INFO, "GL: user level set to %d — tess=%d compute=%d ssbo=%d bindless=%d cubemapArray=%d geom=%d msaaTex=%d instancing=%d",
        newLevel, gX_GL.hasTessellation, gX_GL.hasCompute, gX_GL.hasSSBO, gX_GL.hasBindless, gX_GL.hasCubeMapArray,
        gX_GL.hasGeometryShaders, gX_GL.hasMSTexture, gX_GL.hasInstancing);
    return newLevel;
}

char *gX_GL_VersionString(gX_I0)
{
    switch (gX_GL.useLevel) {
        case GX_GL_LEVEL_200: return (char*)"120";
        case GX_GL_LEVEL_300: return (char*)"130";
        case GX_GL_LEVEL_310: return (char*)"140";
        case GX_GL_LEVEL_320: return (char*)"150";
        case GX_GL_LEVEL_400: return (char*)"400";
        case GX_GL_LEVEL_410: return (char*)"410";
        case GX_GL_LEVEL_420: return (char*)"420";
        case GX_GL_LEVEL_430: return (char*)"430";
        case GX_GL_LEVEL_500: return (char*)"500";
        default:              return (char*)"330";
    }
}

char *gX_GL_ShaderDefines(gX_I0)
{
    static gX_I8 buf[512];
    gX_I32 n = 0;
    n += sprintf(buf + n, "//gX: GL level defines\n");
    n += sprintf(buf + n, "#define GX_GL_VERSION %d\n", gX_GL.useLevel);
    n += sprintf(buf + n, "#define GX_GL_HAS_TESSELLATION %d\n", gX_GL.hasTessellation);
    n += sprintf(buf + n, "#define GX_GL_HAS_COMPUTE      %d\n", gX_GL.hasCompute);
    n += sprintf(buf + n, "#define GX_GL_HAS_BINDLESS     %d\n", gX_GL.hasBindless);
    n += sprintf(buf + n, "#define GX_GL_HAS_SSBO         %d\n", gX_GL.hasSSBO);
    n += sprintf(buf + n, "#define GX_GL_HAS_PROGRAM_BINARY %d\n", gX_GL.hasProgramBinary);
    n += sprintf(buf + n, "#define GX_GL_HAS_CUBEMAP_ARRAY %d\n", gX_GL.hasCubeMapArray);
    n += sprintf(buf + n, "#define GX_GL_HAS_GEOMETRY     %d\n", gX_GL.hasGeometryShaders);
    n += sprintf(buf + n, "#define GX_GL_HAS_MSAA_TEXTURE %d\n", gX_GL.hasMSTexture);
    n += sprintf(buf + n, "#define GX_GL_HAS_INSTANCING  %d\n", gX_GL.hasInstancing);
    buf[n] = '\0';
    return buf;
}
