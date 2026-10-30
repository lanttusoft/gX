/**********************************************************************************************
*
*   [gX: src/renderer/gfx/decal.c]
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


//opencode: extracted from gfx.c — decal system
#include <stdlib.h>

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../gfx.h"
#include "boilerplate.h"
#include "../../main.h"
#include "decal.h"

//opencode: add a decal at pos with normal, size, texture, and lifetime
gX_I32 gX_RendererGFX_Decal_Add(gX_RendererGFX *gfx, gX_F32v3 pos, gX_F32v3 normal,
    gX_F32 size, gX_I32 textureIndex, gX_F64 lifetime)
{
    gX_I32 index = -1;
    gX_RendererGFX_Decal *d = gX_Heap_Add_FirstEmpty_NoPtr_GetIndex(&gfx->decals, &index);
    if (!d) return -1;
    d->pos = pos;
    d->normal = normal;
    d->size = size;
    //opencode: random rotation around normal for variety — a decal-local
    //   PCG stream (seeded once) keeps it deterministic and immune to other
    //   consumers' draws, unlike the shared rand() state.
    static gX_Random32 decalRng;
    static gX_I1 decalRngSeeded = 0;
    if (!decalRngSeeded) {
        gX_Random32_Seed(&decalRng, 0xDECA1u, 0u);
        decalRngSeeded = 1;
    }
    d->rotation = (gX_F32)gX_Random32_Bounded(&decalRng, 360u) * 0.0174533f;
    d->textureIndex = textureIndex;
    d->alpha = 1.0f;
    d->color = gX_WHITE;
    d->snowLayer = 0;
    d->expireTime = (lifetime < 0.0) ? -1.0 : maindata.timeElapsed + lifetime;
    return index;
}

//opencode: update decal lifetimes — expired decals are marked unused
gX_I0 gX_RendererGFX_Decal_UpdateAll(gX_RendererGFX *gfx)
{
    GX_HEAP_FOREACH(gfx->decals, gX_RendererGFX_Decal, d) {
        if (d->expireTime > 0.0 && maindata.timeElapsed >= d->expireTime)
        {
            gX_U32 di = (gX_U32)(d - (gX_RendererGFX_Decal*)gfx->decals.data);
            gfx->decals.meta[di].USED = false;
        }
    }
}

//opencode: remove all decals
gX_I0 gX_RendererGFX_Decal_RemoveAll(gX_RendererGFX *gfx)
{
    GX_HEAP_REMOVE_ALL(gfx->decals);
}
