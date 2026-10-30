/**********************************************************************************************
*
*   [gX: src/renderer/gfx/point_depth.h]
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



#ifndef RENDERER_GFX_POINT_DEPTH_H
#define RENDERER_GFX_POINT_DEPTH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "shader.h"

typedef struct gX_RendererGFX_PointDepth {
    gX_RendererGFX_Shader shader;
    gX_I32  lightSpaceLoc;
    gX_I32  lightPosLoc;
    gX_I32  farPlaneLoc;
    gX_I32  matModelLoc;
    gX_I1   locsCached;
} gX_RendererGFX_PointDepth;

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_POINT_DEPTH_H
