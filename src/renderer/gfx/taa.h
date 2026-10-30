/**********************************************************************************************
*
*   [gX: src/renderer/gfx/taa.h]
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


#ifndef RENDERER_GFX_TAA_H
#define RENDERER_GFX_TAA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "shader.h"
#include "texture.h"
#include "temporal_history.h"

//opencode: TAA (temporal anti-aliasing) — frame accumulation with jitter + history resolve
typedef struct gX_RendererGFX_TAA {
    gX_I1   enable;
    gX_F32  blendFactor;
    gX_RendererGFX_TemporalHistory temporal;

    gX_RendererGFX_Shader resolveShader;
    gX_RendererGFX_RenderTexture historyMap;
    gX_RendererGFX_RenderTexture resolveMap;

    gX_F32v2 jitter;
    gX_F32v2 prevJitter;

    gX_I32  colorMapLoc;
    gX_I32  depthMapLoc;
    gX_I32  historyMapLoc;
    gX_I32  prevVPLoc;
    gX_I32  invVPLoc;
    //ai: unsafe inverse/snapshot metadata selects current-frame output without invalid matrix uploads.
    gX_I32  reprojectionValidLoc;
    gX_I32  resolutionLoc;
    gX_I32  blendFactorLoc;
    gX_I32  jitterLoc;
    gX_I32  prevJitterLoc;
    gX_I32  velocityMapLoc;   //opencode: v0.1.38 motion vector buffer
    gX_I32  hasVelocityLoc;   //opencode: v0.1.38 buffer-present flag
    gX_I32  oracleAccumLoc;   //opencode: oracle (Paper 1) clamp bypass
    //ai: retained native hybrid reprojection diagnostics.
    gX_I32  hybridReprojectionDebugLoc;
    //ai: invalidate paused SDF history on authoring/routing changes or skipped resolve frames.
    gX_U64  hybridSignature;
    //ai: advances only after a successful resolve commit, for raster and hybrid history alike.
    gX_U64  lastResolveFrame;
    gX_I1   hybridHistoryActive;
    gX_I1   locsCached;
} gX_RendererGFX_TAA;

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: TAA resolve and loc caching (jitter is computed inline in
//   update.c BEFORE the shader update; the old UpdateJitter helper was
//   dead code - defined, never called - and is removed)
gX_I0 gX_RendererGFX_TAA_Resolve(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_TAA_CacheLocs(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_TAA_H
