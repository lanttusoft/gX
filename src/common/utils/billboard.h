/**********************************************************************************************
*
*   [gX: src/common/utils/billboard.h]
*   //opencode: reusable billboard drawing utility — always faces the camera
*
*   Copyright (c) 2025-2026 lanttu, opencode
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

#ifndef UTILS_BILLBOARD_H
#define UTILS_BILLBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../types.h"

// Draw a textured quad that always faces the camera (billboard)
// position: world position of the billboard center
// size: width and height of the billboard
// texture: the sprite texture to draw (may be NULL for white)
// cameraPos: the camera's world position (for face-tracking)
gX_I0 gX_Billboard_Draw(gX_F32v3 position, gX_F32v2 size, gX_Texture *texture, gX_F32v3 cameraPos);

// Draw a billboard with tint color
gX_I0 gX_Billboard_Draw_Tinted(gX_F32v3 position, gX_F32v2 size, gX_Texture *texture, gX_F32v3 cameraPos, gX_Color tint);

// Draw a text string as a camera-facing billboard in 3D space
// Uses a shared reusable render texture (no per-frame allocations)
// text: the string to draw
// fontSize: text size in pixels
// color: text color
// font: label font; not-ready fonts fall back to the engine default
gX_I0 gX_Billboard_Text_Draw(gX_F32v3 position, gX_F32v2 size, const gX_I8 *text, gX_F32 fontSize, gX_Color color, gX_F32v3 cameraPos, gX_Font font);

// Draw a text string as a flat 3D quad with fixed orientation (non-billboard, sign-like)
// text: the string to draw
// rotation: euler angles in degrees for quad orientation
gX_I0 gX_Text3D_Draw(gX_F32v3 position, gX_F32v2 size, gX_F32v3 rotation, const gX_I8 *text, gX_F32 fontSize, gX_Color color, gX_Font font);

//ai: optional shadow is rasterized into the shared label RT (pixel offset 0..4).
gX_I0 gX_Billboard_Text_Draw_Styled(gX_F32v3 position, gX_F32v2 size, const gX_I8 *text,
    gX_F32 fontSize, gX_Color color, gX_F32v3 cameraPos, gX_Font font,
    gX_Color shadowColor, gX_F32 shadowPixels);
gX_I0 gX_Text3D_Draw_Styled(gX_F32v3 position, gX_F32v2 size, gX_F32v3 rotation,
    const gX_I8 *text, gX_F32 fontSize, gX_Color color, gX_Font font,
    gX_Color shadowColor, gX_F32 shadowPixels);

#ifdef __cplusplus
}
#endif

#endif //UTILS_BILLBOARD_H
