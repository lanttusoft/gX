/**********************************************************************************************
*
*   [gX: src/tools/editor/editor_palette.cpp]
*
*   Copyright (c) 2025-2026 lanttu
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*
**********************************************************************************************/
//ai: GX-I043 — command palette. It is a thin view over gX_EditorCommands_*;
//   the shared picker owns search, keyboard/mouse selection and geometry.
#include "editor_palette.h"
#include "editor_commands.h"
#include "editor_picker.h"
#include "../../common/log.h"

#include <stdio.h>

extern "C" gX_I0 gX_GUI_Load_ImGui(gX_I0);
extern "C" gX_I0 gX_EditorHost_EnsureCommands(gX_I0);
extern "C" gX_I1 gX_EditorPicker_IsOpen(gX_I0);

static gX_I0 editor_palette_accept(const gX_I8 *path, gX_I0 *user)
{
    (void)user;
    if (!path || !path[0]) return;
    if (!gX_EditorCommands_Run(path))
        gX_TraceLog(gX_LOG_WARNING, "EDITOR_PALETTE: command failed %s", path);
}

gX_I0 gX_EditorPalette_Open(gX_I0)
{
    if (gX_EditorPicker_IsOpen()) return;
    gX_I32 count = gX_EditorCommands_Count();
    if (count <= 0) {
        gX_TraceLog(gX_LOG_INFO, "EDITOR_PALETTE: no commands");
        return;
    }
    gX_I8 labels[GX_EDITOR_COMMAND_MAX][192];
    gX_EditorPickerItem items[GX_EDITOR_COMMAND_MAX];
    gX_I32 shown = 0;
    for (gX_I32 i = 0; i < count && shown < GX_EDITOR_COMMAND_MAX; i++) {
        const gX_EditorCommand *command = gX_EditorCommands_At(i);
        if (!command || !gX_EditorCommands_Enabled(command->id)) continue;
        snprintf((char *)labels[shown], sizeof(labels[shown]), "%s%s%s",
            command->label ? (const char *)command->label : "?",
            command->shortcut ? "    " : "",
            command->shortcut ? (const char *)command->shortcut : "");
        items[shown].path = command->id;
        items[shown].label = labels[shown];
        shown++;
    }
    if (shown == 0) {
        gX_TraceLog(gX_LOG_INFO, "EDITOR_PALETTE: no enabled commands");
        return;
    }
    gX_EditorPickerRequest request = {
        "editor.palette",
        "Command palette",
        "Type a command...",
        items,
        shown,
        NULL,
        editor_palette_accept,
        NULL,
    };
    gX_EditorPicker_Open(&request);
    gX_TraceLog(gX_LOG_INFO, "EDITOR_PALETTE: open commands=%d", shown);
}

gX_I0 gX_EditorPalette_OpenGlobal(gX_I0)
{
    gX_GUI_Load_ImGui();
    gX_EditorHost_EnsureCommands();
    gX_EditorPalette_Open();
}
