/**********************************************************************************************
*
*   [gX: src/renderer/ws2d.h]
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


#ifndef RENDERER_WS2D_H
#define RENDERER_WS2D_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

//opencode: world-space 2D module — managed render textures for use as
//   GFX model materials. Each WS2D entry is a named render texture that
//   can be drawn to with the existing 2D drawing API (gX_Rectangle_Draw,
//   gX_Texture_Draw, gX_RawString_Draw, etc.) and then used as a texture
//   on a 3D model rendered by the GFX module.
//
//   Usage:
//     gX_I32 h = gX_WS2D_Create("tv_screen", 256, 256);
//     gX_WS2D_Begin(h);
//       gX_WS2D_Clear(h, (gX_Color){0,0,0,255});
//       gX_Rectangle_Draw(...);
//       gX_RawString_Draw(...);
//     gX_WS2D_End(h);
//     gX_Model_SetMaterialTexture(&model, 0, gX_WS2D_GetTexture(h));
//     gX_WS2D_Destroy(h);

#define GX_WS2D_MAX_ENTRIES 16
#define GX_WS2D_NAME_MAX 64

typedef struct gX_WS2D_Entry {
    gX_I8    name[GX_WS2D_NAME_MAX];
    gX_I1    active;
    gX_U16   width;
    gX_U16   height;
    gX_RenderTexture rt;
} gX_WS2D_Entry;

typedef struct gX_RendererWS2D {
    gX_I8 name[128];
    gX_I8 description[256];
    gX_LoadingState loadingstate;

    //opencode: fixed-size array of managed render textures
    gX_WS2D_Entry entries[GX_WS2D_MAX_ENTRIES];
    gX_U32 entryCount;
} gX_RendererWS2D;

// Module lifecycle (called by renderer module system)
gX_I32 gX_RendererWS2D_Load(gX_RendererWS2D *ws2d);
gX_I32 gX_RendererWS2D_Unload(gX_RendererWS2D *ws2d);
gX_I0  gX_RendererWS2D_Update(gX_RendererWS2D *ws2d);
gX_I0  gX_RendererWS2D_DrawTo(gX_RendererWS2D *ws2d, gX_RenderTexture *outputRT);  // no-op (canvas-less)
#define gX_RendererWS2D_Draw(ws2d) gX_RendererWS2D_DrawTo((ws2d), NULL)

// Public API — create/destroy named render textures
gX_I32 gX_WS2D_Create(const gX_I8 *name, gX_U16 w, gX_U16 h);
gX_I0  gX_WS2D_Destroy(gX_I32 handle);
gX_Texture gX_WS2D_GetTexture(gX_I32 handle);

// Drawing — bind RT and clear. Between Begin/End, use any gX_Drawing_* calls.
gX_I0 gX_WS2D_Begin(gX_I32 handle);
gX_I0 gX_WS2D_End(gX_I32 handle);
gX_I0 gX_WS2D_Clear(gX_I32 handle, gX_Color color);

// Query
gX_I32 gX_WS2D_Find(const gX_I8 *name);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_WS2D_H
