/**********************************************************************************************
*
*   [gX: src/common/utils/shader.c]
*
*   Copyright (c) 2025 lanttu
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

#include "shader.h"

#include "../enum.h"
#include "../log.h"
#include "../shader_compiler.h"
#include "../../rhi/rhi_caps.h"

#include <stdlib.h>

#include "../../main.h"

#include "../../wrappers/wrappers.h"

//opencode: allocate locs array if NULL (binary cache path leaves it uninitialized)
#ifndef RL_MAX_SHADER_LOCATIONS
#define RL_MAX_SHADER_LOCATIONS 32
#endif

gX_I0 gX_Shader_GetDefaultLocations(gX_Shader *shader) {
    if (!shader->locs) {
        shader->locs = (int*)calloc((size_t)RL_MAX_SHADER_LOCATIONS, sizeof(int));
    }
    //opencode: use GetLocationAttrib for vertex attributes (GetShaderLocation only finds uniforms)
    shader->locs[gX_SHADER_LOC_VERTEX_POSITION] =   gX_Shader_GetLocationAttrib(*shader,"vertexPosition");
    shader->locs[gX_SHADER_LOC_VERTEX_TEXCOORD01] = gX_Shader_GetLocationAttrib(*shader,"vertexTexCoord");
    shader->locs[gX_SHADER_LOC_VERTEX_TEXCOORD02] = gX_Shader_GetLocationAttrib(*shader,"vertexTexCoord2");
    shader->locs[gX_SHADER_LOC_VERTEX_NORMAL] =     gX_Shader_GetLocationAttrib(*shader,"vertexNormal");
    shader->locs[gX_SHADER_LOC_VERTEX_TANGENT] =    gX_Shader_GetLocationAttrib(*shader,"vertexTangent");
    shader->locs[gX_SHADER_LOC_VERTEX_COLOR] =      gX_Shader_GetLocationAttrib(*shader,"vertexColor");
    
    shader->locs[gX_SHADER_LOC_MATRIX_MVP] =        gX_Shader_GetLocation(*shader,"mvp");
    shader->locs[gX_SHADER_LOC_MATRIX_VIEW] =       gX_Shader_GetLocation(*shader,"matView");
    shader->locs[gX_SHADER_LOC_MATRIX_PROJECTION] = gX_Shader_GetLocation(*shader,"matProjection");
    shader->locs[gX_SHADER_LOC_MATRIX_MODEL] =      gX_Shader_GetLocation(*shader,"matModel");
    shader->locs[gX_SHADER_LOC_MATRIX_NORMAL] =     gX_Shader_GetLocation(*shader,"matNormal");
    
    shader->locs[gX_SHADER_LOC_VECTOR_VIEW] =       gX_Shader_GetLocation(*shader,"viewPosition");
    
    shader->locs[gX_SHADER_LOC_COLOR_DIFFUSE] =     gX_Shader_GetLocation(*shader,"albedoColor");
    shader->locs[gX_SHADER_LOC_COLOR_SPECULAR] =    gX_Shader_GetLocation(*shader,"metalnessColor");
    shader->locs[gX_SHADER_LOC_COLOR_AMBIENT] =     gX_Shader_GetLocation(*shader,"ambientColor");
    
    shader->locs[gX_SHADER_LOC_MAP_ALBEDO] =        gX_Shader_GetLocation(*shader,"albedoMap");
    shader->locs[gX_SHADER_LOC_MAP_METALNESS] =     gX_Shader_GetLocation(*shader,"metalnessMap");
    shader->locs[gX_SHADER_LOC_MAP_NORMAL] =        gX_Shader_GetLocation(*shader,"normalMap");
    shader->locs[gX_SHADER_LOC_MAP_HEIGHT] =        gX_Shader_GetLocation(*shader,"heightMap");
    shader->locs[gX_SHADER_LOC_MAP_OCCLUSION] =     gX_Shader_GetLocation(*shader,"occlusionMap");
    shader->locs[gX_SHADER_LOC_MAP_EMISSION] =      gX_Shader_GetLocation(*shader,"emissionMap");
    shader->locs[gX_SHADER_LOC_MAP_HEIGHT] =        gX_Shader_GetLocation(*shader,"heightMap");
    shader->locs[gX_SHADER_LOC_MAP_CUBEMAP] =       gX_Shader_GetLocation(*shader,"cubeMap");
    shader->locs[gX_SHADER_LOC_MAP_IRRADIANCE] =    gX_Shader_GetLocation(*shader,"irradianceMap");
    shader->locs[gX_SHADER_LOC_MAP_PREFILTER] =     gX_Shader_GetLocation(*shader,"prefilterMap");
    shader->locs[gX_SHADER_LOC_MAP_BRDF] =          gX_Shader_GetLocation(*shader,"bdrfMap");
}

gX_I0 gX_Shader_SetValue_Int(gX_Shader shader, gX_I32 loc, gX_I32 i) {
    gX_Shader_SetValue(shader,loc,&i,gX_SHADER_UNIFORM_INT);
}

gX_I0 gX_Shader_SetValue_Float(gX_Shader shader, gX_I32 loc, gX_F32 f) {
    gX_Shader_SetValue(shader,loc,&f,gX_SHADER_UNIFORM_FLOAT);
}

gX_I0 gX_Shader_SetValue_V2(gX_Shader shader, gX_I32 loc, gX_F32v2 v2) {
    gX_F32 v[2] = {v2.x,v2.y};
    gX_Shader_SetValue(shader,loc,v,gX_SHADER_UNIFORM_VEC2);
}

gX_I0 gX_Shader_SetValue_V3(gX_Shader shader, gX_I32 loc, gX_F32v3 v3) {
    gX_F32 v[3] = {v3.x,v3.y,v3.z};
    gX_Shader_SetValue(shader,loc,v,gX_SHADER_UNIFORM_VEC3);
}

gX_I0 gX_Shader_SetValue_V4(gX_Shader shader, gX_I32 loc, gX_F32v4 v4) {
    gX_F32 v[4] = {v4.x,v4.y,v4.z,v4.w};
    gX_Shader_SetValue(shader,loc,v,gX_SHADER_UNIFORM_VEC4);
}

gX_I0 gX_Shader_SetValue_Rectangle(gX_Shader shader, gX_I32 loc, gX_Rectangle rect) {
    gX_F32 r[4] = {rect.x,rect.y,rect.width,rect.height};
    gX_Shader_SetValue(shader,loc,r,gX_SHADER_UNIFORM_VEC4);
}

gX_I0 gX_Shader_SetValue_Color(gX_Shader shader, gX_I32 loc, gX_Color color) {
    gX_F32 col[4] = {0};
    col[0] = (gX_F32)(color.r)/255.0f;
    col[1] = (gX_F32)(color.g)/255.0f;
    col[2] = (gX_F32)(color.b)/255.0f;
    col[3] = (gX_F32)(color.a)/255.0f;
    gX_Shader_SetValue(shader,loc,col,gX_SHADER_UNIFORM_VEC4);
}

gX_I0 gX_Shader_SetValue_ColorRGB(gX_Shader shader, gX_I32 loc, gX_Color color) {
    gX_F32 col[3] = {0};
    col[0] = (gX_F32)(color.r)/255.0f;
    col[1] = (gX_F32)(color.g)/255.0f;
    col[2] = (gX_F32)(color.b)/255.0f;
    gX_Shader_SetValue(shader,loc,col,gX_SHADER_UNIFORM_VEC3);
}

gX_I0 gX_Shader_SetValue_IntArray(gX_Shader shader, gX_I32 loc, gX_I32 *i, gX_U32 amount) {
    gX_Shader_SetValue_Array(shader,loc,i,gX_SHADER_UNIFORM_INT,amount);
}

gX_I0 gX_Shader_SetValue_FloatArray(gX_Shader shader, gX_I32 loc, gX_F32 *f, gX_U32 amount) {
    gX_Shader_SetValue_Array(shader,loc,f,gX_SHADER_UNIFORM_FLOAT,amount);
}

gX_I0 gX_Shader_SetValue_V2Array(gX_Shader shader, gX_I32 loc, gX_F32v2 *v2, gX_U32 amount) {
    gX_Shader_SetValue_Array(shader,loc,v2,gX_SHADER_UNIFORM_VEC2,amount);
}

gX_I0 gX_Shader_SetValue_V3Array(gX_Shader shader, gX_I32 loc, gX_F32v3 *v3, gX_U32 amount) {
    gX_Shader_SetValue_Array(shader,loc,v3,gX_SHADER_UNIFORM_VEC3,amount);
}

gX_I0 gX_Shader_SetValue_V4Array(gX_Shader shader, gX_I32 loc, gX_F32v4 *v4, gX_U32 amount) {
    gX_Shader_SetValue_Array(shader,loc,v4,gX_SHADER_UNIFORM_VEC4,amount);
}

gX_I32 gX_Shader_GetLocation_Multi(gX_Shader shader, gX_U32 count, ...) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE3,"Shader_GetLocation_Multi","UTILS: Getting multiple shader locs")
    gX_I32 locFound = -1;
    va_list args;
    va_start(args, count);
    
    for (gX_U32 i = 0; i < count; i++) {
        gX_TraceLog(gX_LOG_TRACE3,"UTILS: i: %i",i);
        gX_I8 *locName = va_arg(args, gX_I8* );
        gX_TraceLog(gX_LOG_TRACE,"UTILS: locName: %s",locName);
        if (locName) locFound = gX_Shader_GetLocation(shader, locName);
        gX_TraceLog(gX_LOG_TRACE3,"UTILS: locFound: %i",locFound);
        if (locFound != -1) break;
    }
    
    if (locFound == -1) {
        gX_TraceLog(gX_LOG_TRACE,"UTILS: shader location not found");
    }
    
    va_end(args);
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE3,"UTILS: Getting done")
    return locFound;
}

gX_I0 gX_Shader_Locations_Get(gX_Shader_Locations *locs, gX_Shader *shader) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE3,"Shader_Locations_Get","UTILS: Getting locations for shader")
    
    locs->locTime       = gX_Shader_GetLocation(*shader,"time");
    locs->locRes        = gX_Shader_GetLocation(*shader,"res");
    
    locs->locUniform    = gX_Shader_GetLocation(*shader,"uniforms");
    
    locs->locTexInput   = gX_Shader_GetLocation_Multi(*shader, 2, "tex", "texInput");
    locs->locTex0       = gX_Shader_GetLocation_Multi(*shader, 2, "tex0", "texture0");
    locs->locTex1       = gX_Shader_GetLocation_Multi(*shader, 2, "tex1", "texture1");
    locs->locTex2       = gX_Shader_GetLocation_Multi(*shader, 2, "tex2", "texture2");
    locs->locTex3       = gX_Shader_GetLocation_Multi(*shader, 2, "tex3", "texture3");
    locs->locTex4       = gX_Shader_GetLocation_Multi(*shader, 2, "tex4", "texture4");
    locs->locTex5       = gX_Shader_GetLocation_Multi(*shader, 2, "tex5", "texture5");
    locs->locTex6       = gX_Shader_GetLocation_Multi(*shader, 2, "tex6", "texture6");
    locs->locTex7       = gX_Shader_GetLocation_Multi(*shader, 2, "tex7", "texture7");
    
    locs->locCamPos     = gX_Shader_GetLocation_Multi(*shader, 2, "cpos", "camPos");
    locs->locCamTarget  = gX_Shader_GetLocation_Multi(*shader, 2, "ctarget", "camTarget");
    locs->locCamUp      = gX_Shader_GetLocation_Multi(*shader, 2, "cup", "camUp");
    locs->locCamFov     = gX_Shader_GetLocation_Multi(*shader, 2, "cfov", "camFov");
    
    locs->locLightDir   = gX_Shader_GetLocation_Multi(*shader, 1, "lightDir");
    
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE3,"UTILS: Getting done")
}

//opencode: standard shader UBO helpers
gX_I0 gX_Shader_Standard_GetLocations(gX_Shader_StandardUBO *ubo, gX_Shader shader) {
    ubo->locMVP       = gX_Shader_GetLocation(shader, "mvp");
    ubo->locView      = gX_Shader_GetLocation(shader, "matView");
    ubo->locModel     = gX_Shader_GetLocation(shader, "matModel");
    ubo->locProjection = gX_Shader_GetLocation(shader, "matProjection");
    ubo->locNormal    = gX_Shader_GetLocation(shader, "matNormal");
    ubo->locLightSpace = gX_Shader_GetLocation(shader, "matLightSpace");
    ubo->locLight     = gX_Shader_GetLocation(shader, "lightPosition");
    ubo->locViewPos   = gX_Shader_GetLocation(shader, "viewPosition");
    ubo->locFlags[0]  = gX_Shader_GetLocation(shader, "enableLightingAmbient");
    ubo->locFlags[1]  = gX_Shader_GetLocation(shader, "enableLightingDiffuse");
    ubo->locFlags[2]  = gX_Shader_GetLocation(shader, "enableLightingSpecular");
}

gX_I0 gX_Shader_Standard_UploadMatrices(gX_Shader_StandardUBO *ubo, gX_Shader shader,
    gX_Matrix mvp, gX_Matrix view, gX_Matrix model, gX_Matrix projection,
    gX_Matrix normal, gX_Matrix lightSpace) {
    if (ubo->locMVP >= 0)       gX_Shader_SetValue_Matrix(shader, ubo->locMVP, mvp);
    if (ubo->locView >= 0)      gX_Shader_SetValue_Matrix(shader, ubo->locView, view);
    if (ubo->locModel >= 0)     gX_Shader_SetValue_Matrix(shader, ubo->locModel, model);
    if (ubo->locProjection >= 0) gX_Shader_SetValue_Matrix(shader, ubo->locProjection, projection);
    if (ubo->locNormal >= 0)    gX_Shader_SetValue_Matrix(shader, ubo->locNormal, normal);
    if (ubo->locLightSpace >= 0) gX_Shader_SetValue_Matrix(shader, ubo->locLightSpace, lightSpace);
}

gX_I0 gX_Shader_Standard_UploadVectors(gX_Shader_StandardUBO *ubo, gX_Shader shader,
    gX_F32v3 lightPos, gX_F32v3 viewPos) {
    if (ubo->locLight >= 0)  gX_Shader_SetValue_V3(shader, ubo->locLight, lightPos);
    if (ubo->locViewPos >= 0) gX_Shader_SetValue_V3(shader, ubo->locViewPos, viewPos);
}

gX_I0 gX_Shader_Standard_UploadFlags(gX_Shader_StandardUBO *ubo, gX_Shader shader,
    gX_I32 ambient, gX_I32 diffuse, gX_I32 specular) {
    if (ubo->locFlags[0] >= 0) gX_Shader_SetValue_Int(shader, ubo->locFlags[0], ambient);
    if (ubo->locFlags[1] >= 0) gX_Shader_SetValue_Int(shader, ubo->locFlags[1], diffuse);
    if (ubo->locFlags[2] >= 0) gX_Shader_SetValue_Int(shader, ubo->locFlags[2], specular);
}

//opencode: Shader Feature Level System — resolve level to feature bitmask
gX_U32 gX_Shader_FlagsFromLevel(gX_F32 level, gX_U32 extraFlags) {
    gX_U32 flags = extraFlags;
    gX_U32 base = (gX_U32)level;

    if (base >= 5) flags |= GX_SHADER_FEATURE_TEXTURE | GX_SHADER_FEATURE_LIGHTING
        | GX_SHADER_FEATURE_NORMAL_MAP | GX_SHADER_FEATURE_SPECULAR
        | GX_SHADER_FEATURE_SHADOW | GX_SHADER_FEATURE_PARALLAX | GX_SHADER_FEATURE_FOG
        | GX_SHADER_FEATURE_SKINNING | GX_SHADER_FEATURE_INSTANCING
        | GX_SHADER_FEATURE_DISPLACEMENT | GX_SHADER_FEATURE_CURVATURE | GX_SHADER_FEATURE_WIND
        | GX_SHADER_FEATURE_PBR | GX_SHADER_FEATURE_HDR | GX_SHADER_FEATURE_IBL
        | GX_SHADER_FEATURE_MORPH | GX_SHADER_FEATURE_LOD_CROSSFADE
        | GX_SHADER_FEATURE_BLOOM | GX_SHADER_FEATURE_IBL_PREFILTER
        | GX_SHADER_FEATURE_SSR | GX_SHADER_FEATURE_SSAO | GX_SHADER_FEATURE_GODRAYS;
    else if (base >= 4) flags |= GX_SHADER_FEATURE_TEXTURE | GX_SHADER_FEATURE_LIGHTING
        | GX_SHADER_FEATURE_NORMAL_MAP | GX_SHADER_FEATURE_SPECULAR
        | GX_SHADER_FEATURE_SHADOW | GX_SHADER_FEATURE_PARALLAX | GX_SHADER_FEATURE_FOG
        | GX_SHADER_FEATURE_SKINNING | GX_SHADER_FEATURE_INSTANCING
        | GX_SHADER_FEATURE_DISPLACEMENT | GX_SHADER_FEATURE_CURVATURE | GX_SHADER_FEATURE_WIND
        | GX_SHADER_FEATURE_PBR | GX_SHADER_FEATURE_HDR | GX_SHADER_FEATURE_IBL;
    else if (base >= 3) flags |= GX_SHADER_FEATURE_TEXTURE | GX_SHADER_FEATURE_LIGHTING
        | GX_SHADER_FEATURE_NORMAL_MAP | GX_SHADER_FEATURE_SPECULAR
        | GX_SHADER_FEATURE_SHADOW | GX_SHADER_FEATURE_PARALLAX | GX_SHADER_FEATURE_FOG
        | GX_SHADER_FEATURE_SKINNING | GX_SHADER_FEATURE_INSTANCING;
    else if (base >= 2) flags |= GX_SHADER_FEATURE_TEXTURE | GX_SHADER_FEATURE_LIGHTING
        | GX_SHADER_FEATURE_NORMAL_MAP | GX_SHADER_FEATURE_SPECULAR;
    else if (base >= 1) flags |= GX_SHADER_FEATURE_TEXTURE;

    return flags;
}

//opencode: convert feature bitmask to "#define GX_FEATURE_X\n" string (caller must free)
gX_I8 *gX_Shader_FlagsToDefines(gX_U32 flags) {
    // count enabled bits for allocation size
    gX_U32 count = 0;
    for (gX_U32 i = 0; i < GX_SHADER_FEATURE_COUNT; i++)
        if (flags & (1u << i)) count++;

    // each #define line: "#define GX_FEATURE_XXX\n" = ~32 bytes max
    gX_U32 bufsz = count * 64 + 64;
    gX_I8 *buf = (gX_I8*)calloc(1, (size_t)bufsz);
    if (!buf) return NULL;

    gX_I8 *p = buf;

    #define APPEND_DEFINE(name) do { \
        if (flags & name) { \
            gX_U32 n = (gX_U32)snprintf(p, (size_t)(bufsz - (gX_U32)(p - buf)), "#define " #name "\n"); \
            if (n < bufsz - (gX_U32)(p - buf)) p += n; \
        } \
    } while(0)

    APPEND_DEFINE(GX_SHADER_FEATURE_TEXTURE);
    APPEND_DEFINE(GX_SHADER_FEATURE_LIGHTING);
    APPEND_DEFINE(GX_SHADER_FEATURE_NORMAL_MAP);
    APPEND_DEFINE(GX_SHADER_FEATURE_SPECULAR);
    APPEND_DEFINE(GX_SHADER_FEATURE_SHADOW);
    APPEND_DEFINE(GX_SHADER_FEATURE_PARALLAX);
    APPEND_DEFINE(GX_SHADER_FEATURE_FOG);
    APPEND_DEFINE(GX_SHADER_FEATURE_SKINNING);
    APPEND_DEFINE(GX_SHADER_FEATURE_INSTANCING);
    APPEND_DEFINE(GX_SHADER_FEATURE_DISPLACEMENT);
    APPEND_DEFINE(GX_SHADER_FEATURE_CURVATURE);
    APPEND_DEFINE(GX_SHADER_FEATURE_WIND);
    APPEND_DEFINE(GX_SHADER_FEATURE_PBR);
    APPEND_DEFINE(GX_SHADER_FEATURE_HDR);
    APPEND_DEFINE(GX_SHADER_FEATURE_IBL);
    APPEND_DEFINE(GX_SHADER_FEATURE_MORPH);
    APPEND_DEFINE(GX_SHADER_FEATURE_LOD_CROSSFADE);
    APPEND_DEFINE(GX_SHADER_FEATURE_BLOOM);
    APPEND_DEFINE(GX_SHADER_FEATURE_IBL_PREFILTER);
    APPEND_DEFINE(GX_SHADER_FEATURE_SSR);
    APPEND_DEFINE(GX_SHADER_FEATURE_SSAO);
    APPEND_DEFINE(GX_SHADER_FEATURE_GODRAYS);

    #undef APPEND_DEFINE

    return buf;
}

//opencode: backend caps — D7: max shader feature level is AUTO-DERIVED from
//   the RHI hardware caps (gX_RHI_GetCaps) instead of a hardcoded constant.
//   The renderer's texture-unit table (constants.h) gates the ladder:
//     L1 texture-only            — any GL 2.0+
//     L2 lighting/normal/spec    — needs FBO + GLSL 130+
//     L3 shadow/parallax/fog/…   — needs units ≥ 12 (shadow 8, CSM 10, cookie 11)
//     L4 PBR/HDR/IBL/displace…   — needs units ≥ 19 (IBL 16-18, reflection 19)
//     L5 SSR/SSAO/godrays/morph  — needs units ≥ 23 (SSR HiZ chain unit 22)
//   On weak GPUs the max level drops automatically; --shader-level N still
//   overrides downward. N64 keeps its fixed L2 (Phong-class).
static gX_BackendShaderCaps gX_shader_backendCaps = {0};
static gX_I1 gX_shader_capsInitialized = 0;

static gX_I0 gX_Shader_InitBackendCaps(gX_I0) {
    if (gX_shader_capsInitialized) return;
    gX_shader_capsInitialized = 1;

    const gX_RHI_Caps *caps = gX_RHI_GetCaps();
    gX_U32 maxL = 0;
    if (caps->glMajor >= 2) maxL = 1;
    if (maxL >= 1 && caps->hasFBO && caps->shaderModel >= 130) maxL = 2;
    if (maxL >= 2 && caps->maxTextureUnits >= 12) maxL = 3;
    if (maxL >= 3 && caps->maxTextureUnits >= 19) maxL = 4;
    if (maxL >= 4 && caps->maxTextureUnits >= 23) maxL = 5;
    if (maxL > GX_SHADER_LEVEL_MAX) maxL = GX_SHADER_LEVEL_MAX;
    for (gX_U32 i = 0; i < GX_SHADER_TYPE_COUNT; i++)
        gX_shader_backendCaps.maxLevel[i] = maxL;
    gX_shader_backendCaps.supportsCompute = (gX_I1)caps->hasCompute;
    gX_shader_backendCaps.supportsTessellation = (gX_I1)caps->hasTessellation;
    gX_TraceLog(gX_LOG_INFO, "SHADER_FEATURE: backend max level %u (GL %d.%d, units=%d, fbo=%d, glsl=%d)",
        maxL, caps->glMajor, caps->glMinor, caps->maxTextureUnits, caps->hasFBO, caps->shaderModel);

    // Per-type overrides
    gX_shader_backendCaps.maxLevel[GX_SHADER_TYPE_DEPTH] = 0;          // depth is fixed
    gX_shader_backendCaps.maxLevel[GX_SHADER_TYPE_DEPTH_VISUALIZER] = 0;
    gX_shader_backendCaps.maxLevel[GX_SHADER_TYPE_WIREFRAME] = 0;
    gX_shader_backendCaps.maxLevel[GX_SHADER_TYPE_POST_BLUR] = 0;
    gX_shader_backendCaps.maxLevel[GX_SHADER_TYPE_POST_TONEMAP] = 0;
    gX_shader_backendCaps.maxLevel[GX_SHADER_TYPE_POST_BLOOM] = 0;
    gX_shader_backendCaps.maxLevel[GX_SHADER_TYPE_POST_SSAO] = 0;
    gX_shader_backendCaps.maxLevel[GX_SHADER_TYPE_POST_SSR] = 0;
    gX_shader_backendCaps.maxLevel[GX_SHADER_TYPE_POST_COMPOSITE] = 0;
}

const gX_BackendShaderCaps *gX_Shader_GetBackendCaps(gX_I0) {
    gX_Shader_InitBackendCaps();
    return &gX_shader_backendCaps;
}

// debug override (set by --shader-level N)
static gX_I32 gX_shader_forceLevel = -1;

gX_I0 gX_Shader_ForceLevel(gX_I32 level) {
    gX_shader_forceLevel = level;
}

gX_I32 gX_Shader_GetForcedLevel(gX_I0) {
    return gX_shader_forceLevel;
}
