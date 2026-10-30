/**********************************************************************************************
*
*   [gX: src/renderer/raymarch/schema.c]
*   [renderer/raymarch/schema.c] — Raymarch JSON schema stubs
*
*   Copyright (c) 2025 lanttu
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

//opencode: GX backend — no-op stubs
#include "schema.h"

#if !defined(GAME_BACKEND_GX)
gX_JSON_Schema schemaJSON_rendererRM;
#endif

gX_I0 gX_RendererRaymarch_SchemaJSON_Create(gX_RendererRaymarch *rm) { (void)rm; }
gX_I0 gX_RendererRaymarch_SchemaJSON_Delete(gX_I0) { }
