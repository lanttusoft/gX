/**********************************************************************************************
*
*   [gX: src/renderer/gfx/schema.h]
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


#ifndef RENDERER_GFX_SCHEMA2_H
#define RENDERER_GFX_SCHEMA2_H

#include "../../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

//opencode: forward-declare gX_RendererGFX (defined in gfx.h)
typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: offset-based schema preset save/load — new API
gX_I32 gX_RendererGFX_Preset_Save_New(gX_RendererGFX *gfx, const gX_I8 *path);
//ai: owned in-memory snapshot; diagnostic mode excludes scene/asset collections.
struct json_object;
struct json_object *gX_RendererGFX_Preset_ToObj(gX_RendererGFX *Gfx, gX_I1 Diagnostic);
//ai: common camera encoding for preset, editor camera-list and report snapshots.
struct json_object *gX_RendererGFX_Preset_CameraToObj(const gX_Camera3D *Camera);
gX_I32 gX_RendererGFX_Preset_Load_New(gX_RendererGFX *gfx, const gX_I8 *path);

//opencode: validates all schema field offsets stay within their struct members
gX_I32 gX_RendererGFX_Schema_SelfTest(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_SCHEMA2_H
