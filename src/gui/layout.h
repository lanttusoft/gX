/**********************************************************************************************
*
*   [gX: src/gui/layout.h]
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


#ifndef GUI_LAYOUT_H
#define GUI_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

#include "config.h"
#include "enum.h"

//opencode: forward declarations (full types in element/struct.h + canvas.h)
typedef struct gX_GUI_Element gX_GUI_Element;
typedef struct gX_GUI_Canvas gX_GUI_Canvas;

//opencode: web box-model layout (Phase 1). Pure math + canvas resolve.
//   See element/layout.c for the size-resolution rules (auto = measured
//   content or stretch, per display mode).

//opencode: style inputs to the pure resolver — all values in PIXELS.
//   The element-level resolve converts grid-unit layout fields to pixels.
typedef struct gX_GUI_Layout_Style {
    gX_I32  display;    // gX_GUI_ELEMENT_LAYOUT_DISPLAY_*
    gX_I32  justify;    // gX_GUI_ELEMENT_LAYOUT_JUSTIFY_*
    gX_I32  align;      // gX_GUI_ELEMENT_LAYOUT_ALIGN_*
    gX_F32  gap;        // spacing between children (px)
    gX_F32  padding[4]; // top, right, bottom, left (px)
    gX_I1   wrap;       // flex lines wrap onto the cross axis when full
} gX_GUI_Layout_Style;

//opencode: one child item — inW/inH requested size (<=0 = auto), m[] margins
//   (top,right,bottom,left px), out* resolved rect (absolute box coords).
//   grow distributes leftover main-axis space by weight; min/max clamp the
//   main-axis size after growth (<=0 = unset).
typedef struct gX_GUI_Layout_Item {
    gX_F32 inW, inH;
    gX_F32 m[4];
    gX_F32 grow;
    gX_F32 minW, minH, maxW, maxH;
    gX_F32 outX, outY, outW, outH;
} gX_GUI_Layout_Item;

//opencode: pure box-model resolve — distributes items inside box per style.
//   Unit-testable without a canvas. Writes outX/outY/outW/outH per item.
gX_I0 gX_GUI_Layout_Resolve(const gX_GUI_Layout_Style *style,
                            gX_F32 boxX, gX_F32 boxY, gX_F32 boxW, gX_F32 boxH,
                            gX_GUI_Layout_Item *items, gX_U32 count);

//opencode: element-level resolve — measures text/texture children, runs the
//   pure math, writes resolved rects into the child elements. Called per
//   container element by gX_GUI_Canvas_Layout_Resolve.
gX_I0 gX_GUI_Element_Layout_Resolve(gX_GUI_Element *element, gX_GUI_Canvas *canvas);

//opencode: canvas pass — runs after the layer sort in Canvas_Draw, before the
//   draw loop. Element order = builder order, so nested containers resolve
//   correctly in one pass.
gX_I0 gX_GUI_Canvas_Layout_Resolve(gX_GUI_Canvas *canvas);

//opencode: child capture — records the heap slots built after the container
//   as its children, skipping slots owned by nested containers. Called by
//   Builder_Layout_End; unit-testable on a bare canvas heap.
struct gX_GUI_Element_Layout;
gX_I0 gX_GUI_Canvas_Layout_Capture(gX_GUI_Canvas *canvas, gX_I32 containerSlot,
    struct gX_GUI_Element_Layout *layout);

#ifdef __cplusplus
}
#endif

#endif //GUI_LAYOUT_H
