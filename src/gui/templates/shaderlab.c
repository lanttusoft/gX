/**********************************************************************************************
*
*   [gX: src/gui/templates/shaderlab.c]
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


#include "template_header.h"

#include "../binding.h"
#include "../style.h"

//opencode: shader card shadow — same offset treatment as the materials lab.
static gX_I0 gX_GUI_Template_ShaderLab_Shadow(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY)
{
    gX_GUI_Builder_Element_Begin(b);
        gX_GUI_Builder_RectGrid(b, gridX + 0.06f, gridY + 0.08f, 2.0f, 2.15f);
        gX_GUI_Builder_Color(b, 8, 8, 18, 130);
        gX_GUI_Builder_Shader_Begin(b);
            gX_GUI_Builder_Shader_Frag(b, "guisurface.frag");
            gX_GUI_Builder_Shader_Draw(b, true);
            gX_GUI_Builder_Shader_Uniform(b, 0, (gX_F32v4){
                gui.style.cornerRadius, 0.0f, 0.0f, 0.0f});
            gX_GUI_Builder_Shader_Uniform(b, 1, (gX_F32v4){0.0f, 0.0f, 0.0f, 1.0f});
            gX_GUI_Builder_Shader_Uniform(b, 2, (gX_F32v4){0.0f, 0.0f, 0.0f, 1.0f});
        gX_GUI_Builder_Shader_End(b);
    gX_GUI_Builder_Element_End(b);
}

//opencode: one postprocess-style experiment card. Mode and params mirror the
//   postprocess chain: translucency samples the backdrop, lens warps its UV,
//   color math grades the sampled result.
static gX_I0 gX_GUI_Template_ShaderLab_Card(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, const gX_I8 *name, const gX_I8 *cue,
    gX_Color color, gX_F32 mode, gX_F32 intensity, gX_F32v4 params)
{
    gX_GUI_Builder_Element_Begin(b);
        gX_GUI_Builder_RectGrid(b, gridX, gridY, 2.0f, 2.15f);
        gX_GUI_Builder_ColorV(b, color);
        gX_GUI_Builder_Shader_Begin(b);
            gX_GUI_Builder_Shader_Frag(b, "guifx.frag");
            gX_GUI_Builder_Shader_Draw(b, true);
            gX_GUI_Builder_Shader_Uniform(b, 0, (gX_F32v4){
                gui.style.cornerRadius, gui.style.borderWidth, mode, intensity});
            gX_GUI_Builder_Shader_Uniform(b, 1, (gX_F32v4){
                gui.style.blueprint.accentColor.r / 255.0f,
                gui.style.blueprint.accentColor.g / 255.0f,
                gui.style.blueprint.accentColor.b / 255.0f, 1.0f});
            gX_GUI_Builder_Shader_Uniform(b, 2, params);
        gX_GUI_Builder_Shader_End(b);
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, name);
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
            if (b->curText)
            {
                b->curText->fontSize = 17;
                b->curText->color = gui.style.a11y.textOnCanvas;
                b->curText->offset.y = -28.0f;
            }
        gX_GUI_Builder_Text_End(b);
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, cue);
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
            if (b->curText)
            {
                b->curText->fontSize = 11;
                b->curText->color = gui.style.a11y.textOnCanvas;
                b->curText->offset.y = 37.0f;
            }
        gX_GUI_Builder_Text_End(b);
    gX_GUI_Builder_Element_End(b);
}

//opencode: return from the overlay without leaving the title scene.
static gX_I0 gX_GUI_Template_ShaderLab_Back(gX_I0 *arg)
{
    (void)arg;
    gui.auditTemplateID = -1;
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(2);
    if (canvas)
    {
        gX_GUI_Canvas_Clear(canvas);
        canvas->templateID = gX_GUI_TEMPLATE_NULL;
    }
    gX_GUI_Canvas *title = gX_GUI_Canvas_FromIndex(0);
    if (title)
    {
        title->refresh = true;
    }
    if (gui.canvases.meta)
    {
        gui.canvases.meta[2].UPDATE = false;
        gui.canvases.meta[2].DRAW = false;
    }
    gX_GUI_Focus_Clear();
}

//opencode: v0.6.11 — shader experiment gallery. Six guifx.frag modes group the
//   postprocess vocabulary for GUI surfaces: translucency, lens, color math.
gX_I0 gX_GUI_Template_ShaderLab(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_ShaderLab(gX_I32 canvasindex)
{
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "ShaderLab",
        "Postprocess-style GUI shader experiments", gX_GUI_TEMPLATE_SHADERLAB);

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.0f, 0.0f, 8.0f, 8.0f);
        gX_GUI_Builder_ColorV(&b, gui.style.a11y.surfaceCanvas);
        gX_GUI_Builder_Shader_Begin(&b);
            gX_GUI_Builder_Shader_Frag(&b, "guimaterials.frag");
            gX_GUI_Builder_Shader_Draw(&b, true);
            gX_GUI_Builder_Shader_Uniform(&b, 0, (gX_F32v4){0.0f, 0.0f, 5.0f, 0.0f});
            gX_GUI_Builder_Shader_Uniform(&b, 1, (gX_F32v4){
                gui.style.blueprint.accentColor.r / 255.0f,
                gui.style.blueprint.accentColor.g / 255.0f,
                gui.style.blueprint.accentColor.b / 255.0f, 1.0f});
            gX_GUI_Builder_Shader_Uniform(&b, 2, (gX_F32v4){0.0f, 0.0f, 0.0f, 0.0f});
        gX_GUI_Builder_Shader_End(&b);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.0f, 0.0f, 8.0f, 0.72f);
        gX_GUI_Builder_ColorV(&b, gui.style.blueprint.inkColor);
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "SHADER LAB  /  TRANSLUCENCY LENS COLOR");
            b.currentElement->stateRendering.TEXT_CENTER_X = true;
            b.currentElement->stateRendering.TEXT_CENTER_Y = true;
            if (b.curText)
            {
                b.curText->fontSize = 14;
                b.curText->color = gui.style.blueprint.accentColor;
            }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.45f, 0.84f, 7.1f, 0.38f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "Six backdrop experiments: frost, glass, lens, chroma, grade, poster.");
            if (b.curText)
            {
                b.curText->fontSize = 11;
                b.curText->color = gui.style.a11y.textOnCanvas;
            }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.0f, 0.70f, 8.0f, 0.02f);
        gX_GUI_Builder_ColorV(&b, gui.style.blueprint.accentColor);
    gX_GUI_Builder_Element_End(&b);

    //opencode: cues stay under ~20 chars — fontSize 11 truncates longer cues.
    gX_GUI_Template_ShaderLab_Shadow(&b, 0.35f, 1.45f);
    gX_GUI_Template_ShaderLab_Card(&b, 0.35f, 1.45f, "FROST", "frosted / blurred",
        (gX_Color){44, 83, 126, 255}, 0.0f, 0.65f, (gX_F32v4){3.0f, 0.0f, 0.0f, 0.0f});
    gX_GUI_Template_ShaderLab_Shadow(&b, 2.55f, 1.45f);
    gX_GUI_Template_ShaderLab_Card(&b, 2.55f, 1.45f, "GLASS", "refraction / sheen",
        (gX_Color){39, 139, 157, 255}, 1.0f, 0.55f, (gX_F32v4){28.0f, 0.0f, 0.0f, 0.0f});
    gX_GUI_Template_ShaderLab_Shadow(&b, 4.75f, 1.45f);
    gX_GUI_Template_ShaderLab_Card(&b, 4.75f, 1.45f, "LENS", "fisheye magnify",
        (gX_Color){119, 93, 132, 255}, 2.0f, 0.75f, (gX_F32v4){0.0f, 0.0f, 0.0f, 0.0f});
    gX_GUI_Template_ShaderLab_Shadow(&b, 0.35f, 4.05f);
    gX_GUI_Template_ShaderLab_Card(&b, 0.35f, 4.05f, "CHROMA", "rgb split lens",
        (gX_Color){34, 111, 174, 255}, 3.0f, 0.70f, (gX_F32v4){9.0f, 0.0f, 0.0f, 0.0f});
    gX_GUI_Template_ShaderLab_Shadow(&b, 2.55f, 4.05f);
    gX_GUI_Template_ShaderLab_Card(&b, 2.55f, 4.05f, "GRADE", "bcg + saturation",
        (gX_Color){126, 135, 143, 255}, 4.0f, 0.60f, (gX_F32v4){0.06f, 1.18f, 1.35f, 0.0f});
    gX_GUI_Template_ShaderLab_Shadow(&b, 4.75f, 4.05f);
    gX_GUI_Template_ShaderLab_Card(&b, 4.75f, 4.05f, "POSTER", "posterize / vignette",
        (gX_Color){34, 111, 90, 255}, 5.0f, 0.55f, (gX_F32v4){4.0f, 0.0f, 0.0f, 0.0f});

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.7f, 6.72f, 6.6f, 0.35f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "FX = backdrop sampling, not a new widget type");
            b.currentElement->stateRendering.TEXT_CENTER_X = true;
            if (b.curText)
            {
                b.curText->fontSize = 12;
                b.curText->color = gui.style.blueprint.accentColor;
            }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 3.0f, 7.28f, 2.0f, 0.46f);
        gX_GUI_Builder_ColorV(&b, gui.style.title2.backBg);
        gX_GUI_Element_Template_Button(b.currentElement);
        b.currentElement->description[0] = '\0';
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "BACK TO TITLE");
            b.currentElement->stateRendering.TEXT_CENTER_X = true;
            b.currentElement->stateRendering.TEXT_CENTER_Y = true;
            if (b.curText)
            {
                b.curText->fontSize = 12;
                b.curText->color = gui.style.title2.backText;
            }
        gX_GUI_Builder_Text_End(&b);
        gX_GUI_OnClick(b.currentElement, gX_GUI_Template_ShaderLab_Back, 0);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Free(&b);
}
