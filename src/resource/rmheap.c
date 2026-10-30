/**********************************************************************************************
*
*   [gX: src/resource/rmheap.c]
*   [resource/rmheap.c] — Resource heap management (ported from gameX)
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

#include <string.h>
#include <stdio.h>

#include "resource.h"

#include "../common/log.h"
#include "../common/enum.h"
#include "../common/utils.h"

#include "../wrappers/wrappers.h"

#include "../main.h"

gX_I0 gX_RMHeap_Clear(gX_Heap *rmheap) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"RMHeap_Clear","RESOURCE_HEAP: Clearing heap")
    for (gX_U32 i = 0; i < rmheap->amount; i++) {
        gX_TraceLog_RESOURCE(gX_LOG_TRACE,"i: %i",i);
        gX_RMHeap_Unload(rmheap, i);
    }
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Heap cleared")
}

#define RMHEAP_LOAD_SLOT(lc,mc,uc) \
    case gX_DATATYPE_STRUCT_##uc: { \
        gX_##mc *lc##Ptr = slotPtr; \
        *lc##Ptr = gX_##mc##_Load(path1); \
        break; \
    } \
    
#define RMHEAP_LOAD_SLOT2(lc,mc,uc) \
    case gX_DATATYPE_STRUCT_##uc: { \
        gX_##mc *lc##Ptr = slotPtr; \
        const gX_I8 *p1 = (path1 && path1[0]) ? path1 : NULL; \
        const gX_I8 *p2 = (path2 && path2[0]) ? path2 : NULL; \
        *lc##Ptr = gX_##mc##_Load(p1, p2); \
        break; \
    } \

static gX_I0 gX_RMHeap_Load_Slot(gX_I0 *slotPtr, gX_I32 type, const gX_I8 *path1, const gX_I8 *path2) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"RMHeap_Load_Slot","RESOURCE_HEAP: Loading slot")
    gX_TraceLog_RESOURCE(gX_LOG_TRACE,"type: %i",type);
    gX_TraceLog_RESOURCE(gX_LOG_TRACE,"path1: %s",path1);
    gX_TraceLog_RESOURCE(gX_LOG_TRACE,"path2: %s",path2);
    switch (type) {
        RMHEAP_LOAD_SLOT(texture,Texture,TEXTURE)
        RMHEAP_LOAD_SLOT2(shader,Shader,SHADER)
        RMHEAP_LOAD_SLOT(font,Font,FONT)
        RMHEAP_LOAD_SLOT(model,Model,MODEL)
        RMHEAP_LOAD_SLOT(sound,Sound,SOUND)
        RMHEAP_LOAD_SLOT(music,Music,MUSIC)
        default: break;
    }
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Slot loaded")
}

static gX_I0 gX_RMHeap_LoadValues_Slot(gX_I0 *slotPtr, gX_I32 type, gX_F32 val1, gX_F32 val2) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"RMHeap_LoadValues_Slot","RESOURCE_HEAP: Loading values to slot")
    gX_TraceLog_RESOURCE(gX_LOG_TRACE,"type: %i",type);
    gX_TraceLog_RESOURCE(gX_LOG_TRACE,"val1: %.3f",val1);
    gX_TraceLog_RESOURCE(gX_LOG_TRACE,"val2: %.3f",val2);
    switch (type) {
        case gX_DATATYPE_STRUCT_RENDERTEXTURE: {
            gX_RenderTexture *rtPtr = slotPtr;
            *rtPtr = gX_RenderTexture_Load((gX_U32)val1,(gX_U32)val2);
            break;
        }
        default: break;
    }
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Slot loaded")
}

//opencode: zero the slot without calling the backend unload wrapper.
//   Renderers unload their OWN copies of GPU resources (textures, shaders,
//   models) during gX_Renderer_Unload(). This RM slot has a duplicate copy
//   with the same GPU resource ID. Calling the wrapper again = double-free
//   (heap corruption, exit 134). The RM slot data is just tracking metadata —
//   actual GPU cleanup happens in rlglClose() at shutdown or when the
//   renderer that loaded it explicitly unloads.
#define RMHEAP_UNLOAD_SLOT(lc,mc,uc) \
    case gX_DATATYPE_STRUCT_##uc: { \
        gX_##mc *lc##Ptr = slotPtr; \
        memset(lc##Ptr, 0, sizeof(gX_##mc)); \
        break; \
    } \

static gX_I0 gX_RMHeap_Unload_Slot(gX_I0 *slotPtr, gX_I32 type) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"RMHeap_Unload_Slot","RESOURCE_HEAP: Unloading slot")
    gX_TraceLog_RESOURCE(gX_LOG_TRACE,"type: %i",type);
    switch (type) {
        RMHEAP_UNLOAD_SLOT(texture,Texture,TEXTURE)
        RMHEAP_UNLOAD_SLOT(rt,RenderTexture,RENDERTEXTURE)
        RMHEAP_UNLOAD_SLOT(shader,Shader,SHADER)
        RMHEAP_UNLOAD_SLOT(font,Font,FONT)
        RMHEAP_UNLOAD_SLOT(model,Model,MODEL)
        RMHEAP_UNLOAD_SLOT(sound,Sound,SOUND)
        RMHEAP_UNLOAD_SLOT(music,Music,MUSIC)
        default: break;
    }
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Unloading done")
}

gX_I0 *gX_RMHeap_Load(gX_Heap *rmheap, const gX_I8 *path1, const gX_I8 *path2) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"RMHeap_Load","RESOURCE_HEAP: Loading to heap")
    gX_I32 slotIndex = -1;
    gX_I0 *slot = gX_Heap_Add_FirstEmpty_NoPtr_GetIndex(rmheap,&slotIndex);
    if (!slot) {
        gX_TraceLog_RESOURCE(gX_LOG_ERROR, "Failed to allocate slot in heap (type %i)", rmheap->type);
        gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Load failed")
        return 0;
    }
    gX_Heap_ClearBlock(rmheap, (gX_U32)slotIndex);
    
    gX_TraceLog_RESOURCE(gX_LOG_TRACE,"slotIndex %i",slotIndex);
    
    gX_RMHeap_Load_Slot(slot,rmheap->type,path1,path2);
    
    if (path1) gX_Heap_SetFileName( rmheap, (gX_U32)slotIndex, path1);
    if (path2) gX_Heap_SetFileName2(rmheap, (gX_U32)slotIndex, path2);
    gX_TraceLog_RESOURCE(gX_LOG_INFO, "Loaded [%s]%s type=%i slot=%i", path1 ? path1 : "", path2 ? path2 : "", rmheap->type, slotIndex);
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Loading done")
    return slot;
}

gX_I0 *gX_RMHeap_LoadFileName(gX_Heap *rmheap, const gX_I8 *dir, const gX_I8 *fn1, const gX_I8 *fn2) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"RMHeap_Load","RESOURCE_HEAP: Loading to heap")
    gX_I32 slotIndex = -1;
    gX_I0 *slot = gX_Heap_Add_FirstEmpty_NoPtr_GetIndex(rmheap,&slotIndex);
    gX_Heap_ClearBlock(rmheap, (gX_U32)slotIndex);
    
    gX_TraceLog_RESOURCE(gX_LOG_TRACE,"slotIndex %i",slotIndex);
    
    //opencode: use snprintf to prevent buffer overflow on long paths
    gX_I8 path1[1024] = {0};
    gX_I8 path2[1024] = {0};
    if (fn1) snprintf(path1, sizeof(path1), "%s%s", dir, fn1);
    if (fn2) snprintf(path2, sizeof(path2), "%s%s", dir, fn2);
    gX_RMHeap_Load_Slot(slot,rmheap->type,path1,path2);
    
    if (fn1) gX_Heap_SetFileName( rmheap, (gX_U32)slotIndex, fn1);
    if (fn2) gX_Heap_SetFileName2(rmheap, (gX_U32)slotIndex, fn2);
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Loading done")
    return slot;
}

gX_I0 *gX_RMHeap_Reload(gX_Heap *rmheap, gX_U32 index, const gX_I8 *path1, const gX_I8 *path2) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"RMHeap_Reload","RESOURCE_HEAP: Reloading to heap")
    gX_I0 *slot = gX_Heap_SeekPtr_Index(rmheap,index);

    gX_RMHeap_Unload_Slot(slot, rmheap->type);
    gX_RMHeap_Load_Slot(slot,rmheap->type,path1,path2);
    
    if (path1) gX_Heap_SetFileName( rmheap, index, path1);
    if (path2) gX_Heap_SetFileName2(rmheap, index, path2);
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Reloading done")
    return slot;
}

gX_I0 *gX_RMHeap_LoadValues(gX_Heap *rmheap, gX_F32 val1, gX_F32 val2) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"RMHeap_LoadValues","RESOURCE_HEAP: Loading values to heap")
    
    //opencode: fix index — use GetIndex directly instead of broken SeekIndex_FirstEmpty
    gX_I32 slotIndex = -1;
    gX_I0 *slot = gX_Heap_Add_FirstEmpty_NoPtr_GetIndex(rmheap, &slotIndex);
    
    gX_RMHeap_LoadValues_Slot(slot,rmheap->type,val1,val2);
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Loading done")
    return slot;
}

gX_I0 *gX_RMHeap_ReloadValues(gX_Heap *rmheap, gX_U32 index, gX_F32 val1, gX_F32 val2) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"RMHeap_ReloadValues","RESOURCE_HEAP: Reloading values to heap")
    gX_I0 *slot = gX_Heap_SeekPtr_Index(rmheap,index);

    gX_RMHeap_Unload_Slot(slot, rmheap->type);
    gX_RMHeap_LoadValues_Slot(slot,rmheap->type,val1,val2);
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Reloading done")
    return slot;
}

gX_I0 gX_RMHeap_Unload(gX_Heap *rmheap, gX_U32 index) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"RMHeap_Unload","RESOURCE_HEAP: Unloading from heap")
    gX_I0 *slot = gX_Heap_Remove_Index_NoPtr(rmheap,index);
    
    gX_RMHeap_Unload_Slot(slot, rmheap->type);
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Unloading done")
}

//first check is resource exists: if not, load
gX_I0 *gX_RMHeap_CheckElseLoad(gX_Heap *rmheap, const gX_I8 *path1, const gX_I8 *path2) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"RMHeap_CheckElseLoad","RESOURCE_HEAP: Checking if loaded - loading if not")
    gX_I0 *result = 0;
    
    gX_I0 *testLoaded = 0;
    testLoaded = gX_RMHeap_GetPtr_FileName(rmheap, path1);
    if (testLoaded == 0) {
        gX_TraceLog_RESOURCE(gX_LOG_TRACE,"nothing found: loading");
        result = gX_RMHeap_Load(rmheap, path1, path2);
        gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Loading done")
    }
    else {
        gX_TraceLog_RESOURCE(gX_LOG_TRACE,"something found: returning ptr to resource");
        result = testLoaded;
        gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Checking done")
    }
    
    return result;
}

gX_I0 *gX_RMHeap_CheckElseLoadFileName(gX_Heap *rmheap, const gX_I8 *dir, const gX_I8 *fn1, const gX_I8 *fn2) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"RMHeap_CheckElseLoad","RESOURCE_HEAP: Checking if loaded - loading if not")
    gX_I0 *result = 0;
    
    gX_I0 *testLoaded = 0;
    if (fn1 && fn1[0]) {
        testLoaded = gX_RMHeap_GetPtr_FileName(rmheap, fn1);
    } else if (fn2 && fn2[0]) {
        //opencode: fragment-only shader — look up by fn2
        testLoaded = gX_RMHeap_GetPtr_FileName2(rmheap, fn2);
    }
    if (testLoaded == 0) {
        gX_TraceLog_RESOURCE(gX_LOG_TRACE,"nothing found: loading");
        result = gX_RMHeap_LoadFileName(rmheap, dir, fn1, fn2);
        gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Loading done")
    }
    else {
        gX_TraceLog_RESOURCE(gX_LOG_TRACE,"something found: returning ptr to resource");
        result = testLoaded;
        gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Checking done")
    }
    
    return result;
}

gX_I0 *gX_RMHeap_GetPtr_FileName( gX_Heap *rmheap, const gX_I8 *filename) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"RMHeap_GetPtr_FileName",
                         gX_Text_Format("RESOURCE_HEAP: Getting ptr for filename %s",filename))
    gX_I0 *result = 0;
    if (!filename || !filename[0]) {
        gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: null filename")
        return result;
    }
    gX_U32 i = 0;
    for (i = 0; i < rmheap->amount; i++) {
        gX_TraceLog_RESOURCE(gX_LOG_TRACE,"i: %i",i);
        if (!strcmp(rmheap->fileName[i],filename)) {
            gX_TraceLog_RESOURCE(gX_LOG_TRACE,"found: %i",i);
            result = gX_Heap_SeekPtr_Index(rmheap, i);
            break;
        }
    }
    if (!result) {
        gX_TraceLog_RESOURCE(gX_LOG_TRACE,"did not find anything");
    }
    
    gX_TraceLog_RESOURCE(gX_LOG_TRACE,"i: %i, p: 0x%x",i,result);
    
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Getting done")
    return result;
}

//opencode: filename2 lookup (searches fileName2[], used for fragment shader paths)
gX_I0 *gX_RMHeap_GetPtr_FileName2(gX_Heap *rmheap, const gX_I8 *filename2) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"RMHeap_GetPtr_FileName2",
                         gX_Text_Format("RESOURCE_HEAP: Getting ptr for filename2 %s",filename2))
    gX_I0 *result = 0;
    if (!filename2 || !filename2[0] || !rmheap->fileName2) return result;
    
    gX_U32 i = 0;
    for (i = 0; i < rmheap->amount; i++) {
        gX_TraceLog_RESOURCE(gX_LOG_TRACE,"i: %i",i);
        if (!strcmp(rmheap->fileName2[i],filename2)) {
            gX_TraceLog_RESOURCE(gX_LOG_TRACE,"found: %i",i);
            result = gX_Heap_SeekPtr_Index(rmheap, i);
            break;
        }
    }
    if (!result) {
        gX_TraceLog_RESOURCE(gX_LOG_TRACE,"did not find anything");
    }
    
    gX_TraceLog_RESOURCE(gX_LOG_TRACE,"i: %i, p: 0x%x",i,result);
    
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"RESOURCE_HEAP: Getting done")
    return result;
}
