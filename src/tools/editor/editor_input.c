/**********************************************************************************************
*
*   [gX: src/tools/editor/editor_input.c]
*
*   Copyright (c) 2025-2026 lanttu
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*
**********************************************************************************************/
//ai: GX-I043 — central input-owner policy. The semantic probes still live at
//   their sources; this module combines them once per frame and keeps the
//   mouse, keyboard and text capture decisions consistent.
#include "editor_input.h"
#include "../../common/log.h"

#include <stdlib.h>

extern gX_I1 gX_Tools_RuntimeActive(gX_I0);
extern gX_I1 gX_Tools_BugReport_IsOpen(gX_I0);
extern gX_I1 gX_Tools_ImGui_WantsMouse(gX_I0);
extern gX_I1 gX_Tools_ImGui_WantsKeyboard(gX_I0);
extern gX_I1 gX_Tools_ImGui_WantsTextInput(gX_I0);
extern gX_I1 gX_EditorHost_Active(gX_I0);
extern gX_I1 gX_EditorPicker_IsOpen(gX_I0);

static gX_EditorInputOwner s_owner = GX_EDITOR_INPUT_GAME;
static const gX_I8 *s_reason = (const gX_I8 *)"game";
static gX_I1 s_captureMouse = 0;
static gX_I1 s_captureKeyboard = 0;
static gX_I1 s_captureText = 0;
static gX_EditorInputOwner s_tracedOwner = GX_EDITOR_INPUT_GAME;
static const gX_I8 *s_tracedReason = (const gX_I8 *)"game";
static gX_I1 s_traced = 0;

static const gX_I8 *editor_input_owner_name(gX_EditorInputOwner owner)
{
    switch (owner)
    {
        case GX_EDITOR_INPUT_NATIVE_GUI: return (const gX_I8 *)"native";
        case GX_EDITOR_INPUT_IMGUI: return (const gX_I8 *)"imgui";
        case GX_EDITOR_INPUT_MODAL: return (const gX_I8 *)"modal";
        case GX_EDITOR_INPUT_GAME:
        default: return (const gX_I8 *)"game";
    }
}

static gX_I0 editor_input_trace(gX_I0)
{
    const gX_I8 *trace = getenv("GX_EDITOR_INPUT_TRACE");
    if (!trace || !trace[0] || trace[0] == '0') return;
    if (s_traced && s_tracedOwner == s_owner && s_tracedReason == s_reason) return;
    s_traced = 1;
    s_tracedOwner = s_owner;
    s_tracedReason = s_reason;
    gX_TraceLog(gX_LOG_INFO,
        "EDITOR_INPUT: owner=%s reason=%s mouse=%d keyboard=%d text=%d",
        editor_input_owner_name(s_owner), s_reason,
        (gX_I32)s_captureMouse, (gX_I32)s_captureKeyboard,
        (gX_I32)s_captureText);
}

gX_I0 gX_EditorInput_BeginFrame(gX_I0)
{
    s_owner = GX_EDITOR_INPUT_GAME;
    s_reason = (const gX_I8 *)"game";
    s_captureMouse = 0;
    s_captureKeyboard = 0;
    s_captureText = 0;
}

gX_I0 gX_EditorInput_Set(gX_EditorInputOwner owner, const gX_I8 *reason)
{
    if (owner < s_owner) return;
    s_owner = owner;
    if (reason && reason[0]) s_reason = reason;
}

gX_I0 gX_EditorInput_Update(gX_I0)
{
    gX_EditorInput_BeginFrame();
    gX_I1 tools = (gX_Tools_RuntimeActive() || gX_EditorHost_Active()) ? 1 : 0;
    gX_I1 modal = 0;
    gX_I1 imguiMouse = gX_Tools_ImGui_WantsMouse() ? 1 : 0;
    gX_I1 imguiKeyboard = gX_Tools_ImGui_WantsKeyboard() ? 1 : 0;
    gX_I1 imguiText = gX_Tools_ImGui_WantsTextInput() ? 1 : 0;
    if (tools) gX_EditorInput_Set(GX_EDITOR_INPUT_IMGUI,
        (const gX_I8 *)"tools");
    if (imguiMouse || imguiKeyboard) gX_EditorInput_Set(
        GX_EDITOR_INPUT_IMGUI, (const gX_I8 *)"imgui");
    if (gX_EditorPicker_IsOpen()) {
        gX_EditorInput_Set(GX_EDITOR_INPUT_MODAL, (const gX_I8 *)"picker");
        modal = 1;
    }
    if (gX_Tools_BugReport_IsOpen()) {
        gX_EditorInput_Set(GX_EDITOR_INPUT_MODAL, (const gX_I8 *)"report");
        modal = 1;
    }
    s_captureMouse = (tools || modal || imguiMouse) ? 1 : 0;
    s_captureKeyboard = (modal || imguiKeyboard) ? 1 : 0;
    s_captureText = imguiText;
    editor_input_trace();
}

gX_EditorInputOwner gX_EditorInput_Owner(gX_I0)
{
    return s_owner;
}

const gX_I8 *gX_EditorInput_Reason(gX_I0)
{
    return s_reason;
}

gX_I1 gX_EditorInput_CapturesMouse(gX_I0)
{
    return s_captureMouse;
}

gX_I1 gX_EditorInput_CapturesKeyboard(gX_I0)
{
    return s_captureKeyboard;
}

gX_I1 gX_EditorInput_CapturesText(gX_I0)
{
    return s_captureText;
}
