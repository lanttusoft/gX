/**********************************************************************************************
*
*   [gX: src/renderer/gfx/mirror.h]
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


#ifndef RENDERER_GFX_MIRROR_H
#define RENDERER_GFX_MIRROR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "../../gxmath/matrix.h"
#include "shader.h"
#include "texture.h"
#include "temporal_history.h"

//opencode: two-mirror temporal feedback — bounded ping-pong regress for
//   facing mirrors. Recursion budget: 1 stored bounce per mirror (regress
//   depth grows with settled frames, energy-capped). Validity is frame-
//   global and conservative: camera, size, or exposure change clears both
//   histories (black rebuild fallback); per-pixel velocity masking is
//   deferred. Plane 0 reuses the legacy full-res planar RT (oracle parity);
//   plane 1 + both histories are half-res. All heap/RT state — no BSS.
typedef struct gX_RendererGFX_Mirror {
    gX_RendererGFX_RenderTexture freshB;
    gX_RendererGFX_RenderTexture histA;
    gX_RendererGFX_RenderTexture histB;
    gX_RendererGFX_Shader feedbackShader;
    gX_RendererGFX_TemporalHistory history;
    gX_F32 energy;
    gX_I32 renderingPlane;
    gX_I1 historyValid;
    gX_Matrix lastVP;
    gX_I32 lastWidth;
    gX_I32 lastHeight;
    gX_F32 lastExposure;
    gX_Matrix mvpA;
    gX_Matrix mvpB;
    gX_I32 histMapLoc;
    gX_I32 energyLoc;
    gX_I1 locsCached;
} gX_RendererGFX_Mirror;

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: pair active — fixture hook set, RTs loaded.
gX_I1 gX_RendererGFX_Mirror_Active(gX_RendererGFX *gfx);
//opencode: frame setup — validity check + history clear on change.
gX_I0 gX_RendererGFX_Mirror_BeginFrame(gX_RendererGFX *gfx);
//opencode: per-plane render target (0 = legacy planar RT, 1 = freshB).
gX_RenderTexture *gX_RendererGFX_Mirror_PlaneTarget(gX_RendererGFX *gfx, gX_I32 plane);
//opencode: record the mirror VP used for a plane render.
gX_I0 gX_RendererGFX_Mirror_StoreMvp(gX_RendererGFX *gfx, gX_I32 plane, const gX_Matrix *mvp);
//opencode: counterpart test — slab tagged for the other plane.
gX_I1 gX_RendererGFX_Mirror_IsCounterpart(gX_RendererGFX *gfx, gX_I32 mirrorPair);
//opencode: feedback commit — fresh to history with energy decay.
gX_I0 gX_RendererGFX_Mirror_Commit(gX_RendererGFX *gfx);
//opencode: sampling getters for the model bind (fresh in main, history in mirror).
gX_Texture gX_RendererGFX_Mirror_FreshTex(gX_RendererGFX *gfx, gX_I32 plane);
gX_Matrix gX_RendererGFX_Mirror_FreshMvp(gX_RendererGFX *gfx, gX_I32 plane);
gX_Texture gX_RendererGFX_Mirror_HistTex(gX_RendererGFX *gfx, gX_I32 plane);
gX_Matrix gX_RendererGFX_Mirror_HistMvp(gX_RendererGFX *gfx, gX_I32 plane);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_MIRROR_H
