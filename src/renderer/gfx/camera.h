/**********************************************************************************************
*
*   [gX: src/renderer/gfx/camera.h]
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


#ifndef RENDERER_GFX_CAMERA_H
#define RENDERER_GFX_CAMERA_H

#include "../../common/types.h"
#include "../../common/text.h"

#include "../../camera.h"

typedef struct gX_RendererGFX_Camera {
    gX_Camera3D      camera3D;
    gX_F32           near;
    gX_F32           far;
    gX_F32           size;
    gX_Matrix        view;
    gX_Matrix        projection;
    gX_Matrix        viewProjection;
    gX_RenderTexture *map;
} gX_RendererGFX_Camera;

gX_I32 gX_RendererGFX_Camera_Init(gX_RendererGFX_Camera *camera);

#endif //RENDERER_GFX_CAMERA_H
