/**********************************************************************************************
*
*   [gX: src/gui/element/layout.c]
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

#include "element.h"
#include "../gui.h"
#include "modules.h"

#include "../enum.h"
#include "../layout.h"

#include "../../wrappers/wrappers.h"
#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../main.h"

//opencode: web box-model layout container (Phase 1). The container element
//   captures its children at build time (Builder_Layout_Begin/End records the
//   element heap slots between them). gX_GUI_Canvas_Layout_Resolve runs after
//   the layer sort in Canvas_Draw and writes each child's rect from the
//   container's display/justify/align/padding/gap + child margins.
//
//   Size resolution (v1):
//     - child rect w/h explicit (>0) → used as-is
//     - w==0 in ROW (or h==0 in COL): measured from content (text / texture)
//     - w==0 in COL/BLOCK: stretch to content width
//     - h==0 in ROW: stretch to content height (align=STRETCH default)
//   BLOCK behaves as FLEX_COL with width always stretching unless explicit.

//=============================================================================
// module lifecycle (macro-generated per the element module pattern)
//=============================================================================

inline gX_I0 gX_GUI_Element_Layout_Init(gX_GUI_Element_Layout *layout) {
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing layout values");
    
    layout->state     = 0;
    layout->display   = gX_GUI_ELEMENT_LAYOUT_DISPLAY_BLOCK;
    layout->justify   = gX_GUI_ELEMENT_LAYOUT_JUSTIFY_START;
    layout->align     = gX_GUI_ELEMENT_LAYOUT_ALIGN_STRETCH;
    layout->gap       = 0.0f;
    layout->padding[0] = 0.0f;
    layout->padding[1] = 0.0f;
    layout->padding[2] = 0.0f;
    layout->padding[3] = 0.0f;
    layout->wrap      = false;
    layout->childCount = 0;
    layout->memberFirst = -1;
    layout->memberLast = -1;
    
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing layout done");
}

gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEAR( Layout,layout)
gX_GUI_ELEMENT_MODULE_FUNCDEF_GET(   Layout,layout)
//opencode: custom Add — the element state bitfield is exactly 32 bits with no
//   room for a KEEP_HEAP_LAYOUT bit, so this mirrors the macro minus the state
//   write (the layout heap is always deinit'd on Unload instead).
gX_I32 gX_GUI_Element_Add_Layout(gX_GUI_Element *element) {
    gX_I32 errors = 0;
    if (!(element->layouts).state.INITIALIZED) {
        gX_Heap_Zero(&(element->layouts));
        gX_Heap_SetBlockSize(&(element->layouts), sizeof(gX_GUI_Element_Layout));
        gX_Heap_SetMax(&(element->layouts), gX_GUI_ELEMENT_DEFAULT_MAX_LAYOUTS);
        gX_Heap_SetAmount(&(element->layouts), 0);
        gX_Heap_Init(&(element->layouts));
    }
    gX_GUI_Element_Layout *elementmodule = gX_Heap_Add_FirstEmpty_NoPtr(&(element->layouts));
    if (!elementmodule) {
        gX_TraceLog(gX_LOG_WARNING, "GUI_ELEMENT: Layout heap full (max %i)",
            gX_GUI_ELEMENT_DEFAULT_MAX_LAYOUTS);
        errors++;
        return errors;
    }
    gX_GUI_Element_Layout_Clear(elementmodule);
    gX_GUI_Element_Layout_Init(elementmodule);
    return errors;
}
gX_GUI_ELEMENT_MODULE_FUNCDEF_REMOVE(Layout,layout)
gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEARMODULE( Layout,layout)

//=============================================================================
// pure box-model math (unit-testable — no canvas/GUI dependencies)
//=============================================================================

//opencode: main-axis min/max for one item (ROW tracks W, COL/BLOCK tracks H).
static gX_I0 gX_GUI_Layout_ClampMain(gX_GUI_Layout_Item *it, gX_I1 row) {
    gX_F32 *main = row ? &it->outW : &it->outH;
    gX_F32 minMain = row ? it->minW : it->minH;
    gX_F32 maxMain = row ? it->maxW : it->maxH;
    if (minMain > 0.0f && *main < minMain) *main = minMain;
    if (maxMain > 0.0f && *main > maxMain) *main = maxMain;
}

//opencode: single flex line — grows items into leftover main space, then
//   positions per justify/align. Shared by the flat path and every wrap line.
static gX_I0 gX_GUI_Layout_ResolveLine(const gX_GUI_Layout_Style *style, gX_I1 row,
    gX_F32 mainStart, gX_F32 mainSize, gX_F32 crossStart, gX_F32 crossSize,
    gX_GUI_Layout_Item *items, gX_U32 count) {
    gX_F32 total = 0.0f;
    gX_F32 free = 0.0f;
    gX_F32 growSum = 0.0f;
    gX_F32 cursor = mainStart;
    gX_F32 gap = style->gap;
    for (gX_U32 i = 0; i < count; i++) {
        gX_F32 mainMargins = row ? (items[i].m[1] + items[i].m[3])
                                 : (items[i].m[0] + items[i].m[2]);
        total += (row ? items[i].outW : items[i].outH) + mainMargins;
        if (i > 0) total += style->gap;
        if (items[i].grow > 0.0f) growSum += items[i].grow;
    }
    free = mainSize - total;
    if (free < 0.0f) free = 0.0f;
    //opencode: flex grow — leftover main space split by weight, then clamped
    //   to each item's main min/max (single pass; clamping may leave a
    //   remainder that justify distributes).
    if (free > 0.0f && growSum > 0.0f) {
        total = 0.0f;
        for (gX_U32 i = 0; i < count; i++) {
            if (items[i].grow > 0.0f) {
                if (row) items[i].outW += free * items[i].grow / growSum;
                else items[i].outH += free * items[i].grow / growSum;
                gX_GUI_Layout_ClampMain(&items[i], row);
            }
            gX_F32 mainMargins = row ? (items[i].m[1] + items[i].m[3])
                                     : (items[i].m[0] + items[i].m[2]);
            total += (row ? items[i].outW : items[i].outH) + mainMargins;
            if (i > 0) total += style->gap;
        }
        free = mainSize - total;
        if (free < 0.0f) free = 0.0f;
    }

    if (style->justify == gX_GUI_ELEMENT_LAYOUT_JUSTIFY_CENTER) {
        cursor += free * 0.5f;
    } else if (style->justify == gX_GUI_ELEMENT_LAYOUT_JUSTIFY_END) {
        cursor += free;
    } else if (style->justify == gX_GUI_ELEMENT_LAYOUT_JUSTIFY_SPACE_BETWEEN && count > 1) {
        gap += free / (gX_F32)(count - 1);
    }

    for (gX_U32 i = 0; i < count; i++) {
        gX_F32 w = items[i].outW, h = items[i].outH;
        gX_F32 mTop = items[i].m[0], mRight = items[i].m[1];
        gX_F32 mBottom = items[i].m[2], mLeft = items[i].m[3];
        gX_F32 mainBefore = (row) ? mLeft : mTop;
        gX_F32 mainAfter  = (row) ? mRight : mBottom;
        gX_F32 crossBefore = (row) ? mTop : mLeft;
        gX_F32 crossAfter  = (row) ? mBottom : mRight;

        //opencode: main axis — justify START/CENTER/END/SPACE_BETWEEN handled
        //   by cursor/gap above
        gX_F32 mainPos = cursor + mainBefore;

        //opencode: cross axis — align START/CENTER/END/STRETCH
        gX_F32 crossFree = crossSize - ((row ? h : w) + crossBefore + crossAfter);
        if (crossFree < 0.0f) crossFree = 0.0f;
        gX_F32 crossPos = crossStart + crossBefore;
        if (style->align == gX_GUI_ELEMENT_LAYOUT_ALIGN_CENTER) {
            crossPos = crossStart + crossBefore + crossFree * 0.5f;
        } else if (style->align == gX_GUI_ELEMENT_LAYOUT_ALIGN_END) {
            crossPos = crossStart + crossSize - (row ? h : w) - crossAfter;
            if (crossPos < crossStart + crossBefore) crossPos = crossStart + crossBefore;
        } else if (style->align == gX_GUI_ELEMENT_LAYOUT_ALIGN_STRETCH) {
            //opencode: stretch only when the child didn't request a size
            gX_I1 mainAuto = (row) ? (items[i].inH <= 0.0f) : (items[i].inW <= 0.0f);
            if (mainAuto) {
                crossPos = crossStart + crossBefore;
                if (row) { items[i].outH = h = crossSize - crossBefore - crossAfter; }
                else     { items[i].outW = w = crossSize - crossBefore - crossAfter; }
                if (w < 0.0f) w = 0.0f;
                if (h < 0.0f) h = 0.0f;
            }
        }

        if (row) {
            items[i].outX = mainPos;
            items[i].outY = crossPos;
        } else {
            items[i].outX = crossPos;
            items[i].outY = mainPos;
        }
        items[i].outW = w;
        items[i].outH = h;
        cursor = mainPos + ((row) ? w : h) + mainAfter + gap;
    }
}

gX_I0 gX_GUI_Layout_Resolve(const gX_GUI_Layout_Style *style,
                            gX_F32 boxX, gX_F32 boxY, gX_F32 boxW, gX_F32 boxH,
                            gX_GUI_Layout_Item *items, gX_U32 count) {
    gX_I1 row = false;
    gX_I1 col = false;
    gX_F32 mainStart = 0.0f;
    gX_F32 mainSize = 0.0f;
    gX_F32 crossStart = 0.0f;
    gX_F32 crossSize = 0.0f;
    if (!style || !items || count == 0) return;
    for (gX_U32 i = 0; i < count; i++) {
        items[i].outW = items[i].inW;
        items[i].outH = items[i].inH;
    }

    row = (style->display == gX_GUI_ELEMENT_LAYOUT_DISPLAY_FLEX_ROW);
    col = (style->display == gX_GUI_ELEMENT_LAYOUT_DISPLAY_FLEX_COL) ||
          (style->display == gX_GUI_ELEMENT_LAYOUT_DISPLAY_BLOCK);

    mainStart = boxX;
    mainSize  = boxW;
    crossStart = boxY;
    crossSize  = boxH;
    if (col) {
        mainStart = boxY;
        mainSize  = boxH;
        crossStart = boxX;
        crossSize  = boxW;
    }

    //opencode: base sizes — auto main size: explicit wins, 0 = measured
    //   content (ROW) or stretch (COL/BLOCK); auto cross: stretch (ROW) or
    //   measured (COL). Min/max clamp applies with or without grow.
    for (gX_U32 i = 0; i < count; i++) {
        gX_F32 w = items[i].outW, h = items[i].outH;
        if (row) {
            if (w <= 0.0f) w = 0.0f;
        } else {
            if (w <= 0.0f) w = boxW - items[i].m[1] - items[i].m[3];
            if (w < 0.0f) w = 0.0f;
        }
        if (row) {
            if (h <= 0.0f) h = boxH - items[i].m[0] - items[i].m[2];
            if (h < 0.0f) h = 0.0f;
        } else {
            if (h <= 0.0f) h = 0.0f;
        }
        items[i].outW = w;
        items[i].outH = h;
        gX_GUI_Layout_ClampMain(&items[i], row);
        if (row) {
            if (items[i].minH > 0.0f && h < items[i].minH) h = items[i].minH;
            if (items[i].maxH > 0.0f && h > items[i].maxH) h = items[i].maxH;
        } else {
            if (items[i].minW > 0.0f && w < items[i].minW) w = items[i].minW;
            if (items[i].maxW > 0.0f && w > items[i].maxW) w = items[i].maxW;
        }
        items[i].outW = w;
        items[i].outH = h;
    }

    //opencode: wrap — greedy flex lines along the main axis; each line gets
    //   its own justify pass and stacks on the cross axis by line depth.
    //   STRETCH fills the line depth. Disabled by default (flat path).
    if (style->wrap && count > 1) {
        gX_U32 lineStart = 0;
        gX_F32 crossCursor = crossStart;
        while (lineStart < count) {
            gX_U32 lineEnd = lineStart;
            gX_F32 lineUsed = 0.0f;
            gX_F32 lineCross = 0.0f;
            while (lineEnd < count) {
                gX_F32 mainMargins = row ? (items[lineEnd].m[1] + items[lineEnd].m[3])
                                         : (items[lineEnd].m[0] + items[lineEnd].m[2]);
                gX_F32 need = (row ? items[lineEnd].outW : items[lineEnd].outH)
                    + mainMargins + (lineEnd > lineStart ? style->gap : 0.0f);
                if (lineEnd > lineStart && lineUsed + need > mainSize) break;
                lineUsed += need;
                {
                    gX_F32 crossMargins = row ? (items[lineEnd].m[0] + items[lineEnd].m[2])
                                             : (items[lineEnd].m[1] + items[lineEnd].m[3]);
                    gX_F32 depth = (row ? items[lineEnd].outH : items[lineEnd].outW)
                        + crossMargins;
                    if (depth > lineCross) lineCross = depth;
                }
                lineEnd++;
            }
            if (lineEnd == lineStart) lineEnd++;
            gX_GUI_Layout_ResolveLine(style, row, mainStart, mainSize,
                crossCursor, lineCross, &items[lineStart], lineEnd - lineStart);
            crossCursor += lineCross + style->gap;
            lineStart = lineEnd;
        }
        return;
    }

    gX_GUI_Layout_ResolveLine(style, row, mainStart, mainSize,
        crossStart, crossSize, items, count);
}

//=============================================================================
// element-level resolve — measures children, runs the pure math, writes rects
//=============================================================================

static gX_GUI_Element *gX_GUI_Element_Layout_FindByID(gX_GUI_Canvas *canvas, gX_U32 id) {
    gX_GUI_Element *elements = canvas->elements.data;
    if (!elements) return NULL;
    for (gX_U32 i = 0; i < canvas->elements.amount; i++) {
        if (!canvas->elements.meta[i].USED) continue;
        if (elements[i].id == id) return &elements[i];
    }
    return NULL;
}

//opencode: measure a child's content size (text / texture) for auto sizing.
//   Mirrors the draw-time math in text.c (resScale) and texture params scale.
static gX_I1 gX_GUI_Element_Layout_Measure(gX_GUI_Element *child, gX_F32 *outW, gX_F32 *outH) {
    if (!child) return false;
    if (child->texts.amount > 0 && child->texts.data) {
        gX_GUI_Element_Text *text = &((gX_GUI_Element_Text*)child->texts.data)[0];
        if (text->text[0] != '\0' && (child->stateRendering.RECT_AUTO_WIDTH || child->stateRendering.RECT_AUTO_HEIGHT)) {
            //opencode: v0.2.0 — engine UI font first
            gX_Font font = (gui.style.uiFontLoaded) ? gui.style.uiFont : gX_Font_GetDefault();
            if (text->hasCustomFont && text->customFontPtr) font = *(text->customFontPtr);
            //opencode: explicit legacy math — identical bits to the old
            //   bare-macro division ((SW/1024)*mult), but context-free.
            gX_F32 resScale = (gX_F32)maindata.screenWidth / 1024.0f * gX_scale_mult;
            if (resScale < 0.5f) resScale = 0.5f;
            if (resScale > 4.0f) resScale = 4.0f;
            gX_F32 fontsize = text->fontSize * resScale;
            gX_F32 spacing = (gX_F32)text->spacing * resScale;
            gX_F32v2 len = gX_RawString_Measure(text->text, font, fontsize, spacing);
            if (outW) *outW = len.x;
            if (outH) *outH = len.y;
            return true;
        }
    }
    if (child->textures.amount > 0 && child->textures.data) {
        gX_GUI_Element_Texture *texMod = &((gX_GUI_Element_Texture*)child->textures.data)[0];
        if (texMod->texture) {
            gX_F32 w = (gX_F32)texMod->texture->width * texMod->params.scale.x;
            gX_F32 h = (gX_F32)texMod->texture->height * texMod->params.scale.y;
            if (texMod->params.src.width > 0) w = (gX_F32)texMod->params.src.width * texMod->params.scale.x;
            if (texMod->params.src.height > 0) h = (gX_F32)texMod->params.src.height * texMod->params.scale.y;
            if (outW) *outW = w;
            if (outH) *outH = h;
            return true;
        }
    }
    return false;
}

//opencode: child capture — all elements built after the container belong to
//   it, EXCEPT slots owned by an already-closed nested container (its
//   Layout_End recorded memberFirst/memberLast). Without the skip,
//   grandchildren would be double-parented and consume outer flex space.
//   Testable without a builder (INTERACT-style canvas heap + manual ranges).
gX_I0 gX_GUI_Canvas_Layout_Capture(gX_GUI_Canvas *canvas, gX_I32 containerSlot,
    gX_GUI_Element_Layout *layout) {
    gX_U32 amount = 0;
    if (!canvas || !layout || containerSlot < 0) return;
    if (!canvas->elements.data || !canvas->elements.meta) return;
    amount = canvas->elements.amount;
    layout->childCount = 0;
    layout->memberFirst = -1;
    layout->memberLast = -1;
    for (gX_I32 slot = containerSlot + 1; slot < (gX_I32)amount; slot++) {
        gX_GUI_Element *child = NULL;
        gX_I1 owned = false;
        gX_U32 o = 0;
        if (layout->childCount >= gX_GUI_ELEMENT_DEFAULT_MAX_LAYOUT_CHILDREN) break;
        if (!canvas->elements.meta[slot].USED) continue;
        child = gX_Heap_SeekPtr_Index(&(canvas->elements), (gX_U32)slot);
        if (!child) continue;
        if ((gX_I32)child->id <= containerSlot) continue;
        for (o = 0; o < amount && !owned; o++) {
            gX_GUI_Element *other = NULL;
            gX_GUI_Element_Layout *ol = NULL;
            if ((gX_I32)o == containerSlot) continue;
            if (!canvas->elements.meta[o].USED) continue;
            other = gX_Heap_SeekPtr_Index(&(canvas->elements), o);
            if (!other || other->layouts.amount == 0 || !other->layouts.data) continue;
            ol = &((gX_GUI_Element_Layout*)other->layouts.data)[0];
            if (ol == layout) continue;
            if (ol->memberFirst >= 0 && slot >= ol->memberFirst && slot <= ol->memberLast) owned = true;
        }
        if (owned) continue;
        layout->children[layout->childCount] = (gX_I32)child->id;
        layout->childCount++;
        if (layout->memberFirst < 0) layout->memberFirst = slot;
        layout->memberLast = slot;
    }
}

gX_I0 gX_GUI_Element_Layout_Resolve(gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    if (!element || !canvas || element->layouts.amount == 0 || !element->layouts.data) return;
    gX_GUI_Element_Layout *layout = &((gX_GUI_Element_Layout*)element->layouts.data)[0];
    if (layout->display == gX_GUI_ELEMENT_LAYOUT_DISPLAY_NULL) return;
    if (layout->childCount == 0 || layout->childCount > gX_GUI_ELEMENT_DEFAULT_MAX_LAYOUT_CHILDREN) return;
    
    //opencode: grid units → pixels from the canvas box (8x8 grid)
    gX_F32 gu = canvas->canvasBox.width / (gX_F32)gX_GUI_BUILDER_DIVISIONS;
    gX_F32 gv = canvas->canvasBox.height / (gX_F32)gX_GUI_BUILDER_DIVISIONS;
    
    gX_GUI_Layout_Style style;
    style.display = layout->display;
    style.justify = layout->justify;
    style.align   = layout->align;
    style.wrap    = layout->wrap;
    //opencode: gap is main-axis spacing — convert with the main-axis unit
    //   (was always gu: on non-square grids every FLEX_COL gap drifted by
    //   gap*(gu-gv), e.g. settings rows shifted +4/+6px at 1536x1010).
    style.gap     = layout->gap
        * ((layout->display == gX_GUI_ELEMENT_LAYOUT_DISPLAY_FLEX_ROW) ? gu : gv);
    style.padding[0] = layout->padding[0] * gv;
    style.padding[1] = layout->padding[1] * gu;
    style.padding[2] = layout->padding[2] * gv;
    style.padding[3] = layout->padding[3] * gu;
    
    gX_F32 boxX = element->rect.x + style.padding[3];
    gX_F32 boxY = element->rect.y + style.padding[0];
    gX_F32 boxW = element->rect.width - style.padding[1] - style.padding[3];
    gX_F32 boxH = element->rect.height - style.padding[0] - style.padding[2];
    if (boxW < 0.0f) boxW = 0.0f;
    if (boxH < 0.0f) boxH = 0.0f;
    
    gX_GUI_Layout_Item items[gX_GUI_ELEMENT_DEFAULT_MAX_LAYOUT_CHILDREN];
    memset(items, 0, sizeof(items));
    gX_U32 count = 0;
    
    for (gX_U32 i = 0; i < layout->childCount; i++) {
        gX_GUI_Element *child = gX_GUI_Element_Layout_FindByID(canvas, (gX_U32)layout->children[i]);
        if (!child || child->id == element->id) continue;
        gX_GUI_Layout_Item *it = &items[count];
        it->inW = child->rect.width;
        it->inH = child->rect.height;
        if (it->inW <= 0.0f || it->inH <= 0.0f) {
            gX_F32 mw = 0.0f, mh = 0.0f;
            if (gX_GUI_Element_Layout_Measure(child, &mw, &mh)) {
                if (it->inW <= 0.0f) it->inW = mw;
                if (it->inH <= 0.0f) it->inH = mh;
            }
        }
        it->m[0] = child->margin[0] * gv;
        it->m[1] = child->margin[1] * gu;
        it->m[2] = child->margin[2] * gv;
        it->m[3] = child->margin[3] * gu;
        //opencode: flex sizing — grid units to pixels (grow is unitless)
        it->grow = child->flexGrow;
        it->minW = child->flexMin[0] * gu;
        it->minH = child->flexMin[1] * gv;
        it->maxW = child->flexMax[0] * gu;
        it->maxH = child->flexMax[1] * gv;
        count++;
    }
    
    if (count == 0) return;
    
    gX_GUI_Layout_Resolve(&style, boxX, boxY, boxW, boxH, items, count);
    
    //opencode: write resolved rects back into the children
    gX_U32 wi = 0;
    for (gX_U32 i = 0; i < layout->childCount; i++) {
        gX_GUI_Element *child = gX_GUI_Element_Layout_FindByID(canvas, (gX_U32)layout->children[i]);
        if (!child || child->id == element->id) continue;
        gX_GUI_Layout_Item *it = &items[wi];
        child->rect.x = it->outX;
        child->rect.y = it->outY;
        child->rect.width = it->outW;
        child->rect.height = it->outH;
        wi++;
    }
}

gX_I0 gX_GUI_Canvas_Layout_Resolve(gX_GUI_Canvas *canvas) {
    if (!canvas || canvas->elements.data == NULL) return;
    gX_GUI_Element *elements = canvas->elements.data;
    //opencode: parents are built before children (builder order), so a single
    //   index-order pass positions nested containers correctly.
    for (gX_U32 i = 0; i < canvas->elements.amount; i++) {
        gX_GUI_Element *element = &elements[i];
        if (!canvas->elements.meta[i].USED) continue;
        if (element->layouts.amount == 0 || !element->layouts.data) continue;
        gX_GUI_Element_Layout_Resolve(element, canvas);
    }
}
