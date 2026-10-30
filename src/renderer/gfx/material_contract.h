/**********************************************************************************************
*
*   [gX: src/renderer/gfx/material_contract.h]
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

#ifndef GX_RENDERER_GFX_MATERIAL_CONTRACT_H
#define GX_RENDERER_GFX_MATERIAL_CONTRACT_H

#include "../../common/types.h"

//ai: map raylib's native table to the source GLB table; metadata-cache size is a separate bound.
static inline gX_I32 gfx_glb_material_index(gX_I32 NativeIndex, gX_I32 NativeCount,
    gX_I32 SourceCount)
{
    if (NativeIndex < 0 || NativeIndex >= NativeCount || SourceCount <= 0) return -1;
    gX_I32 Offset = NativeCount > SourceCount ? NativeCount - SourceCount : 0;
    gX_I32 Index = NativeIndex - Offset;
    return Index >= 0 && Index < SourceCount ? Index : -1;
}

#endif
