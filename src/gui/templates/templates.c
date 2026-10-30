/**********************************************************************************************
*
*   [gX: src/gui/templates/templates.c]
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


//opencode: ported from gameX — essential templates using gX_GUI_Builder API.
//For GAME_BACKEND_GX, builder functions allocate elements without rendering,
//so templates build correctly but elements won't draw.

#include "template_header.h"
#include "../binding.h"

#if gX_ENABLE_RENDERER == 1
    #include "../../renderer/renderer.h"
    #include "../../renderer/gfx/viewport_grid.h"

    //opencode: phase-1 GFX dashboard tile labels. The template owns
    //   composition; the renderer owns only the four live output RTs.
    static const gX_I8 *s_viewportGridLabels[GX_GFX_VIEWPORT_GRID_COUNT] = {
        "VIEW 1", "VIEW 2", "VIEW 3", "VIEW 4"
    };
#endif

//=============================================================================
// Demo
//=============================================================================

gX_I0 gX_GUI_Template_Demo(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_Demo(gX_I32 canvasindex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "Demo", "demo", gX_GUI_TEMPLATE_DEMO);

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_Rect(&b, 64, 64, 64, 64);
        gX_GUI_Builder_Color(&b, 0, 128, 0, 255);
        gX_GUI_Builder_Shader_Begin(&b);
            gX_GUI_Builder_Shader_Frag(&b, "guisurface.frag");
            gX_GUI_Builder_Shader_Draw(&b, true);
        gX_GUI_Builder_Shader_End(&b);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_Rect(&b, 256, 64, 400, 128);
        gX_GUI_Builder_Color(&b, 255, 0, 0, 255);
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "testing text");
            gX_GUI_Builder_Text_FontSize(&b, 24.0f);
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Free(&b);
}

//=============================================================================
// Demo2
//=============================================================================

gX_I0 gX_GUI_Template_Demo2(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_Demo2(gX_I32 canvasindex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "Demo2", "demo 2", gX_GUI_TEMPLATE_DEMO2);

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_Rect(&b, 64, 256, 64, 64);
        gX_GUI_Builder_Color(&b, 0, 255, 0, 255);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_Rect(&b, 64, 64, 256, 128);
        gX_GUI_Builder_Color(&b, 0, 0, 255, 255);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Free(&b);
}

//=============================================================================
// Scene — full-screen canvas. On GX backend, uses a solid fill since no
// renderer canvas texture is available.
//=============================================================================

gX_I0 gX_GUI_Template_Scene(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_Scene(gX_I32 canvasindex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    //opencode: gX — transparent canvas. The scene (title gradient / GFX
    //   lavender sky) draws its own background; a white fill here would
    //   cover it.
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "Scene", "transparent full-screen canvas", gX_GUI_TEMPLATE_SCENE);
    gX_GUI_Builder_Free(&b);
}

//opencode: GUI composition endpoint for GX_GFX_GRID=1. Each texture pointer
//   refers into the single GFX renderer's stable viewport-grid state; no scene
//   data is duplicated and no UI rebuild is needed after RT allocation.
gX_I0 gX_GUI_Template_ViewportGrid(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_ViewportGrid(gX_I32 canvasindex) {
#if gX_ENABLE_RENDERER == 1
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "ViewportGrid",
        "Shared GFX scene rendered in four GUI viewport tiles",
        gX_GUI_TEMPLATE_VIEWPORTGRID);
    gX_F32 margin = 8.0f;
    gX_F32 titleHeight = 28.0f;
    gX_F32 tileWidth = ((gX_F32)b.width - margin * 3.0f) * 0.5f;
    gX_F32 tileHeight = ((gX_F32)b.height - titleHeight - margin * 3.0f) * 0.5f;

    canvas->doTransition = false;
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_Rect(&b, 0.0f, 0.0f, (gX_F32)b.width, (gX_F32)b.height);
        gX_GUI_Builder_Color(&b, 8, 10, 16, 255);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_Rect(&b, margin, 4.0f,
            (gX_F32)b.width - margin * 2.0f, titleHeight - 4.0f);
        gX_GUI_Builder_Color(&b, 20, 28, 42, 255);
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "GFX VIEWPORT GRID  |  shared scene, four cameras");
            gX_GUI_Builder_Text_FontSize(&b, 16.0f);
            gX_GUI_Builder_Text_Offset(&b, 8.0f, 3.0f);
            gX_GUI_Builder_Text_Color(&b, gX_WHITE);
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    gX_RendererGFX *gfx = renderer.gfx.data;
    for (gX_U32 i = 0; gfx && i < GX_GFX_VIEWPORT_GRID_COUNT; i++) {
        gX_U32 column = i % 2u;
        gX_U32 row = i / 2u;
        gX_F32 x = margin + (gX_F32)column * (tileWidth + margin);
        gX_F32 y = titleHeight + margin + (gX_F32)row * (tileHeight + margin);
        gX_Texture *texture = gX_RendererGFX_ViewportGrid_GetTexture(gfx, i);
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_Rect(&b, x, y, tileWidth, tileHeight);
            gX_GUI_Builder_ColorV(&b, gX_WHITE);
            b.currentElement->stateRendering.RECT_NO_DRAW = true;
            gX_GUI_Builder_Texture_Begin(&b);
                gX_GUI_Builder_Texture_Set(&b, texture);
            gX_GUI_Builder_Texture_End(&b);
            gX_GUI_Builder_Text_Begin(&b);
                gX_GUI_Builder_Text_Set(&b, s_viewportGridLabels[i]);
                gX_GUI_Builder_Text_FontSize(&b, 15.0f);
                gX_GUI_Builder_Text_Offset(&b, 8.0f, 6.0f);
                gX_GUI_Builder_Text_Color(&b, gX_WHITE);
            gX_GUI_Builder_Text_End(&b);
        gX_GUI_Builder_Element_End(&b);
    }
    gX_GUI_Builder_Free(&b);
#else
    (void)canvasindex;
#endif
}

//=============================================================================
// Example — color bar test using macros (legacy compatibility)
//=============================================================================

gX_I0 gX_GUI_Template_Example(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_Example(gX_I32 canvasindex) {
    GUI_TEMPLATE_PROLOGUE(Example,"example gui template",EXAMPLE)
    
    GUI_TEMPLATE_ADD_ELEMENT_RECTWHD_COLOR(0.0f,7.0f,1.0f,1.0f,   0,  0,  0,127)
    GUI_TEMPLATE_ADD_ELEMENT_RECTWHD_COLOR(1.0f,7.0f,1.0f,1.0f,   0,  0,255,127)
    GUI_TEMPLATE_ADD_ELEMENT_RECTWHD_COLOR(2.0f,7.0f,1.0f,1.0f,   0,255,  0,127)
    GUI_TEMPLATE_ADD_ELEMENT_RECTWHD_COLOR(3.0f,7.0f,1.0f,1.0f,   0,255,255,127)
    GUI_TEMPLATE_ADD_ELEMENT_RECTWHD_COLOR(4.0f,7.0f,1.0f,1.0f, 255,  0,  0,127)
    GUI_TEMPLATE_ADD_ELEMENT_RECTWHD_COLOR(5.0f,7.0f,1.0f,1.0f, 255,  0,255,127)
    GUI_TEMPLATE_ADD_ELEMENT_RECTWHD_COLOR(6.0f,7.0f,1.0f,1.0f, 255,255,  0,127)
    GUI_TEMPLATE_ADD_ELEMENT_RECTWHD_COLOR(7.0f,7.0f,1.0f,1.0f, 255,255,255,127)
    
    GUI_TEMPLATE_EPILOGUE
}

//=============================================================================
// SDFT — minimal shader-rasterization repro (v0.2 debug)
//   Row A y=64:  batch-path red rect (control, 200x100 @ x64)
//   Row B y=200: batch-path blue rect + guisurface.frag attached (magenta expected)
//   Row C y=336: batch-path green rect + sdfmag.frag attached (magenta expected)
//=============================================================================
gX_I0 gX_GUI_Template_SDFT(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_SDFT(gX_I32 canvasindex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "SDFT", "sdf repro", gX_GUI_TEMPLATE_SDFT);

    // control: pure batch red
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_Rect(&b, 64, 64, 200, 100);
        gX_GUI_Builder_Color(&b, 255, 0, 0, 255);
    gX_GUI_Builder_Element_End(&b);

    // test 1: blue rect + guisurface (full SDF source)
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_Rect(&b, 300, 200, 200, 100);
        gX_GUI_Builder_Color(&b, 0, 0, 255, 255);
        gX_GUI_Builder_Shader_Begin(&b);
            gX_GUI_Builder_Shader_Frag(&b, "guisurface.frag");
            gX_GUI_Builder_Shader_Draw(&b, true);
        gX_GUI_Builder_Shader_End(&b);
    gX_GUI_Builder_Element_End(&b);

    // test 2: green rect + sdfmag (constant magenta)
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_Rect(&b, 536, 336, 200, 100);
        gX_GUI_Builder_Color(&b, 0, 255, 0, 255);
        gX_GUI_Builder_Shader_Begin(&b);
            gX_GUI_Builder_Shader_Frag(&b, "sdfmag.frag");
            gX_GUI_Builder_Shader_Draw(&b, true);
        gX_GUI_Builder_Shader_End(&b);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Free(&b);
}

//=============================================================================
// EscapeMenu — full pause overlay (ported from gameX, engine-scoped)
//=============================================================================

//opencode: v0.4.1 — full pause menu port from gameX. Blur + dim + panel +
//   title + engine buttons (Resume/Settings/Back to Title) + X close +
//   ESC hint. The game-side buttons (Inventory/Quests/Bestiary/Save Game)
//   are dropped per the pivot (engine-only). Consumes the gui.style.escape.*
//   tokens that the theme schema already carried (previously unused) and
//   the gui.ping blur machinery.
gX_I0 gX_GUI_Template_EscapeMenu(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_EscapeMenu(gX_I32 canvasindex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "EscapeMenu", "escape menu", gX_GUI_TEMPLATE_ESCAPEMENU);

    canvas->canvasBox = (gX_Rectangle){0, 0, (gX_F32)maindata.screenWidth, (gX_F32)maindata.screenHeight};

    //opencode: blur shader element — reads from gui.ping for blurred background
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_Rect(&b, 0, 0, (gX_F32)b.width, (gX_F32)b.height);
        gX_GUI_Builder_Color(&b, 255, 255, 255, 255);
        gX_GUI_Builder_Shader_Begin(&b);
            gX_GUI_Builder_Shader_Frag(&b, "postprocess/blur.frag");
            gX_GUI_Builder_Shader_Draw(&b, true);
            gX_GUI_Builder_Shader_Uniform(&b, 0, (gX_F32v4){2.0f, 0.0f, 0.0f, 0.0f});
        gX_GUI_Builder_Shader_End(&b);
        b.currentElement->stateRendering.RECT_NO_DRAW = true;
        b.currentElement->state.IGNORE_OCCLUSION = true;
    gX_GUI_Builder_Element_End(&b);

    //opencode: semi-transparent overlay — token color + cssClass hook
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_Rect(&b, 0, 0, (gX_F32)b.width, (gX_F32)b.height);
        gX_GUI_Builder_Color(&b, gui.style.escape.dimOverlay.r, gui.style.escape.dimOverlay.g,
            gui.style.escape.dimOverlay.b, gui.style.escape.dimOverlay.a);
        gX_GUI_Builder_Class(&b, "pause-dim");
        b.currentElement->state.IGNORE_OCCLUSION = true;
    gX_GUI_Builder_Element_End(&b);

    //menu panel
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 1.5f, 1.5f, 5.0f, 5.5f);
        gX_GUI_Builder_Color(&b, gui.style.escape.panelBg.r, gui.style.escape.panelBg.g,
            gui.style.escape.panelBg.b, gui.style.escape.panelBg.a);
        b.currentElement->cornerRadius = gui.style.cornerRadius;
        //opencode: production glass — themes with non-zero glass tokens get a
        //   glass pause panel; the flat fill is the fallback (default).
        gX_GUI_Builder_Shader_Begin(&b);
            gX_GUI_Builder_Shader_Glass(&b);
        gX_GUI_Builder_Shader_End(&b);
        gX_GUI_Builder_Class(&b, "pause-panel");
    gX_GUI_Builder_Element_End(&b);

    //panel title
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 3.25f, 1.6f, 1.5f, 0.5f);
        gX_GUI_Builder_Color(&b, gui.style.escape.titleBarBg.r, gui.style.escape.titleBarBg.g,
            gui.style.escape.titleBarBg.b, gui.style.escape.titleBarBg.a);
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "Pause");
            gX_GUI_Builder_Text_Color(&b, gui.style.escape.titleText);
        gX_GUI_Builder_Text_End(&b);
        gX_GUI_Builder_Class(&b, "pause-title");
    gX_GUI_Builder_Element_End(&b);

    //opencode: button helper macro — hover sound, focusable, click sound,
    //   token colors + cssClass hook
    #define GX_PAUSE_BTN(label, y, clickFn, arg) \
        gX_GUI_Builder_Element_Begin(&b); \
            gX_GUI_Builder_RectGrid(&b, 2.2f, y, 3.6f, 0.50f); \
            gX_GUI_Builder_Color(&b, gui.style.escape.buttonBg.r, gui.style.escape.buttonBg.g, \
                gui.style.escape.buttonBg.b, gui.style.escape.buttonBg.a); \
            gX_GUI_Element_Template_Button_Standard(b.currentElement); \
            gX_GUI_Builder_ColorV(&b, gui.style.escape.buttonBg); \
            gX_GUI_Builder_Class(&b, "pause-btn"); \
            b.currentElement->state.FOCUSABLE = true; \
            gX_GUI_Builder_Sound_Begin(&b); \
                b.curSound->sound = gX_Audio_SoundGet("buttonHover"); \
                b.curSound->execute.hovered = true; \
                b.curSound->execute.once = true; \
                b.curSound->params.volume = 0.5f; \
            gX_GUI_Builder_Sound_End(&b); \
            gX_GUI_Builder_Sound_Begin(&b); \
                b.curSound->sound = gX_Audio_SoundGet("buttonConfirm"); \
                b.curSound->execute.clickedLeft = true; \
                b.curSound->execute.once = true; \
                b.curSound->params.volume = 0.7f; \
            gX_GUI_Builder_Sound_End(&b); \
            gX_GUI_Builder_Text_Begin(&b); \
                gX_GUI_Builder_Text_Set(&b, label); \
                gX_GUI_Builder_Text_CenterX(&b); \
                if (b.curText) b.curText->autoShrink = true; \
            gX_GUI_Builder_Text_End(&b); \
            gX_GUI_OnClick(b.currentElement, clickFn, (gX_I0*)(gX_U64)(uintptr_t)arg); \
        gX_GUI_Builder_Element_End(&b)

    GX_PAUSE_BTN("Resume",     2.35f, gX_Widget_OnClick_CloseEscapeMenu, 0);
    GX_PAUSE_BTN("Settings",   3.00f, gX_Widget_OnClick_OpenSettings, 0);
    GX_PAUSE_BTN("Back to Title", 3.65f, gX_Widget_OnClick_SwitchToTitle, 0);

    #undef GX_PAUSE_BTN

    //ai: pause mixer — master/music/effects/synth bus volumes. The settings
    //   fields are the persisted surface; gX_Settings_Update pushes a change
    //   to the audio buses the same frame, so a drag is live and Save in the
    //   settings view keeps it. Single column: the pixel fonts need the full
    //   panel width for "label: 1.000" (two columns truncated).
    gX_GUI_Builder_Widget_Slider(&b, 2.3f, 4.55f, 3.4f, 0.34f,
        "Master", &settings.masterVolume, 0.0f, 2.0f);
    gX_GUI_Builder_Widget_Slider(&b, 2.3f, 4.92f, 3.4f, 0.34f,
        "Music", &settings.musicVolume, 0.0f, 1.0f);
    gX_GUI_Builder_Widget_Slider(&b, 2.3f, 5.29f, 3.4f, 0.34f,
        "Effects", &settings.sfxVolume, 0.0f, 1.0f);
    gX_GUI_Builder_Widget_Slider(&b, 2.3f, 5.66f, 3.4f, 0.34f,
        "Synth", &settings.synthVolume, 0.0f, 1.0f);

    //close button (top-right of panel)
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 6.1f, 1.6f, 0.4f, 0.4f);
        gX_GUI_Builder_Color(&b, gui.style.escape.closeBg.r, gui.style.escape.closeBg.g,
            gui.style.escape.closeBg.b, gui.style.escape.closeBg.a);
        gX_GUI_Element_Template_Button_Standard(b.currentElement);
        gX_GUI_Builder_Class(&b, "pause-close");
        b.currentElement->state.FOCUSABLE = true;
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "X");
            gX_GUI_Builder_Text_CenterX(&b);
            //opencode: close glyph uses widget scale — the 32px default was
            //   taller than the 0.4-grid button (layout pass).
            if (b.curText) { b.curText->fontSize = gui.style.smallSize; b.curText->autoShrink = true; }
        gX_GUI_Builder_Text_End(&b);
        gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_CloseEscapeMenu, 0);
    gX_GUI_Builder_Element_End(&b);

    //Esc hint text
    //ai: hint chip — a quiet dark backing keeps the light hint legible on the
    //   Aero glass panel (the bare panel can composite light scene pixels).
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 2.65f, 6.30f, 2.7f, 0.55f);
        gX_GUI_Builder_Color(&b, 8, 10, 18, 250);
        b.currentElement->cornerRadius = gui.style.cornerRadius;
    gX_GUI_Builder_Element_End(&b);
    gX_GUI_Builder_Element_Begin(&b);
        //opencode: the hint was 1.5 grid units wide for a 254px label (layout
        //   pass); widen the box and keep a shrink guard for themes with
        //   wider type.
        gX_GUI_Builder_RectGrid(&b, 2.8f, 6.35f, 2.4f, 0.4f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        b.currentElement->stateRendering.RECT_NO_DRAW = true;
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "ESC to close");
            gX_GUI_Builder_Text_Color(&b, gui.style.escape.hintText);
            gX_GUI_Builder_Text_CenterX(&b);
            if (b.curText) b.curText->autoShrink = true;
        gX_GUI_Builder_Text_End(&b);
        gX_GUI_Builder_Class(&b, "pause-hint");
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Free(&b);
}

//=============================================================================
// DebugDisplay — live engine stats overlay (gX: rebuilds every frame while
//   SHOW_DEBUGDISPLAY is set, so the text rows carry current values)
//=============================================================================

//opencode: v0.4.1 — unified FPS counter (same D10 pattern as the direct-
//   draw FPS overlay in gX_Drawing_FPS; kept here so canvas text can show it)
static gX_I32 gX_GUI_DebugDisplay_FPS(gX_I0) {
    static gX_F64 lastFpsUpdate = 0.0;
    static gX_I32 frameCount = 0;
    static gX_I32 currentFps = 0;
    gX_F64 now = gX_Time_Get();
    frameCount++;
    if (now - lastFpsUpdate >= 1.0) {
        currentFps = frameCount;
        frameCount = 0;
        lastFpsUpdate = now;
    }
    return currentFps;
}

gX_I0 gX_GUI_Template_DebugDisplay(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_DebugDisplay(gX_I32 canvasindex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "DebugDisplay", "debug overlay", gX_GUI_TEMPLATE_DEBUGDISPLAY);

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_Rect(&b, 8, 8, 280, 132);
        gX_GUI_Builder_Color(&b, 20, 20, 20, 180);
    gX_GUI_Builder_Element_End(&b);

    gX_I8 row[4][128];
    const gX_I8 *rows[5];
    gX_I32 fps = gX_GUI_DebugDisplay_FPS();

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_Rect(&b, 16, 14, 260, 20);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        b.currentElement->stateRendering.RECT_NO_DRAW = true;
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "debug display");
            gX_GUI_Builder_Text_Color(&b, (gX_Color){255, 255, 255, 255});
            //opencode: rows are 18-20px tall — the 32px default clipped every
            //   label into the next row (layout pass); widget scale fits.
            if (b.curText) { b.curText->fontSize = gui.style.smallSize; b.curText->autoShrink = true; }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    snprintf(row[0], sizeof(row[0]), "fps %d", fps);
    snprintf(row[1], sizeof(row[1]), "scene %s", gX_Scenes_String(maindata.masterIndex));
    snprintf(row[2], sizeof(row[2]), "screen %ux%u", maindata.screenWidth, maindata.screenHeight);
    snprintf(row[3], sizeof(row[3]), "canvases %u", gui.canvases.amount);
    rows[0] = row[0];
    rows[1] = row[1];
    rows[2] = row[2];
    rows[3] = row[3];
    rows[4] = NULL;

    for (gX_I32 i = 0; rows[i]; i++) {
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_Rect(&b, 16, 40 + (gX_F32)i * 22.0f, 260, 18);
            gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
            b.currentElement->stateRendering.RECT_NO_DRAW = true;
            gX_GUI_Builder_Text_Begin(&b);
                gX_GUI_Builder_Text_Set(&b, rows[i]);
                gX_GUI_Builder_Text_Color(&b, (gX_Color){200, 200, 220, 220});
                if (b.curText) { b.curText->fontSize = gui.style.smallSize; b.curText->autoShrink = true; }
            gX_GUI_Builder_Text_End(&b);
        gX_GUI_Builder_Element_End(&b);
    }

    gX_GUI_Builder_Free(&b);
}

//=============================================================================
// Message — toast popup (ported from gameX; gX previously SET toastMessage
//   but never rendered it — the Message canvas was empty)
//=============================================================================

gX_I0 gX_GUI_Template_Message(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_Message(gX_I32 canvasindex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "Message", "message overlay", gX_GUI_TEMPLATE_MESSAGE);

    //opencode: panel — 5x3 grid units, centered (1.5,2.5)
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 1.5f, 2.5f, 5.0f, 3.0f);
        gX_GUI_Builder_Color(&b, 10, 10, 20, 200);
    gX_GUI_Builder_Element_End(&b);

    //opencode: accent border (top + left)
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 1.5f, 2.5f, 5.0f, 0.05f);
        gX_GUI_Builder_Color(&b, 100, 200, 255, 100);
    gX_GUI_Builder_Element_End(&b);
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 1.5f, 2.5f, 0.05f, 3.0f);
        gX_GUI_Builder_Color(&b, 100, 200, 255, 100);
    gX_GUI_Builder_Element_End(&b);

    //opencode: toast text — the live toastMessage (rebuilds with the toast)
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 1.7f, 2.8f, 4.6f, 2.4f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        b.currentElement->stateRendering.RECT_NO_DRAW = true;
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, gui.state.toastMessage[0] ? gui.state.toastMessage : "message");
            gX_GUI_Builder_Text_Color(&b, (gX_Color){255, 255, 255, 255});
            gX_GUI_Builder_Text_FontSize(&b, 20.0f);
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    //opencode: close button (X, top-right inside the panel)
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 5.6f, 2.7f, 0.5f, 0.5f);
        gX_GUI_Builder_Color(&b, 60, 50, 80, 200);
        gX_GUI_Element_Template_Button(b.currentElement);
        b.currentElement->state.FOCUSABLE = true;
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "X");
            gX_GUI_Builder_Text_Color(&b, gui.style.escape.titleText);
            gX_GUI_Builder_Text_CenterX(&b);
        gX_GUI_Builder_Text_End(&b);
        gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_DismissToast, 0);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Free(&b);
}

//=============================================================================
// Dialog — modal confirm/alert (F1). Dim backdrop + centered panel with title,
//   message, OK and optional Cancel. Buttons close via Dialog_Close so the
//   stored onOk/onCancel fires and the modal stack pops to the menu (if any).
//=============================================================================

static gX_I0 gX_Dialog_OnOk_Clicked(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Dialog_Close(true);
}

static gX_I0 gX_Dialog_OnCancel_Clicked(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Dialog_Close(false);
}

gX_I0 gX_GUI_Template_Dialog(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_Dialog(gX_I32 canvasindex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "Dialog", "modal dialog", gX_GUI_TEMPLATE_DIALOG);
    gX_I1 hasCancel = gui.dialogHasCancel;

    //opencode: dim backdrop — full canvas, swallows clicks (modal scope
    //   already blocks the background; this gives the visual dim).
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.0f, 0.0f, 8.0f, 8.0f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 140);
    gX_GUI_Builder_Element_End(&b);

    //opencode: centered panel 4.4 x 2.6 grid units.
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 1.8f, 2.7f, 4.4f, 2.6f);
        gX_GUI_Builder_Color(&b, 22, 18, 40, 255);
    gX_GUI_Builder_Element_End(&b);

    //opencode: accent bar under the title.
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 1.8f, 3.25f, 4.4f, 0.05f);
        gX_GUI_Builder_Color(&b, 100, 200, 255, 120);
    gX_GUI_Builder_Element_End(&b);

    //opencode: title + message (baked at open time from dialog state).
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 2.0f, 2.8f, 4.0f, 0.45f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        b.currentElement->stateRendering.RECT_NO_DRAW = true;
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, gui.dialogTitle[0] ? gui.dialogTitle : "Confirm");
            gX_GUI_Builder_Text_Color(&b, (gX_Color){220, 220, 255, 255});
            gX_GUI_Builder_Text_FontSize(&b, 24.0f);
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 2.0f, 3.35f, 4.0f, 0.9f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        b.currentElement->stateRendering.RECT_NO_DRAW = true;
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, gui.dialogMessage[0] ? gui.dialogMessage : "Are you sure?");
            gX_GUI_Builder_Text_Color(&b, (gX_Color){200, 200, 200, 255});
            gX_GUI_Builder_Text_FontSize(&b, 16.0f);
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    if (hasCancel) {
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectGrid(&b, 2.1f, 4.45f, 1.8f, 0.55f);
            gX_GUI_Builder_Color(&b, 70, 60, 100, 255);
            gX_GUI_Element_MakeClickable(b.currentElement);
            b.currentElement->state.FOCUSABLE = true;
            gX_GUI_Builder_Text_Begin(&b);
                gX_GUI_Builder_Text_Set(&b, gui.dialogCancelLabel[0] ? gui.dialogCancelLabel : "Cancel");
                gX_GUI_Builder_Text_Color(&b, (gX_Color){230, 230, 230, 255});
                gX_GUI_Builder_Text_CenterX(&b);
            gX_GUI_Builder_Text_End(&b);
            gX_GUI_OnClick(b.currentElement, gX_Dialog_OnCancel_Clicked, 0);
        gX_GUI_Builder_Element_End(&b);

        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectGrid(&b, 4.1f, 4.45f, 1.8f, 0.55f);
            gX_GUI_Builder_Color(&b, 140, 50, 60, 255);
            gX_GUI_Element_MakeClickable(b.currentElement);
            b.currentElement->state.FOCUSABLE = true;
            gX_GUI_Builder_Text_Begin(&b);
                gX_GUI_Builder_Text_Set(&b, gui.dialogOkLabel[0] ? gui.dialogOkLabel : "OK");
                gX_GUI_Builder_Text_Color(&b, (gX_Color){255, 255, 255, 255});
                gX_GUI_Builder_Text_CenterX(&b);
            gX_GUI_Builder_Text_End(&b);
            gX_GUI_OnClick(b.currentElement, gX_Dialog_OnOk_Clicked, 0);
        gX_GUI_Builder_Element_End(&b);
    } else {
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectGrid(&b, 3.0f, 4.45f, 2.0f, 0.55f);
            gX_GUI_Builder_Color(&b, 70, 90, 140, 255);
            gX_GUI_Element_MakeClickable(b.currentElement);
            b.currentElement->state.FOCUSABLE = true;
            gX_GUI_Builder_Text_Begin(&b);
                gX_GUI_Builder_Text_Set(&b, gui.dialogOkLabel[0] ? gui.dialogOkLabel : "OK");
                gX_GUI_Builder_Text_Color(&b, (gX_Color){255, 255, 255, 255});
                gX_GUI_Builder_Text_CenterX(&b);
            gX_GUI_Builder_Text_End(&b);
            gX_GUI_OnClick(b.currentElement, gX_Dialog_OnOk_Clicked, 0);
        gX_GUI_Builder_Element_End(&b);
    }

    gX_GUI_Builder_Free(&b);
}
