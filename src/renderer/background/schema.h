/**********************************************************************************************
*
*   [gX: src/renderer/background/schema.h]
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


#ifndef RENDERER_BACKGROUND_SCHEMA_H
#define RENDERER_BACKGROUND_SCHEMA_H

#ifdef __cplusplus
extern "C" {
#endif

//opencode: merged from schema2.h — offset-based JSON/YAML/XML save/load
#include "../../common/types.h"
struct gX_RendererBackground;
gX_I32 gX_RendererBG_Preset_Save_New(struct gX_RendererBackground *bg, const gX_I8 *path);
gX_I32 gX_RendererBG_Preset_Load_New(struct gX_RendererBackground *bg, const gX_I8 *path);
gX_I32 gX_RendererBG_Preset_Save_Yaml(struct gX_RendererBackground *bg, const gX_I8 *path);
gX_I32 gX_RendererBG_Preset_Load_Yaml(struct gX_RendererBackground *bg, const gX_I8 *path);
gX_I32 gX_RendererBG_Preset_Save_Xml(struct gX_RendererBackground *bg, const gX_I8 *path);
gX_I32 gX_RendererBG_Preset_Load_Xml(struct gX_RendererBackground *bg, const gX_I8 *path);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_BACKGROUND_SCHEMA_H
