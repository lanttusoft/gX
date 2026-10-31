/**********************************************************************************************
*
*   [gX: src/gui/templates/titlegx.c]
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


//opencode: v0.1.41 — gX title screen through the real GUI builder pipeline
//   (replaces the placeholder wrapper draws in scene/title.c). Buttons wire
//   through the respond OnClick system like every other template.
#include "template_header.h"

#include <stdlib.h>

#include "../builder.h"
#include "../gui.h"
#include "../canvas.h"
#include "../element/element.h"
#include "settings.h"

#include "../../common/log.h"
#include "../../resource/resource.h"
#include "../../rhi/rhi.h"

#include "../binding.h"

//opencode: v0.2.0-pre4 — exclusive sub-views (0 = main menu, 1 = save slots).
//   The builder branches on this state so exactly one view renders per build;
//   LOAD SAVE / BACK swap it via gX_Widget_OnClick_TitleSaves/TitleBack in
//   binding.c which also set canvas->refresh to trigger a rebuild.
//opencode: v0.6.6 — menu column geometry (3.4-wide buttons centered:
//   x = (8 - 3.4) / 2 = 2.3; was 2.8, +96px right of center).
//opencode: F3 — MENU_X retired: the column is anchor-centered now
//   (was x = (8 - 3.4) / 2 = 2.3; RectAnchored CENTER derives it).
#define TITLEGX_MENU_W  3.4f
#define TITLEGX_VIEW_MENU  0
#define TITLEGX_VIEW_SAVES 1
//opencode: v0.6.5 — EXTRAS (in-app tools) + CREDITS sub-views.
#define TITLEGX_VIEW_EXTRAS 2
#define TITLEGX_VIEW_CREDITS 3
#define TITLEGX_VIEW_SETTINGS 4
//ai: title demos hub view for the Battle and renderer showcase routes.
#define TITLEGX_VIEW_DEMOS 5

static gX_I32 titleView = TITLEGX_VIEW_MENU;

//opencode: v0.2.0-pre8 — menu density variants per frontend_iteration1.png:
//   FULL = continue/new/load/settings/demos/extras/quit,
//   PLAY = new game/load game/quit, DEMOS = demos/quit.
//   GX_MENU_VARIANT=play|demos selects; full is the default.
#define TITLEGX_VARIANT_FULL  0
#define TITLEGX_VARIANT_PLAY  1
#define TITLEGX_VARIANT_DEMOS 2

static gX_I32 titleVariant(void) {
    const gX_I8 *vEnv = getenv("GX_MENU_VARIANT");
    if (vEnv) {
        if (strcmp(vEnv, "play") == 0)  return TITLEGX_VARIANT_PLAY;
        if (strcmp(vEnv, "demos") == 0) return TITLEGX_VARIANT_DEMOS;
    }
    return TITLEGX_VARIANT_FULL;
}

gX_I0 gX_GUI_Template_TitleGX_SetView(gX_I32 view);
gX_I0 gX_GUI_Template_TitleGX_SetView(gX_I32 view) {
    if (view >= TITLEGX_VIEW_MENU && view <= TITLEGX_VIEW_DEMOS) {
        titleView = view;
        gui.focusFirstPending = true;
        gX_GUI_Trace_Event("title-view", view, -1, NULL);
    }
}

//opencode: title subviews return to the main menu through keyboard/gamepad Back.
gX_I1 gX_GUI_Template_TitleGX_Back(gX_I0)
{
    if (titleView == TITLEGX_VIEW_MENU) return false;
    gX_GUI_Template_TitleGX_SetView(TITLEGX_VIEW_MENU);
    gX_GUI_Focus_Clear();
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(0);
    if (canvas) canvas->refresh = true;
    return true;
}

//opencode: F3 — menu-button entrance cascade. Per-button fade (background +
//   label via two animation modules) with a durationWait stagger, latched by
//   the scene-entry transition. Gated on a live transition at build time:
//   sub-view rebuilds run with no transition, so they stay instant —
//   pre-hiding the buttons without a trigger would strand them invisible.
//   Skipped under reduced motion. Per-button canvas delays (phase-2
//   transitionDelay) can't stagger here: the whole menu shares canvas 0.
#define TITLEGX_ENTER_DUR 0.25f
#define TITLEGX_ENTER_STEP 0.06f
static gX_I0 titlegx_MenuButtonFx(gX_GUI_Builder *b, gX_U32 index) {
    gX_Color bg;
    gX_GUI_Element_Animation *abg = NULL;
    gX_GUI_Element_Animation *atx = NULL;
    gX_GUI_Element_Text *t = NULL;
    if (!b || !b->currentElement) return;
    gX_GUI_Builder_Widget_AddHoverColor(b, (gX_Color){120, 90, 20, 255}, 1.0f);
    if (gui.style.a11y.motionReduced) return;
    if (!gui.transition.execute && !gui.transition.executing) return;
    bg = b->currentElement->color;
    b->currentElement->color.a = 0;
    gX_GUI_Element_Add_Animation(b->currentElement);
    abg = gX_GUI_Element_Animation_Get(b->currentElement, 0);
    if (abg) {
        abg->readTransitionGUI = true;
        abg->doColor = true;
        abg->modeAdd = false;
        abg->start = (gX_F32v4){(gX_F32)bg.r, (gX_F32)bg.g, (gX_F32)bg.b, 0.0f};
        abg->end = (gX_F32v4){(gX_F32)bg.r, (gX_F32)bg.g, (gX_F32)bg.b, (gX_F32)bg.a};
        abg->duration = TITLEGX_ENTER_DUR;
        abg->durationWait = (gX_F32)index * TITLEGX_ENTER_STEP;
        abg->easing = gX_EASE_QUAD_OUT;
    }
    t = gX_GUI_Element_Text_Get(b->currentElement, 0);
    if (t) {
        gX_Color fg = t->color;
        t->color.a = 0;
        gX_GUI_Element_Add_Animation(b->currentElement);
        atx = gX_GUI_Element_Animation_Get(b->currentElement, 1);
        if (atx) {
            atx->readTransitionGUI = true;
            atx->doTextColor = true;
            atx->textIndex = 0;
            atx->modeAdd = false;
            atx->start = (gX_F32v4){(gX_F32)fg.r, (gX_F32)fg.g, (gX_F32)fg.b, 0.0f};
            atx->end = (gX_F32v4){(gX_F32)fg.r, (gX_F32)fg.g, (gX_F32)fg.b, (gX_F32)fg.a};
            atx->duration = TITLEGX_ENTER_DUR;
            atx->durationWait = (gX_F32)index * TITLEGX_ENTER_STEP;
            atx->easing = gX_EASE_QUAD_OUT;
        }
    }
}

//opencode: main-menu polish — clear the element default description because
//   the menu labels already explain their actions and the generic tooltip was
//   colliding with the following button row.
static gX_I0 titlegx_MenuClearTooltip(gX_GUI_Builder *b) {
    if (!b || !b->currentElement) return;
    b->currentElement->description[0] = '\0';
}

//opencode: main-menu branding — use the title font for labels containing X;
//   the shared UI font's X glyph is H-shaped in the small atlas.
static gX_I0 titlegx_MenuUseTitleFont(gX_GUI_Builder *b) {
    if (!b || !b->curText || !gui.style.titleFontLoaded) return;
    gX_GUI_Builder_Text_Font(b, &gui.style.titleFont);
}

//ai: title template owns Battle and renderer routes in the Demos view.
gX_I0 gX_GUI_Template_TitleGX(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_TitleGX(gX_I32 canvasindex) {
    //opencode: yield to the themlab gallery — it draws its own opaque
    //   full-canvas backdrop and intends to replace the scene; compositing
    //   the title over it interleaves two full UIs. Canvas-2 presence is
    //   the signal (auto-handles every close path, no flag lifecycle).
    {
        gX_GUI_Canvas *lab = gX_GUI_Canvas_FromIndex(2);
        if (lab && (lab->templateID == gX_GUI_TEMPLATE_THEMLAB
            || lab->templateID == gX_GUI_TEMPLATE_MATERIALSLAB
            || lab->templateID == gX_GUI_TEMPLATE_SHADERLAB)) return;
    }
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "TitleGX", "gX title screen", gX_GUI_TEMPLATE_TITLEGX);

    //opencode: headless audit hook — GX_GUI_TITLE_VIEW builds a title sub-view directly.
    {
        const gX_I8 *viewEnv = getenv("GX_GUI_TITLE_VIEW");
        if (viewEnv) {
            gX_I32 vv = atoi(viewEnv);
            titleView = (vv >= TITLEGX_VIEW_MENU && vv <= TITLEGX_VIEW_DEMOS) ? vv : TITLEGX_VIEW_MENU;
        }
    }

    //opencode: fullscreen vertical gradient — same colors as the old wrapper
    //   backdrop in scene/title.c ({40,0,60} -> {100,20,120})
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_Rect(&b, 0.0f, 0.0f, (gX_F32)b.width, (gX_F32)b.height);
        //opencode: opaque title gradient; the default 3D centerpiece was removed.
        gX_Color gTop = gui.style.title2.bgGradientTop, gBot = gui.style.title2.bgGradientBottom;
        gTop.a = 255;
        gBot.a = 255;
        gX_GUI_Builder_ColorV(&b, gTop);
        b.currentElement->stateRendering.GRADIENT_ENABLE = true;
        b.currentElement->stateRendering.GRADIENT_STATE = 0;
        gX_GUI_Element_Add_Color(b.currentElement);
        gX_GUI_Element_Color *top = gX_GUI_Element_Color_Get(b.currentElement, 0);
        if (top) { top->gradientColor = true; top->color = gTop; }
        gX_GUI_Element_Add_Color(b.currentElement);
        gX_GUI_Element_Color *bot = gX_GUI_Element_Color_Get(b.currentElement, 1);
        if (bot) { bot->gradientColor = true; bot->color = gBot; }
    gX_GUI_Builder_Element_End(&b);

    {
        const gX_I32 starCount = 48;
        gX_F64 t = gui.style.a11y.motionReduced ? 0.0 : maindata.timeElapsed;
        for (gX_I32 i = 0; i < starCount; i++) {
            gX_U32 h = (gX_U32)i * 2654435761u;
            gX_F32 x = 0.35f + (gX_F32)(h % 1000u) / 1000.0f * 7.3f;
            gX_F32 y = 0.55f + (gX_F32)((h / 1000u) % 1000u) / 1000.0f * 3.4f;
            gX_F32 size = 0.010f + (gX_F32)((h / 1000000u) % 100u) / 100.0f * 0.016f;
            gX_F32 twinkle = 0.5f + 0.5f * sinf((gX_F32)t * 1.6f + (gX_F32)i * 0.71f);
            gX_U8 alpha = (gX_U8)(45.0f + 85.0f * twinkle);
            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, x, y, size, size);
                gX_GUI_Builder_Color(&b, 232, 222, 255, alpha);
            gX_GUI_Builder_Element_End(&b);
        }
    }

    if (titleView == TITLEGX_VIEW_SETTINGS) {
        gX_GUI_Template_Settings(canvasindex);
        //opencode: the embedded settings template rewrites the canvas identity
        //   (its Builder_New sets templateID=SETTINGS); restore the title
        //   identity or a refresh rebuilds Settings forever and BACK is a
        //   dead end (caught by gui-interaction-check).
        gX_GUI_Canvas_Add_TemplateID(canvas, gX_GUI_TEMPLATE_TITLEGX);
        gX_GUI_Canvas_Add_Name(canvas, "TitleGX");
    } else if (titleView == TITLEGX_VIEW_SAVES) {
        //opencode: v0.2.0-pre4 — save-slot sub-screen (LOAD GAME target):
        //   centered panel over dimmed gradient; BACK returns to the menu view
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectGrid(&b, 0.5f, 0.5f, 7.0f, 7.0f);
            gX_GUI_Builder_ColorV(&b, gui.style.title2.savePanelBg);

            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 1.2f, 0.9f, 5.6f, 0.6f);
                gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
                b.currentElement->stateRendering.RECT_NO_DRAW = true;
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "LOAD SAVE");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    if (b.curText) { b.curText->fontSize = 20; b.curText->color = gui.style.title2.saveHeaderText; }
                gX_GUI_Builder_Text_End(&b);
            gX_GUI_Builder_Widget_SetTooltip(&b, "Browse save slots and pick one to load");
            gX_GUI_Builder_Element_End(&b);

            //opencode: slot 1 loads the GFX demo ("NEW GAME" contradicted the
            //   LOAD header); empties are DISABLED-inert — no click, no focus,
            //   dimmed label. Per-slot tooltips removed: they duplicated the
            //   label and the plates collided across the tight stack.
            struct { gX_F32 y; const gX_I8 *label; gX_I1 empty; } slots[3] = {
                { 1.8f, "SAVE 1 - DEMO", 0 },
                { 3.3f, "SAVE 2 - EMPTY", 1 },
                { 4.8f, "SAVE 3 - EMPTY", 1 },
            };
            for (gX_I32 si = 0; si < 3; si++) {
                gX_GUI_Builder_Element_Begin(&b);
                    gX_GUI_Builder_RectGrid(&b, 1.2f, slots[si].y, 5.6f, 1.2f);
                    gX_GUI_Builder_ColorV(&b, gui.style.title2.saveSlotBg);
                    gX_GUI_Element_Template_Button(b.currentElement);
                    if (slots[si].empty) b.currentElement->state.DISABLED = true;
                    gX_GUI_Builder_Text_Begin(&b);
                        gX_GUI_Builder_Text_Set(&b, slots[si].label);
                        //opencode: empty-label dim lives in the shared button
                        //   rule (DISABLED dims white labels engine-wide).
                        if (b.curText) {
                            b.curText->fontSize = 14;
                            b.curText->color = gui.style.title2.saveSlotText;
                        }
                    gX_GUI_Builder_Text_End(&b);
                    if (!slots[si].empty) gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_CallSwitch,
                        (gX_I0*)(gX_U64)(uintptr_t)gX_SCENE_RENDERER_GFX);
                gX_GUI_Builder_Element_End(&b);
            }

            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 3.0f, 6.4f, 2.0f, 0.55f);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.backBg);
                gX_GUI_Element_Template_Button(b.currentElement);
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "BACK");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) { b.curText->fontSize = 14; b.curText->color = gui.style.title2.backText; }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_TitleBack, 0);
            gX_GUI_Builder_Element_End(&b);

        gX_GUI_Builder_Element_End(&b);
    } else if (titleView == TITLEGX_VIEW_DEMOS) {
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectGrid(&b, 0.5f, 0.5f, 7.0f, 7.0f);
            gX_GUI_Builder_ColorV(&b, gui.style.title2.savePanelBg);

            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 1.2f, 0.9f, 5.6f, 0.6f);
                gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
                b.currentElement->stateRendering.RECT_NO_DRAW = true;
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "DEMOS");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    if (b.curText) {
                        b.curText->fontSize = 20;
                        b.curText->color = gui.style.title2.saveHeaderText;
                    }
                gX_GUI_Builder_Text_End(&b);
            gX_GUI_Builder_Element_End(&b);

            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 1.2f, 1.55f, 5.6f, 0.74f);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.saveSlotBg);
                gX_GUI_Element_Template_Button(b.currentElement);
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "BATTLE ARENA");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) {
                        b.curText->fontSize = 15;
                        b.curText->color = gui.style.title2.saveSlotText;
                    }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_CallSwitch,
                    (gX_I0*)(gX_U64)(uintptr_t)gX_SCENE_BATTLE);
            gX_GUI_Builder_Element_End(&b);

            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 1.2f, 2.37f, 5.6f, 0.74f);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.saveSlotBg);
                gX_GUI_Element_Template_Button(b.currentElement);
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "PHYSICS SANDBOX");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) {
                        b.curText->fontSize = 15;
                        b.curText->color = gui.style.title2.saveSlotText;
                    }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_CallSwitch,
                    (gX_I0*)(gX_U64)(uintptr_t)gX_SCENE_PHYSICS);
            gX_GUI_Builder_Element_End(&b);

            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 1.2f, 3.19f, 5.6f, 0.74f);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.saveSlotBg);
                gX_GUI_Element_Template_Button(b.currentElement);
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "GFX SHOWCASE");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) {
                        b.curText->fontSize = 15;
                        b.curText->color = gui.style.title2.saveSlotText;
                    }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_CallSwitch,
                    (gX_I0*)(gX_U64)(uintptr_t)gX_SCENE_RENDERER_GFX);
            gX_GUI_Builder_Element_End(&b);

            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 1.2f, 4.01f, 5.6f, 0.74f);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.saveSlotBg);
                gX_GUI_Element_Template_Button(b.currentElement);
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "BACKGROUND");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) {
                        b.curText->fontSize = 15;
                        b.curText->color = gui.style.title2.saveSlotText;
                    }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_CallSwitch,
                    (gX_I0*)(gX_U64)(uintptr_t)gX_SCENE_RENDERER_BACKGROUND);
            gX_GUI_Builder_Element_End(&b);

            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 1.2f, 4.83f, 5.6f, 0.74f);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.saveSlotBg);
                gX_GUI_Element_Template_Button(b.currentElement);
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "RAYMARCH");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) {
                        b.curText->fontSize = 15;
                        b.curText->color = gui.style.title2.saveSlotText;
                    }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_CallSwitch,
                    (gX_I0*)(gX_U64)(uintptr_t)gX_SCENE_RENDERER_RAYMARCH);
            gX_GUI_Builder_Element_End(&b);

            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 3.0f, 6.55f, 2.0f, 0.55f);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.backBg);
                gX_GUI_Element_Template_Button(b.currentElement);
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "BACK");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) {
                        b.curText->fontSize = 14;
                        b.curText->color = gui.style.title2.backText;
                    }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_TitleBack, 0);
            gX_GUI_Builder_Element_End(&b);

        gX_GUI_Builder_Element_End(&b);
    } else if (titleView == TITLEGX_VIEW_EXTRAS) {
        //opencode: v0.6.5 — EXTRAS: in-app tools (out-of-box editors).
        //   Each opens its tool over the title; BACK returns to the menu
        //   (tools stay open until X/Close — independent of title view).
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectGrid(&b, 0.5f, 0.5f, 7.0f, 7.0f);
            gX_GUI_Builder_ColorV(&b, gui.style.title2.savePanelBg);

            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 1.2f, 0.9f, 5.6f, 0.6f);
                gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
                b.currentElement->stateRendering.RECT_NO_DRAW = true;
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "EXTRAS - TOOLS");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    if (b.curText) { b.curText->fontSize = 20; b.curText->color = gui.style.title2.saveHeaderText; }
                gX_GUI_Builder_Text_End(&b);
            gX_GUI_Builder_Element_End(&b);

            //opencode: two-column tool grid (up to ten cards) — wavetable,
            //   textwriter and physics editors join; the old single column
            //   ran out of panel height at eight.
            struct { const gX_I8 *label; gX_I32 col; gX_I32 row; gX_I32 which; } tools[12] = {
                { "GUI EDITOR", 0, 0, 0 },
                { "THEME EDITOR", 1, 0, 1 },
                { "THEME LAB", 0, 1, 2 },
                { "GFX EDITOR", 1, 1, 3 },
                { "MATERIAL LAB", 0, 2, 4 },
                { "SHADER LAB", 1, 2, 5 },
                { "WAVETABLE EDITOR", 0, 3, 6 },
                { "TEXTWRITER EDITOR", 1, 3, 7 },
                { "PHYSICS EDITOR", 0, 4, 8 },
                { "RAIL EDITOR", 1, 4, 9 },
                { "BACKGROUND EDITOR", 0, 5, 10 },
                { "SKY EDITOR", 1, 5, 11 },  //opencode: 12th slot — grid full
            };
            for (gX_I32 ti = 0; ti < 12; ti++) {
                gX_F32 tx = (tools[ti].col == 0) ? 1.25f : 4.15f;
                gX_F32 ty = 1.55f + tools[ti].row * 0.86f;
                gX_GUI_Builder_Element_Begin(&b);
                    gX_GUI_Builder_RectGrid(&b, tx, ty, 2.6f, 0.74f);
                    gX_GUI_Builder_ColorV(&b, gui.style.title2.saveSlotBg);
                    gX_GUI_Element_Template_Button(b.currentElement);
                    gX_GUI_Builder_Text_Begin(&b);
                        gX_GUI_Builder_Text_Set(&b, tools[ti].label);
                        b.currentElement->stateRendering.TEXT_CENTER_X = true;
                        b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                        if (b.curText) { b.curText->fontSize = 14; b.curText->color = gui.style.title2.saveSlotText; }
                    gX_GUI_Builder_Text_End(&b);
                    if (tools[ti].which == 0) gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_OpenEditor, 0);
                    else if (tools[ti].which == 1) gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_OpenTheme, 0);
                    else if (tools[ti].which == 2) gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_OpenThemLab, 0);
                    else if (tools[ti].which == 3) gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_OpenGfx, 0);
                    else if (tools[ti].which == 4) gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_OpenMaterials, 0);
                    else if (tools[ti].which == 5) gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_OpenShaderLab, 0);
                    else if (tools[ti].which == 6) gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_OpenWavetable, 0);
                    else if (tools[ti].which == 7) gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_OpenTextwriter, 0);
                    else if (tools[ti].which == 8) gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_OpenPhysics, 0);
                    else if (tools[ti].which == 9) gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_OpenRail, 0);
                    else if (tools[ti].which == 10) gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_OpenBackground, 0);
                    else gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_OpenSky, 0);
                    //opencode: tool cards are self-describing; inline tooltips
                    //   collided with the next card in the tight stacks.
                gX_GUI_Builder_Element_End(&b);
            }

            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 3.0f, 6.85f, 2.0f, 0.42f);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.backBg);
                gX_GUI_Element_Template_Button(b.currentElement);
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "BACK");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) { b.curText->fontSize = 14; b.curText->color = gui.style.title2.backText; }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_TitleBack, 0);
            gX_GUI_Builder_Element_End(&b);

        gX_GUI_Builder_Element_End(&b);
    } else if (titleView == TITLEGX_VIEW_CREDITS) {
        //opencode: v0.6.5 — CREDITS (was EXTRAS tooltip text; now its own
        //   footer link bottom-right). Static panel + BACK.
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectGrid(&b, 0.5f, 0.5f, 7.0f, 7.0f);
            gX_GUI_Builder_ColorV(&b, gui.style.title2.savePanelBg);

            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 1.2f, 0.9f, 5.6f, 0.6f);
                gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
                b.currentElement->stateRendering.RECT_NO_DRAW = true;
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "CREDITS");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    if (b.curText) { b.curText->fontSize = 20; b.curText->color = gui.style.title2.saveHeaderText; }
                gX_GUI_Builder_Text_End(&b);
            gX_GUI_Builder_Element_End(&b);

            gX_GUI_Builder_Element_Begin(&b);
                //opencode: center the credit block between its header and
                //   BACK instead of leaving the modal's middle empty.
                gX_GUI_Builder_RectGrid(&b, 1.2f, 2.95f, 5.6f, 2.4f);
                gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
                b.currentElement->stateRendering.RECT_NO_DRAW = true;
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "gX - a game engine\n\nlanttu + opencode\n\nraylib 6.0  Dear ImGui\nKenney fonts");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    if (b.curText) { b.curText->fontSize = 14; b.curText->color = gui.style.title2.saveSlotText; }
                gX_GUI_Builder_Text_End(&b);
            gX_GUI_Builder_Element_End(&b);

            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 3.0f, 6.4f, 2.0f, 0.55f);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.backBg);
                gX_GUI_Element_Template_Button(b.currentElement);
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, "BACK");
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) { b.curText->fontSize = 14; b.curText->color = gui.style.title2.backText; }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_TitleBack, 0);
            gX_GUI_Builder_Element_End(&b);

        gX_GUI_Builder_Element_End(&b);
    } else {
        // wordmark
        gX_Texture *logo = gX_Resource_LoadTexture("texture/gx_title.png");
        gX_F32 logoW = 2.2f;
        gX_F32 logoH = logo && logo->width > 0
            ? logoW * (gX_F32)logo->height / (gX_F32)logo->width : logoW;
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectAnchored(&b, logoW, logoH,
                GX_GUI_ANCHOR_CENTER, GX_GUI_ANCHOR_START, 0.0f, 1.10f);
            gX_F32 logoPulse = gui.style.a11y.motionReduced ? 1.0f
                : 0.86f + 0.14f * (0.5f
                    + 0.5f * sinf((gX_F32)maindata.timeElapsed * 1.2f));
            gX_U8 logoAlpha = (gX_U8)(255.0f * logoPulse);
            gX_GUI_Builder_Color(&b, 255, 255, 255, logoAlpha);
            b.currentElement->stateRendering.RECT_NO_DRAW = true;
            if (logo) {
                gX_GUI_Builder_Texture_Begin(&b);
                    gX_GUI_Builder_Texture_Set(&b, logo);
                gX_GUI_Builder_Texture_End(&b);
            }
        gX_GUI_Builder_Element_End(&b);

        // subtitle
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectGrid(&b, 0.0f, 3.45f, 8.0f, 0.4f);
            gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
            b.currentElement->stateRendering.RECT_NO_DRAW = true;
            gX_GUI_Builder_Text_Begin(&b);
                gX_GUI_Builder_Text_Set(&b, "gX ENGINE  /  v0.21.1 BETA");
                b.currentElement->stateRendering.TEXT_CENTER_X = true;
                if (b.curText) { b.curText->fontSize = 14; b.curText->color = gui.style.title2.subtitleText; b.curText->autoShrink = true; }
                gX_GUI_Builder_Text_Shadow(&b, true);
            gX_GUI_Builder_Text_End(&b);
        gX_GUI_Builder_Element_End(&b);

        //opencode: v0.2.0-pre8 — density variants per frontend_iteration1.png;
        //   items stack on a running y cursor.
        //opencode: F3 — the column is anchor-centered (was magic x=2.3) and
        //   each built button joins the entrance cascade in build order.
        gX_I32 variant = titleVariant();
        gX_F32 menuH = 0.42f;
        gX_F32 menuStep = 0.49f;
        gX_I32 menuCount = (variant == TITLEGX_VARIANT_FULL) ? 7
            : (variant == TITLEGX_VARIANT_PLAY ? 3 : 2);
        //opencode: center every density variant on the full-menu visual
        //   midpoint so reduced menus read as intentional compositions.
        gX_F32 itemY = 5.22f - ((gX_F32)menuCount - 1.0f) * menuStep * 0.5f;
        gX_U32 animIdx = 0;

        if (variant == TITLEGX_VARIANT_FULL) {
            // RESUME
            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectAnchored(&b, TITLEGX_MENU_W, menuH,
                    GX_GUI_ANCHOR_CENTER, GX_GUI_ANCHOR_START, 0.0f, itemY);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.menuBg1);
                gX_GUI_Element_Template_Button(b.currentElement);
                titlegx_MenuClearTooltip(&b);
                    gX_GUI_Builder_Text_Begin(&b);
                        gX_GUI_Builder_Text_Set(&b, "RESUME");
                        titlegx_MenuUseTitleFont(&b);
                        b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) { b.curText->fontSize = 14; b.curText->color = gui.style.title2.menuText1; }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_CallSwitch,
                    (gX_I0*)(gX_U64)(uintptr_t)gX_SCENE_RENDERER_GFX);
                titlegx_MenuButtonFx(&b, animIdx++);
            gX_GUI_Builder_Element_End(&b);
            itemY += menuStep;
        }

        if (variant != TITLEGX_VARIANT_DEMOS) {
            // RUN DEMO
            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectAnchored(&b, TITLEGX_MENU_W, menuH,
                    GX_GUI_ANCHOR_CENTER, GX_GUI_ANCHOR_START, 0.0f, itemY);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.menuBg2);
                gX_GUI_Element_Template_Button(b.currentElement);
                titlegx_MenuClearTooltip(&b);
                    gX_GUI_Builder_Text_Begin(&b);
                        gX_GUI_Builder_Text_Set(&b, "RUN DEMO");
                        titlegx_MenuUseTitleFont(&b);
                        b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) { b.curText->fontSize = 14; b.curText->color = gui.style.title2.menuText2; }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_CallSwitch,
                    (gX_I0*)(gX_U64)(uintptr_t)gX_SCENE_RENDERER_GFX);
                titlegx_MenuButtonFx(&b, animIdx++);
            gX_GUI_Builder_Element_End(&b);
            itemY += menuStep;
        }

        if (variant != TITLEGX_VARIANT_DEMOS) {
            // LOAD SAVE -> save-slot sub-view
            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectAnchored(&b, TITLEGX_MENU_W, menuH,
                    GX_GUI_ANCHOR_CENTER, GX_GUI_ANCHOR_START, 0.0f, itemY);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.menuBg3);
                gX_GUI_Element_Template_Button(b.currentElement);
                titlegx_MenuClearTooltip(&b);
                    gX_GUI_Builder_Text_Begin(&b);
                        gX_GUI_Builder_Text_Set(&b, "LOAD SAVE");
                        titlegx_MenuUseTitleFont(&b);
                        b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) { b.curText->fontSize = 14; b.curText->color = gui.style.title2.menuText3; }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_TitleSaves, 0);
                titlegx_MenuButtonFx(&b, animIdx++);
            gX_GUI_Builder_Element_End(&b);
            itemY += menuStep;
        }

        if (variant == TITLEGX_VARIANT_FULL) {
            // SETTINGS
            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectAnchored(&b, TITLEGX_MENU_W, menuH,
                    GX_GUI_ANCHOR_CENTER, GX_GUI_ANCHOR_START, 0.0f, itemY);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.menuBg4);
                gX_GUI_Element_Template_Button(b.currentElement);
                titlegx_MenuClearTooltip(&b);
                    gX_GUI_Builder_Text_Begin(&b);
                        gX_GUI_Builder_Text_Set(&b, "SETTINGS");
                        titlegx_MenuUseTitleFont(&b);
                        b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) { b.curText->fontSize = 14; b.curText->color = gui.style.title2.menuText4; }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_OpenSettings, 0);
                titlegx_MenuButtonFx(&b, animIdx++);
            gX_GUI_Builder_Element_End(&b);
            itemY += menuStep;
        }

        if (variant != TITLEGX_VARIANT_PLAY) {
            //opencode: open both native renderer demos from a small title hub.
            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectAnchored(&b, TITLEGX_MENU_W, menuH,
                    GX_GUI_ANCHOR_CENTER, GX_GUI_ANCHOR_START, 0.0f, itemY);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.menuBg5);
                gX_GUI_Element_Template_Button(b.currentElement);
                titlegx_MenuClearTooltip(&b);
                    gX_GUI_Builder_Text_Begin(&b);
                        gX_GUI_Builder_Text_Set(&b, "DEMOS");
                        titlegx_MenuUseTitleFont(&b);
                        b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) { b.curText->fontSize = 13; b.curText->color = gui.style.title2.menuText5; }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_TitleDemos, 0);
                titlegx_MenuButtonFx(&b, animIdx++);
            gX_GUI_Builder_Element_End(&b);
            itemY += menuStep;
        }

        if (variant == TITLEGX_VARIANT_FULL) {
            // EXTRAS
            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectAnchored(&b, TITLEGX_MENU_W, menuH,
                    GX_GUI_ANCHOR_CENTER, GX_GUI_ANCHOR_START, 0.0f, itemY);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.menuBg6);
                gX_GUI_Element_Template_Button(b.currentElement);
                titlegx_MenuClearTooltip(&b);
                    gX_GUI_Builder_Text_Begin(&b);
                        gX_GUI_Builder_Text_Set(&b, "EXTRAS");
                        titlegx_MenuUseTitleFont(&b);
                        b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) { b.curText->fontSize = 13; b.curText->color = gui.style.title2.menuText6; }
                gX_GUI_Builder_Text_End(&b);
                gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_TitleExtras, 0);
                titlegx_MenuButtonFx(&b, animIdx++);
            gX_GUI_Builder_Element_End(&b);
            itemY += menuStep;
        }

        // QUIT (bottom)
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectAnchored(&b, TITLEGX_MENU_W, menuH,
                GX_GUI_ANCHOR_CENTER, GX_GUI_ANCHOR_START, 0.0f, itemY);
            gX_GUI_Builder_ColorV(&b, gui.style.title2.quitBg);
            gX_GUI_Element_Template_Button(b.currentElement);
            titlegx_MenuClearTooltip(&b);
            gX_GUI_Builder_Text_Begin(&b);
                gX_GUI_Builder_Text_Set(&b, "QUIT");
                titlegx_MenuUseTitleFont(&b);
                b.currentElement->stateRendering.TEXT_CENTER_X = true;
                b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                if (b.curText) { b.curText->fontSize = 13; b.curText->color = gui.style.title2.quitText; }
            gX_GUI_Builder_Text_End(&b);
            gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_Quit, 0);
            titlegx_MenuButtonFx(&b, animIdx++);
        gX_GUI_Builder_Element_End(&b);
        itemY += menuStep;

        //opencode: v0.2.0-pre8 — footer links row per frontend_iteration1.png
        //   Keep the same navigation chrome across density variants.
        {
            //opencode: footer contains only the working CREDITS route. Dead
            //   INFO/DEMO links and the duplicate QUIT action are removed.
            struct { const gX_I8 *label; gX_F32 x; gX_F32 w; gX_I32 act; } foot[1] = {
                { "CREDITS", 6.35f, 0.88f, 1 },
            };
            //opencode: main-menu polish — a quiet rule gives the footer a
            //   clear home and wider per-link boxes prevent CREDITS truncation.
            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 0.75f, 7.24f, 6.55f, 0.015f);
                gX_GUI_Builder_ColorV(&b, gui.style.title2.heroBand);
            gX_GUI_Builder_Element_End(&b);
            //ai: UX pass — the menu never said how to operate it; the center
            //   footer slot carries the input hint (ESC routes Back through
            //   gX_GUI_Handle_Back, arrows/Enter drive the focus system).
            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 1.95f, 7.38f, 4.2f, 0.3f);
                gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
                b.currentElement->stateRendering.RECT_NO_DRAW = true;
                gX_GUI_Builder_Text_Begin(&b);
                    //ai: ESC opens the overlay menu on the main menu (Back
                    //   only inside subviews) — the hint says what it does.
                    gX_GUI_Builder_Text_Set(&b,
                        "ARROWS SELECT  ENTER CONFIRM  ESC MENU");
                    titlegx_MenuUseTitleFont(&b);
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    if (b.curText) {
                        b.curText->fontSize = 10;
                        b.curText->color = gui.style.title2.footerText;
                    }
                gX_GUI_Builder_Text_End(&b);
            gX_GUI_Builder_Element_End(&b);
            for (gX_I32 fi = 0; fi < 1; fi++) {
                gX_GUI_Builder_Element_Begin(&b);
                    gX_GUI_Builder_RectGrid(&b, foot[fi].x, 7.34f, foot[fi].w, 0.28f);
                    gX_Color creditsBg = gui.style.title2.menuBg5;
                    creditsBg.a = 90;
                    gX_GUI_Builder_ColorV(&b, creditsBg);
                    gX_GUI_Element_Template_Button(b.currentElement);
                    gX_GUI_Builder_Widget_AddHoverColor(&b,
                        (gX_Color){120, 90, 20, 255}, 1.0f);
                    gX_GUI_Builder_Text_Begin(&b);
                        gX_GUI_Builder_Text_Set(&b, foot[fi].label);
                        titlegx_MenuUseTitleFont(&b);
                        b.currentElement->stateRendering.TEXT_CENTER_X = true;
                        if (b.curText) {
                            b.curText->fontSize = 10;
                            b.curText->color = gui.style.title2.footerText;
                        }
                    gX_GUI_Builder_Text_End(&b);
                    gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_TitleCredits, 0);
                gX_GUI_Builder_Element_End(&b);
            }
        }

    }

    gX_GUI_Builder_Free(&b);
}
