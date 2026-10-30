/**********************************************************************************************
*
*   [gX: src/common/marshal/json.h]
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


#ifndef GX_JSON_H
#define GX_JSON_H

#include "json_struct.h"

#include "../types.h"
#include "../utils.h"
#include "../enum.h"
#include "../log.h"

#include "json_impl.h"

typedef struct gX_JSONschemaContext {
    gX_I1 JSONschemaDebug;
    gX_I1 schemaTruncated;   //opencode: set when object limit hit (warn once)
    gX_JSON_Object *currentObject;
    gX_I32 currentObjectIndex;
    gX_JSON_Object *currentContext[8];
    gX_I32 currentContextStack;
} gX_JSONschemaContext;

extern gX_JSONschemaContext jsoncontext;

#define GX_SCHEMAJSON_INIT \
    jsoncontext.JSONschemaDebug = false; \
    jsoncontext.schemaTruncated = false; \
    jsoncontext.currentObject = 0; \
    jsoncontext.currentObjectIndex = -1; \
    jsoncontext.currentContextStack = -1; \

#define GX_SCHEMAJSON_ADDOBJECT_TEMPLATE(where,name) \
    jsoncontext.currentObjectIndex++; \
    /*opencode: bounds guard — a rich canvas (128 elements × modules) can \
       exceed gX_JSON_SCHEMA_MAXOBJECTS; previously wrote past the global \
       objects[] array (silent corruption). Once full, currentObject becomes \
       NULL and the field-setting macros below skip (no return — the \
       ADDOBJECT macros are also used in non-void functions).*/ \
    if (jsoncontext.currentObjectIndex >= gX_JSON_SCHEMA_MAXOBJECTS) { \
        if (!jsoncontext.schemaTruncated) { \
            gX_TraceLog(gX_LOG_WARNING, "JSON_SCHEMA: object limit reached (%d) — schema truncated", gX_JSON_SCHEMA_MAXOBJECTS); \
            jsoncontext.schemaTruncated = true; \
        } \
        jsoncontext.currentObject = 0; \
    } else { \
        jsoncontext.currentObject = &(where.objects[(gX_U32)jsoncontext.currentObjectIndex]); \
        strcpy(jsoncontext.currentObject->key,name); \
        jsoncontext.currentObject->loaded = true; \
    } \
    
#define GX_SCHEMAJSON_ADDOBJECT_CONTEXT(name) \
    GX_SCHEMAJSON_ADDOBJECT(name) \
    if (jsoncontext.currentObject) { \
        if (jsoncontext.currentContextStack >= 0) { \
            jsoncontext.currentObject->hasUpper = true; \
            jsoncontext.currentObject->upper = jsoncontext.currentContext[jsoncontext.currentContextStack]; \
        } \
        if (jsoncontext.JSONschemaDebug) { \
            gX_TraceLog(gX_LOG_DEBUG,"JSON_SCHEMA: <%s> currentContext[%i]: %s", \
            name,jsoncontext.currentContextStack,jsoncontext.currentContext[jsoncontext.currentContextStack]->key); \
        } \
    } \
    
#define GX_SCHEMAJSON_ADDOBJECT_TERM(name,ptr,type) \
    GX_SCHEMAJSON_ADDOBJECT_CONTEXT(#name) \
    if (jsoncontext.currentObject) { \
        jsoncontext.currentObject->dataPtr = &ptr; \
        jsoncontext.currentObject->datatype = gX_DATATYPE_##type; \
        strcpy(jsoncontext.currentObject->dataRemark,#ptr); \
    }

//opencode: STRING registration — records the target buffer capacity via
//   sizeof() so loaders can bound copies (ptr must be a fixed-size array).
#define GX_SCHEMAJSON_ADDOBJECT_STRING(name,ptr) \
    GX_SCHEMAJSON_ADDOBJECT_CONTEXT(#name) \
    if (jsoncontext.currentObject) { \
        jsoncontext.currentObject->dataPtr = ptr; \
        jsoncontext.currentObject->datatype = gX_DATATYPE_STRING; \
        jsoncontext.currentObject->dataSize = (gX_U32)sizeof(ptr); \
        strcpy(jsoncontext.currentObject->dataRemark,#ptr); \
    }
    
#define GX_SCHEMAJSON_PUSHCONTEXT \
    jsoncontext.currentContextStack++; \
    jsoncontext.currentContext[jsoncontext.currentContextStack] = jsoncontext.currentObject; \
    
#define GX_SCHEMAJSON_POPCONTEXT \
    jsoncontext.currentContext[jsoncontext.currentContextStack] = 0; \
    jsoncontext.currentContextStack--; \
    
#define GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT(name) \
    GX_SCHEMAJSON_ADDOBJECT_CONTEXT(#name) \
    GX_SCHEMAJSON_PUSHCONTEXT \
    
#define GX_SCHEMAJSON_ADDOBJECT_PUSHCONTEXT_STRING(name) \
    GX_SCHEMAJSON_ADDOBJECT_CONTEXT(name) \
    GX_SCHEMAJSON_PUSHCONTEXT \
    
//do not use, will return first instance with the key name
gX_JSON_Object *gX_JSON_Schema_SearchObject(gX_JSON_Schema *schema, const gX_I8 *key);

//opencode: allocate the schema object arena once (program lifetime). Schemas
//   size their own arenas: settings 64, style 128, GUI 8192 on first marshal.
gX_I32 gX_JSON_Schema_EnsureArena(gX_JSON_Schema *schema, gX_U32 capacity);

extern gX_JSON json;

gX_I32 gX_JSON_Init(gX_I0);
gX_I32 gX_JSON_Deinit(gX_I0);

gX_I32 gX_JSON_Load(const gX_I8 *path);
gX_I32 gX_JSON_Save(const gX_I8 *path);

gX_I32 gX_JSON_Root_Print(gX_I0);

#endif //GX_JSON_H
