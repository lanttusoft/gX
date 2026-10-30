/**********************************************************************************************
*
*   [gX: src/scripting/tour/tour_overlay.cpp]
*   gX — a game engine
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


//ai: tour overlay API — a script-facing paint list replayed from the shell
//   draw hook (between the renderer and the GUI), the gX-native successor of
//   gameX's demo paint list (PaintClear/DrawRect/DrawText/SetColorRGB). Ops
//   accumulate in call order and persist across script suspensions
//   (Sleep/Yield); scripts call Overlay_Clear and re-add when the overlay
//   changes. All ops use the colour set by Overlay_SetColor (default white);
//   Overlay_Bar derives bg/border from it (progress bars). Text draws with
//   the default font — rich text/fonts live in the TextWriter_* surface.
//   Coordinates are render-target pixels; Overlay_Width/Height expose the
//   render size so tours can centre without hardcoding a resolution.
//opencode: raylib headers FIRST with C++ linkage — wrapper headers reached
//   through other includes would give raymath's operator overloads C linkage
//   (see src/tools/gfx/gfx_editor.cpp).
#include "raylib.h"
#include "raymath.h"

#include "tour.h"

#include <angelscript.h>

#include <string>

#include "../../common/log.h"
#include "../../main.h"
#include "../../wrappers/drawing.h"
#include "../../wrappers/shape.h"
#include "../../wrappers/text.h"

// ===========================================================================
// Paint list
// ===========================================================================

#define TOUR_OVERLAY_MAX 256

enum {
    TOUR_OVL_RECT = 0,
    TOUR_OVL_RECT_BORDER,
    TOUR_OVL_BAR,
    TOUR_OVL_LINE,
    TOUR_OVL_CIRCLE,
    TOUR_OVL_TRIANGLE,
    TOUR_OVL_TEXT
};

typedef struct TourOverlay_Op {
    gX_I32 kind;
    gX_F32 p[6];
    gX_Color color;
    std::string text;
} TourOverlay_Op;

static TourOverlay_Op s_overlayOps[TOUR_OVERLAY_MAX];
static gX_I32 s_overlayCount = 0;
static gX_Color s_overlayColor = {255, 255, 255, 255};

static TourOverlay_Op *TourOverlay_Push(gX_I32 kind) {
    if (s_overlayCount >= TOUR_OVERLAY_MAX) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "Overlay: paint list full (%d)", TOUR_OVERLAY_MAX);
        return NULL;
    }
    TourOverlay_Op *op = &s_overlayOps[s_overlayCount++];
    op->kind = kind;
    op->p[0] = op->p[1] = op->p[2] = op->p[3] = 0.0f;
    op->p[4] = op->p[5] = 0.0f;
    op->color = s_overlayColor;
    op->text.clear();
    return op;
}

// ===========================================================================
// Script API
// ===========================================================================

static void TourOverlay_Clear(void) {
    s_overlayCount = 0;
}

static void TourOverlay_SetColor(gX_I32 r, gX_I32 g, gX_I32 b, gX_I32 a) {
    s_overlayColor = (gX_Color){(gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a};
}

static void TourOverlay_Rect(gX_F32 x, gX_F32 y, gX_F32 w, gX_F32 h) {
    TourOverlay_Op *op = TourOverlay_Push(TOUR_OVL_RECT);
    if (op) { op->p[0] = x; op->p[1] = y; op->p[2] = w; op->p[3] = h; }
}

static void TourOverlay_RectBorder(gX_F32 x, gX_F32 y, gX_F32 w, gX_F32 h) {
    TourOverlay_Op *op = TourOverlay_Push(TOUR_OVL_RECT_BORDER);
    if (op) { op->p[0] = x; op->p[1] = y; op->p[2] = w; op->p[3] = h; }
}

static void TourOverlay_Bar(gX_F32 x, gX_F32 y, gX_F32 w, gX_F32 h, gX_F32 fillRatio) {
    TourOverlay_Op *op = TourOverlay_Push(TOUR_OVL_BAR);
    if (op) { op->p[0] = x; op->p[1] = y; op->p[2] = w; op->p[3] = h; op->p[4] = fillRatio; }
}

static void TourOverlay_Line(gX_F32 x1, gX_F32 y1, gX_F32 x2, gX_F32 y2) {
    TourOverlay_Op *op = TourOverlay_Push(TOUR_OVL_LINE);
    if (op) { op->p[0] = x1; op->p[1] = y1; op->p[2] = x2; op->p[3] = y2; }
}

static void TourOverlay_Circle(gX_F32 cx, gX_F32 cy, gX_F32 radius) {
    TourOverlay_Op *op = TourOverlay_Push(TOUR_OVL_CIRCLE);
    if (op) { op->p[0] = cx; op->p[1] = cy; op->p[2] = radius; }
}

static void TourOverlay_Triangle(gX_F32 x1, gX_F32 y1, gX_F32 x2, gX_F32 y2, gX_F32 x3, gX_F32 y3) {
    TourOverlay_Op *op = TourOverlay_Push(TOUR_OVL_TRIANGLE);
    if (op) { op->p[0] = x1; op->p[1] = y1; op->p[2] = x2; op->p[3] = y2; op->p[4] = x3; op->p[5] = y3; }
}

static void TourOverlay_Text(gX_F32 x, gX_F32 y, gX_F32 size, const std::string &text) {
    TourOverlay_Op *op = TourOverlay_Push(TOUR_OVL_TEXT);
    if (op) { op->p[0] = x; op->p[1] = y; op->p[2] = size; op->text = text; }
}

static gX_F32 TourOverlay_TextWidth(const std::string &text, gX_F32 size) {
    if (text.empty()) return 0.0f;
    gX_F32v2 measure = gX_RawString_Measure(text.c_str(), gX_Font_GetDefault(), size, 1.0f);
    return measure.x;
}

static gX_I32 TourOverlay_Count(void) {
    return s_overlayCount;
}

static gX_F32 TourOverlay_Width(void) {
    return (gX_F32)maindata.screenWidth;
}

static gX_F32 TourOverlay_Height(void) {
    return (gX_F32)maindata.screenHeight;
}

// ===========================================================================
// Draw hook replay
// ===========================================================================

static gX_I0 TourOverlay_Tick(gX_I0) {
    for (gX_I32 i = 0; i < s_overlayCount; i++) {
        TourOverlay_Op *op = &s_overlayOps[i];
        switch (op->kind) {
            case TOUR_OVL_RECT:
                gX_Rectangle_Draw((gX_Rectangle){op->p[0], op->p[1], op->p[2], op->p[3]}, op->color);
                break;
            case TOUR_OVL_RECT_BORDER:
                gX_Rectangle_Draw_Border((gX_Rectangle){op->p[0], op->p[1], op->p[2], op->p[3]}, op->color);
                break;
            case TOUR_OVL_BAR: {
                gX_Color bg = (gX_Color){0, 0, 0, 140};
                gX_Color border = (gX_Color){op->color.r, op->color.g, op->color.b, 180};
                gX_Rectangle_Draw_Bar((gX_Rectangle){op->p[0], op->p[1], op->p[2], op->p[3]},
                    op->p[4], bg, op->color, border);
                break;
            }
            case TOUR_OVL_LINE:
                //opencode: lines/circles/triangles are gX batch primitives
                //   (immediate) while rects/text defer to the raylib queue —
                //   flush the pending native ops first or a later-specified
                //   fill lands on top of the shape at frame end.
                gX_Drawing_Flush();
                gX_Line_Draw((gX_Line){{op->p[0], op->p[1]}, {op->p[2], op->p[3]}, 1.0f}, op->color);
                break;
            case TOUR_OVL_CIRCLE:
                gX_Drawing_Flush();
                gX_Circle_Draw((gX_Circle){{op->p[0], op->p[1]}, op->p[2]}, op->color);
                break;
            case TOUR_OVL_TRIANGLE:
                gX_Drawing_Flush();
                gX_Triangle_Draw((gX_Triangle){{op->p[0], op->p[1]}, {op->p[2], op->p[3]},
                    {op->p[4], op->p[5]}, 1.0f}, op->color);
                break;
            case TOUR_OVL_TEXT:
                if (!op->text.empty()) {
                    gX_RawString_Draw_Lesser(op->text.c_str(),
                        (gX_F32v2){op->p[0], op->p[1]}, op->p[2], op->color);
                }
                break;
            default:
                break;
        }
    }
    return;
}

// ===========================================================================
// Registration
// ===========================================================================

#define AS_CHECK(r) do { if (r < 0) \
    gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "AS tour-overlay registration failed (code %d)", r); \
} while(0)

gX_I32 gX_TourOverlay_Register(asIScriptEngine *engine) {
    static gX_I1 hooked = 0;
    if (!hooked) {
        if (gX_Scripting_RegisterDrawHook("tour_overlay", TourOverlay_Tick) == 0) {
            hooked = 1;
        }
    }

    int r = 0;

    r = engine->RegisterGlobalFunction("void Overlay_Clear()",
        asFUNCTION(TourOverlay_Clear), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Overlay_SetColor(int, int, int, int)",
        asFUNCTION(TourOverlay_SetColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Overlay_Rect(float, float, float, float)",
        asFUNCTION(TourOverlay_Rect), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Overlay_RectBorder(float, float, float, float)",
        asFUNCTION(TourOverlay_RectBorder), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Overlay_Bar(float, float, float, float, float)",
        asFUNCTION(TourOverlay_Bar), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Overlay_Line(float, float, float, float)",
        asFUNCTION(TourOverlay_Line), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Overlay_Circle(float, float, float)",
        asFUNCTION(TourOverlay_Circle), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Overlay_Triangle(float, float, float, float, float, float)",
        asFUNCTION(TourOverlay_Triangle), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Overlay_Text(float, float, float, const string &in)",
        asFUNCTION(TourOverlay_Text), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Overlay_TextWidth(const string &in, float)",
        asFUNCTION(TourOverlay_TextWidth), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Overlay_Count()",
        asFUNCTION(TourOverlay_Count), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Overlay_Width()",
        asFUNCTION(TourOverlay_Width), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Overlay_Height()",
        asFUNCTION(TourOverlay_Height), asCALL_CDECL); AS_CHECK(r);

    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "registered tour overlay API (paint list on the draw hook)");
    return 0;
}
