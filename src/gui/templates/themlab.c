/**********************************************************************************************
*
*   [gX: src/gui/templates/themlab.c]
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


//opencode: v0.6.2 — THEMLAB — live design preview. Every widget family on
//   this canvas is drawn from the LIVE style tokens (gui.style.*), so token
//   edits (the ImGui theme editor, --theme-editor / --themlab) re-skin the
//   gallery in the same frame via gX_GUI_Style_MarkRefresh. The right panel
//   previews the blueprint line-art direction (frontend_design.md): a
//   lattice-canvas backdrop + a mini title hero/menu mockup fed by the
//   title2 + blueprint token groups.

#include "template_header.h"

#include "../binding.h"
#include "../style.h"

//opencode: widget state — resets on entry (harness/pixel determinism)
static gX_F32 g_sliderVal = 0.62f;
static gX_I1  g_checkVal = false;
static gX_I1  g_toggleVal = true;
static gX_I32 g_radioVal = 2;
static gX_I32 g_dropVal = 1;
static gX_I32 g_stepperVal = 8;
static gX_F32 g_progressVal = 0.73f;
static gX_I32 g_btnState = 0;
static gX_I32 g_listSel = 0;
static gX_I32 g_listScroll = 0;
static gX_I32 g_tabActive = 1;
static gX_I8  g_inputBuf[64] = "designer";
static gX_I1  g_blueprintOn = true;

static const gX_I8 *g_tabs[3] = { "design", "tokens", "export" };
static const gX_I8 g_listItems[] =
    "continue\0new game\0load game\0settings\0demos\0extras\0credits\0quit\0";

//opencode: F4 — dialog demo. OK resets the gallery statics (visible: every
//   widget snaps back to defaults); Cancel leaves them alone. The canvas is
//   refreshed so the rebuild picks up the reset values.
gX_I0 gX_GUI_Template_ThemLab_ResetState(gX_I0);
static gX_GUI_Canvas *g_labCanvas = NULL;
static gX_I0 lab_reset_confirmed(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Template_ThemLab_ResetState();
    gui.refresh = 1;
    if (g_labCanvas) g_labCanvas->refresh = 1;
}
static gX_I0 lab_open_reset_dialog(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Dialog_Open("Reset gallery", "Reset all widget values to defaults?",
        "Reset", "Cancel",
        lab_reset_confirmed, NULL, NULL, NULL);
}

//opencode: widget-lab APPLY demo — toast proves the raw-funcPtr click path.
static gX_I0 lab_apply_toast(gX_I0 *arg) {
    (void)arg;
    strncpy(gui.state.toastMessage, "Applied!", sizeof(gui.state.toastMessage) - 1);
    gui.state.toastMessage[sizeof(gui.state.toastMessage) - 1] = '\0';
    gui.state.SHOW_MSG = true;
}

gX_I0 gX_GUI_Template_ThemLab_ResetState(gX_I0) {
    g_sliderVal = 0.62f;
    g_checkVal = false;
    g_toggleVal = true;
    g_radioVal = 2;
    g_dropVal = 1;
    g_stepperVal = 8;
    g_progressVal = 0.73f;
    g_btnState = 0;
    g_listSel = 0;
    g_listScroll = 0;
    g_tabActive = 1;
    strncpy(g_inputBuf, "designer", sizeof(g_inputBuf) - 1);
    g_inputBuf[sizeof(g_inputBuf) - 1] = '\0';
    g_blueprintOn = true;
}

//opencode: one live-token chip — surface bg + its textOn label
static gX_I0 lab_surface_chip(gX_GUI_Builder *b, gX_F32 gx, gX_F32 gy,
    gX_Color bg, const gX_I8 *label) {
    gX_GUI_Builder_Element_Begin(b);
        gX_GUI_Builder_RectGrid(b, gx, gy, 1.35f, 0.30f);
        gX_GUI_Builder_ColorV(b, bg);
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, label);
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
            if (b->curText) { b->curText->fontSize = 12; b->curText->color = gui.style.a11y.textOnCanvas; }
        gX_GUI_Builder_Text_End(b);
    gX_GUI_Builder_Element_End(b);
}

//opencode: mini title menu — a column of buttons + wordmark. Blueprint
//   mode re-skins the same mockup with ink/accent (the A/B decision aid).
static gX_I0 lab_title_menu(gX_GUI_Builder *b, gX_F32 gx, gX_F32 gy, gX_I1 blueprint) {
    gX_Color ink = gui.style.blueprint.inkColor;
    gX_Color accent = gui.style.blueprint.accentColor;
    gX_Color ramps[3][2] = {
        { blueprint ? ink : gui.style.title2.menuBg1, blueprint ? accent : gui.style.title2.menuText1 },
        { blueprint ? ink : gui.style.title2.menuBg2, blueprint ? accent : gui.style.title2.menuText2 },
        { blueprint ? ink : gui.style.title2.menuBg3, blueprint ? accent : gui.style.title2.menuText3 },
    };
    const gX_I8 *labels[3] = { "RESUME", "RUN DEMO", "LOAD SAVE" };
    for (gX_I32 i = 0; i < 3; i++) {
        gX_GUI_Builder_Element_Begin(b);
            gX_GUI_Builder_RectGrid(b, gx, gy + 0.42f * (gX_F32)i, 1.5f, 0.36f);
            gX_GUI_Builder_ColorV(b, ramps[i][0]);
            gX_GUI_Element_Template_Button(b->currentElement);
            gX_GUI_Builder_Text_Begin(b);
                gX_GUI_Builder_Text_Set(b, labels[i]);
                b->currentElement->stateRendering.TEXT_CENTER_X = true;
                b->currentElement->stateRendering.TEXT_CENTER_Y = true;
                if (b->curText) { b->curText->fontSize = 11; b->curText->color = ramps[i][1]; }
            gX_GUI_Builder_Text_End(b);
        gX_GUI_Builder_Element_End(b);
    }
    gX_GUI_Builder_Element_Begin(b);
        gX_GUI_Builder_RectGrid(b, gx, gy - 0.5f, 1.5f, 0.42f);
        gX_GUI_Builder_Color(b, 0, 0, 0, 0);
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, "gX");
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            if (b->curText) { b->curText->fontSize = 30; b->curText->color = blueprint ? gui.style.blueprint.accentColor : gui.style.title2.wordmarkText; }
        gX_GUI_Builder_Text_End(b);
    gX_GUI_Builder_Element_End(b);
}

gX_I0 gX_GUI_Template_ThemLab(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_ThemLab(gX_I32 canvasindex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "ThemLab", "live design preview", gX_GUI_TEMPLATE_THEMLAB);
    g_labCanvas = canvas;

    //opencode: full-canvas backdrop (a11y surfaceCanvas, opaque) so the
    //   scene behind (title) never bleeds into the preview gallery
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.0f, 0.0f, 8.0f, 8.0f);
        gX_GUI_Builder_ColorV(&b, gui.style.a11y.surfaceCanvas);
    gX_GUI_Builder_Element_End(&b);

    //opencode: short header — the old 100-char caption clipped both edges.
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.0f, 0.0f, 8.0f, 0.5f);
        gX_GUI_Builder_ColorV(&b, gui.style.blueprint.inkColor);
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "THEM LAB - live design preview");
            if (b.curText) { b.curText->fontSize = 14; b.curText->color = gui.style.blueprint.accentColor; }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    //opencode: declutter — fewer, wider buttons so labels never ellipsize.
    //--- LEFT: widget gallery (live tokens) ---
    gX_GUI_Builder_Widget_Section(&b, 0.1f, 0.6f, 4.6f, "Buttons", 14);
    gX_GUI_Builder_Widget_Button(&b, 0.15f, 1.05f, 1.4f, 0.38f, "PLAY", 0, &g_btnState);
    gX_GUI_Builder_Widget_Button(&b, 1.65f, 1.05f, 1.4f, 0.38f, "BACK", 0, &g_btnState);
    gX_GUI_Builder_Widget_Button(&b, 3.15f, 1.05f, 1.4f, 0.38f, "QUIT", 0, &g_btnState);

    //opencode: declutter — one widget family per row with room to breathe.
    gX_GUI_Builder_Widget_Section(&b, 0.1f, 1.55f, 4.6f, "Inputs", 14);
    gX_GUI_Builder_Widget_Slider(&b, 0.15f, 2.0f, 4.4f, 0.38f, "slider", &g_sliderVal, 0.0f, 1.0f);
    gX_GUI_Builder_Widget_CheckBox(&b, 0.15f, 2.5f, 1.4f, 0.38f, "check", &g_checkVal);
    gX_GUI_Builder_Widget_ToggleSwitch(&b, 1.65f, 2.5f, 1.7f, 0.38f, "toggle", &g_toggleVal);
    gX_GUI_Builder_Widget_Radio(&b, 0.15f, 3.0f, 0.7f, 0.34f, "A", 1, &g_radioVal);
    gX_GUI_Builder_Widget_Radio(&b, 0.95f, 3.0f, 0.7f, 0.34f, "B", 2, &g_radioVal);
    gX_GUI_Builder_Widget_Radio(&b, 1.75f, 3.0f, 0.7f, 0.34f, "C", 3, &g_radioVal);
    gX_GUI_Builder_Widget_DropDown(&b, 2.6f, 3.0f, 1.95f, 0.34f, "drop", &g_dropVal, 4);
    //opencode: stepper needs 2 x 0.6 button + value cell — 1.5 overflowed
    //   the increment button into the progress bar (theme-lab audit).
    gX_GUI_Builder_Widget_Stepper(&b, 0.15f, 3.5f, 2.0f, 0.34f, "step", &g_stepperVal, gX_DATATYPE_I32, 1, 0, 100);
    gX_GUI_Builder_Widget_ProgressBar(&b, 2.25f, 3.5f, 1.25f, 0.34f, &g_progressVal, 0.0f, 1.0f,
        gui.style.shared.hpGreen, true);
    gX_GUI_Builder_Widget_TextInput(&b, 3.6f, 3.5f, 1.1f, 0.34f,
        "tiName", g_inputBuf, (gX_U32)sizeof(g_inputBuf), 32, "name", false, NULL, NULL);

    //opencode: exact 3-item height (3 x 0.6) — no dead space under the list.
    gX_GUI_Builder_Widget_Section(&b, 0.1f, 4.0f, 4.6f, "Lists & tabs", 14);
    gX_GUI_Builder_Widget_ScrollableList(&b, 0.15f, 4.45f, 2.0f, 1.8f,
        g_listItems, 8, &g_listSel, &g_listScroll);
    //opencode: 2.4 wide — tab gutters ate the labels at 2.2 (audit).
    gX_GUI_Builder_Widget_TabPanel(&b, 2.25f, 4.45f, 2.4f, 1.2f, g_tabs, 3, &g_tabActive);

    //opencode: widget-lab row — the builders ThemLab never showed: spinner,
    //   texture button, raw-funcPtr action button. The DIALOG reset demo
    //   moved here; the flex playground already lives in guitester row 11.
    gX_Texture *labTex = (gX_Texture*)gX_RMHeap_CheckElseLoad(
        b.textureHeap, "texture/background/000.png", NULL);
    gX_GUI_Builder_Widget_Section(&b, 0.1f, 6.4f, 4.6f, "More widgets", 14);
    gX_GUI_Builder_Widget_Spinner(&b, 0.15f, 6.85f, 0.5f, 0.5f,
        gui.style.blueprint.accentColor);
    if (labTex) {
        gX_GUI_Builder_Widget_TextureButton(&b, 0.75f, 6.85f, 0.9f, 0.5f, labTex);
    }
    gX_GUI_Builder_Widget_ActionButton(&b, 1.75f, 6.85f, 1.2f, 0.5f,
        "APPLY", lab_apply_toast);
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 3.05f, 6.85f, 1.35f, 0.5f);
        gX_GUI_Builder_Color(&b, 140, 50, 60, 255);
        gX_GUI_Element_MakeClickable(b.currentElement);
        b.currentElement->state.FOCUSABLE = true;
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "DIALOG");
            b.currentElement->stateRendering.TEXT_CENTER_X = true;
            b.currentElement->stateRendering.TEXT_CENTER_Y = true;
            if (b.curText) { b.curText->fontSize = 12; b.curText->color = (gX_Color){255, 255, 255, 255}; }
        gX_GUI_Builder_Text_End(&b);
        gX_GUI_OnClick(b.currentElement, lab_open_reset_dialog, 0);
    gX_GUI_Builder_Element_End(&b);

    //--- RIGHT: blueprint preview panel ---
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 5.05f, 0.55f, 2.85f, 7.35f);
        gX_GUI_Builder_ColorV(&b, gui.style.a11y.surfaceOverlay);
        b.currentElement->stateRendering.RECT_NO_DRAW = true;
        gX_GUI_Builder_Shader_Begin(&b);
            gX_GUI_Builder_Shader_Frag(&b, "guisurface.frag");
            gX_GUI_Builder_Shader_Draw(&b, true);
            gX_GUI_Builder_Shader_Uniform(&b, 0, (gX_F32v4){
                gui.style.cornerRadius,
                gui.style.borderWidth,
                g_blueprintOn ? gui.style.blueprint.latticeAlpha : 0.0f,
                g_blueprintOn ? 1.0f : 0.0f});
            gX_GUI_Builder_Shader_Uniform(&b, 1, (gX_F32v4){
                gui.style.blueprint.inkColor.r / 255.0f,
                gui.style.blueprint.inkColor.g / 255.0f,
                gui.style.blueprint.inkColor.b / 255.0f, 1.0f});
            gX_GUI_Builder_Shader_Uniform(&b, 2, (gX_F32v4){
                gui.style.blueprint.latticeColor.r / 255.0f,
                gui.style.blueprint.latticeColor.g / 255.0f,
                gui.style.blueprint.latticeColor.b / 255.0f, 1.0f});
            gX_GUI_Builder_Shader_Uniform(&b, 3, (gX_F32v4){
                gui.style.blueprint.latticeCell, 0.0f, 0.0f, 0.0f});
        gX_GUI_Builder_Shader_End(&b);
    gX_GUI_Builder_Element_End(&b);

    //opencode: hero band + caption + title mockup, pulled up to close the
    //   dead gap the audit found between the swatch and the wordmark.
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 5.15f, 0.9f, 2.65f, 0.4f);
        gX_GUI_Builder_ColorV(&b, g_blueprintOn ? gui.style.blueprint.accentColor : gui.style.title2.heroBand);
    gX_GUI_Builder_Element_End(&b);
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 5.15f, 1.32f, 2.65f, 0.22f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        b.currentElement->stateRendering.RECT_NO_DRAW = true;
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "hero band");
            b.currentElement->stateRendering.TEXT_CENTER_X = true;
            if (b.curText) { b.curText->fontSize = 10; b.curText->color = gui.style.a11y.textOnCanvas; }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);
    lab_title_menu(&b, 5.55f, 2.45f, g_blueprintOn);

    //opencode: widened toggle ("blueprint: ON" clipped at 1.4) + one-line
    //   readouts so nothing truncates.
    gX_GUI_Builder_Widget_ToggleSwitch(&b, 5.2f, 3.85f, 2.2f, 0.36f, "blue", &g_blueprintOn);
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 5.2f, 4.3f, 2.6f, 0.3f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        gX_GUI_Builder_Text_Begin(&b);
            static gX_I8 readout[64];
            snprintf(readout, sizeof(readout), "ink %u,%u,%u",
                gui.style.blueprint.inkColor.r, gui.style.blueprint.inkColor.g, gui.style.blueprint.inkColor.b);
            gX_GUI_Builder_Text_Set(&b, readout);
            if (b.curText) { b.curText->fontSize = 11; b.curText->color = gui.style.blueprint.accentColor; }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 5.2f, 4.6f, 2.6f, 0.3f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        gX_GUI_Builder_Text_Begin(&b);
            static gX_I8 readout2[64];
            snprintf(readout2, sizeof(readout2), "accent %u,%u,%u",
                gui.style.blueprint.accentColor.r, gui.style.blueprint.accentColor.g, gui.style.blueprint.accentColor.b);
            gX_GUI_Builder_Text_Set(&b, readout2);
            if (b.curText) { b.curText->fontSize = 11; b.curText->color = gui.style.blueprint.accentColor; }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 5.2f, 4.9f, 2.6f, 0.3f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        gX_GUI_Builder_Text_Begin(&b);
            static gX_I8 readout3[64];
            snprintf(readout3, sizeof(readout3), "cell %.0fpx a=%.2f",
                gui.style.blueprint.latticeCell, gui.style.blueprint.latticeAlpha);
            gX_GUI_Builder_Text_Set(&b, readout3);
            if (b.curText) { b.curText->fontSize = 11; b.curText->color = gui.style.blueprint.latticeColor; }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    //opencode: swatches + surfaces shifted up — the old tail ran off-screen.
    {
        gX_Color sw[3] = { gui.style.blueprint.inkColor, gui.style.blueprint.accentColor, gui.style.blueprint.latticeColor };
        const gX_I8 *nm[3] = { "ink", "accent", "lattice" };
        for (gX_I32 si = 0; si < 3; si++) {
            gX_GUI_Builder_Element_Begin(&b);
                gX_GUI_Builder_RectGrid(&b, 5.2f + 0.85f * (gX_F32)si, 5.25f, 0.75f, 0.34f);
                gX_GUI_Builder_ColorV(&b, sw[si]);
                gX_GUI_Builder_Text_Begin(&b);
                    gX_GUI_Builder_Text_Set(&b, nm[si]);
                    b.currentElement->stateRendering.TEXT_CENTER_X = true;
                    b.currentElement->stateRendering.TEXT_CENTER_Y = true;
                    if (b.curText) { b.curText->fontSize = 10; b.curText->color = gui.style.a11y.textOnCanvas; }
                gX_GUI_Builder_Text_End(&b);
            gX_GUI_Builder_Element_End(&b);
        }
    }
    //opencode: a11y surface stack — chips + On- pair labels
    gX_GUI_Builder_Widget_Section(&b, 5.2f, 5.7f, 2.5f, "Surfaces", 12);
    lab_surface_chip(&b, 5.2f, 6.15f, gui.style.a11y.surfaceCanvas,  "canvas");
    lab_surface_chip(&b, 6.6f, 6.15f, gui.style.a11y.surfaceRaised,  "raised");
    lab_surface_chip(&b, 5.2f, 6.5f, gui.style.a11y.surfaceOverlay,  "overlay");
    lab_surface_chip(&b, 6.6f, 6.5f, gui.style.a11y.surfaceSunken,   "sunken");

    //opencode: text-card banner fills the reclaimed tail (a missing widget).
    gX_GUI_Builder_Widget_TextCard(&b, 5.2f, 6.95f, 2.5f, 0.6f, "gX");

    //opencode: lab widgets keep the "gui element" default description, which
    //   renders as a stray micro-caption on the focused widget — clear them.
    if (canvas && canvas->elements.data && canvas->elements.meta) {
        gX_GUI_Element *elems = (gX_GUI_Element*)canvas->elements.data;
        for (gX_U32 ei = 0; ei < canvas->elements.amount; ei++) {
            if (!canvas->elements.meta[ei].USED) continue;
            elems[ei].description[0] = '\0';
        }
    }

    gX_GUI_Builder_Free(&b);
}
