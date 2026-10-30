/**********************************************************************************************
*
*   [gX: src/renderer/gfx/sun_uv.h]
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


//opencode: shared sun screen-space UV computation — replaces duplicated code in godray.c and lensflare.c
#ifndef RENDERER_GFX_SUN_UV_H
#define RENDERER_GFX_SUN_UV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: compute sun screen-space UV from the directional light direction.
//   Projects sun direction through view-projection to get NDC, maps to UV.
//   Returns uv in [0,1] range, clamped to valid screen edges.
//   outBehind: set to true if sun is behind the camera (w <= 0).
gX_F32v2 gX_RendererGFX_GetSunScreenUV(gX_RendererGFX *gfx, gX_I1 *outBehind);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_SUN_UV_H
