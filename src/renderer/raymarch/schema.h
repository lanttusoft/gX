/**********************************************************************************************
*
*   [gX: src/renderer/raymarch/schema.h]
*   [renderer/raymarch/schema.h] — Raymarch JSON schema declarations
*   Ported from gameX — GX backend: struct definitions only
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

#ifndef RENDERER_RAYMARCH_SCHEMA_H
#define RENDERER_RAYMARCH_SCHEMA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/config.h"
#include "../../common/types.h"

#if !defined(GAME_BACKEND_GX)
#include "../../common/marshal/json.h"
extern gX_JSON_Schema schemaJSON_rendererRM;
#endif

typedef struct gX_RendererRaymarch gX_RendererRaymarch;

gX_I0 gX_RendererRaymarch_SchemaJSON_Create(gX_RendererRaymarch *rm);
gX_I0 gX_RendererRaymarch_SchemaJSON_Delete(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_RAYMARCH_SCHEMA_H
