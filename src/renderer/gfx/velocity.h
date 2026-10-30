/**********************************************************************************************
*
*   [gX: src/renderer/gfx/velocity.h]
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


#ifndef RENDERER_GFX_VELOCITY_H
#define RENDERER_GFX_VELOCITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "shader.h"
#include "texture.h"

//opencode: v0.1.38 — per-pixel motion vector buffer (TAA/SSGI input).
//   R/G carry UV motion, B marks written vectors and carries previous depth
//   for raster/terrain producers, and A carries the glass flag. Renders opaque
//   geometry a second time with the cheapest possible
//   shader: velocity = 0.5*(currNDC - prevNDC) in UV space, computed from the
//   current and previous view-projections (both UNJITTERED — Halton jitter is
//   added by standard.vert at render time and compensated explicitly by
//   consumers).
//ai: opt-in shared SDF ownership uses B=-1 for static camera fallback and
//   B=-2 to reject animated SDF history; B=2+previous depth is still >0.5 for
//   existing written-vector consumers, while legacy water/particle writers use B=1.
typedef struct gX_RendererGFX_Velocity {
    gX_RendererGFX_Shader shader;
    gX_RendererGFX_RenderTexture map;    //opencode: HDR RGBA16F; RG motion, B validity/depth payload, A glass

    gX_Matrix prevVP;                    //opencode: previous frame VP (unjittered)
    gX_I1      hasPrevVP;
    gX_I1      cameraMoved;

    gX_I32 currVPLoc;
    gX_I32 prevVPLoc;
    gX_I32 modelLoc;
    gX_I32 prevModelLoc;
    gX_I32 glassLoc; //opencode: M1-b glass flag (uGlass)
    gX_I1  locsCached;
} gX_RendererGFX_Velocity;

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: render the velocity buffer (after opaque pass)
gX_I0 gX_RendererGFX_Velocity_Draw(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_VELOCITY_H
