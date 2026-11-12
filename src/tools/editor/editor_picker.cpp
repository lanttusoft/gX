/**********************************************************************************************
*
*   [gX: src/tools/editor/editor_picker.cpp]
*
*   Copyright (c) 2025-2026 lanttu
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*
**********************************************************************************************/
//ai: GX-I043 — shared picker. The caller supplies the item list and accept
//   callback; this file owns search, keyboard/mouse selection and geometry.
#include "editor_picker.h"
#include "../tools_ui_scale.h"
#include "../../common/log.h"
#include "../../common/alloc.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../imgui/imgui.h"

static gX_I1 s_open = 0;
static gX_I1 s_pending = 0;
static gX_I1 s_traced = 0;
static gX_I8 s_id[GX_EDITOR_PICKER_ID];
static gX_I8 s_title[GX_EDITOR_PICKER_TITLE];
static gX_I8 s_hint[GX_EDITOR_PICKER_HINT];
static gX_I8 s_search[GX_EDITOR_PICKER_SEARCH];
typedef struct gX_EditorPickerEntry
{
    gX_I8 path[GX_EDITOR_PICKER_PATH];
    gX_I8 label[GX_EDITOR_PICKER_LABEL];
} gX_EditorPickerEntry;

static gX_EditorPickerEntry *s_entries = NULL;
static gX_I32 s_count = 0;
static gX_I8 s_selected[GX_EDITOR_PICKER_PATH];
static gX_I0 (*s_accept)(const gX_I8 *path, gX_I0 *user) = NULL;
static gX_I0 *s_user = NULL;

static gX_I0 editor_picker_clear(void)
{
    if (s_entries)
    {
        gX_Free((gX_I0 *)s_entries);
        s_entries = NULL;
    }
    s_count = 0;
}

static gX_I1 editor_picker_matches(const gX_I8 *path)
{
    if (!s_search[0]) return 1;
    return strstr((const char *)path, (const char *)s_search) != NULL ? 1 : 0;
}

static gX_I0 editor_picker_ellipsis(const gX_I8 *text, gX_F32 avail,
    gX_I8 *out, gX_I32 outSize)
{
    if (!text || !out || outSize <= 0) return;
    if (ImGui::CalcTextSize(text).x <= avail)
    {
        snprintf((char *)out, (size_t)outSize, "%s", text);
        return;
    }
    gX_I8 buf[GX_EDITOR_PICKER_PATH];
    snprintf((char *)buf, sizeof(buf), "%s", text);
    gX_I32 len = (gX_I32)strlen((const char *)buf);
    gX_F32 dots = ImGui::CalcTextSize("...").x;
    while (len > 0)
    {
        buf[len] = '\0';
        if (ImGui::CalcTextSize(buf).x + dots <= avail) break;
        len--;
    }
    snprintf((char *)out, (size_t)outSize, "%s...", buf);
}

static gX_I0 editor_picker_accept(const gX_I8 *path)
{
    if (!path || !path[0]) return;
    snprintf((char *)s_selected, sizeof(s_selected), "%s", path);
    gX_TraceLog(gX_LOG_INFO, "EDITOR_PICKER: accept id=%s path=%s",
        s_id, s_selected);
    gX_I0 (*accept)(const gX_I8 *, gX_I0 *) = s_accept;
    gX_I0 *user = s_user;
    s_open = 0;
    s_pending = 0;
    ImGui::CloseCurrentPopup();
    editor_picker_clear();
    if (accept) accept(s_selected, user);
}

gX_I0 gX_EditorPicker_Open(const gX_EditorPickerRequest *request)
{
    if (!request || !request->items || request->count <= 0) return;
    snprintf((char *)s_id, sizeof(s_id), "%s",
        request->id ? (const char *)request->id : "picker");
    snprintf((char *)s_title, sizeof(s_title), "%s",
        request->title ? (const char *)request->title : "Select");
    snprintf((char *)s_hint, sizeof(s_hint), "%s",
        request->searchHint ? (const char *)request->searchHint : "Search...");
    snprintf((char *)s_selected, sizeof(s_selected), "%s",
        request->selected ? (const char *)request->selected : "");
    editor_picker_clear();
    s_count = request->count;
    if (s_count > GX_EDITOR_PICKER_MAX_ITEMS)
        s_count = GX_EDITOR_PICKER_MAX_ITEMS;
    s_entries = (gX_EditorPickerEntry *)gX_Calloc((gX_U32)(
        s_count * (gX_I32)sizeof(gX_EditorPickerEntry)));
    if (!s_entries) {
        s_count = 0;
        s_open = 0;
        return;
    }
    for (gX_I32 i = 0; i < s_count; i++)
    {
        const gX_I8 *path = request->items[i].path;
        const gX_I8 *label = request->items[i].label;
        snprintf((char *)s_entries[i].path, sizeof(s_entries[i].path), "%s",
            path ? (const char *)path : "");
        snprintf((char *)s_entries[i].label, sizeof(s_entries[i].label), "%s",
            label && label[0] ? (const char *)label
                : (path ? (const char *)path : ""));
    }
    s_accept = request->accept;
    s_user = request->user;
    s_search[0] = '\0';
    s_open = 1;
    s_pending = 1;
    s_traced = 0;
    gX_TraceLog(gX_LOG_INFO, "EDITOR_PICKER: open id=%s items=%d",
        s_id, s_count);
}

gX_I1 gX_EditorPicker_IsOpen(gX_I0)
{
    return s_open;
}

gX_I0 gX_EditorPicker_Draw(gX_I0)
{
    if (!s_open) return;
    if (!ImGui::GetCurrentContext()) return;
    if (s_pending)
    {
        s_pending = 0;
        ImGui::OpenPopup((const char *)s_title);
    }
    ImGuiIO &io = ImGui::GetIO();
    gX_F32 width = gX_Tools_UI_ScaleWidth(560.0f);
    gX_F32 maxWidth = io.DisplaySize.x - 80.0f;
    if (width > maxWidth) width = maxWidth;
    if (width < 320.0f) width = 320.0f;
    gX_F32 height = gX_Tools_UI_ScaleWidth(420.0f);
    gX_F32 maxHeight = io.DisplaySize.y - 80.0f;
    if (height > maxHeight) height = maxHeight;
    if (height < 260.0f) height = 260.0f;
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 260.0f),
        ImVec2(io.DisplaySize.x - 20.0f, io.DisplaySize.y - 20.0f));
    if (!ImGui::BeginPopupModal((const char *)s_title, NULL,
            ImGuiWindowFlags_NoSavedSettings))
    {
        s_open = 0;
        editor_picker_clear();
        return;
    }
    if (!s_traced)
    {
        s_traced = 1;
        ImVec2 wmin = ImGui::GetWindowPos();
        ImVec2 wmax = ImVec2(wmin.x + ImGui::GetWindowSize().x,
            wmin.y + ImGui::GetWindowSize().y);
        gX_TraceLog(gX_LOG_INFO,
            "EDITOR_PICKER: geometry id=%s window=%.0f,%.0f,%.0f,%.0f items=%d",
            s_id, wmin.x, wmin.y, wmax.x, wmax.y, s_count);
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##editorPickerSearch", (const char *)s_hint,
        (char *)s_search, sizeof(s_search));
    gX_F32 footerH = ImGui::GetFrameHeightWithSpacing()
        + ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild("##editorPickerItems", ImVec2(0.0f, -footerH), true);
    gX_I32 matching = 0;
    for (gX_I32 i = 0; i < s_count; i++)
    {
        if (!editor_picker_matches(s_entries[i].path)) continue;
        matching++;
        gX_F32 avail = ImGui::GetContentRegionAvail().x;
        gX_I8 display[GX_EDITOR_PICKER_PATH];
        editor_picker_ellipsis(s_entries[i].label, avail, display, sizeof(display));
        gX_I1 selected = strcmp((const char *)s_selected,
            (const char *)s_entries[i].path) == 0;
        ImGui::PushID(i);
        if (ImGui::Selectable((const char *)display, selected,
                ImGuiSelectableFlags_AllowDoubleClick))
        {
            snprintf((char *)s_selected, sizeof(s_selected), "%s",
                s_entries[i].path);
            if (ImGui::IsMouseDoubleClicked(0))
                editor_picker_accept(s_entries[i].path);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", s_entries[i].path);
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)
                || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
                editor_picker_accept(s_entries[i].path);
        }
        if (getenv("GX_EDITOR_PICKER_TRACE"))
        {
            ImVec2 imin = ImGui::GetItemRectMin();
            ImVec2 imax = ImGui::GetItemRectMax();
            gX_TraceLog(gX_LOG_INFO,
                "EDITOR_PICKER: item id=%s path=%s avail=%.0f item=%.0f",
                s_id, s_entries[i].path, avail, imax.x - imin.x);
        }
        ImGui::PopID();
    }
    if (matching == 0) ImGui::TextDisabled("No matches.");
    ImGui::EndChild();
    ImGui::TextDisabled("Double-click or Enter to open.");
    ImGui::BeginDisabled(!s_selected[0]);
    if (ImGui::Button("Open"))
        editor_picker_accept(s_selected);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        gX_TraceLog(gX_LOG_INFO, "EDITOR_PICKER: cancel id=%s", s_id);
        s_open = 0;
        s_pending = 0;
        ImGui::CloseCurrentPopup();
        editor_picker_clear();
    }
    ImGui::EndPopup();
}
