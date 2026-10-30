/**********************************************************************************************
*
*   [gX: src/common/marshal/marshal.c]
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


//opencode: JSON marshal dispatcher (JSON-only — yaml/toml/xml schemas were
// stripped in compression P3: no consumers, settings is JSON-only).
#include "marshal.h"

#include <string.h>

#include "../log.h"

#include "schema_json.h"

gX_I32 gX_Marshal_Save(gX_SchemaField *fields, gX_U32 fieldCount,
                        gX_I0 *base, const gX_I8 *path)
{
    if (!strstr(path, ".json")) {
        gX_TraceLog(gX_LOG_ERROR, "Marshal: unsupported format for '%s' (JSON only)", path);
        return 1;
    }
    return gX_Schema_JSON_Save(fields, fieldCount, base, path);
}

gX_I32 gX_Marshal_Load(gX_SchemaField *fields, gX_U32 fieldCount,
                        gX_I0 *base, const gX_I8 *path)
{
    if (!strstr(path, ".json")) {
        gX_TraceLog(gX_LOG_ERROR, "Marshal: unsupported format for '%s' (JSON only)", path);
        return 1;
    }
    return gX_Schema_JSON_Load(fields, fieldCount, base, path);
}
