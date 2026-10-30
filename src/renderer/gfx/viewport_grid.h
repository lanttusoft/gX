/**********************************************************************************************
*
*   [gX: src/renderer/gfx/viewport_grid.h]
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


#ifndef RENDERER_GFX_VIEWPORT_GRID_H
#define RENDERER_GFX_VIEWPORT_GRID_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"

#define GX_GFX_VIEWPORT_GRID_COUNT 4

typedef struct gX_RendererGFX_ViewportGrid {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_RenderTexture targets[GX_GFX_VIEWPORT_GRID_COUNT];
    gX_U32 width;
    gX_U32 height;
    gX_I1 loaded;
    gX_I1 rendering;
} gX_RendererGFX_ViewportGrid;

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: opt-in GUI viewport dashboard. It shares one GFX scene and renders
//   four camera views into half-resolution RTs; it is intentionally not the
//   later independent-scene/temporal-context architecture.
gX_I1 gX_RendererGFX_ViewportGrid_Enabled(gX_I0);
gX_I0 gX_RendererGFX_ViewportGrid_Init(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_ViewportGrid_Unload(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_ViewportGrid_Draw(gX_RendererGFX *gfx);
gX_Texture *gX_RendererGFX_ViewportGrid_GetTexture(gX_RendererGFX *gfx,
    gX_U32 index);

#ifdef __cplusplus
}
#endif

#endif // RENDERER_GFX_VIEWPORT_GRID_H
