/**********************************************************************************************
*
*   [gX: src/gui/canvas.h]
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


#ifndef GUI_CANVAS_H
#define GUI_CANVAS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/heap.h"
#include "../common/types.h"
#include "../wrappers/texture.h"
    
#include "config.h"
#include "css.h"

//opencode: audit — forward decl so the eligibility/draw-order helpers can
//   take element pointers without pulling the full element struct.
typedef struct gX_GUI_Element gX_GUI_Element;
    
typedef struct gX_GUI_Canvas_State {
    
    //gX_I1 useTransition;
    
    gX_I1 doNotLoadBuffer;
    
    gX_I32 cursorX;
    gX_I32 cursorY;
} gX_GUI_Canvas_State;

typedef struct gX_GUI_Canvas_Camera {
    gX_Camera2D camera2D;
    gX_Camera3D camera3D;
    gX_F32 ambientFactor;
    gX_F32v3 lightDir;
} gX_GUI_Canvas_Camera;
    
typedef struct gX_GUI_Canvas {
    gX_I8   name[gX_GUI_CANVAS_NAMEBFR_SIZE];
    gX_I8   description[gX_GUI_CANVAS_DESCBFR_SIZE];
    
    gX_I32  canvasID;
    gX_I32  templateID;
    
    gX_Rectangle canvasBox;
    gX_Color canvasHue;
    gX_F32v2 canvasOffset;
    gX_F32v2 canvasScale;
    //opencode: v0.3.8 — swing rotation in degrees applied at composite time
    //   (driven by the SKEW_SLIDE transition style)
    gX_F32  canvasAngle;
    
    gX_I1   refresh;
    gX_I1   doTransition;
    //opencode: per-canvas transition delay for staggering (seconds offset from global transition T)
    gX_F32  transitionDelay;
    gX_GUI_Canvas_State state;
    
    gX_GUI_Canvas_Camera camera;
    
    gX_Heap elements;
    
    //opencode: gameCSS stylesheets attached to this canvas (indices into the
    //   global sheet registry; attached by gX_GUI_Builder_CSS at build time)
    gX_I32  cssSheets[gX_GUI_CSS_MAX_CANVAS_SHEETS];
    gX_U32  cssSheetCount;
    
    gX_RenderTexture_Wrapper buffer;

    //opencode: scissor clip rect — if width > 0, elements are clipped to this rect
    gX_Rectangle clipRect;

    //opencode: edit lock — set by the GUI editor on its target canvas while
    //   open; element state updates stay inert (no hover/press/drag/click
    //   visuals, sounds, or actions) so editing never triggers the UI.
    gX_I1 editLock;
    
} gX_GUI_Canvas;


gX_I32 gX_GUI_Canvas_Load(   gX_GUI_Canvas *canvas);
gX_I32 gX_GUI_Canvas_Unload( gX_GUI_Canvas *canvas);

gX_I32 gX_GUI_Canvas_Clear(  gX_GUI_Canvas *canvas);

gX_I0 gX_GUI_Canvas_Add_Name(       gX_GUI_Canvas *canvas, const gX_I8 *name);
gX_I0 gX_GUI_Canvas_Add_Description(gX_GUI_Canvas *canvas, const gX_I8 *desc);
gX_I0 gX_GUI_Canvas_Add_CanvasID(   gX_GUI_Canvas *canvas, gX_I32 id);
gX_I0 gX_GUI_Canvas_Add_TemplateID( gX_GUI_Canvas *canvas, gX_I32 id);

gX_I32 gX_GUI_Canvas_Add_Element(    gX_GUI_Canvas *canvas);
gX_I32 gX_GUI_Canvas_Remove_Element( gX_GUI_Canvas *canvas, gX_U32 index);

//opencode: helpers — reduce boilerplate
gX_GUI_Canvas *gX_GUI_Canvas_FromIndex( gX_I32 index);
gX_I0          gX_GUI_Canvas_SetTransition(gX_GUI_Canvas *canvas, gX_I1 enabled);

gX_I0 gX_GUI_Canvas_Update( gX_GUI_Canvas *canvas);
gX_I0 gX_GUI_Canvas_Draw(   gX_GUI_Canvas *canvas);

//opencode: audit — single interaction-eligibility predicate (USED + visible +
//   enabled). Pointer picking, focus traversal, and keyboard/gamepad
//   activation all use this so a removed slot or a hidden/disabled control
//   can never be hovered, clicked, focused, or activated.
gX_I1 gX_GUI_Canvas_Element_Eligible(const gX_GUI_Canvas *canvas, gX_U32 index);

//opencode: audit — build the per-frame render-order index list (stable, by
//   layer, live slots only). The owning element array is never permuted, so
//   heap metadata (USED/UPDATE/DRAW) and element->id stay aligned to their
//   slot; callers draw in order[] order. order must hold at least
//   gX_GUI_CANVAS_DEFAULT_MAX_ELEMENTS entries.
gX_I0 gX_GUI_Canvas_BuildDrawOrder(const gX_GUI_Canvas *canvas, gX_U32 *order, gX_U32 maxOrder, gX_U32 *outCount);

//opencode: inverse-transform a screen-space point into canvas buffer space
//   using the composite transform (offset + canvasScale + canvasAngle).
//   Pointer hit testing runs in this space so transition visuals and their
//   interaction targets stay aligned. False when the canvas has no buffer or
//   the scale is degenerate (collapsed canvas). Exported for unit tests.
gX_I1 gX_GUI_Canvas_ToBufferPoint(const gX_GUI_Canvas *canvas, gX_F32v2 screen, gX_F32v2 *out);

//ai: forward-transform a canvas-buffer point to screen space (exact inverse of
//   ToBufferPoint — same offset/scale/angle, pivot at the unscaled dst
//   center). Used by scripting's click helpers to click element rects.
//   False when the canvas has no buffer or the scale is degenerate; callers
//   then treat the buffer point as screen space (no-buffer canvases draw
//   elements untransformed).
gX_I1 gX_GUI_Canvas_ToScreenPoint(const gX_GUI_Canvas *canvas, gX_F32v2 buffer, gX_F32v2 *out);

//opencode: per-frame canvas/element draw stats (reset each frame by gX_GUI_Draw)
extern gX_U32 g_guiCanvasDrawCount;
extern gX_U32 g_guiElementDrawCount;

#ifdef __cplusplus
}
#endif

#endif //GUI_CANVAS_H
