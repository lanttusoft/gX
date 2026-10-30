/**********************************************************************************************
*
*   [gX: src/renderer/gfx/fog_volume.h]
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


#ifndef RENDERER_GFX_FOG_VOLUME_H
#define RENDERER_GFX_FOG_VOLUME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "../../gxmath/vector3.h"
#include "../../gxmath/matrix.h"
#include "constants.h"
#include "shader.h"
#include "texture.h"

//opencode: volumetric fog — screen-space raymarch through a height-based density
//   volume with sun inscattering. Two-pass: gen (half-res accumulation) → blend
//   (add over scene). Each pixel marches from camera to scene surface, sampling
//   the depth buffer to determine fog density and inscattering along the ray.
typedef struct gX_RendererGFX_FogVolume {
    gX_I1       enable;       //opencode: master toggle
    gX_F32      density;      //opencode: global fog density (0.001-0.1, default 0.01)
    gX_F32      heightFalloff;//opencode: height decay (lower = fog stays low, default 5.0)
    gX_F32      scattering;   //opencode: sun-scattering strength (0.0-1.0, default 0.5)
    gX_F32      hgAsymmetry;  //opencode: v0.3-M3 — HG forward-scatter lobe (default 0.55)

    gX_RendererGFX_Shader genShader;
    gX_RendererGFX_Shader blendShader;

    gX_RendererGFX_RenderTexture genMap;  //opencode: half-res fog accumulation

    gX_I32  genSceneDepthLoc;
    gX_I32  genSceneColorLoc;
    gX_I32  genSunDirLoc;
    gX_I32  genSunColorLoc;
    gX_I32  genSunBrightnessLoc;
    gX_I32  genDensityLoc;
    gX_I32  genHeightFalloffLoc;
    gX_I32  genScatteringLoc;
    gX_I32  genResLoc;
    gX_I32  genCamPosLoc;
    gX_I32  genInvProjLoc;
    gX_I32  genInvViewLoc;
    //opencode: v0.3-M3 — god rays: CSM shadow sampling + HG phase
    gX_I32  genCsmArrayLoc;
    gX_I32  genShadowMapLoc;
    gX_I32  genCsmVPLoc;
    gX_I32  genCsmSplitsLoc;
    gX_I32  genCascadeCountLoc;
    gX_I32  genLayer0MatLoc;
    gX_I32  genHgLoc;
    //opencode: v0.3.8 - plain-2D copies of cascade layers 0..3 (the fog gen
    //   program cannot declare sampler2DArray - see architecture.md M4
    //   blocker - so layers are blitted to plain depth textures per frame)
    gX_U32  csmCopyTex[4];
    gX_U32  csmCopyFbo[4];
    gX_U32  csmSrcFbo;
    gX_I1   copiesInited;
    gX_I32  genCsmTex1Loc;
    gX_I32  genCsmTex2Loc;
    gX_I32  genCsmTex3Loc;
    gX_I32  genCsmVp1Loc;
    gX_I32  genCsmVp2Loc;
    gX_I32  genCsmVp3Loc;

    gX_I32  blendSceneColorLoc;
    gX_I32  blendFogMapLoc;
    gX_I32  blendSunColorLoc;

    gX_I1   locsCached;
    gX_U32  copiesFrame;    //opencode: v0.3.8 - frame stamp for cascade copies
    gX_I32  genLutModeLoc;  //opencode: v0.3.8
    gX_I32  genClampLdrLoc; //opencode: O-blowout - LDR cap for post-ACES consumers
    gX_I32  genPointCountLoc;   //opencode: item-13 - point-light array locs
    gX_I32  genPointPosLoc;
    gX_I32  genPointColorLoc;
    gX_I32  genPointAttenLoc;
    gX_I32  genSpotDirLoc;      //opencode: spot cone array (axis + cosHalf)
    //opencode: point-shadow atlas locs (plain-2D tile copies, M3.1-safe)
    gX_I32  genPointShadowAtlasLoc;
    gX_I32  genPointShadowTileLoc;
    gX_I32  genPointShadowTileWLoc;
    gX_I32  genPointShadowBiasLoc;
    //opencode: point-shadow atlas (6 tiles x up-to-4 casters, 512px each =
    //   12288x512 D24; lazy, only with shadow-casting points + fog)
    gX_U32  ptShadowAtlasTex;
    gX_U32  ptShadowAtlasFbo;
    gX_U32  ptShadowSrcFbo;
    gX_I1   ptShadowInited;
    gX_U32  ptShadowFrame;    //opencode: per-frame copy stamp (cascade pattern)
    gX_F32  ptShadowTiles[GX_FOGVOL_MAX_POINT_LIGHTS * 4];
    gX_I32  ptShadowHeapIdx[GX_FOGVOL_MAX_POINT_LIGHTS];
    gX_I1   enablePointLights;  //opencode: item-13 - hook-gated (default on)
} gX_RendererGFX_FogVolume;

typedef struct gX_RendererGFX gX_RendererGFX;

gX_I0 gX_RendererGFX_FogVolume_Draw(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_FogVolume_UpdateCascadeCopies(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_FogVolume_LutPass(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_FogVolume_PlanarFog(gX_RendererGFX *gfx, gX_F32v3 camPos,
    gX_F32 fovy, gX_Matrix invView, gX_Matrix invProj, gX_U32 depthTexId);
gX_I0 gX_RendererGFX_FogVolume_CacheLocs(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_FOG_VOLUME_H
