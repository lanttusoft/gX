/**********************************************************************************************
*
*   [gX: src/gui/templates/settings.c]
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


//opencode: settings template — builder API, binds settings including live theme presets.
#include <stdio.h>

#include "settings.h"
#include "template_header.h"

#include "../builder.h"
#include "../gui.h"
#include "../canvas.h"
#include "../element/element.h"

#include "../../settings.h"
#include "../../common/enum.h"
#include "../../common/log.h"
#if gX_ENABLE_SCENE == 1
    #include "../../scene/scene.h"
#endif
#include "../../main.h"

//opencode: tint a widget after the shared builder creates it, keeping its
//   hover animation synchronized with the new Settings accent surface.
static gX_I0 gX_GUI_Template_Settings_TintCurrent(gX_GUI_Builder *b, gX_Color color) {
    gX_GUI_Element_Color *hover;
    if (!b || !b->currentElement) return;
    b->currentElement->color = color;
    hover = gX_GUI_Element_Color_Get(b->currentElement, 0);
    if (hover) {
        hover->color = color;
        hover->colorOriginal = color;
    }
}

//opencode: settings keyboard-bind labels and compact two-column layout.
static const gX_I8 *gX_Settings_BindLabels[gX_BIND_COMMON_AMOUNT] = {
    "Move Fwd", "Move Back", "Move Left", "Move Right", "Move Up", "Move Down",
    "Rotate CW", "Rotate CCW", "Tilt More", "Tilt Less", "Zoom More", "Zoom Less",
    "FOV Zoom", "FOV Wide", "FOV Reset", "Speed Fast", "Speed Slow", "Cursor",
    "Confirm", "Cancel", "Menu"
};

static gX_GUI_BindCaptureContext gX_Settings_BindContexts[gX_BIND_COMMON_AMOUNT][2];

static const gX_I8 *gX_Settings_KeyLabel(gX_I32 key) {
    if (key < 0) return "--";
    return gX_Keys_String(key);
}

static gX_I0 gX_GUI_Template_Settings_BindLabel(gX_GUI_Builder *b,
    gX_F32 gx, gX_F32 gy, const gX_I8 *text) {
    gX_GUI_Builder_Element_Begin(b);
        gX_GUI_Builder_RectGrid(b, gx, gy, 1.6f, 0.34f);
        gX_GUI_Builder_Color(b, 0, 0, 0, 0);
        b->currentElement->stateRendering.RECT_NO_DRAW = true;
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, text);
            if (b->curText) {
                b->curText->fontSize = 11;
                b->curText->color = (gX_Color){190, 190, 215, 255};
            }
        gX_GUI_Builder_Text_End(b);
    gX_GUI_Builder_Element_End(b);
}

static gX_I0 gX_GUI_Template_Settings_BindButton(gX_GUI_Builder *b,
    gX_F32 gx, gX_F32 gy, gX_F32 gw, const gX_I8 *text,
    gX_GUI_BindCaptureContext *context) {
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
        gX_GUI_Builder_RectGrid(b, gx, gy, gw, 0.34f);
        gX_GUI_Builder_ColorV(b, gui.style.blueprint.accentColor);
        gX_GUI_Element_Template_Button(b->currentElement);
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, text);
            b->currentElement->stateRendering.TEXT_CENTER_X = true;
            b->currentElement->stateRendering.TEXT_CENTER_Y = true;
            if (b->curText) {
                b->curText->fontSize = 11;
                b->curText->color = gX_WHITE;
            }
        gX_GUI_Builder_Text_End(b);
        gX_GUI_Builder_Widget_AddHoverColor(b, gX_WHITE, 0.35f);
        gX_GUI_Builder_Widget_AddHoverClickSounds(b);
        gX_GUI_OnClick_Context(b->currentElement, gX_Widget_OnClick_CaptureBind,
            context, sizeof(*context));
        gX_GUI_Builder_Widget_AddButtonAnimations(b, 0, 0);
    gX_GUI_Builder_Element_End(b);
}

static gX_I0 gX_GUI_Template_Settings_Bindings(gX_GUI_Builder *b) {
    for (gX_I32 i = 0; i < gX_BIND_COMMON_AMOUNT; i++) {
        gX_I32 column = i / 11;
        gX_I32 row = i % 11;
        gX_F32 x = column == 0 ? 0.75f : 4.1f;
        gX_F32 y = 1.45f + (gX_F32)row * 0.43f;
        gX_GUI_Template_Settings_BindLabel(b, x, y, gX_Settings_BindLabels[i]);
        for (gX_I32 slot = 0; slot < 2; slot++) {
            gX_I8 keyLabel[32];
            const gX_I8 *key = gX_Settings_KeyLabel(settings.bindKeys[i][slot]);
            if (input.bindCaptureActive && input.bindCaptureIndex == (gX_U32)i
                && input.bindCaptureSlot == (gX_U32)slot) key = "PRESS KEY";
            snprintf(keyLabel, sizeof(keyLabel), "%s", key);
            gX_Settings_BindContexts[i][slot].bindIndex = (gX_U32)i;
            gX_Settings_BindContexts[i][slot].keySlot = (gX_U32)slot;
            gX_GUI_Template_Settings_BindButton(b, x + 1.35f + (gX_F32)slot * 0.8f,
                y, 0.75f, keyLabel, &gX_Settings_BindContexts[i][slot]);
        }
    }

    gX_GUI_Builder_Widget_Button(b, 1.1f, 6.62f, 1.8f, 0.42f,
        "Defaults", 1, &settings.resetFlag);
    gX_GUI_Builder_Widget_Button(b, 3.1f, 6.62f, 1.8f, 0.42f,
        "Save", 1, &settings.saveFlag);
    gX_GUI_Builder_Widget_TextButton(b, 5.1f, 6.62f, 1.8f, 0.42f,
        "Back", gui.style.title2.backBg, 0.35f,
        0, 0, gX_Widget_OnClick_BackToSettings, 0);
    gX_GUI_Template_Settings_TintCurrent(b, gui.style.blueprint.accentColor);
}


gX_I0 gX_GUI_Template_Settings(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_Settings(gX_I32 canvasindex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "Settings", "settings panel", gX_GUI_TEMPLATE_SETTINGS);

    //ai: hide the title footer/hints behind the settings modal.
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_Rect(&b, 0.0f, 0.0f, (gX_F32)b.width, (gX_F32)b.height);
        gX_GUI_Builder_Color(&b, 8, 6, 16, 232);
        b.currentElement->state.IGNORE_OCCLUSION = true;
    gX_GUI_Builder_Element_End(&b);

    //opencode: menu shell unity — frame settings like the saves/extras views.
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.5f, 0.5f, 7.0f, 7.2f);
        gX_GUI_Builder_ColorV(&b, gui.style.title2.savePanelBg);
    gX_GUI_Builder_Element_End(&b);

    //opencode: same centered header treatment as LOAD GAME / CREDITS.
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 1.2f, 0.85f, 5.6f, 0.5f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        b.currentElement->stateRendering.RECT_NO_DRAW = true;
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, settings.bindView ? "KEYBOARD BINDINGS" : "SETTINGS");
            b.currentElement->stateRendering.TEXT_CENTER_X = true;
            if (b.curText) { b.curText->fontSize = 20; b.curText->color = gui.style.title2.saveHeaderText; }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    if (settings.bindView) {
        gX_GUI_Template_Settings_Bindings(&b);
        gX_GUI_Builder_Free(&b);
        return;
    }

    //opencode: two-column reflow keeps the compact panel usable without
    //   shrinking the control text or pushing actions below the frame.
    gX_GUI_Builder_Widget_Section(&b, 0.8f, 1.65f, 3.0f, "Display", 16);
    gX_GUI_Builder_Widget_ToggleSwitch(&b, 0.8f, 2.25f, 3.0f, 0.45f,
        "Fullscreen", &settings.fullScreen);
    gX_GUI_Builder_Widget_Stepper(&b, 0.8f, 2.9f, 3.0f, 0.45f,
        "FPS", &settings.targetFPS, gX_DATATYPE_F32, 15.0f, 30.0f, 240.0f);
    {
        gX_I8 resLabel[64];
        snprintf(resLabel, sizeof(resLabel),
            "Resolution: %dx%d", settings.screenWidth, settings.screenHeight);
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_RectGrid(&b, 0.8f, 3.45f, 3.0f, 0.3f);
            gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
            b.currentElement->stateRendering.RECT_NO_DRAW = true;
            gX_GUI_Builder_Text_Begin(&b);
                gX_GUI_Builder_Text_Set(&b, resLabel);
                if (b.curText) { b.curText->fontSize = 14; b.curText->color = (gX_Color){150, 150, 180, 255}; }
            gX_GUI_Builder_Text_End(&b);
        gX_GUI_Builder_Element_End(&b);
    }

    gX_GUI_Builder_Widget_Section(&b, 4.1f, 1.65f, 3.0f, "Accessibility", 16);
    //ai: A1 — wire the real preference; screen flash/shake effects do not
    //   exist in gX, so the old dead toggles are gone.
    gX_GUI_Builder_Widget_ToggleSwitch(&b, 4.1f, 2.25f, 3.0f, 0.45f,
        "Reduce Motion", &settings.reduceMotion);
    gX_GUI_Builder_Widget_ToggleSwitch(&b, 4.1f, 2.8f, 3.0f, 0.45f,
        "Tool Nav", &settings.toolKeyboardNav);
    //ai: UX pass — "Tool scale: 1.000" truncated to "1..." in the slider box
    //   (the Sens precedent: the label shares the box with the value).
    gX_GUI_Builder_Widget_Slider(&b, 4.1f, 3.35f, 3.0f, 0.45f,
        "Scale", &settings.toolUiScale, 1.0f, 1.75f);

    gX_GUI_Builder_Widget_Section(&b, 0.8f, 3.9f, 3.0f, "Audio", 16);
    gX_GUI_Builder_Widget_Slider(&b, 0.8f, 4.5f, 3.0f, 0.5f,
        "Volume", &settings.masterVolume, 0.0f, 2.0f);
    gX_GUI_Builder_Widget_Section(&b, 0.8f, 5.02f, 3.0f, "UI Theme", 16);
    //opencode: the radio stack sat flush against the action row (the last
    //   row's rect reached y=632 against buttons at y=631, and the section
    //   header touched the first radio) — shift the block up one step so the
    //   rows keep a real gap at the lean floor.
    gX_GUI_Builder_Widget_Radio(&b, 0.8f, 5.44f, 3.0f, 0.3f,
        "Default", GX_SETTINGS_UI_THEME_DEFAULT, &settings.uiThemePreset);
    gX_GUI_Builder_Widget_Radio(&b, 0.8f, 5.77f, 3.0f, 0.3f,
        "High contrast", GX_SETTINGS_UI_THEME_HIGH_CONTRAST, &settings.uiThemePreset);
    gX_GUI_Builder_Widget_Radio(&b, 0.8f, 6.10f, 3.0f, 0.3f,
        "Color-safe", GX_SETTINGS_UI_THEME_COLOR_VISION, &settings.uiThemePreset);

    gX_GUI_Builder_Widget_Section(&b, 4.1f, 4.65f, 3.0f, "Controls", 16);
    gX_GUI_Builder_Widget_Slider(&b, 4.1f, 5.05f, 3.0f, 0.5f,
        //opencode: compact-column label — the full "Sensitivity: 3.000"
        //   string exceeded the slider text box at narrow panel width.
        "Sens", &settings.cameraSens, 0.5f, 10.0f);
    gX_GUI_Builder_Widget_ToggleSwitch(&b, 4.1f, 5.55f, 3.0f, 0.45f,
        "Invert Y", &settings.invertY);
    gX_GUI_Builder_Widget_Slider(&b, 4.1f, 6.0f, 3.0f, 0.5f,
        "FOV", &settings.cameraFov, 60.0f, 120.0f);

    //opencode: action row combines settings actions and the keyboard-bindings view.
    gX_GUI_Builder_Widget_Button(&b, 0.55f, 6.57f, 1.55f, 0.45f,
        "Defaults", 1, &settings.resetFlag);
    gX_GUI_Template_Settings_TintCurrent(&b, gui.style.blueprint.latticeColor);
    gX_GUI_Builder_Widget_TextButton(&b, 2.2f, 6.57f, 1.75f, 0.45f,
        "Key Bindings", gui.style.blueprint.accentColor, 0.35f,
        0, 0, gX_Widget_OnClick_OpenKeyBindings, 0);
    gX_GUI_Builder_Widget_Button(&b, 4.05f, 6.57f, 1.3f, 0.45f,
        "Apply", 1, &settings.applyFlag);
    gX_GUI_Template_Settings_TintCurrent(&b, gui.style.blueprint.accentColor);
    gX_GUI_Builder_Widget_Button(&b, 5.45f, 6.57f, 1.3f, 0.45f,
        "Save", 1, &settings.saveFlag);
    gX_GUI_Template_Settings_TintCurrent(&b, gui.style.blueprint.accentColor);

    // Back button — returns to escape menu
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 3.0f, 7.05f, 2.0f, 0.42f);
        gX_GUI_Builder_ColorV(&b, gui.style.title2.backBg);
        gX_GUI_Element_Template_Button(b.currentElement);
        gX_GUI_Element_MakeClickable(b.currentElement);
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "BACK");
            b.currentElement->stateRendering.TEXT_CENTER_X = true;
            b.currentElement->stateRendering.TEXT_CENTER_Y = true;
            if (b.curText) { b.curText->fontSize = 14; b.curText->color = gui.style.title2.backText; }
        gX_GUI_Builder_Text_End(&b);
        gX_GUI_OnClick(b.currentElement, gX_Widget_OnClick_BackToEscapeMenu, 0);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Element_End(&b);

    //ai: version lives inside the settings panel now; the title footer leaked over subviews.
    gX_GUI_Builder_Element_Begin(&b);
        gX_GUI_Builder_RectGrid(&b, 0.75f, 7.55f, 1.8f, 0.3f);
        gX_GUI_Builder_Color(&b, 0, 0, 0, 0);
        b.currentElement->stateRendering.RECT_NO_DRAW = true;
        gX_GUI_Builder_Text_Begin(&b);
            gX_GUI_Builder_Text_Set(&b, "v0.18.1 BETA");
            if (b.curText) { b.curText->fontSize = 10; b.curText->color = gui.style.title2.versionText; }
        gX_GUI_Builder_Text_End(&b);
    gX_GUI_Builder_Element_End(&b);

    gX_GUI_Builder_Free(&b);
}
