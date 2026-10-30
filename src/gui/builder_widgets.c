/**********************************************************************************************
*
*   [gX: src/gui/builder_widgets.c]
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


#include "builder.h"
#include "gui.h"
#include "binding.h"
#include "element/element.h"
#include "templates/template_header.h"
//opencode: hunt - implicit-decl sweep (float-arg promotion hazard)

#include <stdio.h>
#include "../common/log.h"
#include "../common/macros.h"
#include "../main.h"
#include "../input.h"
#include "../audio.h"
#if gX_ENABLE_SCENE == 1
    #include "../scene/scene.h"
#endif
#include "../wrappers/color.h"

#include "colorpicker.h"

//opencode: widget template implementations — split from builder.c to keep files focused.
//Declarations live in builder.h alongside the low-level builder primitives.

extern gX_I0 gX_GUI_Element_Template_Slider(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Template_CheckBox(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Template_DropDown(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Template_Radio(gX_GUI_Element *element);

//opencode: widget callback safety — owner-scoped contexts hold raw canvas
//   pointers. Validate the canvas is still a live heap slot before touching
//   refresh so a stale context can only skip its refresh, never write through a
//   dangling pointer. The separate scroll geometry registry is checked here too.
static gX_I1 gX_GUI_Widget_CanvasLive(const gX_GUI_Canvas *canvas)
{
    gX_GUI_Canvas *base = (gX_GUI_Canvas*)gui.canvases.data;
    if (!canvas || !base || gui.canvases.amount == 0) return false;
    return canvas >= base && canvas < base + gui.canvases.amount;
}

//opencode: v0.6.x — request a rebuild of the widget's canvas. Shared by the
//   owner-scoped widget callbacks below.
static gX_I0 gX_GUI_Widget_RequestRefresh(gX_GUI_Canvas *canvas)
{
    gui.refresh = 1;
    if (gX_GUI_Widget_CanvasLive(canvas)) canvas->refresh = 1;
}

//=============================================================================
// Owner-scoped widget callback contexts (GUI audit finding 4)
//=============================================================================
//opencode: v0.6.x — widget callbacks used to read fixed global pools keyed by
//   an index packed into funcPtrArg. A partial canvas refresh reset the pool
//   while other canvases still pointed into it, so a click could mutate another
//   widget's pointers or state. Each context now lives inline in the owning
//   element's data module (gX_GUI_OnClick_Context); it travels with the element,
//   so untouched canvases keep valid contexts. Each callback casts its arg to
//   the struct built by its widget function.

typedef struct gX_Widget_Ctx_Back {
    gX_I32 targetState;
    gX_I32 *statePtr;
    gX_GUI_Canvas *canvas;
} gX_Widget_Ctx_Back;

static gX_I0 back_onclick_cb(gX_I0 *arg) {
    const gX_Widget_Ctx_Back *ctx = (const gX_Widget_Ctx_Back*)arg;
    if (!ctx || !ctx->statePtr) return;
    *ctx->statePtr = ctx->targetState;
    gui.refresh = 1;
    if (gX_GUI_Widget_CanvasLive(ctx->canvas)) ctx->canvas->refresh = 1;
    gui.transition.execute = 1;
}

typedef struct gX_Widget_Ctx_SetI32 {
    gX_I32 value;
    gX_I32 *statePtr;
} gX_Widget_Ctx_SetI32;

static gX_I0 set_i32_onclick_cb(gX_I0 *arg) {
    const gX_Widget_Ctx_SetI32 *ctx = (const gX_Widget_Ctx_SetI32*)arg;
    if (!ctx || !ctx->statePtr) return;
    *ctx->statePtr = ctx->value;
}

//checkbox + toggle share the flip-a-bool shape.
typedef struct gX_Widget_Ctx_Bool {
    gX_I1 *valPtr;
    gX_GUI_Canvas *canvas;
} gX_Widget_Ctx_Bool;

static gX_I0 checkbox_onclick_cb(gX_I0 *arg) {
    const gX_Widget_Ctx_Bool *ctx = (const gX_Widget_Ctx_Bool*)arg;
    if (!ctx || !ctx->valPtr) return;
    *ctx->valPtr = !*ctx->valPtr;
    gX_GUI_Widget_RequestRefresh(ctx->canvas);
}

static gX_I0 toggle_onclick_cb(gX_I0 *arg) {
    const gX_Widget_Ctx_Bool *ctx = (const gX_Widget_Ctx_Bool*)arg;
    if (!ctx || !ctx->valPtr) return;
    *ctx->valPtr = !*ctx->valPtr;
    gX_GUI_Widget_RequestRefresh(ctx->canvas);
}

typedef struct gX_Widget_Ctx_Cycle {
    gX_I32 *valPtr;
    gX_I32 maxValue;
    gX_GUI_Canvas *canvas;
} gX_Widget_Ctx_Cycle;

static gX_I0 dropdown_onclick_cb(gX_I0 *arg) {
    const gX_Widget_Ctx_Cycle *ctx = (const gX_Widget_Ctx_Cycle*)arg;
    if (!ctx || !ctx->valPtr) return;
    (*ctx->valPtr)++;
    if (*ctx->valPtr > ctx->maxValue) *ctx->valPtr = 0;
    gX_GUI_Widget_RequestRefresh(ctx->canvas);
}

typedef struct gX_Widget_Ctx_Select {
    gX_I32 *statePtr;
    gX_I32 setValue;
    gX_GUI_Canvas *canvas;
} gX_Widget_Ctx_Select;

static gX_I0 radio_onclick_cb(gX_I0 *arg) {
    const gX_Widget_Ctx_Select *ctx = (const gX_Widget_Ctx_Select*)arg;
    if (!ctx || !ctx->statePtr) return;
    *ctx->statePtr = ctx->setValue;
    gX_GUI_Widget_RequestRefresh(ctx->canvas);
}

typedef struct gX_Widget_Ctx_Stepper {
    gX_I32 *intPtr;
    gX_F32 *floatPtr;
    gX_F32 step;
    gX_F32 min;
    gX_F32 max;
    gX_GUI_Canvas *canvas;
} gX_Widget_Ctx_Stepper;

static gX_I0 stepper_dec_cb(gX_I0 *arg) {
    const gX_Widget_Ctx_Stepper *ctx = (const gX_Widget_Ctx_Stepper*)arg;
    if (!ctx) return;
    if (ctx->floatPtr) { *ctx->floatPtr -= ctx->step; if (*ctx->floatPtr < ctx->min) *ctx->floatPtr = ctx->min; }
    if (ctx->intPtr) { *ctx->intPtr -= (gX_I32)ctx->step; if (*ctx->intPtr < (gX_I32)ctx->min) *ctx->intPtr = (gX_I32)ctx->min; }
    //opencode: rebuild the widget's canvas so the value display text updates —
    //   the "%s: %.2f" string is baked at build time and has no data binding
    gX_GUI_Widget_RequestRefresh(ctx->canvas);
}

static gX_I0 stepper_inc_cb(gX_I0 *arg) {
    const gX_Widget_Ctx_Stepper *ctx = (const gX_Widget_Ctx_Stepper*)arg;
    if (!ctx) return;
    if (ctx->floatPtr) { *ctx->floatPtr += ctx->step; if (*ctx->floatPtr > ctx->max) *ctx->floatPtr = ctx->max; }
    if (ctx->intPtr) { *ctx->intPtr += (gX_I32)ctx->step; if (*ctx->intPtr > (gX_I32)ctx->max) *ctx->intPtr = (gX_I32)ctx->max; }
    gX_GUI_Widget_RequestRefresh(ctx->canvas);
}

typedef struct gX_Widget_Ctx_Tab {
    gX_I32 *activeTab;
    gX_I32 tabIndex;
    gX_GUI_Canvas *canvas;
} gX_Widget_Ctx_Tab;

static gX_I0 tab_onclick_cb(gX_I0 *arg) {
    const gX_Widget_Ctx_Tab *ctx = (const gX_Widget_Ctx_Tab*)arg;
    if (!ctx || !ctx->activeTab) return;
    *ctx->activeTab = ctx->tabIndex;
    gX_GUI_Widget_RequestRefresh(ctx->canvas);
}

//opencode: shared by every scroll-list element (item / track / arrows). Only
//   the fields the element needs are populated at build time.
typedef struct gX_Widget_Ctx_Scroll {
    gX_I32 *selectedPtr;
    gX_I32 itemIndex;
    gX_I32 *scrollPtr;
    gX_I32 totalItems;
    gX_I32 visibleItems;
    gX_GUI_Canvas *canvas;
    gX_F32 trackY;
    gX_F32 trackH;
    gX_F32 thumbY;
    gX_F32 thumbH;
} gX_Widget_Ctx_Scroll;

//opencode: guard the inline context capacity — a larger widget context is
//   rejected by gX_GUI_OnClick_Context rather than being truncated.
_Static_assert(sizeof(gX_Widget_Ctx_Scroll) <= gX_GUI_ELEMENT_CALLBACK_CTX_MAX,
    "scroll widget context exceeds gX_GUI_ELEMENT_CALLBACK_CTX_MAX");

static gX_I0 scrollitem_onclick_cb(gX_I0 *arg) {
    const gX_Widget_Ctx_Scroll *ctx = (const gX_Widget_Ctx_Scroll*)arg;
    if (!ctx || !ctx->selectedPtr) return;
    *ctx->selectedPtr = ctx->itemIndex;
    //opencode: refresh the list's own canvas — selection highlight is baked
    //   at build time and needs a rebuild to show
    gX_GUI_Widget_RequestRefresh(ctx->canvas);
}

static gX_I0 scroll_up_cb(gX_I0 *arg) {
    const gX_Widget_Ctx_Scroll *ctx = (const gX_Widget_Ctx_Scroll*)arg;
    if (!ctx || !ctx->scrollPtr) return;
    if (*ctx->scrollPtr > 0) (*ctx->scrollPtr)--;
    gX_GUI_Widget_RequestRefresh(ctx->canvas);
}

static gX_I0 scroll_down_cb(gX_I0 *arg) {
    const gX_Widget_Ctx_Scroll *ctx = (const gX_Widget_Ctx_Scroll*)arg;
    gX_I32 maxScroll = 0;
    if (!ctx || !ctx->scrollPtr) return;
    maxScroll = ctx->totalItems - ctx->visibleItems;
    if (maxScroll < 0) maxScroll = 0;
    if (*ctx->scrollPtr < maxScroll) (*ctx->scrollPtr)++;
    gX_GUI_Widget_RequestRefresh(ctx->canvas);
}

//opencode: track click pages toward the click — above the thumb pages up,
//   below it pages down.
static gX_I0 scroll_track_cb(gX_I0 *arg) {
    const gX_Widget_Ctx_Scroll *ctx = (const gX_Widget_Ctx_Scroll*)arg;
    gX_F32v2 buf = {0, 0};
    gX_I1 hasBuf = false;
    if (!ctx || !ctx->scrollPtr) return;
    if (ctx->canvas) {
        hasBuf = gX_GUI_Canvas_ToBufferPoint(ctx->canvas, input.mouse.coord, &buf);
    }
    if (hasBuf) {
        gX_F32 center = ctx->thumbY + ctx->thumbH * 0.5f;
        if (buf.y < center) {
            *ctx->scrollPtr = gX_GUI_Scroll_PageStep(
                *ctx->scrollPtr, ctx->totalItems, ctx->visibleItems, -1);
        } else {
            *ctx->scrollPtr = gX_GUI_Scroll_PageStep(
                *ctx->scrollPtr, ctx->totalItems, ctx->visibleItems, 1);
        }
    } else {
        *ctx->scrollPtr = gX_GUI_Scroll_PageStep(
            *ctx->scrollPtr, ctx->totalItems, ctx->visibleItems, 1);
    }
    gX_GUI_Widget_RequestRefresh(ctx->canvas);
}

//=============================================================================
// Widget_Button
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_Button(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *text, gX_I32 targetState, gX_I32 *statePtr)
{
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_BUTTON;
        gX_GUI_Builder_RectGrid(b, gridX, gridY, gridW, gridH);
        gX_GUI_Builder_ColorV(b, gui.style.buttonBase);
        if (text) strncpy(b->currentElement->name, text, sizeof(b->currentElement->name) - 1);
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, text);
            //opencode: widget-scale type — buttons previously kept the 32px
            //   default, so compact action rows truncated ("Defau...");
            //   shrink-to-fit guards longer labels at the same scale.
            if (b->curText) {
                b->curText->color = gui.style.buttonText;
                b->curText->fontSize = gui.style.smallSize;
                b->curText->autoShrink = true;
            }
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;

        gX_GUI_Element_MakeClickable(b->currentElement);

        //hover color effect
        gX_GUI_Element_Add_Color(b->currentElement);
        gX_GUI_Element_Color *hc = gX_GUI_Element_Color_Get(b->currentElement, 0);
        if (hc) {
            hc->rectColor = true;
            hc->modeMultiply = true;
            hc->factor = (gX_F32v4){gui.style.hoverMultiplier, gui.style.hoverMultiplier, gui.style.hoverMultiplier, 1.0f};
            hc->colorOriginal = b->currentElement->color;
            hc->Tseconds = gui.style.hoverSeconds;
            hc->execute.hovered = true;
        }

        //attribute + data for click state binding
        gX_GUI_Element_Add_Attribute(b->currentElement);
        gX_GUI_Element_Attribute *attr = gX_GUI_Element_Attribute_Get(b->currentElement, 0);
        if (attr) attr->value = (gX_F32v4){(gX_F32)targetState, 0.0f, 0.0f, 0.0f};
        gX_GUI_Element_Add_Data(b->currentElement);
        gX_GUI_Element_Data *data = gX_GUI_Element_Data_Get(b->currentElement, 0);
        if (data) {
            data->dst.ptr = statePtr;
            data->dst.datatype = gX_DATATYPE_I32;
            data->dst.write = true;
            data->execute.clickedLeft = true;
            data->execute.once = true;
            data->useDataOperator = true;
            data->op = gX_GUI_ELEMENT_FUNCTION_SET;
            data->src.attributeIndex = 0;
            data->src.attributeReadBeforeOp = true;
        }

        //sounds
        if (gX_Audio_SoundGet("buttonHover")) {
            gX_GUI_Element_Add_Sound(b->currentElement);
            gX_GUI_Element_Sound *snd = gX_GUI_Element_Sound_Get(b->currentElement, 0);
            if (snd) { snd->sound = gX_Audio_SoundGet("buttonHover"); snd->params.volume = gX_GUI_SOUND_DEFAULT_VOLUME;
                snd->params.pitch = gX_GUI_SOUND_DEFAULT_PITCH; snd->params.pan = gX_GUI_SOUND_DEFAULT_PAN;
                snd->execute.hovered = true; snd->execute.once = true; }
        }
        if (gX_Audio_SoundGet("buttonCancel")) {
            gX_GUI_Element_Add_Sound(b->currentElement);
            gX_GUI_Element_Sound *snd2 = gX_GUI_Element_Sound_Get(b->currentElement, 1);
            if (snd2) { snd2->sound = gX_Audio_SoundGet("buttonCancel"); snd2->params.volume = gX_GUI_SOUND_DEFAULT_VOLUME;
                snd2->params.pitch = gX_GUI_SOUND_DEFAULT_PITCH; snd2->params.pan = gX_GUI_SOUND_DEFAULT_PAN;
                snd2->execute.clickedLeft = true; snd2->execute.once = true; }
        }
    gX_GUI_Builder_Element_End(b);
}

//=============================================================================
// Widget_Back
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_Back(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY,
    gX_I32 targetState, gX_I32 *statePtr)
{
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    if (!b->currentElement) return; //opencode: guard against element heap exhaustion
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_BUTTON;
        gX_GUI_Builder_RectGrid(b, gridX, gridY, gui.style.backW, gui.style.backH);
        gX_GUI_Builder_ColorV(b, gui.style.backBase);
        strncpy(b->currentElement->name, "back", sizeof(b->currentElement->name) - 1);
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, "back");
            if (b->curText) { b->curText->color = gui.style.backText; b->curText->fontSize = 22; }
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
        gX_GUI_Builder_Text_End(b);
        
        gX_GUI_Element_MakeClickable(b->currentElement);

        //hover
        gX_GUI_Element_Add_Color(b->currentElement);
        gX_GUI_Element_Color *hc = gX_GUI_Element_Color_Get(b->currentElement, 0);
        if (hc) { hc->rectColor = true; hc->modeMultiply = true;
            hc->factor = (gX_F32v4){gui.style.hoverMultiplier,gui.style.hoverMultiplier,gui.style.hoverMultiplier,1.0f};
            hc->colorOriginal = b->currentElement->color; hc->Tseconds = gui.style.hoverSeconds;
            hc->execute.hovered = true; }
        if (gX_Audio_SoundGet("buttonHover")) {
            gX_GUI_Element_Add_Sound(b->currentElement);
            gX_GUI_Element_Sound *snd = gX_GUI_Element_Sound_Get(b->currentElement, 0);
            if (snd) { snd->sound = gX_Audio_SoundGet("buttonHover"); snd->params.volume = gX_GUI_SOUND_DEFAULT_VOLUME;
                snd->params.pitch = gX_GUI_SOUND_DEFAULT_PITCH; snd->params.pan = gX_GUI_SOUND_DEFAULT_PAN;
                snd->execute.hovered = true; snd->execute.once = true; }
        }
        if (gX_Audio_SoundGet("buttonCancel")) {
            gX_GUI_Element_Add_Sound(b->currentElement);
            gX_GUI_Element_Sound *snd2 = gX_GUI_Element_Sound_Get(b->currentElement, 1);
            if (snd2) { snd2->sound = gX_Audio_SoundGet("buttonCancel"); snd2->params.volume = gX_GUI_SOUND_DEFAULT_VOLUME;
                snd2->params.pitch = gX_GUI_SOUND_DEFAULT_PITCH; snd2->params.pan = gX_GUI_SOUND_DEFAULT_PAN;
                snd2->execute.clickedLeft = true; snd2->execute.once = true; }
        }

        //click callback: sets *statePtr = targetState + trigger refresh/transition
        //opencode: owner-scoped context — lives in the element's data module,
        //   so a partial refresh on another canvas cannot alias it.
        {
            gX_Widget_Ctx_Back ctx = {targetState, statePtr, b->canvas};
            gX_GUI_OnClick_Context(b->currentElement, back_onclick_cb, &ctx, sizeof(ctx));
        }
    gX_GUI_Builder_Element_End(b);
}


//=============================================================================
// Widget_TextCard
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_TextCard(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *text)
{
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_PANEL;
        gX_GUI_Builder_RectGrid(b, gridX, gridY, gridW, gridH);
        gX_GUI_Builder_Color(b, 127, 127, 255, 255);
        b->currentElement->stateRendering.GRADIENT_ENABLE = true;
        gX_GUI_Element_Add_Color(b->currentElement);
        gX_GUI_Element_Color *gc1 = gX_GUI_Element_Color_Get(b->currentElement, 0);
        if (gc1) { gc1->gradientColor = true; gc1->color = (gX_Color){127,127,255,255}; }
        gX_GUI_Element_Add_Color(b->currentElement);
        gX_GUI_Element_Color *gc2 = gX_GUI_Element_Color_Get(b->currentElement, 1);
        if (gc2) { gc2->gradientColor = true; gc2->color = (gX_Color){63,63,127,255}; }
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, text);
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
            //opencode: the fixed 64px size was taller than the 0.6-grid card
            //   (layout pass); scale from the card height with the widget
            //   shrink guard for long labels.
            if (b->curText) {
                gX_F32 size = b->currentElement->rect.height * 0.75f;
                if (size > 64.0f) size = 64.0f;
                if (size < 14.0f) size = 14.0f;
                b->curText->fontSize = size;
                b->curText->spacing = 5;
                b->curText->autoShrink = true;
                b->curText->color = gX_LIGHTGRAY;
            }
        gX_GUI_Builder_Text_End(b);
    gX_GUI_Builder_Element_End(b);
}

//=============================================================================
// Widget_Slider
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_Slider(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *label, gX_F32 *value, gX_F32 min, gX_F32 max)
{
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_SLIDER;
        gX_GUI_Builder_RectGrid(b, gridX, gridY, gridW, gridH);
        //opencode: blueprint control accent — sliders previously used a
        //   neutral gray that broke the shared Settings accent hierarchy.
        gX_GUI_Builder_ColorV(b, gui.style.blueprint.latticeColor);
        gX_GUI_Element_Template_Slider(b->currentElement);
        gX_GUI_Element_Add_Attribute(b->currentElement);
        gX_GUI_Element_Attribute *attr = gX_GUI_Element_Attribute_Get(b->currentElement, 0);
        if (attr) attr->value = (gX_F32v4){*value, *value, 0, 0};
        gX_GUI_Element_Add_Data(b->currentElement);
        gX_GUI_Element_Data *data = gX_GUI_Element_Data_Get(b->currentElement, 0);
        if (data) {
            data->dst.ptr = value; data->dst.datatype = gX_DATATYPE_F32;
            data->dst.write = true; data->dst.read = false;
            data->execute.pressedLeft = true; data->execute.draggedLeft = true; data->execute.once = false;
            data->usePotentio = true; data->dst.readPotentio = true;
            data->potentio.min.x = b->currentElement->rect.x;
            data->potentio.max.x = b->currentElement->rect.x + b->currentElement->rect.width;
            data->potentio.min.y = min; data->potentio.max.y = max;
            data->potentio.reg.y = b->currentElement->rect.y + 0.5f * b->currentElement->rect.height;
            data->potentio.copyCursor = true; data->potentio.writeAfter = true;
            data->potentio.enableX = true; data->potentio.limit = true;
            data->executeAnyway = true;
            data->dst.textIndex = 0; data->dst.textUse = true; data->dst.textArraySize = 1;
            //build format string: "label: %.3f" or just "%.3f"
            //use strcpy/snprintf with %s to pass the literal "%.3f" — NOT as a format argument
            if (label && label[0]) {
                snprintf(data->format, gX_GUI_ELEMENT_FORMAT_MAX, "%s: %s", label, gX_GUI_BUILDER_WIDGET_SLIDER_FORMAT);
            } else {
                strcpy(data->format, gX_GUI_BUILDER_WIDGET_SLIDER_FORMAT);
            }
            //init reg.x so first executeAnyway frame produces TX = current value
            gX_F32 norm = (max > min) ? ((*value - min) / (max - min)) : 0.0f;
            data->potentio.reg.x = b->currentElement->rect.x + norm * b->currentElement->rect.width;
            data->potentio.useShape = true; data->potentio.shapeIndex = 0;
        }
        //circle knob — position from current value
        gX_GUI_Element_Add_Shape(b->currentElement);
        gX_GUI_Element_Shape *shp = gX_GUI_Element_Shape_Get(b->currentElement, 0);
        if (shp) { shp->type = gX_GUI_ELEMENT_SHAPE_CIRCLE;
            gX_F32 t = (max > min) ? ((*value - min) / (max - min)) : 0.0f;
            gX_F32 knobX = b->currentElement->rect.x + t * b->currentElement->rect.width;
            gX_F32 knobY = b->currentElement->rect.y + b->currentElement->rect.height * 0.5f;
            shp->v2[0] = (gX_F32v2){knobX, knobY};
            shp->scalar[0] = gX_GUI_BUILDER_WIDGET_KNOB_RADIUS; shp->color = gui.style.shared.accentGold; }
        gX_GUI_Builder_Text_Begin(b);
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
        gX_GUI_Builder_Text_End(b);
    gX_GUI_Builder_Element_End(b);
}

//=============================================================================
// Widget_TextureButton
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_TextureButton(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    gX_Texture *texture)
{
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_BUTTON;
        gX_GUI_Builder_RectGrid(b, gridX, gridY, gridW, gridH);
        gX_GUI_Builder_Color(b, 255, 255, 255, 255);
        b->currentElement->stateRendering.RECT_NO_DRAW = true;
        b->currentElement->stateRendering.RECT_DRAW_WIRE = true;
        //set stateRendering flags so the draw function uses texture->params
        b->currentElement->stateRendering.TEXTURE_CUSTOM_PARAMS = true;
        gX_GUI_Builder_Texture_Begin(b);
            gX_GUI_Builder_Texture_Set(b, texture);
            b->curTexture->params.dst = b->currentElement->rect;
        gX_GUI_Builder_Texture_End(b);
    gX_GUI_Builder_Element_End(b);
}

//=============================================================================
// Widget_CheckBox
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_CheckBox(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *text, gX_I1 *value)
{
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_CHECKBOX;
        gX_GUI_Builder_RectGrid(b, gridX, gridY, gridW, gridH);
        gX_GUI_Builder_Color(b, 127, 127, 127, 255);
        //opencode: "[X] label" / "[ ] label" checkmark indicator — baked at
        //   build time, rebuilt on click via the refresh callback below
        gX_I8 display[160] = {0};
        snprintf(display, sizeof(display), "[%s] %s",
            (value && *value) ? "X" : " ", text ? text : "");
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, display);
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
            //opencode: widget-scale type — the 32px text default truncates
            //   inside compact widget boxes (theme-lab audit).
            if (b->curText) { b->curText->fontSize = gui.style.smallSize; b->curText->autoShrink = true; }
        gX_GUI_Builder_Text_End(b);
        gX_GUI_Element_Template_CheckBox(b->currentElement);
        //opencode: owner-scoped callback — the old SET-operator binding never
        //   toggled the value (attribute was baked) and never refreshed.
        {
            gX_Widget_Ctx_Bool ctx = {value, b->canvas};
            gX_GUI_OnClick_Context(b->currentElement, checkbox_onclick_cb, &ctx, sizeof(ctx));
        }
    gX_GUI_Builder_Element_End(b);
}

//=============================================================================
// Widget_DropDown
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_DropDown(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *label, gX_I32 *value, gX_I32 maxValue)
{
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_DROPDOWN;
        gX_GUI_Builder_RectGrid(b, gridX, gridY, gridW, gridH);
        gX_GUI_Builder_Color(b, 127, 127, 127, 255);
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, label);
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
            //opencode: widget-scale type — the 32px text default truncates
            //   inside compact widget boxes (theme-lab audit).
            if (b->curText) { b->curText->fontSize = gui.style.smallSize; b->curText->autoShrink = true; }
        gX_GUI_Builder_Text_End(b);
        gX_GUI_Element_Template_DropDown(b->currentElement);
        //opencode: owner-scoped wrapping cycle callback — the old data-operator
        //   INC ignored maxValue entirely (unbounded cycling) and never refreshed.
        {
            gX_Widget_Ctx_Cycle ctx = {value, maxValue, b->canvas};
            gX_GUI_OnClick_Context(b->currentElement, dropdown_onclick_cb, &ctx, sizeof(ctx));
        }
    gX_GUI_Builder_Element_End(b);
}

//=============================================================================
// Widget_Radio
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_Radio(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *text, gX_I32 setValue, gX_I32 *statePtr)
{
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_RADIO;
        gX_GUI_Builder_RectGrid(b, gridX, gridY, gridW, gridH);
        //ai: UX pass — the fixed grey read as a disabled control and ignored
        //   the theme; mirror the toggle switch's on/off tokens.
        if (statePtr && *statePtr == setValue)
            gX_GUI_Builder_ColorV(b, gui.style.blueprint.accentColor);
        else
            gX_GUI_Builder_ColorV(b, gui.style.blueprint.latticeColor);
        //opencode: "( *) label" / "( ) label" selection indicator
        gX_I8 display[160] = {0};
        snprintf(display, sizeof(display), "(%s) %s",
            (statePtr && *statePtr == setValue) ? "*" : " ", text ? text : "");
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, display);
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
            //opencode: widget-scale type — the 32px text default truncates
            //   inside compact widget boxes (theme-lab audit).
            if (b->curText) { b->curText->fontSize = gui.style.smallSize; b->curText->autoShrink = true; }
        gX_GUI_Builder_Text_End(b);
        gX_GUI_Element_Template_Radio(b->currentElement);
        //opencode: owner-scoped callback — old SET operator never refreshed the
        //   selection highlight
        {
            gX_Widget_Ctx_Select ctx = {statePtr, setValue, b->canvas};
            gX_GUI_OnClick_Context(b->currentElement, radio_onclick_cb, &ctx, sizeof(ctx));
        }
    gX_GUI_Builder_Element_End(b);
}

//=============================================================================
// Widget_ToggleSwitch
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_ToggleSwitch(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *label, gX_I1 *value)
{
    if (!value) return;
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_TOGGLE;
        gX_GUI_Builder_RectGrid(b, gridX, gridY, gridW, gridH);
        //opencode: state-tinted track — muted blueprint reads OFF, bright
        //   blueprint accent reads ON at a glance.
        if (value && *value) gX_GUI_Builder_ColorV(b, gui.style.blueprint.accentColor);
        else gX_GUI_Builder_ColorV(b, gui.style.blueprint.latticeColor);

        // Build display text: "label: ON" or "label: OFF"
        gX_I8 display[128] = {0};
        snprintf(display, sizeof(display), "%s: %s", label ? label : "",
            (value && *value) ? "ON" : "OFF");

        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, display);
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
            //opencode: widget-scale type — the 32px text default truncates
            //   inside compact widget boxes (theme-lab audit).
            if (b->curText) { b->curText->fontSize = gui.style.smallSize; b->curText->autoShrink = true; }
        gX_GUI_Builder_Text_End(b);

        gX_GUI_Element_MakeClickable(b->currentElement);

        //hover color effect
        gX_GUI_Element_Add_Color(b->currentElement);
        gX_GUI_Element_Color *hc = gX_GUI_Element_Color_Get(b->currentElement, 0);
        if (hc) {
            hc->rectColor = true;
            hc->modeMultiply = true;
            hc->factor = (gX_F32v4){gX_GUI_HOVER_COLOR_MULTIPLIER, gX_GUI_HOVER_COLOR_MULTIPLIER, gX_GUI_HOVER_COLOR_MULTIPLIER, 1.0f};
            hc->colorOriginal = b->currentElement->color;
            hc->Tseconds = gX_GUI_HOVER_TRANSITION_SECONDS;
            hc->execute.hovered = true;
        }

        //sounds
        if (gX_Audio_SoundGet("buttonHover")) {
            gX_GUI_Element_Add_Sound(b->currentElement);
            gX_GUI_Element_Sound *snd = gX_GUI_Element_Sound_Get(b->currentElement, 0);
            if (snd) { snd->sound = gX_Audio_SoundGet("buttonHover"); snd->params.volume = gX_GUI_SOUND_DEFAULT_VOLUME;
                snd->params.pitch = gX_GUI_SOUND_DEFAULT_PITCH; snd->params.pan = gX_GUI_SOUND_DEFAULT_PAN;
                snd->execute.hovered = true; snd->execute.once = true; }
        }

        //click callback via the element's owner-scoped context
        {
            gX_Widget_Ctx_Bool ctx = {value, b->canvas};
            gX_GUI_OnClick_Context(b->currentElement, toggle_onclick_cb, &ctx, sizeof(ctx));
        }
    gX_GUI_Builder_Element_End(b);
}

//=============================================================================
// Widget_ColorPicker
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_ColorPicker(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY,
    gX_U8 *targetR, gX_U8 *targetG, gX_U8 *targetB,
    gX_ColorPicker *picker)
{
    gX_ColorPicker_Init(picker, targetR, targetG, targetB);
    gX_ColorPicker_Create(picker, b, gridX, gridY);
}

//=============================================================================
// Widget_ProgressBar
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_ProgressBar(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    gX_F32 *value, gX_F32 min, gX_F32 max, gX_Color fillColor, gX_I1 showPercent)
{
    gX_F32 range = (max > min) ? (max - min) : 1.0f;
    gX_F32 norm = (*value - min) / range;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    // --- track (background; label lives on the overlay below) ---
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_PROGRESS;
        gX_GUI_Builder_RectGrid(b, gridX, gridY, gridW, gridH);
        gX_GUI_Builder_Color(b, 63, 63, 63, 255);
        b->currentElement->stateRendering.RECT_DRAW_WIRE = false;
        //text label shows percentage
        gX_I8 label[64] = {0};
        if (showPercent) {
            snprintf(label, sizeof(label), "%d%%", (gX_I32)(norm * 100.0f));
        }
    gX_GUI_Builder_Element_End(b);

    // --- fill (foreground, sized by value) ---
    gX_F32 fillW = gridW * norm;
    if (fillW < gX_GUI_WIDGET_PROGRESS_MIN_SLIVER) fillW = gX_GUI_WIDGET_PROGRESS_MIN_SLIVER;

    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_PROGRESS;
        gX_GUI_Builder_RectGrid(b, gridX, gridY, fillW, gridH);
        gX_GUI_Builder_ColorV(b, fillColor);
        b->currentElement->stateRendering.RECT_NO_DRAW = false;
    gX_GUI_Builder_Element_End(b);

    //opencode: percent overlay ABOVE the fill — the track label painted
    //   underneath, so the fill edge swallowed its leading digit (audit).
    if (showPercent) {
        gX_GUI_Builder_Element_Begin(b);
        if (!b->currentElement) return;
        b->currentElement->type = gX_GUI_ELEMENT_TYPE_LABEL;
            gX_GUI_Builder_RectGrid(b, gridX, gridY, gridW, gridH);
            gX_GUI_Builder_Color(b, 0, 0, 0, 0);
            b->currentElement->stateRendering.RECT_NO_DRAW = true;
            gX_GUI_Builder_Text_Begin(b);
                gX_GUI_Builder_Text_Set(b, label);
                b->currentElement->stateRendering.TEXT_CENTER_X = true;
                b->currentElement->stateRendering.TEXT_CENTER_Y = true;
                if (b->curText) { b->curText->fontSize = 18; b->curText->color = gX_LIGHTGRAY; }
            gX_GUI_Builder_Text_End(b);
        gX_GUI_Builder_Element_End(b);
    }
}

//=============================================================================
// Widget_TextInput
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_TextInput(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *name, gX_I8 *textPtr, gX_U32 textCapacity, gX_U32 maxLength,
    const gX_I8 *placeholder, gX_I1 passwordMode,
    gX_I0 (*onCommit)(gX_I0 *userData), gX_I0 *onCommitUserData)
{
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_TEXTINPUT;
        gX_GUI_Builder_RectGrid(b, gridX, gridY, gridW, gridH);
        //opencode: textinput field background — dark box (solid fill so the
        //   interior reads the field color; the cursor/text sit on top)
        gX_GUI_Builder_Color(b, 30, 30, 50, 255);
        if (name) strncpy(b->currentElement->name, name, sizeof(b->currentElement->name) - 1);
        if (placeholder) strncpy(b->currentElement->description, placeholder,
            sizeof(b->currentElement->description) - 1);

        gX_GUI_Element_Add_TextInput(b->currentElement);
        gX_GUI_Element_TextInput *ti = gX_GUI_Element_TextInput_Get(b->currentElement, 0);
        if (ti) {
            if (maxLength > 0) gX_GUI_Element_TextInput_SetMaxLength(ti, maxLength);
            if (placeholder) gX_GUI_Element_TextInput_SetPlaceholder(ti, placeholder);
            if (passwordMode) gX_GUI_Element_TextInput_SetPassword(ti, true);
            if (textPtr) gX_GUI_Element_TextInput_SetText(ti, textPtr);
            if (onCommit) gX_GUI_Element_TextInput_SetOnCommit(ti, onCommit, onCommitUserData);
            //opencode: capacity-bounded sync — callers pass the full byte size
            //   of textPtr (0 disables syncing); never write past it (audit
            //   CRITICAL #1 fix).
            gX_GUI_Element_TextInput_SetExternalBuffer(ti, textPtr, textCapacity);
        }

        gX_GUI_Element_MakeClickable(b->currentElement);

        //hover color effect
        gX_GUI_Element_Add_Color(b->currentElement);
        gX_GUI_Element_Color *hc = gX_GUI_Element_Color_Get(b->currentElement, 0);
        if (hc) {
            hc->rectColor = true;
            hc->modeMultiply = true;
            hc->factor = (gX_F32v4){gui.style.hoverMultiplier, gui.style.hoverMultiplier, gui.style.hoverMultiplier, 1.0f};
            hc->colorOriginal = b->currentElement->color;
            hc->Tseconds = gui.style.hoverSeconds;
            hc->execute.hovered = true;
        }
    gX_GUI_Builder_Element_End(b);
}

//=============================================================================
// Widget_ScrollableList
//=============================================================================

//opencode: v0.6.x — the per-item callback pool (g_scrollPool) was removed;
//   item/track/arrow contexts now live in each element's data module. Only the
//   geometry registry remains: it feeds the non-callback input paths (wheel
//   hit-testing and thumb dragging) and is keyed by (canvas, scrollPtr) so it
//   is updated in place across the rebuilds a drag triggers.
#define SCROLL_LIST_MAX 16
static struct {
    gX_I32 *scrollPtr;
    gX_I32 *selectedPtr;
    gX_I32 totalItems;
    gX_I32 visibleItems;
    gX_Rectangle listRect;
    gX_Rectangle thumbRect;
    gX_GUI_Canvas *canvas;
    gX_I1 dragging;
} g_scrollLists[SCROLL_LIST_MAX];
static gX_U32 g_scrollListCount = 0;

//opencode: pure scroll math — headless-testable, no input or heap access.
gX_I32 gX_GUI_Scroll_Clamp(gX_I32 offset, gX_I32 totalItems, gX_I32 visibleItems) {
    gX_I32 maxScroll = totalItems - visibleItems;
    if (maxScroll < 0) maxScroll = 0;
    if (offset < 0) return 0;
    if (offset > maxScroll) return maxScroll;
    return offset;
}

gX_I32 gX_GUI_Scroll_WheelStep(gX_I32 offset, gX_I32 totalItems, gX_I32 visibleItems, gX_I32 wheelSteps) {
    return gX_GUI_Scroll_Clamp(offset - wheelSteps, totalItems, visibleItems);
}

gX_I32 gX_GUI_Scroll_PageStep(gX_I32 offset, gX_I32 totalItems, gX_I32 visibleItems, gX_I32 pages) {
    return gX_GUI_Scroll_Clamp(offset + pages * visibleItems, totalItems, visibleItems);
}

//opencode: thumb-drag mapping — buffer-space pointer Y onto the scroll
//   range, rounded and clamped. Pure so the drag curve is unit-testable.
gX_I32 gX_GUI_Scroll_DragStep(gX_F32 listY, gX_F32 listH, gX_F32 thumbH,
    gX_F32 mouseY, gX_I32 totalItems, gX_I32 visibleItems) {
    gX_F32 trackH = listH - thumbH;
    gX_F32 frac = 0.0f;
    gX_I32 maxScroll = totalItems - visibleItems;
    if (maxScroll <= 0) return 0;
    if (trackH <= 0.0f) return gX_GUI_Scroll_Clamp(0, totalItems, visibleItems);
    frac = (mouseY - listY) / trackH;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    return gX_GUI_Scroll_Clamp((gX_I32)(frac * (gX_F32)maxScroll + 0.5f),
        totalItems, visibleItems);
}

gX_I0 gX_GUI_Scroll_MoveSelection(gX_I32 *selected, gX_I32 *scroll,
    gX_I32 totalItems, gX_I32 visibleItems, gX_I32 delta) {
    gX_I32 ns = 0;
    if (!selected || !scroll || totalItems <= 0) return;
    if (visibleItems < 1) visibleItems = 1;
    ns = *selected + delta;
    if (ns < 0) ns = 0;
    if (ns > totalItems - 1) ns = totalItems - 1;
    *selected = ns;
    if (ns < *scroll) *scroll = ns;
    if (ns >= *scroll + visibleItems) *scroll = ns - visibleItems + 1;
    *scroll = gX_GUI_Scroll_Clamp(*scroll, totalItems, visibleItems);
}

//opencode: v0.6.x — drop the geometry registry entries when every canvas is
//   being rebuilt. Callback contexts need no reset (they are element-owned).
gX_I0 gX_GUI_Builder_Widget_ScrollList_ResetRegistry(gX_I0) {
    g_scrollListCount = 0;
}

gX_I0 gX_GUI_Builder_Widget_ScrollableList(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *items, gX_I32 itemCount, gX_I32 *selectedIndex,
    gX_I32 *scrollOffset)
{
    if (!items || itemCount <= 0 || !selectedIndex || !scrollOffset) return;

    gX_F32 totalW = gridW;
    gX_F32 scrollbarW = gX_GUI_WIDGET_SCROLLBAR_WIDTH;
    gX_F32 listW = totalW - scrollbarW;
    if (listW < 1.0f) { listW = totalW; scrollbarW = 0.0f; }

    gX_F32 itemH = gX_GUI_WIDGET_SCROLLLIST_ITEM_H;
    gX_I32 visibleItems = (gX_I32)(gridH / itemH);
    if (visibleItems < 1) visibleItems = 1;

    //clamp scroll offset
    gX_I32 maxScroll = itemCount - visibleItems;
    if (maxScroll < 0) maxScroll = 0;
    if (*scrollOffset > maxScroll) *scrollOffset = maxScroll;
    if (*scrollOffset < 0) *scrollOffset = 0;

    // --- list items — each gets its own pool entry ---
    for (gX_I32 i = 0; i < visibleItems; i++) {
        gX_I32 itemIdx = *scrollOffset + i;
        if (itemIdx >= itemCount) break;

        const gX_I8 *itemText = items;
        gX_I32 ci = itemIdx;
        while (ci > 0 && *itemText) { itemText += strlen(itemText) + 1; ci--; }
        if (!(*itemText)) itemText = "???";

        gX_F32 iy = gridY + (gX_F32)i * itemH;
        gX_I1 isSelected = (itemIdx == *selectedIndex);

            gX_GUI_Builder_Element_Begin(b);
                b->currentElement->type = gX_GUI_ELEMENT_TYPE_BUTTON;
                gX_GUI_Builder_RectGrid(b, gridX, iy, listW, itemH);
                if (isSelected) {
                    gX_GUI_Builder_ColorV(b, gui.style.selectedBg);
                } else {
                    gX_GUI_Builder_ColorV(b, gui.style.unselectedBg);
                }
                gX_GUI_Builder_Text_Begin(b);
                    gX_GUI_Builder_Text_Set(b, itemText);
                    //opencode: center the label — top-left text in a tall row
                    //   reads as wasted vertical space (theme-lab audit).
                    b->currentElement->stateRendering.TEXT_CENTER_X = true;
                    b->currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b->curText) { b->curText->fontSize = gui.style.smallSize; b->curText->autoShrink = true; b->curText->color = isSelected ? gui.style.selectedText : gui.style.unselectedText; }
                gX_GUI_Builder_Text_End(b);
            gX_GUI_Element_MakeClickable(b->currentElement);
            //opencode: owner-scoped context per item
            {
                gX_Widget_Ctx_Scroll ctx = {0};
                ctx.selectedPtr = selectedIndex;
                ctx.scrollPtr = scrollOffset;
                ctx.totalItems = itemCount;
                ctx.visibleItems = visibleItems;
                ctx.itemIndex = itemIdx;
                ctx.canvas = b->canvas;
                gX_GUI_OnClick_Context(b->currentElement, scrollitem_onclick_cb, &ctx, sizeof(ctx));
            }
        gX_GUI_Builder_Element_End(b);
    }

    // --- scrollbar ---
    if (scrollbarW > 0.1f && maxScroll > 0) {
        gX_F32 thumbH = (gX_F32)visibleItems / (gX_F32)itemCount * gridH;
        if (thumbH < 0.3f) thumbH = 0.3f;
        gX_F32 thumbY = gridY + ((gX_F32)(*scrollOffset) / (gX_F32)maxScroll) * (gridH - thumbH);
        gX_Rectangle trackRect = {0, 0, 0, 0};
        gX_Rectangle thumbRect = {0, 0, 0, 0};
        gX_I32 trackSlot = -1;

        //scrollbar track (click pages toward the click). The callback is
        //attached after the thumb is built so its context can carry the
        //buffer-space thumb rect; the element is re-seeked by slot because a
        //later element addition can reallocate the canvas element heap.
        gX_GUI_Builder_Element_Begin(b);
            b->currentElement->type = gX_GUI_ELEMENT_TYPE_PANEL;
            gX_GUI_Builder_RectGrid(b, gridX + listW, gridY, scrollbarW, gridH);
            gX_GUI_Builder_ColorV(b, gui.style.scrollbarTrack);
            if (b->currentElement) { trackSlot = b->elementIndex; trackRect = b->currentElement->rect; }
            gX_GUI_Element_MakeClickable(b->currentElement);
        gX_GUI_Builder_Element_End(b);

        //up arrow
        {
            gX_GUI_Builder_Element_Begin(b);
                b->currentElement->type = gX_GUI_ELEMENT_TYPE_BUTTON;
                gX_GUI_Builder_RectGrid(b, gridX + listW, gridY, scrollbarW, 0.4f);
                gX_GUI_Builder_ColorV(b, gui.style.scrollbarArrow);
                gX_GUI_Builder_Text_Begin(b);
                    gX_GUI_Builder_Text_Set(b, "^");
                    b->currentElement->stateRendering.TEXT_CENTER_X = true;
                    b->currentElement->stateRendering.TEXT_CENTER_Y = true;
                gX_GUI_Builder_Text_End(b);
                gX_GUI_Element_MakeClickable(b->currentElement);
                {
                    gX_Widget_Ctx_Scroll ctx = {0};
                    ctx.selectedPtr = selectedIndex;
                    ctx.scrollPtr = scrollOffset;
                    ctx.totalItems = itemCount;
                    ctx.visibleItems = visibleItems;
                    ctx.itemIndex = -1;
                    ctx.canvas = b->canvas;
                    gX_GUI_OnClick_Context(b->currentElement, scroll_up_cb, &ctx, sizeof(ctx));
                }
            gX_GUI_Builder_Element_End(b);
        }

        //thumb (dragged via the list registry — hit-tested by rect)
        gX_GUI_Builder_Element_Begin(b);
            b->currentElement->type = gX_GUI_ELEMENT_TYPE_PANEL;
            gX_GUI_Builder_RectGrid(b, gridX + listW, thumbY, scrollbarW, thumbH);
            gX_GUI_Builder_ColorV(b, gui.style.scrollbarThumb);
            if (b->currentElement) thumbRect = b->currentElement->rect;
        gX_GUI_Builder_Element_End(b);

        //down arrow
        {
            gX_GUI_Builder_Element_Begin(b);
                b->currentElement->type = gX_GUI_ELEMENT_TYPE_BUTTON;
                gX_GUI_Builder_RectGrid(b, gridX + listW, gridY + gridH - 0.4f, scrollbarW, 0.4f);
                gX_GUI_Builder_ColorV(b, gui.style.scrollbarArrow);
                gX_GUI_Builder_Text_Begin(b);
                    gX_GUI_Builder_Text_Set(b, "v");
                    b->currentElement->stateRendering.TEXT_CENTER_X = true;
                    b->currentElement->stateRendering.TEXT_CENTER_Y = true;
                gX_GUI_Builder_Text_End(b);
                gX_GUI_Element_MakeClickable(b->currentElement);
                {
                    gX_Widget_Ctx_Scroll ctx = {0};
                    ctx.selectedPtr = selectedIndex;
                    ctx.scrollPtr = scrollOffset;
                    ctx.totalItems = itemCount;
                    ctx.visibleItems = visibleItems;
                    ctx.itemIndex = -1;
                    ctx.canvas = b->canvas;
                    gX_GUI_OnClick_Context(b->currentElement, scroll_down_cb, &ctx, sizeof(ctx));
                }
            gX_GUI_Builder_Element_End(b);
        }
        //opencode: attach the track callback now that the thumb geometry is
        //   known. Element rects live in canvas buffer space; the click
        //   callback maps the mouse there too, so store raw rects.
        if (trackSlot >= 0) {
            gX_GUI_Element *track = gX_Heap_SeekPtr_Index(&(b->canvas->elements),
                (gX_U32)trackSlot);
            if (track) {
                gX_Widget_Ctx_Scroll ctx = {0};
                ctx.selectedPtr = selectedIndex;
                ctx.scrollPtr = scrollOffset;
                ctx.totalItems = itemCount;
                ctx.visibleItems = visibleItems;
                ctx.itemIndex = -2;
                ctx.canvas = b->canvas;
                ctx.trackY = trackRect.y;
                ctx.trackH = trackRect.height;
                ctx.thumbY = thumbRect.y;
                ctx.thumbH = thumbRect.height;
                gX_GUI_OnClick_Context(track, scroll_track_cb, &ctx, sizeof(ctx));
            }
        }
        if (trackRect.width > 0.0f && trackRect.height > 0.0f) {
            gX_Rectangle listRect = {trackRect.x - listW * (gX_F32)b->widthDiv,
                trackRect.y, listW * (gX_F32)b->widthDiv + trackRect.width,
                trackRect.height};
            gX_U32 li = 0;
            gX_I1 found = false;
            for (li = 0; li < g_scrollListCount; li++) {
                if (g_scrollLists[li].canvas == b->canvas
                    && g_scrollLists[li].scrollPtr == scrollOffset) {
                    found = true;
                    break;
                }
            }
            if (!found && g_scrollListCount < SCROLL_LIST_MAX) {
                li = g_scrollListCount++;
                g_scrollLists[li].dragging = false;
            }
            if (found || li < SCROLL_LIST_MAX) {
                g_scrollLists[li].scrollPtr = scrollOffset;
                g_scrollLists[li].selectedPtr = selectedIndex;
                g_scrollLists[li].totalItems = itemCount;
                g_scrollLists[li].visibleItems = visibleItems;
                g_scrollLists[li].listRect = listRect;
                g_scrollLists[li].thumbRect = thumbRect;
                g_scrollLists[li].canvas = b->canvas;
            }
        }
    }
}

//opencode: scroll-list hit test — mouse in buffer space inside the list rect.
static gX_I1 gX_ScrollList_Hit(const gX_GUI_Canvas *canvas, gX_Rectangle rect) {
    gX_F32v2 buf = input.mouse.coord;
    if (!canvas) return false;
    if (!gX_GUI_Widget_CanvasLive(canvas)) return false;
    if (!gX_GUI_Canvas_InputAllowed((gX_I32)(canvas - (gX_GUI_Canvas*)gui.canvases.data))) return false;
    if (canvas->buffer.loadingState.loaded) {
        if (!gX_GUI_Canvas_ToBufferPoint(canvas, input.mouse.coord, &buf)) return false;
    }
    return gX_Rectangle_Point_CheckCollision(rect, buf);
}

//opencode: wheel over the topmost list scrolls it (up decreases the offset).
gX_I1 gX_GUI_ScrollList_UpdateWheel(gX_I0) {
    gX_I32 steps = 0;
    if (input.mouse.scroll == 0.0f) return false;
    steps = (gX_I32)input.mouse.scroll;
    if (steps == 0) steps = input.mouse.scroll > 0.0f ? 1 : -1;
    for (gX_I32 li = (gX_I32)g_scrollListCount - 1; li >= 0; li--) {
        gX_I32 want = 0;
        if (!g_scrollLists[li].scrollPtr) continue;
        if (!gX_ScrollList_Hit(g_scrollLists[li].canvas, g_scrollLists[li].listRect)) continue;
        want = gX_GUI_Scroll_WheelStep(*g_scrollLists[li].scrollPtr,
            g_scrollLists[li].totalItems, g_scrollLists[li].visibleItems, steps);
        if (want == *g_scrollLists[li].scrollPtr) return true;
        *g_scrollLists[li].scrollPtr = want;
        gui.refresh = 1;
        if (gX_GUI_Widget_CanvasLive(g_scrollLists[li].canvas)) {
            g_scrollLists[li].canvas->refresh = 1;
        }
        return true;
    }
    return false;
}

//opencode: thumb drag — press inside the thumb starts it, release ends it,
//   motion maps the buffer-space Y onto the scroll range (clamped).
gX_I1 gX_GUI_ScrollList_UpdateDrag(gX_I0) {
    gX_I1 any = false;
    for (gX_U32 li = 0; li < g_scrollListCount; li++) {
        gX_GUI_Canvas *canvas = g_scrollLists[li].canvas;
        gX_F32v2 buf = input.mouse.coord;
        gX_I32 want = 0;
        gX_I32 maxScroll = 0;
        if (!g_scrollLists[li].scrollPtr) continue;
        if (!gX_GUI_Widget_CanvasLive(canvas)) {
            g_scrollLists[li].dragging = false;
            continue;
        }
        maxScroll = g_scrollLists[li].totalItems - g_scrollLists[li].visibleItems;
        if (maxScroll <= 0) {
            g_scrollLists[li].dragging = false;
            continue;
        }
        if (canvas->buffer.loadingState.loaded) {
            if (!gX_GUI_Canvas_ToBufferPoint(canvas, input.mouse.coord, &buf)) {
                if (!input.mouse.leftButton.DOWN) g_scrollLists[li].dragging = false;
                continue;
            }
        }
        if (input.mouse.leftButton.PRESSED
            && gX_Rectangle_Point_CheckCollision(g_scrollLists[li].thumbRect, buf)) {
            g_scrollLists[li].dragging = true;
        }
        if (!input.mouse.leftButton.DOWN) {
            if (g_scrollLists[li].dragging) any = true;
            g_scrollLists[li].dragging = false;
            continue;
        }
        if (!g_scrollLists[li].dragging) continue;
        want = gX_GUI_Scroll_DragStep(g_scrollLists[li].listRect.y,
            g_scrollLists[li].listRect.height, g_scrollLists[li].thumbRect.height,
            buf.y, g_scrollLists[li].totalItems, g_scrollLists[li].visibleItems);
        if (want == *g_scrollLists[li].scrollPtr) {
            any = true;
            continue;
        }
        *g_scrollLists[li].scrollPtr = gX_GUI_Scroll_Clamp(want,
            g_scrollLists[li].totalItems, g_scrollLists[li].visibleItems);
        gui.refresh = 1;
        if (gX_GUI_Widget_CanvasLive(canvas)) canvas->refresh = 1;
        any = true;
    }
    return any;
}

//opencode: owner-scoped context of the scroll item owning this element, if any.
static gX_Widget_Ctx_Scroll *gX_GUI_ScrollList_FocusedItem(const gX_GUI_Element *element) {
    if (!element || !element->datas.data) return NULL;
    for (gX_U32 i = 0; i < element->datas.amount; i++) {
        gX_GUI_Element_Data *d = &((gX_GUI_Element_Data*)element->datas.data)[i];
        if (!d->isFunctionPtr || d->funcPtr != scrollitem_onclick_cb) continue;
        if (!d->usesCallbackContext) continue;
        return (gX_Widget_Ctx_Scroll*)d->callbackCtx.bytes;
    }
    return NULL;
}

//opencode: keyboard nav on the focused scroll item — arrows move one,
//   PgUp/PgDn page, Home/End jump. Consumes the key so focus never leaves
//   the list mid-navigation.
gX_I1 gX_GUI_ScrollList_HandleNavKeys(gX_I1 up, gX_I1 down, gX_I1 pgup,
    gX_I1 pgdn, gX_I1 home, gX_I1 end) {
    gX_GUI_Canvas *canvas = NULL;
    gX_GUI_Element *fe = NULL;
    gX_Widget_Ctx_Scroll *ctx = NULL;
    gX_I32 oldSel = 0;
    gX_I32 oldScroll = 0;
    if (!up && !down && !pgup && !pgdn && !home && !end) return false;
    if (gui.focusCanvasID < 0 || gui.focusElementID < 0) return false;
    if ((gX_U32)gui.focusCanvasID >= gui.canvases.amount) return false;
    canvas = &((gX_GUI_Canvas*)gui.canvases.data)[gui.focusCanvasID];
    if (!canvas || (gX_U32)gui.focusElementID >= canvas->elements.amount) return false;
    fe = &((gX_GUI_Element*)canvas->elements.data)[gui.focusElementID];
    ctx = gX_GUI_ScrollList_FocusedItem(fe);
    if (!ctx || !ctx->selectedPtr || !ctx->scrollPtr) return false;
    oldSel = *ctx->selectedPtr;
    oldScroll = *ctx->scrollPtr;
    if (up) gX_GUI_Scroll_MoveSelection(ctx->selectedPtr,
        ctx->scrollPtr, ctx->totalItems, ctx->visibleItems, -1);
    if (down) gX_GUI_Scroll_MoveSelection(ctx->selectedPtr,
        ctx->scrollPtr, ctx->totalItems, ctx->visibleItems, 1);
    if (pgup) gX_GUI_Scroll_MoveSelection(ctx->selectedPtr,
        ctx->scrollPtr, ctx->totalItems, ctx->visibleItems, -ctx->visibleItems);
    if (pgdn) gX_GUI_Scroll_MoveSelection(ctx->selectedPtr,
        ctx->scrollPtr, ctx->totalItems, ctx->visibleItems, ctx->visibleItems);
    if (home) {
        *ctx->selectedPtr = 0;
        *ctx->scrollPtr = gX_GUI_Scroll_Clamp(0, ctx->totalItems, ctx->visibleItems);
    }
    if (end) {
        *ctx->selectedPtr = ctx->totalItems - 1;
        if (*ctx->selectedPtr < 0) *ctx->selectedPtr = 0;
        *ctx->scrollPtr = gX_GUI_Scroll_Clamp(
            *ctx->selectedPtr - ctx->visibleItems + 1,
            ctx->totalItems, ctx->visibleItems);
    }
    if (*ctx->selectedPtr != oldSel || *ctx->scrollPtr != oldScroll) {
        gui.refresh = 1;
        if (gX_GUI_Widget_CanvasLive(ctx->canvas)) {
            ctx->canvas->refresh = 1;
        }
    }
    return true;
}

//=============================================================================
// Widget_Stepper
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_Stepper(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *label, gX_I0 *valuePtr, gX_I32 datatype,
    gX_F32 step, gX_F32 min, gX_F32 max)
{
    if (!valuePtr) return;

    //opencode: one owner-scoped context shared (copied) by both buttons — the
    //   dec/inc callbacks each get their own copy in the element data module.
    gX_Widget_Ctx_Stepper stepCtx = {0};
    stepCtx.floatPtr = (datatype == gX_DATATYPE_F32) ? (gX_F32*)valuePtr : 0;
    stepCtx.intPtr   = (datatype != gX_DATATYPE_F32) ? (gX_I32*)valuePtr : 0;
    stepCtx.step = step;
    stepCtx.min  = min;
    stepCtx.max  = max;
    stepCtx.canvas = b->canvas;

    //display text
    gX_I8 display[64] = {0};
    gX_F32 curVal = (datatype == gX_DATATYPE_F32) ? *(gX_F32*)valuePtr : (gX_F32)(*(gX_I32*)valuePtr);
    if (datatype == gX_DATATYPE_F32) {
        snprintf(display, sizeof(display), "%s: %.2f", label ? label : "", curVal);
    } else {
        snprintf(display, sizeof(display), "%s: %d", label ? label : "", (gX_I32)curVal);
    }

    gX_F32 btnW = gX_GUI_WIDGET_STEPPER_BTN_WIDTH;
    gX_F32 valW = gridW - 2.0f * btnW;
    if (valW < 1.0f) valW = 1.0f;

    //decrement button
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_BUTTON;
        gX_GUI_Builder_RectGrid(b, gridX, gridY, btnW, gridH);
        //opencode: stepper states stay in the blueprint family; sign glyphs
        //   provide direction instead of unrelated red/green surfaces.
        gX_GUI_Builder_ColorV(b, gui.style.blueprint.latticeColor);
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, "-");
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
            //opencode: sign glyphs use widget scale — the 32px default clipped
            //   in the 0.45-grid button rows (layout pass).
            if (b->curText) { b->curText->fontSize = gui.style.smallSize; b->curText->autoShrink = true; }
        gX_GUI_Builder_Text_End(b);
        gX_GUI_Element_MakeClickable(b->currentElement);
        b->currentElement->state.FOCUSABLE = true;
        gX_GUI_OnClick_Context(b->currentElement, stepper_dec_cb, &stepCtx, sizeof(stepCtx));
    gX_GUI_Builder_Element_End(b);

    //value display
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_LABEL;
        gX_GUI_Builder_RectGrid(b, gridX + btnW, gridY, valW, gridH);
        gX_GUI_Builder_ColorV(b, gui.style.panelBg);
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, display);
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
            //opencode: widget-scale type — the 32px text default truncates
            //   inside the value cell (theme-lab audit).
            if (b->curText) { b->curText->fontSize = gui.style.smallSize; b->curText->autoShrink = true; }
        gX_GUI_Builder_Text_End(b);
    gX_GUI_Builder_Element_End(b);

    //increment button
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_BUTTON;
        gX_GUI_Builder_RectGrid(b, gridX + btnW + valW, gridY, btnW, gridH);
        gX_GUI_Builder_ColorV(b, gui.style.blueprint.accentColor);
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, "+");
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
            if (b->curText) { b->curText->fontSize = gui.style.smallSize; b->curText->autoShrink = true; }
        gX_GUI_Builder_Text_End(b);
        gX_GUI_Element_MakeClickable(b->currentElement);
        b->currentElement->state.FOCUSABLE = true;
        gX_GUI_OnClick_Context(b->currentElement, stepper_inc_cb, &stepCtx, sizeof(stepCtx));
    gX_GUI_Builder_Element_End(b);
}

//=============================================================================
// Widget_TabPanel
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_TabPanel(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 **tabLabels, gX_I32 tabCount, gX_I32 *activeTab)
{
    if (!tabLabels || tabCount <= 0 || !activeTab) return;
    if (*activeTab < 0) *activeTab = 0;
    if (*activeTab >= tabCount) *activeTab = tabCount - 1;

    //opencode: honor narrow panels — the 1.0 floor overflowed past gridW
    //   (3 tabs always spanned 3.0 units, bleeding into the next column).
    gX_F32 tabW = gridW / (gX_F32)tabCount;
    if (tabW < 0.6f) tabW = 0.6f;

    // --- tab buttons ---
    for (gX_I32 i = 0; i < tabCount; i++) {
        gX_F32 tx = gridX + (gX_F32)i * tabW;
        gX_I1 isActive = (i == *activeTab);

        gX_GUI_Builder_Element_Begin(b);
            b->currentElement->type = gX_GUI_ELEMENT_TYPE_BUTTON;
            gX_GUI_Builder_RectGrid(b, tx, gridY, tabW - gui.style.tabGutter, gui.style.tabBarH);
            if (isActive) {
                gX_GUI_Builder_ColorV(b, gui.style.selectedBg);
            } else {
                gX_GUI_Builder_ColorV(b, gui.style.unselectedBg);
            }
            gX_GUI_Builder_Text_Begin(b);
                gX_GUI_Builder_Text_Set(b, tabLabels[i] ? tabLabels[i] : "?");
                b->currentElement->stateRendering.TEXT_CENTER_X = true;
                b->currentElement->stateRendering.TEXT_CENTER_Y = true;
                //opencode: 11px tabs — 0.8-unit buttons fit ~5 smallSize chars
                //   (theme-lab audit: design/tokens/export ellipsized at 14).
                if (b->curText) { b->curText->fontSize = 11; }
            gX_GUI_Builder_Text_End(b);
            //opencode: owner-scoped callback — old SET operator never refreshed
            //   the highlight/content area
            gX_GUI_Element_MakeClickable(b->currentElement);
            {
                gX_Widget_Ctx_Tab ctx = {activeTab, i, b->canvas};
                gX_GUI_OnClick_Context(b->currentElement, tab_onclick_cb, &ctx, sizeof(ctx));
            }
        gX_GUI_Builder_Element_End(b);
    }

    // --- content area (placeholder rect below tabs) ---
    gX_F32 contentY = gridY + gui.style.tabBarH + 0.2f;
    gX_F32 contentH = gridH - gui.style.tabBarH - 0.2f;
    if (contentH > 0.0f) {
        gX_GUI_Builder_Element_Begin(b);
            b->currentElement->type = gX_GUI_ELEMENT_TYPE_PANEL;
            gX_GUI_Builder_RectGrid(b, gridX, contentY, gridW, contentH);
            gX_GUI_Builder_ColorV(b, gui.style.scrollbarTrack);
            //text showing active tab name
            gX_GUI_Builder_Text_Begin(b);
                const gX_I8 *activeLabel = tabLabels[*activeTab] ? tabLabels[*activeTab] : "";
                gX_I8 contentText[128] = {0};
                snprintf(contentText, sizeof(contentText), "[%s content]", activeLabel);
                gX_GUI_Builder_Text_Set(b, contentText);
                b->currentElement->stateRendering.TEXT_CENTER_X = true;
                b->currentElement->stateRendering.TEXT_CENTER_Y = true;
                //opencode: widget-scale type (theme-lab audit).
                if (b->curText) { b->curText->fontSize = gui.style.smallSize; b->curText->autoShrink = true; }
            gX_GUI_Builder_Text_End(b);
        gX_GUI_Builder_Element_End(b);
    }
}

//opencode: section divider + header label — uses gui.style for colors and sizes.
//   Draws a thin horizontal rule at (gx, gy, gw, dividerH), then a label below it.
//   Returns gy + label offset, for chaining controls directly after the section.
gX_F32 gX_GUI_Builder_Widget_Section(gX_GUI_Builder *b,
    gX_F32 gx, gX_F32 gy, gX_F32 gw,
    const gX_I8 *label, gX_F32 labelSize)
{
    gX_F32 dividerH = gui.style.sectionDividerH;
    gX_F32 labelH = labelSize / (gX_F32)b->heightDiv * 2.0f + 0.1f;

    // Divider line
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return gy;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_PANEL;
        gX_GUI_Builder_RectGrid(b, gx, gy, gw, dividerH);
        gX_GUI_Builder_ColorV(b, gui.style.sectionHeader);
    gX_GUI_Builder_Element_End(b);

    // Label text
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return gy;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_LABEL;
        gX_GUI_Builder_RectGrid(b, gx, gy + dividerH + 0.05f, gw, labelH);
        gX_GUI_Builder_Color(b, 0, 0, 0, 0);
        b->currentElement->stateRendering.RECT_NO_DRAW = true;
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, label);
            if (b->curText) {
                b->curText->fontSize = labelSize;
                b->curText->color = gui.style.sectionHeader;
            }
        gX_GUI_Builder_Text_End(b);
    gX_GUI_Builder_Element_End(b);

    return gy + dividerH + 0.05f + labelH + 0.1f;
}

//opencode: spinner / loading indicator — pulsing circle with infinite oscillation.
//   The circle pulses between full color and 30% alpha to indicate activity.
gX_I0 gX_GUI_Builder_Widget_Spinner(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    gX_Color color)
{
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_SPINNER;
        gX_GUI_Builder_RectGrid(b, gridX, gridY, gridW, gridH);
        gX_GUI_Builder_ColorV(b, color);
        b->currentElement->stateRendering.RECT_NO_DRAW = true;

        gX_F32 cx = b->currentElement->rect.x + b->currentElement->rect.width * 0.5f;
        gX_F32 cy = b->currentElement->rect.y + b->currentElement->rect.height * 0.5f;
        gX_F32 radius = (b->currentElement->rect.width < b->currentElement->rect.height)
            ? b->currentElement->rect.width * 0.3f
            : b->currentElement->rect.height * 0.3f;

        // Filled circle shape at center
        gX_GUI_Builder_Shape_Begin(b, gX_GUI_ELEMENT_SHAPE_CIRCLE);
            if (b->curShape) {
                b->curShape->v2[0] = (gX_F32v2){cx, cy};
                b->curShape->scalar[0] = radius;
                b->curShape->color = color;
                b->currentElement->stateRendering.SHAPE_PARENTCOLOR = true;
            }
        gX_GUI_Builder_Shape_End(b);

        // Infinite oscillating pulse animation on element color
        gX_GUI_Element_Add_Animation(b->currentElement);
        gX_GUI_Element_Animation *anim = gX_GUI_Element_Animation_Get(b->currentElement, 0);
        if (anim) {
            anim->executeAnyway = true;
            anim->infinite = true;
            anim->oscillate = true;
            anim->doColor = true;
            anim->duration = 0.8f;
            anim->easing = gX_EASE_SINE_IN_OUT;
            anim->start = (gX_F32v4){(gX_F32)color.r, (gX_F32)color.g, (gX_F32)color.b, (gX_F32)color.a};
            anim->end   = (gX_F32v4){(gX_F32)color.r, (gX_F32)color.g, (gX_F32)color.b, (gX_F32)color.a * 0.3f};
        }
    gX_GUI_Builder_Element_End(b);
}

//=============================================================================
// Reusable button sub-effects (generic)
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_AddHoverColor(gX_GUI_Builder *b, gX_Color hover, gX_F32 factor) {
    gX_GUI_Element_Add_Color(b->currentElement);
    gX_GUI_Element_Color *hc = gX_GUI_Element_Color_Get(b->currentElement, 0);
    if (hc) {
        hc->rectColor = true;
        hc->color = hover;
        hc->colorOriginal = b->currentElement->color;
        hc->Tseconds = gui.style.hoverSeconds;
        hc->execute.hovered = true;
        if (factor != 1.0f) { hc->modeMultiply = true; hc->factor = (gX_F32v4){factor,factor,factor,1.0f}; }
    }
}

gX_I0 gX_GUI_Builder_Widget_AddHoverClickSounds(gX_GUI_Builder *b) {
    if (gX_Audio_SoundGet("buttonHover")) {
        gX_GUI_Element_Add_Sound(b->currentElement);
        gX_GUI_Element_Sound *s = gX_GUI_Element_Sound_Get(b->currentElement, 0);
        if (s) { s->sound = gX_Audio_SoundGet("buttonHover"); s->params.volume = gX_GUI_SOUND_DEFAULT_VOLUME;
            s->params.pitch = gX_GUI_SOUND_DEFAULT_PITCH; s->params.pan = gX_GUI_SOUND_DEFAULT_PAN;
            s->execute.hovered = true; s->execute.once = true; }
    }
    if (gX_Audio_SoundGet("buttonCancel")) {
        gX_GUI_Element_Add_Sound(b->currentElement);
        gX_GUI_Element_Sound *s2 = gX_GUI_Element_Sound_Get(b->currentElement, 1);
        if (s2) { s2->sound = gX_Audio_SoundGet("buttonCancel"); s2->params.volume = gX_GUI_SOUND_DEFAULT_VOLUME;
            s2->params.pitch = gX_GUI_SOUND_DEFAULT_PITCH; s2->params.pan = gX_GUI_SOUND_DEFAULT_PAN;
            s2->execute.clickedLeft = true; s2->execute.once = true; }
    }
}

gX_I0 gX_GUI_Builder_Widget_AddButtonAnimations(gX_GUI_Builder *b, gX_I32 target, gX_I32 *statePtr) {
    gX_GUI_Builder_Animation_HoverScale(b, gui.style.hoverScale);
    gX_GUI_Builder_Animation_StateEntrance(b, target, statePtr, gui.style.hoverSeconds);
}

//=============================================================================
// High-level Widget_TextButton — complete interactive text button
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_TextButton(gX_GUI_Builder *b,
    gX_F32 gx, gX_F32 gy, gX_F32 gw, gX_F32 gh,
    const gX_I8 *text, gX_Color baseColor, gX_F32 hoverFactor,
    gX_I32 targetState, gX_I32 *statePtr,
    gX_GUI_ClickCallback onClick, gX_I0 *onClickArg)
{
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_BUTTON;
        gX_GUI_Builder_RectGrid(b, gx, gy, gw, gh);
        gX_GUI_Builder_Color(b, baseColor.r, baseColor.g, baseColor.b, baseColor.a);
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, text);
            //opencode: same widget-scale type and shrink guard as Widget_Button.
            if (b->curText) {
                b->curText->color = gX_WHITE;
                b->curText->fontSize = gui.style.smallSize;
                b->curText->autoShrink = true;
            }
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
        gX_GUI_Builder_Text_End(b);
        gX_GUI_Element_Template_Button(b->currentElement);
        gX_GUI_Builder_Widget_AddHoverColor(b, (gX_Color){255,255,255,255}, hoverFactor);
        gX_GUI_Builder_Widget_AddHoverClickSounds(b);
        if (onClick) {
            gX_GUI_OnClick(b->currentElement, onClick, onClickArg);
        } else if (statePtr) {
            gX_Widget_Ctx_SetI32 ctx = {targetState, statePtr};
            gX_GUI_OnClick_Context(b->currentElement, set_i32_onclick_cb, &ctx, sizeof(ctx));
        }
        gX_GUI_Builder_Widget_AddButtonAnimations(b, targetState, statePtr);
    gX_GUI_Builder_Element_End(b);
}

//=============================================================================
// High-level Widget_TextureButtonEx — textured button with wireframe
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_TextureButtonEx(gX_GUI_Builder *b,
    gX_F32 gx, gX_F32 gy, gX_F32 gw, gX_F32 gh,
    gX_Texture *tex, gX_Color wireColor, gX_Color hoverColor,
    gX_I32 targetState, gX_I32 *statePtr,
    gX_GUI_ClickCallback onClick, gX_I0 *onClickArg)
{
    if (!tex || tex->id <= 0) return;
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_BUTTON;
        gX_GUI_Builder_RectGrid(b, gx, gy, gw, gh);
        gX_GUI_Builder_Color(b, wireColor.r, wireColor.g, wireColor.b, wireColor.a);
        b->currentElement->stateRendering.RECT_NO_DRAW = true;
        b->currentElement->stateRendering.RECT_DRAW_WIRE = true;
        gX_GUI_Builder_Texture_Begin(b);
            gX_GUI_Builder_Texture_Set(b, tex);
        gX_GUI_Builder_Texture_End(b);
        gX_GUI_Element_Template_Button(b->currentElement);
        gX_GUI_Builder_Widget_AddHoverColor(b, hoverColor, 1.0f);
        gX_GUI_Builder_Widget_AddHoverClickSounds(b);
        if (onClick) {
            gX_GUI_OnClick(b->currentElement, onClick, onClickArg);
        } else if (statePtr) {
            gX_Widget_Ctx_SetI32 ctx = {targetState, statePtr};
            gX_GUI_OnClick_Context(b->currentElement, set_i32_onclick_cb, &ctx, sizeof(ctx));
        }
        gX_GUI_Builder_Widget_AddButtonAnimations(b, targetState, statePtr);
    gX_GUI_Builder_Element_End(b);
}

//=============================================================================
// Widget_GradientPanel — full-grid gradient background
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_GradientPanel(gX_GUI_Builder *b,
    gX_Color topColor, gX_Color bottomColor)
{
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_PANEL;
        gX_GUI_Builder_RectGrid(b, 0, 0, 8, 8);
        gX_GUI_Builder_ColorV(b, topColor);
        b->currentElement->stateRendering.GRADIENT_ENABLE = true;
        b->currentElement->stateRendering.GRADIENT_STATE = 0;
        gX_GUI_Element_Add_Color(b->currentElement);
        gX_GUI_Element_Color *gcTop = gX_GUI_Element_Color_Get(b->currentElement, 0);
        if (gcTop) { gcTop->gradientColor = true; gcTop->color = topColor; }
        gX_GUI_Element_Add_Color(b->currentElement);
        gX_GUI_Element_Color *gcBot = gX_GUI_Element_Color_Get(b->currentElement, 1);
        if (gcBot) { gcBot->gradientColor = true; gcBot->color = bottomColor; }
    gX_GUI_Builder_Element_End(b);
}

//=============================================================================
// Widget_ActionButton — calls function pointer on click
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_ActionButton(gX_GUI_Builder *b,
    gX_F32 gx, gX_F32 gy, gX_F32 gw, gX_F32 gh,
    const gX_I8 *text, gX_I0 (*funcPtr)(gX_I0 *userData))
{
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    b->currentElement->type = gX_GUI_ELEMENT_TYPE_BUTTON;
        gX_GUI_Builder_RectGrid(b, gx, gy, gw, gh);
        gX_GUI_Builder_Color(b, gui.style.title2.actionBg.r,
            gui.style.title2.actionBg.g, gui.style.title2.actionBg.b,
            gui.style.title2.actionBg.a);
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, text);
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
        gX_GUI_Builder_Text_End(b);
        gX_GUI_Element_Template_Button(b->currentElement);
        gX_GUI_Builder_Widget_AddHoverColor(b, gui.style.title2.actionHover, 1.0f);
        gX_GUI_Builder_Widget_AddHoverClickSounds(b);
        gX_GUI_Element_Add_Data(b->currentElement);
        gX_GUI_Element_Data *fd = gX_GUI_Element_Data_Get(b->currentElement, 0);
        if (fd) { fd->isFunctionPtr = true; fd->funcPtr = funcPtr;
            fd->execute.clickedLeft = true; fd->execute.once = true; }
        gX_GUI_Builder_Widget_AddButtonAnimations(b, 0, 0);
    gX_GUI_Builder_Element_End(b);
}

//=============================================================================
// Widget_SetTooltip — set tooltip text on current element
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_SetTooltip(gX_GUI_Builder *b, const gX_I8 *tooltip)
{
    if (!b || !b->currentElement || !tooltip) return;
    strncpy(b->currentElement->description, tooltip,
        sizeof(b->currentElement->description) - 1);
    b->currentElement->description[sizeof(b->currentElement->description) - 1] = '\0';
}

//=============================================================================
// Widget_FormRow — label text + widget in a single grid row
//=============================================================================

gX_I0 gX_GUI_Builder_Widget_FormRow(gX_GUI_Builder *b,
    gX_F32 gx, gX_F32 gy, gX_F32 gw, gX_F32 gh,
    const gX_I8 *label, const gX_I8 *tooltip,
    gX_GUI_Builder_Widget_FormRow_Fn builderFn)
{
    gX_F32 labelW = gw * 0.35f;
    gX_F32 widgetX = gx + labelW;
    gX_F32 widgetW = gw - labelW;

    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
        gX_GUI_Builder_RectGrid(b, gx, gy, labelW, gh);
        gX_GUI_Builder_ColorV(b, gui.style.sectionHeader);
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, label);
            gX_GUI_Builder_Text_FontSize(b, gui.style.bodySize);
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
        gX_GUI_Builder_Text_End(b);
        if (tooltip) gX_GUI_Builder_Widget_SetTooltip(b, tooltip);
    gX_GUI_Builder_Element_End(b);

    if (builderFn) builderFn(b, gx, gy, labelW, widgetX, gy, widgetW, gh);
}
