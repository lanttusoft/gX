/**********************************************************************************************
*
*   [gX: src/gui/builder.c]
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


#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "builder.h"
#include "gui.h"
#include "layout.h"
#include "element/element.h"

#include "../common/log.h"
#include "../common/macros.h"
#include "../main.h"

#include "../wrappers/wrappers.h"

#include "../resource/resource.h"
#include "element/timer.h"

extern gX_I0 gX_GUI_Element_Shader_Load(gX_GUI_Element_Shader *shader);

//=============================================================================
// helpers
//=============================================================================

static gX_I0 gX_GUI_Builder_GetResourceHeaps(gX_GUI_Builder *b) {
    gX_TraceLog_SuppressStart();
    b->fontHeap    = gX_ResourceManager_GetHeap_Type(gX_DATATYPE_STRUCT_FONT);
    b->modelHeap   = gX_ResourceManager_GetHeap_Type(gX_DATATYPE_STRUCT_MODEL);
    b->shaderHeap  = gX_ResourceManager_GetHeap_Type(gX_DATATYPE_STRUCT_SHADER);
    b->soundHeap   = gX_ResourceManager_GetHeap_Type(gX_DATATYPE_STRUCT_SOUND);
    b->textureHeap = gX_ResourceManager_GetHeap_Type(gX_DATATYPE_STRUCT_TEXTURE);
    gX_TraceLog_SuppressEnd();
    if (b->fontHeap)    b->fonts    = (gX_Font*)b->fontHeap->data;
    if (b->modelHeap)   b->models   = (gX_Model*)b->modelHeap->data;
    if (b->shaderHeap)  b->shaders  = (gX_Shader*)b->shaderHeap->data;
    if (b->soundHeap)   b->sounds   = (gX_Sound*)b->soundHeap->data;
    if (b->textureHeap) b->textures = (gX_Texture*)b->textureHeap->data;
}

//=============================================================================
// lifecycle
//=============================================================================

gX_GUI_Builder gX_GUI_Builder_New(gX_GUI_Canvas *canvas, const gX_I8 *name, const gX_I8 *desc, gX_I32 templateID) {
    gX_GUI_Builder b;
    memset(&b, 0, sizeof(b));

    b.canvas = canvas;
    b.elementIndex = -1;

    //opencode: use canvas box dimensions for grid math.
    if (canvas->canvasBox.width > 0 && canvas->canvasBox.height > 0) {
        b.width     = (gX_I32)canvas->canvasBox.width;
        b.height    = (gX_I32)canvas->canvasBox.height;
    } else {
        b.width     = maindata.screenWidth;
        b.height    = maindata.screenHeight;
    }
    b.widthDiv  = b.width / gX_GUI_BUILDER_DIVISIONS;
    b.heightDiv = b.height / gX_GUI_BUILDER_DIVISIONS;
    b.aspRatio  = (gX_F32)b.width / (gX_F32)b.height;

    gX_GUI_Canvas_Add_Name(canvas, name);
    gX_GUI_Canvas_Add_Description(canvas, desc);
    gX_GUI_Canvas_Add_CanvasID(canvas, canvas->canvasID);
    gX_GUI_Canvas_Add_TemplateID(canvas, templateID);

    gX_GUI_Builder_GetResourceHeaps(&b);

    gX_TraceLog_GUI(gX_LOG_TRACE, "GUI_Builder: New [%s] canvas %i", name, canvas->canvasID);
    return b;
}

gX_I0 gX_GUI_Builder_Free(gX_GUI_Builder *b) {
    (void)b;
    gX_TraceLog_GUI(gX_LOG_TRACE, "GUI_Builder: Free");
}

//=============================================================================
// element
//=============================================================================

gX_I0 gX_GUI_Builder_Element_Begin(gX_GUI_Builder *b) {
    if (!b || !b->canvas) return;

    //reset sub-module pointers and indices (each element has its own fresh heaps)
    b->curTexture   = 0;
    b->curText      = 0;
    b->curShader    = 0;
    b->curModel     = 0;
    b->curShape     = 0;
    b->curAnimation = 0;
    b->curData      = 0;
    b->curSound     = 0;
    b->curTimer     = 0;
    b->curAttribute = 0;
    b->curColor     = 0;
    b->texIdx = b->txtIdx = b->shdIdx = b->mdlIdx = b->shpIdx = b->timIdx = -1;
    b->aniIdx = b->datIdx = b->sndIdx = b->attIdx = b->colIdx = -1;

    //opencode: use the real heap slot from Add_Element — the old seek-by-
    //   counter diverged from the actual slot when the heap had holes
    //   (Add_FirstEmpty fills holes), and full-heap failure was silent,
    //   leaving currentElement NULL and every property call a silent no-op.
    gX_I32 slot = gX_GUI_Canvas_Add_Element(b->canvas);
    if (slot < 0) {
        gX_TraceLog_GUI(gX_LOG_ERROR, "GUI_Builder: element heap full (canvas '%s') — element not added", b->canvas->name);
        b->currentElement = NULL;
        return;
    }
    b->elementIndex = slot;
    b->currentElement = gX_Heap_SeekPtr_Index(&(b->canvas->elements), (gX_U32)slot);
    if (b->currentElement) {
        b->currentElement->id = (gX_U32)slot;
    }
}

gX_I0 gX_GUI_Builder_Element_End(gX_GUI_Builder *b) {
    (void)b;
}

//=============================================================================
// core element properties
//=============================================================================

gX_I0 gX_GUI_Builder_Rect(gX_GUI_Builder *b, gX_F32 x, gX_F32 y, gX_F32 w, gX_F32 h) {
    if (!b->currentElement) return;
    b->currentElement->rect = (gX_Rectangle){ x, y, w, h };
}

gX_I0 gX_GUI_Builder_RectGrid(gX_GUI_Builder *b, gX_F32 gx, gX_F32 gy, gX_F32 gw, gX_F32 gh) {
    if (!b->currentElement) return;
    //opencode: resolve container bounds for fill (gw=0/gh=0) and negative-edge positioning.
    //   With a Layout container active, elements position relative to its grid-space rect.
    //   gw=0 fills remaining container width after gx offset; same for gh=0.
    //   gx < 0 positions from the container's right edge (e.g. gx=-1 = 1 unit from right).
    gX_F32 baseX = 0.0f, baseY = 0.0f, baseW = (gX_F32)b->widthDiv * 8.0f, baseH = (gX_F32)b->heightDiv * 8.0f;
    if (b->layoutDepth > 0) {
        gX_Rectangle *c = &b->layoutStack[b->layoutDepth - 1];
        baseX = c->x * (gX_F32)b->widthDiv;
        baseY = c->y * (gX_F32)b->heightDiv;
        baseW = c->width  * (gX_F32)b->widthDiv;
        baseH = c->height * (gX_F32)b->heightDiv;
    }
    gX_F32 gu = (gX_F32)b->widthDiv;   // grid unit in pixels (canonical X unit)
    gX_F32 gv = (gX_F32)b->heightDiv;  // grid unit in pixels (canonical Y unit)
    // Element position: positive = from container left/top; negative = from right/bottom
    gX_F32 ex = (gx >= 0.0f) ? baseX + gx * gu : baseX + baseW + gx * gu;
    gX_F32 ey = (gy >= 0.0f) ? baseY + gy * gv : baseY + baseH + gy * gv;
    // Element size: gw>0 = explicit grid units; gw=0 = fill to container edge;
    //   gw<0 = container width + gw (e.g. gw=-1 shrinks width by 1 grid unit from right).
    gX_F32 ew, eh;
    if (gw > 0.0f)      ew = gw * gu;
    else if (gw == 0.0f) ew = (baseX + baseW) - ex;
    else                 ew = (baseX + baseW) - ex + gw * gu;  // gw negative = shrink from right
    if (gh > 0.0f)      eh = gh * gv;
    else if (gh == 0.0f) eh = (baseY + baseH) - ey;
    else                 eh = (baseY + baseH) - ey + gh * gv;
    b->currentElement->rect = (gX_Rectangle){ ex, ey, ew, eh };
}

//opencode: anchor placement — same container base as RectGrid, but the
//   position derives from the anchor edge instead of explicit gx/gy.
gX_I0 gX_GUI_Builder_RectAnchored(gX_GUI_Builder *b, gX_F32 gridW, gX_F32 gridH,
    gX_I32 hAnchor, gX_I32 vAnchor, gX_F32 offX, gX_F32 offY) {
    gX_F32 baseX = 0.0f, baseY = 0.0f, baseW = 0.0f, baseH = 0.0f;
    gX_F32 gu = 0.0f, gv = 0.0f, ew = 0.0f, eh = 0.0f, ex = 0.0f, ey = 0.0f;
    if (!b || !b->currentElement) return;
    baseW = (gX_F32)b->widthDiv * 8.0f;
    baseH = (gX_F32)b->heightDiv * 8.0f;
    if (b->layoutDepth > 0) {
        gX_Rectangle *c = &b->layoutStack[b->layoutDepth - 1];
        baseX = c->x * (gX_F32)b->widthDiv;
        baseY = c->y * (gX_F32)b->heightDiv;
        baseW = c->width  * (gX_F32)b->widthDiv;
        baseH = c->height * (gX_F32)b->heightDiv;
    }
    gu = (gX_F32)b->widthDiv;
    gv = (gX_F32)b->heightDiv;
    ew = gridW * gu;
    eh = gridH * gv;
    if (hAnchor == GX_GUI_ANCHOR_CENTER) ex = baseX + (baseW - ew) * 0.5f;
    else if (hAnchor == GX_GUI_ANCHOR_END) ex = baseX + baseW - ew;
    else ex = baseX;
    if (vAnchor == GX_GUI_ANCHOR_CENTER) ey = baseY + (baseH - eh) * 0.5f;
    else if (vAnchor == GX_GUI_ANCHOR_END) ey = baseY + baseH - eh;
    else ey = baseY;
    b->currentElement->rect = (gX_Rectangle){ ex + offX * gu, ey + offY * gv, ew, eh };
}

//opencode: percent placement — fractions of the container (or canvas) box.
gX_I0 gX_GUI_Builder_RectPercent(gX_GUI_Builder *b, gX_F32 xFrac, gX_F32 yFrac,
    gX_F32 wFrac, gX_F32 hFrac) {
    gX_F32 baseX = 0.0f, baseY = 0.0f, baseW = 0.0f, baseH = 0.0f;
    if (!b || !b->currentElement) return;
    baseW = (gX_F32)b->widthDiv * 8.0f;
    baseH = (gX_F32)b->heightDiv * 8.0f;
    if (b->layoutDepth > 0) {
        gX_Rectangle *c = &b->layoutStack[b->layoutDepth - 1];
        baseX = c->x * (gX_F32)b->widthDiv;
        baseY = c->y * (gX_F32)b->heightDiv;
        baseW = c->width  * (gX_F32)b->widthDiv;
        baseH = c->height * (gX_F32)b->heightDiv;
    }
    b->currentElement->rect = (gX_Rectangle){
        baseX + xFrac * baseW, baseY + yFrac * baseH,
        wFrac * baseW, hFrac * baseH };
}

//opencode: box-model layout container. Creates a CONTAINER element with a
//   layout module at the grid rect; pushes the coordinate stack for RectGrid
//   relative placement (existing behavior); tracks the open container for
//   Layout_* setters and child capture at Layout_End.
gX_I0 gX_GUI_Builder_Layout_Begin(gX_GUI_Builder *b, gX_F32 gx, gX_F32 gy, gX_F32 gw, gX_F32 gh) {
    if (!b) return;
    if (b->layoutDepth >= 8) {
        //opencode: do NOT decrement — the old overflow path corrupted the
        //   stack balance (dropped to 7, then the next Begin overwrote a live
        //   container and End counts triggered spurious underflow errors).
        gX_TraceLog(gX_LOG_ERROR, "GUI_Builder: layout depth exceeded (max 8)");
        return;
    }
    gX_Rectangle *c = &b->layoutStack[b->layoutDepth];
    c->x = gx; c->y = gy; c->width = gw; c->height = gh;
    
    //opencode: create the container element + layout module
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_CONTAINER;
    gX_GUI_Element_Add_Layout(b->currentElement);
    gX_GUI_Element_Layout *layout = gX_GUI_Element_Layout_Get(b->currentElement, 0);
    if (!layout) return;
    b->layoutModules[b->layoutDepth] = layout;
    b->layoutSlots[b->layoutDepth] = b->elementIndex;
    b->curLayout = layout;
    
    //opencode: position the container BEFORE pushing its own stack entry —
    //   otherwise RectGrid resolves against itself (base = the same grid rect,
    //   double-applied: a container at grid (4.5,6.0) landed at (9,12)).
    gX_GUI_Builder_RectGrid(b, gx, gy, gw, gh);
    
    b->layoutDepth++;
}

gX_I0 gX_GUI_Builder_Layout_End(gX_GUI_Builder *b) {
    if (!b) return;
    if (b->layoutDepth == 0) {
        gX_TraceLog(gX_LOG_ERROR, "GUI_Builder: layout stack underflow (no container to end)");
        return;
    }
    b->layoutDepth--;
    
    //opencode: capture children — all elements built between this container's
    //   slot and the current slot belong to it (nested-container members
    //   excluded via recorded member ranges — see Layout_Capture). Stored by
    //   heap slot id (the layer sort permutes the array; ids are
    //   permutation-independent).
    gX_GUI_Element_Layout *layout = b->layoutModules[b->layoutDepth];
    if (layout && b->canvas) {
        gX_I32 containerSlot = b->layoutSlots[b->layoutDepth];
        gX_GUI_Canvas_Layout_Capture(b->canvas, containerSlot, layout);
    }
    b->layoutModules[b->layoutDepth] = NULL;
    b->layoutSlots[b->layoutDepth] = -1;
    b->curLayout = (b->layoutDepth > 0) ? b->layoutModules[b->layoutDepth - 1] : NULL;
}

//opencode: box-model layout property setters — apply to the innermost open
//   container (grid units)
gX_I0 gX_GUI_Builder_Layout_Display(gX_GUI_Builder *b, gX_I32 display) {
    if (!b || !b->curLayout) return;
    b->curLayout->display = display;
}

gX_I0 gX_GUI_Builder_Layout_Justify(gX_GUI_Builder *b, gX_I32 justify) {
    if (!b || !b->curLayout) return;
    b->curLayout->justify = justify;
}

gX_I0 gX_GUI_Builder_Layout_Align(gX_GUI_Builder *b, gX_I32 align) {
    if (!b || !b->curLayout) return;
    b->curLayout->align = align;
}

gX_I0 gX_GUI_Builder_Layout_Gap(gX_GUI_Builder *b, gX_F32 gap) {
    if (!b || !b->curLayout) return;
    b->curLayout->gap = gap;
}

//opencode: wrap flex lines onto the cross axis when the main axis is full
gX_I0 gX_GUI_Builder_Layout_Wrap(gX_GUI_Builder *b, gX_I1 wrap) {
    if (!b || !b->curLayout) return;
    b->curLayout->wrap = wrap;
}

//opencode: per-child flex sizing — grow weight plus main-axis min/max
//   clamps (grid units, <=0 = unset). Applies to the current element.
gX_I0 gX_GUI_Builder_Layout_Grow(gX_GUI_Builder *b, gX_F32 grow) {
    if (!b || !b->currentElement) return;
    b->currentElement->flexGrow = grow;
}

gX_I0 gX_GUI_Builder_Layout_Min(gX_GUI_Builder *b, gX_F32 minW, gX_F32 minH) {
    if (!b || !b->currentElement) return;
    b->currentElement->flexMin[0] = minW;
    b->currentElement->flexMin[1] = minH;
}

gX_I0 gX_GUI_Builder_Layout_Max(gX_GUI_Builder *b, gX_F32 maxW, gX_F32 maxH) {
    if (!b || !b->currentElement) return;
    b->currentElement->flexMax[0] = maxW;
    b->currentElement->flexMax[1] = maxH;
}

gX_I0 gX_GUI_Builder_Layout_Padding(gX_GUI_Builder *b, gX_F32 top, gX_F32 right, gX_F32 bottom, gX_F32 left) {
    if (!b || !b->curLayout) return;
    b->curLayout->padding[0] = top;
    b->curLayout->padding[1] = right;
    b->curLayout->padding[2] = bottom;
    b->curLayout->padding[3] = left;
}

gX_I0 gX_GUI_Builder_Margin(gX_GUI_Builder *b, gX_F32 top, gX_F32 right, gX_F32 bottom, gX_F32 left) {
    if (!b || !b->currentElement) return;
    b->currentElement->margin[0] = top;
    b->currentElement->margin[1] = right;
    b->currentElement->margin[2] = bottom;
    b->currentElement->margin[3] = left;
}

//opencode: gameCSS — attach a stylesheet to the canvas (cached by path)
gX_I0 gX_GUI_Builder_CSS(gX_GUI_Builder *b, const gX_I8 *path) {
    if (!b || !b->canvas) return;
    gX_I32 idx = gX_GUI_CSS_LoadSheet(path);
    if (idx < 0) return;
    for (gX_U32 i = 0; i < b->canvas->cssSheetCount; i++) {
        if (b->canvas->cssSheets[i] == idx) return; // already attached
    }
    if (b->canvas->cssSheetCount >= gX_GUI_CSS_MAX_CANVAS_SHEETS) {
        gX_TraceLog(gX_LOG_WARNING, "GUI_Builder: canvas '%s' sheet limit reached (max %d)",
            b->canvas->name, gX_GUI_CSS_MAX_CANVAS_SHEETS);
        return;
    }
    b->canvas->cssSheets[b->canvas->cssSheetCount++] = idx;
}

gX_I0 gX_GUI_Builder_Class(gX_GUI_Builder *b, const gX_I8 *cls) {
    if (!b || !b->currentElement || !cls) return;
    strncpy(b->currentElement->cssClass, cls, sizeof(b->currentElement->cssClass) - 1);
}

gX_I0 gX_GUI_Builder_Style(gX_GUI_Builder *b, const gX_I8 *css) {
    if (!b || !b->currentElement || !css) return;
    strncpy(b->currentElement->cssStyle, css, sizeof(b->currentElement->cssStyle) - 1);
}

gX_I0 gX_GUI_Builder_Name(gX_GUI_Builder *b, const gX_I8 *name) {
    if (!b || !b->currentElement || !name) return;
    strncpy(b->currentElement->name, name, sizeof(b->currentElement->name) - 1);
}

gX_I0 gX_GUI_Builder_Color(gX_GUI_Builder *b, gX_U8 r, gX_U8 g, gX_U8 b_, gX_U8 a) {
    if (!b->currentElement) return;
    b->currentElement->color = (gX_Color){ r, g, b_, a };
}

gX_I0 gX_GUI_Builder_ColorV(gX_GUI_Builder *b, gX_Color color) {
    if (!b->currentElement) return;
    b->currentElement->color = color;
}

gX_I0 gX_GUI_Builder_RenderFlags(gX_GUI_Builder *b, gX_U32 flags) {
    if (!b->currentElement) return;
    gX_GUI_Element_Rendering_Bitmask *bm = (gX_GUI_Element_Rendering_Bitmask*)&b->currentElement->stateRendering;
    bm->bits = flags;
}

gX_I0 gX_GUI_Builder_StateFlags(gX_GUI_Builder *b, gX_U32 flags) {
    if (!b->currentElement) return;
    gX_GUI_Element_State_Bitmask *bm = (gX_GUI_Element_State_Bitmask*)&b->currentElement->state;
    bm->bits = flags;
}

//=============================================================================
// sub-module: internal helpers
//=============================================================================

#define BUILDER_ADD_MODULE(b, field, Field, idx) \
    if (!b || !b->currentElement) return; \
    gX_GUI_Element_Add_##Field(b->currentElement); \
    b->idx++; \
    b->field = gX_GUI_Element_##Field##_Get(b->currentElement, (gX_U32)b->idx); \
    if (!b->field) { gX_TraceLog_GUI(gX_LOG_WARNING, "GUI_Builder: "#Field" add failed"); }

//opencode: upgrade element type if still unknown — called by each module Begin_*
static gX_I0 gX_Builder_UpgradeType(gX_GUI_Builder *b, gX_GUI_Element_Type t) {
    if (b && b->currentElement && b->currentElement->type == gX_GUI_ELEMENT_TYPE_UNKNOWN) {
        b->currentElement->type = t;
    }
}

//=============================================================================
// texture module
//=============================================================================

gX_I0 gX_GUI_Builder_Texture_Begin(gX_GUI_Builder *b) {
    BUILDER_ADD_MODULE(b, curTexture, Texture, texIdx)
    gX_Builder_UpgradeType(b, gX_GUI_ELEMENT_TYPE_TEXTURE);
}

gX_I0 gX_GUI_Builder_Texture_File(gX_GUI_Builder *b, const gX_I8 *path) {
    if (!b->curTexture) return;
    //opencode: RM heap slots are plain gX_Texture, NOT gX_Texture_Wrapper —
    //   the old wrapper cast landed ~280 bytes past the block (garbage
    //   id/width → backend crash at draw time).
    gX_Texture *tex = (gX_Texture*)gX_RMHeap_GetPtr_FileName2(b->textureHeap, path);
    if (tex) {
        b->curTexture->texture = tex;
    } else {
        gX_TraceLog_GUI(gX_LOG_WARNING, "GUI_Builder: Texture not found: %s", path);
    }
}

gX_I0 gX_GUI_Builder_Texture_Set(gX_GUI_Builder *b, gX_Texture *tex) {
    if (!b->curTexture) return;
    b->curTexture->texture = tex;
}

//opencode: attach a shader element (index into element->shaders) to the texture
gX_I0 gX_GUI_Builder_Texture_Shader(gX_GUI_Builder *b, gX_U32 index) {
    if (!b->curTexture) return;
    b->curTexture->useShader = true;
    b->curTexture->shaderIndex = index;
}

//opencode: invertY auto-detected by backend for RT sources — no-op
gX_I0 gX_GUI_Builder_Texture_InvertY(gX_GUI_Builder *b, gX_I1 invert) {
    (void)b; (void)invert;
}

gX_I0 gX_GUI_Builder_Texture_End(gX_GUI_Builder *b) {
    (void)b;
}

//=============================================================================
// text module
//=============================================================================

gX_I0 gX_GUI_Builder_Text_Begin(gX_GUI_Builder *b) {
    BUILDER_ADD_MODULE(b, curText, Text, txtIdx)
    gX_Builder_UpgradeType(b, gX_GUI_ELEMENT_TYPE_LABEL);
}

gX_I0 gX_GUI_Builder_Text_Set(gX_GUI_Builder *b, const gX_I8 *text) {
    if (!b->curText) return;
    strncpy(b->curText->text, text, gX_GUI_ELEMENT_TEXT_MAX - 1);
    b->curText->text[gX_GUI_ELEMENT_TEXT_MAX - 1] = '\0';
    b->curText->length = (gX_U32)strlen(b->curText->text);
}

gX_I0 gX_GUI_Builder_Text_FontSize(gX_GUI_Builder *b, gX_F32 size) {
    if (!b->curText) return;
    b->curText->fontSize = size;
}

//opencode: opt-in shrink-to-fit — reduce the font size down to the configured
//   floor before wrap/truncation so a fixed-width control can keep its label
//   readable. Deliberately per-element: an intentionally oversized label keeps
//   its truncation signal when this is not set.
gX_I0 gX_GUI_Builder_Text_AutoShrink(gX_GUI_Builder *b, gX_I1 enable) {
    if (!b->curText) return;
    b->curText->autoShrink = enable ? 1 : 0;
}

//opencode: per-element text shadow (gui.style.textShadow tokens); the draw
//   path handles both the bitmap and SDF fonts.
gX_I0 gX_GUI_Builder_Text_Shadow(gX_GUI_Builder *b, gX_I1 enable) {
    if (!b || !b->currentElement) return;
    b->currentElement->stateRendering.TEXT_SHADOW = enable ? 1 : 0;
}

gX_I0 gX_GUI_Builder_Text_Offset(gX_GUI_Builder *b, gX_F32 x, gX_F32 y) {
    if (!b->curText) return;
    b->curText->offset = (gX_F32v2){ x, y };
}

gX_I0 gX_GUI_Builder_Text_Spacing(gX_GUI_Builder *b, gX_I32 spacing) {
    if (!b->curText) return;
    b->curText->spacing = spacing;
}

gX_I0 gX_GUI_Builder_Text_Color(gX_GUI_Builder *b, gX_Color color) {
    if (!b->curText) return;
    b->curText->color = color;
    b->curText->colorOriginal = color;
}

gX_I0 gX_GUI_Builder_Text_Font(gX_GUI_Builder *b, gX_Font *font) {
    if (!b->curText) return;
    b->curText->hasCustomFont = true;
    b->curText->customFontPtr = font;
}

gX_I0 gX_GUI_Builder_Text_FontDefault(gX_GUI_Builder *b) {
    if (!b->curText) return;
    b->curText->hasCustomFont = false;
    b->curText->customFontPtr = 0;
}

gX_I0 gX_GUI_Builder_Text_CenterX(gX_GUI_Builder *b) {
    if (!b->currentElement) return;
    b->currentElement->stateRendering.TEXT_CENTER_X = true;
}

gX_I0 gX_GUI_Builder_Text_CenterY(gX_GUI_Builder *b) {
    if (!b->currentElement) return;
    b->currentElement->stateRendering.TEXT_CENTER_Y = true;
}

gX_I0 gX_GUI_Builder_Text_End(gX_GUI_Builder *b) {
    (void)b;
}

//=============================================================================
// shader module
//=============================================================================

gX_I0 gX_GUI_Builder_Shader_Begin(gX_GUI_Builder *b) {
    BUILDER_ADD_MODULE(b, curShader, Shader, shdIdx)
    gX_Builder_UpgradeType(b, gX_GUI_ELEMENT_TYPE_SHADER);
    if (b->curShader) {
        b->curShader->drawShader = true;
    }
}

gX_I0 gX_GUI_Builder_Shader_Frag(gX_GUI_Builder *b, const gX_I8 *fragFile) {
    if (!b->curShader) return;
    b->curShader->shader = (gX_Shader*)gX_RMHeap_GetPtr_FileName2(b->shaderHeap, fragFile);
    if (!b->curShader->shader) {
        gX_TraceLog_GUI(gX_LOG_WARNING, "GUI_Builder: Shader not found: %s", fragFile);
        return;
    }
    //opencode: store frag path for pool migration (removes need for reverse RM heap lookup)
    strncpy(b->curShader->fragPath, fragFile, sizeof(b->curShader->fragPath) - 1);
    gX_GUI_Element_Shader_Load(b->curShader);
}

gX_I0 gX_GUI_Builder_Shader_Loops(gX_GUI_Builder *b, gX_U32 loops) {
    if (!b->curShader) return;
    b->curShader->loops = loops;
}

gX_I0 gX_GUI_Builder_Shader_Draw(gX_GUI_Builder *b, gX_I1 draw) {
    if (!b->curShader) return;
    b->curShader->drawShader = draw;
}

gX_I0 gX_GUI_Builder_Shader_Uniform(gX_GUI_Builder *b, gX_U32 index, gX_F32v4 value) {
    if (!b->curShader || index >= 4) return;
    b->curShader->uniforms[index].value = value;
    b->curShader->uniforms[index].initial = value;
}

gX_I0 gX_GUI_Builder_Shader_End(gX_GUI_Builder *b) {
    (void)b;
}

//opencode: one-call Aero-glass surface — glass.frag sampling the accumulated
//   backdrop (gui.ping: scene + earlier canvases) with token-driven blur,
//   tint, rim, refraction and gloss. Zeroed tokens or GX_GUI_GLASS=0 (the
//   force-off switch, same pattern as GX_GUI_SDF) leave the element as a
//   plain translucent fill — the honest fallback when glass is unavailable.
gX_I0 gX_GUI_Builder_SurfaceDepth(gX_GUI_Builder *b, gX_I32 mode) {
    if (!b || !b->currentElement) return;
    if (mode < 0) mode = 0;
    if (mode > 3) mode = 3;
    b->currentElement->stateRendering.SURFACE_DEPTH = (gX_U32)mode;
}

gX_I0 gX_GUI_Builder_Shader_Glass(gX_GUI_Builder *b) {
    if (!b || !b->curShader) return;
    const gX_I8 *forceOff = getenv("GX_GUI_GLASS");
    if (forceOff && forceOff[0] == '0') return;
    gX_F32 blur = gui.style.glass.blur;
    gX_F32 tint = gui.style.glass.tint;
    gX_F32 rim = gui.style.glass.rim;
    gX_F32 refraction = gui.style.glass.refraction;
    gX_F32 gloss = gui.style.glass.gloss;
    if (blur <= 0.01f && tint <= 0.001f && rim <= 0.001f && gloss <= 0.001f) return;
    gX_F32 resScale = (gX_F32)maindata.screenWidth / 1024.0f * gX_scale_mult;
    if (resScale < 0.5f) resScale = 0.5f;
    if (resScale > 4.0f) resScale = 4.0f;
    gX_GUI_Builder_Shader_Frag(b, "glass.frag");
    gX_GUI_Builder_Shader_Uniform(b, 0,
        (gX_F32v4){blur * resScale, tint, rim, refraction * resScale});
    gX_F32 radius = (b->currentElement && b->currentElement->cornerRadius >= 0.0f)
        ? b->currentElement->cornerRadius : gui.style.cornerRadius;
    gX_GUI_Builder_Shader_Uniform(b, 1, (gX_F32v4){gloss, radius, 0.0f, 0.0f});
    gX_GUI_Builder_Shader_Draw(b, true);
}

//=============================================================================
// model module
//=============================================================================

gX_I0 gX_GUI_Builder_Model_Begin(gX_GUI_Builder *b) {
    BUILDER_ADD_MODULE(b, curModel, Model, mdlIdx)
    gX_Builder_UpgradeType(b, gX_GUI_ELEMENT_TYPE_MODEL);
}

gX_I0 gX_GUI_Builder_Model_File(gX_GUI_Builder *b, const gX_I8 *path) {
    if (!b->curModel) return;
    //opencode: RM heap slots are plain gX_Model, NOT gX_Model_Wrapper —
    //   the old wrapper cast produced an OOB pointer (same flaw as Texture_File)
    //opencode: v0.3.8 — lookup by fileName1, not fileName2: model defaults are
    //   added with fn1=name ("teapot.glb"), so FileName2 never matches and the
    //   first template to use Model_File silently got NULL
    gX_Model *mdl = (gX_Model*)gX_RMHeap_GetPtr_FileName(b->modelHeap, path);
    if (mdl) {
        b->curModel->model = mdl;
    } else {
        gX_TraceLog_GUI(gX_LOG_WARNING, "GUI_Builder: Model not found: %s", path);
    }
}

//opencode: attach a shader element (by index into the element's shaders heap)
//   to the model — the shader element owns program/locs/uniforms/hot-reload
gX_I0 gX_GUI_Builder_Model_Shader(gX_GUI_Builder *b, gX_U32 index) {
    if (!b->curModel) return;
    b->curModel->useShader = true;
    b->curModel->shaderIndex = index;
}

gX_I0 gX_GUI_Builder_Model_End(gX_GUI_Builder *b) {
    (void)b;
}

//=============================================================================
// shape module
//=============================================================================

gX_I0 gX_GUI_Builder_Shape_Begin(gX_GUI_Builder *b, gX_I32 type) {
    BUILDER_ADD_MODULE(b, curShape, Shape, shpIdx)
    gX_Builder_UpgradeType(b, gX_GUI_ELEMENT_TYPE_SHAPE);
    if (b->curShape) {
        b->curShape->type = type;
    }
}

//opencode: attach a shader element (index into element->shaders) to the shape
gX_I0 gX_GUI_Builder_Shape_Shader(gX_GUI_Builder *b, gX_U32 index) {
    if (!b->curShape) return;
    b->curShape->useShader = true;
    b->curShape->shaderIndex = index;
}

gX_I0 gX_GUI_Builder_Shape_End(gX_GUI_Builder *b) {
    (void)b;
}

//=============================================================================
// animation module
//=============================================================================

gX_I0 gX_GUI_Builder_Animation_Begin(gX_GUI_Builder *b) {
    BUILDER_ADD_MODULE(b, curAnimation, Animation, aniIdx)
}

gX_I0 gX_GUI_Builder_Animation_End(gX_GUI_Builder *b) {
    (void)b;
}

//=============================================================================
// data module
//=============================================================================

gX_I0 gX_GUI_Builder_Data_Begin(gX_GUI_Builder *b) {
    BUILDER_ADD_MODULE(b, curData, Data, datIdx)
    if (b->curData) {
        b->curData->executeAnyway = true;
    }
}

gX_I0 gX_GUI_Builder_Data_Src(gX_GUI_Builder *b, gX_I0 *ptr, gX_I32 datatype) {
    if (!b->curData) return;
    b->curData->src.ptr = ptr;
    b->curData->src.datatype = datatype;
    b->curData->src.read = true;
}

gX_I0 gX_GUI_Builder_Data_Dst(gX_GUI_Builder *b, gX_I0 *ptr, gX_I32 datatype) {
    if (!b->curData) return;
    b->curData->dst.ptr = ptr;
    b->curData->dst.datatype = datatype;
    b->curData->dst.write = true;
}

gX_I0 gX_GUI_Builder_Data_Format(gX_GUI_Builder *b, const gX_I8 *fmt) {
    if (!b->curData) return;
    strncpy(b->curData->format, fmt, gX_GUI_ELEMENT_FORMAT_MAX - 1);
    b->curData->format[gX_GUI_ELEMENT_FORMAT_MAX - 1] = '\0';
}

gX_I0 gX_GUI_Builder_Data_End(gX_GUI_Builder *b) {
    (void)b;
}

//=============================================================================
// sound module
//=============================================================================

gX_I0 gX_GUI_Builder_Sound_Begin(gX_GUI_Builder *b) {
    BUILDER_ADD_MODULE(b, curSound, Sound, sndIdx)
}

gX_I0 gX_GUI_Builder_Sound_End(gX_GUI_Builder *b) {
    (void)b;
}

//=============================================================================
// timer module
//=============================================================================

gX_I0 gX_GUI_Builder_Timer_Begin(gX_GUI_Builder *b) {
    BUILDER_ADD_MODULE(b, curTimer, Timer, timIdx)
    if (b->curTimer) {
        gX_GUI_Element_Timer_Init(b->curTimer);
    }
}

gX_I0 gX_GUI_Builder_Timer_Mode(gX_GUI_Builder *b, gX_I32 mode) {
    if (!b->curTimer) return;
    b->curTimer->mode = (gX_GUI_Timer_Mode)mode;
}

gX_I0 gX_GUI_Builder_Timer_Duration(gX_GUI_Builder *b, gX_F32 seconds) {
    if (!b->curTimer) return;
    b->curTimer->duration = seconds;
}

gX_I0 gX_GUI_Builder_Timer_OnFire(gX_GUI_Builder *b, gX_I0 (*cb)(gX_I0*), gX_I0 *userData) {
    if (!b->curTimer) return;
    b->curTimer->onFire = cb;
    b->curTimer->onFireUserData = userData;
}

gX_I0 gX_GUI_Builder_Timer_End(gX_GUI_Builder *b) {
    (void)b;
}

//=============================================================================
// attribute module
//=============================================================================

gX_I0 gX_GUI_Builder_Attribute_Begin(gX_GUI_Builder *b) {
    BUILDER_ADD_MODULE(b, curAttribute, Attribute, attIdx)
}

gX_I0 gX_GUI_Builder_Attribute_XY(gX_GUI_Builder *b, gX_F32 x, gX_F32 y) {
    if (!b->curAttribute) return;
    b->curAttribute->value = (gX_F32v4){ x, y, 0.0f, 0.0f };
}

gX_I0 gX_GUI_Builder_Attribute_End(gX_GUI_Builder *b) {
    (void)b;
}

//=============================================================================
// animation builder helpers
//=============================================================================

gX_I0 gX_GUI_Builder_Animation_HoverScale(gX_GUI_Builder *b, gX_F32 scaleFactor) {
    if (!b || !b->currentElement) return;
    BUILDER_ADD_MODULE(b, curAnimation, Animation, aniIdx)
    if (!b->curAnimation) return;
    b->curAnimation->execute.hovered = true;
    b->curAnimation->execute.once = true;
    b->curAnimation->start = (gX_F32v4){scaleFactor, scaleFactor, 0, 0};
    b->curAnimation->end   = (gX_F32v4){1, 1, 0, 0};
    b->curAnimation->duration = gui.style.hoverSeconds;
    b->curAnimation->doScale = true;
    b->curAnimation->easing = gX_EASE_QUAD_OUT;
}

gX_I0 gX_GUI_Builder_Animation_StateEntrance(gX_GUI_Builder *b,
    gX_I32 state, gX_I32 *statePtr, gX_F32 duration)
{
    if (!b || !b->currentElement) return;
    BUILDER_ADD_MODULE(b, curAnimation, Animation, aniIdx)
    if (!b->curAnimation) return;
    b->curAnimation->readTransitionGUI = true;
    b->curAnimation->readTransitionOut = true;
    b->curAnimation->readTransitionIn = true;
    b->curAnimation->readCondition = true;
    b->curAnimation->cond = state;
    b->curAnimation->condPtr = statePtr;
    b->curAnimation->doScale = true;
    b->curAnimation->modeAdd = true;
    b->curAnimation->start = (gX_F32v4){1, 1, 0, 0};
    b->curAnimation->end   = (gX_F32v4){2, 0, 0, 0};
    b->curAnimation->duration = duration;
    b->curAnimation->easing = gX_EASE_QUAD_OUT;
}

gX_I0 gX_GUI_Builder_Animation_TransitionFade(gX_GUI_Builder *b) {
    if (!b || !b->currentElement) return;
    BUILDER_ADD_MODULE(b, curAnimation, Animation, aniIdx)
    if (!b->curAnimation) return;
    b->curAnimation->readTransitionGUI = true;
    b->curAnimation->readTransitionOut = true;
    b->curAnimation->readTransitionIn = true;
    b->curAnimation->doScale = true;
    b->curAnimation->modeAdd = true;
    b->curAnimation->start = (gX_F32v4){1, 1, 0, 0};
    b->curAnimation->end   = (gX_F32v4){2, 0, 0, 0};
    b->curAnimation->duration = gui.style.hoverSeconds;
    b->curAnimation->easing = gX_EASE_QUAD_OUT;
}
//widget template implementations moved to builder_widgets.c
