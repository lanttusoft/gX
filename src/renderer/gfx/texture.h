/**********************************************************************************************
*
*   [gX: src/renderer/gfx/texture.h]
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


#ifndef RENDERER_GFX_TEXTURE_H
#define RENDERER_GFX_TEXTURE_H

#include "../../common/types.h"
#include "../../common/text.h"
#include "../../common/log.h"
#include "../../wrappers/texture.h"

//opencode: consolidated — embeds Texture_Wrapper instead of duplicating its fields
typedef struct gX_RendererGFX_Texture {
    gX_Texture_Wrapper wrapper;
} gX_RendererGFX_Texture;

//opencode: consolidated — embeds RenderTexture_Wrapper instead of duplicating
typedef struct gX_RendererGFX_RenderTexture {
    gX_RenderTexture_Wrapper wrapper;
    gX_I1 hdr;      // use HDR float16 format
    gX_I32 filter;  // texture filter to apply
    gX_U32 stencilRbo; //opencode: stencil renderbuffer ID (0 = no stencil)
} gX_RendererGFX_RenderTexture;

gX_I32 gX_RendererGFX_Texture_Init(     gX_RendererGFX_Texture *texture);
gX_I32 gX_RendererGFX_Texture_Load(     gX_RendererGFX_Texture *texture);
gX_I32 gX_RendererGFX_Texture_Unload(   gX_RendererGFX_Texture *texture);

gX_I32 gX_RendererGFX_RenderTexture_Init(  gX_RendererGFX_RenderTexture *map);
gX_I32 gX_RendererGFX_RenderTexture_Load(  gX_RendererGFX_RenderTexture *map);
gX_I32 gX_RendererGFX_RenderTexture_Unload(gX_RendererGFX_RenderTexture *map);

gX_I0 gX_RendererGFX_Texture_Update(        gX_RendererGFX_Texture *texture);
gX_I0 gX_RendererGFX_RenderTexture_Update(  gX_RendererGFX_RenderTexture *map);

//opencode: forward decl for gX_RendererGFX (defined in gfx.h)
typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: sync global textureFilter to all RT structs and loaded textures.
//   Sets the filter field on every render texture in the GFX struct and applies
//   it to already-loaded GL textures. Called from Load_InitRTs and update loop.
gX_I0 gX_RendererGFX_Textures_ApplyFilter(gX_RendererGFX *gfx);

#endif //RENDERER_GFX_TEXTURE_H
