/**********************************************************************************************
*
*   [gX: src/common/marshal/json_struct.h]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu
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


#ifndef GX_JSON_STRUCT_H
#define GX_JSON_STRUCT_H

#include "../types.h"
#include "json_impl.h"

typedef struct gX_JSON_Object {
    gX_I1 loaded;
    gX_I1 hasUpper;
    json_type type;
    gX_I32 datatype;
    gX_I8 key[128];
    struct json_object *object;
    struct gX_JSON_Object *upper;
    gX_I0 *dataPtr;
    //opencode: capacity of the target buffer for STRING fields. Set by
    //   GX_SCHEMAJSON_ADDOBJECT_STRING (sizeof of the fixed array). Loaders
    //   use it to bound string copies — previously unbounded strcpy could
    //   overflow element blocks from hand-edited JSON.
    gX_U32 dataSize;
    gX_TextLine dataRemark;
    gX_F32v4 defaultValue;
} gX_JSON_Object;

//opencode: arena capacity defaults — the GUI canvas marshal is the only
//   schema that can grow large (128 elements × modules); settings/style
//   allocate their own small arenas via gX_JSON_Schema_EnsureArena.
#if defined(GX_TIER) && GX_TIER == 3
#define gX_JSON_SCHEMA_MAXOBJECTS 128
#else
#define gX_JSON_SCHEMA_MAXOBJECTS 8192
#endif
typedef struct gX_JSON_Schema {
    gX_TextLine name;
    gX_TextLine description;
    gX_TextLine created_date;
    gX_TextLine created_time;
    gX_TextLine author;
    gX_TextLine comments;
    
    gX_U32 loadedCount;
    gX_U32 objectsCount;
    //opencode: heap arena, allocated once per schema on first use
    //   (settings 64, style 128, GUI 8192 when marshaling). Static
    //   embedding cost ~2.5 MB per schema for 64-object schemas.
    gX_JSON_Object *objects;
} gX_JSON_Schema;

typedef struct gX_JSON {
    
    struct json_object *root;
    
    gX_JSON_Schema *schema;
    
} gX_JSON;

#endif //GX_JSON_STRUCT_H
