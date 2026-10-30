/**********************************************************************************************
*
*   [gX: src/renderer/gfx/draw.h]
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


#ifndef RENDERER_GFX_DRAW_H
#define RENDERER_GFX_DRAW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "texture.h"

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: Phase 1 — MSAA resolve copies the NAMED hdrMSAA target into
//   hdrScene (no handle swapping). No-op when MSAA is off.
gX_I0 gX_RendererGFX_MSAA_Resolve(gX_RendererGFX *gfx);

//opencode: per-pass draw wrappers for the main render loop
gX_I0 gX_RendererGFX_Draw_Shadow(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Draw_PointShadows(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Draw_Reflections(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Draw_Sky(gX_RendererGFX *gfx);
//opencode: shared procedural sky draw — fullscreen (equirect=0) or SSR sky LUT (equirect=1)
gX_I0 gX_RendererGFX_Sky_DrawTo(gX_RendererGFX *gfx, gX_RenderTexture target, gX_I1 equirect);
gX_I0 gX_RendererGFX_SSR_HiZ_Chain(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_SSR_DrawNormals(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Draw_Opaque(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Draw_SSR(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Draw_Transparent(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Draw_Decals(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Draw_DebugOverlays(gX_RendererGFX *gfx, gX_RenderTexture *overlayTgt);
gX_I0 gX_RendererGFX_Draw_TextLabels(   gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Draw_SSAO(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_SSGI_Draw(gX_RendererGFX *gfx);

//opencode: water surface — drawn after opaque pass
gX_I0 gX_RendererGFX_Draw_Water(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_DRAW_H
