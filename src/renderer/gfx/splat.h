/**********************************************************************************************
*
*   [gX: src/renderer/gfx/splat.h]
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


#ifndef RENDERER_GFX_SPLAT_H
#define RENDERER_GFX_SPLAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "../../gxmath/matrix.h"
#include "../../gxmath/random.h"

//opencode: 3D gaussian splat research fixture — a genuine miniature of the
//   splat rasterizer (perspective-projected 2D covariance, conic falloff,
//   back-to-front sorted alpha compositing) over a procedural seeded cloud.
//   No assets: fibonacci-sphere shell, normal-mapped colors. GL33-safe by
//   design (plain triangles, GLSL 330). Reconstruction-from-data (.ply
//   loading, tile rasterizer) is explicitly deferred until this fixture
//   demonstrates stable value over planar, SSR, and cubemap paths.
#define gX_SPLAT_MAX 1024

typedef struct gX_RendererGFX_Splat {
    gX_U32 seed;
    gX_Random32 rng;   //opencode: per-splat PCG stream (no shared rand())
    gX_I32 targetCount;
    gX_I32 activeCount;
    gX_F32 sigma;
    gX_F32 centerX;
    gX_F32 centerY;
    gX_F32 centerZ;
    gX_F32 radius;
    gX_F32 *posX;
    gX_F32 *posY;
    gX_F32 *posZ;
    gX_F32 *colR;
    gX_F32 *colG;
    gX_F32 *colB;
    gX_F32 *depth;
    gX_F32 *conic;
    gX_F32 *eigenU;
    gX_F32 *eigenV;
    gX_U32 *order;
    gX_F32 *upload;
    gX_U32 vbo;
    gX_U32 vao;
    gX_U32 renderProg;
    gX_I1 loaded;
    gX_I1 locsCached;
    gX_I32 uVP;
    gX_I32 uHalfView;
    gX_I32 uAxisU;
    gX_I32 uAxisV;
} gX_RendererGFX_Splat;

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: splat lifecycle — init defaults, per-frame env/LOD, quad draw.
gX_I0 gX_RendererGFX_Splat_Init(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Splat_Unload(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Splat_Update(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Splat_Draw(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_SPLAT_H
