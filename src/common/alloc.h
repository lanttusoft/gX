/**********************************************************************************************
*
*   [gX: src/common/alloc.h]
*
*   Copyright (c) 2025 lanttu
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

#ifndef ALLOC_H
#define ALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"
#include "text.h"
    
//ai: remove unused per-allocation text; no tracker path reads or writes it.
typedef struct gX_HeapInstance {
    gX_I1       used;
    gX_I64      address;
    gX_U64      amount; //bytes
    gX_U32      changeCounter;
} gX_HeapInstance;
    
#define GX_HEAPINSTANCES_AMOUNT 1024
typedef struct gX_HeapTracker {
    gX_HeapInstance instances[GX_HEAPINSTANCES_AMOUNT];
    gX_U64 totalMemoryAmount;
    //opencode: live/peak/total accounting — GX_MEM_DEBUG=1 dumps it at exit
    gX_U64 liveBytes;
    gX_U64 liveCount;
    gX_U64 peakBytes;
    gX_U64 peakCount;
    gX_U64 totalAllocs;
    gX_U64 totalFrees;
} gX_HeapTracker;

extern gX_HeapTracker heaptracker;

gX_I0 *gX_Malloc(gX_U32 amount);
gX_I0 *gX_Calloc(gX_U32 amount);
gX_I0 *gX_Realloc(gX_I0 *ptr, gX_U32 amount);
gX_I0 *gX_Realloc_Clear(gX_I0 *ptr, gX_U32 amount, gX_U32 oldAmount);
gX_I32  gX_Free(gX_I0 *ptr);
gX_I32  gX_Alloc_Tests(gX_I0);

//opencode: prints the accounting plus the still-live allocations (a leak
//   report) and returns the live count.
gX_U32 gX_Alloc_Dump(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //ALLOC_H
