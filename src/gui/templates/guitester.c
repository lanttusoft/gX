/**********************************************************************************************
*
*   [gX: src/gui/templates/guitester.c]
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
#include "template_header.h"
#include "../builder.h"

static gX_F32 g_sliderVal = 0.5f;
static gX_I1  g_checkVal = false;
static gX_F32 g_progressVal = 0.6f;
static gX_I32 g_buttonState = 0;

//opencode: textinput widget state — buffer persists so the harness can read
//   the typed text back through the widget's external buffer.
static gX_I8  g_textInputBuf[128] = "Hello";
static gX_I8  g_textInputCommitMsg[128] = "not committed";
static gX_I32 g_textInputCommitCount = 0;

//opencode: commit callback — fires on Enter in the textinput
static gX_I0 g_textInput_OnCommit(gX_I0 *userData) {
    (void)userData;
    g_textInputCommitCount++;
    snprintf(g_textInputCommitMsg, sizeof(g_textInputCommitMsg), "committed:%d", g_textInputCommitCount);
}

//opencode: reset widget state on scene entry — slider positions/checkbox
//   state persisted across scene switches (harness pixel checks assume
//   the initial values)
gX_I0 gX_GUI_Template_GuiTester_ResetState(gX_I0) {
    g_sliderVal = 0.5f;
    g_checkVal = false;
    g_progressVal = 0.6f;
    g_buttonState = 0;
    strncpy(g_textInputBuf, "Hello", sizeof(g_textInputBuf) - 1);
    g_textInputBuf[sizeof(g_textInputBuf) - 1] = '\0';
    g_textInputCommitCount = 0;
    g_textInputCommitMsg[0] = '\0';
}

gX_I0 gX_GUI_Template_GuiTester(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_GuiTester(gX_I32 canvasindex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "GuiTester", "GUI test suite", gX_GUI_TEMPLATE_GUITESTER);

    // Row 0: header
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.5f, 0.1f, 7.0f, 0.3f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "GUI Test Suite");
            b.currentElement->stateRendering.TEXT_CENTER_X = true;
            b.currentElement->stateRendering.TEXT_CENTER_Y = true;
            //opencode: 0.3-grid row — the 32px default was taller than the box.
            if (b.curText) { b.curText->color = gX_WHITE; b.curText->fontSize = 24.0f; b.curText->autoShrink = true; }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    // Row 1: colored rects — red, green, blue, yellow
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.5f, 0.6f, 1.5f, 0.6f);
        gX_GUI_Builder_Color(&b, 255, 0, 0, 255);
    gX_GUI_Builder_Element_End(&b);
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 2.5f, 0.6f, 1.5f, 0.6f);
        gX_GUI_Builder_Color(&b, 0, 255, 0, 255);
    gX_GUI_Builder_Element_End(&b);
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 4.5f, 0.6f, 1.5f, 0.6f);
        gX_GUI_Builder_Color(&b, 0, 0, 255, 255);
    gX_GUI_Builder_Element_End(&b);
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 6.5f, 0.6f, 1.5f, 0.6f);
        gX_GUI_Builder_Color(&b, 255, 255, 0, 255);
    gX_GUI_Builder_Element_End(&b);

    // Row 2: vertical gradient (red -> blue)
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.5f, 1.4f, 3.0f, 1.0f);
        gX_GUI_Builder_ColorV(&b, (gX_Color){255, 0, 0, 255});
        b.currentElement->stateRendering.GRADIENT_ENABLE = true;
        b.currentElement->stateRendering.GRADIENT_STATE = 0;
        gX_GUI_Element_Add_Color(b.currentElement);
        gX_GUI_Element_Color *gt = gX_GUI_Element_Color_Get(b.currentElement, 0);
        if (gt) { gt->gradientColor = true; gt->color = (gX_Color){255,0,0,255}; }
        gX_GUI_Element_Add_Color(b.currentElement);
        gX_GUI_Element_Color *gb = gX_GUI_Element_Color_Get(b.currentElement, 1);
        if (gb) { gb->gradientColor = true; gb->color = (gX_Color){0,0,255,255}; }
    gX_GUI_Builder_Element_End(&b);

    // Row 2b: horizontal gradient (green -> yellow)
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 4.5f, 1.4f, 3.0f, 1.0f);
        gX_GUI_Builder_ColorV(&b, (gX_Color){0, 255, 0, 255});
        b.currentElement->stateRendering.GRADIENT_ENABLE = true;
        b.currentElement->stateRendering.GRADIENT_STATE = 1;
        gX_GUI_Element_Add_Color(b.currentElement);
        gX_GUI_Element_Color *hl = gX_GUI_Element_Color_Get(b.currentElement, 0);
        if (hl) { hl->gradientColor = true; hl->color = (gX_Color){0,255,0,255}; }
        gX_GUI_Element_Add_Color(b.currentElement);
        gX_GUI_Element_Color *hr = gX_GUI_Element_Color_Get(b.currentElement, 1);
        if (hr) { hr->gradientColor = true; hr->color = (gX_Color){255,255,0,255}; }
    gX_GUI_Builder_Element_End(&b);

    // Row 2c: four-point gradient — colors[0..3] = TL / BL / BR / TR
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.5f, 4.1f, 3.0f, 1.0f);
        gX_GUI_Builder_ColorV(&b, (gX_Color){255, 0, 0, 255});
        b.currentElement->stateRendering.GRADIENT_ENABLE = true;
        b.currentElement->stateRendering.GRADIENT_STATE = 2;
        gX_GUI_Element_Add_Color(b.currentElement);
        gX_GUI_Element_Color *fp0 = gX_GUI_Element_Color_Get(b.currentElement, 0);
        if (fp0) { fp0->gradientColor = true; fp0->color = (gX_Color){255,0,0,255}; }
        gX_GUI_Element_Add_Color(b.currentElement);
        gX_GUI_Element_Color *fp1 = gX_GUI_Element_Color_Get(b.currentElement, 1);
        if (fp1) { fp1->gradientColor = true; fp1->color = (gX_Color){0,0,255,255}; }
        gX_GUI_Element_Add_Color(b.currentElement);
        gX_GUI_Element_Color *fp2 = gX_GUI_Element_Color_Get(b.currentElement, 2);
        if (fp2) { fp2->gradientColor = true; fp2->color = (gX_Color){255,255,0,255}; }
        gX_GUI_Element_Add_Color(b.currentElement);
        gX_GUI_Element_Color *fp3 = gX_GUI_Element_Color_Get(b.currentElement, 3);
        if (fp3) { fp3->gradientColor = true; fp3->color = (gX_Color){0,255,0,255}; }
    gX_GUI_Builder_Element_End(&b);

    // Row 2d: per-element depth override — left forced flat, right forced
    //   metallic, independent of the theme's surfaceDepth.mode.
    for (gX_I32 side = 0; side < 2; side++) {
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectGrid(&b, 4.5f + 0.8f * (gX_F32)side, 4.1f,
                0.7f, 1.0f);
            gX_GUI_Builder_ColorV(&b, (gX_Color){150, 150, 150, 255});
            b.currentElement->stateRendering.GRADIENT_ENABLE = true;
            //opencode: horizontal authored gradient -> the forced metallic
            //   override lays its band along X (visible in the clean top strip
            //   where the title canvas does not overlap the fixture).
            b.currentElement->stateRendering.GRADIENT_STATE = 1;
            gX_GUI_Builder_SurfaceDepth(&b, side == 0 ? 1 : 3);
            gX_GUI_Element_Add_Color(b.currentElement);
            gX_GUI_Element_Color *dtop = gX_GUI_Element_Color_Get(b.currentElement, 0);
            if (dtop) { dtop->gradientColor = true; dtop->color = (gX_Color){150,150,150,255}; }
            gX_GUI_Element_Add_Color(b.currentElement);
            gX_GUI_Element_Color *dbot = gX_GUI_Element_Color_Get(b.currentElement, 1);
            if (dbot) { dbot->gradientColor = true; dbot->color = (gX_Color){150,150,150,255}; }
        gX_GUI_Builder_Element_End(&b);
    }

    // Row 3: wireframe rect
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.5f, 2.7f, 3.0f, 0.5f);
        gX_GUI_Builder_Color(&b, 255, 255, 255, 255);
        b.currentElement->stateRendering.RECT_DRAW_WIRE = true;
    gX_GUI_Builder_Element_End(&b);

    // Row 3b: centered text
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 4.5f, 2.7f, 3.0f, 0.5f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "Test OK");
            b.currentElement->stateRendering.TEXT_CENTER_X = true;
            b.currentElement->stateRendering.TEXT_CENTER_Y = true;
            if (b.curText) b.curText->color = gX_WHITE;
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    // Row 4a: shader element (guigradient.frag)
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.5f, 3.5f, 3.0f, 0.5f);
        gX_GUI_Builder_Color(&b, 128, 128, 128, 255);
        gX_GUI_Builder_Shader_Begin(&b);
            gX_GUI_Builder_Shader_Frag(&b, "guigradient.frag");
            gX_GUI_Builder_Shader_Draw(&b, true);
        gX_GUI_Builder_Shader_End(&b);
    gX_GUI_Builder_Element_End(&b);

    // Row 4b: shapes background rect + rect-relative shapes
    //opencode: shapes positioned as fractions of the parent rect — they were
    //   absolute 1080p coords (1520..2100 x) that moved out of the rect at
    //   other resolutions (and went off-screen on the right).
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 4.5f, 3.5f, 3.0f, 0.5f);
        gX_GUI_Builder_ColorV(&b, (gX_Color){40, 40, 40, 255});
        gX_F32 rx = b.currentElement->rect.x;
        gX_F32 ry = b.currentElement->rect.y;
        gX_F32 rw = b.currentElement->rect.width;
        gX_F32 rh = b.currentElement->rect.height;
        gX_GUI_Builder_Shape_Begin(&b, gX_GUI_ELEMENT_SHAPE_CIRCLE);
            b.curShape->v2[0] = (gX_F32v2){rx + 0.083f*rw, ry + 0.5f*rh};
            b.curShape->scalar[0] = 20.0f;
            b.curShape->color = (gX_Color){255, 200, 0, 255};
        gX_GUI_Builder_Shape_End(&b);
        gX_GUI_Builder_Shape_Begin(&b, gX_GUI_ELEMENT_SHAPE_LINE);
            b.curShape->v2[0] = (gX_F32v2){rx + 0.125f*rw, ry + 0.583f*rh};
            b.curShape->v2[1] = (gX_F32v2){rx + 0.25f*rw, ry + 0.75f*rh};
            b.curShape->scalar[0] = 4.0f;
            b.curShape->color = (gX_Color){0, 200, 255, 255};
        gX_GUI_Builder_Shape_End(&b);
        gX_GUI_Builder_Shape_Begin(&b, gX_GUI_ELEMENT_SHAPE_TRIANGLE);
            b.curShape->v2[0] = (gX_F32v2){rx + 0.292f*rw, ry + 0.667f*rh};
            b.curShape->v2[1] = (gX_F32v2){rx + 0.354f*rw, ry + 0.25f*rh};
            b.curShape->v2[2] = (gX_F32v2){rx + 0.417f*rw, ry + 0.75f*rh};
            b.curShape->scalar[0] = 2.0f;
            b.curShape->color = (gX_Color){200, 50, 200, 255};
        gX_GUI_Builder_Shape_End(&b);
        gX_GUI_Builder_Shape_Begin(&b, gX_GUI_ELEMENT_SHAPE_POINT);
            b.curShape->v2[0] = (gX_F32v2){rx + 0.479f*rw, ry + 0.667f*rh};
            b.curShape->color = (gX_Color){255, 0, 0, 255};
        gX_GUI_Builder_Shape_End(&b);
        gX_GUI_Builder_Shape_Begin(&b, gX_GUI_ELEMENT_SHAPE_BEZIER);
            b.curShape->v2[0] = (gX_F32v2){rx + 0.5f*rw, ry + 0.25f*rh};
            b.curShape->v2[1] = (gX_F32v2){rx + 0.6875f*rw, ry + 0.75f*rh};
            b.curShape->v2[2] = (gX_F32v2){rx + 0.5625f*rw, ry + 0.167f*rh};
            b.curShape->v2[3] = (gX_F32v2){rx + 0.625f*rw, ry + 0.792f*rh};
            b.curShape->scalar[0] = 3.0f;
            b.curShape->color = (gX_Color){0, 255, 0, 255};
        gX_GUI_Builder_Shape_End(&b);
    gX_GUI_Builder_Element_End(&b);

    // Row 5: widgets — Button, Slider, CheckBox, ProgressBar
    gX_GUI_Builder_Widget_Button(&b, 0.5f, 4.3f, 1.2f, 0.4f, "Btn", 0, &g_buttonState);
    gX_GUI_Builder_Widget_Slider(&b, 2.0f, 4.3f, 2.0f, 0.4f, "S", &g_sliderVal, 0.0f, 1.0f);
    gX_GUI_Builder_Widget_CheckBox(&b, 4.5f, 5.0f, 0.5f, 0.4f, "C", &g_checkVal);
    gX_GUI_Builder_Widget_ProgressBar(&b, 5.5f, 5.0f, 2.0f, 0.4f, &g_progressVal, 0.0f, 1.0f,
        (gX_Color){50, 200, 50, 255}, true);

    // Row 6: wireframe rect (small)
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.5f, 5.8f, 3.0f, 0.3f);
        gX_GUI_Builder_Color(&b, 255, 255, 0, 255);
        b.currentElement->stateRendering.RECT_DRAW_WIRE = true;
    gX_GUI_Builder_Element_End(&b);
    // Row 6b: small text
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 4.5f, 5.8f, 3.0f, 0.3f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "gui fine");
            b.currentElement->stateRendering.TEXT_CENTER_X = true;
            b.currentElement->stateRendering.TEXT_CENTER_Y = true;
            if (b.curText) { b.curText->color = gX_GREEN; b.curText->fontSize = 24.0f; b.curText->autoShrink = true; }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    // Row 7a: simple green shader test (guitester.frag outputs pure green)
    // Load via fn2 so fileName2 is set — Shader_Frag searches by fileName2.
    gX_RMHeap_CheckElseLoadFileName(b.shaderHeap, "resources/shader/", NULL, "guitester.frag");
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.5f, 6.6f, 3.0f, 0.3f);
        gX_GUI_Builder_Color(&b, 128, 128, 128, 255);
        gX_GUI_Builder_Shader_Begin(&b);
            gX_GUI_Builder_Shader_Frag(&b, "guitester.frag");
            gX_GUI_Builder_Shader_Draw(&b, true);
        gX_GUI_Builder_Shader_End(&b);
    gX_GUI_Builder_Element_End(&b);

    // Row 8: 3D model element — renders cube.glb at known position
    canvas->camera.camera3D.position = (gX_F32v3){0, 0, 10};
    canvas->camera.camera3D.target = (gX_F32v3){0, 0, 0};
    canvas->camera.camera3D.up = (gX_F32v3){0, 1, 0};
    canvas->camera.camera3D.fovy = 45;
    // Load cube.glb into model heap for the model element
    //opencode: RM heap slots are plain gX_Model — the old gX_Model_Wrapper
    //   cast + &wrapper->model produced an OOB pointer (~280 bytes past the
    //   block) so the cube never rendered (harness checkpoint was vacuous).
    gX_Model *cubeM = (gX_Model*)gX_RMHeap_CheckElseLoad(
        b.modelHeap, "resources/model/primitive/cube.glb", NULL);
    {
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectGrid(&b, 0.5f, 7.2f, 3.0f, 0.6f);
            gX_GUI_Builder_ColorV(&b, gX_DARKGRAY);
            gX_GUI_Builder_Model_Begin(&b);
                if (cubeM) {
                    b.curModel->model = cubeM;
                    b.curModel->useParameters = true;
                    //opencode: WHITE element color — the model tint (element
                    //   color × canvasHue) was DARKGRAY, identical to the bg,
                    //   so the cube was indistinguishable from the background.
                    b.currentElement->color = gX_WHITE;
                    b.curModel->paramScale = (gX_F32v3){0.6f, 0.6f, 0.6f};
                    b.curModel->paramRotate = (gX_F32v3){0.25f, 0.5f, 0.0f};
                }
            gX_GUI_Builder_Model_End(&b);
        gX_GUI_Builder_Element_End(&b);
    }

    // Row 9: shape + shader — circle drawn via guitester.frag (pure green).
    //   Deterministic: the shader output (0,255,0) proves the shader path
    //   ran (the batch shape path would draw the shape color instead).
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 4.5f, 7.2f, 3.0f, 0.6f);
        gX_GUI_Builder_ColorV(&b, gX_DARKGRAY);
        gX_GUI_Builder_Shader_Begin(&b);
            gX_GUI_Builder_Shader_Frag(&b, "guitester.frag");
            gX_GUI_Builder_Shader_Draw(&b, false);
        gX_GUI_Builder_Shader_End(&b);
        gX_GUI_Builder_Shape_Begin(&b, gX_GUI_ELEMENT_SHAPE_CIRCLE);
            //opencode: center the circle in the element rect — was hardcoded
            //   (1920,1800) canvas pixels, which moved off-buffer when the
            //   canvas shrank (resolution-dependent checkpoint failure)
            b.curShape->v2[0] = (gX_F32v2){
                b.currentElement->rect.x + b.currentElement->rect.width * 0.5f,
                b.currentElement->rect.y + b.currentElement->rect.height * 0.5f};
            b.curShape->scalar[0] = 60.0f;
            b.curShape->color = (gX_Color){255, 0, 0, 255};
            gX_GUI_Builder_Shape_Shader(&b, 0);
        gX_GUI_Builder_Shape_End(&b);
    gX_GUI_Builder_Element_End(&b);

    // Row 10: texture + shader — texture quad drawn via guitester.frag
    gX_Texture *guiTex = (gX_Texture*)gX_RMHeap_CheckElseLoad(
        b.textureHeap, "texture/background/000.png", NULL);
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.5f, 5.4f, 3.0f, 0.4f);
        gX_GUI_Builder_ColorV(&b, gX_DARKGRAY);
        gX_GUI_Builder_Shader_Begin(&b);
            gX_GUI_Builder_Shader_Frag(&b, "guitester.frag");
            gX_GUI_Builder_Shader_Draw(&b, false);
        gX_GUI_Builder_Shader_End(&b);
        gX_GUI_Builder_Texture_Begin(&b);
            if (guiTex) {
                gX_GUI_Builder_Texture_Set(&b, guiTex);
                gX_GUI_Builder_Texture_Shader(&b, 0);
            }
        gX_GUI_Builder_Texture_End(&b);
    gX_GUI_Builder_Element_End(&b);

    // Row 11: box-model flex layout — a FLEX_ROW container positions 3 rect
    //   children (START justify, START align, explicit sizes). The layout
    //   resolve overwrites child positions — checkpoints prove the flex math
    //   ran at known grid positions (children at gx 5.0/6.0/7.0, gy 6.0..6.3).
    gX_GUI_Builder_Layout_Begin(&b, 4.5f, 6.0f, 3.0f, 0.5f);
        gX_GUI_Builder_ColorV(&b, gX_DARKGRAY);
        gX_GUI_Builder_Layout_Display(&b, gX_GUI_ELEMENT_LAYOUT_DISPLAY_FLEX_ROW);
        gX_GUI_Builder_Layout_Align(&b, gX_GUI_ELEMENT_LAYOUT_ALIGN_START);
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectGrid(&b, 0.0f, 0.0f, 1.0f, 0.3f);
            gX_GUI_Builder_Color(&b, 255, 0, 0, 255);
        gX_GUI_Builder_Element_End(&b);
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectGrid(&b, 0.0f, 0.0f, 1.0f, 0.3f);
            gX_GUI_Builder_Color(&b, 0, 255, 0, 255);
        gX_GUI_Builder_Element_End(&b);
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectGrid(&b, 0.0f, 0.0f, 1.0f, 0.3f);
            gX_GUI_Builder_Color(&b, 0, 0, 255, 255);
        gX_GUI_Builder_Element_End(&b);
    gX_GUI_Builder_Layout_End(&b);

    // Row 12: gameCSS — stylesheet-driven colors. Attach the sheet once, then
    //   three rects: class color (blue), id override (white — specificity
    //   100 > 10), inline style (green — origin 2 beats the sheet).
    gX_GUI_Builder_CSS(&b, "resources/style/guitester.css");
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 4.5f, 6.6f, 0.9f, 0.6f);
        gX_GUI_Builder_Class(&b, "demo-blue");
        gX_GUI_Builder_Name(&b, "plain");
    gX_GUI_Builder_Element_End(&b);
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 5.5f, 6.6f, 0.9f, 0.6f);
        gX_GUI_Builder_Class(&b, "demo-blue");
        gX_GUI_Builder_Name(&b, "overridden");
    gX_GUI_Builder_Element_End(&b);
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 6.5f, 6.6f, 0.9f, 0.6f);
        gX_GUI_Builder_Class(&b, "demo-red");
        gX_GUI_Builder_Name(&b, "inlined");
        gX_GUI_Builder_Style(&b, "background-color: #00ff00;");
    gX_GUI_Builder_Element_End(&b);

    // Row 13: text input widget — editable text field (default "Hello") plus
    //   a password field. The buffer is read back by the harness/DOM scripts.
    //   Positioned in the vertical gap between the model row (x<=3.5) and the
    //   shape row (x>=4.5) so the MarkupDemo panel (canvas 1) does not occlude
    //   clicks on these fields.
    gX_GUI_Builder_Widget_TextInput(&b, 3.55f, 7.15f, 0.9f, 0.25f,
        "tiText", g_textInputBuf, (gX_U32)sizeof(g_textInputBuf), 32, "type here...", false,
        g_textInput_OnCommit, NULL);
    gX_GUI_Builder_Widget_TextInput(&b, 3.55f, 7.45f, 0.9f, 0.25f,
        "tiPass", NULL, 0, 32, "password", true,
        NULL, NULL);

    //opencode: v0.6.0 — word-wrap demo: a long sentence in a fixed-width
    //   rect wraps into lines instead of overflowing/truncating (wrap flag).
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 6.2f, 4.4f, 2.5f, 1.6f);
        gX_GUI_Builder_Color(&b, 30, 30, 30, 255);
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "Word wrap demo: this long sentence wraps at the element width instead of overflowing or truncating.");
            if (b.curText) { b.curText->wrap = true; b.curText->color = gX_WHITE; b.curText->fontSize = 16; }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    //opencode: v0.6.0 — SDF text demo: bitmap vs SDF twin at 3x base size.
    //   The SDF label stays crisp at scale; the bitmap atlas bilinear-upscales.
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 6.2f, 2.4f, 2.5f, 1.1f);
        gX_GUI_Builder_Color(&b, 25, 25, 25, 255);
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "Bitmap 96px");
            if (b.curText) { b.curText->color = gX_WHITE; b.curText->fontSize = 96; }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 6.2f, 3.55f, 2.5f, 1.1f);
        gX_GUI_Builder_Color(&b, 25, 25, 25, 255);
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "SDF 96px");
            if (b.curText) { b.curText->color = gX_WHITE; b.curText->fontSize = 96; b.curText->sdf = true; }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    //opencode: Aero-glass demo panel — samples the accumulated backdrop
    //   (scene + earlier canvases) through glass.frag. A zeroed glass token
    //   set (or GX_GUI_GLASS=0) falls back to the plain translucent fill.
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 6.7f, 6.9f, 1.2f, 0.6f);
        gX_GUI_Builder_Color(&b, 190, 215, 245, 110);
        b.currentElement->cornerRadius = 10.0f;
        gX_GUI_Builder_Name(&b, "glass-panel");
        gX_GUI_Builder_Shader_Begin(&b);
            gX_GUI_Builder_Shader_Glass(&b);
        gX_GUI_Builder_Shader_End(&b);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Free(&b);
}
