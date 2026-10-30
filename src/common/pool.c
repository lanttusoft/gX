/**********************************************************************************************
*
*   [gX: src/common/pool.c]
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


//opencode: standalone pool allocator implementation.
// Zero-malloc design: caller provides pre-allocated data + generation buffers.
// Pool_Alloc returns a handle with the current frame's generation stamp.
// Pool_Get validates the handle's generation against the slot's generation
// to detect stale handles (pool was cleared since alloc).
// Pool_Clear only resets count — data and generations stay intact.

#include "pool.h"

//=============================================================================
// Init
//=============================================================================

gX_I0 gX_Pool_Init(gX_Pool *pool, void *dataBuf, gX_U16 *genBuf,
                     gX_U32 elementSize, gX_U32 capacity) {
    pool->data        = (gX_U8*)dataBuf;
    pool->generations = genBuf;
    pool->capacity    = capacity;
    pool->count       = 0;
    pool->elementSize = elementSize;

    //opencode: zero the data buffer. Generations start at 1 so that the first
    //   alloc returns GX_POOL_HANDLE(idx, 1) which is != GX_POOL_NULL (0).
    memset(pool->data, 0, (size_t)elementSize * (size_t)capacity);
    for (gX_U32 i = 0; i < capacity; i++) {
        pool->generations[i] = 1;
    }
}

//=============================================================================
// Alloc
//=============================================================================

gX_Pool_Handle gX_Pool_Alloc(gX_Pool *pool) {
    if (pool->count >= pool->capacity) {
        return GX_POOL_NULL;
    }

    gX_U32 idx = pool->count;
    pool->count++;

    //opencode: stamp the slot with the current generation. The handle returned
    //   to the caller carries the same gen. On subsequent Get, compare.
    //   Generation starts at 0. First alloc: gen=0, handle gen=0. Pool_Clear
    //   bumps gen to 1. Next alloc after clear: gen=1, handle gen=1.
    //   A handle from the first alloc (gen=0) will fail Get after clear.
    gX_U16 gen = pool->generations[idx];
    return GX_POOL_HANDLE(idx, gen);
}

//=============================================================================
// Get
//=============================================================================

void *gX_Pool_Get(gX_Pool *pool, gX_Pool_Handle h) {
    if (!GX_POOL_VALID(h)) return NULL;
    //opencode: gen=0 is reserved for GX_POOL_NULL encoding; reject
    if (GX_POOL_GEN(h) == 0) return NULL;

    gX_U32 idx = (gX_U32)GX_POOL_IDX(h);
    gX_U16 gen = GX_POOL_GEN(h);

    //opencode: out-of-range or generation mismatch → stale handle
    if (idx >= pool->count)     return NULL;
    if (pool->generations[idx] != gen) return NULL;

    return pool->data + ((size_t)idx * (size_t)pool->elementSize);
}

//=============================================================================
// Clear
//=============================================================================

gX_I0 gX_Pool_Clear(gX_Pool *pool) {
    //opencode: bump generation for every allocated slot so that all existing
    //   handles become stale. Then reset count. Data stays in memory.
    //   Skip gen=0 to keep GX_POOL_NULL (0) unambiguous.
    for (gX_U32 i = 0; i < pool->count; i++) {
        gX_U16 g = pool->generations[i] + 1;
        if (g == 0) g = 1;  // wrap from 65535 → 1, not 0
        pool->generations[i] = g;
    }
    pool->count = 0;
}

//=============================================================================
// Count
//=============================================================================

gX_U32 gX_Pool_Count(const gX_Pool *pool) {
    return pool->count;
}
