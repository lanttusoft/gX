/**********************************************************************************************
*
*   [gX: src/common/marshal/marshal.h]
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


//opencode: created as part of the unified marshal system

#ifndef MARSHAL_H
#define MARSHAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../schema.h"

// Format-agnostic marshal dispatcher.
// Detects file format by extension and delegates to the appropriate
// schema reader/writer (JSON, YAML, TOML, XML).

gX_I32 gX_Marshal_Save(gX_SchemaField *fields, gX_U32 fieldCount,
                        gX_I0 *base, const gX_I8 *path);

gX_I32 gX_Marshal_Load(gX_SchemaField *fields, gX_U32 fieldCount,
                        gX_I0 *base, const gX_I8 *path);

#ifdef __cplusplus
}
#endif

#endif //MARSHAL_H
