/**********************************************************************************************
*
*   [gX: src/renderer/gfx/camera.c]
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


#include "camera.h"

gX_I32 gX_RendererGFX_Camera_Init(gX_RendererGFX_Camera *cam) {
    //opencode: v3.0 — was (0,1,0) with fovy=5 orthographic, but that direction
    //   (straight down) is parallel to up=(0,1,0) → cross(up,view_dir)=0 →
    //   degenerate view matrix collapses all vertices → no fragments rendered.
    //   Moved to (3,1,3) perspective fovy=45 for a non-degenerate 3D view.
    cam->camera3D.position = (gX_F32v3){6.0f,2.5f,6.0f};
    cam->camera3D.target = (gX_F32v3){0.0f,0.0f,0.0f};
    cam->camera3D.up = (gX_F32v3){0.0f,1.0f,0.0f};
    cam->camera3D.fovy = 45.0f;
    cam->camera3D.projection = gX_CAMERA_PERSPECTIVE;
    //opencode: near plane 0 causes depth precision issues — use 0.1
    cam->near = 0.1f;
    cam->far = 12.0f;
    cam->size = 8.0f;
    return 0;
}
