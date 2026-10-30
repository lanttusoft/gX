/**********************************************************************************************
*
*   [gX: src/renderer/gfx/glb_writer.h]
*   gX — a game engine
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


#ifndef RENDERER_GFX_GLB_WRITER_H
#define RENDERER_GFX_GLB_WRITER_H

#include "../../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gX_RendererGFX_Model gX_RendererGFX_Model;

//opencode: scene export v2 — write one glTF 2.0 binary (GLB) for a loaded
//   model: every mesh becomes a primitive with the world transform baked,
//   materials mirror the render-side values (PBR factors + emissive), and
//   override albedo/emissive maps are embedded. Returns false when the model
//   has no drawable mesh or the file cannot be written.
gX_I1 gX_RendererGFX_GLB_WriteModel(const gX_RendererGFX_Model *model,
    const gX_I8 *path);

#ifdef __cplusplus
}
#endif

#endif
