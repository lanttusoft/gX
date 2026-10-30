/*
  SPDX-License-Identifier: GPL-3.0-or-later
  Copyright (c) 2025-2026 lanttu, opencode

  This file is part of gX.
  gX is free software: you can redistribute it and/or modify it under the
  terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version.
  gX is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
  details.
  You should have received a copy of the GNU General Public License along with
  gX. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef IMGUI_GX_WIDGETS_H
#define IMGUI_GX_WIDGETS_H

//ai: small gradient-backed ImGui widgets for editor surfaces. Header-only so
//    the vendored ImGui tree stays untouched.
#include "imgui.h"

static inline ImU32 gX_Tools_ImGui_GradientShift(ImU32 color, float amount)
{
    ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
    value.x += amount;
    value.y += amount;
    value.z += amount;
    if (value.x > 1.0f) value.x = 1.0f;
    if (value.y > 1.0f) value.y = 1.0f;
    if (value.z > 1.0f) value.z = 1.0f;
    if (value.x < 0.0f) value.x = 0.0f;
    if (value.y < 0.0f) value.y = 0.0f;
    if (value.z < 0.0f) value.z = 0.0f;
    return ImGui::ColorConvertFloat4ToU32(value);
}

static inline void gX_Tools_ImGui_GradientRect(ImDrawList *draw,
    ImVec2 minimum, ImVec2 maximum, ImU32 top, ImU32 bottom)
{
    draw->AddRectFilledMultiColor(minimum, maximum, top, top, bottom, bottom);
}

static inline void gX_Tools_ImGui_GradientAccent(ImU32 start, ImU32 end,
    float height)
{
    //ai: draw inside the existing item-spacing gap; do not advance the cursor,
    //    so a decorative accent cannot move calibrated editor controls.
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec2 anchor = ImGui::GetCursorScreenPos();
    ImVec2 minimum = ImVec2(anchor.x,
        anchor.y - style.ItemSpacing.y + 1.0f);
    ImVec2 maximum = ImVec2(anchor.x + ImGui::GetContentRegionAvail().x,
        minimum.y + height);
    gX_Tools_ImGui_GradientRect(ImGui::GetWindowDrawList(), minimum, maximum,
        start, end);
}

static inline bool gX_Tools_ImGui_GradientButton(const char *label,
    ImVec2 size, ImU32 top, ImU32 bottom, ImU32 text)
{
    ImU32 textColor = text;
    if (textColor == 0)
        textColor = ImGui::GetColorU32(ImGuiCol_Text);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
    bool pressed = ImGui::Button(label, size);
    ImVec2 minimum = ImGui::GetItemRectMin();
    ImVec2 maximum = ImGui::GetItemRectMax();
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    bool focused = ImGui::IsItemFocused();
    ImU32 fillTop = top;
    ImU32 fillBottom = bottom;
    if (held) {
        fillTop = gX_Tools_ImGui_GradientShift(top, 0.10f);
        fillBottom = gX_Tools_ImGui_GradientShift(bottom, 0.10f);
    } else if (hovered) {
        fillTop = gX_Tools_ImGui_GradientShift(top, 0.05f);
        fillBottom = gX_Tools_ImGui_GradientShift(bottom, 0.05f);
    }
    ImDrawList *draw = ImGui::GetWindowDrawList();
    gX_Tools_ImGui_GradientRect(draw, minimum, maximum, fillTop, fillBottom);
    ImU32 border = focused ? IM_COL32(255, 208, 120, 255)
                           : IM_COL32(190, 172, 224, 110);
    draw->AddRect(minimum, maximum, border, 0.0f, 0,
        focused ? 2.0f : 1.0f);
    ImVec2 textSize = ImGui::CalcTextSize(label);
    ImVec2 textPosition = ImVec2(
        minimum.x + (maximum.x - minimum.x - textSize.x) * 0.5f,
        minimum.y + (maximum.y - minimum.y - textSize.y) * 0.5f);
    draw->AddText(textPosition, textColor, label);
    ImGui::PopStyleColor(4);
    return pressed;
}

#endif
