/**********************************************************************************************
*
*   [gX: src/renderer/gfx/shader.h]
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


#ifndef RENDERER_GFX_SHADER_H
#define RENDERER_GFX_SHADER_H

#include "../../common/types.h"
#include "../../common/text.h"

typedef struct gX_RendererGFX_ShaderLocs {
    gX_I32  lightSpaceLoc;
    gX_I32  lightPosLoc;
    gX_I32  viewPosLoc;
    gX_I32  nearPlaneLoc;
    gX_I32  farPlaneLoc;
    
    gX_I32  resolutionLoc;
    gX_I32  cameraPositionLoc;
    gX_I32  cameraTargetLoc;
    gX_I32  cameraUpLoc;
    gX_I32  cameraFovLoc;
    
    
    //flags
    gX_I32  enableLightingAmbientLoc;
    gX_I32  enableLightingDiffuseLoc;
    gX_I32  enableLightingSpecularLoc;
    
    gX_I32  enableMapAlbedoLoc;
    gX_I32  enableMapMetalnessLoc;
    gX_I32  enableMapNormalLoc;
    gX_I32  enableMapParallaxLoc;
    //opencode: parallax quality (0=simple, 1=steep, 2=steep+self-shadow).
    //   Per-model in standard.frag, gated on hasHeightMap.
    gX_I32  parallaxModeLoc;
    
    gX_I32  enableGouraudLoc;
    gX_I32  enablePhongLoc;

    //opencode: per-model PBR toggle — 0 = Blinn-Phong (uses shininess),
    //   1 = Cook-Torrance + IBL (uses metallic/roughness/ao). Standard.frag
    //   branches between the two shading paths based on this uniform.
    gX_I32  usePBRLoc;
    //opencode: Paper 1 M2 — opt-in Lambert parity fixture gate. Production
    //   draws upload zero; the oracle fixture suppresses IBL/specular only.
    gX_I32  oracleDiffuseOnlyLoc;
    //opencode: Paper 1 M3 — opt-in direct GGX parity fixture gate.
    gX_I32  oracleGgxLoc;
    //opencode: Paper 1 M4 — opt-in one-secondary environment path gate.
    gX_I32  oracleBounceLoc;

    gX_I32  enableFogLoc;
    gX_I32  enableShadowsLoc;
    gX_I32  enableReflectionsLoc;
    //ai: per-model environment mixing opt-in.
    gX_I32 balancedReflectionLoc;
    //opencode: per-model screen-space-reflection receiver flag.
    gX_I32  receiveSSRLoc;
    //opencode: SSAO toggle — enables the ssaoMap sample in phong_ambient()
    gX_I32  enableSSAOLoc;

    gX_I32  showPositionLoc;
    gX_I32  showTexCoordLoc;
    gX_I32  showTexCoord2Loc;
    gX_I32  showNormalLoc;
    gX_I32  showColorLoc;
    gX_I32  showTangentLoc;
    gX_I32  diagUniformModeLoc;
    //ai: per-material raw base-color-map diagnostic selector.
    gX_I32  debugBaseColorMapLoc;
    //ai: per-primitive opt-in terrain state, reset on every material bind.
    gX_I32 terrainBlendLoc;
    gX_I32 terrainAlbedoMapLoc;
    gX_I32 terrainDetailMapLoc;
    gX_I32 terrainHasDetailLoc;
    gX_I32 terrainDetailScaleLoc;
    
    //maps
    gX_I32  hasAlbedoMapLoc;
    gX_I32  hasMetalnessMapLoc;
    gX_I32  hasNormalMapLoc;
    gX_I32  hasHeightMapLoc;
    gX_I32  cubeMapLoc;
    gX_I32  shadowMapLoc;
    //opencode: depthMapLoc — unused in main shader (standard.frag has no depthMap),
    //   but kept because depthVisualizer shader uses the same locations struct
    //   and the visualizer needs depthMapLoc for its sampler binding.
    gX_I32  depthMapLoc;
    
    gX_I32  albedoMapScaleLoc;
    gX_I32  albedoMapOffsetLoc;
    gX_I32  albedoMapFactorLoc;
    
    gX_I32  metalnessMapScaleLoc;
    gX_I32  metalnessMapOffsetLoc;
    gX_I32  metalnessMapFactorLoc;
    
    gX_I32  normalMapScaleLoc;
    gX_I32  normalMapOffsetLoc;
    gX_I32  normalMapFactorLoc;
    
    gX_I32  heightMapScaleLoc;
    gX_I32  heightMapOffsetLoc;
    gX_I32  heightMapFactorLoc;

    //opencode: static lightmap sampled with the mesh's second UV channel.
    gX_I32  hasLightmapLoc;
    gX_I32  lightmapMapLoc;
    gX_I32  lightmapStrengthLoc;
    gX_I32  lightmapScaleLoc;
    gX_I32  lightmapOffsetLoc;
    
    //fog
    gX_I32  fogNearLoc;
    gX_I32  fogFarLoc;
    gX_I32  fogColorLoc;
    gX_I32  heightFogDensityLoc;
    gX_I32  heightFogBaseLoc;
    
    //ambient
    gX_I32  ambientColorLoc;
    gX_I32  ambientFactorLoc;
    
    //sun
    gX_I32  sunColorLoc;
    gX_I32  sunBrightnessLoc;
    gX_I32  exposureLoc;
    gX_I32  sunDiffuseTintLoc;
    gX_I32  sunSpecularTintLoc;
    gX_I32  sunDiffuseFactorLoc;
    gX_I32  sunSpecularFactorLoc;
    
	//rim lighting
	gX_I32  rimColorLoc;
	gX_I32  rimIntensityLoc;
	gX_I32  rimPowerLoc;

	//environment-aware ambient
	gX_I32  envAmbientFactorLoc;
	
	//lights
    gX_I32  lightsAmountLoc;
    gX_I32  lightsTypeLoc;
    gX_I32  lightsFovLoc;
    gX_I32  lightsBrightnessLoc;
    gX_I32  lightsPosLoc;
    gX_I32  lightsDirLoc;
    gX_I32  lightsColorLoc;
    gX_I32  lightsAttenConstLoc;
    gX_I32  lightsAttenLinearLoc;
    gX_I32  lightsAttenQuadLoc;
    gX_I32  lightsDiffuseFactorLoc;
    gX_I32  lightsSpecularFactorLoc;

    //opencode: light cookies (projected textures for spot lights)
    gX_I32  lightsCookieAtlasLoc;
    gX_I32  lightsCookieRectsLoc;
    gX_I32  lightsCookieEnabledLoc;
    
    //shadow
    gX_I32  shadowModeLoc;
    gX_I32  shadowDiffuseBiasLoc;
    gX_I32  shadowSpecularBiasLoc;
    
    //opencode: point light shadows
    gX_I32  pointShadowMapsLoc;
    gX_I32  pointShadowFarPlanesLoc;
    gX_I32  pointShadowRangesLoc;  //opencode: per-light shadow range (bounding volume)
    
    //model
    gX_I32  albedoFactorLoc;
    gX_I32  metalnessFactorLoc;
    gX_I32  modelShininessLoc;
    gX_I32  albedoColorLoc;
    gX_I32  metalnessColorLoc;
    
    //opencode: specular color tint (Phong: metalnessFactor multiplier, PBR: F0 tint)
    gX_I32  specularColorLoc;
    
    //opencode: PBR roughness/AO
    gX_I32  hasRoughnessMapLoc;
    gX_I32  hasAoMapLoc;
    gX_I32  roughnessLoc;
    gX_I32  aoLoc;
    gX_I32  roughnessMapScaleLoc;
    gX_I32  roughnessMapOffsetLoc;
    gX_I32  roughnessMapFactorLoc;
    gX_I32  aoMapScaleLoc;
    gX_I32  aoMapOffsetLoc;
    gX_I32  aoMapFactorLoc;
    //opencode: emissive
    gX_I32  hasEmissiveMapLoc;
    gX_I32  emissiveFactorLoc;
    gX_I32  emissiveColorLoc;
    //opencode: bypass scene lighting for editor light-marker spheres.
    gX_I32  lightMarkerModeLoc;
    //opencode: emission map scale/offset
    gX_I32  emissiveMapScaleLoc;
    gX_I32  emissiveMapOffsetLoc;
    gX_I32  emissiveMapFactorLoc;
    
    //opencode: alpha test
    gX_I32  enableAlphaTestLoc;
    gX_I32  alphaCutoffLoc;
    
    //opencode: alpha blending
    gX_I32  blendOpacityLoc;
    
    //opencode: flat shading toggle
    gX_I32  enableFlatLoc;
    //ai: per-model normal inversion for inside-out / authored-flipped meshes.
    gX_I32  flipNormalsLoc;

    //opencode: reflection bias multiplier
    gX_I32  reflectionBiasLoc;

    //opencode: gloss map — modulates shininess (Phong) or roughness (PBR)
    gX_I32  hasGlossMapLoc;
    gX_I32  glossMapLoc;

    //opencode: planar reflection
    gX_I32  reflTexLoc;
    gX_I32  reflMVPLoc;
    gX_I32  planarReflectionLoc;
    //opencode: mirror fill renders linear (no tonemap/exposure/SSAO) so the
    //   RT holds display-independent content tonemapped once in main
    gX_I32  mirrorFillLoc;
    //opencode: non-planar reflection (GTA4/5 style per-fragment local plane)
    gX_I32  nonPlanarReflectionLoc;
    gX_I32  reflectionPlaneNormalLoc;
    gX_I32  reflectionPlaneHeightLoc;
    //opencode: procedural wave displacement (vertex shader)
    gX_I32  waveAmplitudeLoc;
    gX_I32  gxTimeLoc;
    
    //opencode: detail texture (HL2/Source style)
    gX_I32  detailMapLoc;
    gX_I32  detailUVScaleLoc;
    gX_I32  detailStrengthLoc;
    gX_I32  detailDistFadeLoc;

    //opencode: parallax-corrected cubemap
    gX_I32  cubePosLoc;
    gX_I32  cubeSizeLoc;
    //opencode: probe-grid blend — second-nearest probe (unit 31)
    gX_I32  cubeMapBLoc;
    gX_I32  cubePosBLoc;
    gX_I32  probeBlendBLoc;
    //opencode: view/projection matrices (manually uploaded — raylib doesn't update
    //   these for custom shaders when camera moves)
    gX_I32  matViewLoc;
    gX_I32  matProjectionLoc;

    //opencode: per-model transforms (uploaded before gX_Model_Draw_Parameters)
    gX_I32  matModelLoc;
    gX_I32  gX_ModelMatrixLoc; // custom model matrix — raylib doesn't overwrite this
    gX_I32  matNormalLoc;
    gX_I32  ssrNormalPassLoc; //opencode: opt-in SSR normal-only draw

    //opencode: GPU skinning — bone matrices + per-model switch
    gX_I32  boneMatricesLoc;
    gX_I32  gX_UseSkinningLoc;

    //opencode: TAA jitter — sub-pixel offset in vertex shader
    gX_I32  taaJitterLoc;

    //opencode: oracle (Paper 1) - per-frame seed rotates PCF taps
    gX_I32  oracleSeedLoc;
    gX_I32  oracleSeedActiveLoc;
    //opencode: oracle estimator - single-light Monte Carlo pick
    gX_I32  oracleEstimateActiveLoc;
    //opencode: oracle multi-ray (Paper 1) - rays per pixel per frame
    gX_I32  oracleRayCountLoc;
    //opencode: Paper 1 M1 - cosine-hemisphere environment transport oracle
    gX_I32  oraclePathActiveLoc;
    //opencode: M1 - transparent roughness-alpha repair pass flag
    gX_I32  roughnessAlphaLoc;

    //opencode: refractive materials
    gX_I32  refractiveIndexLoc;
    gX_I32  enableRefractionLoc;
    gX_I32  bgTexLoc;
    //opencode: SSAO map (the blurred output, sampled in phong_ambient)
    gX_I32  ssaoMapLoc;
    //opencode: contact shadows (spike 2) - short-range sun occlusion
    gX_I32  contactMapLoc;
    gX_I32  enableContactLoc;

    //opencode: IBL (image-based lighting) — split-sum approximation
    gX_I32  irradianceMapLoc;
    gX_I32  prefilterMapLoc;
    gX_I32  fogLutMapLoc;       //opencode: v0.3.8
    gX_I32  hasFogLutLoc;       //opencode: v0.3.8
    gX_I32  brdfLutLoc;

    //opencode: configurable shader tuning constants (category 3)
    gX_I32  shadowBiasMaxLoc;
    gX_I32  shadowBiasMinLoc;
    gX_I32  parallaxMinLayersLoc;
    gX_I32  parallaxMaxLayersLoc;
    gX_I32  parallaxOffsetLoc;
    gX_I32  parallaxShadowOffsetLoc;
    gX_I32  parallaxShadowStepsLoc;
    gX_I32  pointShadowBiasLoc;
    gX_I32  pointShadowExtraLoc;
    gX_I32  pointShadowMinBiasLoc;
    gX_I32  tangentLengthThresholdLoc;
    gX_I32  normalYThresholdLoc;

    //opencode: CSM (cascaded shadow maps) — single sampler2DArray
    gX_I32  csmShadowMapArrayLoc;
    gX_I32  csmVPLoc;        //opencode: mat4 array (csmVP[4])
    gX_I32  csmSplitsLoc;    //opencode: vec3 split distances
    gX_I32  csmBlendScaleLoc;
    gX_I32  cascadeCountLoc;

    //opencode: hardware instancing — per-instance SSBO model matrices
    gX_I32  instanceCountLoc;
    gX_I32  instanceMatricesLoc;
    
} gX_RendererGFX_ShaderLocs;

//opencode: std140 uniform buffer object layout (binding 0 — per-frame lighting)
// std140 vec3 = 12 bytes, next member at 16-byte boundary
// Total: 16 + 16 + 16 + 16 = 64 bytes
typedef struct __attribute__((aligned(16))) gX_UBO_Standard {
    gX_F32 viewPosition[4];  // offset   0, size 16 (vec3 + 4b pad)
    gX_F32 lightPosition[4]; // offset  16, size 16 (vec3 + 4b pad)
    gX_F32 ambientColor[4];  // offset  32, size 16
    gX_F32 ambientFactor;    // offset  48, size  4
    gX_F32 _pad0[3];         // offset  52, size 12 → total 64 bytes
} gX_UBO_Standard;
//opencode: compile-time size check (C: _Static_assert, C++: static_assert)
#ifdef __cplusplus
static_assert(sizeof(gX_UBO_Standard) == 64, "gX_UBO_Standard must be 64 bytes");
#else
_Static_assert(sizeof(gX_UBO_Standard) == 64, "gX_UBO_Standard must be 64 bytes");
#endif

typedef struct gX_RendererGFX_Shader {
    gX_I1       loadAsap;
    gX_I1       loaded;
    gX_I1       reload;
    gX_I1       reloadDeferred; //opencode: defer reload to next frame boundary
    gX_U64      reloadCheckedFrame; //opencode: each shader is checked once per frame
    gX_I1       locsStale;      //opencode: set by OnReload — effect shaders re-cache locations
    gX_TextLine vertPath;
    gX_TextLine fragPath;
    gX_I32      version;
    gX_Shader   shader;
    gX_RendererGFX_ShaderLocs locations;
    gX_Material material;
    gX_I8       defines[1024]; //opencode: compile-time defines (e.g., "#define QUALITY 2\n")
                                //   was 256 — QUALITY+GX_GLFW_FRAG+GL level defines exceed it,
                                //   truncating GX_GL_HAS_CUBEMAP_ARRAY (point shadows stripped)
    gX_I32      shaderType;   //opencode: GX_SHADER_TYPE_* for feature level, -1 = legacy path
    gX_U32      uboStandard;         //opencode: GL buffer handle for standard UBO (binding 0)
    gX_I32      uboStandardIndex;    //opencode: glGetUniformBlockIndex result
    gX_UBO_Standard uboData;         //opencode: CPU-side copy for batch upload
} gX_RendererGFX_Shader;

gX_I32 gX_RendererGFX_Shader_Init(gX_RendererGFX_Shader *shader, const gX_I8 *vertPath, const gX_I8 *fragPath);
gX_I0  gX_RendererGFX_Shader_SetType(gX_RendererGFX_Shader *shader, gX_I32 shaderType);
gX_I32 gX_RendererGFX_Shader_Load(      gX_RendererGFX_Shader *shader);
gX_I32 gX_RendererGFX_Shader_Unload(    gX_RendererGFX_Shader *shader);
gX_I0  gX_RendererGFX_Shader_BindTextureUniforms(gX_RendererGFX_Shader *shader);
gX_I0  gX_RendererGFX_CookieAtlas_Rebuild(gX_RendererGFX *gfx);
gX_I0  gX_RendererGFX_CookieAtlas_Delete(gX_RendererGFX *gfx);

#endif //RENDERER_GFX_SHADER_H
