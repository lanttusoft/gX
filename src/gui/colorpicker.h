/**********************************************************************************************
*
*   [gX: src/gui/colorpicker.h]
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


#ifndef GUI_COLORPICKER_H
#define GUI_COLORPICKER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

// Forward declaration for Builder
typedef struct gX_GUI_Builder gX_GUI_Builder;

typedef struct gX_ColorPicker {
    gX_I1 active;           // visible in current view
    gX_I32 dragChannel;     // 0=R, 1=G, 2=B, -1=idle
    gX_I1  leftWasDown;    // previous frame left button DOWN state

    // Target color bytes (pointers to element's color fields)
    gX_U8 *targetR;
    gX_U8 *targetG;
    gX_U8 *targetB;

    // Bar screen rects (updated on create)
    gX_Rectangle barRects[3];
    gX_Rectangle swatchRect;

    // Grid positions (set at create time)
    gX_F32 gridX, gridY;
} gX_ColorPicker;

// Initialize the picker with target color pointers
gX_I0 gX_ColorPicker_Init(gX_ColorPicker *picker, gX_U8 *r, gX_U8 *g, gX_U8 *b);

// Create visual elements using the Builder API (called during template creation)
gX_I0 gX_ColorPicker_Create(gX_ColorPicker *picker, gX_GUI_Builder *b, gX_F32 gridX, gX_F32 gridY);

// Handle mouse input (call from Editor Update or wherever)
// Returns 1 if the picker handled the input
gX_I1 gX_ColorPicker_Update(gX_ColorPicker *picker);

// Draw overlay (call from Editor Draw)
gX_I0 gX_ColorPicker_Draw(gX_ColorPicker *picker);

#ifdef __cplusplus
}
#endif

#endif //GUI_COLORPICKER_H
