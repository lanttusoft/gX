/**********************************************************************************************
*
*   [gX: src/common/schema.h]
*   Static offset-based field descriptor tree for serialization.
*   Replaces flat dataPtr arrays with compile-time offsetof descriptors.
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

#ifndef SCHEMA_H
#define SCHEMA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>  // offsetof

#include "types.h"
#include "enum.h"    // gX_DATATYPE_*

//opencode: rounding precision for float serialization — suppress FP noise
#define GX_SCHEMA_ROUND_PRECISION  1000000.0
#define GX_SCHEMA_ROUND_PRECISIONF 1000000.0f

// ---------------------------------------------------------------------------
// Field type enum — describes the structure/kind of a schema field
// ---------------------------------------------------------------------------
typedef enum {
    GX_SCHEMA_SCALAR,    // Leaf: int, float, vec3, color, string, etc.
    GX_SCHEMA_BOOL,      // Leaf: I1, serialized as JSON boolean
    GX_SCHEMA_ENUM,      // Leaf: I32 with string mapping callbacks
    GX_SCHEMA_OBJECT,    // Nested struct with child fields
    GX_SCHEMA_HEAP,      // gX_Heap-backed dynamic array
    GX_SCHEMA_ARRAY,     // Fixed-size inline C array of scalars or objects
} gX_SchemaFieldType;

//opencode: missing JSON values warn by default; descriptors may elevate
//   contract-critical fields to errors or suppress known sparse data.
typedef enum {
    GX_SCHEMA_MISSING_WARNING = 0,
    GX_SCHEMA_MISSING_ERROR = 1,
    GX_SCHEMA_MISSING_IGNORE = 2,
} gX_SchemaMissingSeverity;

// ---------------------------------------------------------------------------
// Forward declaration
// ---------------------------------------------------------------------------
typedef struct gX_SchemaField gX_SchemaField;

// ---------------------------------------------------------------------------
// Schema field descriptor — captures everything needed to read/write one
// struct member without runtime dataPtr arrays.
//
// The callbacks (heapCount, heapAt, heapAdd, enumToString, stringToEnum)
// take a 'parent' pointer — the base address of the struct that *contains*
// the field (or for heap callbacks, the struct that owns the heap).
// ---------------------------------------------------------------------------
typedef struct gX_SchemaField {
    gX_I8              key[64];          // JSON/YAML key name
    gX_SchemaFieldType fieldType;        // SCALAR, BOOL, ENUM, OBJECT, HEAP
    gX_I32             datatype;         // gX_DATATYPE_* for scalars
    gX_U32             offset;           // offsetof(parent_struct, field)

    //opencode: capacity of the STRING target buffer (set by GX_SCHEMA_STRING
    // via sizeof). Loaders bound copies with it — fixes overflow on fields
    // smaller than gX_TextLine (e.g. cssClass[64], onclick[64]). 0 = unknown
    // (fall back to gX_TextLine-size copy). Also used as the element stride
    // for scalar STRING arrays (gX_Datatype_Size(STRING) returns 0).
    gX_U32             stringCap;

    //opencode: default-value skipping — if hasDefault is true and the field's
    // runtime value equals defaultValue, the field is omitted from JSON output.
    gX_F64             defaultValue;
    gX_I1              hasDefault;
    gX_SchemaMissingSeverity missingSeverity; //opencode: warning by default

    // --- For nested OBJECT ---
    gX_U32             childCount;
    gX_SchemaField    *children;

    // --- For dynamic HEAP arrays ---
    // Callbacks receive 'base' (the struct that owns the heap field)
    gX_U32  (*heapCount)(gX_I0 *base);
    gX_I0  *(*heapAt)(gX_I0 *base, gX_U32 index);
    gX_I32  (*heapAdd)(gX_I0 *base);
    gX_U32             heapChildCount;
    gX_SchemaField    *heapChildren;

    // --- For fixed-size ARRAY fields ---
    // Scalar arrays: datatype specifies the element type; stride = gX_Datatype_Size(datatype)
    // Object arrays: arrayChildren/arrayChildCount specify child fields; stride is datatype-sized
    gX_U32             arrayCount;        // Number of elements in the C array
    gX_U32             arrayChildCount;   // For object arrays: number of child field descriptors
    gX_SchemaField    *arrayChildren;     // For object arrays: child field descriptors
    gX_U32             arrayStride;       // Element stride in bytes (0 = auto from datatype)

    // --- For ENUM fields ---
    gX_I8  *(*enumToString)(gX_I32 value);
    gX_I32  (*stringToEnum)(const gX_I8 *str);
} gX_SchemaField;

// ---------------------------------------------------------------------------
// Registration macros — designated initializer wrappers
// ---------------------------------------------------------------------------

//opencode: renamed parameter 'key' to 'key_' to avoid preprocessor clash with
// the designated-initializer field name '.key' (the preprocessor would otherwise
// expand '.key' into '."string"' which is invalid C)
#define GX_SCHEMA_SCALAR(key_, structType, field, dtype) \
    { .key = key_, .fieldType = GX_SCHEMA_SCALAR, .datatype = dtype, \
      .offset = offsetof(structType, field) }

#define GX_SCHEMA_BOOL(key_, structType, field) \
    { .key = key_, .fieldType = GX_SCHEMA_BOOL, .datatype = gX_DATATYPE_I1, \
      .offset = offsetof(structType, field) }

#define GX_SCHEMA_ENUM(key_, structType, field, toStr, fromStr) \
    { .key = key_, .fieldType = GX_SCHEMA_ENUM, .datatype = gX_DATATYPE_I32, \
      .offset = offsetof(structType, field), \
      .enumToString = toStr, .stringToEnum = fromStr }

#define GX_SCHEMA_OBJECT(key_, structType, field, childArr, childCountVal) \
    { .key = key_, .fieldType = GX_SCHEMA_OBJECT, .offset = offsetof(structType, field), \
      .children = childArr, .childCount = childCountVal }

#define GX_SCHEMA_HEAP(key_, structType, field, cntFn, atFn, addFn, childArr, childCountVal) \
    { .key = key_, .fieldType = GX_SCHEMA_HEAP, .offset = offsetof(structType, field), \
      .heapCount = cntFn, .heapAt = atFn, .heapAdd = addFn, \
      .heapChildren = childArr, .heapChildCount = childCountVal }

#define GX_SCHEMA_ARRAY(key_, structType, field, dtype, count) \
    { .key = key_, .fieldType = GX_SCHEMA_ARRAY, .datatype = gX_DATATYPE_##dtype, \
      .offset = offsetof(structType, field), .arrayCount = count, \
      .arrayStride = 0 }

//opencode: STRING array — C array of fixed-size char buffers. Sets both the
// element stride and stringCap so all backends copy/count correctly
// (gX_Datatype_Size(STRING) returns 0).
#define GX_SCHEMA_ARRAY_STRING(key_, structType, field, count) \
    { .key = key_, .fieldType = GX_SCHEMA_ARRAY, .datatype = gX_DATATYPE_STRING, \
      .offset = offsetof(structType, field), .arrayCount = count, \
      .arrayStride = (gX_U32)sizeof(((structType*)0)->field[0]), \
      .stringCap = (gX_U32)sizeof(((structType*)0)->field[0]) }

//opencode: object ARRAY — fixed-size C array of nested structs. stride falls
// back to sizeof(element) when arrayStride is 0 (loaders handle this).
#define GX_SCHEMA_ARRAY_OBJECT(key_, structType, field, childArr, childCountVal, count) \
    { .key = key_, .fieldType = GX_SCHEMA_ARRAY, .datatype = 0, \
      .offset = offsetof(structType, field), .arrayCount = count, \
      .arrayStride = sizeof(((structType*)0)->field[0]), \
      .arrayChildren = childArr, .arrayChildCount = childCountVal }

// --- Convenience macros for common scalar types ---

#define GX_SCHEMA_V3(key, structType, field) \
    GX_SCHEMA_SCALAR(key, structType, field, gX_DATATYPE_F32V3)

#define GX_SCHEMA_V2(key, structType, field) \
    GX_SCHEMA_SCALAR(key, structType, field, gX_DATATYPE_F32V2)

#define GX_SCHEMA_V4(key, structType, field) \
    GX_SCHEMA_SCALAR(key, structType, field, gX_DATATYPE_F32V4)

#define GX_SCHEMA_COLOR(key, structType, field) \
    GX_SCHEMA_SCALAR(key, structType, field, gX_DATATYPE_COLOR)

#define GX_SCHEMA_STRING(key_, structType, field) \
    { .key = key_, .fieldType = GX_SCHEMA_SCALAR, .datatype = gX_DATATYPE_STRING, \
      .offset = offsetof(structType, field), \
      .stringCap = (gX_U32)sizeof(((structType*)0)->field) }

//opencode: required identifiers report incomplete entries while partial
//   documents continue loading with initialized field values.
#define GX_SCHEMA_REQUIRED_STRING(key_, structType, field) \
    { .key = key_, .fieldType = GX_SCHEMA_SCALAR, .datatype = gX_DATATYPE_STRING, \
      .offset = offsetof(structType, field), \
      .stringCap = (gX_U32)sizeof(((structType*)0)->field), \
      .missingSeverity = GX_SCHEMA_MISSING_ERROR }

//opencode: sparse material override properties are intentionally field-scoped.
#define GX_SCHEMA_SPARSE_STRING(key_, structType, field) \
    { .key = key_, .fieldType = GX_SCHEMA_SCALAR, .datatype = gX_DATATYPE_STRING, \
      .offset = offsetof(structType, field), \
      .stringCap = (gX_U32)sizeof(((structType*)0)->field), \
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE }

#define GX_SCHEMA_SPARSE_F32(key_, structType, field) \
    { .key = key_, .fieldType = GX_SCHEMA_SCALAR, .datatype = gX_DATATYPE_F32, \
      .offset = offsetof(structType, field), \
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE }

#define GX_SCHEMA_SPARSE_COLOR(key_, structType, field) \
    { .key = key_, .fieldType = GX_SCHEMA_SCALAR, .datatype = gX_DATATYPE_COLOR, \
      .offset = offsetof(structType, field), \
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE }

#define GX_SCHEMA_I32(key, structType, field) \
    GX_SCHEMA_SCALAR(key, structType, field, gX_DATATYPE_I32)

#define GX_SCHEMA_U32(key, structType, field) \
    GX_SCHEMA_SCALAR(key, structType, field, gX_DATATYPE_U32)

#define GX_SCHEMA_F32(key, structType, field) \
    GX_SCHEMA_SCALAR(key, structType, field, gX_DATATYPE_F32)

#define GX_SCHEMA_I1(key, structType, field) \
    GX_SCHEMA_SCALAR(key, structType, field, gX_DATATYPE_I1)

#define GX_SCHEMA_I64(key, structType, field) \
    GX_SCHEMA_SCALAR(key, structType, field, gX_DATATYPE_I64)

#define GX_SCHEMA_U64(key, structType, field) \
    GX_SCHEMA_SCALAR(key, structType, field, gX_DATATYPE_U64)

#define GX_SCHEMA_F64(key, structType, field) \
    GX_SCHEMA_SCALAR(key, structType, field, gX_DATATYPE_F64)

//opencode: default-value macros — field is skipped in JSON when its value == defaultValue
#define GX_SCHEMA_F32_DEF(key_, structType, field, def) \
    { .key = key_, .fieldType = GX_SCHEMA_SCALAR, .datatype = gX_DATATYPE_F32, \
      .offset = offsetof(structType, field), \
      .hasDefault = 1, .defaultValue = (gX_F64)(def) }

#define GX_SCHEMA_I32_DEF(key_, structType, field, def) \
    { .key = key_, .fieldType = GX_SCHEMA_SCALAR, .datatype = gX_DATATYPE_I32, \
      .offset = offsetof(structType, field), \
      .hasDefault = 1, .defaultValue = (gX_F64)(def) }

#define GX_SCHEMA_BOOL_DEF(key_, structType, field, def) \
    { .key = key_, .fieldType = GX_SCHEMA_BOOL, .datatype = gX_DATATYPE_I1, \
      .offset = offsetof(structType, field), \
      .hasDefault = 1, .defaultValue = (gX_F64)((def) ? 1 : 0), \
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE }

#define GX_SCHEMA_V3_DEF(key_, structType, field, def) \
    { .key = key_, .fieldType = GX_SCHEMA_SCALAR, .datatype = gX_DATATYPE_F32V3, \
      .offset = offsetof(structType, field), \
      .hasDefault = 1, .defaultValue = (gX_F64)(def) }

#define GX_SCHEMA_V2_DEF(key_, structType, field, def) \
    { .key = key_, .fieldType = GX_SCHEMA_SCALAR, .datatype = gX_DATATYPE_F32V2, \
      .offset = offsetof(structType, field), \
      .hasDefault = 1, .defaultValue = (gX_F64)(def) }

// ---------------------------------------------------------------------------
// Datatype size helper — returns the byte size of a gX_DATATYPE_* value.
// Returns 0 for unknown types.
// ---------------------------------------------------------------------------
static inline gX_U32 gX_Datatype_Size(gX_I32 datatype)
{
    switch (datatype)
    {
        case gX_DATATYPE_I8:
        case gX_DATATYPE_U8:    return 1;
        case gX_DATATYPE_I16:
        case gX_DATATYPE_U16:   return 2;
        case gX_DATATYPE_I32:
        case gX_DATATYPE_U32:
        case gX_DATATYPE_F32:
        case gX_DATATYPE_I1:    return 4;
        case gX_DATATYPE_I64:
        case gX_DATATYPE_U64:
        case gX_DATATYPE_F64:   return 8;
        case gX_DATATYPE_F32V2: return sizeof(gX_F32v2);
        case gX_DATATYPE_F32V3: return sizeof(gX_F32v3);
        case gX_DATATYPE_F32V4: return sizeof(gX_F32v4);
        case gX_DATATYPE_F64V2: return sizeof(gX_F64v2);
        case gX_DATATYPE_F64V3: return sizeof(gX_F64v3);
        case gX_DATATYPE_F64V4: return sizeof(gX_F64v4);
        case gX_DATATYPE_COLOR: return sizeof(gX_Color);
        case gX_DATATYPE_RECTANGLE: return sizeof(gX_Rectangle);
        default:                return 0;
    }
}

#ifdef __cplusplus
}
#endif

#endif //SCHEMA_H
