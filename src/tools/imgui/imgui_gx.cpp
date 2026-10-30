/**********************************************************************************************
*
*   [gX: src/tools/imgui/imgui_gx.cpp]
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

//opencode: v0.4.0 — gX Tools suite: implements the three ImGui lifecycle
//   functions declared in gui.h (Load/Unload/Draw) that were previously
//   dead declarations. The vendored rlImGui backend is C++ — this file is
//   the C-linkage shim. The editor chrome (src/tools/editor/) runs on
//   ImGui so it does NOT depend on the gX GUI system being complete
//   (breaks the gui-creates-gui loop).
extern "C" {
#include "../../gui/gui.h"
#include "../../main.h"
#include "../../settings.h"
#include "../../common/log.h"
#include "../../input.h"
#include "../../rhi/gl_state.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_state.h"
#include "../../wrappers/wrappers_gl.h"
#include "../../gxmath/matrix.h"
}

#include "rlImGui.h"
#include "imgui.h"

#include <math.h>

extern "C" gX_I0 gX_Tools_Editor_Draw(gX_I0);
extern "C" gX_I0 gX_Tools_Theme_Draw(gX_I0);
extern "C" gX_I0 gX_Tools_Gfx_Draw(gX_I0);
extern "C" gX_I0 gX_Tools_Wavetable_Draw(gX_I0);
extern "C" gX_I1 *gX_Tools_RuntimeGfxFlag(gX_I0);
extern "C" gX_I1 *gX_Tools_RuntimeEditorFlag(gX_I0);
extern "C" gX_I1 *gX_Tools_RuntimeWavetableFlag(gX_I0);
extern "C" gX_I0 gX_Tools_Textwriter_Draw(gX_I0);
extern "C" gX_I1 *gX_Tools_RuntimeTextwriterFlag(gX_I0);
extern "C" gX_I0 gX_Tools_Physics_Draw(gX_I0);
extern "C" gX_I1 *gX_Tools_RuntimePhysicsFlag(gX_I0);
extern "C" gX_I1 *gX_Tools_RuntimeRailFlag(gX_I0);
extern "C" gX_I0 gX_Tools_Background_Draw(gX_I0);
extern "C" gX_I1 *gX_Tools_RuntimeBackgroundFlag(gX_I0);
extern "C" gX_I0 gX_Tools_Sky_Draw(gX_I0);
extern "C" gX_I1 *gX_Tools_RuntimeSkyFlag(gX_I0);
//opencode: the script-panel open flag stays kernel-side; the panel itself
//   draws from the gx_shell module via gX_Module_ToolsDraw().
extern "C" gX_I1 *gX_Tools_RuntimeScriptFlag(gX_I0);

#include "../../module/module.h"

static gX_I1 s_loaded = 0;
static gX_I1 s_themeMode = 0;
//opencode: immutable reference style lets user scale changes avoid cumulative
//   rounding from ImGuiStyle::ScaleAllSizes().
static ImGuiStyle s_imguiBaseStyle;
static gX_I1 s_imguiBaseStyleValid = 0;
static gX_I32 s_imguiThemeApplied = -1;
static gX_F32 s_imguiScaleRequested = 1.0f;
static gX_F32 s_imguiScaleApplied = 0.0f;
//opencode: current synthetic drag chord in the GX_AI_INPUT ImGui bridge.
static ImGuiKey s_aiModifierKey = ImGuiKey_None;
static ImGuiKey s_aiModifierAxis = ImGuiKey_None;
static ImGuiKey s_aiModifierCtrl = ImGuiKey_None;
static ImGuiKey s_aiKey = ImGuiKey_None;
static gX_I1 s_aiKeyShift = 0;
static gX_I1 s_aiKeyCtrl = 0;

//opencode: map GX_AI_INPUT raylib key codes to ImGui's named-key events.
static ImGuiKey gX_AI_Input_ImGuiKey(gX_I32 keyCode) {
    if (keyCode >= '0' && keyCode <= '9')
        return (ImGuiKey)(ImGuiKey_0 + keyCode - '0');
    if (keyCode >= 'A' && keyCode <= 'Z')
        return (ImGuiKey)(ImGuiKey_A + keyCode - 'A');
    if (keyCode >= 290 && keyCode <= 301)
        return (ImGuiKey)(ImGuiKey_F1 + keyCode - 290);
    switch (keyCode) {
        case 32: return ImGuiKey_Space;
        case 256: return ImGuiKey_Escape;
        case 257: return ImGuiKey_Enter;
        case 258: return ImGuiKey_Tab;
        case 259: return ImGuiKey_Backspace;
        case 260: return ImGuiKey_Insert;
        case 261: return ImGuiKey_Delete;
        case 262: return ImGuiKey_RightArrow;
        case 263: return ImGuiKey_LeftArrow;
        case 264: return ImGuiKey_DownArrow;
        case 265: return ImGuiKey_UpArrow;
        case 266: return ImGuiKey_PageUp;
        case 267: return ImGuiKey_PageDown;
        case 268: return ImGuiKey_Home;
        case 269: return ImGuiKey_End;
        case 320: return ImGuiKey_Keypad0;
        case 321: return ImGuiKey_Keypad1;
        case 322: return ImGuiKey_Keypad2;
        case 323: return ImGuiKey_Keypad3;
        case 324: return ImGuiKey_Keypad4;
        case 325: return ImGuiKey_Keypad5;
        case 326: return ImGuiKey_Keypad6;
        case 327: return ImGuiKey_Keypad7;
        case 328: return ImGuiKey_Keypad8;
        case 329: return ImGuiKey_Keypad9;
        case 330: return ImGuiKey_KeypadDecimal;
        case 331: return ImGuiKey_KeypadDivide;
        case 332: return ImGuiKey_KeypadMultiply;
        case 333: return ImGuiKey_KeypadSubtract;
        case 334: return ImGuiKey_KeypadAdd;
        case 335: return ImGuiKey_KeypadEnter;
        case 340: return ImGuiKey_LeftShift;
        case 341: return ImGuiKey_LeftCtrl;
        case 342: return ImGuiKey_LeftAlt;
        case 343: return ImGuiKey_LeftSuper;
        case 344: return ImGuiKey_RightShift;
        case 345: return ImGuiKey_RightCtrl;
        case 346: return ImGuiKey_RightAlt;
        case 347: return ImGuiKey_RightSuper;
        default: return ImGuiKey_None;
    }
}

//opencode: map the synthesized GX_AI_INPUT button index to ImGui's button enum.
static ImGuiMouseButton gX_AI_Input_ImGuiButton(gX_I32 button) {
    if (button == 1) return ImGuiMouseButton_Right;
    if (button == 2) return ImGuiMouseButton_Middle;
    return ImGuiMouseButton_Left;
}

//opencode: normalize tool-scale requests before they reach ImGui's style/font APIs.
static gX_F32 gX_Tools_ImGui_UIScale_Clamp(gX_F32 scale)
{
    if (!isfinite(scale) || scale < 1.0f) return 1.0f;
    if (scale > 1.75f) return 1.75f;
    return scale;
}

//opencode: apply the native preset's matching ImGui palette without replacing
//   the captured base style or changing the user's scale and layout metrics.
static gX_I0 gX_Tools_ImGui_Theme_Apply(gX_I0)
{
    if (!s_imguiBaseStyleValid || s_imguiThemeApplied == settings.uiThemePreset) return;
    ImGuiStyle &style = ImGui::GetStyle();
    for (gX_I32 i = 0; i < ImGuiCol_COUNT; i++)
        style.Colors[i] = s_imguiBaseStyle.Colors[i];
    //opencode: geometry must reset too — the color loop only restores colors.
    style.WindowRounding = s_imguiBaseStyle.WindowRounding;
    style.ChildRounding = s_imguiBaseStyle.ChildRounding;
    style.FrameRounding = s_imguiBaseStyle.FrameRounding;
    style.PopupRounding = s_imguiBaseStyle.PopupRounding;
    style.GrabRounding = s_imguiBaseStyle.GrabRounding;
    style.ScrollbarRounding = s_imguiBaseStyle.ScrollbarRounding;
    style.TabRounding = s_imguiBaseStyle.TabRounding;
    style.WindowBorderSize = s_imguiBaseStyle.WindowBorderSize;
    style.FrameBorderSize = s_imguiBaseStyle.FrameBorderSize;
    if (settings.uiThemePreset == GX_SETTINGS_UI_THEME_AERO) {
        //opencode: Frutiger Aero — glossy translucent blues, rounded glass,
        //   aqua accents. Keeps AA-friendly contrast on the text.
        style.WindowRounding = 7.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 5.0f;
        style.PopupRounding = 6.0f;
        style.GrabRounding = 5.0f;
        style.ScrollbarRounding = 6.0f;
        style.TabRounding = 5.0f;
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.Colors[ImGuiCol_Text] = ImVec4(0.97f, 0.99f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.62f, 0.74f, 0.82f, 1.0f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.72f, 0.85f, 0.95f, 0.94f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.80f, 0.90f, 0.98f, 0.55f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.78f, 0.89f, 0.97f, 0.96f);
        style.Colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.65f);
        style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.2f, 0.4f, 0.6f, 0.18f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.62f, 0.78f, 0.90f, 0.75f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.55f, 0.75f, 0.92f, 0.85f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.45f, 0.70f, 0.90f, 0.95f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.45f, 0.66f, 0.86f, 0.92f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.30f, 0.58f, 0.84f, 0.96f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.62f, 0.80f, 0.94f, 0.85f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.10f, 0.85f, 0.80f, 1.0f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.20f, 0.80f, 0.85f, 1.0f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.10f, 0.95f, 0.85f, 1.0f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.35f, 0.65f, 0.88f, 0.88f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.42f, 0.74f, 0.95f, 0.95f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 0.62f, 0.90f, 1.0f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.40f, 0.70f, 0.92f, 0.75f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.46f, 0.78f, 0.96f, 0.88f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.66f, 0.92f, 0.95f);
        style.Colors[ImGuiCol_Separator] = ImVec4(0.98f, 0.99f, 1.0f, 0.55f);
        style.Colors[ImGuiCol_Tab] = ImVec4(0.45f, 0.70f, 0.90f, 0.80f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.55f, 0.80f, 0.97f, 0.92f);
        style.Colors[ImGuiCol_TabSelected] = ImVec4(0.30f, 0.62f, 0.88f, 0.95f);
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.70f, 0.84f, 0.94f, 0.45f);
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.45f, 0.72f, 0.92f, 0.90f);
        style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.10f, 0.90f, 0.82f, 1.0f);
        style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.30f, 0.75f, 0.95f, 0.55f);
    }
    if (settings.uiThemePreset == GX_SETTINGS_UI_THEME_HIGH_CONTRAST) {
        style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.78f, 0.78f, 0.78f, 1.0f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.0f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.86f, 0.86f, 0.86f, 1.0f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.0f, 0.28f, 0.42f, 1.0f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.0f, 0.38f, 0.56f, 1.0f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.0f, 0.20f, 0.32f, 1.0f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.02f, 0.02f, 0.02f, 1.0f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.92f, 0.0f, 1.0f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.0f, 0.92f, 0.0f, 1.0f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.0f, 0.75f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.0f, 0.22f, 0.34f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.0f, 0.36f, 0.52f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.0f, 0.48f, 0.68f, 1.0f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.0f, 0.22f, 0.34f, 1.0f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.36f, 0.52f, 1.0f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.0f, 0.48f, 0.68f, 1.0f);
        style.Colors[ImGuiCol_NavHighlight] = ImVec4(1.0f, 0.92f, 0.0f, 1.0f);
    } else if (settings.uiThemePreset == GX_SETTINGS_UI_THEME_COLOR_VISION) {
        style.Colors[ImGuiCol_Text] = ImVec4(0.96f, 0.96f, 0.94f, 1.0f);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.68f, 0.73f, 0.78f, 1.0f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.08f, 0.13f, 1.0f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.12f, 0.18f, 1.0f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.04f, 0.07f, 0.12f, 1.0f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.30f, 0.40f, 0.50f, 1.0f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.14f, 0.22f, 1.0f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.10f, 0.34f, 0.48f, 1.0f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.0f, 0.32f, 0.50f, 1.0f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.07f, 0.12f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.0f, 0.22f, 0.34f, 1.0f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.06f, 0.10f, 0.16f, 1.0f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.90f, 0.62f, 0.0f, 1.0f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.71f, 0.91f, 1.0f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.90f, 0.62f, 0.0f, 1.0f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.0f, 0.30f, 0.46f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.0f, 0.42f, 0.62f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.58f, 0.36f, 0.0f, 1.0f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.0f, 0.30f, 0.46f, 1.0f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.42f, 0.62f, 1.0f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.58f, 0.36f, 0.0f, 1.0f);
        style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.90f, 0.62f, 0.0f, 1.0f);
    }
    s_imguiThemeApplied = settings.uiThemePreset;
    gX_TraceLog(gX_LOG_INFO, "GX_UI_THEME: applied preset=%d frameRound=%.1f "
        "windowBg=(%.2f,%.2f,%.2f,%.2f)", settings.uiThemePreset,
        (gX_F64)style.FrameRounding,
        (gX_F64)style.Colors[ImGuiCol_WindowBg].x,
        (gX_F64)style.Colors[ImGuiCol_WindowBg].y,
        (gX_F64)style.Colors[ImGuiCol_WindowBg].z,
        (gX_F64)style.Colors[ImGuiCol_WindowBg].w);
}

//opencode: apply all ImGui accessibility settings before NewFrame so layout,
//   font measurement, and pointer hit boxes use the same scale.
static gX_I0 gX_Tools_ImGui_Accessibility_Apply(gX_I0)
{
    if (!s_loaded || !ImGui::GetCurrentContext()) return;
    if (s_imguiBaseStyleValid && fabsf(s_imguiScaleApplied - s_imguiScaleRequested) > 0.001f) {
        ImGui::GetStyle() = s_imguiBaseStyle;
        ImGui::GetStyle().ScaleAllSizes(s_imguiScaleRequested);
        ImGui::GetStyle().FontScaleMain = s_imguiScaleRequested;
        s_imguiScaleApplied = s_imguiScaleRequested;
        s_imguiThemeApplied = -1;
    }
    gX_Tools_ImGui_Theme_Apply();
    ImGuiIO &io = ImGui::GetIO();
    if (settings.toolKeyboardNav)
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    else
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
}

//opencode: apply persistent editor accessibility preferences through ImGui's
//   normal style path; native game UI continues using its own theme tokens.
extern "C" gX_I0 gX_Tools_ImGui_UIScale_Request(gX_F32 scale)
{
    s_imguiScaleRequested = gX_Tools_ImGui_UIScale_Clamp(scale);
}

//opencode: Frutiger Aero v2 — per-panel vertical gradient + gloss band. Call
//   right after a window's Begin (before content) so widgets draw on top; a
//   no-op outside the Aero theme.
extern "C" gX_I1 gX_Tools_ImGui_Aero_Panel(gX_I0)
{
    if (settings.uiThemePreset != GX_SETTINGS_UI_THEME_AERO) return 0;
    ImVec2 window = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    gX_F32 contentTop = ImGui::GetCursorScreenPos().y;
    ImVec2 min = ImVec2(window.x, contentTop);
    ImVec2 max = ImVec2(window.x + size.x, window.y + size.y);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImU32 top = IM_COL32(216, 238, 252, 235);
    ImU32 bottom = IM_COL32(148, 188, 222, 235);
    draw->AddRectFilledMultiColor(min, max, top, top, bottom, bottom);
    ImU32 glossTop = IM_COL32(255, 255, 255, 96);
    ImU32 glossBottom = IM_COL32(255, 255, 255, 0);
    ImVec2 glossMax = ImVec2(max.x,
        contentTop + (max.y - contentTop) * 0.32f);
    draw->AddRectFilledMultiColor(min, glossMax, glossTop, glossTop,
        glossBottom, glossBottom);
    static gX_I1 traced = 0;
    if (!traced) {
        gX_TraceLog(gX_LOG_INFO, "GFX_AERO_UI: panel=%dx%d gloss=1",
            (gX_I32)size.x, (gX_I32)size.y);
        traced = 1;
    }
    return 1;
}

//opencode: Frutiger Aero v2 — horizontal gradient behind the main menu bar.
//   Call right after BeginMainMenuBar so the items draw on top.
extern "C" gX_I0 gX_Tools_ImGui_Aero_MenuBar(gX_I0)
{
    if (settings.uiThemePreset != GX_SETTINGS_UI_THEME_AERO) return;
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    ImU32 left = IM_COL32(70, 136, 198, 244);
    ImU32 right = IM_COL32(176, 224, 248, 244);
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(pos,
        ImVec2(pos.x + size.x, pos.y + size.y), left, right, right, left);
}

//opencode: shared menu exposes accessibility settings, live theme preview, and persistence.
extern "C" gX_I0 gX_Tools_ImGui_AccessibilityMenu(gX_I0)
{
    if (!ImGui::BeginMenu("Accessibility")) return;
    gX_F32 scalePercent = settings.toolUiScale * 100.0f;
    if (ImGui::SliderFloat("Tool UI scale", &scalePercent, 100.0f, 175.0f, "%.0f%%")) {
        settings.toolUiScale = gX_Tools_ImGui_UIScale_Clamp(scalePercent * 0.01f);
        settings.dirtyFlag = 1;
        gX_Tools_ImGui_UIScale_Request(settings.toolUiScale);
    }
    bool keyboardNav = settings.toolKeyboardNav != 0;
    if (ImGui::Checkbox("Keyboard navigation", &keyboardNav)) {
        settings.toolKeyboardNav = keyboardNav ? 1 : 0;
        settings.dirtyFlag = 1;
    }
    ImGui::TextDisabled("Tab, arrows, and Enter navigate tool controls.");
    bool reduceMotion = settings.reduceMotion != 0;
    if (ImGui::Checkbox("Reduce motion", &reduceMotion)) {
        settings.reduceMotion = reduceMotion ? 1 : 0;
        settings.dirtyFlag = 1;
    }
    ImGui::TextDisabled("Snap transitions and pause fades instead of animating.");
    ImGui::Separator();
    ImGui::TextUnformatted("Theme preview");
    if (ImGui::MenuItem("Default", NULL,
            settings.uiThemePreset == GX_SETTINGS_UI_THEME_DEFAULT)) {
        settings.uiThemePreset = GX_SETTINGS_UI_THEME_DEFAULT;
        settings.dirtyFlag = 1;
    }
    if (ImGui::MenuItem("High contrast", NULL,
            settings.uiThemePreset == GX_SETTINGS_UI_THEME_HIGH_CONTRAST)) {
        settings.uiThemePreset = GX_SETTINGS_UI_THEME_HIGH_CONTRAST;
        settings.dirtyFlag = 1;
    }
    if (ImGui::MenuItem("Color vision friendly", NULL,
            settings.uiThemePreset == GX_SETTINGS_UI_THEME_COLOR_VISION)) {
        settings.uiThemePreset = GX_SETTINGS_UI_THEME_COLOR_VISION;
        settings.dirtyFlag = 1;
    }
    if (ImGui::MenuItem("Frutiger Aero", NULL,
            settings.uiThemePreset == GX_SETTINGS_UI_THEME_AERO)) {
        settings.uiThemePreset = GX_SETTINGS_UI_THEME_AERO;
        settings.dirtyFlag = 1;
    }
    ImGui::TextDisabled("Selection previews immediately; save settings to keep it.");
    if (ImGui::MenuItem("Reset UI scale to 100%")) {
        settings.toolUiScale = 1.0f;
        settings.dirtyFlag = 1;
        gX_Tools_ImGui_UIScale_Request(settings.toolUiScale);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Save settings", NULL, false, settings.dirtyFlag != 0))
        gX_Settings_Save();
    ImGui::TextDisabled("Preferences: resources/settings.json");
    ImGui::EndMenu();
}

//opencode: the vendored rlImGui renderer consumes raylib's legacy matrix
//   state, while GFX leaves that state on the active 3D camera for model draws.
//   Give ImGui an explicit 2D scope and let GL_State restore the renderer state.
static gX_I0 gX_GUI_ImGui_Draw2D(gX_I0) {
    gX_GL_State_Push();
    gX_GL_Matrix_SetProjection(gX_Matrix_Ortho(
        0.0f, (gX_F32)maindata.windowWidth,
        (gX_F32)maindata.windowHeight, 0.0f, 0.0f, 1.0f));
    gX_GL_Matrix_SetModelView(gX_Matrix_Identity());
    gX_RHI_Disable(gX_RHI_DEPTH_TEST);
    gX_RHI_Blend_Mode(gX_RHI_BLEND_ALPHA);
    gX_Tools_ImGui_Accessibility_Apply();
    rlImGuiBegin();
    //opencode: GX_AI_INPUT bridge — while scripted, the backend poll is
    //   suppressed and this posts synth pos, mouse edges, and drag modifiers.
    {
        static gX_I1 s_prevDown = 0;
        static gX_I32 s_prevButton = 0;
        gX_F32 sx = 0, sy = 0;
        gX_I1 sdown = 0;
        gX_I32 button = 0;
        gX_I32 modifierKey = 0, modifierAxis = 0;
        gX_I32 keyCode = 0;
        gX_I1 modifierCtrl = 0, modifierDown = 0;
        gX_I1 keyDown = 0, keyShift = 0, keyCtrl = 0;
        ImGuiIO &sio = ImGui::GetIO();
        if (gX_AI_Input_Synth(&sx, &sy, &sdown, &button,
                &modifierKey, &modifierAxis, &modifierCtrl, &modifierDown,
                &keyCode, &keyDown, &keyShift, &keyCtrl)) {
            gX_Rectangle back = gX_GUI_GameToScreen((gX_Rectangle){sx, sy, 0, 0});
            sio.AddMousePosEvent(back.x, back.y);
            //opencode: post release/transition edges for the synthesized button
            //   so right/middle camera gestures reach ImGui's drag state.
            if (s_prevDown && (button != s_prevButton || !sdown))
                sio.AddMouseButtonEvent(gX_AI_Input_ImGuiButton(s_prevButton), false);
            if (sdown && (!s_prevDown || button != s_prevButton))
                sio.AddMouseButtonEvent(gX_AI_Input_ImGuiButton(button), true);
            s_prevDown = sdown;
            s_prevButton = button;
            ImGuiKey nextModifierKey = modifierDown
                ? gX_AI_Input_ImGuiKey(modifierKey) : ImGuiKey_None;
            ImGuiKey nextModifierAxis = modifierDown
                ? gX_AI_Input_ImGuiKey(modifierAxis) : ImGuiKey_None;
            ImGuiKey nextModifierCtrl = modifierDown && modifierCtrl
                ? ImGuiMod_Ctrl : ImGuiKey_None;
            if (nextModifierKey != s_aiModifierKey) {
                if (s_aiModifierKey != ImGuiKey_None) sio.AddKeyEvent(s_aiModifierKey, false);
                if (nextModifierKey != ImGuiKey_None) sio.AddKeyEvent(nextModifierKey, true);
                s_aiModifierKey = nextModifierKey;
            }
            if (nextModifierAxis != s_aiModifierAxis) {
                if (s_aiModifierAxis != ImGuiKey_None) sio.AddKeyEvent(s_aiModifierAxis, false);
                if (nextModifierAxis != ImGuiKey_None) sio.AddKeyEvent(nextModifierAxis, true);
                s_aiModifierAxis = nextModifierAxis;
            }
            if (nextModifierCtrl != s_aiModifierCtrl) {
                if (s_aiModifierCtrl != ImGuiKey_None) sio.AddKeyEvent(s_aiModifierCtrl, false);
                if (nextModifierCtrl != ImGuiKey_None) sio.AddKeyEvent(nextModifierCtrl, true);
                s_aiModifierCtrl = nextModifierCtrl;
            }
            ImGuiKey nextKey = keyDown ? gX_AI_Input_ImGuiKey(keyCode) : ImGuiKey_None;
            if (nextKey != s_aiKey) {
                if (s_aiKey != ImGuiKey_None) sio.AddKeyEvent(s_aiKey, false);
                if (nextKey != ImGuiKey_None) sio.AddKeyEvent(nextKey, true);
                s_aiKey = nextKey;
            }
            if (keyShift != s_aiKeyShift) {
                sio.AddKeyEvent(ImGuiMod_Shift, keyShift != 0);
                s_aiKeyShift = keyShift;
            }
            if (keyCtrl != s_aiKeyCtrl) {
                sio.AddKeyEvent(ImGuiMod_Ctrl, keyCtrl != 0);
                s_aiKeyCtrl = keyCtrl;
            }
        } else {
            if (s_aiModifierKey != ImGuiKey_None) sio.AddKeyEvent(s_aiModifierKey, false);
            if (s_aiModifierAxis != ImGuiKey_None) sio.AddKeyEvent(s_aiModifierAxis, false);
            if (s_aiModifierCtrl != ImGuiKey_None) sio.AddKeyEvent(s_aiModifierCtrl, false);
            s_aiModifierKey = ImGuiKey_None;
            s_aiModifierAxis = ImGuiKey_None;
            s_aiModifierCtrl = ImGuiKey_None;
            if (s_aiKey != ImGuiKey_None) sio.AddKeyEvent(s_aiKey, false);
            if (s_aiKeyShift) sio.AddKeyEvent(ImGuiMod_Shift, false);
            if (s_aiKeyCtrl) sio.AddKeyEvent(ImGuiMod_Ctrl, false);
            s_aiKey = ImGuiKey_None;
            s_aiKeyShift = 0;
            s_aiKeyCtrl = 0;
        }
    }
    {
        if (*gX_Tools_RuntimeScriptFlag() || *gX_Tools_RuntimeRailFlag())
            gX_Module_ToolsDraw();
        else if (*gX_Tools_RuntimeGfxFlag())
            gX_Tools_Gfx_Draw();
        else if (*gX_Tools_RuntimeWavetableFlag())
            gX_Tools_Wavetable_Draw();
        else if (*gX_Tools_RuntimeTextwriterFlag())
            gX_Tools_Textwriter_Draw();
        else if (*gX_Tools_RuntimePhysicsFlag())
            gX_Tools_Physics_Draw();
        else if (*gX_Tools_RuntimeBackgroundFlag())
            gX_Tools_Background_Draw();
        else if (*gX_Tools_RuntimeSkyFlag())
            gX_Tools_Sky_Draw();
        else if (s_themeMode)
            gX_Tools_Theme_Draw();
        else if (*gX_Tools_RuntimeEditorFlag()
                 || maindata.masterIndex == gX_SCENE_GUIPLAYGROUND)
            gX_Tools_Editor_Draw();
        //opencode: module overlays (shell console) draw on top whenever open.
        gX_Module_OverlayDraw();
    }
    rlImGuiEnd();
    gX_GL_State_Pop();
}

extern "C" gX_I0 gX_GUI_Load_ImGui(gX_I0) {
    if (s_loaded) return;
    rlImGuiSetup(true);
    s_imguiBaseStyle = ImGui::GetStyle();
    s_imguiBaseStyleValid = 1;
    s_imguiThemeApplied = -1;
    s_imguiScaleApplied = 0.0f;
    s_imguiScaleRequested = gX_Tools_ImGui_UIScale_Clamp(settings.toolUiScale);
    //opencode: disable the ini persistence — a stale imgui.ini (e.g. from an
    //   earlier default-sized run) can pin a clipped window layout that the
    //   editor's fixed size then fights every frame
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    s_loaded = 1;
    gX_TraceLog(gX_LOG_INFO, "Tools: ImGui loaded (gX Tools suite)");
}

extern "C" gX_I0 gX_GUI_Unload_ImGui(gX_I0) {
    if (!s_loaded) return;
    rlImGuiShutdown();
    s_loaded = 0;
    s_imguiBaseStyleValid = 0;
    s_imguiThemeApplied = -1;
    s_imguiScaleApplied = 0.0f;
}

extern "C" gX_I0 gX_GUI_Draw_ImGui(gX_I0) {
    if (!s_loaded) return;
    gX_Tools_ImGui_UIScale_Request(settings.toolUiScale);
    //opencode: v0.6.9 — fullscreen viewport + stable DisplaySize for ImGui.
    //   Backend DisplaySize flaps across resizes (jumping/clipping panels);
    //   maindata window dims are synchronous (set with SetWindowSize). Game
    //   phase uses game-rect viewport (pillarbox); ImGui needs full window
    //   (margins drawable). rlImGuiBegin inherits GL viewport, so set it.
    {
        extern gX_I0 gX_RHI_Viewport(gX_I32 x, gX_I32 y, gX_I32 w, gX_I32 h);
        gX_RHI_Viewport(0, 0, (gX_I32)maindata.windowWidth, (gX_I32)maindata.windowHeight);
        ImGuiIO &dio = ImGui::GetIO();
        dio.DisplaySize = ImVec2((float)maindata.windowWidth, (float)maindata.windowHeight);
    }
    gX_GUI_ImGui_Draw2D();
}

extern "C" gX_I0 gX_Tools_SetImGuiThemeMode(gX_I1 on);
extern "C" gX_I0 gX_Tools_SetImGuiThemeMode(gX_I1 on) {
    s_themeMode = on ? 1 : 0;
}
