/**********************************************************************************************
*
*   [gX: src/common/heap.h]
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

#ifndef HEAP_H
#define HEAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"
#include "text.h"

typedef struct gX_Heap_State {
    gX_I1   INITIALIZED               : 1;
    gX_I1   EMPTY                     : 1;
    gX_I1   ENABLE_FILENAME           : 1;
    gX_I1   ENABLE_FILENAME2          : 1;
    gX_I1   HEAP_ALLOCATED            : 1;
    gX_I1   META_ALLOCATED            : 1;
    gX_I1   FILENAME_ALLOCATED        : 1;
    gX_I1   FILENAME2_ALLOCATED       : 1;
    gX_I1   UPDATE_ALL                : 1;
    gX_I1   DRAW_ALL                  : 1;
    gX_I32  PADDING                   : 6;
} gX_Heap_State;

typedef struct gX_Heap_Meta {
    gX_I1   USED                      : 1;
    gX_I1   HAS_FILENAME              : 1;
    gX_I1   HAS_FILENAME2             : 1;
    gX_I1   UPDATE                    : 1;
    gX_I1   DRAW                      : 1;
    gX_I1   UNLOAD_AFTER_MASTERINDEX_CHANGE : 1;
    gX_I32  PADDING                   : 2;
} gX_Heap_Meta;

#define gX_HEAP_NAMEBFR_SIZE 128
#define gX_HEAP_DESCBFR_SIZE 256
typedef struct gX_Heap {
    
    gX_I8           name[gX_HEAP_NAMEBFR_SIZE];
    gX_I8           desc[gX_HEAP_DESCBFR_SIZE];
    
    gX_U32          id;
    gX_I32          type;
    gX_Heap_State   state;
    gX_I32          refreshState;
    
    gX_U32          amount;
    gX_U32          max;
    gX_U32          blockSize;
    gX_Heap_Meta   *meta;
    gX_I0          *data;
    gX_TextLine    *fileName;
    gX_TextLine    *fileName2;
    
} gX_Heap;

// --- Core lifecycle ---
gX_I0  gX_Heap_Zero(   gX_Heap *heap);
gX_I32 gX_Heap_Init(   gX_Heap *heap);
gX_I32 gX_Heap_DeInit( gX_Heap *heap);
gX_I32 gX_Heap_Assert( gX_Heap *heap);

// --- Config setters ---
gX_I32 gX_Heap_SetName(        gX_Heap *heap, const gX_I8 *name);
gX_I32 gX_Heap_SetDescription( gX_Heap *heap, const gX_I8 *desc);
gX_I32 gX_Heap_SetID(          gX_Heap *heap, gX_U32 id);
gX_I32 gX_Heap_SetType(        gX_Heap *heap, gX_I32 type);
gX_I32 gX_Heap_SetBlockSize(   gX_Heap *heap, gX_U32 blocksize);
gX_I32 gX_Heap_SetMax(         gX_Heap *heap, gX_U32 max);
gX_I32 gX_Heap_SetAmount(      gX_Heap *heap, gX_U32 amount);
gX_I32 gX_Heap_SetFileName(    gX_Heap *heap, gX_U32 index, const gX_I8 *filename);
gX_I32 gX_Heap_SetFileName2(   gX_Heap *heap, gX_U32 index, const gX_I8 *filename2);

// --- Add ---
gX_I0 *gX_Heap_Add_Push_NoPtr(              gX_Heap *heap);
gX_I0 *gX_Heap_Add_FirstEmpty_NoPtr(        gX_Heap *heap);
gX_I0 *gX_Heap_Add_FirstEmpty_NoPtr_GetIndex(gX_Heap *heap, gX_I32 *index);

// --- Remove ---
gX_I0  *gX_Heap_Remove_Index_NoPtr(gX_Heap *heap, gX_U32 index);

// --- Access ---
gX_I0  *gX_Heap_SeekPtr_Index(gX_Heap *heap, gX_U32 index);
gX_I32  gX_Heap_ClearBlock(    gX_Heap *heap, gX_U32 index);

// --- Convenience macros ---
#define gX_Heap_Add      gX_Heap_Add_FirstEmpty_NoPtr
#define gX_Heap_Remove   gX_Heap_Remove_Index_NoPtr

// --- Tests ---
gX_I32 gX_Heap_Tests( gX_Heap *heap);

#ifdef __cplusplus
}
#endif

#endif //HEAP_H
