/**********************************************************************************************
*
*   [gX: src/renderer/gfx/decal.h]
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


#ifndef RENDERER_GFX_DECAL_H
#define RENDERER_GFX_DECAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: decal management
gX_I32 gX_RendererGFX_Decal_Add(gX_RendererGFX *gfx, gX_F32v3 pos, gX_F32v3 normal, gX_F32 size, gX_I32 textureIndex, gX_F64 lifetime);
gX_I0  gX_RendererGFX_Decal_UpdateAll(gX_RendererGFX *gfx);
gX_I0  gX_RendererGFX_Decal_RemoveAll(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_DECAL_H
