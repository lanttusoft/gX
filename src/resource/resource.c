/**********************************************************************************************
*
*   [gX: src/resource/resource.c]
*   [resource/resource.c] — Resource manager (ported from gameX)
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
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "resource.h"

#include "../common/log.h"
#include "../common/enum.h"
#include "../common/utils.h"

#include "../wrappers/wrappers.h"

#include "../main.h"

//opencode: rewritten — typed slot array replaces heap-of-heaps
gX_ResourceManager resources;

gX_ResourceSlot *gX_ResourceSlot_Find(gX_I32 type) {
    for (gX_U32 i = 0; i < GX_RESOURCE_SLOT_COUNT; i++) {
        if (resources.slots[i].active && resources.slots[i].type == type)
            return &resources.slots[i];
    }
    return 0;
}

gX_ResourceSlot *gX_ResourceSlot_Add(gX_I32 type) {
    // Reuse inactive slot or use first empty
    gX_ResourceSlot *slot = 0;
    for (gX_U32 i = 0; i < GX_RESOURCE_SLOT_COUNT; i++) {
        if (!resources.slots[i].active) { slot = &resources.slots[i]; break; }
    }
    if (!slot) return 0;
    
    slot->active = true;
    slot->type = type;
    
    gX_U32 blocksize = (gX_U32)gX_Struct_GetSize(type);
    gX_Heap_Zero(&slot->heap);
    gX_Heap_SetBlockSize(&slot->heap, blocksize);
    gX_Heap_SetMax(&slot->heap, 128);
    gX_Heap_SetAmount(&slot->heap, 1);
    gX_Heap_SetType(&slot->heap, type);
    slot->heap.state.ENABLE_FILENAME = true;
    slot->heap.state.ENABLE_FILENAME2 = true;
    gX_Heap_Init(&slot->heap);
    
    switch (type) {
        case gX_DATATYPE_STRUCT_FONT:    strcpy(slot->name, "fontheap");    strcpy(slot->directory, "resources/font/"); break;
        case gX_DATATYPE_STRUCT_MODEL:   strcpy(slot->name, "modelheap");   strcpy(slot->directory, "resources/model/"); break;
        case gX_DATATYPE_STRUCT_MUSIC:   strcpy(slot->name, "musicheap");   strcpy(slot->directory, "resources/music/"); break;
        case gX_DATATYPE_STRUCT_SHADER:  strcpy(slot->name, "shaderheap");  strcpy(slot->directory, "resources/shader/"); break;
        case gX_DATATYPE_STRUCT_SOUND:   strcpy(slot->name, "audioheap");   strcpy(slot->directory, "resources/audio/"); break;
        case gX_DATATYPE_STRUCT_TEXTURE: strcpy(slot->name, "textureheap"); strcpy(slot->directory, "resources/texture/"); break;
        default:                         strcpy(slot->name, "resourceheap"); break;
    }
    gX_Heap_SetName(&slot->heap, slot->name);
    gX_Heap_SetDescription(&slot->heap, slot->directory);
    
    //opencode: allocate mtime array for hot-reload tracking
    slot->loadMtimes = (gX_I64*)calloc(128, sizeof(gX_I64));
    
    return slot;
}

gX_Heap *gX_ResourceManager_GetHeap_Type(gX_U32 type) {
    gX_ResourceSlot *slot = gX_ResourceSlot_Find((gX_I32)type);
    return slot ? &slot->heap : 0;
}

gX_I32 gX_ResourceManager_Load(gX_I0) {
    gX_TRACELOG_PROLOGUE("ResourceManager_Load","RESOURCE: Loading resourcemanager")
    
    strcpy(resources.name,"[gX] ResourceManager");
    strcpy(resources.description,"resourcemanager");
    gX_TraceLog_RESOURCE(gX_LOG_GREEN,"name: %s",resources.name);
    gX_TraceLog_RESOURCE(gX_LOG_GREEN,"desc: %s",resources.description);
    
    resources.defaultsLoaded = false;
    resources.defaultsLoadAsap = true;
    
    gX_TRACELOG_EPILOGUE("RESOURCE: Loading done")
    return 0;
}

gX_I32 gX_ResourceManager_Unload(gX_I0) {
    gX_TRACELOG_PROLOGUE("ResourceManager_Unload","RESOURCE: Unloading resourcemanager")
    
    for (gX_U32 i = 0; i < GX_RESOURCE_SLOT_COUNT; i++) {
        if (resources.slots[i].active) {
            gX_RMHeap_Clear(&resources.slots[i].heap);
            gX_Heap_DeInit(&resources.slots[i].heap);
            free(resources.slots[i].loadMtimes);
            resources.slots[i].loadMtimes = NULL;
            resources.slots[i].active = false;
        }
    }
    resources.defaultsLoaded = false;
    
    gX_TRACELOG_EPILOGUE("RESOURCE: Unloading done")
    return 0;
}

gX_I0 gX_ResourceManager_Clear(gX_I0) {
    for (gX_U32 i = 0; i < GX_RESOURCE_SLOT_COUNT; i++) {
        if (resources.slots[i].active)
            gX_RMHeap_Clear(&resources.slots[i].heap);
    }
}

gX_I0 gX_ResourceManager_Update(gX_I0) {
    if (resources.defaultsLoadAsap && !(resources.defaultsLoaded) && maindata.backendLoaded) {
        resources.defaultsLoadAsap = false;
        gX_TraceLog_RESOURCE(gX_LOG_INFO, "Loading default resources (slot count: %u)...",
                             (gX_U32)GX_RESOURCE_SLOT_COUNT);
        gX_F64 start = gX_Time_Get();
        gX_ResourceManager_Load_Defaults();
        gX_F64 elapsed = (gX_Time_Get() - start) * 1000.0;
        gX_TraceLog_RESOURCE(gX_LOG_INFO, "Default resources loaded in %.0f ms", elapsed);
        resources.defaultsLoaded = true;
    }

    //opencode: hot-reload — check file modification times once per second
    {
        static gX_U32 frameCount = 0;
        if (++frameCount % 60 == 0) {  //opencode: once per second at 60fps
            for (gX_U32 si = 0; si < GX_RESOURCE_SLOT_COUNT; si++) {
                gX_ResourceSlot *slot = &resources.slots[si];
                if (!slot->active || !slot->loadMtimes) continue;
                gX_Heap *h = &slot->heap;
                for (gX_U32 hi = 0; hi < h->amount; hi++) {
                    if (!h->meta[hi].USED) continue;
                    const gX_I8 *fn = h->fileName[hi];
                    if (!fn || !fn[0]) continue;
                    gX_I8 fullPath[1024];
                    snprintf(fullPath, sizeof(fullPath), "%s%s", slot->directory, fn);
                    struct stat st;
                    if (stat(fullPath, &st) != 0) continue;
                    gX_I64 fileMtime = (gX_I64)st.st_mtime;
                    if (fileMtime != slot->loadMtimes[hi]) {
                        if (slot->loadMtimes[hi] == 0) {
                            slot->loadMtimes[hi] = fileMtime;
                        } else {
                            //opencode: file changed — reload via RM
                            slot->loadMtimes[hi] = st.st_mtime;
                            const gX_I8 *fn2 = h->fileName2 ? h->fileName2[hi] : NULL;
                            gX_RMHeap_Reload(h, hi, fn, fn2);
                            gX_TraceLog(gX_LOG_NOTICE,
                                "RESOURCE: hot-reloaded [%s] %s", slot->name, fn);
                        }
                    }
                }
            }
        }
    }
}

// ========================================================================
// Unified resource loader
// ========================================================================

gX_I0 *gX_Resource_Load(gX_I32 type, const gX_I8 *subpath) {
    gX_ResourceSlot *slot = gX_ResourceSlot_Find(type);
    if (!slot) {
        slot = gX_ResourceSlot_Add(type);
        if (!slot) return NULL;
    }
    gX_U32 oldCount = slot->heap.amount;
    gX_I0 *result = gX_RMHeap_CheckElseLoadFileName(&slot->heap, slot->directory, subpath, NULL);
    if (result && slot->heap.amount > oldCount) {
        //opencode: new resource loaded — record mtime for hot-reload
        gX_U32 idx = slot->heap.amount - 1;
        gX_I8 fp[1024]; snprintf(fp, sizeof(fp), "%s%s", slot->directory, subpath);
        struct stat st;
        if (stat(fp, &st) == 0) slot->loadMtimes[idx] = st.st_mtime;
    }
    return result;
}

gX_I0 *gX_Resource_Load2(gX_I32 type, const gX_I8 *subpath1, const gX_I8 *subpath2) {
    gX_ResourceSlot *slot = gX_ResourceSlot_Find(type);
    if (!slot) {
        slot = gX_ResourceSlot_Add(type);
        if (!slot) return NULL;
    }
    gX_U32 oldCount = slot->heap.amount;
    gX_I0 *result = gX_RMHeap_CheckElseLoadFileName(&slot->heap, slot->directory, subpath1, subpath2);
    if (result && slot->heap.amount > oldCount) {
        gX_U32 idx = slot->heap.amount - 1;
        gX_I8 fp[1024]; snprintf(fp, sizeof(fp), "%s%s", slot->directory, subpath1 ? subpath1 : subpath2);
        struct stat st;
        if (stat(fp, &st) == 0) slot->loadMtimes[idx] = st.st_mtime;
    }
    return result;
}

gX_Shader *gX_Resource_LoadShaderFrag(const gX_I8 *frag) {
    gX_ResourceSlot *slot = gX_ResourceSlot_Find(gX_DATATYPE_STRUCT_SHADER);
    if (!slot) {
        slot = gX_ResourceSlot_Add(gX_DATATYPE_STRUCT_SHADER);
        if (!slot) return NULL;
    }
    gX_U32 oldCount = slot->heap.amount;
    gX_I0 *result = gX_RMHeap_LoadFileName(&slot->heap, slot->directory, NULL, frag);
    if (result && slot->heap.amount > oldCount && frag) {
        gX_U32 idx = slot->heap.amount - 1;
        gX_I8 fp[1024]; snprintf(fp, sizeof(fp), "%s%s", slot->directory, frag);
        struct stat st;
        if (stat(fp, &st) == 0) slot->loadMtimes[idx] = (gX_I64)st.st_mtime;
    }
    return (gX_Shader*)result;
}
