/**********************************************************************************************
*
*   [gX: src/renderer/gfx/shader.c]
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


#include <stdlib.h> //opencode: spike-2 APPLY hook (getenv)
#include <string.h>

#include "../gfx.h"
#include "shader.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_tex.h"
//opencode: hunt - implicit-decl sweep
#include "../../rhi/rhi_buf.h"
#include "../renderer.h"
#include "gl_level.h"
#include "boilerplate.h"
#include "../../wrappers/wrappers.h"

#include "../../wrappers/wrappers.h"

#include "../../main.h"
#include "../../camera.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/utils.h"
#include "../../common/alloc.h"
#include "../../common/shader_compiler.h"

//opencode: UBO functions from GLAD

#include "../../gxmath/matrix.h"
#include "gl_level.h"



//load

gX_I32 gX_RendererGFX_Shader_Init(gX_RendererGFX_Shader *shader, const gX_I8 *vertPath, const gX_I8 *fragPath) {
    shader->loadAsap = true;
    shader->reloadCheckedFrame = (gX_U64)-1;
    shader->locsStale = false;
    if (vertPath) strcpy(shader->vertPath,vertPath);
    if (fragPath) strcpy(shader->fragPath,fragPath);
    shader->version = 330;
    shader->shaderType = -1; // legacy path by default
    return 0;
}

gX_I0 gX_RendererGFX_Shader_SetType(gX_RendererGFX_Shader *shader, gX_I32 shaderType) {
    shader->shaderType = shaderType;
}

#define GFX_SHADER_GETLOC(name) \
    shader->locations.name##Loc = gX_Shader_GetLocation(shader->shader,#name);

//ai: load the standard and diagnostic shader uniform locations.
static gX_I32 gX_RendererGFX_Shader_Load_Locations(gX_RendererGFX_Shader *shader) {
    
    gX_Shader_GetDefaultLocations(&shader->shader);
    
    shader->locations.lightSpaceLoc = gX_Shader_GetLocation(shader->shader,"matLightSpace");
    shader->locations.lightPosLoc   = gX_Shader_GetLocation(shader->shader,"lightPosition");
    shader->locations.viewPosLoc    = gX_Shader_GetLocation(shader->shader,"viewPosition");
    
    //opencode: nearPlane/farPlane/depthMap — use gX_RHI_GetUniformLocation for depth visualizer
    //   (raylib's GetShaderLocation returns -1 for custom uniforms not in RL_SHADER_LOC_*).
    shader->locations.nearPlaneLoc = gX_RHI_GetUniformLocation(shader->shader.id, "nearPlane");
    shader->locations.farPlaneLoc = gX_RHI_GetUniformLocation(shader->shader.id, "farPlane");
    shader->locations.depthMapLoc = gX_RHI_GetUniformLocation(shader->shader.id, "depthMap");
    
    GFX_SHADER_GETLOC(resolution)
    GFX_SHADER_GETLOC(boneMatrices)
    GFX_SHADER_GETLOC(gX_UseSkinning)
    GFX_SHADER_GETLOC(cameraPosition)
    GFX_SHADER_GETLOC(cameraTarget)
    GFX_SHADER_GETLOC(cameraUp)
    GFX_SHADER_GETLOC(cameraFov)
    
    
    //flags
    GFX_SHADER_GETLOC(enableLightingAmbient)
    GFX_SHADER_GETLOC(enableLightingDiffuse)
    GFX_SHADER_GETLOC(enableLightingSpecular)
    
    GFX_SHADER_GETLOC(enableMapAlbedo)
    GFX_SHADER_GETLOC(enableMapMetalness)
    GFX_SHADER_GETLOC(enableMapNormal)
    GFX_SHADER_GETLOC(enableMapParallax)
    GFX_SHADER_GETLOC(parallaxMode)
    
    GFX_SHADER_GETLOC(enableGouraud)
    GFX_SHADER_GETLOC(enablePhong)

    //opencode: PBR toggle uniform — per-model selection between Blinn-Phong
    //   (legacy) and Cook-Torrance + IBL paths.
    GFX_SHADER_GETLOC(usePBR)
    GFX_SHADER_GETLOC(oracleDiffuseOnly)
    GFX_SHADER_GETLOC(oracleGgx)
    GFX_SHADER_GETLOC(oracleBounce)

    GFX_SHADER_GETLOC(enableFog)
    GFX_SHADER_GETLOC(enableShadows)
    GFX_SHADER_GETLOC(enableReflections)
    GFX_SHADER_GETLOC(balancedReflection)
    //opencode: cache the independent per-model SSR receiver flag.
    GFX_SHADER_GETLOC(receiveSSR)
    GFX_SHADER_GETLOC(enableSSAO)
    
    GFX_SHADER_GETLOC(showPosition)
    GFX_SHADER_GETLOC(showTexCoord)
    GFX_SHADER_GETLOC(showTexCoord2)
    GFX_SHADER_GETLOC(showNormal)
    GFX_SHADER_GETLOC(showColor)
    GFX_SHADER_GETLOC(showTangent)
    GFX_SHADER_GETLOC(diagUniformMode)
    GFX_SHADER_GETLOC(debugBaseColorMap)
    //ai: optional terrain uniforms remain -1 on unrelated shaders.
    GFX_SHADER_GETLOC(terrainBlend)
    GFX_SHADER_GETLOC(terrainAlbedoMap)
    GFX_SHADER_GETLOC(terrainDetailMap)
    GFX_SHADER_GETLOC(terrainHasDetail)
    GFX_SHADER_GETLOC(terrainDetailScale)
    
    //maps
    GFX_SHADER_GETLOC(hasAlbedoMap)
    GFX_SHADER_GETLOC(hasMetalnessMap)
    GFX_SHADER_GETLOC(hasNormalMap)
    GFX_SHADER_GETLOC(hasHeightMap)
    GFX_SHADER_GETLOC(cubeMap)
    GFX_SHADER_GETLOC(shadowMap)
    GFX_SHADER_GETLOC(ssaoMap)
    GFX_SHADER_GETLOC(contactMap)   //opencode: contact shadows (spike 2)
    GFX_SHADER_GETLOC(enableContact) //opencode: contact shadows (spike 2)
    
    GFX_SHADER_GETLOC(albedoMapScale)
    GFX_SHADER_GETLOC(albedoMapOffset)
    GFX_SHADER_GETLOC(albedoMapFactor)
    
    GFX_SHADER_GETLOC(metalnessMapScale)
    GFX_SHADER_GETLOC(metalnessMapOffset)
    GFX_SHADER_GETLOC(metalnessMapFactor)
    
    GFX_SHADER_GETLOC(normalMapScale)
    GFX_SHADER_GETLOC(normalMapOffset)
    GFX_SHADER_GETLOC(normalMapFactor)
    
    GFX_SHADER_GETLOC(heightMapScale)
    GFX_SHADER_GETLOC(heightMapOffset)
    GFX_SHADER_GETLOC(heightMapFactor)

    //opencode: static lightmap controls and sampler.
    GFX_SHADER_GETLOC(hasLightmap)
    GFX_SHADER_GETLOC(lightmapStrength)
    GFX_SHADER_GETLOC(lightmapScale)
    GFX_SHADER_GETLOC(lightmapOffset)
    GFX_SHADER_GETLOC(lightmapMap)

    //fog
    GFX_SHADER_GETLOC(fogNear)
    GFX_SHADER_GETLOC(fogFar)
    GFX_SHADER_GETLOC(fogColor)
    GFX_SHADER_GETLOC(heightFogDensity)
    GFX_SHADER_GETLOC(heightFogBase)

    //opencode: detail texture
    GFX_SHADER_GETLOC(detailMap)
    GFX_SHADER_GETLOC(detailUVScale)
    GFX_SHADER_GETLOC(detailStrength)
    GFX_SHADER_GETLOC(detailDistFade)

    //ambient
    GFX_SHADER_GETLOC(ambientColor)
    GFX_SHADER_GETLOC(ambientFactor)
    
    //sun
    GFX_SHADER_GETLOC(sunColor)
    GFX_SHADER_GETLOC(sunBrightness)
    GFX_SHADER_GETLOC(exposure)
    GFX_SHADER_GETLOC(sunDiffuseTint)
    GFX_SHADER_GETLOC(sunSpecularTint)
    GFX_SHADER_GETLOC(sunDiffuseFactor)
    GFX_SHADER_GETLOC(sunSpecularFactor)
    
    //opencode: rim lighting
    GFX_SHADER_GETLOC(rimColor)
    GFX_SHADER_GETLOC(rimIntensity)
    GFX_SHADER_GETLOC(rimPower)

    GFX_SHADER_GETLOC(envAmbientFactor)

    //lights
    GFX_SHADER_GETLOC(lightsAmount)
    GFX_SHADER_GETLOC(lightsType)
    GFX_SHADER_GETLOC(lightsFov)
    GFX_SHADER_GETLOC(lightsBrightness)
    GFX_SHADER_GETLOC(lightsPos)
    GFX_SHADER_GETLOC(lightsDir)
    GFX_SHADER_GETLOC(lightsColor)
    GFX_SHADER_GETLOC(lightsAttenConst)
    GFX_SHADER_GETLOC(lightsAttenLinear)
    GFX_SHADER_GETLOC(lightsAttenQuad)
    GFX_SHADER_GETLOC(lightsDiffuseFactor)
    GFX_SHADER_GETLOC(lightsSpecularFactor)

    //opencode: light cookies — use gX_RHI_GetUniformLocation directly (raylib's
    //   GetShaderLocation returns -1 for custom uniforms not in RL_SHADER_LOC_*).
    shader->locations.lightsCookieAtlasLoc = gX_RHI_GetUniformLocation(shader->shader.id, "lightsCookieAtlas");
    shader->locations.lightsCookieRectsLoc = gX_RHI_GetUniformLocation(shader->shader.id, "lightsCookieRects");
    shader->locations.lightsCookieEnabledLoc = gX_RHI_GetUniformLocation(shader->shader.id, "lightsCookieEnabled");
    
    //lights
    GFX_SHADER_GETLOC(shadowMode)
    GFX_SHADER_GETLOC(shadowDiffuseBias)
    GFX_SHADER_GETLOC(shadowSpecularBias)
    
    //opencode: point light shadows (looked up directly since they use sampler arrays)
    shader->locations.pointShadowMapsLoc = gX_Shader_GetLocation(shader->shader, "pointShadowMaps");
    shader->locations.pointShadowFarPlanesLoc = gX_Shader_GetLocation(shader->shader, "pointShadowFarPlanes");
    shader->locations.pointShadowRangesLoc = gX_Shader_GetLocation(shader->shader, "pointShadowRanges");
    
    //model
    GFX_SHADER_GETLOC(albedoFactor)
    GFX_SHADER_GETLOC(metalnessFactor)
    GFX_SHADER_GETLOC(modelShininess)
    GFX_SHADER_GETLOC(albedoColor)
    GFX_SHADER_GETLOC(metalnessColor)
    
    //opencode: specular color tint
    GFX_SHADER_GETLOC(specularColor)

    //model matrices
    GFX_SHADER_GETLOC(matModel)
    GFX_SHADER_GETLOC(gX_ModelMatrix)
    GFX_SHADER_GETLOC(matNormal)
    GFX_SHADER_GETLOC(ssrNormalPass)

    //opencode: PBR roughness/AO
    GFX_SHADER_GETLOC(hasRoughnessMap)
    GFX_SHADER_GETLOC(hasAoMap)
    GFX_SHADER_GETLOC(roughness)
    GFX_SHADER_GETLOC(ao)
    GFX_SHADER_GETLOC(roughnessMapScale)
    GFX_SHADER_GETLOC(roughnessMapOffset)
    GFX_SHADER_GETLOC(roughnessMapFactor)
    GFX_SHADER_GETLOC(aoMapScale)
    GFX_SHADER_GETLOC(aoMapOffset)
    GFX_SHADER_GETLOC(aoMapFactor)
    //opencode: emissive
    GFX_SHADER_GETLOC(hasEmissiveMap)
    GFX_SHADER_GETLOC(emissiveFactor)
    GFX_SHADER_GETLOC(emissiveColor)
    GFX_SHADER_GETLOC(lightMarkerMode)
    //opencode: emission map scale/offset
    GFX_SHADER_GETLOC(emissiveMapScale)
    GFX_SHADER_GETLOC(emissiveMapOffset)
    GFX_SHADER_GETLOC(emissiveMapFactor)
    
    //opencode: alpha test
    GFX_SHADER_GETLOC(enableAlphaTest)
    GFX_SHADER_GETLOC(alphaCutoff)
    
    //opencode: alpha blending
    GFX_SHADER_GETLOC(blendOpacity)
    
    //opencode: planar reflection
    GFX_SHADER_GETLOC(reflTex)
    GFX_SHADER_GETLOC(reflMVP)
    GFX_SHADER_GETLOC(planarReflection)
    GFX_SHADER_GETLOC(mirrorFill)
    //opencode: non-planar reflection (GTA4/5 style) + wave displacement
    GFX_SHADER_GETLOC(nonPlanarReflection)
    GFX_SHADER_GETLOC(reflectionPlaneNormal)
    GFX_SHADER_GETLOC(reflectionPlaneHeight)
    GFX_SHADER_GETLOC(waveAmplitude)
    GFX_SHADER_GETLOC(gxTime)
    //opencode: parallax-corrected cubemap
    GFX_SHADER_GETLOC(cubePos)
    GFX_SHADER_GETLOC(cubeSize)
    //opencode: probe-grid blend — second-nearest probe (unit 31)
    GFX_SHADER_GETLOC(cubeMapB)
    GFX_SHADER_GETLOC(cubePosB)
    GFX_SHADER_GETLOC(probeBlendB)
    //opencode: refractive materials
    GFX_SHADER_GETLOC(refractiveIndex)
    GFX_SHADER_GETLOC(enableRefraction)
    GFX_SHADER_GETLOC(bgTex)
    GFX_SHADER_GETLOC(ssaoMap)

    //opencode: TAA jitter
    GFX_SHADER_GETLOC(taaJitter)

    //opencode: oracle (Paper 1) - per-frame PCF seed
    GFX_SHADER_GETLOC(oracleSeed)
    GFX_SHADER_GETLOC(oracleSeedActive)
    //opencode: oracle estimator - single-light Monte Carlo pick
    GFX_SHADER_GETLOC(oracleEstimateActive)
    //opencode: oracle multi-ray (Paper 1) - rays per pixel per frame
    GFX_SHADER_GETLOC(oracleRayCount)
    //opencode: Paper 1 M1 - cosine-hemisphere environment transport oracle
    GFX_SHADER_GETLOC(oraclePathActive)
    //opencode: M1 - transparent roughness-alpha repair pass flag
    GFX_SHADER_GETLOC(roughnessAlpha)

    //opencode: view/projection matrices
    GFX_SHADER_GETLOC(matView)
    GFX_SHADER_GETLOC(matProjection)

    //opencode: IBL
    GFX_SHADER_GETLOC(irradianceMap)
    GFX_SHADER_GETLOC(prefilterMap)
    //opencode: v0.3.8 - fog delta LUT
    shader->locations.fogLutMapLoc = gX_Shader_GetLocation(shader->shader, "fogLutMap");
    shader->locations.hasFogLutLoc = gX_Shader_GetLocation(shader->shader, "hasFogLut");
    //opencode: brdf LUT — shader name is "bdrfMap", store to brdfLutLoc
    shader->locations.brdfLutLoc = gX_Shader_GetLocation(shader->shader, "bdrfMap");

    //opencode: reflection bias, gloss map, flat shading
    GFX_SHADER_GETLOC(reflectionBias)
    GFX_SHADER_GETLOC(hasGlossMap)
    GFX_SHADER_GETLOC(glossMap)
    GFX_SHADER_GETLOC(enableFlat)

    //opencode: configurable shader tuning constants
    GFX_SHADER_GETLOC(shadowBiasMax)
    GFX_SHADER_GETLOC(shadowBiasMin)
    GFX_SHADER_GETLOC(parallaxMinLayers)
    GFX_SHADER_GETLOC(parallaxMaxLayers)
    GFX_SHADER_GETLOC(parallaxOffset)
    GFX_SHADER_GETLOC(parallaxShadowOffset)
    GFX_SHADER_GETLOC(parallaxShadowSteps)
    GFX_SHADER_GETLOC(pointShadowBias)
    GFX_SHADER_GETLOC(pointShadowExtra)
    GFX_SHADER_GETLOC(pointShadowMinBias)
    GFX_SHADER_GETLOC(tangentLengthThreshold)
    GFX_SHADER_GETLOC(normalYThreshold)

    //opencode: CSM — single sampler2DArray (name must match standard.frag
    //   declaration csmShadowMapArray — the old "csmShadowMap" query was a
    //   silent -1, leaving the array never bound)
    GFX_SHADER_GETLOC(csmShadowMapArray)
    GFX_SHADER_GETLOC(csmSplits)
    GFX_SHADER_GETLOC(csmBlendScale)
    GFX_SHADER_GETLOC(cascadeCount)
    //opencode: csmVP is a mat4[4] array — needs special lookup
    shader->locations.csmVPLoc = gX_Shader_GetLocation(shader->shader, "csmVP");
    //opencode: hardware instancing uniforms
    GFX_SHADER_GETLOC(instanceCount)
    GFX_SHADER_GETLOC(instanceMatrices)
    
    return 0;
}

gX_I0 gX_RendererGFX_Shader_BindTextureUniforms(gX_RendererGFX_Shader *shader) {
    // Populate raylib locs array for material texture sampler binding
    // This ensures raylib's mesh drawing binds textures to correct sampler uniforms
    // raylib locs indices: ALBEDO=15, METALNESS=16, NORMAL=17, ROUGHNESS=18,
    // OCCLUSION=19, EMISSION=20, HEIGHT=21, CUBEMAP=22, IRRADIANCE=23, PREFILTER=24, BRDF=25
    if (shader->shader.id > 0 && shader->shader.locs != NULL) {
        shader->shader.locs[15] = gX_Shader_GetLocation(shader->shader, "albedoMap");
        shader->shader.locs[16] = gX_Shader_GetLocation(shader->shader, "metalnessMap");
        shader->shader.locs[17] = gX_Shader_GetLocation(shader->shader, "normalMap");
        shader->shader.locs[18] = gX_Shader_GetLocation(shader->shader, "roughnessMap");
        shader->shader.locs[19] = gX_Shader_GetLocation(shader->shader, "occlusionMap");
        shader->shader.locs[20] = gX_Shader_GetLocation(shader->shader, "emissionMap");
        shader->shader.locs[21] = gX_Shader_GetLocation(shader->shader, "heightMap");
        shader->shader.locs[22] = gX_Shader_GetLocation(shader->shader, "cubeMap");
        shader->shader.locs[23] = gX_Shader_GetLocation(shader->shader, "irradianceMap");
        shader->shader.locs[24] = gX_Shader_GetLocation(shader->shader, "prefilterMap");
        shader->shader.locs[25] = gX_Shader_GetLocation(shader->shader, "bdrfMap");
        //opencode: glossMap at loc 26 (extra slot, not in standard raylib material maps)
        shader->shader.locs[26] = gX_Shader_GetLocation(shader->shader, "glossMap");
    }
}

//opencode: hot-reload callback — re-query locations, rebind texture uniforms, recreate UBOs
static gX_I0 gX_RendererGFX_Shader_OnReload(gX_Shader *unused, gX_I0 *userData) {
    (void)unused;
    gX_RendererGFX_Shader *shader = (gX_RendererGFX_Shader *)userData;
    //opencode: mark locations stale so effect shaders (bloom/dof/taa/mb/godray/
    //   lensflare/fog/ssr/ssao) re-cache their per-effect uniform locations —
    //   they cache once and would otherwise keep uploading to the OLD program.
    shader->locsStale = true;
    gX_RendererGFX_Shader_Load_Locations(shader);
    gX_RendererGFX_Shader_BindTextureUniforms(shader);

    if (shader->uboStandard > 0) {
        gX_RHI_Buffer_Delete(shader->uboStandard);
        shader->uboStandard = 0;
    }
    shader->uboStandardIndex = -1;
    shader->uboStandardIndex = gX_RHI_GetUniformBlockIndex(shader->shader.id, "gX_Std");
    if (shader->uboStandardIndex != (gX_I32)gX_RHI_INVALID_INDEX && shader->uboStandardIndex >= 0) {
        gX_RHI_UniformBlockBinding(shader->shader.id, (gX_U32)shader->uboStandardIndex, 0);
        shader->uboStandard = gX_RHI_Buffer_Create("ubo_standard");
        gX_RHI_Buffer_Bind(gX_RHI_UNIFORM_BUFFER, shader->uboStandard);
        gX_RHI_Buffer_Data(gX_RHI_UNIFORM_BUFFER, (gX_U32)sizeof(gX_UBO_Standard), NULL, gX_RHI_DYNAMIC_DRAW);
        gX_RHI_Buffer_Bind(gX_RHI_UNIFORM_BUFFER, 0);
    }
}

gX_I32 gX_RendererGFX_Shader_Load(gX_RendererGFX_Shader *shader) {
    //opencode: every new program requires consumers to refresh their cached locations
    shader->locsStale = true;
    //opencode: use local buffers for vert/frag paths — gX_Text_Format may use a
    //   single static buffer (GLFW), causing the SECOND call to overwrite the
    //   first, making both vert and frag point to the same string.
    gX_I8 vert[gX_GFX_PATH_BUFFER] = {0}, frag[gX_GFX_PATH_BUFFER] = {0};
    if (shader->vertPath[0] != '\0')
        snprintf(vert, sizeof(vert), shader->vertPath, shader->version);
    if (shader->fragPath[0] != '\0')
        snprintf(frag, sizeof(frag), shader->fragPath, shader->version);
    //opencode: use Compile_Level when shaderType is set (feature level system)
    gX_I8 *definesPtr = shader->defines[0] ? shader->defines : NULL;
    if (shader->shaderType >= 0 && shader->shaderType < GX_SHADER_TYPE_COUNT) {
        //opencode: request the max feature level — gX_Shader_Compile_Level
        //   clamps by template cap AND caps-driven backend maxLevel (D7).
        shader->shader = gX_Shader_Compile_Level(
            (gX_ShaderType)shader->shaderType,
            (gX_F32)GX_SHADER_LEVEL_MAX, 0, definesPtr);
    } else {
        // Legacy path (fallback)
        shader->shader = gX_Shader_Load_Ex(
            vert[0] ? vert : NULL,
            frag[0] ? frag : NULL,
            definesPtr);
    }
    gX_RendererGFX_Shader_Load_Locations(shader);
    gX_RendererGFX_Shader_BindTextureUniforms(shader);
    shader->loaded = true;
    gX_GL_RawRegister(shader->shader.id);
    gX_TraceLog_GFX(gX_LOG_NOTICE,"loaded shader [%s:%s] id=%u",shader->vertPath,shader->fragPath,shader->shader.id);

    //opencode: initialize std140 UBO for the standard block (binding 0)
    shader->uboStandard = 0;
    shader->uboStandardIndex = -1;
    if (shader->shader.id > 0) {
        memset(&shader->uboData, 0, sizeof(shader->uboData));
        shader->uboStandardIndex = gX_RHI_GetUniformBlockIndex(shader->shader.id, "gX_Std");
        if (shader->uboStandardIndex != (gX_I32)gX_RHI_INVALID_INDEX && shader->uboStandardIndex >= 0) {
            gX_RHI_UniformBlockBinding(shader->shader.id, (gX_U32)shader->uboStandardIndex, 0);
            shader->uboStandard = gX_RHI_Buffer_Create("ubo_standard");
            gX_RHI_Buffer_Bind(gX_RHI_UNIFORM_BUFFER, shader->uboStandard);
            gX_RHI_Buffer_Data(gX_RHI_UNIFORM_BUFFER, (gX_U32)sizeof(gX_UBO_Standard), NULL, gX_RHI_DYNAMIC_DRAW);
            gX_RHI_Buffer_Bind(gX_RHI_UNIFORM_BUFFER, 0);
            gX_TraceLog_GFX(gX_LOG_TRACE, "gX_Std UBO created: id=%u, blockIndex=%d, size=%zu",
                shader->uboStandard, shader->uboStandardIndex, sizeof(gX_UBO_Standard));
        } else {
            //opencode: UBO not found — query active uniform blocks for diagnostics
            gX_I32 activeBlocks = 0;
            gX_RHI_GetProgramiv(shader->shader.id, gX_RHI_ACTIVE_UNIFORM_BLOCKS, &activeBlocks);
            gX_TraceLog_GFX(gX_LOG_DEBUG, "gX_Std UBO: no block found (shader has %d active uniform blocks) — "
                "falling back to individual uniforms. This is expected on raylib/NVIDIA GL config "
                "where UBO blocks are stripped by the shader loader.", activeBlocks);
        }
    }

    //opencode: watch for hot-reload with callback, store defines for recompile
    gX_Shader_Compiler_Watch_Ex(shader->shader, vert, frag, NULL,
        gX_RendererGFX_Shader_OnReload, (gX_I0 *)shader);
    if (definesPtr) {
        gX_Shader_Compiler_SetDefines(shader->shader, definesPtr);
    }
    return 0;
}

//unload

gX_I32 gX_RendererGFX_Shader_Unload(gX_RendererGFX_Shader *shader) {
    //opencode: unwatch before unload
    if (shader->shader.id > 0) {
        gX_GL_RawUnregister(shader->shader.id);
        gX_Shader_Compiler_Unwatch(shader->shader);
    }
    //opencode: delete UBO buffers
    if (shader->uboStandard > 0) {
        gX_RHI_Buffer_Delete(shader->uboStandard);
        shader->uboStandard = 0;
    }
    gX_Shader_Unload(shader->shader);
    shader->loaded = false;
    return 0;
}

//update

static gX_I0 gX_RendererGFX_Shader_SetValues(gX_RendererGFX_Shader *shader, gX_RendererGFX *gfx) {

    //opencode: UBO upload was abandoned — the shader uses individual uniforms instead
    //   (preprocessor injects STANDARD_UNIFORM_TEXT, not a std140 UBO block).
    //   The uboStandard stays 0 because glGetUniformBlockIndex returns INVALID_INDEX.
    //   The individual uniforms are uploaded below via gX_Shader_SetValue_*.

    //opencode: activate shader program first so all uniforms target the correct
    //   program — was setting matLightSpace etc on the wrong program (main shader
    //   instead of depth). Explicitly binding here also covers the case where the
    //   shadow map is unloaded (shadow bind below is skipped) — otherwise the raw
    //   RHI uniform calls in the lights block target the previously-active effect
    //   program and spam "uniform not found" per frame.
    gX_RHI_Shader_Use(shader->shader.id);
    //opencode: Step-3-bis — never bind an array through the 2D-forcing
    //   raylib path: during the CSM cascade loop sun.camera.map is aliased
    //   to the array wrapper (draw.c), so depth.id is the 2D-array texture
    //   and this bind raised GL_INVALID_OPERATION "target mismatch" once
    //   per frame on every driver (the array is sampled via
    //   csmShadowMapArray; the 2D shadowMap sampler keeps its cascade-0
    //   binding, which depth draws never sample anyway).
    if (gfx->sun.camera.map && gfx->sun.camera.map->depth.id > 0
        && gX_RHI_Tex_GetTarget(gfx->sun.camera.map->depth.id) != gX_RHI_TEXTURE_2D_ARRAY) {
        gX_Shader_SetValue_TextureSlot(shader->shader, shader->locations.shadowMapLoc, gfx->sun.camera.map->depth, gX_GFX_TEXUNIT_SHADOW);
    }

    //opencode: cubeMap — sky environment cubemap for legacy env/reflection
    //   sampling in standard.frag. Was never bound: the sampler uniform stayed
    //   at default unit 0 (2D albedo map), making the cube sampler incomplete →
    //   GL_INVALID_OPERATION "program texture usage" on every model draw
    //   (surfaced by the GLFW 4.3 debug callback). Falls back to the dummy
    //   white cubemap until the skybox is loaded.
    if (shader->locations.cubeMapLoc >= 0) {
        gX_Texture envCube = gfx->sky.skybox.wrapper.texture;
        if (envCube.id == 0) envCube = gfx->ibl.dummyCubemap;
        if (envCube.id > 0) {
            gX_Shader_SetValue_CubemapSlot(shader->shader, shader->locations.cubeMapLoc, envCube, gX_GFX_TEXUNIT_SKYBOX);
            //opencode: inactive probe-B sampler shares the valid cubemap
            //   binding instead of defaulting to material unit 0 (2D albedo).
            if (shader->locations.cubeMapBLoc >= 0)
                gX_Shader_SetValue_CubemapSlot(shader->shader, shader->locations.cubeMapBLoc, envCube, gX_GFX_TEXUNIT_SKYBOX);
        }
    }
    
    //opencode: CSM uniforms — cascade shadow maps + matrices (up to 4 cascades)
    //   shadowMap stays bound to map0 (cascade 0); the shader samples the array
    //   layers for cascades 1..N-1, selected by view depth against csmSplits.
    //opencode: CSM — single sampler2DArray at dedicated unit. Bound via
    //   the target-aware RHI path: the rl TextureSlot bind forces
    //   GL_TEXTURE_2D, which rejects the array (GL_INVALID_OPERATION
    //   "target mismatch" ~700x/run, sampler reads black = no CSM).
    //   The unit upload is UNCONDITIONAL: before deferred CSM init
    //   (arrayTexId==0, frame 1) the sampler would sit at default unit
    //   0 alongside albedoMap — cross-type collision (2DARRAY vs 2D)
    //   fails program validation and strict drivers skip the draw.
    //   Parked on its dedicated unit it validates; sampling black is
    //   gated by cascade state anyway.
    if (shader->locations.csmShadowMapArrayLoc >= 0) {
        if (gfx->csm.arrayTexId > 0) {
            gX_RHI_Tex_BindOnUnit(gfx->csm.arrayTexId, gX_GFX_TEXUNIT_CSM_ARRAY);
        }
        gX_RHI_Shader_Use(shader->shader.id);
        gX_RHI_UniformInt(shader->locations.csmShadowMapArrayLoc, gX_GFX_TEXUNIT_CSM_ARRAY);
    }
    //opencode: CSM VP matrices + split distances + cascade count
    if (shader->locations.csmVPLoc >= 0) {
        gX_Shader_SetValue_Matrix(shader->shader, shader->locations.csmVPLoc, gfx->csm.vps[0]);
        for (gX_I32 ci = 1; ci < gfx->csm.cascadeCount && ci < 4; ci++) {
            gX_I32 vpLoc = shader->locations.csmVPLoc + ci;
            gX_Shader_SetValue_Matrix(shader->shader, vpLoc, gfx->csm.vps[ci]);
        }
    }
    if (shader->locations.csmSplitsLoc >= 0) {
        gX_F32v3 splits = {gfx->csm.splits[0], gfx->csm.splits[1], gfx->csm.splits[2]};
        gX_Shader_SetValue_V3(shader->shader, shader->locations.csmSplitsLoc, splits);
    }
    if (shader->locations.csmBlendScaleLoc >= 0)
        gX_Shader_SetValue_Float(shader->shader, shader->locations.csmBlendScaleLoc, gfx->csm.blendScale);
    gX_Shader_SetValue_Int(shader->shader, shader->locations.cascadeCountLoc, gfx->csm.cascadeCount);
    
    //opencode: matLightSpace drives fs_in.lightspaceFragPosition, which the
    //   shader uses for cascade 0 (shadowMap). In CSM mode that must be
    //   cascade 0's VP, not the full-scene VP.
    if (gfx->csm.cascadeCount > 1)
        gX_Shader_SetValue_Matrix(shader->shader, shader->locations.lightSpaceLoc, gfx->csm.vps[0]);
    else
        gX_Shader_SetValue_Matrix(shader->shader, shader->locations.lightSpaceLoc, gfx->sun.camera.viewProjection);
    
    //opencode: cascade selection linearizes MAIN-view depth, so these must
    //   be the main camera planes (0.1/1000) — the old sun-camera values
    //   (0.1/sceneSize*4) mis-scaled every view distance (D=60 read as 36),
    //   sliding fragments into nearer cascades than they belong to.
    gX_Shader_SetValue_Float(shader->shader,shader->locations.nearPlaneLoc, camera.frustum.near);
    gX_Shader_SetValue_Float(shader->shader,shader->locations.farPlaneLoc, camera.frustum.far);
    
    //opencode: IBL — bind irradiance + prefilter cubemaps + BRDF LUT
    //   Units 25-27 chosen to avoid raylib's material map range (0-11) and detail map (24).
    if (gfx->ibl.ready) {
        if (shader->locations.irradianceMapLoc >= 0 && gfx->ibl.irradianceMap.id > 0)
            gX_Shader_SetValue_CubemapSlot(shader->shader, shader->locations.irradianceMapLoc, gfx->ibl.irradianceMap, gX_GFX_TEXUNIT_IBL_IRRADIANCE);
        if (shader->locations.prefilterMapLoc >= 0 && gfx->ibl.prefilterMap.id > 0)
            gX_Shader_SetValue_CubemapSlot(shader->shader, shader->locations.prefilterMapLoc, gfx->ibl.prefilterMap, gX_GFX_TEXUNIT_IBL_PREFILTER);
        if (shader->locations.brdfLutLoc >= 0 && gfx->ibl.brdfLut.id > 0)
            gX_Shader_SetValue_TextureSlot(shader->shader, shader->locations.brdfLutLoc, gfx->ibl.brdfLut, gX_GFX_TEXUNIT_IBL_BRDF);
        //opencode: v0.3.8 - fog delta LUT for the IBL reflection (mirror-
        //  horizon seam); gate on fog so an unrendered LUT adds nothing
    } else {
        //opencode: fallback — dynamic cubemap for both irradiance and
        //   prefilter, but ONLY if this frame rendered it (audit: sticky
        //   cubeReady sampled stale faces — celestial features vanished).
        //   Last resort stays the dummy white cubemap (anti-NaN).
        gX_I1 dynFresh = (gfx->reflection.dynamicCubemap.id > 0 && gfx->reflection.cubeUpdatedThisFrame);
        if (dynFresh) {
            if (shader->locations.irradianceMapLoc >= 0)
                gX_Shader_SetValue_CubemapSlot(shader->shader, shader->locations.irradianceMapLoc, gfx->reflection.dynamicCubemap, gX_GFX_TEXUNIT_IBL_IRRADIANCE);
            if (shader->locations.prefilterMapLoc >= 0)
                gX_Shader_SetValue_CubemapSlot(shader->shader, shader->locations.prefilterMapLoc, gfx->reflection.dynamicCubemap, gX_GFX_TEXUNIT_IBL_PREFILTER);
        }
        //opencode: last resort — dummy white cubemap prevents NaN on NVIDIA
        //   when QUALITY >= 1 enables the IBL shader path. Without a bound
        //   cubemap, unbound sampler reads on NVIDIA return NaN which
        //   propagates through PBR math to produce garbage output.
        //   (audit: was bound unconditionally AFTER the dynamic map,
        //   overwriting it — now true last resort only.)
        if (!dynFresh && gfx->ibl.dummyCubemap.id > 0) {
            if (shader->locations.irradianceMapLoc >= 0)
                gX_Shader_SetValue_CubemapSlot(shader->shader, shader->locations.irradianceMapLoc, gfx->ibl.dummyCubemap, gX_GFX_TEXUNIT_IBL_IRRADIANCE);
            if (shader->locations.prefilterMapLoc >= 0)
                gX_Shader_SetValue_CubemapSlot(shader->shader, shader->locations.prefilterMapLoc, gfx->ibl.dummyCubemap, gX_GFX_TEXUNIT_IBL_PREFILTER);
        }
    }
    //opencode: v0.3.8 - fog delta LUT for the IBL/cubemap reflection
    //   (mirror-horizon seam). Works for both ibl.ready and the fallback:
    //   either way the reflection samples an UNFOGGED environment.
    if (shader->locations.fogLutMapLoc >= 0 && gfx->ssr.fogLut.wrapper.loadingState.loaded
        && gfx->state.ENABLE_VOLUME_FOG) {
        gX_Shader_SetValue_TextureSlot(shader->shader, shader->locations.fogLutMapLoc,
            gfx->ssr.fogLut.wrapper.rt.texture, gX_GFX_TEXUNIT_FOG_LUT);
        if (shader->locations.hasFogLutLoc >= 0)
            gX_RHI_UniformInt(shader->locations.hasFogLutLoc, 1);
    } else if (shader->locations.hasFogLutLoc >= 0) {
        gX_RHI_UniformInt(shader->locations.hasFogLutLoc, 0);
    }

    {
        gX_RendererGFX_Light *lightPtrs = gfx->lights.data;
        //opencode: bind the shared point shadow 2D array (layer = light*6+face).
        //   Always bound — the driver validates sampler/texture combos at draw
        //   time; an unbound sampler2DArray at unit 0 (2D material textures)
        //   would reject the draw ("program texture usage" — black scene).
        //   farPlanes stay gated (0 = no sampling), so it's inert when off.
        if (shader->locations.pointShadowMapsLoc >= 0) {
            gX_U32 arrayId = gfx->common.pointShadowArray;
            if (arrayId == 0) {
                //opencode: Phase 1b — lazy point-shadow storage: the full
                //   384-layer array (~400 MB) is created only for a real
                //   shadow-casting point light (same predicate as
                //   Light_Update); otherwise a tiny placeholder keeps the
                //   sampler valid (farPlanes stay 0 → never sampled).
                gX_I1 needArray = 0;
                GX_HEAP_FOREACH(gfx->lights, gX_RendererGFX_Light, pl) {
                    //opencode: no break — GX_HEAP_FOREACH break only skips
                    //   to the next entry; the flag guards instead.
                    if (!needArray && pl->castShadows && !pl->sun) needArray = 1;
                }
                if (needArray) gX_RendererGFX_PointShadow_InitArray(gfx);
                else gX_RendererGFX_PointShadow_InitPlaceholder(gfx);
                arrayId = gfx->common.pointShadowArray;
            }
            //opencode: target-aware RHI bind — the rl TextureSlot bind forces
            //   GL_TEXTURE_2D, which rejects the 2D_ARRAY (GL_INVALID_OPERATION
            //   "target mismatch" ~350x/run, sampler reads black). Same class
            //   as the CSM array + env-cubemap material fixes.
            if (arrayId > 0) {
                gX_RHI_Tex_BindOnUnit(arrayId, gX_GFX_TEXUNIT_POINT_SHADOW);
                gX_RHI_Shader_Use(shader->shader.id);
                gX_RHI_UniformInt(shader->locations.pointShadowMapsLoc, gX_GFX_TEXUNIT_POINT_SHADOW);
            }
        }
        for (gX_U32 li = 0; li < gX_GFX_MAX_LIGHTS; li++) {
            gX_F32 farPlane = 0.0f;
            if (gfx->state.ENABLE_POINT_SHADOWS &&
                li < gfx->lights.amount &&
                gfx->lights.meta[li].USED &&
                lightPtrs[li].castShadows &&
                lightPtrs[li].shadowDepth.id > 0) {
                farPlane = lightPtrs[li].shadowFar;
            }
            if (shader->locations.pointShadowFarPlanesLoc >= 0) {
                gX_Shader_SetValue_Float(shader->shader,
                    shader->locations.pointShadowFarPlanesLoc + (gX_I32)li, farPlane);
            }

            //opencode: per-light shadow range — 0 = unlimited, >0 = max distance for shadows
            if (shader->locations.pointShadowRangesLoc >= 0) {
                gX_F32 rangeVal = 0.0f;
                if (li < gfx->lights.amount &&
                    gfx->lights.meta[li].USED && lightPtrs[li].castShadows) {
                    rangeVal = lightPtrs[li].shadowRange;
                }
                gX_Shader_SetValue_Float(shader->shader,
                    shader->locations.pointShadowRangesLoc + (gX_I32)li, rangeVal);
            }
        }
    }

    //opencode: sun.dir is direction light TRAVELS; shader expects direction TO light
    {
        static gX_I1 loggedLightPos = false;
        if (!loggedLightPos) {
            loggedLightPos = true;
            gX_TraceLog_GFX(gX_LOG_DEBUG, "LIGHTING DIAG: lightPosLoc=%d viewPosLoc=%d ambientColorLoc=%d ambientFactorLoc=%d sunColorLoc=%d sunBrightnessLoc=%d matViewLoc=%d matProjLoc=%d matModelLoc=%d",
                shader->locations.lightPosLoc, shader->locations.viewPosLoc,
                shader->locations.ambientColorLoc, shader->locations.ambientFactorLoc,
                shader->locations.sunColorLoc, shader->locations.sunBrightnessLoc,
                shader->locations.matViewLoc, shader->locations.matProjectionLoc,
                shader->locations.matModelLoc);
        }
    }
    //opencode: lightPosition = direction FROM surface TO light (= -sun.dir)
    gX_F32v3 lightDir = gX_F32v3_Scale(gfx->sun.dir, -1.0f);
    gX_Shader_SetValue_V3(shader->shader,shader->locations.lightPosLoc, lightDir);
    gX_Shader_SetValue_V3(shader->shader,shader->locations.viewPosLoc,  camera.camera3D.position);
    
    gX_F32v2 res = (gX_F32v2){maindata.renderWidth,maindata.renderHeight};
    gX_Shader_SetValue_V2(shader->shader,shader->locations.resolutionLoc, res);
    gX_Shader_SetValue_V3(shader->shader,shader->locations.cameraPositionLoc,   camera.camera3D.position);
    gX_Shader_SetValue_V3(shader->shader,shader->locations.cameraTargetLoc,     camera.camera3D.target);
    gX_Shader_SetValue_V3(shader->shader,shader->locations.cameraUpLoc,         camera.camera3D.up);
    gX_Shader_SetValue_Float(shader->shader,shader->locations.cameraFovLoc,     camera.camera3D.fovy);
    //opencode: global time — drives the wave displacement (waveAmplitude > 0)
    if (shader->locations.gxTimeLoc >= 0)
        gX_Shader_SetValue_Float(shader->shader, shader->locations.gxTimeLoc, (gX_F32)gX_FrameTime_Get());
    //opencode: upload view/projection matrices (raylib doesn't update these for custom shaders)
    {
        gX_Matrix viewMat = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
        gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
        gX_Matrix projMat = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect,
            camera.frustum.near, camera.frustum.far);
        gX_Shader_SetValue_Matrix(shader->shader, shader->locations.matViewLoc, viewMat);
        gX_Shader_SetValue_Matrix(shader->shader, shader->locations.matProjectionLoc, projMat);
    }
    //opencode: TAA jitter (upload even when disabled — defaults to 0,0 in shader)
    gX_Shader_SetValue_V2(shader->shader, shader->locations.taaJitterLoc, gfx->taa.jitter);
    
    //flags
    gX_Shader_SetValue_Int(shader->shader,shader->locations.enableLightingAmbientLoc, gfx->state.ENABLE_LIGHTING_AMBIENT);
    gX_Shader_SetValue_Int(shader->shader,shader->locations.enableLightingDiffuseLoc, gfx->state.ENABLE_LIGHTING_DIFFUSE);
    gX_Shader_SetValue_Int(shader->shader,shader->locations.enableLightingSpecularLoc, gfx->state.ENABLE_LIGHTING_SPECULAR);
    
    gX_Shader_SetValue_Int(shader->shader,shader->locations.enableMapAlbedoLoc, gfx->state.ENABLE_MAP_ALBEDO);
    gX_Shader_SetValue_Int(shader->shader,shader->locations.enableMapMetalnessLoc, gfx->state.ENABLE_MAP_METALNESS);
    gX_Shader_SetValue_Int(shader->shader,shader->locations.enableMapNormalLoc, gfx->state.ENABLE_MAP_NORMAL);
    gX_Shader_SetValue_Int(shader->shader,shader->locations.enableMapParallaxLoc, gfx->state.ENABLE_MAP_PARALLAX);
    
    gX_Shader_SetValue_Int(shader->shader,shader->locations.enableGouraudLoc, gfx->state.ENABLE_GOURAUD);
    //ai: Flat is a normal mode, not a lighting model — keep the Phong term
    //    active so Flat does not silently collapse into Unlit.
    gX_Shader_SetValue_Int(shader->shader,shader->locations.enablePhongLoc,
        gfx->state.ENABLE_PHONG
            || (gfx->state.ENABLE_FLAT && !gfx->state.ENABLE_GOURAUD));
    if (shader->locations.enableFlatLoc >= 0)
        gX_Shader_SetValue_Int(shader->shader,shader->locations.enableFlatLoc, gfx->state.ENABLE_FLAT);

    gX_Shader_SetValue_Int(shader->shader,shader->locations.enableFogLoc,   gfx->state.ENABLE_FOG);
    gX_Shader_SetValue_Int(shader->shader,shader->locations.enableShadowsLoc,   gfx->state.ENABLE_SHADOWS);
    gX_Shader_SetValue_Int(shader->shader,shader->locations.enableReflectionsLoc,   gfx->state.ENABLE_REFLECTIONS);
    //opencode: SSAO gate — when ENABLE_SSAO is on and the ssaoBlurMap is loaded,
    //   multiply phong_ambient() by the SSAO factor (single texture sample).
    if (shader->locations.enableSSAOLoc >= 0)
        gX_Shader_SetValue_Int(shader->shader, shader->locations.enableSSAOLoc,
            gfx->state.ENABLE_SSAO && gfx->ssao.blurMap.wrapper.loadingState.loaded ? 1 : 0);
    //opencode: bind the temporally accumulated GTAO when available
    //   (converged oracle rotation); otherwise the spatial blur map.
    //   Raylib's SetShaderValueTextureSlot only supports units 0-11
    //   (rlSetTexture), so the unit-12 bind silently failed and the sampler
    //   read texture 0 (white) → SSAO was a no-op. Raw GL bind + sampler
    //   set instead.
    if (gfx->ssao.blurMap.wrapper.loadingState.loaded && shader->locations.ssaoMapLoc >= 0) {
        gX_Texture aoTex = (gfx->ssao.gtaoFinalTex.id != 0)
            ? gfx->ssao.gtaoFinalTex : gfx->ssao.blurMap.wrapper.rt.texture;
        gX_RHI_Tex_BindOnUnit(aoTex.id, gX_GFX_TEXUNIT_SSAO);
        gX_RHI_UniformInt(shader->locations.ssaoMapLoc, gX_GFX_TEXUNIT_SSAO);
    }
    //opencode: contact shadows (spike 2) - separate APPLY hook so gen and
    //   use A/B independently (GX_T_CONTACT runs the march; APPLY consumes
    //   it in the PBR sun term). Default off = bit-identical forward.
    {
        const gX_I8 *applyHook = getenv("GX_T_CONTACTAPPLY");
        gX_I1 applyOn = (applyHook && applyHook[0] != '\0' && applyHook[0] != '0'
            && gfx->contact.map.wrapper.loadingState.loaded) ? 1 : 0;
        if (shader->locations.enableContactLoc >= 0)
            gX_Shader_SetValue_Int(shader->shader, shader->locations.enableContactLoc, applyOn);
        if (applyOn && shader->locations.contactMapLoc >= 0) {
            gX_RHI_Tex_BindOnUnit(gfx->contact.map.wrapper.rt.texture.id, gX_GFX_TEXUNIT_CONTACT);
            gX_RHI_UniformInt(shader->locations.contactMapLoc, gX_GFX_TEXUNIT_CONTACT);
        }
    }

    //opencode: configurable shader tuning constant uploads
    {
        gX_RendererGFX_ShaderLocs *l = &shader->locations;
        if (l->shadowBiasMaxLoc >= 0)
            gX_Shader_SetValue_Float(shader->shader, l->shadowBiasMaxLoc, gfx->state.shadowBiasMax);
        if (l->shadowBiasMinLoc >= 0)
            gX_Shader_SetValue_Float(shader->shader, l->shadowBiasMinLoc, gfx->state.shadowBiasMin);
        if (l->parallaxMinLayersLoc >= 0)
            gX_Shader_SetValue_Float(shader->shader, l->parallaxMinLayersLoc, gfx->state.parallaxMinLayers);
        if (l->parallaxMaxLayersLoc >= 0)
            gX_Shader_SetValue_Float(shader->shader, l->parallaxMaxLayersLoc, gfx->state.parallaxMaxLayers);
        if (l->parallaxOffsetLoc >= 0)
            gX_Shader_SetValue_Float(shader->shader, l->parallaxOffsetLoc, gfx->state.parallaxOffset);
        if (l->parallaxShadowOffsetLoc >= 0)
            gX_Shader_SetValue_Float(shader->shader, l->parallaxShadowOffsetLoc, gfx->state.parallaxShadowOffset);
        if (l->parallaxShadowStepsLoc >= 0)
            gX_Shader_SetValue_Float(shader->shader, l->parallaxShadowStepsLoc, gfx->state.parallaxShadowSteps);
        if (l->pointShadowBiasLoc >= 0)
            gX_Shader_SetValue_Float(shader->shader, l->pointShadowBiasLoc, gfx->state.pointShadowBias);
        if (l->pointShadowExtraLoc >= 0)
            gX_Shader_SetValue_Float(shader->shader, l->pointShadowExtraLoc, gfx->state.pointShadowExtra);
        if (l->pointShadowMinBiasLoc >= 0)
            gX_Shader_SetValue_Float(shader->shader, l->pointShadowMinBiasLoc, gfx->state.pointShadowMinBias);
        if (l->tangentLengthThresholdLoc >= 0)
            gX_Shader_SetValue_Float(shader->shader, l->tangentLengthThresholdLoc, gfx->state.tangentLengthThreshold);
        if (l->normalYThresholdLoc >= 0)
            gX_Shader_SetValue_Float(shader->shader, l->normalYThresholdLoc, gfx->state.normalYThreshold);
    }
    
    gX_Shader_SetValue_Int(shader->shader,shader->locations.showPositionLoc,   gfx->state.SHOW_POSITION);
    gX_Shader_SetValue_Int(shader->shader,shader->locations.showTexCoordLoc,   gfx->state.SHOW_TEXCOORD);
    gX_Shader_SetValue_Int(shader->shader,shader->locations.showTexCoord2Loc,   gfx->state.SHOW_TEXCOORD2);
    gX_Shader_SetValue_Int(shader->shader,shader->locations.showNormalLoc,   gfx->state.SHOW_NORMAL);
    gX_Shader_SetValue_Int(shader->shader,shader->locations.showColorLoc,   gfx->state.SHOW_COLOR);
    gX_Shader_SetValue_Int(shader->shader,shader->locations.showTangentLoc,   gfx->state.SHOW_TANGENT);
    gX_Shader_SetValue_Int(shader->shader,shader->locations.diagUniformModeLoc, gfx->state.DIAG_UNIFORM_MODE);
    
    //fog
    gX_Shader_SetValue_Float(shader->shader,shader->locations.fogNearLoc,   gfx->fog.near);
    gX_Shader_SetValue_Float(shader->shader,shader->locations.fogFarLoc,    gfx->fog.far);
    //opencode: upload the fog's own color (configurable via schema) — was uploading
    //   the sky color, making gfx->fog.color dead config.
    gX_Shader_SetValue_Color(shader->shader,shader->locations.fogColorLoc,  gfx->fog.color);
    gX_Shader_SetValue_Float(shader->shader,shader->locations.heightFogDensityLoc, gfx->fog.heightDensity);
    gX_Shader_SetValue_Float(shader->shader,shader->locations.heightFogBaseLoc,    gfx->fog.heightBase);

    //opencode: detail texture — force 0 when disabled (shader gates on detailStrength>0)
    gX_Shader_SetValue_Float(shader->shader,shader->locations.detailUVScaleLoc,   gfx->detail.uvScale);
    gX_Shader_SetValue_Float(shader->shader,shader->locations.detailStrengthLoc,
        gfx->state.ENABLE_DETAIL ? gfx->detail.strength : 0.0f);
    gX_Shader_SetValue_Float(shader->shader,shader->locations.detailDistFadeLoc,  gfx->detail.distFade);
    if (gfx->state.ENABLE_DETAIL && gfx->detail.map.id > 0) {
        gX_Shader_SetValue_TextureSlot(shader->shader, shader->locations.detailMapLoc, gfx->detail.map, gX_GFX_TEXUNIT_DETAIL);
    }
    
    //ambient
    { //opencode: debug ambient location
        static gX_I1 logged = false;
        if (!logged) {
            logged = true;
            gX_TraceLog(gX_LOG_INFO, "RHI_DBG: ambientColorLoc=%d ambientFactorLoc=%d albedoFactorLoc=%d sky.strength=%.3f sky.color=(%d,%d,%d)",
                shader->locations.ambientColorLoc, shader->locations.ambientFactorLoc,
                shader->locations.albedoFactorLoc,
                (double)gfx->sky.strength,
                gfx->sky.color.r, gfx->sky.color.g, gfx->sky.color.b);
        }
    }
    gX_Shader_SetValue_Color(shader->shader,shader->locations.ambientColorLoc,  gfx->sky.color);
    gX_Shader_SetValue_Float(shader->shader,shader->locations.ambientFactorLoc, gfx->sky.strength);

    //opencode: reflection bias
    if (shader->locations.reflectionBiasLoc >= 0)
        gX_Shader_SetValue_Float(shader->shader, shader->locations.reflectionBiasLoc, gfx->reflection.bias);

    //sun
    gX_Shader_SetValue_Color(shader->shader,shader->locations.sunColorLoc,          gfx->sun.color);
    gX_Shader_SetValue_Float(shader->shader,shader->locations.sunBrightnessLoc,     gfx->sun.brightness);
    gX_Shader_SetValue_Float(shader->shader,shader->locations.exposureLoc,          gfx->state.exposure);
    gX_Shader_SetValue_Color(shader->shader,shader->locations.sunDiffuseTintLoc,    gfx->sun.diffuseTint);
    gX_Shader_SetValue_Color(shader->shader,shader->locations.sunSpecularTintLoc,   gfx->sun.specularTint);
    gX_Shader_SetValue_Float(shader->shader,shader->locations.sunDiffuseFactorLoc,  gfx->sun.diffuseFactor);
    gX_Shader_SetValue_Float(shader->shader,shader->locations.sunSpecularFactorLoc, gfx->sun.specularFactor);

    //opencode: rim lighting — force 0 when disabled (shader gates on rimIntensity>0.001)
    gX_Shader_SetValue_Color(shader->shader,shader->locations.rimColorLoc,      gfx->state.rimColor);
    gX_Shader_SetValue_Float(shader->shader,shader->locations.rimIntensityLoc,
        gfx->state.ENABLE_RIM ? gfx->state.rimIntensity : 0.0f);
    gX_Shader_SetValue_Float(shader->shader,shader->locations.rimPowerLoc,      gfx->state.rimPower);
    //opencode: environment ambient — force 0 when disabled (shader gates on envAmbientFactor>0)
    gX_Shader_SetValue_Float(shader->shader,shader->locations.envAmbientFactorLoc,
        gfx->state.ENABLE_ENV_AMBIENT ? gfx->state.envAmbientFactor : 0.0f);
    
    //lights
    //opencode: light slot span = highest USED heap index + 1 (not the used
    //   count). Heap removals (editor, GLB unload, RemoveAll + re-add) leave
    //   holes; every other light-indexed upload (farPlanes, ranges, shadow
    //   layers, cookie rects/flags) already uses heap order, and the shader
    //   samples shadow layer i*6 — so the property arrays must be heap
    //   indexed too or packed/uniform indices diverge after any hole.
    gX_U32 usedLightCount = 0;
    {
        for (gX_U32 li = 0; li < gfx->lights.amount; li++) {
            if (gfx->lights.meta[li].USED) usedLightCount = li + 1;
        }
    }
    //opencode: v0.5.0 — clamp the uniform-path count to the fixed [32] arrays
    //   (the SSBO path below carries up to gX_GFX_MAX_LIGHTS_SSBO instead);
    //   without the clamp, >32 lights made the GLSL loop read OOB-uniforms.
#ifndef GX_COMPUTE
    if (usedLightCount > gX_GFX_MAX_LIGHTS) usedLightCount = gX_GFX_MAX_LIGHTS;
#else
    //opencode: SSBO carries 64 — clamp the span so the frag loop (raised
    //   to 64 below) never reads past the buffer when the heap overflows it.
    if (usedLightCount > gX_GFX_MAX_LIGHTS_SSBO) usedLightCount = gX_GFX_MAX_LIGHTS_SSBO;
#endif
    gX_Shader_SetValue_Int(shader->shader,shader->locations.lightsAmountLoc, (gX_I32)usedLightCount);

    //opencode: v0.3.2 — Forward+ tile grid bind + enable flag
    if (gfx->state.ENABLE_CLUSTERED && gfx->clusters.inited) {
        gX_Texture gridTex = {0}; gridTex.id = gfx->clusters.gridTex;
        gX_Shader_SetValue_TextureSlot(shader->shader,
            gX_Shader_GetLocation(shader->shader, "lightGridTex"), gridTex, 29);
        gX_Shader_SetValue_Int(shader->shader,
            gX_Shader_GetLocation(shader->shader, "clusteredEnabled"),
            gfx->state.ENABLE_CLUSTERED ? 1 : 0);
    } else {
        gX_Shader_SetValue_Int(shader->shader,
            gX_Shader_GetLocation(shader->shader, "clusteredEnabled"), 0);
    }
    
    //opencode: upload per-light uniforms — skip if shader has no lights array
    if (shader->locations.lightsAmountLoc >= 0) {
#ifdef GX_COMPUTE
        //opencode: v0.5.0 — SSBO light data (GX_COMPUTE build). 7x vec4 per
        //   light, std430 (no padding): pos+type, dir+fov, color, atten+
        //   brightness, diff+spec. Heap order == uniform-array order, so the
        //   per-tile mask slot maps to the same light both paths. Cookies +
        //   point-shadow arrays stay uniform-capped at 32 (their samplers are
        //   fixed-size arrays in the shader).
        //opencode: NOTE — heap-indexed SSBO writes (not dense-packed): the
        //   per-tile cluster mask addresses lights by HEAP slot, and
        //   farPlanes/layers/cookies are heap-ordered, so a dense pack
        //   misaligns every light after the first heap hole (editor/GLB
        //   removal, RemoveAll + re-add) — the same hole bug fixed above
        //   for the uniform path. Holes stay zero-filled (type 0 inert).
        gX_F32 lightData[gX_GFX_MAX_LIGHTS_SSBO * 28] = {0};
        gX_U32 ssboCount = 0;
        GX_HEAP_FOREACH(gfx->lights, gX_RendererGFX_Light, light) {
            gX_U32 slot = (gX_U32)(light - (gX_RendererGFX_Light*)gfx->lights.data);
            if (slot >= gX_GFX_MAX_LIGHTS_SSBO) continue;
            if (slot + 1 > ssboCount) ssboCount = slot + 1;
            gX_F32 *d = &lightData[slot * 28];
            d[0] = light->pos.x;  d[1] = light->pos.y;  d[2] = light->pos.z;
            d[3] = (gX_F32)light->type;
            d[4] = light->dir.x;  d[5] = light->dir.y;  d[6] = light->dir.z;
            d[7] = light->fov;
            d[8]  = (gX_F32)light->color.r / 255.0f;
            d[9]  = (gX_F32)light->color.g / 255.0f;
            d[10] = (gX_F32)light->color.b / 255.0f;
            d[11] = (gX_F32)light->color.a / 255.0f;
            d[12] = light->attenConst;  d[13] = light->attenLinear;
            d[14] = light->attenQuad;   d[15] = light->brightness;
            d[16] = light->diffuseFactor; d[17] = light->specularFactor;
            d[18] = (gX_F32)((light->sun ? 1 : 0) + (light->castShadows ? 2 : 0) +
                (light->cookieEnabled ? 4 : 0));
            d[19] = light->shadowRange;
            //opencode: far plane for the shared point-shadow 2D array; 0 = no
            //   cubemap -> shader skips sampling (mirrors the uniform path).
            //   (gX_GFX_MAX_LIGHTS_SSBO * 24: 6 vec4s per light, std430)
            d[20] = 0.0f;
            if (gfx->state.ENABLE_POINT_SHADOWS &&
                light->castShadows && light->shadowDepth.id > 0)
                d[20] = light->shadowFar;

            d[21] = 0.0f; d[22] = 0.0f; d[23] = 0.0f;
            //opencode: cookie atlas rect (vec4) — mirrors the uniform upload;
            //   (gX_GFX_MAX_LIGHTS_SSBO * 28: 7 vec4s per light, std430)
            {
                gX_Rectangle cr = gfx->common.cookieRects[slot];
                d[24] = cr.x; d[25] = cr.y; d[26] = cr.width; d[27] = cr.height;

            }
        }
        if (!gfx->clusters.lightSsbo) {
            gfx->clusters.lightSsboCapacity = gX_GFX_MAX_LIGHTS_SSBO * 28 * 4;
            gfx->clusters.lightSsbo = rlLoadShaderBuffer(
                gfx->clusters.lightSsboCapacity, NULL, RL_DYNAMIC_COPY);
        }
        rlUpdateShaderBuffer(gfx->clusters.lightSsbo, lightData, ssboCount * 28 * 4, 0);
        rlBindShaderBuffer(gfx->clusters.lightSsbo, 0);
#else
        gX_I32 types[gX_GFX_MAX_LIGHTS] = {0};
        gX_F32 fovs[gX_GFX_MAX_LIGHTS] = {0};
        gX_F32 brights[gX_GFX_MAX_LIGHTS] = {0};
        gX_F32 attenConsts[gX_GFX_MAX_LIGHTS] = {0};
        gX_F32 attenLinears[gX_GFX_MAX_LIGHTS] = {0};
        gX_F32 attenQuads[gX_GFX_MAX_LIGHTS] = {0};
        gX_F32 diffFactors[gX_GFX_MAX_LIGHTS] = {0};
        gX_F32 specFactors[gX_GFX_MAX_LIGHTS] = {0};
        gX_F32v3 poss[gX_GFX_MAX_LIGHTS] = {{0}};
        gX_F32v3 dirs[gX_GFX_MAX_LIGHTS] = {{0}};
        gX_F32v4 cols[gX_GFX_MAX_LIGHTS] = {{0}};
        //opencode: heap-indexed writes (not packed): shader index i must
        //   equal the heap index so farPlanes/ranges (heap loop above),
        //   shadow layers (heap lightIdx*6) and cookie rects/flags (heap)
        //   all address the same light. Holes stay zero-filled (arrays init
        //   to 0 above); the fragment shader skips zero attenuation before
        //   dividing, so a hole cannot poison a valid later slot with inf*0.
        {
            gX_RendererGFX_Light *lightBase = (gX_RendererGFX_Light*)gfx->lights.data;
            for (gX_U32 li = 0; li < gfx->lights.amount && li < gX_GFX_MAX_LIGHTS; li++) {
                if (!gfx->lights.meta[li].USED) continue;
                gX_RendererGFX_Light *light = &lightBase[li];
                types[li] = light->type;
                fovs[li] = light->fov;
                brights[li] = light->brightness;
                attenConsts[li] = light->attenConst;
                attenLinears[li] = light->attenLinear;
                attenQuads[li] = light->attenQuad;
                diffFactors[li] = light->diffuseFactor;
                specFactors[li] = light->specularFactor;
                poss[li] = light->pos;
                dirs[li] = light->dir;
                cols[li].x = (gX_F32)light->color.r / 255.0f;
                cols[li].y = (gX_F32)light->color.g / 255.0f;
                cols[li].z = (gX_F32)light->color.b / 255.0f;
                cols[li].w = (gX_F32)light->color.a / 255.0f;
            }
        }
        gX_Shader_SetValue_IntArray(shader->shader, shader->locations.lightsTypeLoc, types, gX_GFX_MAX_LIGHTS);
        gX_Shader_SetValue_FloatArray(shader->shader, shader->locations.lightsFovLoc, fovs, gX_GFX_MAX_LIGHTS);
        gX_Shader_SetValue_FloatArray(shader->shader, shader->locations.lightsBrightnessLoc, brights, gX_GFX_MAX_LIGHTS);
        gX_Shader_SetValue_FloatArray(shader->shader, shader->locations.lightsAttenConstLoc, attenConsts, gX_GFX_MAX_LIGHTS);
        gX_Shader_SetValue_FloatArray(shader->shader, shader->locations.lightsAttenLinearLoc, attenLinears, gX_GFX_MAX_LIGHTS);
        gX_Shader_SetValue_FloatArray(shader->shader, shader->locations.lightsAttenQuadLoc, attenQuads, gX_GFX_MAX_LIGHTS);
        gX_Shader_SetValue_FloatArray(shader->shader, shader->locations.lightsDiffuseFactorLoc, diffFactors, gX_GFX_MAX_LIGHTS);
        gX_Shader_SetValue_FloatArray(shader->shader, shader->locations.lightsSpecularFactorLoc, specFactors, gX_GFX_MAX_LIGHTS);
        gX_Shader_SetValue_V3Array(shader->shader, shader->locations.lightsPosLoc, poss, gX_GFX_MAX_LIGHTS);
        gX_Shader_SetValue_V3Array(shader->shader, shader->locations.lightsDirLoc, dirs, gX_GFX_MAX_LIGHTS);
        gX_Shader_SetValue_V4Array(shader->shader, shader->locations.lightsColorLoc, cols, gX_GFX_MAX_LIGHTS);
#endif //GX_COMPUTE

        //opencode: light cookies — pack all enabled cookies into a shared atlas
        //   (4x4 grid of 256px cells) and bind it once; every spot light gets
        //   its own cookie via per-light UV rects. Single-sampler bind (atlas)
        //   stays under GL_MAX_FRAGMENT_IMAGE_UNIFORMS.
        if (gfx->state.ENABLE_COOKIES && shader->locations.lightsCookieAtlasLoc >= 0) {
            gX_I32 cookieEnabledFlags[gX_GFX_MAX_LIGHTS];
            gX_U32 boundCookieUnit = gX_GFX_TEXUNIT_COOKIE;
            gX_RendererGFX_Light *lptr = gfx->lights.data;

            //opencode: rebuild the atlas when any cookie changed (lazy, once)
            if (gfx->common.cookieAtlasDirty) {
                gX_RendererGFX_CookieAtlas_Rebuild(gfx);
            }

            for (gX_U32 ci = 0; ci < gfx->lights.amount && ci < gX_GFX_MAX_LIGHTS; ci++) {
                cookieEnabledFlags[ci] = (gfx->lights.meta[ci].USED && lptr[ci].cookieEnabled) ? 1 : 0;

            }
            //opencode: bind atlas texture on dedicated unit — gX_RHI_Tex_BindOnUnit handles
            //   activeTexture save/restore internally
            gX_RHI_Tex_BindOnUnit(gfx->common.cookieAtlasTex, boundCookieUnit);
            gX_RHI_UniformInt(shader->locations.lightsCookieAtlasLoc, (gX_I32)boundCookieUnit);
            if (shader->locations.lightsCookieRectsLoc >= 0) {
                //opencode: v0.5.2 FIX — vec4 array must go through glUniform4fv;
                //   glUniform1fv (FloatArray) is a type mismatch the driver
                //   rejects silently — rects stayed zero -> frag sampled the
                //   atlas corner (white) -> no cookie effect in the uniform path.
                gX_RHI_UniformVec4(shader->locations.lightsCookieRectsLoc,
                    gX_GFX_MAX_LIGHTS, (const gX_F32*)gfx->common.cookieRects);
            }
            if (shader->locations.lightsCookieEnabledLoc >= 0) {
                gX_RHI_Uniform_IntArray(shader->locations.lightsCookieEnabledLoc, gX_GFX_MAX_LIGHTS, cookieEnabledFlags);
            }
        }
    } // end if (lightsAmountLoc >= 0)
}

//opencode: build the #define string for the given shader quality level.
//   Injected into vertex+fragment shaders at compile time. Shaders can use
//   #if QUALITY >= N to gate expensive features (PBR, IBL, parallax, etc.).
//opencode: rebuild the cookie atlas — packs every enabled light's cookie into
//   an 8x8 grid (256px cells). The wrapper keeps no CPU copy of loaded textures,
//   so each light stores its cookiePath and the atlas reloads the image on
//   rebuild (lazy, only when cookieAtlasDirty is set). Empty slots stay white.
#define GX_COOKIE_ATLAS_CELLS 8
#define GX_COOKIE_ATLAS_CELL  256

//ai: cookie uploads require validated cell bounds and owned RGBA8 data; invalid images leave the initialized white cell, never resized or reinterpreted.
gX_I0 gX_RendererGFX_CookieAtlas_Rebuild(gX_RendererGFX *gfx) {
    const gX_U32 atlasSize = GX_COOKIE_ATLAS_CELLS * GX_COOKIE_ATLAS_CELL;
    if (gfx->common.cookieAtlasTex == 0) {
        gfx->common.cookieAtlasTex = gX_RHI_Tex_Create2D(atlasSize, atlasSize,
            gX_RHI_RGBA, gX_RHI_RGBA, gX_RHI_UNSIGNED_BYTE, NULL, "cookie_atlas");
        gX_RHI_Tex_Param(gfx->common.cookieAtlasTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_LINEAR);
        gX_RHI_Tex_Param(gfx->common.cookieAtlasTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_LINEAR);
        gX_RHI_Tex_Param(gfx->common.cookieAtlasTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_EDGE);
        gX_RHI_Tex_Param(gfx->common.cookieAtlasTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_EDGE);
    }
    //opencode: gX_Calloc takes BYTES and returns gX_I0* — pass the raw byte
    //   count, never divide by 8. The old (bytes+7)/8 math allocated 512KB and
    //   overflowed the 4MB white fill → heap corruption on first cookie enable.
    gX_I0 *whiteUnits = gX_Calloc(atlasSize * atlasSize * 4);
    gX_U8 *whiteCell = (gX_U8*)whiteUnits;
    if (whiteCell) {
        for (gX_U32 i = 0; i < atlasSize * atlasSize * 4; i += 4)
            whiteCell[i] = whiteCell[i+1] = whiteCell[i+2] = whiteCell[i+3] = 255;
        gX_RHI_Tex_Upload(gfx->common.cookieAtlasTex, gX_RHI_TEXTURE_2D, 0, 0, 0,
            atlasSize, atlasSize, gX_RHI_RGBA, gX_RHI_UNSIGNED_BYTE, whiteCell);
        gX_Free(whiteUnits);
    }
    gX_F32 inv = 1.0f / (gX_F32)atlasSize;
#ifdef GX_COMPUTE
    const gX_U32 cookieMax = gX_GFX_MAX_LIGHTS_SSBO;
#else
    const gX_U32 cookieMax = gX_GFX_MAX_LIGHTS;
#endif
    for (gX_U32 ci = 0; ci < gfx->lights.amount && ci < cookieMax
        && ci < GX_COOKIE_ATLAS_CELLS * GX_COOKIE_ATLAS_CELLS; ci++) {
        gX_RendererGFX_Light *l = &((gX_RendererGFX_Light*)gfx->lights.data)[ci];
        gX_U32 col = ci % GX_COOKIE_ATLAS_CELLS;
        gX_U32 row = ci / GX_COOKIE_ATLAS_CELLS;
        gfx->common.cookieRects[ci] = (gX_Rectangle){
            (gX_F32)col * GX_COOKIE_ATLAS_CELL * inv,
            (gX_F32)row * GX_COOKIE_ATLAS_CELL * inv,
            (gX_F32)GX_COOKIE_ATLAS_CELL * inv,
            (gX_F32)GX_COOKIE_ATLAS_CELL * inv};
        if (gfx->lights.meta[ci].USED && l->cookieEnabled && l->cookiePath[0]) {
            gX_Image img = gX_Image_Load(l->cookiePath);
            if (!img.data || img.width <= 0 || img.height <= 0
                || img.width > GX_COOKIE_ATLAS_CELL || img.height > GX_COOKIE_ATLAS_CELL
                || img.format < gX_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE
                || img.format >= gX_PIXELFORMAT_COMPRESSED_DXT1_RGB)
            {
                gX_TraceLog_GFX(gX_LOG_WARNING, "COOKIE_ATLAS: invalid image %s (%dx%d format=%d); keeping white cell",
                    l->cookiePath, img.width, img.height, img.format);
            }
            else
            {
                if (img.format != gX_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
                    gX_Image_Format(&img, gX_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
                if (img.data && img.format == gX_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
                    && img.width > 0 && img.height > 0
                    && img.width <= GX_COOKIE_ATLAS_CELL && img.height <= GX_COOKIE_ATLAS_CELL)
                {
                    gX_RHI_Tex_Upload(gfx->common.cookieAtlasTex, gX_RHI_TEXTURE_2D, 0,
                        (gX_I32)(col * GX_COOKIE_ATLAS_CELL), (gX_I32)(row * GX_COOKIE_ATLAS_CELL),
                        (gX_U32)img.width, (gX_U32)img.height, gX_RHI_RGBA, gX_RHI_UNSIGNED_BYTE, img.data);
                }
                else
                {
                    gX_TraceLog_GFX(gX_LOG_WARNING, "COOKIE_ATLAS: RGBA8 conversion failed for %s; keeping white cell",
                        l->cookiePath);
                }
            }
            gX_Image_Unload(img);
        }
    }
    gfx->common.cookieAtlasDirty = 0;
}

//opencode: delete the cookie atlas texture (unload path)
gX_I0 gX_RendererGFX_CookieAtlas_Delete(gX_RendererGFX *gfx) {
    if (gfx->common.cookieAtlasTex > 0) {
        gX_RHI_Tex_Delete(gfx->common.cookieAtlasTex);
        gfx->common.cookieAtlasTex = 0;
    }
    gfx->common.cookieAtlasDirty = 1;
}

static gX_I0 gX_BuildShaderDefines(gX_RendererGFX_Shader *shader, gX_RendererGFX *gfx)
{
    shader->defines[0] = '\0';
    gX_I32 q = gfx->state.shaderQuality;
    if (q < 0) q = 0;
    if (q > 2) q = 2;
    //opencode: GX_GLFW_FRAG=1 forces a simplified Phong-only path (avoids unbound
    //   sampler NaN issues on NVIDIA compat profile). On GL 4.0+ the full shader
    //   with sampler arrays (cookies, point shadows) compiles and works correctly.
    //   On GLFW with GL 4.3 Core Profile, GX_GLFW_FRAG=0 to enable cookies + PBR.
    gX_I1 useGXFrag = 0;
    snprintf(shader->defines, sizeof(shader->defines),
        "#define QUALITY %d\n#define GX_GLFW_FRAG %d\n%s%s", q, useGXFrag,
#ifdef GX_COMPUTE
        "#define GX_COMPUTE_SSBO 1\n",
#else
        "",
#endif
        gX_GL_ShaderDefines());
}

gX_I0 gX_RendererGFX_Shader_Update(gX_RendererGFX_Shader *shader, gX_RendererGFX *gfx) {
    //opencode: sync quality defines before loading
    gX_BuildShaderDefines(shader, gfx);
    if (shader->loadAsap && maindata.backendLoaded && !(shader->loaded)) {
        gX_RendererGFX_Shader_Load(shader);
        //opencode: register as raw program so SetValue dispatches through GL directly
        //   instead of raylib's SetShaderValue (which treats locations as RL_SHADER_LOC_*
        //   indices and misroutes custom uniforms like "focalDistance", "depthMap", etc.)
        if (shader->shader.id > 0) gX_GL_RawRegister(shader->shader.id);
    }
    //opencode: check each shader once per frame. Later same-frame callers must
    //   not re-check: a mid-frame program swap strands earlier uniform uploads
    //   on the dead program while draws run on the new one.
    gX_I1 doReloadCheck = (maindata.frameCount != shader->reloadCheckedFrame);
    if (doReloadCheck && maindata.backendLoaded && shader->loaded && shader->shader.id > 0) {
        shader->reloadCheckedFrame = maindata.frameCount;
        gX_U32 prevId = shader->shader.id;
        gX_Shader_Compiler_CheckReload(&shader->shader);
        //opencode: if hot-reload created a new program, re-register it for raw GL dispatch
        //   CheckReload swaps shader->id internally but does NOT call RawRegister.
        if (shader->shader.id != prevId && shader->shader.id > 0) {
            gX_GL_RawUnregister(prevId);
            gX_GL_RawRegister(shader->shader.id);
        }
    }
    //opencode: defer reload to next frame boundary to avoid use-after-free during draw
    if (shader->reload && !shader->reloadDeferred) {
        shader->reloadDeferred = true;
        shader->reload = false;
    }
    if (shader->reloadDeferred) {
        gX_RendererGFX_Shader_Unload(shader);
        gX_RendererGFX_Shader_Load(shader);
        shader->reloadDeferred = false;
    }
    
    gX_RendererGFX_Shader_SetValues(shader,gfx);
}
