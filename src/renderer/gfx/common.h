/**********************************************************************************************
*
*   [gX: src/renderer/gfx/common.h]
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



#ifndef RENDERER_GFX_COMMON_H
#define RENDERER_GFX_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "constants.h"
#include "../../common/types.h"

//opencode: shared utilities used across the GFX pipeline.
//   These don't belong to any single subsystem.
typedef struct gX_RendererGFX_Common {
    gX_Texture  cookieFallback;
    //opencode: cookie atlas — packs all enabled light cookies into one
    //   texture (4x4 grid of 256px cells) so every spot light gets its own
    //   cookie instead of the first-enabled-only single sampler.
    gX_U32      cookieAtlasTex;
    gX_I1       cookieAtlasDirty;
#ifdef GX_COMPUTE
    gX_Rectangle cookieRects[gX_GFX_MAX_LIGHTS_SSBO]; //opencode: v0.5.2 — atlas rects for the SSBO range
#else
    gX_Rectangle cookieRects[gX_GFX_MAX_LIGHTS];
#endif
    //opencode: shared point shadow 2D array (6 layers per shadow light)
    gX_U32      pointShadowArray;
    gX_U32      pointShadowFbo;
    gX_I32      pointShadowSize;
    gX_I1       pointShadowInited;
    //opencode: Phase 1b — placeholder mode: pointShadowArray is a tiny
    //   1-layer array (sampler stays valid, farPlanes=0 → never sampled)
    //   until the first shadow-casting point light upgrades it to full.
    gX_I1       pointShadowPlaceholder;
    gX_RenderTexture *compositeTarget;
    gX_Model    sphereMarker;
    gX_I1       sphereMarkerLoaded;
    gX_I1       demoSceneAdded;    //opencode: --gfx/--showcase demo content added once
    gX_I1       battleScene;
    gX_I1       physicsScene;      //ai: physics sandbox owns the heap content
    gX_I32      pickedModelIndex;
} gX_RendererGFX_Common;

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_COMMON_H
