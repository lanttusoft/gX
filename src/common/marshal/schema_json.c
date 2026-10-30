/**********************************************************************************************
*
*   [gX: src/common/marshal/schema_json.c]
*   JSON marshal/unmarshal using static offset-based schema field descriptors.
*
*   Copyright (c) 2026 lanttu, opencode
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

//opencode: suppress float-equal — default-value comparisons are intentional
#pragma GCC diagnostic ignored "-Wfloat-equal"

#include "schema_json.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "json_impl.h"

#include "../log.h"

// ---------------------------------------------------------------------------
// Forward declarations of recursive helpers
// ---------------------------------------------------------------------------
static gX_I32 schema_json_write_field(gX_SchemaField *f, gX_I0 *base,
                                       struct json_object *parent);

static gX_I32 schema_json_read_field(gX_SchemaField *f, gX_I0 *base,
                                      struct json_object *parent,
                                      const gX_I8 *parentPath);
static gX_I32 schema_json_load_from_obj_path(gX_SchemaField *fields,
                                             gX_U32 fieldCount,
                                             gX_I0 *base,
                                             struct json_object *parent,
                                             const gX_I8 *parentPath);

//opencode: retain useful nested paths in partial-schema diagnostics.
static gX_I0 schema_json_make_path(gX_I8 *path, gX_U32 capacity,
                                    const gX_I8 *parentPath,
                                    const gX_I8 *key)
{
    if (parentPath && parentPath[0])
        snprintf(path, capacity, "%s.%s", parentPath, key);
    else
        snprintf(path, capacity, "%s", key);
}

//opencode: append array indices without truncation warnings on deep paths.
static gX_I0 schema_json_make_index_path(gX_I8 *path, gX_U32 capacity,
                                          const gX_I8 *parentPath,
                                          gX_U32 index)
{
    gX_I8 digits[10];
    gX_U32 digitCount = 0;
    gX_U32 value = index;
    gX_U32 digitCapacity = (gX_U32)sizeof(digits);
    do
    {
        digits[digitCount++] = (gX_I8)('0' + value % 10);
        value /= 10;
    }
    while (value > 0 && digitCount < digitCapacity);
    gX_U32 suffixLength = digitCount + 2;
    if (capacity <= suffixLength)
    {
        if (capacity > 0) path[0] = 0;
        return;
    }
    gX_U32 parentLength = (gX_U32)strlen(parentPath);
    gX_U32 parentCapacity = capacity - suffixLength - 1;
    if (parentLength > parentCapacity) parentLength = parentCapacity;
    memcpy(path, parentPath, parentLength);
    gX_U32 offset = parentLength;
    path[offset++] = '[';
    for (gX_U32 i = 0; i < digitCount; i++)
        path[offset++] = digits[digitCount - i - 1];
    path[offset++] = ']';
    path[offset] = 0;
}

// ---------------------------------------------------------------------------
// Write (save) a single field into a json_object
// ---------------------------------------------------------------------------
static gX_I32 schema_json_write_field(gX_SchemaField *f, gX_I0 *base,
                                       struct json_object *parent)
{
    gX_I0 *fieldPtr = (gX_I0 *)((gX_U8 *)base + f->offset);

    //opencode: default-value skipping — omit fields whose runtime value
    // equals the declared default (keeps JSON clean)
    if (f->hasDefault)
    {
        gX_I1 matchesDefault = 0;
        switch (f->fieldType)
        {
            case GX_SCHEMA_BOOL:
                matchesDefault = (*(gX_I1 *)fieldPtr == (gX_I1)f->defaultValue);
                break;
            case GX_SCHEMA_SCALAR:
                switch (f->datatype)
                {
                    case gX_DATATYPE_F32:
                        matchesDefault = (*(gX_F32 *)fieldPtr == (gX_F32)f->defaultValue);
                        break;
                    case gX_DATATYPE_I1:
                        matchesDefault = (*(gX_I1 *)fieldPtr == (gX_I1)f->defaultValue);
                        break;
                    case gX_DATATYPE_I32:
                        matchesDefault = (*(gX_I32 *)fieldPtr == (gX_I32)f->defaultValue);
                        break;
                    case gX_DATATYPE_U32:
                        matchesDefault = (*(gX_U32 *)fieldPtr == (gX_U32)f->defaultValue);
                        break;
                    case gX_DATATYPE_F64:
                        matchesDefault = (*(gX_F64 *)fieldPtr == f->defaultValue);
                        break;
                    case gX_DATATYPE_I64:
                        matchesDefault = (*(gX_I64 *)fieldPtr == (gX_I64)f->defaultValue);
                        break;
                    case gX_DATATYPE_U64:
                        matchesDefault = (*(gX_U64 *)fieldPtr == (gX_U64)f->defaultValue);
                        break;
                    case gX_DATATYPE_I8:
                        matchesDefault = (*(gX_I8 *)fieldPtr == (gX_I8)f->defaultValue);
                        break;
                    case gX_DATATYPE_I16:
                        matchesDefault = (*(gX_I16 *)fieldPtr == (gX_I16)f->defaultValue);
                        break;
                    case gX_DATATYPE_U8:
                        matchesDefault = (*(gX_U8 *)fieldPtr == (gX_U8)f->defaultValue);
                        break;
                    case gX_DATATYPE_U16:
                        matchesDefault = (*(gX_U16 *)fieldPtr == (gX_U16)f->defaultValue);
                        break;
                    case gX_DATATYPE_F32V2:
                    {
                        gX_F32v2 *v = (gX_F32v2 *)fieldPtr;
                        gX_F32 def = (gX_F32)f->defaultValue;
                        matchesDefault = (v->x == def && v->y == def);
                        break;
                    }
                    case gX_DATATYPE_F32V3:
                    {
                        gX_F32v3 *v = (gX_F32v3 *)fieldPtr;
                        gX_F32 def = (gX_F32)f->defaultValue;
                        matchesDefault = (v->x == def && v->y == def && v->z == def);
                        break;
                    }
                    case gX_DATATYPE_F32V4:
                    {
                        gX_F32v4 *v = (gX_F32v4 *)fieldPtr;
                        gX_F32 def = (gX_F32)f->defaultValue;
                        matchesDefault = (v->x == def && v->y == def && v->z == def && v->w == def);
                        break;
                    }
                    case gX_DATATYPE_F64V2:
                    {
                        gX_F64v2 *v = (gX_F64v2 *)fieldPtr;
                        matchesDefault = (v->x == f->defaultValue && v->y == f->defaultValue);
                        break;
                    }
                    case gX_DATATYPE_F64V3:
                    {
                        gX_F64v3 *v = (gX_F64v3 *)fieldPtr;
                        matchesDefault = (v->x == f->defaultValue && v->y == f->defaultValue && v->z == f->defaultValue);
                        break;
                    }
                    case gX_DATATYPE_F64V4:
                    {
                        gX_F64v4 *v = (gX_F64v4 *)fieldPtr;
                        matchesDefault = (v->x == f->defaultValue && v->y == f->defaultValue && v->z == f->defaultValue && v->w == f->defaultValue);
                        break;
                    }
                    default:
                        break;
                }
                break;
            case GX_SCHEMA_ENUM:
            case GX_SCHEMA_OBJECT:
            case GX_SCHEMA_HEAP:
            case GX_SCHEMA_ARRAY:
            default:
                break;
        }
        if (matchesDefault)
        {
            return 0;
        }
    }

    struct json_object *jVal = NULL;

    switch (f->fieldType)
    {
        case GX_SCHEMA_BOOL:
        {
            jVal = json_object_new_boolean(*(gX_I1 *)fieldPtr ? 1 : 0);
            break;
        }

        case GX_SCHEMA_SCALAR:
        {
            switch (f->datatype)
            {
                case gX_DATATYPE_F32:
                {
                    //opencode: round to 6 decimal places to suppress FP noise
                    gX_F32 val = *(gX_F32 *)fieldPtr;
                    val = roundf(val * GX_SCHEMA_ROUND_PRECISIONF) / GX_SCHEMA_ROUND_PRECISIONF;
                    jVal = json_object_new_double(val);
                    break;
                }

                case gX_DATATYPE_I1:
                    jVal = json_object_new_boolean(*(gX_I1 *)fieldPtr ? 1 : 0);
                    break;

                case gX_DATATYPE_I32:
                    jVal = json_object_new_int(*(gX_I32 *)fieldPtr);
                    break;

                case gX_DATATYPE_U32:
                    jVal = json_object_new_int64(*(gX_U32 *)fieldPtr);
                    break;

                case gX_DATATYPE_F64:
                {
                    //opencode: round to 6 decimal places to suppress FP noise
                    gX_F64 val = *(gX_F64 *)fieldPtr;
                    val = round(val * GX_SCHEMA_ROUND_PRECISION) / GX_SCHEMA_ROUND_PRECISION;
                    jVal = json_object_new_double(val);
                    break;
                }

                case gX_DATATYPE_I64:
                    jVal = json_object_new_int64(*(gX_I64 *)fieldPtr);
                    break;

                case gX_DATATYPE_U64:
                    jVal = json_object_new_int64((gX_I64)*(gX_U64 *)fieldPtr);
                    break;

                case gX_DATATYPE_I8:
                    jVal = json_object_new_int(*(gX_I8 *)fieldPtr);
                    break;

                case gX_DATATYPE_I16:
                    jVal = json_object_new_int(*(gX_I16 *)fieldPtr);
                    break;

                case gX_DATATYPE_U8:
                    jVal = json_object_new_int(*(gX_U8 *)fieldPtr);
                    break;

                case gX_DATATYPE_U16:
                    jVal = json_object_new_int(*(gX_U16 *)fieldPtr);
                    break;

                case gX_DATATYPE_STRING:
                    jVal = json_object_new_string((gX_I8 *)fieldPtr);
                    break;

                case gX_DATATYPE_F32V3:
                {
                    gX_F32v3 *v = (gX_F32v3 *)fieldPtr;
                    jVal = json_object_new_array();
                    //opencode: round components to 6 decimal places
                    json_object_array_add(jVal, json_object_new_double(roundf(v->x * GX_SCHEMA_ROUND_PRECISIONF) / GX_SCHEMA_ROUND_PRECISIONF));
                    json_object_array_add(jVal, json_object_new_double(roundf(v->y * GX_SCHEMA_ROUND_PRECISIONF) / GX_SCHEMA_ROUND_PRECISIONF));
                    json_object_array_add(jVal, json_object_new_double(roundf(v->z * GX_SCHEMA_ROUND_PRECISIONF) / GX_SCHEMA_ROUND_PRECISIONF));
                    break;
                }

                case gX_DATATYPE_F32V2:
                {
                    gX_F32v2 *v = (gX_F32v2 *)fieldPtr;
                    jVal = json_object_new_array();
                    json_object_array_add(jVal, json_object_new_double(roundf(v->x * GX_SCHEMA_ROUND_PRECISIONF) / GX_SCHEMA_ROUND_PRECISIONF));
                    json_object_array_add(jVal, json_object_new_double(roundf(v->y * GX_SCHEMA_ROUND_PRECISIONF) / GX_SCHEMA_ROUND_PRECISIONF));
                    break;
                }

                case gX_DATATYPE_F32V4:
                {
                    gX_F32v4 *v = (gX_F32v4 *)fieldPtr;
                    jVal = json_object_new_array();
                    json_object_array_add(jVal, json_object_new_double(roundf(v->x * GX_SCHEMA_ROUND_PRECISIONF) / GX_SCHEMA_ROUND_PRECISIONF));
                    json_object_array_add(jVal, json_object_new_double(roundf(v->y * GX_SCHEMA_ROUND_PRECISIONF) / GX_SCHEMA_ROUND_PRECISIONF));
                    json_object_array_add(jVal, json_object_new_double(roundf(v->z * GX_SCHEMA_ROUND_PRECISIONF) / GX_SCHEMA_ROUND_PRECISIONF));
                    json_object_array_add(jVal, json_object_new_double(roundf(v->w * GX_SCHEMA_ROUND_PRECISIONF) / GX_SCHEMA_ROUND_PRECISIONF));
                    break;
                }

                case gX_DATATYPE_F64V2:
                {
                    gX_F64v2 *v = (gX_F64v2 *)fieldPtr;
                    jVal = json_object_new_array();
                    json_object_array_add(jVal, json_object_new_double(round(v->x * GX_SCHEMA_ROUND_PRECISION) / GX_SCHEMA_ROUND_PRECISION));
                    json_object_array_add(jVal, json_object_new_double(round(v->y * GX_SCHEMA_ROUND_PRECISION) / GX_SCHEMA_ROUND_PRECISION));
                    break;
                }

                case gX_DATATYPE_F64V3:
                {
                    gX_F64v3 *v = (gX_F64v3 *)fieldPtr;
                    jVal = json_object_new_array();
                    json_object_array_add(jVal, json_object_new_double(round(v->x * GX_SCHEMA_ROUND_PRECISION) / GX_SCHEMA_ROUND_PRECISION));
                    json_object_array_add(jVal, json_object_new_double(round(v->y * GX_SCHEMA_ROUND_PRECISION) / GX_SCHEMA_ROUND_PRECISION));
                    json_object_array_add(jVal, json_object_new_double(round(v->z * GX_SCHEMA_ROUND_PRECISION) / GX_SCHEMA_ROUND_PRECISION));
                    break;
                }

                case gX_DATATYPE_F64V4:
                {
                    gX_F64v4 *v = (gX_F64v4 *)fieldPtr;
                    jVal = json_object_new_array();
                    json_object_array_add(jVal, json_object_new_double(round(v->x * GX_SCHEMA_ROUND_PRECISION) / GX_SCHEMA_ROUND_PRECISION));
                    json_object_array_add(jVal, json_object_new_double(round(v->y * GX_SCHEMA_ROUND_PRECISION) / GX_SCHEMA_ROUND_PRECISION));
                    json_object_array_add(jVal, json_object_new_double(round(v->z * GX_SCHEMA_ROUND_PRECISION) / GX_SCHEMA_ROUND_PRECISION));
                    json_object_array_add(jVal, json_object_new_double(round(v->w * GX_SCHEMA_ROUND_PRECISION) / GX_SCHEMA_ROUND_PRECISION));
                    break;
                }

                case gX_DATATYPE_COLOR:
                {
                    gX_Color *c = (gX_Color *)fieldPtr;
                    jVal = json_object_new_array();
                    json_object_array_add(jVal, json_object_new_int(c->r));
                    json_object_array_add(jVal, json_object_new_int(c->g));
                    json_object_array_add(jVal, json_object_new_int(c->b));
                    json_object_array_add(jVal, json_object_new_int(c->a));
                    break;
                }

                case gX_DATATYPE_RECTANGLE:
                {
                    gX_Rectangle *r = (gX_Rectangle *)fieldPtr;
                    jVal = json_object_new_array();
                    json_object_array_add(jVal, json_object_new_double(roundf(r->x * GX_SCHEMA_ROUND_PRECISIONF) / GX_SCHEMA_ROUND_PRECISIONF));
                    json_object_array_add(jVal, json_object_new_double(roundf(r->y * GX_SCHEMA_ROUND_PRECISIONF) / GX_SCHEMA_ROUND_PRECISIONF));
                    json_object_array_add(jVal, json_object_new_double(roundf(r->width * GX_SCHEMA_ROUND_PRECISIONF) / GX_SCHEMA_ROUND_PRECISIONF));
                    json_object_array_add(jVal, json_object_new_double(roundf(r->height * GX_SCHEMA_ROUND_PRECISIONF) / GX_SCHEMA_ROUND_PRECISIONF));
                    break;
                }

                default:
                    gX_TraceLog(gX_LOG_WARNING,
                        "schema_json_write: unhandled scalar datatype %d for key '%s'",
                        f->datatype, f->key);
                    return 1;
            }
            break;
        }

        case GX_SCHEMA_ENUM:
        {
            gX_I32 val = *(gX_I32 *)fieldPtr;
            if (f->enumToString)
            {
                gX_I8 *str = f->enumToString(val);
                jVal = json_object_new_string(str ? str : "unknown");
            }
            else
            {
                jVal = json_object_new_int(val);
            }
            break;
        }

        case GX_SCHEMA_OBJECT:
        {
            jVal = json_object_new_object();
            gX_Schema_JSON_SaveToObj(f->children, f->childCount, fieldPtr, jVal);
            break;
        }

        case GX_SCHEMA_HEAP:
        {
            gX_U32 count = f->heapCount ? f->heapCount(base) : 0;
            jVal = json_object_new_array();
            for (gX_U32 i = 0; i < count; i++)
            {
                gX_I0 *elem = f->heapAt ? f->heapAt(base, i) : NULL;
                if (!elem)
                {
                    continue;
                }
                struct json_object *jElem = json_object_new_object();
                gX_Schema_JSON_SaveToObj(f->heapChildren, f->heapChildCount,
                                          elem, jElem);
                json_object_array_add(jVal, jElem);
            }
            break;
        }

        case GX_SCHEMA_ARRAY:
        {
            //opencode: STRING arrays — gX_Datatype_Size(STRING) is 0, so fall
            // back to the declared element capacity (or gX_TextLine=128).
            gX_U32 stride;
            if (f->arrayStride > 0) {
                stride = f->arrayStride;
            } else if (f->datatype == gX_DATATYPE_STRING) {
                stride = f->stringCap > 0 ? f->stringCap : 128;
            } else {
                stride = gX_Datatype_Size(f->datatype);
            }
            jVal = json_object_new_array();

            if (f->arrayChildren)
            {
                // Object array — serialize each element as a JSON object
                for (gX_U32 i = 0; i < f->arrayCount; i++)
                {
                    gX_I0 *elemPtr = (gX_I0 *)((gX_U8 *)fieldPtr + i * stride);
                    struct json_object *jElem = json_object_new_object();
                    gX_Schema_JSON_SaveToObj(f->arrayChildren,
                                              f->arrayChildCount,
                                              elemPtr, jElem);
                    json_object_array_add(jVal, jElem);
                }
            }
            else
            {
                // Scalar array — serialize each element as a JSON value
                for (gX_U32 i = 0; i < f->arrayCount; i++)
                {
                    gX_I0 *elemPtr = (gX_I0 *)((gX_U8 *)fieldPtr + i * stride);
                    struct json_object *jElem = NULL;

                    switch (f->datatype)
                    {
                        case gX_DATATYPE_F32:
                        {
                            gX_F32 val = *(gX_F32 *)elemPtr;
                            val = roundf(val * GX_SCHEMA_ROUND_PRECISIONF) / GX_SCHEMA_ROUND_PRECISIONF;
                            jElem = json_object_new_double(val);
                            break;
                        }
                        case gX_DATATYPE_I1:
                            jElem = json_object_new_boolean(
                                *(gX_I1 *)elemPtr ? 1 : 0);
                            break;
                        case gX_DATATYPE_I32:
                            jElem = json_object_new_int(*(gX_I32 *)elemPtr);
                            break;
                        case gX_DATATYPE_U32:
                            jElem = json_object_new_int64(*(gX_U32 *)elemPtr);
                            break;
                        case gX_DATATYPE_F64:
                        {
                            gX_F64 val = *(gX_F64 *)elemPtr;
                            val = round(val * GX_SCHEMA_ROUND_PRECISION) / GX_SCHEMA_ROUND_PRECISION;
                            jElem = json_object_new_double(val);
                            break;
                        }
                        case gX_DATATYPE_I64:
                            jElem = json_object_new_int64(
                                *(gX_I64 *)elemPtr);
                            break;
                        case gX_DATATYPE_U64:
                            jElem = json_object_new_int64(
                                (gX_I64)*(gX_U64 *)elemPtr);
                            break;
                        case gX_DATATYPE_I8:
                            jElem = json_object_new_int(
                                *(gX_I8 *)elemPtr);
                            break;
                        case gX_DATATYPE_I16:
                            jElem = json_object_new_int(
                                *(gX_I16 *)elemPtr);
                            break;
                        case gX_DATATYPE_U8:
                            jElem = json_object_new_int(
                                *(gX_U8 *)elemPtr);
                            break;
                        case gX_DATATYPE_U16:
                            jElem = json_object_new_int(
                                *(gX_U16 *)elemPtr);
                            break;
                        case gX_DATATYPE_STRING:
                            jElem = json_object_new_string((gX_I8 *)elemPtr);
                            break;
                        default:
                            gX_TraceLog(gX_LOG_WARNING,
                                "schema_json_write: unsupported datatype %d "
                                "in ARRAY '%s'", f->datatype, f->key);
                            break;
                    }

                    if (jElem)
                    {
                        json_object_array_add(jVal, jElem);
                    }
                }
            }
            break;
        }

        default:
            break;
    }

    if (jVal)
    {
        json_object_object_add(parent, f->key, jVal);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Public: save schema-driven struct to a json_object
// ---------------------------------------------------------------------------
gX_I32 gX_Schema_JSON_SaveToObj(gX_SchemaField *fields, gX_U32 fieldCount,
                                 gX_I0 *base, struct json_object *parent)
{
    for (gX_U32 i = 0; i < fieldCount; i++)
    {
        schema_json_write_field(&fields[i], base, parent);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Public: save schema-driven struct to a JSON file
// ---------------------------------------------------------------------------
gX_I32 gX_Schema_JSON_Save(gX_SchemaField *fields, gX_U32 fieldCount,
                            gX_I0 *base, const gX_I8 *path)
{
    struct json_object *root = json_object_new_object();
    gX_Schema_JSON_SaveToObj(fields, fieldCount, base, root);

    gX_I32 flags = 0;
    flags |= JSON_C_TO_STRING_SPACED;
    flags |= JSON_C_TO_STRING_PRETTY;
    flags |= JSON_C_TO_STRING_PRETTY_TAB;

    gX_I32 ret = json_object_to_file_ext(path, root, flags);
    json_object_put(root);

    if (ret != 0)
    {
        gX_TraceLog(gX_LOG_ERROR, "schema_json: failed to write '%s'", path);
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Read (load) a single field from a json_object
// ---------------------------------------------------------------------------
//opencode: missing values preserve initialized state and emit descriptor-level diagnostics.
static gX_I32 schema_json_read_field(gX_SchemaField *f, gX_I0 *base,
                                      struct json_object *parent,
                                      const gX_I8 *parentPath)
{
    struct json_object *jVal = NULL;
    gX_I8 fieldPath[512];
    schema_json_make_path(fieldPath, sizeof(fieldPath), parentPath, f->key);
    if (!parent || json_object_get_type(parent) != json_type_object ||
        !json_object_object_get_ex(parent, f->key, &jVal))
    {
        if (f->missingSeverity != GX_SCHEMA_MISSING_IGNORE)
        {
            gX_I32 level = f->missingSeverity == GX_SCHEMA_MISSING_ERROR
                ? gX_LOG_ERROR : gX_LOG_WARNING;
            const gX_I8 *required = f->missingSeverity == GX_SCHEMA_MISSING_ERROR
                ? " required" : "";
            gX_TraceLog(level,
                "schema_json: missing%s field '%s'; retaining initialized/default value",
                required, fieldPath);
        }
        return 1;
    }

    gX_I0 *fieldPtr = (gX_I0 *)((gX_U8 *)base + f->offset);
    enum json_type jt = json_object_get_type(jVal);

    switch (f->fieldType)
    {
        case GX_SCHEMA_BOOL:
        {
            if (jt == json_type_boolean)
            {
                *(gX_I1 *)fieldPtr = json_object_get_boolean(jVal) ? 1 : 0;
            }
            break;
        }

        case GX_SCHEMA_SCALAR:
        {
            switch (f->datatype)
            {
                case gX_DATATYPE_F32:
                    if (jt == json_type_double || jt == json_type_int)
                        *(gX_F32 *)fieldPtr = (gX_F32)json_object_get_double(jVal);
                    break;

                case gX_DATATYPE_I1:
                    if (jt == json_type_boolean)
                        *(gX_I1 *)fieldPtr = json_object_get_boolean(jVal) ? 1 : 0;
                    else if (jt == json_type_int)
                        *(gX_I1 *)fieldPtr = json_object_get_int(jVal) ? 1 : 0;
                    break;

                case gX_DATATYPE_I32:
                    if (jt == json_type_int)
                        *(gX_I32 *)fieldPtr = json_object_get_int(jVal);
                    break;

                case gX_DATATYPE_U32:
                    if (jt == json_type_int)
                        *(gX_U32 *)fieldPtr = (gX_U32)json_object_get_int64(jVal);
                    break;

                case gX_DATATYPE_F64:
                    if (jt == json_type_double || jt == json_type_int)
                        *(gX_F64 *)fieldPtr = json_object_get_double(jVal);
                    break;

                case gX_DATATYPE_I64:
                    if (jt == json_type_int)
                        *(gX_I64 *)fieldPtr = json_object_get_int64(jVal);
                    break;

                case gX_DATATYPE_U64:
                    if (jt == json_type_int)
                        *(gX_U64 *)fieldPtr = (gX_U64)json_object_get_int64(jVal);
                    break;

                case gX_DATATYPE_I8:
                    if (jt == json_type_int)
                        *(gX_I8 *)fieldPtr = (gX_I8)json_object_get_int(jVal);
                    break;

                case gX_DATATYPE_I16:
                    if (jt == json_type_int)
                        *(gX_I16 *)fieldPtr = (gX_I16)json_object_get_int(jVal);
                    break;

                case gX_DATATYPE_U8:
                    if (jt == json_type_int)
                        *(gX_U8 *)fieldPtr = (gX_U8)json_object_get_int(jVal);
                    break;

                case gX_DATATYPE_U16:
                    if (jt == json_type_int)
                        *(gX_U16 *)fieldPtr = (gX_U16)json_object_get_int(jVal);
                    break;

                case gX_DATATYPE_STRING:
                {
                    const gX_I8 *s = json_object_get_string(jVal);
                    if (s)
                    {
                        //opencode: cap at the declared buffer size (stringCap),
                        // fall back to gX_TextLine (128) for legacy schemas.
                        gX_U32 cap = f->stringCap > 0 ? f->stringCap : 128;
                        if (cap > 1) cap--;
                        strncpy((gX_I8 *)fieldPtr, s, cap);
                        ((gX_I8 *)fieldPtr)[cap] = '\0';
                    }
                    break;
                }

                case gX_DATATYPE_F32V3:
                {
                    //opencode: accept array of 3 (current format), 2-element, 1-element,
                    //   or a bare scalar (backward-compat with old single-float saves).
                    //   A scalar is broadcast to all 3 components (uniform scale).
                    gX_F32v3 *v = (gX_F32v3 *)fieldPtr;
                    if (jt == json_type_array)
                    {
                        gX_U32 len = (gX_U32)json_object_array_length(jVal);
                        if (len >= 1) v->x = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 0));
                        if (len >= 2) v->y = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 1));
                        if (len >= 3) v->z = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 2));
                        else if (len == 2) v->z = v->y; // 2-elem vec → broadcast y to z
                        else if (len == 1) { v->y = v->x; v->z = v->x; } // 1-elem → broadcast x to all
                    }
                    else if (jt == json_type_double || jt == json_type_int)
                    {
                        //opencode: legacy format — single float applied to all 3 components
                        gX_F32 val = (gX_F32)json_object_get_double(jVal);
                        v->x = v->y = v->z = val;
                    }
                    break;
                }

                case gX_DATATYPE_F32V2:
                {
                    //opencode: accept 2-elem array (current), 1-elem array, or bare scalar
                    gX_F32v2 *v = (gX_F32v2 *)fieldPtr;
                    if (jt == json_type_array)
                    {
                        gX_U32 len = (gX_U32)json_object_array_length(jVal);
                        if (len >= 1) v->x = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 0));
                        if (len >= 2) v->y = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 1));
                        else if (len == 1) v->y = v->x;
                    }
                    else if (jt == json_type_double || jt == json_type_int)
                    {
                        gX_F32 val = (gX_F32)json_object_get_double(jVal);
                        v->x = v->y = val;
                    }
                    break;
                }

                case gX_DATATYPE_F32V4:
                {
                    if (jt == json_type_array &&
                        json_object_array_length(jVal) >= 4)
                    {
                        gX_F32v4 *v = (gX_F32v4 *)fieldPtr;
                        v->x = (gX_F32)json_object_get_double(
                            json_object_array_get_idx(jVal, 0));
                        v->y = (gX_F32)json_object_get_double(
                            json_object_array_get_idx(jVal, 1));
                        v->z = (gX_F32)json_object_get_double(
                            json_object_array_get_idx(jVal, 2));
                        v->w = (gX_F32)json_object_get_double(
                            json_object_array_get_idx(jVal, 3));
                    }
                    break;
                }

                case gX_DATATYPE_F64V2:
                {
                    if (jt == json_type_array &&
                        json_object_array_length(jVal) >= 2)
                    {
                        gX_F64v2 *v = (gX_F64v2 *)fieldPtr;
                        v->x = json_object_get_double(
                            json_object_array_get_idx(jVal, 0));
                        v->y = json_object_get_double(
                            json_object_array_get_idx(jVal, 1));
                    }
                    break;
                }

                case gX_DATATYPE_F64V3:
                {
                    if (jt == json_type_array &&
                        json_object_array_length(jVal) >= 3)
                    {
                        gX_F64v3 *v = (gX_F64v3 *)fieldPtr;
                        v->x = json_object_get_double(
                            json_object_array_get_idx(jVal, 0));
                        v->y = json_object_get_double(
                            json_object_array_get_idx(jVal, 1));
                        v->z = json_object_get_double(
                            json_object_array_get_idx(jVal, 2));
                    }
                    break;
                }

                case gX_DATATYPE_F64V4:
                {
                    if (jt == json_type_array &&
                        json_object_array_length(jVal) >= 4)
                    {
                        gX_F64v4 *v = (gX_F64v4 *)fieldPtr;
                        v->x = json_object_get_double(
                            json_object_array_get_idx(jVal, 0));
                        v->y = json_object_get_double(
                            json_object_array_get_idx(jVal, 1));
                        v->z = json_object_get_double(
                            json_object_array_get_idx(jVal, 2));
                        v->w = json_object_get_double(
                            json_object_array_get_idx(jVal, 3));
                    }
                    break;
                }

                case gX_DATATYPE_COLOR:
                {
                    if (jt == json_type_array &&
                        json_object_array_length(jVal) >= 4)
                    {
                        gX_Color *c = (gX_Color *)fieldPtr;
                        c->r = (gX_U8)json_object_get_int(
                            json_object_array_get_idx(jVal, 0));
                        c->g = (gX_U8)json_object_get_int(
                            json_object_array_get_idx(jVal, 1));
                        c->b = (gX_U8)json_object_get_int(
                            json_object_array_get_idx(jVal, 2));
                        c->a = (gX_U8)json_object_get_int(
                            json_object_array_get_idx(jVal, 3));
                    }
                    break;
                }

                case gX_DATATYPE_RECTANGLE:
                {
                    if (jt == json_type_array &&
                        json_object_array_length(jVal) >= 4)
                    {
                        gX_Rectangle *r = (gX_Rectangle *)fieldPtr;
                        r->x = (gX_F32)json_object_get_double(
                            json_object_array_get_idx(jVal, 0));
                        r->y = (gX_F32)json_object_get_double(
                            json_object_array_get_idx(jVal, 1));
                        r->width = (gX_F32)json_object_get_double(
                            json_object_array_get_idx(jVal, 2));
                        r->height = (gX_F32)json_object_get_double(
                            json_object_array_get_idx(jVal, 3));
                    }
                    break;
                }

                default:
                    gX_TraceLog(gX_LOG_WARNING,
                        "schema_json_load: unhandled datatype %d for '%s'",
                        f->datatype, f->key);
                    break;
            }
            break;
        }

        case GX_SCHEMA_ENUM:
        {
            if (jt == json_type_string && f->stringToEnum)
            {
                *(gX_I32 *)fieldPtr =
                    f->stringToEnum(json_object_get_string(jVal));
            }
            else if (jt == json_type_int)
            {
                *(gX_I32 *)fieldPtr = json_object_get_int(jVal);
            }
            break;
        }

        case GX_SCHEMA_OBJECT:
        {
            if (jt == json_type_object)
            {
                schema_json_load_from_obj_path(f->children, f->childCount,
                    fieldPtr, jVal, fieldPath);
            }
            break;
        }

        case GX_SCHEMA_HEAP:
        {
            if (jt == json_type_array && f->heapAdd && f->heapAt &&
                f->heapChildren)
            {
                gX_U32 count = (gX_U32)json_object_array_length(jVal);
                if (f->heapCount)
                {
                    // Caller is responsible for clearing the heap before load.
                    // We append one element per array entry.
                    for (gX_U32 i = 0; i < count; i++)
                    {
                        struct json_object *jElem =
                            json_object_array_get_idx(jVal, i);
                        if (!jElem)
                        {
                            continue;
                        }
                        gX_I32 idx = f->heapAdd(base);
                        if (idx < 0)
                        {
                            break;
                        }
                        gX_I0 *elem = f->heapAt(base, (gX_U32)idx);
                        if (elem && json_object_get_type(jElem) == json_type_object)
                        {
                            gX_I8 elementPath[512];
                            schema_json_make_index_path(elementPath,
                                sizeof(elementPath), fieldPath, i);
                            schema_json_load_from_obj_path(f->heapChildren,
                                f->heapChildCount, elem, jElem, elementPath);
                        }
                    }
                }
            }
            break;
        }

        case GX_SCHEMA_ARRAY:
        {
            if (jt == json_type_array)
            {
                //opencode: STRING arrays — gX_Datatype_Size(STRING) is 0, so
                // fall back to the declared element capacity (or 128).
                gX_U32 stride;
                if (f->arrayStride > 0) {
                    stride = f->arrayStride;
                } else if (f->datatype == gX_DATATYPE_STRING) {
                    stride = f->stringCap > 0 ? f->stringCap : 128;
                } else {
                    stride = gX_Datatype_Size(f->datatype);
                }
                gX_U32 count = (gX_U32)json_object_array_length(jVal);
                if (count > f->arrayCount) count = f->arrayCount;

                if (f->arrayChildren)
                {
                    // Object array — recurse with child fields into each element
                    for (gX_U32 i = 0; i < count; i++)
                    {
                        struct json_object *jElem =
                            json_object_array_get_idx(jVal, i);
                        if (jElem &&
                            json_object_get_type(jElem) == json_type_object)
                        {
                            gX_I0 *elemPtr = (gX_I0 *)(
                                (gX_U8 *)fieldPtr + i * stride);
                            gX_I8 elementPath[512];
                            schema_json_make_index_path(elementPath,
                                sizeof(elementPath), fieldPath, i);
                            schema_json_load_from_obj_path(f->arrayChildren,
                                f->arrayChildCount, elemPtr, jElem,
                                elementPath);
                        }
                    }
                }
                else
                {
                    // Scalar array — read each element by datatype
                    for (gX_U32 i = 0; i < count; i++)
                    {
                        struct json_object *jElem =
                            json_object_array_get_idx(jVal, i);
                        if (!jElem) continue;
                        gX_I0 *elemPtr = (gX_I0 *)(
                            (gX_U8 *)fieldPtr + i * stride);

                        switch (f->datatype)
                        {
                            case gX_DATATYPE_F32:
                                *(gX_F32 *)elemPtr = (gX_F32)
                                    json_object_get_double(jElem);
                                break;
                            case gX_DATATYPE_I1:
                                *(gX_I1 *)elemPtr = (gX_I1)
                                    json_object_get_boolean(jElem);
                                break;
                            case gX_DATATYPE_I32:
                                *(gX_I32 *)elemPtr = (gX_I32)
                                    json_object_get_int(jElem);
                                break;
                            case gX_DATATYPE_U32:
                                *(gX_U32 *)elemPtr = (gX_U32)
                                    json_object_get_int64(jElem);
                                break;
                            case gX_DATATYPE_F64:
                                *(gX_F64 *)elemPtr =
                                    json_object_get_double(jElem);
                                break;
                            case gX_DATATYPE_I64:
                                *(gX_I64 *)elemPtr =
                                    json_object_get_int64(jElem);
                                break;
                            case gX_DATATYPE_U64:
                                *(gX_U64 *)elemPtr = (gX_U64)
                                    json_object_get_int64(jElem);
                                break;
                            case gX_DATATYPE_I8:
                                *(gX_I8 *)elemPtr = (gX_I8)
                                    json_object_get_int(jElem);
                                break;
                            case gX_DATATYPE_I16:
                                *(gX_I16 *)elemPtr = (gX_I16)
                                    json_object_get_int(jElem);
                                break;
                            case gX_DATATYPE_U8:
                                *(gX_U8 *)elemPtr = (gX_U8)
                                    json_object_get_int(jElem);
                                break;
                            case gX_DATATYPE_U16:
                                *(gX_U16 *)elemPtr = (gX_U16)
                                    json_object_get_int(jElem);
                                break;
                            case gX_DATATYPE_STRING:
                            {
                                const gX_I8 *s =
                                    json_object_get_string(jElem);
                                if (s)
                                {
                                    gX_U32 cap = f->stringCap > 0
                                                 ? f->stringCap : 128;
                                    if (cap > 1) cap--;
                                    strncpy((gX_I8 *)elemPtr, s, cap);
                                    ((gX_I8 *)elemPtr)[cap] = '\0';
                                }
                                break;
                            }
                            default:
                                break;
                        }
                    }
                }
            }
            break;
        }

        default:
            break;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Public: load schema-driven struct from a json_object
// ---------------------------------------------------------------------------
//opencode: keep the legacy entry point while adding path-aware diagnostics.
gX_I32 gX_Schema_JSON_LoadFromObj(gX_SchemaField *fields, gX_U32 fieldCount,
                                   gX_I0 *base, struct json_object *parent)
{
    return gX_Schema_JSON_LoadFromObjPath(fields, fieldCount, base, parent, "");
}

//opencode: allow callers with external/nested structs to name diagnostic paths.
gX_I32 gX_Schema_JSON_LoadFromObjPath(gX_SchemaField *fields,
                                       gX_U32 fieldCount, gX_I0 *base,
                                       struct json_object *parent,
                                       const gX_I8 *parentPath)
{
    return schema_json_load_from_obj_path(fields, fieldCount, base, parent,
        parentPath ? parentPath : "");
}

//opencode: partial objects are accepted; absent fields retain initialized
//   values and are reported according to each descriptor's missing severity.
static gX_I32 schema_json_load_from_obj_path(gX_SchemaField *fields,
                                             gX_U32 fieldCount,
                                             gX_I0 *base,
                                             struct json_object *parent,
                                             const gX_I8 *parentPath)
{
    if (parent && json_object_get_type(parent) != json_type_object)
    {
        gX_TraceLog(gX_LOG_ERROR,
            "schema_json: expected object at '%s'; object skipped",
            parentPath && parentPath[0] ? parentPath : "<root>");
        return 1;
    }
    for (gX_U32 i = 0; i < fieldCount; i++)
    {
        schema_json_read_field(&fields[i], base, parent, parentPath);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Public: load schema-driven struct from a JSON file
// ---------------------------------------------------------------------------
gX_I32 gX_Schema_JSON_Load(gX_SchemaField *fields, gX_U32 fieldCount,
                            gX_I0 *base, const gX_I8 *path)
{
    struct json_object *root = json_object_from_file(path);
    if (!root)
    {
        gX_TraceLog(gX_LOG_ERROR, "schema_json: failed to load '%s'", path);
        return 1;
    }

    if (json_object_get_type(root) != json_type_object)
    {
        gX_TraceLog(gX_LOG_ERROR,
            "schema_json: root in '%s' is not an object", path);
        json_object_put(root);
        return 1;
    }

    schema_json_load_from_obj_path(fields, fieldCount, base, root, "");
    json_object_put(root);
    return 0;
}
