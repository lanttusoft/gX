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

//opencode: global editor host — menu-bar root, editor registry and the first
//   workspace/suite slice. The toolbar is always summonable (F9) from any
//   scene, editor or non-editor. Each suite owns a visible-editor selection
//   and an ImGui layout file (per-suite geometry); the GFX editor stays
//   disabled until the workspace/panel migration lands.
//   See private-docs/post-gfx-plan.md, H2.

extern "C" {
#include "../main.h"
#include "../gui/gui.h"
#include "../wrappers/file.h"
#include "../common/log.h"
}

#include "imgui/imgui.h"
#include "tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" gX_I0 gX_EditorHost_SetOpen(gX_I1 open);

static gX_I1 s_hostOpen = 0;
static gX_I1 s_inited = 0;
static gX_I1 s_guarded = 0;
static gX_I1 s_aboutOpen = 0;
static gX_I1 s_shortcutsOpen = 0;
static gX_I32 s_suite = 0;
static gX_I32 s_pendingSuite = -2; // -2 = none, >=0 = apply
static gX_I32 s_pendingDelete = -1;
static gX_I1 s_saveOpen = 0;
static gX_I8 s_saveName[64] = {0};
static gX_I8 s_saveError[128] = {0};

typedef struct HostEditor {
    const gX_I8 *label;
    const gX_I8 *tier;
    gX_I1 *(*flag)(gX_I0);
    gX_I0 (*open)(gX_I0);
    gX_I1 enabled;
} HostEditor;

static const HostEditor kEditors[] = {
    { "GUI",        "alpha/WIP", gX_Tools_RuntimeEditorFlag,     gX_Tools_OpenEditor_Runtime,     1 },
    { "Theme",      "alpha/WIP", gX_Tools_RuntimeThemeFlag,      gX_Tools_OpenTheme_Runtime,      1 },
    { "Sky",        "alpha/WIP", gX_Tools_RuntimeSkyFlag,        gX_Tools_OpenSky_Runtime,        1 },
    { "Background", "alpha/WIP", gX_Tools_RuntimeBackgroundFlag, gX_Tools_OpenBackground_Runtime, 1 },
    { "Physics",    "alpha/WIP", gX_Tools_RuntimePhysicsFlag,    gX_Tools_OpenPhysics_Runtime,    1 },
    { "Wavetable",  "alpha/WIP", gX_Tools_RuntimeWavetableFlag,  gX_Tools_OpenWavetable_Runtime,  1 },
    { "Textwriter", "alpha/WIP", gX_Tools_RuntimeTextwriterFlag, gX_Tools_OpenTextwriter_Runtime, 1 },
    { "Rail",       "alpha/WIP", gX_Tools_RuntimeRailFlag,       gX_Tools_OpenRail_Runtime,       1 },
    { "GFX",        "beta",      gX_Tools_RuntimeGfxFlag,        gX_Tools_OpenGfx_Runtime,        0 },
    { "Script",     "alpha/WIP", gX_Tools_RuntimeScriptFlag,     gX_Tools_OpenScript_Runtime,     1 },
};

static const gX_I32 kEditorCount = (gX_I32)(sizeof(kEditors) / sizeof(kEditors[0]));
static const HostEditor *host_active_editor(void);
static const HostEditor *s_lastActive = NULL;
static gX_I1 s_activeTraced = 0;

#define HOST_SUITE_MAX 16

typedef struct HostSuite {
    gX_I8 name[64];
    gX_I32 editor; // registry index, -1 = no editor
    gX_I1 user;
} HostSuite;

static HostSuite s_suites[HOST_SUITE_MAX];
static gX_I32 s_suiteCount = 0;

static const struct { const gX_I8 *name; gX_I32 editor; } kBuiltinSuites[] = {
    { "Default",    -1 },
    { "Sky",         2 },
    { "Background",  3 },
    { "Physics",     4 },
    { "Audio",       5 },
    { "Writing",     6 },
    { "GUI",         0 },
    { "Theme",       1 },
};
static const gX_I32 kBuiltinSuiteCount =
    (gX_I32)(sizeof(kBuiltinSuites) / sizeof(kBuiltinSuites[0]));

static const gX_I8 *host_suite_name(gX_I32 index) {
    if (index < 0 || index >= s_suiteCount) return "Default";
    return s_suites[index].name;
}

static gX_I32 host_suite_editor(gX_I32 index) {
    if (index < 0 || index >= s_suiteCount) return -1;
    return s_suites[index].editor;
}

static gX_I1 host_suite_user(gX_I32 index) {
    if (index < 0 || index >= s_suiteCount) return 0;
    return s_suites[index].user;
}

static gX_I1 host_suite_name_valid(const gX_I8 *name) {
    if (!name || !name[0]) return 0;
    gX_U32 len = (gX_U32)strlen((const char *)name);
    if (len < 1 || len >= 64) return 0;
    for (gX_U32 i = 0; i < len; i++) {
        gX_I8 c = name[i];
        gX_I1 ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return 0;
    }
    return 1;
}

static gX_I0 host_trace(gX_I1 open) {
    gX_TraceLog(gX_LOG_INFO, "EDITOR_HOST: open=%d", (gX_I32)open);
}

static gX_I0 host_trace_active(gX_I0) {
    gX_I32 index = -1;
    for (gX_I32 i = 0; i < kEditorCount; i++) {
        if (kEditors[i].flag && *kEditors[i].flag()) { index = i; break; }
    }
    const HostEditor *active = index >= 0 ? &kEditors[index] : NULL;
    if (active == s_lastActive && s_activeTraced) return;
    s_lastActive = active;
    s_activeTraced = 1;
    gX_TraceLog(gX_LOG_INFO, "EDITOR_HOST: active=%s",
        active ? (const char *)active->label : "none");
}

static gX_I0 host_suite_path(gX_I32 index, gX_I8 *out, gX_U32 size) {
    const gX_I8 *work = gX_Directory_Working_Get();
    const gX_I8 *name = (index >= 0 && index < s_suiteCount)
        ? host_suite_name(index) : "Default";
    snprintf((char *)out, size, "%s/gx_editor_%s.ini",
        (const char *)work, (const char *)name);
}

static gX_I0 host_selection_path(gX_I8 *out, gX_U32 size) {
    const gX_I8 *work = gX_Directory_Working_Get();
    snprintf((char *)out, size, "%s/gx_editor_workspace.txt",
        (const char *)work);
}

static gX_I1 host_file_exists(const gX_I8 *path) {
    FILE *file = fopen((const char *)path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static gX_I0 host_suites_file_path(gX_I8 *out, gX_U32 size) {
    const gX_I8 *work = gX_Directory_Working_Get();
    snprintf((char *)out, size, "%s/gx_editor_suites.txt", (const char *)work);
}

static gX_I0 host_suites_persist(gX_I0) {
    if (s_guarded) return;
    gX_I8 path[512];
    host_suites_file_path(path, sizeof(path));
    FILE *file = fopen((const char *)path, "wb");
    if (!file) return;
    for (gX_I32 i = 0; i < s_suiteCount; i++) {
        if (!s_suites[i].user) continue;
        fprintf(file, "%s|%d\n", (const char *)s_suites[i].name,
            s_suites[i].editor);
    }
    fclose(file);
}

static gX_I1 host_suite_name_used(const gX_I8 *name) {
    for (gX_I32 i = 0; i < s_suiteCount; i++) {
        if (strcmp((const char *)name, (const char *)s_suites[i].name) == 0)
            return 1;
    }
    return 0;
}

static gX_I0 host_suites_init(gX_I0) {
    s_suiteCount = 0;
    for (gX_I32 i = 0; i < kBuiltinSuiteCount; i++) {
        snprintf((char *)s_suites[s_suiteCount].name,
            sizeof(s_suites[s_suiteCount].name), "%s",
            (const char *)kBuiltinSuites[i].name);
        s_suites[s_suiteCount].editor = kBuiltinSuites[i].editor;
        s_suites[s_suiteCount].user = 0;
        s_suiteCount++;
    }
    if (s_guarded) return;
    gX_I8 path[512];
    host_suites_file_path(path, sizeof(path));
    FILE *file = fopen((const char *)path, "rb");
    if (!file) return;
    gX_I8 line[160];
    while (fgets((char *)line, sizeof(line), file) && s_suiteCount < HOST_SUITE_MAX) {
        for (gX_U32 i = 0; i < sizeof(line); i++) {
            if (line[i] == '\n' || line[i] == '\r') { line[i] = '\0'; break; }
        }
        char *bar = strchr((char *)line, '|');
        if (!bar) continue;
        *bar = '\0';
        gX_I32 editor = atoi(bar + 1);
        if (editor < -1 || editor >= kEditorCount) continue;
        if (!host_suite_name_valid(line)) continue;
        if (host_suite_name_used(line)) continue;
        snprintf((char *)s_suites[s_suiteCount].name,
            sizeof(s_suites[s_suiteCount].name), "%s", (char *)line);
        s_suites[s_suiteCount].editor = editor;
        s_suites[s_suiteCount].user = 1;
        s_suiteCount++;
    }
    fclose(file);
}

static gX_I0 host_save_layout(gX_I0) {
    if (s_guarded || s_suite < 0 || s_suite >= s_suiteCount) return;
    if (!ImGui::GetCurrentContext()) return;
    gX_I8 path[512];
    host_suite_path(s_suite, path, sizeof(path));
    ImGui::SaveIniSettingsToDisk((const char *)path);
}

static gX_I0 host_load_layout(gX_I32 index) {
    if (s_guarded || index < 0 || index >= s_suiteCount) return;
    if (!ImGui::GetCurrentContext()) return;
    gX_I8 path[512];
    host_suite_path(index, path, sizeof(path));
    if (host_file_exists(path))
        ImGui::LoadIniSettingsFromDisk((const char *)path);
}

static gX_I0 host_save_selection(gX_I0) {
    if (s_guarded || s_suite < 0 || s_suite >= s_suiteCount) return;
    gX_I8 path[512];
    host_selection_path(path, sizeof(path));
    FILE *file = fopen((const char *)path, "wb");
    if (!file) return;
    fprintf(file, "%s\n", (const char *)host_suite_name(s_suite));
    fclose(file);
}

static gX_I32 host_read_selection(gX_I0) {
    if (s_guarded) return -1;
    gX_I8 path[512];
    host_selection_path(path, sizeof(path));
    FILE *file = fopen((const char *)path, "rb");
    if (!file) return -1;
    gX_I8 line[64] = {0};
    if (!fgets((char *)line, sizeof(line), file)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    for (gX_U32 i = 0; i < sizeof(line); i++) {
        if (line[i] == '\n' || line[i] == '\r') { line[i] = '\0'; break; }
    }
    for (gX_I32 i = 0; i < s_suiteCount; i++) {
        if (strcmp((const char *)line, (const char *)host_suite_name(i)) == 0)
            return i;
    }
    return -1;
}

static gX_I0 host_open_editor(gX_I32 index) {
    if (index < 0 || index >= kEditorCount) return;
    const HostEditor *editor = &kEditors[index];
    if (!editor->enabled || !editor->open) return;
    editor->open();
    gX_EditorHost_SetOpen(1);
    host_trace_active();
}

static gX_I0 host_close_editor(void) {
    gX_Tools_Close_Runtime();
    host_trace_active();
}

static gX_I0 host_save_user_suite(const gX_I8 *name) {
    if (s_guarded) {
        snprintf((char *)s_saveError, sizeof(s_saveError),
            "read-only run (GX_NO_SETTINGS_SAVE)");
        return;
    }
    if (!host_suite_name_valid(name)) {
        snprintf((char *)s_saveError, sizeof(s_saveError),
            "use letters, digits, _ or -");
        return;
    }
    gX_I32 found = -1;
    for (gX_I32 i = 0; i < s_suiteCount; i++) {
        if (strcmp((const char *)name, (const char *)s_suites[i].name) != 0)
            continue;
        if (!s_suites[i].user) {
            snprintf((char *)s_saveError, sizeof(s_saveError),
                "a built-in workspace already uses that name");
            return;
        }
        found = i;
        break;
    }
    gX_I32 editor = -1;
    for (gX_I32 i = 0; i < kEditorCount; i++) {
        if (kEditors[i].flag && *kEditors[i].flag()) { editor = i; break; }
    }
    if (editor < 0) {
        snprintf((char *)s_saveError, sizeof(s_saveError),
            "open an editor first");
        return;
    }
    if (!kEditors[editor].enabled) {
        snprintf((char *)s_saveError, sizeof(s_saveError),
            "that editor is not hostable yet");
        return;
    }
    if (found < 0) {
        if (s_suiteCount >= HOST_SUITE_MAX) {
            snprintf((char *)s_saveError, sizeof(s_saveError),
                "workspace limit reached");
            return;
        }
        found = s_suiteCount++;
        snprintf((char *)s_suites[found].name,
            sizeof(s_suites[found].name), "%s", (const char *)name);
        s_suites[found].user = 1;
    }
    s_suites[found].editor = editor;
    host_suites_persist();
    s_suite = found;
    host_save_selection();
    s_saveOpen = 0;
    s_saveError[0] = '\0';
    gX_TraceLog(gX_LOG_INFO, "EDITOR_HOST: suite saved=%s", (const char *)name);
}

static gX_I0 host_delete_suite(gX_I32 index) {
    if (!host_suite_user(index)) return;
    gX_I8 name[64];
    snprintf((char *)name, sizeof(name), "%s", (const char *)host_suite_name(index));
    if (s_suite == index) {
        host_close_editor();
        s_suite = 0;
    } else if (s_suite > index) {
        s_suite--;
    }
    if (!s_guarded) {
        gX_I8 path[512];
        host_suite_path(index, path, sizeof(path));
        remove((const char *)path);
    }
    for (gX_I32 i = index; i < s_suiteCount - 1; i++)
        s_suites[i] = s_suites[i + 1];
    s_suiteCount--;
    s_pendingDelete = -1;
    host_suites_persist();
    host_save_selection();
    gX_TraceLog(gX_LOG_INFO, "EDITOR_HOST: suite deleted=%s", (const char *)name);
}

static gX_I0 host_apply_suite(gX_I32 index) {
    if (index < 0 || index >= s_suiteCount) return;
    host_save_layout();
    host_close_editor();
    s_suite = index;
    gX_TraceLog(gX_LOG_INFO, "EDITOR_HOST: workspace=%s",
        (const char *)host_suite_name(index));
    if (host_suite_editor(index) >= 0) host_open_editor(host_suite_editor(index));
    host_load_layout(index);
    host_save_selection();
}

static gX_I0 host_init_env(gX_I0) {
    if (s_inited) return;
    s_inited = 1;
    const gX_I8 *guard = getenv("GX_NO_SETTINGS_SAVE");
    s_guarded = (guard && guard[0] && guard[0] != '0') ? 1 : 0;
    host_suites_init();

    gX_I32 selected = host_read_selection();
    const gX_I8 *suiteEnv = getenv("GX_EDITOR_WORKSPACE");
    if (suiteEnv && suiteEnv[0]) {
        selected = -1;
        for (gX_I32 i = 0; i < s_suiteCount; i++) {
            if (strcmp((const char *)suiteEnv, (const char *)host_suite_name(i)) == 0) {
                selected = i;
                break;
            }
        }
        if (selected < 0) {
            gX_TraceLog(gX_LOG_WARNING,
                "EDITOR_HOST: unknown workspace '%s'", (const char *)suiteEnv);
        }
    }
    s_suite = 0;
    s_pendingSuite = selected >= 0 ? selected : -2;

    const gX_I8 *openEnv = getenv("GX_EDITOR_HOST");
    if (openEnv && openEnv[0] && openEnv[0] != '0') {
        s_hostOpen = 1;
        host_trace(1);
    }
    if (s_hostOpen && s_suite == 0 && selected < 0)
        gX_TraceLog(gX_LOG_INFO, "EDITOR_HOST: workspace=Default");
}

extern "C" gX_I0 gX_EditorHost_SetOpen(gX_I1 open) {
    host_init_env();
    open = open ? 1 : 0;
    if (s_hostOpen == open) return;
    if (!open) {
        host_save_layout();
        host_save_selection();
    }
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

extern "C" gX_I0 gX_EditorHost_Update(gX_I0) {
    host_init_env();
    if (!s_hostOpen) return;
    if (!ImGui::GetCurrentContext()) gX_GUI_Load_ImGui();
    if (s_pendingDelete >= 0) host_delete_suite(s_pendingDelete);
    if (s_pendingSuite != -2) {
        gX_I32 pending = s_pendingSuite;
        s_pendingSuite = -2;
        if (pending >= 0) host_apply_suite(pending);
    }
    {
        //opencode: capture/test hook — save the active editor as a user suite.
        static gX_I1 saveEnvDone = 0;
        if (!saveEnvDone) {
            const gX_I8 *saveEnv = getenv("GX_EDITOR_SUITE_SAVE");
            if (!saveEnv || !saveEnv[0]) saveEnvDone = 1;
            else if (!s_guarded && host_active_editor()) {
                saveEnvDone = 1;
                host_save_user_suite(saveEnv);
            }
        }
    }
    if (!s_guarded && s_suite >= 0 && ImGui::GetCurrentContext()) {
        ImGuiIO &io = ImGui::GetIO();
        static gX_U64 lastSaveFrame = 0;
        gX_U64 frame = maindata.frameCount;
        if (io.WantSaveIniSettings || frame - lastSaveFrame >= 60) {
            host_save_layout();
            io.WantSaveIniSettings = false;
            lastSaveFrame = frame;
        }
    }
}

static gX_I0 host_editor_menu(const HostEditor *editor) {
    if (!ImGui::BeginMenu(editor->label)) return;
    if (ImGui::MenuItem("Close")) {
        *editor->flag() = 0;
        host_trace_active();
    }
    ImGui::EndMenu();
}

static const HostEditor *host_active_editor(void);

static const HostEditor *host_active_editor(void) {
    for (gX_I32 i = 0; i < kEditorCount; i++) {
        if (kEditors[i].flag && *kEditors[i].flag()) return &kEditors[i];
    }
    return NULL;
}

static gX_I0 host_draw_suite_tabs(void) {
    ImGuiIO &io = ImGui::GetIO();
    gX_F32 barH = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(0.0f, barH));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, barH));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_HorizontalScrollbar;
    if (ImGui::Begin("##gx_workspace_tabs", NULL, flags)) {
        for (gX_I32 i = 0; i < s_suiteCount; i++) {
            ImGui::PushID(i);
            if (ImGui::Selectable(host_suite_name(i), s_suite == i, 0, ImVec2(0, 0)))
                s_pendingSuite = i;
            if (host_suite_user(i)) {
                ImGui::SameLine(0.0f, 2.0f);
                if (ImGui::SmallButton("x")) s_pendingDelete = i;
            }
            ImGui::PopID();
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+")) {
            s_saveName[0] = '\0';
            s_saveError[0] = '\0';
            s_saveOpen = 1;
        }
    }
    ImGui::End();
}

static gX_I0 host_draw_menu_bar(void) {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("gX")) {
        if (ImGui::BeginMenu("Change Editor")) {
            for (gX_I32 i = 0; i < kEditorCount; i++) {
                const HostEditor *editor = &kEditors[i];
                gX_I8 label[96];
                snprintf(label, sizeof(label), "%s  (%s)%s",
                    (const char *)editor->label, (const char *)editor->tier,
                    editor->enabled ? "" : " - migration pending");
                if (ImGui::MenuItem(label, NULL, false, editor->enabled))
                    host_open_editor(i);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close Editor")) host_close_editor();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Console", "F10")) {
            gX_Tools_OpenScript_Runtime();
            host_trace_active();
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
        if (ImGui::BeginMenu("Workspaces")) {
            for (gX_I32 i = 0; i < s_suiteCount; i++) {
                if (ImGui::MenuItem(host_suite_name(i), NULL, s_suite == i))
                    s_pendingSuite = i;
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        for (gX_I32 i = 0; i < kEditorCount; i++) {
            const HostEditor *editor = &kEditors[i];
            if (!editor->enabled) continue;
            gX_I1 open = *editor->flag() ? 1 : 0;
            if (ImGui::MenuItem(editor->label, NULL, &open)) {
                if (open) host_open_editor(i);
                else {
                    *editor->flag() = 0;
                    host_trace_active();
                }
            }
        }
        ImGui::EndMenu();
    }
    {
        const HostEditor *active = host_active_editor();
        if (active) host_editor_menu(active);
    }
    if (ImGui::BeginMenu("Window")) {
        for (gX_I32 i = 0; i < kEditorCount; i++) {
            const HostEditor *editor = &kEditors[i];
            if (!editor->enabled) continue;
            gX_I1 open = *editor->flag() ? 1 : 0;
            if (ImGui::MenuItem(editor->label, NULL, &open)) {
                if (open) host_open_editor(i);
                else {
                    *editor->flag() = 0;
                    host_trace_active();
                }
            }
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

    {
        const HostEditor *active = host_active_editor();
        ImGui::SameLine(ImGui::GetWindowWidth() - 420.0f);
        ImGui::Text("%s | %s  %s", (const char *)host_suite_name(s_suite),
            active ? (const char *)active->label : "No editor",
            active ? (const char *)active->tier : "");
    }
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
            ImGui::TextUnformatted("Workspaces: View > Workspaces (per-suite layout).");
            ImGui::TextUnformatted("Quit: F4 twice (or gX > Quit).");
        }
        ImGui::End();
    }
    if (s_saveOpen) {
        ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Appearing);
        if (ImGui::Begin("Save Workspace", &s_saveOpen,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Capture the active editor as a workspace.");
            ImGui::InputText("Name", (char *)s_saveName, sizeof(s_saveName));
            if (s_saveError[0])
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s",
                    (const char *)s_saveError);
            if (ImGui::Button("Save")) host_save_user_suite(s_saveName);
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) s_saveOpen = 0;
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
    host_trace_active();
    host_draw_menu_bar();
    host_draw_suite_tabs();
    host_draw_windows();
}
