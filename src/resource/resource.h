/**********************************************************************************************
*
*   [gX: src/resource/resource.h]
*   [resource/resource.h] — Resource manager (ported from gameX)
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

#ifndef RESOURCE_H
#define RESOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/heap.h"
#include "../common/enum.h"

//opencode: typed resource slot replacing the old "heap of heaps" pattern
#define GX_RESOURCE_SLOT_COUNT 6

typedef struct gX_ResourceSlot {
    gX_I1      active;
    gX_I32     type;
    gX_TextLine directory;
    gX_TextLine name;
    gX_Heap     heap;
    gX_I64     *loadMtimes;  //opencode: per-slot mtime tracking for hot-reload
} gX_ResourceSlot;

typedef struct gX_ResourceManager {
    gX_I8 name[128];
    gX_I8 description[256];
    
    gX_I1  defaultsLoadAsap;
    gX_I1  defaultsLoaded;
    gX_I1  defaultsReload;
    
    gX_U32 state;
    
    gX_ResourceSlot slots[GX_RESOURCE_SLOT_COUNT];
} gX_ResourceManager;

extern gX_ResourceManager resources;

gX_I32 gX_ResourceManager_Load(gX_I0);
gX_I32 gX_ResourceManager_Unload(gX_I0);
gX_I0  gX_ResourceManager_Clear(gX_I0);

gX_I32 gX_ResourceManager_Load_Defaults(gX_I0);

gX_I0 gX_ResourceManager_Update(gX_I0);

gX_Heap *gX_ResourceManager_GetHeap_Type(gX_U32 type);

// RMHeap — per-heap resource operations (takes gX_Heap*, works on any slot's heap)
gX_I0  gX_RMHeap_Clear(          gX_Heap *rmheap);
gX_I0 *gX_RMHeap_CheckElseLoad(  gX_Heap *rmheap,               const gX_I8 *path1, const gX_I8 *path2);
gX_I0 *gX_RMHeap_CheckElseLoadFileName(gX_Heap *rmheap, const gX_I8 *dir, const gX_I8 *fn1, const gX_I8 *fn2);
gX_I0 *gX_RMHeap_Load(           gX_Heap *rmheap,               const gX_I8 *path1, const gX_I8 *path2);
gX_I0 *gX_RMHeap_Reload(         gX_Heap *rmheap, gX_U32 index, const gX_I8 *path1, const gX_I8 *path2);
gX_I0 *gX_RMHeap_LoadFileName(   gX_Heap *rmheap, const gX_I8 *dir, const gX_I8 *fn1,   const gX_I8 *fn2);
gX_I0  gX_RMHeap_Unload(         gX_Heap *rmheap, gX_U32 index);

gX_I0 *gX_RMHeap_GetPtr_FileName( gX_Heap *rmheap, const gX_I8 *filename);
gX_I0 *gX_RMHeap_GetPtr_FileName2(gX_Heap *rmheap, const gX_I8 *filename2);

// Resource slot helpers (replaces old AddHeap/RemoveHeap/getters)
gX_ResourceSlot *gX_ResourceSlot_Find(gX_I32 type);
gX_ResourceSlot *gX_ResourceSlot_Add(gX_I32 type);

//opencode: unified resource loader — routes through the resource manager
//   heap for the given type. subpath is relative to the slot's base dir
//   (e.g. "misc/testcard.png" for textures -> "resources/texture/misc/testcard.png").
//   Returns pointer to the resource in the heap, or NULL. The pointer is
//   the actual resource struct (gX_Texture*, gX_Model*, etc.), not a wrapper.
gX_I0 *gX_Resource_Load(gX_I32 type, const gX_I8 *subpath);
gX_I0 *gX_Resource_Load2(gX_I32 type, const gX_I8 *subpath1, const gX_I8 *subpath2);

//opencode: convenience wrappers for common types
#define gX_Resource_LoadTexture(subpath)    ((gX_Texture*)  gX_Resource_Load(gX_DATATYPE_STRUCT_TEXTURE, subpath))
#define gX_Resource_LoadModel(subpath)      ((gX_Model*)    gX_Resource_Load(gX_DATATYPE_STRUCT_MODEL,   subpath))
#define gX_Resource_LoadSound(subpath)      ((gX_Sound*)    gX_Resource_Load(gX_DATATYPE_STRUCT_SOUND,   subpath))
#define gX_Resource_LoadMusic(subpath)      ((gX_Music*)    gX_Resource_Load(gX_DATATYPE_STRUCT_MUSIC,   subpath))
#define gX_Resource_LoadShader(vert, frag)  ((gX_Shader*)   gX_Resource_Load2(gX_DATATYPE_STRUCT_SHADER,  vert, frag))
gX_Shader *gX_Resource_LoadShaderFrag(const gX_I8 *frag);

#ifdef __cplusplus
}
#endif

#endif //RESOURCE_H
