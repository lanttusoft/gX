/**********************************************************************************************
*
*   [gX: src/common/alloc.c]
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

//opencode: _GNU_SOURCE for Dl_info/dladdr (the PIE-safe symbol lookup)
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "alloc.h"
#include "log.h"

gX_HeapTracker heaptracker;

static gX_I64 gX_HeapTracker_FindEmpty(gX_I0) {
    gX_I64 instance_index = -1;
    for (gX_I64 i = 0; i < GX_HEAPINSTANCES_AMOUNT; i++) {
        if (!heaptracker.instances[i].used) {
            instance_index = i;
            break;
        }
    }
    return instance_index;
}

//ai: only live records may match an allocation address.
static gX_I64 gX_HeapTracker_FindAddress(gX_I64 ptr) {
    gX_I64 instance_index = -1;
    for (gX_U32 i = 0; i < GX_HEAPINSTANCES_AMOUNT; i++) {
        if (heaptracker.instances[i].used && heaptracker.instances[i].address == ptr) {
            instance_index = i;
            break;
        }
    }
    return instance_index;
}

#define GX_ALLOC_FAILED_TEST \
    if (!ptr) { \
        gX_TRACELOG_EPILOGUE_ERROR("ALLOC: Allocating failed",0) \
        return ptr; \
    } \

#define GX_HEAPTRACKER_ADD_INSTANCE \
    gX_I64 instance_index = gX_HeapTracker_FindEmpty(); \
    if (instance_index > -1) { \
        heaptracker.instances[instance_index].used = true; \
        heaptracker.instances[instance_index].address = (gX_I64)ptr; \
        heaptracker.instances[instance_index].amount = (gX_U64)amount; \
    } \

//opencode: allocation sites — a heap side-table (8 KB, allocated on first
//   use) so the static footprint stays inside the lean gate's bss budget.
//   Leak entries carry the caller's return address; resolve with
//   addr2line -e build/gX_build -f -C <addr>.
static void **gX_Alloc_Sites;

static gX_I0 gX_Alloc_SiteSet(gX_I64 index, const gX_I0 *site) {
    if (index < 0) return;
    if (!site && !gX_Alloc_Sites) return;
    if (!gX_Alloc_Sites) {
        gX_Alloc_Sites = (void **)malloc(
            sizeof(void *) * GX_HEAPINSTANCES_AMOUNT);
        if (!gX_Alloc_Sites) return;
        for (gX_I32 i = 0; i < GX_HEAPINSTANCES_AMOUNT; i++)
            gX_Alloc_Sites[i] = NULL;
    }
    gX_Alloc_Sites[index] = (void *)site;
}

//opencode: live/peak/total accounting, independent of the 1024-entry instance
//   table (the engine makes far more allocations than that).
static gX_I0 gX_Alloc_Account(gX_U64 bytes) {
    heaptracker.liveBytes += bytes;
    heaptracker.liveCount++;
    heaptracker.totalAllocs++;
    if (heaptracker.liveBytes > heaptracker.peakBytes)
        heaptracker.peakBytes = heaptracker.liveBytes;
    if (heaptracker.liveCount > heaptracker.peakCount)
        heaptracker.peakCount = heaptracker.liveCount;
}

static gX_I0 gX_Alloc_Release(gX_U64 bytes) {
    heaptracker.liveBytes = (bytes > heaptracker.liveBytes)
        ? 0 : heaptracker.liveBytes - bytes;
    if (heaptracker.liveCount > 0) heaptracker.liveCount--;
    heaptracker.totalFrees++;
}

//opencode: API returns gX_I0* (void*) — amounts are BYTES (malloc/calloc semantics),
//   callers must never divide by 8 (a /8 mistake once overflowed a 512KB cookie
//   atlas with 4MB of writes). Alignment = whatever malloc/calloc guarantee.
gX_I0 *gX_Malloc(gX_U32 amount) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"Malloc","ALLOC: Allocating bytes")
    gX_I0 *ptr = malloc(amount);
    GX_ALLOC_FAILED_TEST
    GX_HEAPTRACKER_ADD_INSTANCE
    gX_Alloc_SiteSet(instance_index, __builtin_return_address(0));
    gX_Alloc_Account(amount);
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"ALLOC: Allocating successful")
    return ptr;
}

gX_I0 *gX_Calloc(gX_U32 amount) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"Calloc","ALLOC: Allocating bytes")
    gX_I0 *ptr = calloc(amount, sizeof(gX_U8));
    GX_ALLOC_FAILED_TEST
    GX_HEAPTRACKER_ADD_INSTANCE
    gX_Alloc_SiteSet(instance_index, __builtin_return_address(0));
    gX_Alloc_Account(amount);
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"ALLOC: Allocating successful")
    return ptr;
}

//ai: realloc(NULL,n) is a fresh allocation, not a release of an empty tracker slot.
gX_I0 *gX_Realloc(gX_I0 *ptr, gX_U32 amount) {
    if (!ptr) return gX_Malloc(amount);
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"Realloc","ALLOC: Reallocating bytes")
    gX_I64 oldAddress = (gX_I64)ptr;
    gX_I0 *newptr = realloc(ptr, amount);
    if (!newptr) {
        gX_TRACELOG_EPILOGUE_ERROR("ALLOC: Reallocating failed",0)
        return ptr;
    }
    gX_I64 instance_index = gX_HeapTracker_FindAddress(oldAddress);
    if (instance_index > -1) {
        gX_Alloc_Release(heaptracker.instances[instance_index].amount);
        //opencode: track the moved address — recording oldAddress left a stale
        //   entry that later matched recycled heap memory (phantom leaks and
        //   double frees in the GUI markup cleanup test).
        heaptracker.instances[instance_index].address = (gX_I64)newptr;
        heaptracker.instances[instance_index].amount = (gX_U64)amount;
        gX_Alloc_Account(amount);
    }
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"ALLOC: Reallocating successful")
    return newptr;
}

//ai: a null clear-reallocation has no preserved prefix; initialize and track all bytes.
gX_I0 *gX_Realloc_Clear(gX_I0 *ptr, gX_U32 amount, gX_U32 oldAmount) {
    if (!ptr) return gX_Calloc(amount);
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"Realloc_Clear","ALLOC: Reallocating clear bytes")
    gX_I64 oldAddress = (gX_I64)ptr;
    gX_I0 *newptr = realloc(ptr, amount);
    if (!newptr) {
        gX_TRACELOG_EPILOGUE_ERROR("ALLOC: Reallocating failed",0)
        return ptr;
    }
    if (amount > oldAmount) {
        gX_I8 *bytePtr = (gX_I8*)newptr;
        memset(bytePtr + oldAmount, 0, (gX_U64)(amount - oldAmount));
    }
    gX_I64 instance_index = gX_HeapTracker_FindAddress(oldAddress);
    if (instance_index > -1) {
        gX_Alloc_Release(heaptracker.instances[instance_index].amount);
        heaptracker.instances[instance_index].address = (gX_I64)newptr;
        heaptracker.instances[instance_index].amount = (gX_U64)amount;
        gX_Alloc_Account(amount);
    }
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"ALLOC: Reallocating successful")
    return newptr;
}

gX_I32 gX_Free(gX_I0 *ptr) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"Free","ALLOC: Freeing bytes")
    if (!ptr) {
        gX_TRACELOG_EPILOGUE_ERROR("ALLOC: Null ptr: potential memory leak",0)
        return 1;
    }
    gX_I64 oldAddress = (gX_I64)ptr;
    free(ptr);
    gX_I64 instance_index = gX_HeapTracker_FindAddress(oldAddress);
    if (instance_index > -1) {
        gX_Alloc_Release(heaptracker.instances[instance_index].amount);
        gX_Alloc_SiteSet(instance_index, NULL);
        heaptracker.instances[instance_index].used = false;
        heaptracker.instances[instance_index].address = 0;
        heaptracker.instances[instance_index].amount = 0;
    }
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"ALLOC: Freeing successful")
    return 0;
}

gX_U32 gX_Alloc_Dump(gX_I0) {
    gX_U32 leaks = 0;
    gX_U64 leakBytes = 0;
    for (gX_U32 i = 0; i < GX_HEAPINSTANCES_AMOUNT; i++) {
        if (!heaptracker.instances[i].used) continue;
        leaks++;
        leakBytes += heaptracker.instances[i].amount;
        if (leaks <= 8) {
            //opencode: the binary is PIE, so raw addresses do not resolve
            //   with addr2line; dladdr gives symbol+offset directly.
            const void *site = gX_Alloc_Sites ? gX_Alloc_Sites[i] : NULL;
            Dl_info info;
            if (site && dladdr(site, &info) && info.dli_sname) {
                gX_TraceLog(gX_LOG_INFO,
                    "GX_MEM: leak %u addr=%lld bytes=%llu site=%s+0x%llx",
                    leaks, (long long)heaptracker.instances[i].address,
                    (unsigned long long)heaptracker.instances[i].amount,
                    info.dli_sname,
                    (unsigned long long)((const char *)site
                        - (const char *)info.dli_saddr));
            } else if (site && dladdr(site, &info)) {
                //opencode: static callers have no exported name; the
                //   base-relative offset resolves with
                //   addr2line -e build/gX_build -f -C 0xOFFSET
                gX_TraceLog(gX_LOG_INFO,
                    "GX_MEM: leak %u addr=%lld bytes=%llu site=+0x%llx",
                    leaks, (long long)heaptracker.instances[i].address,
                    (unsigned long long)heaptracker.instances[i].amount,
                    (unsigned long long)((const char *)site
                        - (const char *)info.dli_fbase));
            } else {
                gX_TraceLog(gX_LOG_INFO,
                    "GX_MEM: leak %u addr=%lld bytes=%llu site=%p",
                    leaks, (long long)heaptracker.instances[i].address,
                    (unsigned long long)heaptracker.instances[i].amount,
                    site);
            }
        }
    }
    gX_TraceLog(gX_LOG_INFO,
        "GX_MEM: live=%llu bytes=%llu peak=%llu peak_bytes=%llu "
        "allocs=%llu frees=%llu leaks=%llu leak_bytes=%llu",
        (unsigned long long)heaptracker.liveCount,
        (unsigned long long)heaptracker.liveBytes,
        (unsigned long long)heaptracker.peakCount,
        (unsigned long long)heaptracker.peakBytes,
        (unsigned long long)heaptracker.totalAllocs,
        (unsigned long long)heaptracker.totalFrees,
        (unsigned long long)leaks, (unsigned long long)leakBytes);
    return leaks;
}

//ai: native null realloc accounting, growth/prefix preservation and clear-tail regression.
static gX_I32 gX_Alloc_Test_NullRealloc(gX_I1 Clear)
{
    gX_U64 LiveBytes = heaptracker.liveBytes;
    gX_U64 LiveCount = heaptracker.liveCount;
    gX_U64 Allocs = heaptracker.totalAllocs;
    gX_U64 Frees = heaptracker.totalFrees;
    gX_U8 *Pointer = Clear ? gX_Realloc_Clear(NULL, 32, 0) : gX_Realloc(NULL, 32);
    if (!Pointer) return 1;
    gX_I64 Index = gX_HeapTracker_FindAddress((gX_I64)Pointer);
    gX_I32 Errors = 0;
    if (Index < 0 || heaptracker.instances[Index].amount != 32
        || heaptracker.liveBytes != LiveBytes + 32
        || heaptracker.liveCount != LiveCount + 1
        || heaptracker.totalAllocs != Allocs + 1 || heaptracker.totalFrees != Frees)
        Errors++;
    for (gX_U32 Byte = 0; Byte < 32; Byte++)
    {
        if (Clear && Pointer[Byte] != 0) Errors++;
        Pointer[Byte] = (gX_U8)(Byte + 1);
    }
    Pointer = Clear ? gX_Realloc_Clear(Pointer, 128, 32) : gX_Realloc(Pointer, 128);
    Index = gX_HeapTracker_FindAddress((gX_I64)Pointer);
    if (Index < 0 || heaptracker.instances[Index].amount != 128) Errors++;
    else
    {
        for (gX_U32 Byte = 0; Byte < 128; Byte++)
        {
            if (Byte < 32 && Pointer[Byte] != (gX_U8)(Byte + 1)) Errors++;
            if (Clear && Byte >= 32 && Pointer[Byte] != 0) Errors++;
        }
    }
    gX_I64 Address = (gX_I64)Pointer;
    Errors += gX_Free(Pointer);
    if (heaptracker.liveBytes != LiveBytes || heaptracker.liveCount != LiveCount
        || gX_HeapTracker_FindAddress(Address) >= 0) Errors++;
    return Errors;
}

//ai: include both null-allocation branches alongside the established growth/free smoke.
gX_I32 gX_Alloc_Tests(gX_I0) {
    gX_I32 errors = 0;
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_DEBUG,"Alloc_Tests","ALLOC: Testing alloc")
    gX_I64 *testPtr = gX_Malloc(8);
    testPtr = gX_Realloc_Clear(testPtr, 1024, 8);
    errors = gX_Free(testPtr);
    errors += gX_Alloc_Test_NullRealloc(false);
    errors += gX_Alloc_Test_NullRealloc(true);
    if (errors) gX_TraceLog(gX_LOG_ERROR, "ALLOC_TESTS: null/growth accounting failed (%d)", errors);
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_DEBUG,"ALLOC: Testing successful")
    return errors;
}
