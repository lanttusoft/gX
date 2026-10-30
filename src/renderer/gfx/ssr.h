/**********************************************************************************************
*
*   [gX: src/renderer/gfx/ssr.h]
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


#ifndef RENDERER_GFX_SSR_H
#define RENDERER_GFX_SSR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "shader.h"
#include "texture.h"

//opencode: screen-space reflections — ray-marched reflection from depth buffer.
//   Self-contained sub-struct with RTs, shaders, uniform locations, and tuning.
typedef struct gX_RendererGFX_SSR {
    //opencode: render targets
    gX_RendererGFX_RenderTexture map;           // SSR ray-march output
    gX_RendererGFX_RenderTexture normalMap;     //opencode: opt-in interpolated model normals
    gX_RendererGFX_RenderTexture skyLut;        // 128x64 equirect sky fallback LUT
    gX_RendererGFX_RenderTexture fogLut;        //opencode: v0.3.8 - 128x64 equirect fog delta LUT (sky escapes)

    //opencode: min-reduced Hi-Z depth chain (dedicated R32F mip pyramid)
    gX_Texture  hizTex;
    gX_U32      hizFbo;
    gX_I32      hizLevels;
    gX_I1       hizInited;

    //opencode: v0.1.24 — set by Draw_SSR for the frame that actually ran the
    //   ray march. The march is deliberately SKIPPED when a planar-reflection
    //   model owns the reflections (water / showcase-planar), and the map RT
    //   then holds stale content — the composite pass must check this flag
    //   instead of only ENABLE_SSR, or it paints the stale map over the frame.
    gX_I1       drewThisFrame;
    gX_I1       normalBufferEnabled;            //opencode: GX_SSR_NORMALS=1 opt-in
    gX_I1       normalPass;                     //opencode: main shader emits normal only

    //opencode: shaders
    gX_RendererGFX_Shader shader;               // ray-march SSR
    gX_RendererGFX_Shader hizShader;            // Hi-Z copy + min-reduce

    //opencode: SSR shader uniform locations
    gX_I32  depthMapLoc;
    gX_I32  colorMapLoc;
    gX_I32  normalMapLoc;                       //opencode: interpolated normal input
    gX_I32  hasNormalMapLoc;
    gX_I32  normalFallbackAngleLoc;             //opencode: opt-in edge fallback threshold
    gX_I32  normalLiftModeLoc;                  //opencode: opt-in lift-off experiment
    gX_I32  normalLiftHybridAngleLoc;           //opencode: hybrid lift angle threshold
    gX_I32  normalAcceptModeLoc;                //opencode: opt-in hit acceptance experiment
    gX_I32  selfPlaneRejectEnabledLoc;          //opencode: edge-ray self-plane A/B
    gX_I32  vpLoc;
    gX_I32  invVPLoc;
    gX_I32  camPosLoc;
    gX_I32  debugModeLoc;
    gX_I32  hizMapLoc;
    gX_I32  skyLutMapLoc;
    gX_I32  fogLutMapLoc;       //opencode: v0.3.8
    gX_I32  hasFogLutLoc;       //opencode: v0.3.8
    gX_I32  sunDirLoc;
    gX_I32  sunSizeLoc;
    gX_I32  sunColorLoc;
    gX_I32  waterYLoc;
    gX_I32  hasWaterLoc;
    gX_I32  waterWaveCountLoc;
    gX_I32  waterWaveDirsLoc;
    gX_I32  waterWaveAmpsLoc;
    gX_I32  waterWaveFreqsLoc;
    gX_I32  waterWaveSpeedsLoc;
    gX_I32  waterWaveSteepsLoc;
    gX_I32  waterRippleCountLoc;
    gX_I32  waterRipplePosLoc;
    gX_I32  waterRippleAmpLoc;
    gX_I32  waterRippleRadiusLoc;
    gX_I32  waterRipplePhaseLoc;
    gX_I32  waterTimeLoc;
    gX_I32  envMapLoc;       //opencode: dynamic reflection cubemap fallback (v0.1.11)
    gX_I32  hasEnvMapLoc;
    gX_I32  velMapLoc;       //opencode: M1-b velocity.w glass flag
    gX_I32  hasVelMapLoc;
    gX_I32  glassOnLoc;      //opencode: M1-b gate (GX_T_SSRGLASS)
    //ai: cached opt-in shared-hybrid gap traversal control.
    gX_I32  hybridGapTraversalLoc;
    gX_I32  edgeFadeLoc;
    gX_I32  maxBouncesLoc;
    gX_I32  bounceAttenLoc;

    //opencode: SSR tuning uniform locations
    gX_I32  maxStepsLoc;
    gX_I32  stepSizeLoc;
    gX_I32  maxDistanceLoc;
    gX_I32  depthBiasLoc;
    gX_I32  opacityLoc;
    gX_I32  planarMapLoc;       //opencode: v0.3.4 auxview
    gX_I32  hasPlanarLoc;       //opencode: v0.3.4 auxview
    gX_I32  planarDepthMapLoc;  //opencode: audit — planar depth validity mask
    gX_I32  hasPlanarDepthLoc;  //opencode: audit — depth-mask enable
    gX_I32  planarVPLoc;        //opencode: v0.3.4 auxview mirror VP
    gX_I32  planeDistLoc;       //opencode: v0.3.6 planar precision
    gX_I32  planeNormalLoc;     //opencode: v0.3.6
    gX_I32  planarDeferLoc;     //opencode: hunt - planar defer switch
    gX_I32  roughnessLoc;
    gX_I32  maxLodLoc;
    gX_I32  maxDepthDeltaLoc;       //opencode: silhouette crossing threshold
    gX_I32  maxDepthDistanceScaleLoc; //opencode: opt-in distance-scale A/B
    gX_I32  worldGapThresholdLoc;   //opencode: world-space hit threshold
    gX_I32  finalDepthThresholdLoc; //opencode: post-bisection depth threshold
    gX_I32  hitFacingThresholdLoc;  //opencode: hit normal facing threshold


    //opencode: Hi-Z shader uniform locations
    gX_I32  hizDepthMapLoc;
    gX_I32  hizHizMapLoc;
    gX_I32  hizModeLoc;
    gX_I32  hizSrcLevelLoc;

    //opencode: state
    gX_I1   locsCached;
    gX_I1   hizLocsCached;

    //opencode: tuning parameters (was in state; self-contained keeps SSR modular)
    gX_I32  maxSteps;
    gX_F32  stepSize;
    gX_F32  maxDistance;
    gX_F32  depthBias;
    gX_F32  opacity;
    gX_F32  roughness;
    gX_F32  maxLod;
    gX_F32  edgeFade;        //opencode: screen-edge dissolve band (v0.1.11)
    gX_I32  maxBounces;      //opencode: extra ray bounces after the first hit
    gX_F32  bounceAtten;     //opencode: per-bounce energy attenuation
    gX_F32  maxDepthDelta;       //opencode: silhouette crossing threshold
    gX_F32  worldGapThreshold;   //opencode: world-space hit threshold
    gX_F32  finalDepthThreshold; //opencode: post-bisection depth threshold
    gX_F32  hitFacingThreshold;  //opencode: hit normal facing threshold

} gX_RendererGFX_SSR;

typedef struct gX_RendererGFX gX_RendererGFX;

gX_I0 gX_RendererGFX_SSR_CacheLocs(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_SSR_H
