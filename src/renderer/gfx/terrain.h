/**********************************************************************************************
*
*   [gX: src/renderer/gfx/terrain.h]
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


#ifndef RENDERER_GFX_TERRAIN_H
#define RENDERER_GFX_TERRAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "shader.h"

//opencode: Paper 2 spike — terrain-as-G-buffer-pass. Depth-only SDF march
//   writing raster-identical depth into the scene target (second.rt), so
//   every downstream screenspace effect works on terrain unchanged.
//   Hook-gated (GX_T_TERRAINDEPTH); no RT owned, depth shader only.
typedef struct gX_RendererGFX_Terrain {
    gX_RendererGFX_Shader depthShader;

    gX_I32  depthInvVPLoc;
    gX_I32  depthViewProjLoc;
    gX_I32  depthResolutionLoc;
    gX_I32  depthHillAmpLoc;
    gX_I32  depthBaseYLoc;
    //opencode: spike 2 — shared-path lighting locs (sun + ambient +
    //   exposure + first-4 point lights, same values as Shader_SetValues).
    gX_I32  depthLightPosLoc;
    gX_I32  depthSunColorLoc;
    gX_I32  depthSunBrightLoc;
    gX_I32  depthSunDiffTintLoc;
    gX_I32  depthSunDiffFactorLoc;
    gX_I32  depthAmbientColorLoc;
    gX_I32  depthAmbientFactorLoc;
    gX_I32  depthExposureLoc;
    gX_I32  depthPointPosLoc;
    gX_I32  depthPointColorLoc;
    gX_I32  depthPointBrightLoc;
    gX_I32  depthPointAttenLoc;
    gX_I32  depthPointDiffLoc;
    gX_I32  depthPointCountLoc;
    //opencode: spike 4 — texture heightfield (lazy R32F upload, CPU
    //   mirror buffer lives in terrain.c).
    gX_U32  heightTexId;
    gX_I32  heightTexLoc;
    gX_I32  heightOnLoc;
    gX_I32  heightTileLoc;
    //opencode: gap-2 shadows — CSM receive locs (mirror of the main
    //   shader's shadow/CSM locs in shader.h).
    gX_I32  shEnableLoc;
    gX_I32  shDiffBiasLoc;
    gX_I32  shSpecBiasLoc;
    gX_I32  shBiasMaxLoc;
    gX_I32  shBiasMinLoc;
    gX_I32  shMapLoc;
    gX_I32  shArrayLoc;
    gX_I32  shVPLoc;
    gX_I32  shSplitsLoc;
    gX_I32  shBlendLoc;
    gX_I32  shCascadeLoc;
    gX_I32  shSunVPLoc;
    gX_I32  shNearLoc;
    gX_I32  shFarLoc;
    //opencode: gap-3 SSAO locs (mirror of the main shader's ssao locs).
    gX_I32  shSsaoEnableLoc;
    gX_I32  shSsaoMapLoc;
    //opencode: spike 3 — terrain-velocity quad (camera motion for
    //   non-model pixels, baked into velocity.map under the models).
    gX_RendererGFX_Shader velShader;
    gX_I32  velDepthMapLoc;
    gX_I32  velInvVPLoc;
    gX_I32  velPrevVPLoc;
    gX_I32  velResolutionLoc;
    //opencode: spike 3 — VP snapshot actually consumed by the quad this
    //   frame (velocity.prevVP is already overwritten by stats time).
    gX_Matrix velUsedPrevVP;
    gX_Matrix velUsedCurrVP;
    gX_Matrix velUsedInvVP;

    gX_I1   locsCached;
} gX_RendererGFX_Terrain;

typedef struct gX_RendererGFX gX_RendererGFX;

gX_I0 gX_RendererGFX_Terrain_Draw(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Terrain_CacheLocs(gX_RendererGFX *gfx);
//opencode: planar — mirrored terrain draw into the already-bound planar
//   FBO (called from Draw_Reflections after the model loops; no
//   Begin/EndTexture inside). Stats read the planar FBO after EndTexture.
gX_I0 gX_RendererGFX_Terrain_DrawMirror(gX_RendererGFX *gfx, gX_Matrix mirrorVP);
gX_I0 gX_RendererGFX_Terrain_PlanarStats(gX_RendererGFX *gfx, gX_U32 planarFbo);
//opencode: spike 3 — quad runs inside Velocity_Draw (after clear, before
//   models); stats run from the pass after models (fresh buffer).
gX_I0 gX_RendererGFX_Terrain_VelocityQuad(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Terrain_VelocityStats(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_TERRAIN_H
