/**********************************************************************************************
*
*   [gX: src/common/marshal/schema_json.h]
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

#ifndef SCHEMA_JSON_H
#define SCHEMA_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../schema.h"

// Forward declaration (json-c)
struct json_object;

// ---------------------------------------------------------------------------
// Top-level file I/O
// ---------------------------------------------------------------------------

// Save the struct described by {fields, fieldCount} rooted at 'base' to a JSON file.
gX_I32 gX_Schema_JSON_Save(gX_SchemaField *fields, gX_U32 fieldCount,
                            gX_I0 *base, const gX_I8 *path);

// Load the struct described by {fields, fieldCount} rooted at 'base' from a
// JSON file. Missing fields retain initialized values and emit descriptor-level
// diagnostics; required-field errors do not abort an otherwise parseable load.
// Heap fields are appended (caller must clear heap first).
gX_I32 gX_Schema_JSON_Load(gX_SchemaField *fields, gX_U32 fieldCount,
                            gX_I0 *base, const gX_I8 *path);

// ---------------------------------------------------------------------------
// Object-level (for nesting / recursion)
// ---------------------------------------------------------------------------

// Write fields into an existing json_object (for use inside OBJECT handlers).
gX_I32 gX_Schema_JSON_SaveToObj(gX_SchemaField *fields, gX_U32 fieldCount,
                                 gX_I0 *base, struct json_object *parent);

// Read fields from an existing json_object.
gX_I32 gX_Schema_JSON_LoadFromObj(gX_SchemaField *fields, gX_U32 fieldCount,
                                   gX_I0 *base, struct json_object *parent);

//opencode: external structs can preserve the JSON path in missing-key reports.
// Load an object with a diagnostic path prefix (for externally-owned structs).
gX_I32 gX_Schema_JSON_LoadFromObjPath(gX_SchemaField *fields,
                                       gX_U32 fieldCount, gX_I0 *base,
                                       struct json_object *parent,
                                       const gX_I8 *parentPath);

#ifdef __cplusplus
}
#endif

#endif //SCHEMA_JSON_H
