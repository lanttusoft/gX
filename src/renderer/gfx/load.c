/**********************************************************************************************
*
*   [gX: src/renderer/gfx/load.c]
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


//opencode: extracted from gfx.c Load/Unload — defaults, RT init, RT cleanup
#include <stdlib.h>
#include <string.h>

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/path.h"
#include "../../common/utils.h"
#include "../../gxmath/matrix.h"
#include "../../gxmath/constants.h"
#include "../gfx.h"
#include "../renderer.h"
#include "../../main.h"
#include "../../camera.h"
#include "../../wrappers/wrappers.h"
#include "../../rhi/rhi_buf.h"
#include "load.h"
#include "boilerplate.h"
#include "bloom.h"
#include "gl_level.h"
#include "dof.h"
#include "taa.h"
#include "motionblur.h"
#include "rt_helpers.h"
#include "../../rhi/rhi_fbo.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_tex.h"

//opencode: heap-backed dynamic arrays — Add_Push_NoPtr auto-grows the heap
#define GFX_NEW_MODEL(name) \
    currentModel = gX_Heap_Add_Push_NoPtr(&gfx->models); \
    gX_RendererGFX_Model_Init(currentModel, name);

#define GFX_NEW_TEXTURE(name) \
    { \
        gX_RendererGFX_Texture *t = gX_Heap_Add_Push_NoPtr(&gfx->textures); \
        gX_RendererGFX_Texture_Init(t); \
        strcpy(t->wrapper.path, name); \
    }

gX_I0 gX_RendererGFX_Load_SetDefaults(gX_RendererGFX *gfx)
{
    //opencode: zero all state flags first (heap allocation may leave garbage bits)
    memset(&gfx->state, 0, sizeof(gfx->state));

    //opencode: core lighting — always on
    gfx->state.ENABLE_LIGHTING_AMBIENT = true;
    gfx->state.ENABLE_LIGHTING_DIFFUSE = true;
    gfx->state.ENABLE_LIGHTING_SPECULAR = true;

    gfx->state.ENABLE_MAP_ALBEDO = true;
    gfx->state.ENABLE_MAP_METALNESS = true;
    gfx->state.ENABLE_MAP_NORMAL = true;
    gfx->state.ENABLE_MAP_PARALLAX = true;

    gfx->state.ENABLE_GOURAUD = false;
    gfx->state.ENABLE_PHONG = true;

    //opencode: sun defaults — zero-initialized sun produces zero diffuse/specular
    //   light, making models invisible even with ambient enabled. These defaults
    //   match the n64box.json preset that users have been relying on for visibility.
    gfx->sun.dir = (gX_F32v3){-1.0f, -2.0f, -1.0f};
    gfx->sun.brightness = 1.0f;
    gfx->sun.color = gX_WHITE;    gfx->sun.diffuseTint = gX_WHITE;
    gfx->sun.specularTint = gX_WHITE;
    gfx->sun.diffuseFactor = 1.0f;
    gfx->sun.specularFactor = 1.0f;

    //opencode: AUDIT — exact n64box.json defaults for comparison
    gfx->sky.strength = 0.15f;   //opencode: ambient strength — was 3.0, then 0.25 (washed out)
    gfx->sky.color = (gX_Color){200, 200, 255, 255};
    //opencode: Sky Lab defaults — the values draw.c hardcoded per frame.
    gfx->sky.altitude = 1.0f;
    gfx->sky.turbidity = 2.0f;
    gfx->sky.density = 2.2f;
    gfx->sky.hue = 0.0f;
    gfx->sky.horizon = 0.8f;
    gfx->sky.sunIntensity = 0.5f;
    gfx->sky.contrast = 1.5f;
    gfx->sky.starDensity = 1.0f;
    gfx->sky.starBrightness = 1.0f;
    gfx->sky.starTwinkle = 1.0f;
    gfx->sun.castShadows = false;
    gfx->fog.near = gX_GFX_FOG_NEAR;
    gfx->fog.far = gX_GFX_FOG_FAR;
    gfx->fog.color = gX_WHITE;
    gfx->fog.heightDensity = gX_GFX_FOG_HEIGHT_DENSITY;
    gfx->fog.heightBase = gX_GFX_FOG_HEIGHT_BASE;
    gfx->fog.matchVoidColor = 0;

    //opencode: scene exposure — applied in standard.frag before ACES tonemap
    gfx->state.exposure = 0.80f;

    //opencode: post-process and advanced features — n64box.json values
    gfx->state.ENABLE_FOG = false;
    gfx->state.ENABLE_SHADOWS = false;
    gfx->state.ENABLE_SKYBOX = true;
    gfx->state.ENABLE_PROCEDURAL_SKY = false;
    gfx->state.ENABLE_REFLECTIONS = false;
    gfx->state.ENABLE_PLANAR_REFLECTIONS = true;
    gfx->state.ENABLE_POINT_SHADOWS = false;
    gfx->state.ENABLE_COOKIES = false;
    gfx->state.ENABLE_BLOOM = false;
    gfx->state.ENABLE_MOTION_BLUR = false;
    gfx->state.ENABLE_DOF = false;
    gfx->state.ENABLE_TAA = false;
    gfx->state.ENABLE_FXAA = true; //opencode: v0.1.18 — cheap final-composite AA, on by default
    gfx->state.ENABLE_SSR = false;
    gfx->state.ENABLE_SSAO = false;
    gfx->state.ENABLE_RIM = false;
    //opencode: PBR materials need IBL ambient by default; safe mode disables it below.
    gfx->state.ENABLE_ENV_AMBIENT = true;
    gfx->state.ENABLE_DECALS = false;
    gfx->state.ENABLE_GEO_DEMO = false;
    gfx->state.ENABLE_WATER = false;
    gfx->state.ENABLE_PARTICLES = false;
    //opencode: CSM defaults — 1 cascade (verified: 3 cascades work, toggle via ImGui or settings).
    gfx->csm.cascadeCount = 2;
    gfx->csm.cascadeDistances[0] = gX_GFX_CSM_SPLIT0;
    gfx->csm.cascadeDistances[1] = gX_GFX_CSM_SPLIT1;
    gfx->csm.cascadeDistances[2] = gX_GFX_CSM_SPLIT2;
    gfx->csm.cascadeDistances[3] = gX_GFX_CSM_SPLIT3;
    gfx->csm.blendScale = 1.0f;
    //opencode: CSM defaults — 1 cascade (no split, same behavior as today).
    gfx->ssr.maxSteps = gX_GFX_SSR_MAX_STEPS;
    gfx->ssr.stepSize = gX_GFX_SSR_STEP_SIZE;
    gfx->ssr.maxDistance = gX_GFX_SSR_MAX_DISTANCE;
    gfx->ssr.depthBias = gX_GFX_SSR_DEPTH_BIAS;
    gfx->state.ENABLE_SSR_AUXVIEW = true;   //opencode: v0.3.4 auxview ships on
    gfx->ssr.opacity = gX_GFX_SSR_OPACITY;
    gfx->ssr.roughness = 0.2f; //opencode: was 0.3 — smoother = stronger reflections
    gfx->ssr.maxLod = 2.5f;    //opencode: Hi-Z max mip LOD (was 4.0 — coarse mips made far reflections blocky)
    gfx->ssr.edgeFade = gX_GFX_SSR_EDGE_FADE;    //opencode: screen-edge dissolve band
    gfx->ssr.maxBounces = gX_GFX_SSR_MAX_BOUNCES; //opencode: extra ray bounces
    gfx->ssr.bounceAtten = gX_GFX_SSR_BOUNCE_ATTEN; //opencode: per-bounce attenuation
    gfx->ssr.maxDepthDelta = gX_GFX_SSR_MAX_DEPTH_DELTA;
    gfx->ssr.worldGapThreshold = gX_GFX_SSR_WORLD_GAP_THRESHOLD;
    gfx->ssr.finalDepthThreshold = gX_GFX_SSR_FINAL_DEPTH_THRESHOLD;
    gfx->ssr.hitFacingThreshold = gX_GFX_SSR_HIT_FACING_THRESHOLD;
    gfx->reflection.bias = gX_GFX_REFLECTION_BIAS_DEFAULT;
    //opencode: initialize SSR uniform locations to -1 (avoid uploading to loc 0 before caching)
    gfx->ssr.depthMapLoc = -1;
    gfx->ssr.colorMapLoc = -1;
    gfx->ssr.normalMapLoc = -1;
    gfx->ssr.hasNormalMapLoc = -1;
    gfx->ssr.normalFallbackAngleLoc = -1;
    gfx->ssr.normalLiftModeLoc = -1;
    gfx->ssr.normalLiftHybridAngleLoc = -1;
    gfx->ssr.normalAcceptModeLoc = -1;
    //opencode: optional self-plane rejection experiment uniform.
    gfx->ssr.selfPlaneRejectEnabledLoc = -1;
    gfx->ssr.vpLoc = -1;
    gfx->ssr.invVPLoc = -1;
    gfx->ssr.camPosLoc = -1;
    gfx->ssr.maxStepsLoc = -1;
    gfx->ssr.stepSizeLoc = -1;
    gfx->ssr.maxDistanceLoc = -1;
    gfx->ssr.depthBiasLoc = -1;
    gfx->ssr.planarMapLoc = -1;
    gfx->ssr.planarDepthMapLoc = -1;
    gfx->ssr.hasPlanarDepthLoc = -1;
    gfx->ssr.hasPlanarLoc = -1;
    gfx->ssr.planeDistLoc = -1;
    gfx->ssr.planeNormalLoc = -1;
    gfx->ssr.planarDeferLoc = -1;
    gfx->ssr.opacityLoc = -1;
    gfx->ssr.roughnessLoc = -1;
    gfx->ssr.maxLodLoc = -1;
    gfx->ssr.maxDepthDeltaLoc = -1;
    //opencode: optional silhouette range-scale experiment uniform.
    gfx->ssr.maxDepthDistanceScaleLoc = -1;
    gfx->ssr.worldGapThresholdLoc = -1;
    gfx->ssr.finalDepthThresholdLoc = -1;
    gfx->ssr.hitFacingThresholdLoc = -1;
    gfx->ssr.debugModeLoc = -1;
    gfx->ssr.envMapLoc = -1;      //opencode: v0.1.11 — cubemap fallback + edge/bounce tuning
    gfx->ssr.hasEnvMapLoc = -1;
    gfx->ssr.fogLutMapLoc = -1;    //opencode: v0.3.8
    gfx->ssr.hasFogLutLoc = -1;    //opencode: v0.3.8
    gfx->ssr.edgeFadeLoc = -1;
    gfx->ssr.maxBouncesLoc = -1;
    gfx->ssr.bounceAttenLoc = -1;
    gfx->state.ssrDebugMode = 0;

    //opencode: shader tuning constant defaults
    gfx->state.shadowBiasMax = gX_GFX_SHADOW_BIAS_MAX;
    gfx->state.shadowBiasMin = gX_GFX_SHADOW_BIAS_MIN;
    gfx->state.parallaxMinLayers = gX_GFX_PARALLAX_MIN_LAYERS;
    gfx->state.parallaxMaxLayers = gX_GFX_PARALLAX_MAX_LAYERS;
    gfx->state.parallaxOffset = gX_GFX_PARALLAX_OFFSET;
    gfx->state.parallaxShadowOffset = gX_GFX_PARALLAX_SHADOW_OFFSET;
    gfx->state.parallaxShadowSteps = gX_GFX_PARALLAX_SHADOW_STEPS;
    gfx->state.pointShadowBias = gX_GFX_POINT_SHADOW_BIAS;
    gfx->state.pointShadowExtra = gX_GFX_POINT_SHADOW_EXTRA;
    gfx->state.pointShadowMinBias = gX_GFX_POINT_SHADOW_MIN_BIAS;
    gfx->state.tangentLengthThreshold = gX_GFX_TANGENT_LENGTH_THRESH;
    gfx->state.normalYThreshold = gX_GFX_NORMAL_Y_THRESHOLD;

    //opencode: texture filtering default — trilinear for quality
    gfx->state.textureFilter = gX_TEXTURE_FILTER_TRILINEAR;
    //opencode: shader quality — LOW to force flat ambient (avoids IBL)    
    gfx->state.shaderQuality = 0;
    //opencode: mipmap LOD bias — 0.0 = default (no shift).
    gfx->state.mipmapBias = 0.0f;
    //opencode: MSAA — off by default (users enable via preset or ImGui).
    gfx->state.msaaLevel = 0;
    //opencode: quality preset — High by default (sets all dependent fields).
    gfx->state.qualityPreset = 0;
    //opencode: --preset <0-3> CLI override (was parsed but never applied)
    if (arguments.hasPreset) gfx->state.qualityPreset = arguments.preset;
    gX_RendererGFX_ApplyQualityPreset(gfx);
    //opencode: god rays — off by default, opt-in via preset or ImGui.
    gfx->state.ENABLE_GODRAYS = false;
    gfx->godray.density = 64;
    gfx->godray.decay = 0.9f;
    gfx->godray.weight = 0.42f;
    gfx->godray.exposure = 1.0f;

    //opencode: volumetric fog — off by default, subtle defaults when enabled.
    //opencode: v0.3.2 — CPU-culled tile clustering default-on; GX_NO_CLUSTER=1 falls back
    { const char* nc = getenv("GX_NO_CLUSTER"); gfx->state.ENABLE_CLUSTERED = !(nc && nc[0]=='1'); }
    gfx->state.ENABLE_VOLUME_FOG = false;
    //opencode: default volume fog density tuned against the showcase horizon;
    //   scene-specific forest/lake presets provide denser atmosphere.
    gfx->fogVolume.density       = 0.0005f;
    gfx->fogVolume.scattering    = 1.0f;
    gfx->fogVolume.hgAsymmetry   = 0.7f;
    gfx->fogVolume.enablePointLights = true;   //opencode: item-13

    //opencode: lens flares — off by default.
    gfx->state.ENABLE_LENS_FLARE = false;
    gfx->lensflare.intensity    = 1.0f;
    gfx->lensflare.dispersal    = 1.0f;

    //opencode: occlusion culling — off by default (opt-in for world scene).
    gfx->state.ENABLE_OCCLUSION_CULLING = false;

    //opencode: bindless textures — off by default (opt-in; requires GL 4.x + extension).
    gfx->state.ENABLE_BINDLESS = false;
    //opencode: ACES tone mapping — on by default (strictly better than Reinhard).
    gfx->state.ENABLE_ACES_TONEMAP = true;
    { const char* nx = getenv("GX_NO_AGX"); gfx->state.ENABLE_AGX_TONEMAP = !(nx && nx[0]=='1'); }   //opencode: v0.3-M1 — AgX default
    gfx->state.ENABLE_GAMMA_CORRECTION = true;
    gfx->state.ENABLE_STENCIL = false;
    gfx->state.ENABLE_STENCIL_OUTLINE = false;
    gfx->state.ENABLE_FRUSTUM_CULLING = false;

    //opencode: particle system — disabled by default (enabled in demo scene).
    gfx->state.ENABLE_PARTICLES = false;
    gX_RendererGFX_Particle_Init(gfx);

    //opencode: CPU falling snow — disabled by default (opt-in fixture only).
    gfx->state.ENABLE_SNOWFALL = false;
    gfx->state.ENABLE_RAIN = false;
    gfx->state.ENABLE_FOGSPRITE = false;
    gfx->state.ENABLE_SPLAT = false;
    gX_RendererGFX_Weather_Init(gfx);
    gX_RendererGFX_Splat_Init(gfx);

    gfx->state.SHOW_GRID = false;
    gfx->state.SHOW_GIZMO = false;
    gfx->state.SHOW_BOUNDING_BOX = false; //opencode: debug toggle, off by default
    gfx->state.SHOW_SKELETON = false;     //opencode: bind-pose bone overlay, off by default
    gfx->state.SHOW_LIGHT_MARKERS = false;
    gfx->common.pickedModelIndex = -1;
    gfx->common.battleScene = false;
    gfx->common.physicsScene = false;
    gfx->bloom.threshold = gX_GFX_BLOOM_THRESHOLD;
    gfx->bloom.knee = gX_GFX_BLOOM_KNEE;
    gfx->bloom.intensity = gX_GFX_BLOOM_INTENSITY;
    gfx->bloom.blurAmount = gX_GFX_BLOOM_BLUR_AMOUNT;
    gfx->bloom.exposure = gX_GFX_BLOOM_EXPOSURE;
    gfx->bloom.vignette = gX_GFX_BLOOM_VIGNETTE;
    gfx->bloom.saturation = gX_GFX_BLOOM_SATURATION;
    gfx->bloom.contrast = gX_GFX_BLOOM_CONTRAST;

    //opencode: detail texture defaults
    gfx->state.ENABLE_DETAIL = false;
    gfx->detail.uvScale = gX_GFX_DETAIL_UV_SCALE;
    gfx->detail.strength = gX_GFX_DETAIL_STRENGTH;
    gfx->detail.distFade = gX_GFX_DETAIL_DIST_FADE;
    gfx->detail.mapPath[0] = 0;
    gfx->detail.map = (gX_Texture){0};

    //opencode: --gfx-safe-mode — disable all effects, maps, debug overlays, and sky
    //   NOTE: shader init guards (procedural sky, IBL) caused models to disappear.
    //   All shaders are always loaded; only state flags are disabled here.
    if (arguments.gfxSafeMode) {
        gfx->state.ENABLE_FOG = false;
        gfx->state.ENABLE_SHADOWS = false;
        gfx->state.ENABLE_POINT_SHADOWS = false;
        gfx->state.ENABLE_REFLECTIONS = false;
        gfx->state.ENABLE_PLANAR_REFLECTIONS = false;
    gfx->state.ENABLE_PARTICLES = false;
    gfx->state.ENABLE_SNOWFALL = false;
    gfx->state.ENABLE_RAIN = false;
    gfx->state.ENABLE_FOGSPRITE = false;
    gfx->state.ENABLE_SPLAT = false;
    gfx->state.ENABLE_INSTANCING = false;
        gfx->state.ENABLE_BLOOM = false;
        gfx->state.ENABLE_SSR = false;
        gfx->state.ENABLE_SSAO = false;
        gfx->state.ENABLE_DOF = false;
        gfx->state.ENABLE_TAA = false;
        gfx->state.ENABLE_MOTION_BLUR = false;
        gfx->state.ENABLE_GODRAYS = false;
        gfx->state.ENABLE_LENS_FLARE = false;
        gfx->state.ENABLE_RIM = false;
        gfx->state.ENABLE_DECALS = false;
        gfx->state.ENABLE_WATER = false;
    gfx->state.ENABLE_GEO_DEMO = false;
    gfx->state.ENABLE_INSTANCING = false;
        gfx->state.ENABLE_DETAIL = false;
        gfx->state.ENABLE_COOKIES = false;
        gfx->state.SHOW_GIZMO = false;
        gfx->state.SHOW_BOUNDING_BOX = false;
        gfx->state.SHOW_SKELETON = false;
        gfx->state.SHOW_LIGHT_MARKERS = false;
        gfx->state.SHOW_DEPTH_VISUALIZER = false;
        gfx->state.ENABLE_ENV_AMBIENT = false;
        // Disable sky (shaders stay loaded — guards caused invisible models)
    gfx->state.ENABLE_PROCEDURAL_SKY = true;
        gfx->state.ENABLE_SKYBOX = false;
    }

}

//opencode: validate critical state fields after initialization — warns on zero defaults
gX_I32 gX_RendererGFX_ValidateState(gX_RendererGFX *gfx) {
    gX_I32 warns = 0;
    // Check sun
    if (gfx->sun.brightness <= 0.0f) { gX_TraceLog_GFX(gX_LOG_WARNING, "AUDIT: sun.brightness is 0"); warns++; }
    if (gfx->sun.dir.x == 0 && gfx->sun.dir.y == 0 && gfx->sun.dir.z == 0) { gX_TraceLog_GFX(gX_LOG_WARNING, "AUDIT: sun.dir is (0,0,0)"); warns++; }
    // Check sky
    if (gfx->sky.strength <= 0.0f) { gX_TraceLog_GFX(gX_LOG_WARNING, "AUDIT: sky.strength is 0"); warns++; }
    if (gfx->sky.color.r == 0 && gfx->sky.color.g == 0 && gfx->sky.color.b == 0) { gX_TraceLog_GFX(gX_LOG_WARNING, "AUDIT: sky.color is black"); warns++; }
    // Check state flags
    if (!gfx->state.ENABLE_LIGHTING_AMBIENT && !gfx->state.ENABLE_LIGHTING_DIFFUSE) { gX_TraceLog_GFX(gX_LOG_WARNING, "AUDIT: all lighting is disabled"); warns++; }
    if (!gfx->state.ENABLE_PHONG && !gfx->state.ENABLE_GOURAUD) { gX_TraceLog_GFX(gX_LOG_WARNING, "AUDIT: no lighting model selected (Phong=0, Gouraud=0)"); warns++; }
    // Check renderer
    if (gfx->second.rt == NULL) { gX_TraceLog_GFX(gX_LOG_WARNING, "AUDIT: second.rt is NULL"); warns++; }
    // Check SSBO (created lazily — warning is a false alarm since SSBO init runs after audit)
    if (warns > 0) gX_TraceLog_GFX(gX_LOG_WARNING, "AUDIT: %d validation warnings", warns);
    else gX_TraceLog_GFX(gX_LOG_INFO, "AUDIT: state validation OK");
    return warns;
}

gX_I32 gX_RendererGFX_Render_Init(gX_RendererGFX_Render *render, gX_RendererGFX *gfx, gX_I1 depth)
{
    render->useLightCamera = false;
    render->lightCameraSun = false;
    render->skipBlended = false;
    render->skipColorClear = false;
    if (depth)
    {
        render->depthMap = true;
        render->clearColor = &gfx->sky.color;
        render->camera = &gfx->sun.camera.camera3D;
        render->rt = gfx->sun.camera.map;
    }
    else
    {
        render->depthMap = false;
        render->clearColor = &gfx->sky.color;
        render->camera = &camera.camera3D;
        //opencode: when bloom is enabled, render the opaque scene to hdrScene
        //   (16-bit float) so the bloom composite can read it. Without this,
        //   the opaque pass only renders to sceneColorLDR (8-bit) and the
        //   bloom composite at unit 15 reads hdrScene which has no opaque content.
        if (gfx->state.ENABLE_BLOOM && gfx->bloom.hdrScene.wrapper.loadingState.loaded)
            render->rt = &gfx->bloom.hdrScene.wrapper.rt;
        else
            render->rt = &gfx->sceneColorLDR.wrapper.rt;
    }
    render->ready = true;
    return 0;
}

//opencode: create or destroy the MSAA FBO based on current msaaLevel.
//   Called from InitRTs, on resize, and when msaaLevel changes at runtime.
//   Owns the hdrMSAA target fully (generic RT load/unload never touches it:
//   multisample attachments need custom assembly, not RenderTexture_Load).
gX_I0 gX_RendererGFX_MSAA_Create(gX_RendererGFX *gfx)
{
    //opencode: destroy existing MSAA resources first
    if (gfx->bloom.hdrMSAA.wrapper.rt.id > 0) {
        gX_RHI_Tex_Delete(gfx->bloom.hdrMSAA.wrapper.rt.texture.id);
        gX_RHI_Tex_Delete(gfx->bloom.hdrMSAA.wrapper.rt.depth.id);
        gX_RHI_FBO_Delete(gfx->bloom.hdrMSAA.wrapper.rt.id);
        gfx->bloom.hdrMSAA.wrapper.rt.id = 0;
        gfx->bloom.hdrMSAA.wrapper.rt.texture.id = 0;
        gfx->bloom.hdrMSAA.wrapper.rt.depth.id = 0;
    }
    gfx->bloom.hdrMSAA.wrapper.loadingState.loaded = false;

    gX_I32 msaa = gfx->state.msaaLevel;
    if (msaa < 2) return; //opencode: MSAA disabled (or invalid level)
    //opencode: selected GL policy owns GFX feature use while the RHI retains
    //   raw hardware caps. A GL3.0/3.1 inspection must follow the single-sample
    //   path even if its host GL3.3 context could allocate MSAA textures.
    if (!gX_GL.hasMSTexture)
    {
        gX_TraceLog_GFX(gX_LOG_NOTICE, "GL_POLICY: MSAA texture request=%dx suppressed at selected GL%d",
            msaa, gX_GL.useLevel);
        return;
    }

    //opencode: clamp to valid range
    if (msaa > 8) msaa = 8;
    if (msaa & (msaa - 1)) msaa = 4; //opencode: not power of 2 → default to 4x

    gX_U32 w = (gX_U32)maindata.renderWidth;
    gX_U32 h = (gX_U32)maindata.renderHeight;
    if (w == 0 || h == 0) return;

    //opencode: create multisampled color + depth textures
    gX_U32 colorTex = gX_RHI_Tex_CreateMultisample((gX_U32)msaa, gX_RHI_RGBA16F, w, h, "msaa_color");

    //opencode: create multisampled depth texture (not renderbuffer)
    //   Using a texture allows glBlitFramebuffer depth resolve into hdrScene's
    //   depth texture. Renderbuffer→texture blits fail with GL_INVALID_OPERATION.
    gX_U32 depthTex = gX_RHI_Tex_CreateMultisample((gX_U32)msaa, gX_RHI_DEPTH_COMPONENT24, w, h, "msaa_depth");

    //opencode: create FBO and attach
    gX_U32 fbo = 0;
    gX_RHI_FBO_Gen(1, &fbo);
    gX_RHI_BindFBO(fbo);
    gX_RHI_FBO_AttachTexture(colorTex, gX_RHI_COLOR_ATTACHMENT0, gX_RHI_TEXTURE_2D_MULTISAMPLE);
    gX_RHI_FBO_AttachTexture(depthTex, gX_RHI_DEPTH_ATTACHMENT, gX_RHI_TEXTURE_2D_MULTISAMPLE);

    if (!gX_RHI_FBO_IsComplete()) {
        gX_TraceLog_GFX(gX_LOG_ERROR, "MSAA FBO incomplete (msaa=%d)", msaa);
        gX_RHI_FBO_Delete(fbo);
        gX_RHI_Tex_Delete(depthTex);
        gX_RHI_Tex_Delete(colorTex);
        gX_RHI_UnbindFBO();
        return;
    }
    gX_RHI_UnbindFBO();

    //opencode: Phase 1 — store the multisampled objects in the named hdrMSAA
    //   target (no raw handles, no swapping into hdrScene). loaded=true is
    //   the MSAA-live predicate; loadAsap=false keeps the generic RT loader
    //   away. rt.texture dims feed BeginTexture's viewport/ortho.
    gfx->bloom.hdrMSAA.wrapper.rt.id = fbo;
    gfx->bloom.hdrMSAA.wrapper.rt.texture.id = colorTex;
    gfx->bloom.hdrMSAA.wrapper.rt.texture.width = (gX_I32)w;
    gfx->bloom.hdrMSAA.wrapper.rt.texture.height = (gX_I32)h;
    gfx->bloom.hdrMSAA.wrapper.rt.depth.id = depthTex;
    gfx->bloom.hdrMSAA.wrapper.rt.depth.width = (gX_I32)w;
    gfx->bloom.hdrMSAA.wrapper.rt.depth.height = (gX_I32)h;
    gfx->bloom.hdrMSAA.wrapper.width = w;
    gfx->bloom.hdrMSAA.wrapper.height = h;
    gfx->bloom.hdrMSAA.wrapper.loadingState.loaded = true;
    gfx->bloom.hdrMSAA.wrapper.loadingState.loadAsap = false;
    gX_TraceLog_GFX(gX_LOG_NOTICE, "MSAA FBO created: %dx%d msaa=%d fbo=%u", w, h, msaa, fbo);
}

//opencode: apply a quality preset — cascades to all quality-relevant state fields.
//   One-shot: after setting dependent fields, qualityPreset remains at the chosen
//   tier for reference. Call again when qualityPreset changes.
gX_I0 gX_RendererGFX_ApplyQualityPreset(gX_RendererGFX *gfx)
{
    gX_I32 preset = gfx->state.qualityPreset;
    if (preset < 0) preset = 0;
    if (preset > 3) preset = 3;

    switch (preset) {
    case 0: // Low — no AA, point/bilinear, LOW shader
        gfx->state.textureFilter   = gX_TEXTURE_FILTER_BILINEAR;
        gfx->state.shaderQuality   = 0;
        gfx->state.mipmapBias      = 0.0f;
        gfx->state.msaaLevel       = 0;
        break;
    case 1: // Medium — bilinear, MED shader
        gfx->state.textureFilter   = gX_TEXTURE_FILTER_TRILINEAR;
        gfx->state.shaderQuality   = 1;
        gfx->state.mipmapBias      = 0.0f;
        gfx->state.msaaLevel       = 0;
        break;
    case 2: // High — trilinear + 4x AF, HIGH shader (default gameX config)
        gfx->state.textureFilter   = gX_TEXTURE_FILTER_ANISOTROPIC_4X;
        gfx->state.shaderQuality   = 2;
        gfx->state.mipmapBias      = 0.0f;
        gfx->state.msaaLevel       = 0;
        break;
    case 3: // Ultra — 16x AF, HIGH shader, 4x MSAA, slight negative LOD bias, occlusion culling
        gfx->state.textureFilter   = gX_TEXTURE_FILTER_ANISOTROPIC_16X;
        gfx->state.shaderQuality   = 2;
        gfx->state.mipmapBias      = -0.5f;
        gfx->state.msaaLevel       = 4;
        gfx->state.ENABLE_OCCLUSION_CULLING = true;
        break;
    }

    gX_TraceLog_GFX(gX_LOG_NOTICE, "Quality preset %d applied (filter=%d shader=%d msaa=%d lod=%+.1f)",
        preset, gfx->state.textureFilter, gfx->state.shaderQuality,
        gfx->state.msaaLevel, gfx->state.mipmapBias);
}

//opencode: init all RTs and subsystem resources (shadow, HDR, bloom, SSR, SSAO, refraction, motion blur, DOF, TAA, IBL)
//ai: initialize temporal history locations, HDR GTAO depth history, and point-filtered velocity data.
//ai: optional SSR normals use floating-point storage: exact encoded zero components prevent flat-normal reflection leaks.
gX_I32 gX_RendererGFX_Load_InitRTs(gX_RendererGFX *gfx)
{

    gX_RendererGFX_RenderTexture *map0 = gX_Heap_SeekPtr_Index(&gfx->maps, 0);
    //opencode: map0 should never be NULL here (maps heap pre-allocated with
    // amount=1, max=8). Guarding defensively in case of heap init failure.
    if (map0)
    {
        gX_RendererGFX_RenderTexture_Init(map0);
        //opencode: override sun shadow map size to 1024 (Init sets CUBE_SIZE=4096)
        map0->wrapper.width = gX_GFX_SHADOW_MAP_SIZE;
        map0->wrapper.height = gX_GFX_SHADOW_MAP_SIZE;
        gfx->sun.camera.map = &map0->wrapper.rt;
    }
    else
    {
        gX_TraceLog_GFX(gX_LOG_ERROR, "maps heap failed to allocate — shadow maps disabled");
    }

    //opencode: initialize CSM — single 2D array depth texture (4 layers, deferred GL)
    gfx->csm.inited = false;
    gfx->csm.cascadeCount = 2;
    gfx->csm.arrayTexId = 0;
    gfx->csm.fboId = 0;
    gfx->csm.map.wrapper.loadingState.loaded = false;
    gfx->csm.vps[0] = gX_Matrix_Identity();
    gfx->csm.vps[1] = gX_Matrix_Identity();
    gfx->csm.vps[2] = gX_Matrix_Identity();
    gfx->csm.vps[3] = gX_Matrix_Identity();

    gX_RendererGFX_Render_Init(&gfx->first, gfx, true);
    gX_RendererGFX_Render_Init(&gfx->second, gfx, false);
    gfx->second.skipBlended = true; //opencode: transparent models drawn in separate pass
    gX_RendererGFX_ViewportGrid_Init(gfx);

    GX_GFX_RT_INIT_HDR(gfx, bloom.hdrScene);
    GX_GFX_RT_INIT_HDR(gfx, bloom.hdrMSAA);   // MSAA_Create-owned: custom multisample assembly, never generic-loaded
    gfx->bloom.hdrMSAA.wrapper.loadingState.loadAsap = false;
    GX_GFX_RT_INIT_LDR(gfx, sceneColorLDR);

    GX_GFX_RT_INIT_HDR(gfx, reflection.planar);

    //opencode: SSR — shaders and RTs loaded but disabled by default (opt-in via preset or --gfx-enable)
    GX_GFX_RT_INIT_HDR(gfx, ssr.map);
    //opencode: the shading-normal target is deliberately opt-in: the default
    //   GL33 path remains depth-only and pays no extra full-resolution pass.
    {
        const gX_I8 *normalHook = getenv("GX_SSR_NORMALS");
        gfx->ssr.normalBufferEnabled = normalHook && normalHook[0] != '0';
        gfx->ssr.normalPass = false;
        gX_RendererGFX_RenderTexture_Init(&gfx->ssr.normalMap);
        gfx->ssr.normalMap.wrapper.width = maindata.renderWidth;
        gfx->ssr.normalMap.wrapper.height = maindata.renderHeight;
        gfx->ssr.normalMap.hdr = true;
        gfx->ssr.normalMap.wrapper.loadingState.loadAsap = gfx->ssr.normalBufferEnabled;
    }
    GX_GFX_SHADER_INIT(gfx, ssr.shader, "gfx_ssr.frag");


    //opencode: SSR sky fallback LUT (128x64 equirect RGBA16F) + Hi-Z chain
    GX_GFX_RT_INIT_HDR(gfx, ssr.skyLut);
    gfx->ssr.skyLut.wrapper.width = 128;
    gfx->ssr.skyLut.wrapper.height = 64;
    //opencode: v0.3.8 - fog delta LUT: the gen program's sky branch run per
    //   equirect direction; the SSR escape path adds it to the env fallback
    GX_GFX_RT_INIT_HDR(gfx, ssr.fogLut);
    gfx->ssr.fogLut.wrapper.width = 128;
    gfx->ssr.fogLut.wrapper.height = 64;
    GX_GFX_SHADER_INIT(gfx, ssr.hizShader, "gfx_hiz.frag");
    gfx->ssr.hizInited = false;

    //opencode: SSAO — half-res RTs
    GX_GFX_RT_INIT_HALF_HDR(gfx, ssao.ssgiMap); //opencode: v0.3-M4
    GX_GFX_RT_INIT_HALF(gfx, ssao.map);
    GX_GFX_RT_INIT_HALF(gfx, ssao.blurMap);
    //opencode: contact shadows (spike 1) — half-res LDR occlusion map
    GX_GFX_RT_INIT_HALF(gfx, contact.map);
    GX_GFX_SHADER_INIT(gfx, contact.genShader, "gfx_contact.frag");
    GX_GFX_SHADER_INIT(gfx, ssao.blurShader, "gfx_ssao_blur.frag");
    //opencode: v0.3-M2 — GTAO (default on; legacy kernel SSAO kept as fallback)
    GX_GFX_SHADER_INIT(gfx, ssao.gtaoShader, "gfx_gtao.frag");
    //opencode: GTAO temporal accumulation (SSGI-Phase2 mirror)
    gfx->ssao.gtaoAccCurLoc = -1;
    gfx->ssao.gtaoAccHistLoc = -1;
    gfx->ssao.gtaoAccDepthMapLoc = -1;
    gfx->ssao.gtaoAccHistoryValidLoc = -1;
    gfx->ssao.gtaoAccVelLoc = -1;
    gfx->ssao.gtaoAccHasVelLoc = -1;
    gfx->ssao.gtaoAccResLoc = -1;
    GX_GFX_SHADER_INIT(gfx, ssao.gtaoAccumShader, "gfx_gtao_accum.frag");
    GX_GFX_RT_INIT_HALF_HDR(gfx, ssao.gtaoAccumA);
    GX_GFX_RT_INIT_HALF_HDR(gfx, ssao.gtaoAccumB);
    gX_RendererGFX_TemporalHistory_Init(&gfx->ssao.gtaoTemporal);
    gfx->ssao.gtaoFinalTex.id = 0;
    GX_GFX_SHADER_INIT(gfx, ssao.ssgiShader, "gfx_ssgi.frag"); //opencode: v0.3-M4
    //opencode: v0.3-M4 Phase2 — denoise chain
    GX_GFX_SHADER_INIT(gfx, ssao.ssgiBlurShader, "gfx_ssgi_blur.frag");
    //opencode: v0.3.4-fix - denoise loc fields must start invalid
    gfx->ssao.ssgiBlurMapLoc = -1;
    gfx->ssao.ssgiBlurDepthLoc = -1;
    gfx->ssao.ssgiBlurResLoc = -1;
    gfx->ssao.ssgiBlurNearLoc = -1;
    gfx->ssao.ssgiBlurFarLoc = -1;
    gfx->ssao.ssgiAccCurLoc = -1;
    gfx->ssao.ssgiAccHistLoc = -1;
    gfx->ssao.ssgiAccHistoryValidLoc = -1;
    gfx->ssao.ssgiAccVelLoc = -1;
    gfx->ssao.ssgiAccHasVelLoc = -1;
    gfx->ssao.ssgiAccResLoc = -1;
    GX_GFX_SHADER_INIT(gfx, ssao.ssgiAccumShader, "gfx_ssgi_accum.frag");
    GX_GFX_RT_INIT_HALF_HDR(gfx, ssao.ssgiBlurMap);
    GX_GFX_RT_INIT_HALF_HDR(gfx, ssao.ssgiAccumA);
    GX_GFX_RT_INIT_HALF_HDR(gfx, ssao.ssgiAccumB);
    gX_RendererGFX_TemporalHistory_Init(&gfx->ssao.ssgiTemporal);
    gfx->ssao.temporalModesInitialized = false;
    gfx->ssao.gtaoWasActive = false;
    gfx->ssao.ssgiWasActive = false;
    gfx->ssao.useGTAO = true;
    //opencode: Step 3 — GX_GTAO=0 forces the neutral-white AO fallback
    //   (diagnosis hook, same pattern as GX_SSGI)
    {
        const gX_I8 *ge = getenv("GX_GTAO");
        if (ge && ge[0] == '0') gfx->ssao.useGTAO = false;
    }
    gfx->ssao.thickness = 1.0f;
    gfx->ssao.gtaoLocsCached = false;
    //opencode: v0.3.1 — SSGI ships DEFAULT-ON (GX_SSGI=0 opts out)
    gfx->ssao.useSSGI = true;
    { const char* se = getenv("GX_SSGI"); if (se && se[0]=='0') gfx->ssao.useSSGI = false; }
    gfx->ssao.ssgiIntensity = 2.8f;
    gfx->ssao.ssgiBounceGain = 0.5f;
    //opencode: SSAO defaults — off by default for backward compat. Tunings
    //   (radius 0.5, bias 0.025, strength 1.0, kernel 32) match the reference SSAO
    //   example. Stricter bias would be needed at very large scenes.
    gfx->state.ENABLE_SSAO = false;
    gfx->ssao.radius = 0.5f;
    gfx->ssao.bias = 0.025f;
    gfx->ssao.strength = 2.5f;    // increased from 1.0 for more visible darkening
    gfx->ssao.kernelSize = 32; //opencode: shader caps at 32, was 64

    //opencode: rim lighting defaults
    gfx->state.rimIntensity = 0.5f;
    gfx->state.rimPower = 2.0f;
    gfx->state.rimColor = (gX_Color){255, 255, 255, 255};
    gfx->state.envAmbientFactor = 0.3f;

    GX_GFX_RT_INIT_HDR(gfx, debugVis.refractionBg);

    //opencode: motion blur
    gfx->motionBlur.enable = true;
    gfx->motionBlur.strength = 1.0f;
    gfx->motionBlur.maxSamples = 16;
    gfx->motionBlurPrevVP = gX_Matrix_Identity();
    gfx->taaPrevVP = gX_Matrix_Identity();   //opencode: TAA snapshot (own copy, see gfx.h)
    GX_GFX_RT_INIT_HDR(gfx, motionBlur.map);
    GX_GFX_SHADER_INIT(gfx, motionBlur.shader, "gfx_motionblur.frag");

    //opencode: depth of field
    gfx->dof.enable = false;
    gfx->dof.focalDistance = 5.0f;
    gfx->dof.focalRange = 2.0f;
    gfx->dof.maxRadius = 8.0f;
    gX_U32 dofDivisor = 1;
    const gX_I8 *dofResHook = getenv("GX_T_DOF_RES");
    if (dofResHook && atoi((const char *)dofResHook) == 1) dofDivisor = 2;
    if (dofResHook && atoi((const char *)dofResHook) >= 2) dofDivisor = 4;
    gX_RendererGFX_RT_InitSized(&gfx->dof.cocMap,
        (gX_U32)maindata.renderWidth, (gX_U32)maindata.renderHeight, true);
    gX_RendererGFX_RT_InitSized(&gfx->dof.blurMap,
        (gX_U32)maindata.renderWidth / dofDivisor,
        (gX_U32)maindata.renderHeight / dofDivisor, true);
    gX_RendererGFX_RT_InitSized(&gfx->dof.blurPing,
        (gX_U32)maindata.renderWidth / dofDivisor,
        (gX_U32)maindata.renderHeight / dofDivisor, true);
    if (dofDivisor > 1)
        gX_RendererGFX_RT_InitSized(&gfx->dof.compositeMap,
            (gX_U32)maindata.renderWidth, (gX_U32)maindata.renderHeight, true);
    GX_GFX_SHADER_INIT(gfx, dof.cocShader, "gfx_dof_coc.frag");
    GX_GFX_SHADER_INIT(gfx, dof.blurShader, "gfx_dof_blur.frag");
    GX_GFX_SHADER_INIT(gfx, dof.compositeShader, "gfx_dof_composite.frag");

    //opencode: two-mirror feedback — half-res bounce + history RTs, energy shader.
    gfx->mirror.energy = 0.90f;
    gfx->mirror.renderingPlane = -1;
    gfx->mirror.historyValid = 0;
    gfx->mirror.lastWidth = 0;
    gfx->mirror.lastHeight = 0;
    gfx->mirror.lastExposure = -1.0f;
    gfx->mirror.mvpA = gX_Matrix_Identity();
    gfx->mirror.mvpB = gX_Matrix_Identity();
    gfx->mirror.locsCached = 0;
    gX_RendererGFX_TemporalHistory_Init(&gfx->mirror.history);
    gX_RendererGFX_RT_InitSized(&gfx->mirror.freshB,
        (gX_U32)maindata.renderWidth / 2, (gX_U32)maindata.renderHeight / 2, true);
    gX_RendererGFX_RT_InitSized(&gfx->mirror.histA,
        (gX_U32)maindata.renderWidth / 2, (gX_U32)maindata.renderHeight / 2, true);
    gX_RendererGFX_RT_InitSized(&gfx->mirror.histB,
        (gX_U32)maindata.renderWidth / 2, (gX_U32)maindata.renderHeight / 2, true);
    GX_GFX_SHADER_INIT(gfx, mirror.feedbackShader, "gfx_mirror_feedback.frag");

    //opencode: TAA
    gfx->taa.enable = false;
    gfx->taa.blendFactor = 0.85f;
    gX_RendererGFX_TemporalHistory_Init(&gfx->taa.temporal);
    gfx->taa.jitter = (gX_F32v2){0.0f, 0.0f};
    gfx->taa.prevJitter = (gX_F32v2){0.0f, 0.0f};
    GX_GFX_RT_INIT_HDR(gfx, taa.historyMap);
    GX_GFX_RT_INIT_HDR(gfx, taa.resolveMap);
    GX_GFX_SHADER_INIT(gfx, taa.resolveShader, "gfx_taa_resolve.frag");

    //opencode: RT inspector (GX_RT_VIEW) — frag-only viewer shader
    gfx->rtview.locsCached = false;
    GX_GFX_SHADER_INIT(gfx, rtview.shader, "gfx_rtview.frag");

    //opencode: v0.1.38 — velocity buffer (motion vectors for TAA/SSGI)
    gfx->velocity.hasPrevVP = false;
    gfx->velocity.cameraMoved = false;
    gfx->velocity.locsCached = false;
    GX_GFX_RT_INIT_HDR(gfx, velocity.map);
    gfx->velocity.map.filter = gX_TEXTURE_FILTER_POINT;
    gX_RendererGFX_Shader_Init(&gfx->velocity.shader,
        GX_PATH("shader", "gfx_velocity.vert"),
        GX_PATH("shader", "gfx_velocity.frag"));

    //opencode: god rays — half-res gen map
    GX_GFX_RT_INIT_HALF_HDR(gfx, godray.genMap);
    //opencode: Paper 2 spike — terrain depth shader (no RT owned).
    GX_GFX_SHADER_INIT(gfx, godray.genShader, "gfx_godray_gen.frag");
    GX_GFX_SHADER_INIT(gfx, terrain.depthShader, "gfx_terrain_depth.frag");
    GX_GFX_SHADER_INIT(gfx, terrain.velShader, "gfx_terrain_velocity.frag");
    //opencode: match the former generator constants now that these are live uniforms.
    gfx->godray.density = 64;
    gfx->godray.decay = 0.9f;
    gfx->godray.weight = 0.42f;
    gfx->godray.exposure = 1.0f;

    //opencode: volumetric fog — half-res gen map + shaders
    //opencode: v0.3.8 - HDR storage: the composite delta includes negative
    //   extinction (scene*(T-1)) which LDR clamped to zero; NaNs are scrubbed
    //   in-shader now, which was LDR's original purpose here
    GX_GFX_RT_INIT_HALF_HDR(gfx, fogVolume.genMap);
    GX_GFX_SHADER_INIT(gfx, fogVolume.genShader, "gfx_fog_vol_gen.frag");
    GX_GFX_SHADER_INIT(gfx, fogVolume.blendShader, "gfx_fog_vol_blend.frag");
    //opencode: v0.3-M3 — god rays: falloff lowered so density reaches eye
    //   level (exp(-h*k): k=5 killed all fog below ~2m), HG asymmetry added
    gfx->fogVolume.density = 0.0005f;
    gfx->fogVolume.heightFalloff = 1.2f;   //opencode: v0.3.8 - 0.35 filled the whole sky (eye-band extends up indefinitely); 1.2 hugs the terrain
    gfx->fogVolume.scattering = 1.0f;
    gfx->fogVolume.hgAsymmetry = 0.85f; //opencode: judging - tighter forward
        //   lobe (was 0.7): the wide lobe washed the full-width horizon at
        //   37deg+ off-axis; shaft cores (near-axis) survive, veiling drops.

    //opencode: lens flares — single-pass shader, no RT
    GX_GFX_SHADER_INIT(gfx, lensflare.shader, "gfx_lensflare.frag");
    gfx->lensflare.intensity = 1.0f;
    gfx->lensflare.dispersal = 1.0f;

    //opencode: IBL — BRDF LUT + irradiance/prefilter (shaders only, textures deferred)
    gX_RendererGFX_Shader_Init(&gfx->ibl.brdfShader, 0, GX_PATH("shader", "gfx_ibl_brdf.frag"));
    gX_RendererGFX_Shader_Init(&gfx->ibl.irradianceShader, 0, GX_PATH("shader", "gfx_ibl_irradiance.frag"));
    gX_RendererGFX_Shader_Init(&gfx->ibl.prefilterShader, 0, GX_PATH("shader", "gfx_ibl_prefilter.frag"));
    gfx->ibl.ready = false;

    //opencode: bloom shaders and render textures
    gX_RendererGFX_Shader_Init(&gfx->bloom.extractShader, 0, GX_PATH("shader", "gfx_bloom_extract.frag"));
    gX_RendererGFX_Shader_Init(&gfx->bloom.blurHShader, 0, GX_PATH("shader", "gfx_bloom_blur_h.frag"));
    gX_RendererGFX_Shader_Init(&gfx->bloom.blurVShader, 0, GX_PATH("shader", "gfx_bloom_blur_v.frag"));
    gX_RendererGFX_Shader_Init(&gfx->bloom.compositeShader, 0, GX_PATH("shader", "gfx_bloom_composite.frag"));
    //opencode: v0.3-M1 — auto-exposure: 64x64 luminance target + defaults
    gX_RendererGFX_Shader_Init(&gfx->bloom.luminanceShader, 0, GX_PATH("shader", "gfx_luminance.frag"));
    gfx->bloom.luminanceInputLoc = -1;
    gfx->bloom.compositeDitherModeLoc = -1;
    gfx->bloom.compositeDitherSeedLoc = -1;
    gfx->bloom.compositeDitherStrengthLoc = -1;
    gX_RendererGFX_RenderTexture_Init(&gfx->bloom.luminanceRT);
    gfx->bloom.luminanceRT.wrapper.width = 64;
    gfx->bloom.luminanceRT.wrapper.height = 64;
    gfx->bloom.luminanceRT.hdr = true;
    gfx->bloom.luminanceRT.wrapper.loadingState.loadAsap = true;
    gfx->bloom.autoExposure = true;
    gfx->bloom.exposureCompensation = 1.0f;
    gfx->bloom.exposureMin = 0.25f;
    gfx->bloom.exposureMax = 6.0f;
    gfx->bloom.adaptedExposure = 1.0f;
    gfx->bloom.exposure = 1.0f;
    gfx->bloom.luminanceFrame = 0;
    gX_RendererGFX_Shader_Init(&gfx->fxaaShader, 0, GX_PATH("shader", "gfx_fxaa.frag")); //opencode: v0.1.18
    //opencode: palette quantization is opt-in; keep its shader out of the
    //   production load path and startup shader set.
    if (getenv("GX_T_PALETTE_MODE") && atoi(getenv("GX_T_PALETTE_MODE")) > 0)
        gX_RendererGFX_Shader_Init(&gfx->paletteShader, 0, GX_PATH("shader", "gfx_palette.frag"));
    //opencode: interlacing is opt-in; keep its shader out of production startup.
    if (getenv("GX_T_INTERLACE_MODE") && atoi(getenv("GX_T_INTERLACE_MODE")) > 0)
        gX_RendererGFX_Shader_Init(&gfx->interlaceShader, 0, GX_PATH("shader", "gfx_interlace.frag"));
    //ai: initialize the lazy alpha-mask shader so Battle can enable true-alpha
    //   GFX compositing after startup without recompiling the renderer.
    gfx->transparentBackground = 0;
    {
        const char *transparentBg = getenv("GX_GFX_TRANSPARENT_BG");
        if (transparentBg && transparentBg[0] == '1') gfx->transparentBackground = 1;
    }
    gX_RendererGFX_Shader_Init(&gfx->alphaMaskShader, 0, GX_PATH("shader", "gfx_alpha_mask.frag"));

    //opencode: HDR (RGBA16F) for all bloom RTs — extract/clamp at white in LDR (RGBA8)
    //   would kill bright halos and flatten the HDR feel of the rest of the renderer.
    gX_RendererGFX_RenderTexture_Init(&gfx->bloom.extract);
    gfx->bloom.extract.wrapper.width = maindata.renderWidth / 2;
    gfx->bloom.extract.wrapper.height = maindata.renderHeight / 2;
    gfx->bloom.extract.hdr = true;

    gX_RendererGFX_RenderTexture_Init(&gfx->bloom.ping);
    gfx->bloom.ping.wrapper.width = maindata.renderWidth / 2;
    gfx->bloom.ping.wrapper.height = maindata.renderHeight / 2;
    gfx->bloom.ping.hdr = true;

    gX_RendererGFX_RenderTexture_Init(&gfx->bloom.pong);
    gfx->bloom.pong.wrapper.width = maindata.renderWidth / 2;
    gfx->bloom.pong.wrapper.height = maindata.renderHeight / 2;
    gfx->bloom.pong.hdr = true;

    //opencode: full-res scene buffer for composite pass
    gX_RendererGFX_RenderTexture_Init(&gfx->bloom.scene);
    // scene buffer uses full resolution (no override needed — Init already set default;
    // but explicit assignment ensures correctness regardless of Init defaults)
    gfx->bloom.scene.wrapper.width = maindata.renderWidth;
    gfx->bloom.scene.wrapper.height = maindata.renderHeight;
    gfx->bloom.scene.hdr = true;
    gfx->bloom.scene.wrapper.loadingState.loadAsap = true;

    //opencode: sync global texture filter to all RT structs after init
    gX_RendererGFX_Textures_ApplyFilter(gfx);

    //opencode: create MSAA FBO if enabled
    gX_RendererGFX_MSAA_Create(gfx);

    //opencode: validate state after all RTs, shaders, and SSBO are initialized
    gX_RendererGFX_ValidateState(gfx);

    //opencode: AUDIT — verify all known RTs have loadAsap set
    //   Any RT not loaded will cause silent failures in the pipeline.
    #define CHECK_RT_LOADASAP(rt, name) \
        if (!(rt).wrapper.loadingState.loadAsap) \
            gX_TraceLog_GFX(gX_LOG_WARNING, "AUDIT: RT '%s' missing loadAsap=true", name);
    // Core pipeline RTs
    CHECK_RT_LOADASAP(gfx->bloom.hdrScene, "hdrScene");
    CHECK_RT_LOADASAP(gfx->bloom.luminanceRT, "luminanceRT");
    CHECK_RT_LOADASAP(gfx->sceneColorLDR, "sceneColorLDR");
    CHECK_RT_LOADASAP(gfx->bloom.scene, "bloom.scene");
    // Effects RTs (loaded when feature enabled)
    CHECK_RT_LOADASAP(gfx->ssr.map, "ssrMap");
    CHECK_RT_LOADASAP(gfx->ssao.map, "ssaoMap");
    CHECK_RT_LOADASAP(gfx->ssao.blurMap, "ssaoBlurMap");
    CHECK_RT_LOADASAP(gfx->ssao.gtaoAccumA, "gtaoAccumA");
    CHECK_RT_LOADASAP(gfx->ssao.gtaoAccumB, "gtaoAccumB");
    CHECK_RT_LOADASAP(gfx->ssao.ssgiMap, "ssgiMap");
    CHECK_RT_LOADASAP(gfx->ssao.ssgiBlurMap, "ssgiBlurMap");
    CHECK_RT_LOADASAP(gfx->ssao.ssgiAccumA, "ssgiAccumA");
    CHECK_RT_LOADASAP(gfx->ssao.ssgiAccumB, "ssgiAccumB");
    CHECK_RT_LOADASAP(gfx->reflection.planar, "reflection");
    CHECK_RT_LOADASAP(gfx->debugVis.refractionBg, "refractionBg");
    CHECK_RT_LOADASAP(gfx->motionBlur.map, "motionBlur.map");
    CHECK_RT_LOADASAP(gfx->dof.cocMap, "dof.cocMap");
    CHECK_RT_LOADASAP(gfx->dof.blurMap, "dof.blurMap");
    CHECK_RT_LOADASAP(gfx->dof.blurPing, "dof.blurPing");
    if (getenv("GX_T_DOF_RES") && atoi(getenv("GX_T_DOF_RES")) > 0)
        CHECK_RT_LOADASAP(gfx->dof.compositeMap, "dof.compositeMap");
    CHECK_RT_LOADASAP(gfx->mirror.freshB, "mirror.freshB");
    CHECK_RT_LOADASAP(gfx->mirror.histA, "mirror.histA");
    CHECK_RT_LOADASAP(gfx->mirror.histB, "mirror.histB");
    CHECK_RT_LOADASAP(gfx->taa.historyMap, "taa.historyMap");
    CHECK_RT_LOADASAP(gfx->taa.resolveMap, "taa.resolveMap");
    CHECK_RT_LOADASAP(gfx->godray.genMap, "godray.genMap");
    CHECK_RT_LOADASAP(gfx->fogVolume.genMap, "fogVolume.genMap");
    // Note: bloom.ping/bloom.pong/hdrMSAA are loaded lazily by bloom init
    #undef CHECK_RT_LOADASAP

    return 0;

}

//opencode: free RTs and GL resources — everything except heap deinits and shader objects
gX_I0 gX_RendererGFX_Unload_FreeRTs(gX_RendererGFX *gfx)
{

    gX_TraceLog_GFX(gX_LOG_INFO, "Unloading testmodel");

    gX_TraceLog_SuppressStart();

    //opencode: point-shadow storage belongs to gfx->common and is shared by
    //   every shadow-casting light; release the array and FBO once here.
    if (gfx->common.pointShadowFbo > 0)
        gX_RHI_FBO_Delete(gfx->common.pointShadowFbo);
    if (gfx->common.pointShadowArray > 0)
        gX_RHI_Tex_Delete(gfx->common.pointShadowArray);
    gfx->common.pointShadowFbo = 0;
    gfx->common.pointShadowArray = 0;
    gfx->common.pointShadowSize = 0;
    gfx->common.pointShadowInited = false;
    gfx->common.pointShadowPlaceholder = false;

    GX_GFX_RT_UNLOAD(gfx, motionBlur.map);
    GX_GFX_RT_UNLOAD(gfx, dof.cocMap);
    GX_GFX_RT_UNLOAD(gfx, dof.blurMap);
    GX_GFX_RT_UNLOAD(gfx, dof.blurPing);
    GX_GFX_RT_UNLOAD(gfx, dof.compositeMap);
    GX_GFX_RT_UNLOAD(gfx, mirror.freshB);
    GX_GFX_RT_UNLOAD(gfx, mirror.histA);
    GX_GFX_RT_UNLOAD(gfx, mirror.histB);
    GX_GFX_RT_UNLOAD(gfx, taa.historyMap);
    GX_GFX_RT_UNLOAD(gfx, taa.resolveMap);
    GX_GFX_RT_UNLOAD(gfx, godray.genMap);
    GX_GFX_RT_UNLOAD(gfx, fogVolume.genMap);
    GX_GFX_RT_UNLOAD(gfx, bloom.hdrScene);
    GX_GFX_RT_UNLOAD(gfx, bloom.luminanceRT);
    GX_GFX_RT_UNLOAD(gfx, ssao.ssgiMap);
    GX_GFX_RT_UNLOAD(gfx, ssao.ssgiBlurMap);
    GX_GFX_RT_UNLOAD(gfx, ssao.gtaoAccumA);
    GX_GFX_RT_UNLOAD(gfx, ssao.gtaoAccumB);
    GX_GFX_RT_UNLOAD(gfx, ssao.ssgiAccumA);
    GX_GFX_RT_UNLOAD(gfx, ssao.ssgiAccumB);
    GX_GFX_RT_UNLOAD(gfx, bloom.hdrMSAA);
    GX_GFX_RT_UNLOAD(gfx, reflection.planar);
    GX_GFX_RT_UNLOAD(gfx, ssr.map);
    GX_GFX_RT_UNLOAD(gfx, ssr.normalMap);
    GX_GFX_RT_UNLOAD(gfx, ssr.skyLut);
    GX_GFX_RT_UNLOAD(gfx, ssr.fogLut);
    if (gfx->ssr.hizTex.id > 0)                        gX_RHI_Tex_Delete(gfx->ssr.hizTex.id);
    if (gfx->ssr.hizFbo > 0)                          gX_RHI_FBO_Delete(gfx->ssr.hizFbo);
    gfx->ssr.hizTex.id = 0;
    gfx->ssr.hizFbo = 0;
    gfx->ssr.hizInited = false;

    GX_GFX_RT_UNLOAD(gfx, ssao.map);
    GX_GFX_RT_UNLOAD(gfx, ssao.blurMap);
    GX_GFX_RT_UNLOAD(gfx, contact.map); //opencode: contact shadows (spike 1)
    GX_GFX_RT_UNLOAD(gfx, debugVis.refractionBg);
    GX_GFX_RT_UNLOAD(gfx, sceneColorLDR);
    gX_RendererGFX_ViewportGrid_Unload(gfx);
    //opencode: dynamic cubemap cleanup
    if (gfx->reflection.cubeFboID > 0)  gX_RHI_FBO_Delete(gfx->reflection.cubeFboID);
    if (gfx->reflection.cubeDepthRbo > 0) gX_RHI_RBO_Delete(gfx->reflection.cubeDepthRbo);
    if (gfx->reflection.dynamicCubemap.id > 0) gX_RHI_Tex_Delete(gfx->reflection.dynamicCubemap.id);
    gfx->reflection.cubeFboID = 0;
    gfx->reflection.cubeDepthRbo = 0;
    gfx->reflection.dynamicCubemap.id = 0;
    gfx->reflection.cubeReady = false;
    //opencode: probe grid cleanup (mirrors the dynamic cubemap above)
    for (gX_U32 pi = 0; pi < (gX_U32)GFX_PROBE_MAX; pi++) {
        if (gfx->reflection.probes[pi].fboID > 0) gX_RHI_FBO_Delete(gfx->reflection.probes[pi].fboID);
        if (gfx->reflection.probes[pi].depthRbo > 0) gX_RHI_RBO_Delete(gfx->reflection.probes[pi].depthRbo);
        if (gfx->reflection.probes[pi].cube.id > 0) gX_RHI_Tex_Delete(gfx->reflection.probes[pi].cube.id);
        gfx->reflection.probes[pi].fboID = 0;
        gfx->reflection.probes[pi].depthRbo = 0;
        gfx->reflection.probes[pi].cube.id = 0;
        gfx->reflection.probes[pi].baked = false;
    }
    gfx->reflection.probeCount = 0;
    gfx->reflection.probesReady = false;
    GX_GFX_RT_UNLOAD(gfx, bloom.extract);
    GX_GFX_RT_UNLOAD(gfx, bloom.ping);
    GX_GFX_RT_UNLOAD(gfx, bloom.pong);
    GX_GFX_RT_UNLOAD(gfx, bloom.scene);

    //opencode: unload IBL resources
    if (gfx->ibl.brdfLut.id > 0) gX_RHI_Tex_Delete(gfx->ibl.brdfLut.id);
    if (gfx->ibl.irradianceMap.id > 0) gX_RHI_Tex_Delete(gfx->ibl.irradianceMap.id);
    if (gfx->ibl.prefilterMap.id > 0) gX_RHI_Tex_Delete(gfx->ibl.prefilterMap.id);
    if (gfx->ibl.dummyCubemap.id > 0) gX_RHI_Tex_Delete(gfx->ibl.dummyCubemap.id);
    gfx->ibl.ready = false;

    if (gfx->decalQuad.meshCount > 0) gX_Model_Unload(gfx->decalQuad);
    if (gfx->common.cookieFallback.id > 0) gX_RHI_Tex_Delete(gfx->common.cookieFallback.id);
    gX_RendererGFX_CookieAtlas_Delete(gfx);

    //opencode: unload geometry demo resources
    if (gfx->geoDemo.program.id > 0) gX_Shader_Unload(gfx->geoDemo.program);
    if (gfx->geoDemo.model.meshCount > 0) gX_Model_Unload(gfx->geoDemo.model);
    gfx->geoDemo.loaded = false;

    //opencode: unload CSM array texture + FBO
    if (gfx->csm.arrayTexId > 0) {
        gX_RHI_Tex_Delete(gfx->csm.arrayTexId);
        gfx->csm.arrayTexId = 0;
    }
    if (gfx->csm.fboId > 0) {
        gX_RHI_FBO_Delete(gfx->csm.fboId);
        gfx->csm.fboId = 0;
    }
    gfx->csm.map.wrapper.loadingState.loaded = false;

    //opencode: the Forward+ grid, SSBOs, and compute kernel are not owned by
    //   a RenderTexture wrapper; release their dedicated lifecycle here.
    gX_RendererGFX_LightClusters_Unload(gfx);

    gX_TraceLog_SuppressEnd();

}
