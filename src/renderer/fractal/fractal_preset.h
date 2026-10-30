/**********************************************************************************************
*
*   [gX: src/renderer/fractal/fractal_preset.h]
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


#ifndef RENDERER_FRACTAL_PRESET_H
#define RENDERER_FRACTAL_PRESET_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"

struct gX_RendererFractal; // forward decl

//opencode: save fractal state (gen, cam, pal, trap) to JSON file
gX_I32 gX_RendererFractal_Preset_Save(struct gX_RendererFractal *frac, const gX_I8 *path);

//opencode: load fractal state from JSON file
gX_I32 gX_RendererFractal_Preset_Load(struct gX_RendererFractal *frac, const gX_I8 *path);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_FRACTAL_PRESET_H
