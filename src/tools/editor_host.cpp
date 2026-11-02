/**********************************************************************************************
*
*   [gX: src/tools/editor_host.cpp]
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
*
*   gX is distributed in the hope that it will be useful, but WITHOUT ANY
*   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
*   FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
*   details.
*
*   You should have received a copy of the GNU General Public License along
*   with gX. If not, see <https://www.gnu.org/licenses/>.
*
*
**********************************************************************************************/

//opencode: global editor host — menu-bar root and editor switcher. The
//   toolbar is always summonable (F9) from any scene, editor or non-editor.
//   This slice hosts the existing tool windows; the workspace/panel migration
//   is the next step (see private-docs/post-gfx-plan.md, H2).

extern "C" {
#include "../main.h"
#include "../gui/gui.h"
#include "../common/log.h"
}

#include "imgui/imgui.h"
#include "tools.h"

#include <stdlib.h>
#include <string.h>

static gX_I1 s_hostOpen = 0;
static gX_I1 s_inited = 0;
static gX_I1 s_aboutOpen = 0;
static gX_I1 s_shortcutsOpen = 0;

static gX_I0 host_trace(gX_I1 open) {
    gX_TraceLog(gX_LOG_INFO, "EDITOR_HOST: open=%d", (gX_I32)open);
}

static gX_I0 host_init_env(gX_I0) {
    if (s_inited) return;
    s_inited = 1;
    const gX_I8 *openEnv = getenv("GX_EDITOR_HOST");
    if (openEnv && openEnv[0] && openEnv[0] != '0') {
        s_hostOpen = 1;
        host_trace(1);
    }
}

extern "C" gX_I0 gX_EditorHost_SetOpen(gX_I1 open) {
    host_init_env();
    open = open ? 1 : 0;
    if (s_hostOpen == open) return;
    s_hostOpen = open;
    host_trace(open);
}

extern "C" gX_I0 gX_EditorHost_Toggle(gX_I0) {
    host_init_env();
    gX_EditorHost_SetOpen(s_hostOpen ? 0 : 1);
}

extern "C" gX_I1 gX_EditorHost_IsOpen(gX_I0) {
    host_init_env();
    return s_hostOpen;
}

static const gX_I8 *host_active_editor(void) {
    if (*gX_Tools_RuntimeSkyFlag()) return "Sky (alpha/WIP)";
    if (*gX_Tools_RuntimeGfxFlag()) return "GFX (beta)";
    if (*gX_Tools_RuntimeBackgroundFlag()) return "Background (alpha/WIP)";
    if (*gX_Tools_RuntimePhysicsFlag()) return "Physics (alpha/WIP)";
    if (*gX_Tools_RuntimeWavetableFlag()) return "Wavetable (alpha/WIP)";
    if (*gX_Tools_RuntimeTextwriterFlag()) return "Textwriter (alpha/WIP)";
    if (*gX_Tools_RuntimeEditorFlag()) return "GUI (alpha/WIP)";
    if (*gX_Tools_RuntimeThemeFlag()) return "Theme (alpha/WIP)";
    return "No editor";
}

static gX_I0 host_open_sky(void) {
    extern gX_I0 gX_Tools_OpenSky_Runtime(gX_I0);
    gX_Tools_OpenSky_Runtime();
    gX_EditorHost_SetOpen(1);
}

static gX_I0 host_close_editor(void) {
    extern gX_I0 gX_Tools_Close_Runtime(gX_I0);
    gX_Tools_Close_Runtime();
}

static gX_I0 host_draw_menu_bar(void) {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("gX")) {
        if (ImGui::BeginMenu("Change Editor")) {
            if (ImGui::MenuItem("Sky", "alpha/WIP")) host_open_sky();
            ImGui::MenuItem("GFX", "beta - migration pending", false, false);
            ImGui::Separator();
            if (ImGui::MenuItem("Close Editor")) host_close_editor();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Console", "F10")) {
            extern gX_I0 gX_Tools_OpenScript_Runtime(gX_I0);
            gX_Tools_OpenScript_Runtime();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("About gX")) s_aboutOpen = 1;
        ImGui::MenuItem("Preferences", NULL, false, false);
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "F4 twice")) maindata.close = 1;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("File")) {
        ImGui::MenuItem("Save", NULL, false, false);
        ImGui::MenuItem("Save As", NULL, false, false);
        ImGui::Separator();
        if (ImGui::MenuItem("Close Editor")) host_close_editor();
        if (ImGui::MenuItem("Quit", "F4 twice")) maindata.close = 1;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
        ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
        ImGui::Separator();
        ImGui::MenuItem("Preferences", NULL, false, false);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Editor Bar", "F9", false, false);
        ImGui::MenuItem("Reset Layout", NULL, false, false);
        ImGui::Separator();
        gX_I1 skyOpen = *gX_Tools_RuntimeSkyFlag() ? 1 : 0;
        if (ImGui::MenuItem("Sky Panel", NULL, &skyOpen)) {
            if (skyOpen) host_open_sky();
            else {
                *gX_Tools_RuntimeSkyFlag() = 0;
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Sky")) {
        if (ImGui::MenuItem("Open Sky Editor")) host_open_sky();
        if (ImGui::MenuItem("Close Sky Editor")) {
            *gX_Tools_RuntimeSkyFlag() = 0;
            gX_TraceLog(gX_LOG_INFO, "EDITOR_HOST: close editor=Sky");
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        gX_I1 skyOpen = *gX_Tools_RuntimeSkyFlag() ? 1 : 0;
        if (ImGui::MenuItem("Sky", NULL, &skyOpen)) {
            if (skyOpen) host_open_sky();
            else *gX_Tools_RuntimeSkyFlag() = 0;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Close All")) host_close_editor();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Shortcuts")) s_shortcutsOpen = 1;
        if (ImGui::MenuItem("About gX")) s_aboutOpen = 1;
        ImGui::EndMenu();
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 300.0f);
    ImGui::TextUnformatted(host_active_editor());
    ImGui::SameLine(ImGui::GetWindowWidth() - 140.0f);
    ImGui::Text("%.0f fps", ImGui::GetIO().Framerate);
    ImGui::EndMainMenuBar();
}

static gX_I0 host_draw_windows(void) {
    if (s_aboutOpen) {
        ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
        if (ImGui::Begin("About gX", &s_aboutOpen,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("gX - a game engine (public beta)");
            ImGui::Separator();
            ImGui::TextUnformatted("Editor tiers: GFX beta; all other editors alpha/WIP.");
            ImGui::TextUnformatted("Toolbar: F9 summons this host from any scene.");
            ImGui::TextUnformatted("Quit: F4 twice (or gX > Quit).");
        }
        ImGui::End();
    }
    if (s_shortcutsOpen) {
        ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Appearing);
        if (ImGui::Begin("Shortcuts", &s_shortcutsOpen,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("F9    summon/hide the editor toolbar");
            ImGui::TextUnformatted("F10   console");
            ImGui::TextUnformatted("F4    exit confirmation (twice quits)");
            ImGui::TextUnformatted("F12   screenshot");
        }
        ImGui::End();
    }
}

extern "C" gX_I0 gX_EditorHost_Draw(gX_I0) {
    host_init_env();
    if (!s_hostOpen) return;
    host_draw_menu_bar();
    host_draw_windows();
}
