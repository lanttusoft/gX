/**********************************************************************************************
*
*   [gX: src/resource/loaddefaults.c]
*   [resource/loaddefaults.c] — Default resource loading (ported from gameX)
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

#include "resource.h"

#include "../common/log.h"
#include "../common/enum.h"

#define GX_RM_HEAPS \
    X(font,FONT) \
    X(model,MODEL) \
    X(music,MUSIC) \
    X(shader,SHADER) \
    X(sound,SOUND) \
    X(texture,TEXTURE) \

//opencode: uses gX_ResourceSlot_Add + GetHeap_Type (no more heap-of-heaps)
#define GX_RM_HEAP_MAKE(namelc,nameuc) \
    do { gX_ResourceSlot *_s = gX_ResourceSlot_Find(gX_DATATYPE_STRUCT_##nameuc); \
         if (!_s) gX_ResourceSlot_Add(gX_DATATYPE_STRUCT_##nameuc); } while(0); \
    gX_Heap *namelc##Heap = gX_ResourceManager_GetHeap_Type(gX_DATATYPE_STRUCT_##nameuc); \
    (gX_I0)namelc##Heap; \

#define GX_RM_HEAP_REFRESH(namelc,nameuc) \
    namelc##Heap = gX_ResourceManager_GetHeap_Type(gX_DATATYPE_STRUCT_##nameuc); \

#define GX_RM_ADD1(what,name1) \
    gX_RMHeap_CheckElseLoadFileName(what##Heap, "resources/"#what"/", name1, 0); \

#define GX_RM_ADD1A(what,what2,name1) \
    gX_RMHeap_CheckElseLoadFileName(what##Heap, "resources/"#what2"/", name1, 0); \

#define GX_RM_ADD2(what,name2) \
    gX_RMHeap_CheckElseLoadFileName(what##Heap, "resources/"#what"/", 0, name2); \

#define GX_RM_ADD12(what,name1,name2) \
    gX_RMHeap_CheckElseLoadFileName(what##Heap, "resources/"#what"/", name1, name2); \

#define GX_RM_ADD_FONT(name1) \
    GX_RM_ADD1(font,name1) \

#define GX_RM_ADD_MODEL(name1) \
    GX_RM_ADD1(model,name1) \

#define GX_RM_ADD_MUSIC(name1) \
    GX_RM_ADD1(music,name1) \

#define GX_RM_ADD_SHADER_V(name1) \
    GX_RM_ADD1(shader,name1) \

#define GX_RM_ADD_SHADER_F(name2) \
    GX_RM_ADD2(shader,name2) \

#define GX_RM_ADD_SHADER_VF(name1,name2) \
    GX_RM_ADD12(shader,name1,name2) \

#define GX_RM_ADD_SOUND(name1) \
    GX_RM_ADD1A(sound,audio,name1) \

#define GX_RM_ADD_TEXTURE(name1) \
    GX_RM_ADD1(texture,name1) \

//ai: default preloads use shipped resources only; quarantined models remain explicit author inputs.
gX_I32 gX_ResourceManager_Load_Defaults(gX_I0) {
    gX_TRACELOG_PROLOGUE("ResourceManager_Load","RESOURCE: Loading defaults")
    
    gX_TraceLog_SuppressStart();
    
#define X(lc,uc) GX_RM_HEAP_MAKE(lc,uc)
    GX_RM_HEAPS
#undef X
#define X(lc,uc) GX_RM_HEAP_REFRESH(lc,uc)
    GX_RM_HEAPS
#undef X
    
    //opencode: heap fonts are unread (builders use uiFont directly) — the two
    //   broken Kenney preloads only burned atlas builds; Teletext stays.
    GX_RM_ADD_FONT("EuropeanTeletext.ttf")
    //opencode: gX — FZKai (14M CJK font) dropped from the default set
    
    GX_RM_ADD_MODEL("teapot.glb")
    GX_RM_ADD_MODEL("primitive/cube.glb")
    GX_RM_ADD_MODEL("primitive/icosahedron.glb")
    GX_RM_ADD_MODEL("primitive/icosahedron2.glb")
    GX_RM_ADD_MODEL("primitive/icosahedron3.glb")
    GX_RM_ADD_MODEL("primitive/cone.glb")
    GX_RM_ADD_MODEL("primitive/sphere.glb")
    GX_RM_ADD_MODEL("primitive/cylinder.glb")
    GX_RM_ADD_MODEL("primitive/torus.glb")
    
    GX_RM_ADD_SHADER_VF("diffuse.vert","diffuse.frag")
    GX_RM_ADD_SHADER_VF("model.vert","refractcube.frag")
    GX_RM_ADD_SHADER_VF("map.vert","map.frag")
    GX_RM_ADD_SHADER_F("lens.frag")
    GX_RM_ADD_SHADER_F("guitexture_gradient.frag")
    GX_RM_ADD_SHADER_F("guigradient.frag")
    
    gX_TraceLog_SuppressEnd();
    
    gX_TRACELOG_EPILOGUE("RESOURCE: Loading done")
    return 0;
}
