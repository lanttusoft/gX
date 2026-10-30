/**********************************************************************************************
*
*   [gX: src/common/hashmap.h]
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


#ifndef GX_HASHMAP_H
#define GX_HASHMAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

#define GX_HASHMAP_TOMBSTONE ((gX_I0*)1)

//opencode: entire hashmap implementation — FNV-1a 64-bit hash, Robin Hood probing, open addressing, auto-resize, tombstone deletion
typedef struct gX_HashMap_Entry {
    gX_U64      hash;
    const gX_I0 *key;
    gX_U32      keyLen;
    gX_I0      *value;
} gX_HashMap_Entry;

typedef struct gX_HashMap_State {
    gX_U32  INITIALIZED     : 1;
    gX_U32  RESIZING        : 1;
    gX_U32  PADDING         : 30;
} gX_HashMap_State;

typedef struct gX_HashMap {
    gX_HashMap_State    state;
    gX_HashMap_Entry   *entries;
    gX_U32              capacity;
    gX_U32              count;
    gX_F32              loadFactor;
} gX_HashMap;

gX_I0 gX_HashMap_Init(      gX_HashMap *map, gX_U32 initialCapacity);
gX_I0 gX_HashMap_DeInit(    gX_HashMap *map);

gX_U64 gX_HashMap_Hash(     const gX_I0 *key, gX_U32 keyLen);

gX_I0 *gX_HashMap_Set(      gX_HashMap *map, const gX_I0 *key, gX_U32 keyLen, gX_I0 *value);
gX_I0 *gX_HashMap_Get(      gX_HashMap *map, const gX_I0 *key, gX_U32 keyLen);
gX_I0 *gX_HashMap_Remove(   gX_HashMap *map, const gX_I0 *key, gX_U32 keyLen);

#ifdef __cplusplus
}
#endif

#endif //GX_HASHMAP_H
