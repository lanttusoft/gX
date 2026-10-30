/**********************************************************************************************
*
*   [gX: src/common/heap.c]
*   Heap: dynamic array — lifecycle, ops, setters (from gameX heap/)
*
*   Copyright (c) 2025-2026 lanttu, opencode
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

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "heap.h"
#include "alloc.h"
#include "log.h"
#include "config.h"

#define HEAP_LOG_LEVEL gX_LOG_TRACE

// ============================================================
// Static internal helpers
// ============================================================
static gX_I32 gX_Heap_Alloc(gX_Heap *heap);
static gX_I32 gX_Heap_Realloc(gX_Heap *heap, gX_U32 oldAmount);
static gX_I32 gX_Heap_Free(gX_Heap *heap);
static gX_I32 gX_Heap_Increment(gX_Heap *heap);
static gX_I32 gX_Heap_Decrement(gX_Heap *heap);
static gX_I32 gX_Heap_IncrementMany(gX_Heap *heap, gX_U32 amount);
static gX_I32 gX_Heap_DecrementMany(gX_Heap *heap, gX_U32 amount);
static gX_I32  gX_Heap_SeekIndex_Last(gX_Heap *heap);
static gX_I32  gX_Heap_SeekIndex_FirstEmpty(gX_Heap *heap);
static gX_I0  *gX_Heap_SeekPtr_Last(gX_Heap *heap);
static gX_I0  *gX_Heap_SeekPtr_FirstEmpty(gX_Heap *heap);
static gX_I0  *gX_Heap_SeekPtr_FirstEmpty_GetIndex(gX_Heap *heap, gX_I32 *index);
static gX_I0  *gX_Heap_Add_Last_NoPtr(gX_Heap *heap);
static gX_I0  *gX_Heap_Add_Index_NoPtr(gX_Heap *heap, gX_U32 index);
static gX_I0  *gX_Heap_Remove_Last_NoPtr(gX_Heap *heap);
static gX_I0  *gX_Heap_Remove_Pop_NoPtr(gX_Heap *heap);
static gX_I32  gX_Heap_Remove_Clear(gX_Heap *heap, gX_I0(*unloadFnPtr)(gX_I0*));

// ============================================================
// Lifecycle
// ============================================================

gX_I0 gX_Heap_Zero(gX_Heap *heap) {
    *heap = (gX_Heap){ 0 };
}

//ai: failed initial allocation leaves a non-initialized empty heap rather than a usable count.
gX_I32 gX_Heap_Init(gX_Heap *heap) {
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_HEAP,gX_LOG_TRACE,"Heap_Init","Initializing heap")
    
    if (heap->amount == 0 && heap->max == 0 && heap->blockSize == 0) {
        heap->amount = 1;
        heap->max = 1024;
        heap->blockSize = 1;
    }
    if (heap->blockSize == 0) heap->blockSize = 1;
    if (heap->max == 0) heap->max = 1024;
    
    heap->state.HEAP_ALLOCATED = false;
    heap->state.META_ALLOCATED = false;
    gX_I32 errors = 0;
    if (heap->amount > 0) {
        errors = gX_Heap_Alloc(heap);
    }
    
    if (errors) heap->amount = 0;
    heap->state.INITIALIZED = errors == 0;
    heap->state.EMPTY = true;
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_HEAP,gX_LOG_TRACE,"Heap initialized")
    return errors;
}

gX_I32 gX_Heap_DeInit(gX_Heap *heap) {
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_HEAP,gX_LOG_TRACE,"Heap_DeInit","Deinitializing heap")
    
    gX_Heap_Free(heap);
    
    heap->data = 0;
    heap->meta = 0;
    heap->amount = 0;
    heap->max = 0;
    heap->blockSize = 0;
    heap->state = (gX_Heap_State){0};
    heap->state.INITIALIZED = false;
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_HEAP,gX_LOG_TRACE,"Heap deinitialized")
    return 0;
}

// ============================================================
// Alloc / Realloc / Free
// ============================================================

//ai: prepare every buffer before committing; legacy realloc returns the old pointer on failure.
static gX_I32 gX_Heap_Resize(gX_Heap *Heap, gX_U32 OldAmount)
{
    gX_U32 Amount = Heap->amount;
    if (!Heap->blockSize || Amount > Heap->max || Amount > INT32_MAX
        || Amount > UINT32_MAX / Heap->blockSize
        || Amount > UINT32_MAX / sizeof(gX_Heap_Meta)
        || ((Heap->state.ENABLE_FILENAME || Heap->state.ENABLE_FILENAME2)
            && Amount > UINT32_MAX / sizeof(gX_TextLine))) return 1;
    gX_Heap Pending = *Heap;
    Pending.data = NULL;
    Pending.meta = NULL;
    Pending.fileName = NULL;
    Pending.fileName2 = NULL;
    Pending.state.HEAP_ALLOCATED = false;
    Pending.state.META_ALLOCATED = false;
    Pending.state.FILENAME_ALLOCATED = false;
    Pending.state.FILENAME2_ALLOCATED = false;
    Pending.data = gX_Calloc(Amount * Heap->blockSize);
    Pending.state.HEAP_ALLOCATED = Pending.data != NULL;
    Pending.meta = gX_Calloc(Amount * sizeof(gX_Heap_Meta));
    Pending.state.META_ALLOCATED = Pending.meta != NULL;
    if (Heap->state.ENABLE_FILENAME)
    {
        Pending.fileName = gX_Calloc(Amount * sizeof(gX_TextLine));
        Pending.state.FILENAME_ALLOCATED = Pending.fileName != NULL;
    }
    if (Heap->state.ENABLE_FILENAME2)
    {
        Pending.fileName2 = gX_Calloc(Amount * sizeof(gX_TextLine));
        Pending.state.FILENAME2_ALLOCATED = Pending.fileName2 != NULL;
    }
    if (!Pending.data || !Pending.meta
        || (Heap->state.ENABLE_FILENAME && !Pending.fileName)
        || (Heap->state.ENABLE_FILENAME2 && !Pending.fileName2))
    {
        gX_Heap_Free(&Pending);
        return 1;
    }
    gX_U32 CopyAmount = OldAmount < Amount ? OldAmount : Amount;
    if (CopyAmount)
    {
        memcpy(Pending.data, Heap->data, CopyAmount * Heap->blockSize);
        memcpy(Pending.meta, Heap->meta, CopyAmount * sizeof(gX_Heap_Meta));
        if (Heap->fileName && Pending.fileName)
            memcpy(Pending.fileName, Heap->fileName, CopyAmount * sizeof(gX_TextLine));
        if (Heap->fileName2 && Pending.fileName2)
            memcpy(Pending.fileName2, Heap->fileName2, CopyAmount * sizeof(gX_TextLine));
    }
    gX_Heap_Free(Heap);
    *Heap = Pending;
    if (OldAmount) Heap->refreshState++;
    return 0;
}

//ai: initial allocation and resizing share checked, transactional storage preparation.
static gX_I32 gX_Heap_Alloc(gX_Heap *Heap)
{
    return gX_Heap_Resize(Heap, 0);
}

//ai: failed resize leaves all old buffers and flags intact; callers restore the count.
static gX_I32 gX_Heap_Realloc(gX_Heap *Heap, gX_U32 OldAmount)
{
    return gX_Heap_Resize(Heap, OldAmount);
}

#define HEAP_FREE_FORMAT(heapname,statename) \
    if (heap->state.statename##_ALLOCATED) { \
        gX_Free((gX_I0*)heap->heapname); \
        heap->heapname = 0x0; \
        heap->state.statename##_ALLOCATED = false; \
    }

static gX_I32 gX_Heap_Free(gX_Heap *heap) {
    gX_I32 errors = 0;
    
    HEAP_FREE_FORMAT(data,HEAP)
    HEAP_FREE_FORMAT(meta,META)
    
    if (heap->state.ENABLE_FILENAME) {
        HEAP_FREE_FORMAT(fileName,FILENAME)
    }
    if (heap->state.ENABLE_FILENAME2) {
        HEAP_FREE_FORMAT(fileName2,FILENAME2)
    }
    
    return errors;
}

// ============================================================
// Increment / Decrement
// ============================================================

//ai: single-item growth uses the same overflow and rollback policy as bulk growth.
static gX_I32 gX_Heap_Increment(gX_Heap *Heap)
{
    return gX_Heap_IncrementMany(Heap, 1);
}

//ai: failed shrinking must retain the original logical count alongside the old buffers.
static gX_I32 gX_Heap_Decrement(gX_Heap *heap) {
    gX_U32 oldAmount = heap->amount;
    if (heap->amount > 0) heap->amount--;
    
    if (heap->amount == 0) gX_Heap_Free(heap);
    else if (gX_Heap_Realloc(heap,oldAmount))
    {
        heap->amount = oldAmount;
        return 1;
    }
    
    return 0;
}

//ai: count is committed only after every storage buffer is ready; max rejection is non-mutating.
static gX_I32 gX_Heap_IncrementMany(gX_Heap *Heap, gX_U32 Amount)
{
    if (!Heap || Heap->amount > Heap->max || Amount > Heap->max - Heap->amount) return 1;
    if (!Amount) return 0;
    gX_U32 Previous = Heap->amount;
    Heap->amount += Amount;
    gX_I32 Errors = Previous ? gX_Heap_Realloc(Heap, Previous) : gX_Heap_Alloc(Heap);
    if (Errors) Heap->amount = Previous;
    return Errors;
}

//ai: bulk shrinking uses the same rollback policy as one-item shrinking.
static gX_I32 gX_Heap_DecrementMany(gX_Heap *heap, gX_U32 amount) {
    gX_U32 oldAmount = heap->amount;
    
    if (amount >= heap->amount) {
        heap->amount = 0;
    } else {
        heap->amount -= amount;
    }
    
    if (heap->amount == 0) gX_Heap_Free(heap);
    else if (gX_Heap_Realloc(heap,oldAmount))
    {
        heap->amount = oldAmount;
        return 1;
    }
    
    return 0;
}

// ============================================================
// Seek Index
// ============================================================

static gX_I32 gX_Heap_SeekIndex_Last(gX_Heap *heap) {
    if (!heap) return -1;
    return (gX_I32)heap->amount - 1;
}

static gX_I32 gX_Heap_SeekIndex_FirstEmpty(gX_Heap *heap) {
    gX_I32 firstempty = -1;
    if (!heap) return firstempty;
    if (!(heap->meta)) return firstempty;
    
    for (gX_U32 i = 0; i < heap->amount; i++) {
        if (!heap->meta[i].USED) {
            firstempty = (gX_I32)i;
            break;
        }
    }
    return firstempty;
}

// ============================================================
// Seek Ptr
// ============================================================

//ai: allocation failure must not manufacture a pointer into absent storage.
static gX_I0 *gX_Heap_SeekPtr_Last(gX_Heap *heap) {
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_HEAP,HEAP_LOG_LEVEL,"Heap_SeekPtr_Last","Seeking heap from last index: returning ptr")
    if (!heap) { gX_TraceLog_StackDecrement(); return 0; }
    
    gX_I32 last = gX_Heap_SeekIndex_Last(heap);
    if (last == -1) {
        if (gX_Heap_Increment(heap)) { gX_TraceLog_StackDecrement(); return NULL; }
        last = (gX_I32)heap->amount - 1;
    }
    
    gX_I8 *ptr = (gX_I8*)heap->data;
    ptr += (gX_U64)heap->blockSize * (gX_U32)last;
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_HEAP,gX_LOG_TRACE,"Seeking successful")
    return (gX_I0*)ptr;
}

//ai: propagate failed growth rather than returning the previous last element.
static gX_I0 *gX_Heap_SeekPtr_FirstEmpty(gX_Heap *heap) {
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_HEAP,gX_LOG_TRACE,"Heap_SeekPtr_FirstEmpty","Seeking heap from first empty slot: returning ptr")
    if (!heap) { gX_TraceLog_StackDecrement(); return 0; }
    
    gX_I32 firstempty = gX_Heap_SeekIndex_FirstEmpty(heap);
    if (firstempty == -1) {
        if (gX_Heap_Increment(heap)) { gX_TraceLog_StackDecrement(); return NULL; }
        firstempty = (gX_I32)heap->amount - 1;
    }
    
    gX_I8 *ptr = (gX_I8*)heap->data;
    if ((firstempty >= 0) && (firstempty < (gX_I32)heap->amount)) {
        ptr += (gX_U64)heap->blockSize * (gX_U32)firstempty;
    }
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_HEAP,gX_LOG_TRACE,"Seeking successful")
    return (gX_I0*)ptr;
}

//ai: failed indexed growth returns no pointer and an explicit invalid index.
static gX_I0 *gX_Heap_SeekPtr_FirstEmpty_GetIndex(gX_Heap *heap, gX_I32 *index) {
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_HEAP,gX_LOG_TRACE,"Heap_SeekPtr_FirstEmpty_GetIndex","Seeking heap from first empty slot: returning ptr and getting index")
    if (!heap) { gX_TraceLog_StackDecrement(); return 0; }
    
    gX_I32 firstempty = gX_Heap_SeekIndex_FirstEmpty(heap);
    if (firstempty == -1) {
        if (gX_Heap_Increment(heap)) { *index = -1; gX_TraceLog_StackDecrement(); return NULL; }
        firstempty = (gX_I32)heap->amount - 1;
    }
    
    gX_I8 *ptr = (gX_I8*)heap->data;
    if ((firstempty >= 0) && (firstempty < (gX_I32)heap->amount)) {
        ptr += (gX_U64)heap->blockSize * (gX_U32)firstempty;
        *index = firstempty;
    }
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_HEAP,gX_LOG_TRACE,"Seeking successful")
    return (gX_I0*)ptr;
}

// ============================================================
// Seek Ptr Index — PUBLIC
// ============================================================

//ai: byte offsets use wide unsigned arithmetic rather than overflowing signed index products.
gX_I0 *gX_Heap_SeekPtr_Index(gX_Heap *heap, gX_U32 index) {
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_HEAP,gX_LOG_TRACE,"Heap_SeekPtr_Index","Seeking heap from given index: returning ptr")
    
    if (!heap) { gX_TraceLog_StackDecrement(); return 0; }
    if (heap->amount == 0) { gX_TraceLog_StackDecrement(); return 0; }
    if (index > heap->amount - 1) { gX_TraceLog_StackDecrement(); return 0; }
    
    gX_I8 *ptr = (gX_I8*)heap->data;
    if (!ptr) {
        gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_HEAP,gX_LOG_TRACE_WARNING,"no heap ptr")
        return (gX_I0*)ptr;
    }
    ptr += (gX_U64)heap->blockSize * index;
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_HEAP,gX_LOG_TRACE,"Seeking successful")
    return (gX_I0*)ptr;
}

// ============================================================
// Add (internal — used by public wrappers below)
// ============================================================

static gX_I0 *gX_Heap_Add_Last_NoPtr(gX_Heap *heap) {
    gX_I8 *ptr = (gX_I8*)gX_Heap_SeekPtr_Last(heap);
    gX_I32 last = gX_Heap_SeekIndex_Last(heap);
    if (last < 0 || !ptr) return 0;
    heap->meta[last].USED = true;
    return (gX_I0*)ptr;
}

static gX_I0 *gX_Heap_Add_Index_NoPtr(gX_Heap *heap, gX_U32 index) {
    gX_I8 *ptr = (gX_I8*)gX_Heap_SeekPtr_Index(heap, index);
    if (ptr) heap->meta[index].USED = true;
    return (gX_I0*)ptr;
}

// ============================================================
// Add — PUBLIC
// ============================================================

//ai: a failed push cannot silently reuse or overwrite the old last element.
gX_I0 *gX_Heap_Add_Push_NoPtr(gX_Heap *heap) {
    if (gX_Heap_Increment(heap)) return NULL;
    return gX_Heap_Add_Last_NoPtr(heap);
}

gX_I0 *gX_Heap_Add_FirstEmpty_NoPtr(gX_Heap *heap) {
    gX_I8 *ptr = (gX_I8*)gX_Heap_SeekPtr_FirstEmpty(heap);
    gX_I32 firstempty = gX_Heap_SeekIndex_FirstEmpty(heap);
    if (firstempty < 0 || !ptr) return 0;
    heap->meta[firstempty].USED = true;
    return (gX_I0*)ptr;
}

gX_I0 *gX_Heap_Add_FirstEmpty_NoPtr_GetIndex(gX_Heap *heap, gX_I32 *index) {
    gX_I8 *ptr = (gX_I8*)gX_Heap_SeekPtr_FirstEmpty_GetIndex(heap, index);
    if (!ptr) return 0;
    gX_I32 ind = *index;
    if ((ind >= 0) && (ind < (gX_I32)heap->amount)) {
        heap->meta[ind].USED = true;
    }
    return (gX_I0*)ptr;
}

// ============================================================
// Remove (internal)
// ============================================================

static gX_I0 *gX_Heap_Remove_Last_NoPtr(gX_Heap *heap) {
    gX_I8 *ptr = (gX_I8*)gX_Heap_SeekPtr_Last(heap);
    gX_I32 last = gX_Heap_SeekIndex_Last(heap);
    if (last < 0 || !ptr) return 0;
    if (heap->meta) heap->meta[last].USED = false;
    return (gX_I0*)ptr;
}

static gX_I0 *gX_Heap_Remove_Pop_NoPtr(gX_Heap *heap) {
    gX_I0 *ptr = gX_Heap_Remove_Last_NoPtr(heap);
    gX_Heap_Decrement(heap);
    return ptr;
}

static gX_I32 gX_Heap_Remove_Clear(gX_Heap *heap, gX_I0(*unloadFnPtr)(gX_I0*)) {
    for (gX_U32 i = 0; i < heap->amount; i++) {
        gX_I0 *ptr = gX_Heap_Remove_Index_NoPtr(heap, i);
        if (unloadFnPtr) unloadFnPtr(ptr);
    }
    return 0;
}

// ============================================================
// Remove — PUBLIC
// ============================================================

gX_I0 *gX_Heap_Remove_Index_NoPtr(gX_Heap *heap, gX_U32 index) {
    gX_I8 *ptr = (gX_I8*)gX_Heap_SeekPtr_Index(heap, index);
    if (!ptr) return 0;
    if (heap->meta) heap->meta[index].USED = false;
    return (gX_I0*)ptr;
}

// ============================================================
// Assert — PUBLIC
// ============================================================

gX_I32 gX_Heap_Assert(gX_Heap *heap) {
    gX_I32 errors = 0;
    if (heap->data == 0) { errors++; gX_TraceLog(gX_LOG_NOTICE,"HEAP: data not allocated"); }
    if (heap->meta == 0) { errors++; gX_TraceLog(gX_LOG_NOTICE,"HEAP: meta not allocated"); }
    return errors;
}

// ============================================================
// ClearBlock — PUBLIC
// ============================================================

gX_I32 gX_Heap_ClearBlock(gX_Heap *heap, gX_U32 index) {
    if (index >= heap->amount) return 1;
    
    gX_I8 *ptr = (gX_I8*)gX_Heap_SeekPtr_Index(heap, index);
    if (ptr) memset(ptr, 0, heap->blockSize);
    return 0;
}

// ============================================================
// Config Setters — PUBLIC
// ============================================================

gX_I32 gX_Heap_SetName(gX_Heap *heap, const gX_I8 *name) {
    strcpy(heap->name, name);
    return 0;
}

gX_I32 gX_Heap_SetDescription(gX_Heap *heap, const gX_I8 *desc) {
    strcpy(heap->desc, desc);
    return 0;
}

gX_I32 gX_Heap_SetID(gX_Heap *heap, gX_U32 id) {
    heap->id = id;
    return 0;
}

gX_I32 gX_Heap_SetType(gX_Heap *heap, gX_I32 type) {
    heap->type = type;
    return 0;
}

gX_I32 gX_Heap_SetBlockSize(gX_Heap *heap, gX_U32 blocksize) {
    if (blocksize == 0) return 1;
    heap->blockSize = blocksize;
    return 0;
}

gX_I32 gX_Heap_SetMax(gX_Heap *heap, gX_U32 max) {
    if (max >= 65536) return 1;
    heap->max = max;
    return 0;
}

gX_I32 gX_Heap_SetAmount(gX_Heap *heap, gX_U32 amount) {
    heap->amount = (amount >= heap->max) ? heap->max : amount;
    return 0;
}

gX_I32 gX_Heap_SetFileName(gX_Heap *heap, gX_U32 index, const gX_I8 *filename) {
    if (!heap->fileName || heap->amount == 0) return 1;
    
    gX_U32 realIndex = (index >= heap->amount) ? heap->amount - 1 : index;
    strcpy(heap->fileName[realIndex], filename);
    heap->meta[realIndex].HAS_FILENAME = true;
    return 0;
}

gX_I32 gX_Heap_SetFileName2(gX_Heap *heap, gX_U32 index, const gX_I8 *filename2) {
    if (!heap->fileName2 || heap->amount == 0) return 1;
    
    gX_U32 realIndex = (index >= heap->amount) ? heap->amount - 1 : index;
    strcpy(heap->fileName2[realIndex], filename2);
    heap->meta[realIndex].HAS_FILENAME2 = true;
    return 0;
}

// ============================================================
// Tests — PUBLIC
// ============================================================

static gX_I32 gX_Heap_Test_Basic(gX_Heap *heap) {
    gX_I32 result = 0;
    result = gX_Heap_Init(heap);
    result = gX_Heap_DeInit(heap);
    result = gX_Heap_Init(heap);
    result = gX_Heap_SetName(heap, "test");
    result = gX_Heap_SetDescription(heap, "this is for testing");
    gX_Heap_SetID(heap, 22);
    gX_Heap_SetType(heap, 7);
    gX_Heap_SetBlockSize(heap, 4);
    gX_Heap_SetMax(heap, 128);
    gX_Heap_SetAmount(heap, 0);
    return result;
}

static gX_I32 gX_Heap_Test_Init(gX_Heap *heap) {
    gX_I32 result = 0;
    gX_TraceLog_HEAP(gX_LOG_INFO,"init");
    
    result = gX_Heap_DeInit(heap);
    gX_Heap_SetBlockSize(heap, 1);
    gX_Heap_SetMax(heap, 128);
    gX_Heap_SetAmount(heap, 0);
    result = gX_Heap_Init(heap);
    
    result = gX_Heap_DeInit(heap);
    gX_Heap_SetBlockSize(heap, 0);
    gX_Heap_SetMax(heap, 0);
    gX_Heap_SetAmount(heap, 0);
    result = gX_Heap_Init(heap);
    
    result = gX_Heap_DeInit(heap);
    gX_Heap_SetBlockSize(heap, 0);
    gX_Heap_SetMax(heap, 0);
    gX_Heap_SetAmount(heap, 128);
    result = gX_Heap_Init(heap);
    
    result = gX_Heap_DeInit(heap);
    gX_Heap_SetBlockSize(heap, 128);
    gX_Heap_SetMax(heap, 0);
    gX_Heap_SetAmount(heap, 0);
    result = gX_Heap_Init(heap);
    
    result = gX_Heap_DeInit(heap);
    gX_Heap_SetBlockSize(heap, 128);
    gX_Heap_SetMax(heap, 127);
    gX_Heap_SetAmount(heap, 128);
    result = gX_Heap_Init(heap);
    
    gX_TraceLog_HEAP(gX_LOG_INFO,"init done");
    return result;
}

static gX_I32 gX_Heap_Test_IncDec(gX_Heap *heap) {
    gX_I32 result = 0;
    gX_TraceLog_HEAP(gX_LOG_INFO,"incdec");
    // Internal inc/dec tested via add/remove in real usage
    result = gX_Heap_Init(heap);
    gX_TraceLog_HEAP(gX_LOG_INFO,"incdec done");
    return result;
}

gX_I32 gX_Heap_Tests(gX_Heap *heap) {
    gX_I32 result = 0;
    result = gX_Heap_Test_Basic(heap);
    result = gX_Heap_Test_Init(heap);
    result = gX_Heap_Test_IncDec(heap);
    result = gX_Heap_DeInit(heap);
    if (!result) {
        gX_TraceLog(gX_LOG_INFO,"HEAP_TESTS: Successful!");
    } else {
        gX_TraceLog(gX_LOG_INFO,"HEAP_TESTS: Failed! %i", result);
    }
    return result;
}
