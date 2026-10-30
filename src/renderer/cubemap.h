/**********************************************************************************************
*
*   [gX: src/renderer/cubemap.h]
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


#ifndef RENDERER_CUBEMAP_H
#define RENDERER_CUBEMAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

//opencode: cubemap generation utility — creates and renders cubemap textures
//   for environment reflections. Supports any module that can provide a
//   draw callback (GFX models, raymarch terrain, etc.)

// Callback type: render content for one cubemap face.
// faceCam is already configured with position, target, up, and fovy=90.
// The callback should draw 3D content (models, terrain, etc.).
// The GL state on entry: FBO=cubemap face, viewport=0,0,size,size,
// scissor=off, clear color set to clearColor. On exit: leave the face
// rendered into the FBO.
typedef gX_I0 (*gX_Cubemap_DrawFace)(gX_Camera3D *faceCam, gX_I32 faceIndex);

// Standard cubemap face directions: +X, -X, +Y, -Y, +Z, -Z
extern const gX_F32v3 gX_CUBEMAP_TARGETS[6];
extern const gX_F32v3 gX_CUBEMAP_UPS[6];

// Create an empty cubemap texture (size x size per face, GL_RGB, no mipmaps)
gX_Texture gX_Cubemap_Create(gX_U32 size);

// Destroy cubemap GPU resources
gX_I0 gX_Cubemap_Destroy(gX_Texture cubemap);

//opencode: render a cubemap from a world position.
//   If fbo > 0 and depthRbo > 0, reuses them (avoids alloc per frame).
//   When fbo==0, creates temporary FBO+depth internally and destroys after.
//   Saves/restores GL state (FBO, viewport, scissor).
gX_I0 gX_Cubemap_Render(gX_Texture cubemap, gX_U32 size,
                         gX_F32v3 position, gX_F32v3 clearColor,
                         gX_Cubemap_DrawFace drawFn);
gX_I0 gX_Cubemap_RenderEx(gX_Texture cubemap, gX_U32 size,
                           gX_F32v3 position, gX_F32v3 clearColor,
                           gX_Cubemap_DrawFace drawFn,
                           gX_U32 fbo, gX_U32 depthRbo);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_CUBEMAP_H
