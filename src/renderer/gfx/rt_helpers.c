/**********************************************************************************************
*
*   [gX: src/renderer/gfx/rt_helpers.c]
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


//opencode: renderer-layer helpers for sized render-texture lifecycle changes.
#include "rt_helpers.h"

gX_I0 gX_RendererGFX_RT_InitSized(gX_RendererGFX_RenderTexture *map,
    gX_U32 width, gX_U32 height, gX_I1 hdr)
{
    gX_RendererGFX_RenderTexture_Init(map);
    map->wrapper.width = width;
    map->wrapper.height = height;
    map->hdr = hdr;
}

gX_I0 gX_RendererGFX_RT_ReloadSized(gX_RendererGFX_RenderTexture *map,
    gX_U32 width, gX_U32 height)
{
    if (map->wrapper.loadingState.loaded)
        gX_RendererGFX_RenderTexture_Unload(map);
    map->wrapper.width = width;
    map->wrapper.height = height;
    map->wrapper.loadingState.loaded = false;
    map->wrapper.loadingState.loadAsap = true;
    map->wrapper.loadingState.reload = false;
}
