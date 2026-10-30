/**********************************************************************************************
*
*   [gX: src/gui/schema.h]
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


#ifndef GX_GUI_SCHEMA_H
#define GX_GUI_SCHEMA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/marshal/json.h"
#include "gui.h"
#include "element/struct.h"

//opencode: GUI canvas JSON schema version — bump when element/module struct
//   layout changes so loaders can reject/migrate old files instead of
//   silently mis-reading them. Serialized as the root "guiVersion" term;
//   gX_GUI_SchemaVersionLoaded is the load target (pre-set to the current
//   version so files written before versioning load as v1).
//opencode: v2 — box-model layout module + per-element margins (2026-07-31)
#define gX_GUI_SCHEMA_VERSION 2u
extern gX_U32 gX_GUI_SchemaVersionLoaded;
    
extern gX_JSON_Schema schemaJSON_GUI;

//opencode: validate the loaded file's version against the current one —
//   warns (does not reject) on mismatch.
gX_I0 gX_GUI_Schema_CheckVersion(gX_I0);

gX_I0 gX_GUI_SchemaJSON_Create(gX_I32 id);
gX_I0 gX_GUI_SchemaJSON_Delete(gX_I0);
    
#ifdef __cplusplus
}
#endif

#endif //GX_GUI_SCHEMA_H
