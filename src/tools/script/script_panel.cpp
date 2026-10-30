/**********************************************************************************************
*
*   [gX: src/tools/script/script_panel.cpp]
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


//ai: scripting debugger panel — script list, pause state, breakpoint toggles
//   and Continue/Step/Over/Out controls. The host owns the pause (line
//   callback); this is chrome over the gX_Scripting_Debug_* API.
#include "../imgui/imgui.h"
#include "../tools.h"

#include <stdio.h>
#include <string.h>

#include "../../scripting.h"

#define GX_SCRIPT_PANEL_BP_INPUT_MAX 256

static gX_I32 gX_Tools_Script_BpLine[GX_SCRIPT_PANEL_BP_INPUT_MAX];

extern "C" gX_I0 gX_Tools_Script_Draw(gX_I0);
extern "C" gX_I0 gX_Tools_Script_Draw(gX_I0) {
    if (!g_scripting.loaded) return;

    gX_I1 panelOpen = (*gX_Tools_RuntimeScriptFlag()) != 0;
    ImGui::SetNextWindowSize(ImVec2(560, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Scripting - Debug", &panelOpen)) {
        ImGui::Text("failures %d | scripts %u | %s",
            gX_Scripting_Shell_FailCount(),
            gX_Scripting_ScriptCount(),
            gX_Scripting_Debug_AnyPaused() ? "paused" : "running");
        ImGui::TextDisabled("breakpoints pause the line callback; Continue passes the paused line once");
        ImGui::Separator();

        if (ImGui::BeginChild("##script_list")) {
            for (gX_U32 i = 0; i < gX_Scripting_ScriptCount(); i++) {
                const gX_I8 *path = gX_Scripting_ScriptPath(i);
                if (!path[0]) continue;

                ImGui::PushID((gX_I32)i);
                gX_I1 paused = gX_Scripting_Debug_IsPaused(i) != 0;
                if (paused) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.2f, 1.0f));
                ImGui::Text("%s [%u]%s", gX_Scripting_ScriptName(i), i, paused ? "  PAUSED" : "");
                if (paused) ImGui::PopStyleColor();

                ImGui::SameLine();
                if (ImGui::SmallButton("Continue")) gX_Scripting_Debug_Continue(i);
                ImGui::SameLine();
                if (ImGui::SmallButton("Step")) gX_Scripting_Debug_Step(i);
                ImGui::SameLine();
                if (ImGui::SmallButton("Over")) gX_Scripting_Debug_StepOver(i);
                ImGui::SameLine();
                if (ImGui::SmallButton("Out")) gX_Scripting_Debug_StepOut(i);

                gX_I32 line = gX_Scripting_Debug_GetLine(i);
                ImGui::TextDisabled("  line %d | %s", line, path);

                if (i < GX_SCRIPT_PANEL_BP_INPUT_MAX) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70);
                    ImGui::InputInt("##bp", &gX_Tools_Script_BpLine[i], 1, 0);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Toggle BP")) {
                        gX_Scripting_Debug_ToggleBreakpoint(i, gX_Tools_Script_BpLine[i]);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear BPs")) {
                        gX_Scripting_Debug_ClearBreakpoints(i);
                    }
                    ImGui::SameLine();
                    gX_I8 bpList[128] = {0};
                    for (gX_I32 l = 1; l < 512; l++) {
                        if (!gX_Scripting_Debug_HasBreakpoint(i, l)) continue;
                        gX_I8 piece[16];
                        snprintf(piece, sizeof(piece), "%s%d", bpList[0] ? ", " : "", l);
                        strncat(bpList, piece, sizeof(bpList) - strlen(bpList) - 1);
                    }
                    ImGui::TextDisabled("  bps: %s", bpList[0] ? bpList : "-");
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();

    if (!panelOpen) *gX_Tools_RuntimeScriptFlag() = 0;
}
