/**********************************************************************************************
*
*   [gX: src/tools/background/background_editor.cpp]
*   Background editor — standalone surface over the background renderer
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
*   gX is distributed in the hope that it will be useful, but WITHOUT ANY
*   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
*   FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
*   details.
*   You should have received a copy of the GNU General Public License along
*   with gX. If not, see <https://www.gnu.org/licenses/>.
*
*
**********************************************************************************************/


//opencode: focused background authoring: the shared tuning body
//   (bg_panel.cpp — palette/decomposer), a preset workflow with a fixture
//   list under resources/tools/background, the Background polisher entries,
//   and reaction pulses. Opened by --background-editor (routes to the
//   --background scene), GX_TOOLS_OPEN=background, or the title EXTRAS card.
//opencode: raylib headers FIRST with C++ linkage — wrapper headers reached
//   through renderer.h would give raymath's operator overloads C linkage
//   (see src/tools/gfx/gfx_editor.cpp).
#include "raylib.h"
#include "raymath.h"

#include "../imgui/imgui.h"

#include "../../common/log.h"
#include "../../common/polisher.h"
#include "../../renderer/renderer.h"
#include "../../renderer/background.h"
#include "../../renderer/background/schema.h"
#include "../../wrappers/file.h"
#include "bg_panel.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" gX_I1 *gX_Tools_RuntimeBackgroundFlag(gX_I0);

//opencode: kernel profile — the background scene lives in the renderer
//   cluster, so the whole implementation is guarded and the entry point
//   becomes a no-op there (previously the editor's renderer references broke
//   `make check-kernel`; same shape as the physics editor).
#if gX_ENABLE_RENDERER == 1

#define GX_BGE_PATH_MAX 256
#define GX_BGE_FILE_MAX 16
#define GX_BGE_DIR "resources/tools/background"

static gX_I8  s_bgePath[GX_BGE_PATH_MAX] = GX_BGE_DIR "/editor.json";
static gX_I8  s_bgeStatus[192] = "ready";
static gX_I8  s_bgeFiles[GX_BGE_FILE_MAX][128];
static gX_I32 s_bgeFileCount = 0;
static gX_I1  s_bgeTraced = 0;
static gX_I1  s_bgeFilesInit = 0;

static gX_I0 Bge_Status(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf((char *)s_bgeStatus, sizeof(s_bgeStatus), fmt, args);
    va_end(args);
}

static gX_I0 Bge_RefreshFiles(void) {
    s_bgeFileCount = 0;
    gX_File_Directory listing;
    if (gX_File_Directory_List(GX_BGE_DIR, &listing) > 0) {
        for (gX_I32 i = 0; i < listing.count && s_bgeFileCount < GX_BGE_FILE_MAX; i++) {
            const gX_I8 *name = listing.names[i];
            const gX_I8 *dot = strrchr((const char *)name, '.');
            if (!dot || strcmp((const char *)dot, ".json") != 0) continue;
            strncpy((char *)s_bgeFiles[s_bgeFileCount], (const char *)name, 127);
            s_bgeFiles[s_bgeFileCount][127] = '\0';
            s_bgeFileCount++;
        }
    }
}

static gX_I0 Bge_Load(gX_RendererBackground *bg, const gX_I8 *path) {
    if (gX_RendererBG_Preset_Load_New(bg, path) == 0) {
        Bge_Status("loaded %s", path);
    } else {
        Bge_Status("LOAD FAILED <- %s", path);
    }
}

static gX_I0 Bge_Save(gX_RendererBackground *bg, const gX_I8 *path) {
    if (gX_RendererBG_Preset_Save_New(bg, path) == 0) {
        Bge_Status("saved -> %s", path);
        Bge_RefreshFiles();
    } else {
        Bge_Status("SAVE FAILED -> %s", path);
    }
}

extern "C" gX_I0 gX_Tools_Background_Draw(gX_I0);
extern "C" gX_I0 gX_Tools_Background_Draw(gX_I0) {
    gX_I1 open = *gX_Tools_RuntimeBackgroundFlag();
    if (!open) return;
    //ai: availability trace (not expansion) — the check gate asserts it.
    if (!s_bgeTraced) {
        gX_TraceLog(gX_LOG_INFO, "BG_EDITOR_UI: panel=1");
        s_bgeTraced = 1;
    }
    //opencode: lazy fixture scan on first draw.
    if (!s_bgeFilesInit) {
        s_bgeFilesInit = 1;
        Bge_RefreshFiles();
    }

    ImGui::SetNextWindowSize(ImVec2(560, 680), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("gX Tools - Background Editor", &open)) {
        gX_RendererBackground *bg = gX_Tools_BG_Active();
        if (!bg) {
            ImGui::TextUnformatted("background module unavailable — run the --background scene");
        } else {
            //opencode: shared tuning body (palette/decomposer); the standalone
            //   host draws its own preset workflow below.
            gX_Tools_BG_PanelBody(0, 0);

            // ---- preset files ----
            ImGui::SeparatorText("presets (" GX_BGE_DIR ")");
            ImGui::SetNextItemWidth(-215.0f);
            ImGui::InputText("##bgepath", (char *)s_bgePath, sizeof(s_bgePath));
            ImGui::SameLine();
            if (ImGui::Button("Load .json")) Bge_Load(bg, s_bgePath);
            ImGui::SameLine();
            if (ImGui::Button("Save .json")) Bge_Save(bg, s_bgePath);
            ImGui::BeginChild("BgeFiles", ImVec2(0, 66), true);
            if (s_bgeFileCount == 0) ImGui::TextUnformatted("(no background presets found)");
            for (gX_I32 i = 0; i < s_bgeFileCount; i++) {
                if (ImGui::Selectable((const char *)s_bgeFiles[i])) {
                    snprintf((char *)s_bgePath, sizeof(s_bgePath), GX_BGE_DIR "/%s",
                             (const char *)s_bgeFiles[i]);
                    Bge_Load(bg, s_bgePath);
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) Bge_RefreshFiles();

            // ---- background polisher ----
            //opencode: collapsing + scroll child so the preset/reaction
            //   sections stay on-screen at the lean floor (the entry list can
            //   be long; it scrolls inside its own child when opened).
            if (ImGui::CollapsingHeader("Background polisher")) {
                gX_I32 shown = 0;
                ImGui::BeginChild("BgePolisher", ImVec2(0, 150), true);
                for (gX_U32 i = 0; i < polisher.count; i++) {
                    gX_Polisher_Entry *entry = &polisher.entries[i];
                    if (strcmp((const char *)entry->category, "Background") != 0) continue;
                    shown++;
                    ImGui::PushID((gX_I32)i);
                    if (entry->type == GX_POLISHER_TYPE_F32 && entry->f32Value) {
                        if (entry->flags & GX_POLISHER_UNBOUNDED) {
                            ImGui::DragFloat((const char *)entry->name, entry->f32Value, entry->step);
                        } else {
                            ImGui::SliderFloat((const char *)entry->name, entry->f32Value,
                                entry->f32Min, entry->f32Max);
                        }
                        entry->modified = (*entry->f32Value != entry->f32Original) ? 1 : 0;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("R")) {
                            *entry->f32Value = entry->f32Original;
                            entry->modified = 0;
                            Bge_Status("reset %s", (const char *)entry->name);
                        }
                    } else if (entry->i32Value) {
                        if (entry->flags & GX_POLISHER_UNBOUNDED) {
                            ImGui::DragInt((const char *)entry->name, entry->i32Value,
                                entry->step);
                        } else {
                            ImGui::SliderInt((const char *)entry->name, entry->i32Value,
                                entry->i32Min, entry->i32Max);
                        }
                        entry->modified = (*entry->i32Value != entry->i32Original) ? 1 : 0;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("R")) {
                            *entry->i32Value = entry->i32Original;
                            entry->modified = 0;
                            Bge_Status("reset %s", (const char *)entry->name);
                        }
                    }
                    ImGui::PopID();
                }
                if (shown == 0) ImGui::TextUnformatted("(no Background entries registered)");
                ImGui::EndChild();
            }

            // ---- reactions ----
            ImGui::SeparatorText("reactions");
            struct { const char *label; gX_I32 type; } reacts[] = {
                { "Pulse", GX_BG_REACT_PULSE },
                { "Shake", GX_BG_REACT_SHAKE },
                { "Flash", GX_BG_REACT_FLASH },
                { "Warp",  GX_BG_REACT_WARP },
            };
            for (gX_I32 i = 0; i < (gX_I32)(sizeof(reacts) / sizeof(reacts[0])); i++) {
                if (i > 0) ImGui::SameLine();
                if (ImGui::Button(reacts[i].label)) {
                    gX_RendererBackground_React(bg, reacts[i].type, 1.0f, 0.5f);
                    Bge_Status("reaction: %s", reacts[i].label);
                }
            }
        }
        ImGui::TextUnformatted((const char *)s_bgeStatus);
    }
    ImGui::End();
    if (!open) *gX_Tools_RuntimeBackgroundFlag() = 0;
}

#else //!gX_ENABLE_RENDERER — no background scene to edit (kernel profile)

extern "C" gX_I0 gX_Tools_Background_Draw(gX_I0) { }

#endif
