/**********************************************************************************************
*
*   [gX: src/console/console_panel.cpp]
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


//ai: ported from gameX src/gui/templates/imgui/console.cpp onto gX's tools
//   ImGui: log view with auto-scroll, input with history navigation (Up/Down)
//   and Tab completion of built-in commands.
#include "../tools/imgui/imgui.h"

#include <stdio.h>
#include <string.h>

#include "console.h"

static gX_I8 s_consoleInput[GX_CONSOLE_INPUT_MAX] = "";
static gX_I32 s_consoleHistoryIndex = -1;
static gX_I1 s_consoleScrollToBottom = 0;
static gX_I1 s_consoleFocusInput = 0;
static gX_I1 s_consoleWasOpen = 0;

static const gX_I8 *s_consoleCommands[] = {
    "/clear", "/cls", "/help", "/?", "/history", "/scene", "/scenes",
    "/msg", "/echo", "/time", "/stats", "/ls", "/reload", "/exec", "/run",
    "/scripts", "/state", NULL
};

static gX_I0 gX_Console_Panel_Complete(void) {
    size_t len = strlen(s_consoleInput);
    if (len == 0) return;
    for (gX_I32 i = 0; s_consoleCommands[i]; i++) {
        if (strncmp(s_consoleCommands[i], s_consoleInput, len) == 0 &&
            strcmp(s_consoleCommands[i], s_consoleInput) != 0) {
            snprintf(s_consoleInput, sizeof(s_consoleInput), "%s", s_consoleCommands[i]);
            return;
        }
    }
}

static gX_I0 gX_Console_Panel_History(gX_I32 direction) {
    if (gX_Console_GetHistoryCount() == 0) return;
    gX_I32 count = (gX_I32)gX_Console_GetHistoryCount();
    if (direction < 0) {
        if (s_consoleHistoryIndex < 0) s_consoleHistoryIndex = count - 1;
        else if (s_consoleHistoryIndex > 0) s_consoleHistoryIndex--;
    } else {
        if (s_consoleHistoryIndex < 0) return;
        s_consoleHistoryIndex++;
        if (s_consoleHistoryIndex >= count) {
            s_consoleHistoryIndex = -1;
            s_consoleInput[0] = '\0';
            return;
        }
    }
    if (s_consoleHistoryIndex >= 0) {
        gX_Console_GetHistoryLine((gX_U32)s_consoleHistoryIndex,
            s_consoleInput, sizeof(s_consoleInput));
    }
}

gX_I0 gX_Console_Draw(gX_I0) {
    if (!g_console.open) return;

    if (!s_consoleWasOpen) {
        s_consoleWasOpen = 1;
        s_consoleFocusInput = 1;
        s_consoleScrollToBottom = 1;
    }

    gX_I1 open = 1;
    ImGui::SetNextWindowSize(ImVec2(640, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Console (F10)", &open)) {
        if (ImGui::BeginChild("##console_log", ImVec2(0, -32))) {
            for (gX_U32 i = 0; i < gX_Console_GetLogCount(); i++) {
                gX_I8 text[GX_CONSOLE_LOG_LINE_MAX];
                if (gX_Console_GetLogLine(i, text, sizeof(text)) == 0) {
                    ImGui::TextUnformatted(text);
                }
            }
            if (s_consoleScrollToBottom) {
                ImGui::SetScrollHereY(1.0f);
                s_consoleScrollToBottom = 0;
            }
        }
        ImGui::EndChild();

        gX_I1 inputActive = false;
        if (s_consoleFocusInput) {
            ImGui::SetKeyboardFocusHere();
            s_consoleFocusInput = 0;
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##console_input", s_consoleInput, sizeof(s_consoleInput),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            gX_Console_Execute(s_consoleInput);
            s_consoleInput[0] = '\0';
            s_consoleHistoryIndex = -1;
            s_consoleScrollToBottom = 1;
            s_consoleFocusInput = 1;
        }
        inputActive = ImGui::IsItemActive();

        if (inputActive) {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) gX_Console_Panel_History(-1);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) gX_Console_Panel_History(1);
            if (ImGui::IsKeyPressed(ImGuiKey_Tab)) gX_Console_Panel_Complete();
        }
    }
    ImGui::End();

    if (!open) gX_Console_SetOpen(0);
}
