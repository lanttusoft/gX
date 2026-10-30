/**********************************************************************************************
*
*   [gX: src/renderer/gfx/load.h]
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


#ifndef RENDERER_GFX_LOAD_H
#define RENDERER_GFX_LOAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "texture.h"

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: init RTs, set defaults, free RTs
gX_I32 gX_RendererGFX_Load_InitRTs(gX_RendererGFX *gfx);
gX_I0  gX_RendererGFX_Load_SetDefaults(gX_RendererGFX *gfx);
gX_I32 gX_RendererGFX_ValidateState(   gX_RendererGFX *gfx);
gX_I0  gX_RendererGFX_Unload_FreeRTs(gX_RendererGFX *gfx);

//opencode: MSAA FBO creation — called from InitRTs and when msaaLevel changes
gX_I0  gX_RendererGFX_MSAA_Create(gX_RendererGFX *gfx);

//opencode: quality preset — cascades to textureFilter, shaderQuality, msaaLevel, mipmapBias
gX_I0  gX_RendererGFX_ApplyQualityPreset(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_LOAD_H
