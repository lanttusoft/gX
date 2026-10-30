/**********************************************************************************************
*
*   [gX: src/gui/templates/guieditor.c]
*   gX — a game engine
*
*   Copyright (c) 2026 lanttu, opencode
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


//opencode: v0.2.0-pre5 — GUI editor per gui2.png ("big editor pls"):
//   menu bar top, element selector stack left, transform panel right
//   (value peek), slider manipulators bottom. Edits a TARGET canvas's
//   elements live (GX_GUI_EDITOR_TARGET=<idx>, default 0). The editor
//   assumes a stable target while open — if the target rebuilds, re-select.

#include "template_header.h"

#include <stdlib.h>
#include <string.h>

#include "../builder.h"
#include "../gui.h"
#include "../canvas.h"
#include "../element/element.h"

#include "../../common/log.h"
#include "../../rhi/rhi.h"
#include "../../input.h"
#include "../element/timer.h"

#include "../binding.h"

//opencode: generation-checked target identity. A slot index is reusable after
//   a target rebuild, so editor bindings carry canvas + slot + generation.
typedef struct gX_GUI_Editor_Handle {
    gX_I32 canvasIndex;
    gX_U32 elementIndex;
    gX_U32 generation;
} gX_GUI_Editor_Handle;
static gX_GUI_Editor_Handle g_editorSelection = { -1, 0, 0 };
static gX_GUI_Canvas *g_editorCanvas = 0;

static gX_I32 gX_GUI_Editor_TargetIndex(gX_I0) {
    const gX_I8 *tEnv = (const gX_I8 *)getenv("GX_GUI_EDITOR_TARGET");
    return tEnv ? atoi(tEnv) : 0;
}

static gX_GUI_Canvas *gX_GUI_Editor_Target(gX_I0) {
    return gX_GUI_Canvas_FromIndex(gX_GUI_Editor_TargetIndex());
}

static gX_I0 gX_GUI_Editor_Selection_Clear(gX_I0) {
    g_editorSelection.canvasIndex = -1;
    g_editorSelection.elementIndex = 0;
    g_editorSelection.generation = 0;
}

//opencode: exposed (not static) for unit tests — the selection handle is the
//   editor's identity contract across target rebuilds.
gX_GUI_Element *gX_GUI_Editor_Selection_Resolve(gX_I0) {
    gX_GUI_Canvas *target = gX_GUI_Editor_Target();
    gX_GUI_Element *element = 0;
    if (!target || g_editorSelection.canvasIndex != gX_GUI_Editor_TargetIndex()
        || g_editorSelection.elementIndex >= target->elements.amount
        || !target->elements.data || !target->elements.meta
        || !target->elements.meta[g_editorSelection.elementIndex].USED) {
        gX_GUI_Editor_Selection_Clear();
        return 0;
    }
    element = &((gX_GUI_Element *)target->elements.data)[g_editorSelection.elementIndex];
    if (element->id != g_editorSelection.elementIndex
        || element->generation != g_editorSelection.generation) {
        gX_GUI_Editor_Selection_Clear();
        return 0;
    }
    return element;
}

gX_I0 gX_GUI_Editor_Selection_Set(gX_I32 elementIndex) {
    gX_GUI_Canvas *target = gX_GUI_Editor_Target();
    gX_GUI_Element *element = 0;
    gX_GUI_Editor_Selection_Clear();
    if (!target || elementIndex < 0 || (gX_U32)elementIndex >= target->elements.amount
        || !target->elements.data || !target->elements.meta
        || !target->elements.meta[elementIndex].USED) return;
    element = &((gX_GUI_Element *)target->elements.data)[elementIndex];
    if (element->id != (gX_U32)elementIndex || element->generation == 0) return;
    g_editorSelection.canvasIndex = gX_GUI_Editor_TargetIndex();
    g_editorSelection.elementIndex = (gX_U32)elementIndex;
    g_editorSelection.generation = element->generation;
}

//opencode: selector click — store index, refresh ONLY the editor canvas so
//   target-canvas slider bindings stay valid (a global gui.refresh would
//   rebuild the target and dangle every bound &elem->rect pointer)
static gX_I0 gX_GUI_Editor_Select(gX_I0 *arg);
static gX_I0 gX_GUI_Editor_Select(gX_I0 *arg) {
    gX_I32 elementIndex = (gX_I32)(gX_U64)(uintptr_t)arg;
    gX_GUI_Editor_Selection_Set(elementIndex);
    gX_TraceLog(gX_LOG_NOTICE, "GuiEditor: select %i", elementIndex);
    //opencode: gui.refresh gates the whole per-canvas refresh pass
    //   (update.c runs it only when the global flag is set) — without it the
    //   canvas->refresh flag sits unread. Target canvases have no refresh
    //   flag set, so they stay untouched and slider bindings stay valid.
    gui.refresh = true;
    if (g_editorCanvas) g_editorCanvas->refresh = true;
}

static gX_I0 gX_GUI_Editor_Rebuild(gX_I0 *arg);
static gX_I0 gX_GUI_Editor_Rebuild(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Canvas *target = g_editorCanvas ? gX_GUI_Editor_Target() : 0;
    if (target) {
        //opencode: target rebuild invalidates the selected element's fields.
        //   Clear the slot selection and rebuild the editor alongside the
        //   target so no surviving slider callback retains a raw pointer into
        //   a recycled target element slot.
        gX_GUI_Editor_Selection_Clear();
        target->refresh = true;
        if (g_editorCanvas) g_editorCanvas->refresh = true;
        gui.refresh = true;
    }
}

//opencode: v0.2.0-pre9 — live-sync timer: the outline + value peek are
//   build-time copies, so a repeating timer re-flags the editor canvas while
//   a selection exists. Skipped while a mouse button is held so a slider
//   drag is never interrupted by a mid-gesture rebuild.
static gX_I0 gX_GUI_Editor_SyncFire(gX_I0 *arg);
static gX_I0 gX_GUI_Editor_SyncFire(gX_I0 *arg) {
    (void)arg;
    if (g_editorSelection.canvasIndex < 0) return;
    if (!gX_GUI_Editor_Selection_Resolve()) {
        gui.refresh = true;
        if (g_editorCanvas) g_editorCanvas->refresh = true;
        return;
    }
    if (input.mouse.leftButton.DOWN || input.mouse.rightButton.DOWN) return;
    gui.refresh = true;
    if (g_editorCanvas) g_editorCanvas->refresh = true;
}

static gX_I0 gX_GUI_Editor_ToggleVisible(gX_I0 *arg);
static gX_I0 gX_GUI_Editor_ToggleVisible(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Element *element = gX_GUI_Editor_Selection_Resolve();
    if (!element) return;
    element->visible = element->visible ? 0 : 1;
}

static gX_I0 gX_GUI_Editor_CycleColor(gX_I0 *arg);
static gX_I0 gX_GUI_Editor_CycleColor(gX_I0 *arg) {
    (void)arg;
    static const gX_Color palette[] = {
        {90, 70, 140, 255}, {50, 40, 80, 255}, {130, 60, 110, 255},
        {60, 30, 55, 255},  {40, 60, 100, 255},{150, 120, 190, 255},
    };
    static const gX_I32 paletteCount = (gX_I32)(sizeof(palette)/sizeof(palette[0]));
    static gX_I32 colorIdx = 0;
    gX_GUI_Element *element = gX_GUI_Editor_Selection_Resolve();
    if (!element) return;
    colorIdx = (colorIdx + 1) % paletteCount;
    element->color = palette[colorIdx];
}

typedef struct gX_GUI_Editor_Field_Handle {
    gX_I32 canvasIndex;
    gX_U32 elementIndex;
    gX_U32 generation;
    gX_U32 field;
} gX_GUI_Editor_Field_Handle;

_Static_assert(sizeof(gX_GUI_Editor_Field_Handle) <= gX_GUI_ELEMENT_CALLBACK_CTX_MAX,
    "GUI editor field context exceeds callback context capacity");

//opencode: field-resolve core — shared by the live slider resolver and the
//   unit tests. Returns the target field pointer only while the slot still
//   holds the same element generation; NULL once the slot was removed,
//   recycled by an external rebuild, or the field id is unknown.
gX_I0 *gX_GUI_Editor_Field_Resolve(gX_I32 canvasIndex, gX_U32 elementIndex,
    gX_U32 generation, gX_U32 field) {
    gX_GUI_Canvas *target = gX_GUI_Canvas_FromIndex(canvasIndex);
    gX_GUI_Element *targetElement = 0;
    if (!target || !target->elements.data || !target->elements.meta
        || elementIndex >= target->elements.amount
        || !target->elements.meta[elementIndex].USED) return 0;
    targetElement = &((gX_GUI_Element *)target->elements.data)[elementIndex];
    if (targetElement->id != elementIndex
        || targetElement->generation != generation) return 0;
    switch (field) {
        case 0: return &targetElement->rect.x;
        case 1: return &targetElement->rect.y;
        case 2: return &targetElement->rect.width;
        case 3: return &targetElement->rect.height;
        case 4: return &targetElement->params.scale.x;
        default: break;
    }
    return 0;
}

static gX_I0 *gX_GUI_Editor_ResolveSlider(struct gX_GUI_Element_Data *data,
    struct gX_GUI_Element *element) {
    const gX_GUI_Editor_Field_Handle *ctx =
        (const gX_GUI_Editor_Field_Handle *)data->callbackCtx.bytes;
    gX_I0 *fieldPtr = 0;
    (void)element;
    fieldPtr = gX_GUI_Editor_Field_Resolve(ctx->canvasIndex, ctx->elementIndex,
        ctx->generation, ctx->field);
    if (fieldPtr) return fieldPtr;
    gX_GUI_Editor_Selection_Clear();
    gui.refresh = true;
    if (g_editorCanvas) g_editorCanvas->refresh = true;
    return 0;
}

static gX_I0 gX_GUI_Editor_BindSlider(gX_GUI_Builder *builder, gX_U32 field) {
    gX_GUI_Element_Data *data = gX_GUI_Element_Data_Get(builder->currentElement, 0);
    gX_GUI_Editor_Field_Handle ctx = {
        g_editorSelection.canvasIndex,
        g_editorSelection.elementIndex,
        g_editorSelection.generation,
        field,
    };
    if (!data) return;
    data->resolveDstPtr = gX_GUI_Editor_ResolveSlider;
    memcpy(data->callbackCtx.bytes, &ctx, sizeof(ctx));
}

gX_I0 gX_GUI_Template_GuiEditor(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_GuiEditor(gX_I32 canvasindex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    g_editorCanvas = canvas;
    //opencode: v0.2.0-pre9 — audit-forced canvases get no UPDATE flag, so
    //   the per-frame element loop (timers!) never runs on them. Enable it.
    if (gui.canvases.meta) gui.canvases.meta[canvasindex].UPDATE = true;
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "GuiEditor", "gX gui editor", gX_GUI_TEMPLATE_GUIEDITOR);

    gX_I32 targetIdx = gX_GUI_Editor_TargetIndex();
    gX_GUI_Canvas *target = gX_GUI_Editor_Target();
    gX_GUI_Element *elems = target ? (gX_GUI_Element *)target->elements.data : 0;
    gX_U32 elemCount = (target && elems) ? target->elements.amount : 0;
    gX_GUI_Element *sel = gX_GUI_Editor_Selection_Resolve();

    //opencode: menu bar — title + target info + REBUILD
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.0f, 0.0f, 8.0f, 0.45f);
        gX_GUI_Builder_Color(&b, 20, 15, 38, 235);
        gX_GUI_Builder_Text_Begin(&b);
            static gX_I8 barTxt[96];
            snprintf(barTxt, sizeof(barTxt), "GUI EDITOR - target canvas %i (%u elements)", targetIdx, elemCount);
            gX_GUI_Builder_Text_Set(&b, barTxt);
            if (b.curText) { b.curText->fontSize = 14; b.curText->color = (gX_Color){235, 230, 250, 240}; }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 6.6f, 0.05f, 1.3f, 0.35f);
        gX_GUI_Builder_Color(&b, 60, 30, 55, 220);
        gX_GUI_Element_Template_Button(b.currentElement);
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "REBUILD");
            b.currentElement->stateRendering.TEXT_CENTER_X = true;
            b.currentElement->stateRendering.TEXT_CENTER_Y = true;
            if (b.curText) { b.curText->fontSize = 11; b.curText->color = (gX_Color){245, 220, 238, 235}; }
        gX_GUI_Builder_Text_End(&b);
        gX_GUI_OnClick(b.currentElement, gX_GUI_Editor_Rebuild, 0);
    gX_GUI_Builder_Element_End(&b);

    //opencode: v0.2.0-pre9 — live-sync heartbeat (repeating 0.35s timer);
    //   fires only while a selection exists and no button is held
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 7.9f, 7.9f, 0.05f, 0.05f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        b.currentElement->stateRendering.RECT_NO_DRAW = true;
        gX_GUI_Builder_Timer_Begin(&b);
        gX_GUI_Builder_Timer_Mode(&b, gX_GUI_TIMER_REPEATING);
        gX_GUI_Builder_Timer_Duration(&b, 0.35f);
        gX_GUI_Builder_Timer_OnFire(&b, gX_GUI_Editor_SyncFire, 0);
    gX_GUI_Builder_Element_End(&b);

    //opencode: selector stack — one button per target element
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.0f, 0.5f, 2.0f, 7.5f);
        gX_GUI_Builder_Color(&b, 22, 17, 40, 225);

        gX_U32 shown = elemCount < 14 ? elemCount : 14;
        for (gX_U32 i = 0; i < shown; i++) {
            gX_I1 isSel = sel == &elems[i];
            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 0.05f, 0.6f + 0.52f * (gX_F32)i, 1.9f, 0.48f);
                if (isSel) gX_GUI_Builder_Color(&b, 90, 60, 130, 240);
                else       gX_GUI_Builder_Color(&b, 42, 34, 68, 210);
                gX_GUI_Element_Template_Button(b.currentElement);
                gX_GUI_Builder_Text_Begin(&b);
                    static gX_I8 itemTxt[160];
                    snprintf(itemTxt, sizeof(itemTxt), "[%u] %s", elems[i].id,
                        elems[i].name[0] && strcmp(elems[i].name, "unnamed") != 0 ? elems[i].name : gX_GUI_Element_Type_String(elems[i].type));
                    gX_GUI_Builder_Text_Set(&b, itemTxt);
                    if (b.curText) {
                        b.curText->fontSize = 11;
                        b.curText->color = isSel ? (gX_Color){255, 244, 255, 250}
                                                 : (gX_Color){205, 196, 232, 215};
                    }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_GUI_Editor_Select, (gX_I0*)(gX_U64)(uintptr_t)i);
            gX_GUI_Builder_Element_End(&b);
        }
    gX_GUI_Builder_Element_End(&b);

    //opencode: transform panel — value peek for the selection
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 6.0f, 0.5f, 2.0f, 7.5f);
        gX_GUI_Builder_Color(&b, 22, 17, 40, 225);

        if (sel) {
            struct { const gX_I8 *key; gX_F32 val; } peek[6] = {
                { "x",      sel->rect.x },
                { "y",      sel->rect.y },
                { "w",      sel->rect.width },
                { "h",      sel->rect.height },
                { "scale",  sel->params.scale.x },
                { "rotate", sel->params.angle },
            };
            for (gX_I32 pi = 0; pi < 6; pi++) {
                gX_GUI_Builder_Element_Begin(&b);
                    gX_GUI_Builder_RectGrid(&b, 6.1f, 0.65f + 0.45f * (gX_F32)pi, 1.8f, 0.4f);
                    gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
                    b.currentElement->stateRendering.RECT_NO_DRAW = true;
                    gX_GUI_Builder_Text_Begin(&b);
                        static gX_I8 peekTxt[96];
                        snprintf(peekTxt, sizeof(peekTxt), "%s: %.3f", peek[pi].key, peek[pi].val);
                        gX_GUI_Builder_Text_Set(&b, peekTxt);
                        if (b.curText) { b.curText->fontSize = 12; b.curText->color = (gX_Color){228, 220, 246, 230}; }
                    gX_GUI_Builder_Text_End(&b);
                gX_GUI_Builder_Element_End(&b);
            }
            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 6.1f, 3.55f, 1.8f, 0.9f);
                gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
                b.currentElement->stateRendering.RECT_NO_DRAW = true;
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, sel->name);
                    if (b.curText) { b.curText->fontSize = 10; b.curText->color = (gX_Color){180, 168, 214, 200}; }
                gX_GUI_Builder_Text_End(&b);
            gX_GUI_Builder_Element_End(&b);
            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 6.1f, 4.4f, 1.8f, 0.4f);
                gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
                b.currentElement->stateRendering.RECT_NO_DRAW = true;
                gX_GUI_Builder_Text_Begin(&b);
                    static gX_I8 typeTxt[64];
                    snprintf(typeTxt, sizeof(typeTxt), "type: %s", gX_GUI_Element_Type_String(sel->type));
                    gX_GUI_Builder_Text_Set(&b, typeTxt);
                    if (b.curText) { b.curText->fontSize = 11; b.curText->color = (gX_Color){170, 158, 206, 195}; }
                gX_GUI_Builder_Text_End(&b);
            gX_GUI_Builder_Element_End(&b);

            //opencode: v0.2.0-pre9 — visibility toggle + palette cycle
            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 6.1f, 5.0f, 0.85f, 0.42f);
                gX_GUI_Builder_Color(&b, 60, 30, 55, 220);
                gX_GUI_Element_Template_Button(b.currentElement);
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, sel->visible ? "HIDE" : "SHOW");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) { b.curText->fontSize = 10; b.curText->color = (gX_Color){245, 220, 238, 235}; }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_GUI_Editor_ToggleVisible, 0);
            gX_GUI_Builder_Element_End(&b);

            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 7.05f, 5.0f, 0.85f, 0.42f);
                gX_GUI_Builder_Color(&b, 40, 55, 90, 220);
                gX_GUI_Element_Template_Button(b.currentElement);
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "COLOR");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) { b.curText->fontSize = 10; b.curText->color = (gX_Color){215, 228, 252, 235}; }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_GUI_Editor_CycleColor, 0);
            gX_GUI_Builder_Element_End(&b);
        } else {
            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 6.1f, 0.65f, 1.8f, 0.4f);
                gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
                b.currentElement->stateRendering.RECT_NO_DRAW = true;
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "no selection");
                    if (b.curText) { b.curText->fontSize = 12; b.curText->color = (gX_Color){150, 138, 184, 180}; }
                gX_GUI_Builder_Text_End(&b);
            gX_GUI_Builder_Element_End(&b);
        }
    gX_GUI_Builder_Element_End(&b);

    //opencode: selection outline — wire rect over the live element position
    //   (pixel-space Rect; drawn before sliders so it sits under panels)
    if (sel) {
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_Rect(&b, sel->rect.x, sel->rect.y, sel->rect.width, sel->rect.height);
            gX_GUI_Builder_Color(&b, 120, 220, 255, 255);
            b.currentElement->stateRendering.RECT_DRAW_WIRE = true;
            b.currentElement->stateRendering.RECT_NO_DRAW = false;
        gX_GUI_Builder_Element_End(&b);
    }

    //opencode: slider manipulator row — direct write-through bindings into
    //   the selected element (drag mutates the target live; no rebuild)
    if (sel) {
        gX_F32 maxX = (gX_F32)b.width;
        gX_F32 maxY = (gX_F32)b.height;
        gX_GUI_Builder_Widget_Slider(&b, 2.1f, 6.6f, 1.85f, 0.4f, "x",      &sel->rect.x,          0.0f, maxX);
        gX_GUI_Editor_BindSlider(&b, 0);
        gX_GUI_Builder_Widget_Slider(&b, 2.1f, 7.1f, 1.85f, 0.4f, "y",      &sel->rect.y,          0.0f, maxY);
        gX_GUI_Editor_BindSlider(&b, 1);
        gX_GUI_Builder_Widget_Slider(&b, 4.05f, 6.6f, 1.85f, 0.4f, "w", &sel->rect.width,      0.0f, maxX);
        gX_GUI_Editor_BindSlider(&b, 2);
        gX_GUI_Builder_Widget_Slider(&b, 4.05f, 7.1f, 1.85f, 0.4f, "h", &sel->rect.height,     0.0f, maxY);
        gX_GUI_Editor_BindSlider(&b, 3);
        gX_GUI_Builder_Widget_Slider(&b, 2.1f, 7.55f, 3.8f, 0.4f, "scale",  &sel->params.scale.x,  0.0f, 4.0f);
        gX_GUI_Editor_BindSlider(&b, 4);
    }

    gX_GUI_Builder_Free(&b);
}
