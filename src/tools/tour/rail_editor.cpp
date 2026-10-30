/**********************************************************************************************
*
*   [gX: src/tools/tour/rail_editor.cpp]
*   Camera rail editor — keyframe authoring for the shell's tour camera
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


//opencode: module-side editor (gx_shell) for the tour camera rail — the rail
//   is shell-owned (docs/editors.md placement decision), so this panel ships
//   with the module and costs the default payload nothing. Opened by
//   --rail-editor, GX_TOOLS_OPEN=rail, or the title EXTRAS card through the
//   exe's runtime flag (the module draws it via the tools-draw hook).
//opencode: raylib headers FIRST with C++ linkage — wrapper headers reached
//   through the tour headers would give raymath's operator overloads C
//   linkage (see src/tools/gfx/gfx_editor.cpp).
#include "raylib.h"
#include "raymath.h"

#include "../imgui/imgui.h"

#include "../../common/log.h"
#include "../../scripting/tour/tour_camera.h"
#include "../../wrappers/file.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern "C" gX_I1 *gX_Tools_RuntimeRailFlag(gX_I0);

#define GX_RAIL_EDITOR_PATH_MAX 256
#define GX_RAIL_EDITOR_FILE_MAX 16
#define GX_RAIL_EDITOR_DIR "resources/tools/camera"

static gX_I32 s_railSelected = 0;
static gX_I8  s_railPath[GX_RAIL_EDITOR_PATH_MAX] = "save/camera_rail.json";
static gX_I8  s_railGrabT[32] = "2.0";
static gX_I8  s_railStatus[192] = "ready";
static gX_I8  s_railFiles[GX_RAIL_EDITOR_FILE_MAX][128];
static gX_I32 s_railFileCount = 0;

static gX_I0 RailEditor_Status(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf((char *)s_railStatus, sizeof(s_railStatus), fmt, args);
    va_end(args);
}

static gX_I0 RailEditor_RefreshFiles(void) {
    s_railFileCount = 0;
    gX_File_Directory listing;
    if (gX_File_Directory_List(GX_RAIL_EDITOR_DIR, &listing) > 0) {
        for (gX_I32 i = 0; i < listing.count && s_railFileCount < GX_RAIL_EDITOR_FILE_MAX; i++) {
            const gX_I8 *name = listing.names[i];
            const gX_I8 *dot = strrchr((const char *)name, '.');
            if (!dot || strcmp((const char *)dot, ".json") != 0) continue;
            strncpy((char *)s_railFiles[s_railFileCount], (const char *)name, 127);
            s_railFiles[s_railFileCount][127] = '\0';
            s_railFileCount++;
        }
    }
}

static gX_I1 s_railInit = 0;

static gX_I0 RailEditor_Load(const gX_I8 *path) {
    if (gX_TourCamera_RailLoad(path) == 0) {
        s_railSelected = 0;
        RailEditor_Status("loaded %s (%d keyframes)", path, gX_TourCamera_RailCount());
    } else {
        RailEditor_Status("LOAD FAILED <- %s", path);
    }
}

extern "C" gX_I0 gX_Tools_Rail_Draw(gX_I0);
extern "C" gX_I0 gX_Tools_Rail_Draw(gX_I0) {
    gX_I1 open = *gX_Tools_RuntimeRailFlag();
    if (!open) return;

    //opencode: lazy init on first draw (the exe opener cannot call into the
    //   module; it only flips the flag).
    if (!s_railInit) {
        s_railInit = 1;
        RailEditor_RefreshFiles();
        s_railSelected = 0;
        RailEditor_Status("ready — fly the camera and Grab; Play replays the rail");
    }

    ImGui::SetNextWindowSize(ImVec2(660, 580), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("gX Tools - Camera Rail Editor", &open)) {
        gX_I32 count = gX_TourCamera_RailCount();
        ImGui::Text("keyframes %d / %d   %s", count, gX_TourCamera_RailMax(),
                    gX_TourCamera_RailIsRunning() ? "PLAYING" : "stopped");

        // ---- authoring row ----
        ImGui::SetNextItemWidth(70.0f);
        ImGui::InputText("grab t (s)", (char *)s_railGrabT, sizeof(s_railGrabT));
        ImGui::SameLine();
        if (ImGui::Button("Grab camera")) {
            gX_F32 t = (gX_F32)atof((const char *)s_railGrabT);
            gX_I32 idx = gX_TourCamera_RailAddGrab(t > 0.0f ? t : 2.0f);
            if (idx >= 0) {
                s_railSelected = idx;
                RailEditor_Status("grabbed keyframe %d", idx);
            } else {
                RailEditor_Status("rail full (%d keyframes)", gX_TourCamera_RailMax());
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Play")) {
            gX_TourCamera_RailPlay();
            RailEditor_Status("playing %d keyframes", count);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            gX_TourCamera_RailStop();
            RailEditor_Status("stopped");
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            gX_TourCamera_RailClear();
            s_railSelected = 0;
            RailEditor_Status("rail cleared");
        }

        // ---- persistence ----
        ImGui::SetNextItemWidth(-215.0f);
        ImGui::InputText("##railpath", (char *)s_railPath, sizeof(s_railPath));
        ImGui::SameLine();
        if (ImGui::Button("Load .json")) RailEditor_Load(s_railPath);
        ImGui::SameLine();
        if (ImGui::Button("Save .json")) {
            if (gX_TourCamera_RailSave(s_railPath) == 0) {
                RailEditor_Status("saved %d keyframes -> %s", count, s_railPath);
                RailEditor_RefreshFiles();
            } else {
                RailEditor_Status("SAVE FAILED -> %s", s_railPath);
            }
        }
        ImGui::BeginChild("RailEditorFiles", ImVec2(0, 60), true);
        if (s_railFileCount == 0) ImGui::TextUnformatted("(no camera presets found)");
        for (gX_I32 i = 0; i < s_railFileCount; i++) {
            if (ImGui::Selectable((const char *)s_railFiles[i])) {
                snprintf((char *)s_railPath, sizeof(s_railPath), GX_RAIL_EDITOR_DIR "/%s",
                         (const char *)s_railFiles[i]);
                RailEditor_Load(s_railPath);
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) RailEditor_RefreshFiles();

        // ---- keyframe list ----
        ImGui::SeparatorText("keyframes (select a row, edit below)");
        ImGui::BeginChild("RailEditorKeys", ImVec2(0, 190), true);
        for (gX_I32 i = 0; i < count; i++) {
            gX_TourCamera_Keyframe *kf = gX_TourCamera_RailKey(i);
            if (!kf) continue;
            ImGui::PushID(i);
            gX_I8 label[16];
            snprintf((char *)label, sizeof(label), "%2d", i);
            if (ImGui::Selectable(label, s_railSelected == i, 0, ImVec2(26.0f, 0.0f))) {
                s_railSelected = i;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            ImGui::DragFloat("##t", &kf->t, 0.05f, 0.001f, 60.0f, "%.2fs");
            ImGui::SameLine();
            ImGui::Text("(%.1f,%.1f,%.1f) fov %.0f%s",
                        kf->pos.x, kf->pos.y, kf->pos.z, kf->fov,
                        kf->bezier ? "  bezier" : "");
            ImGui::SameLine();
            if (ImGui::SmallButton("Go")) {
                gX_TourCamera_RailApply(i);
                RailEditor_Status("camera jumped to keyframe %d", i);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("^")) {
                if (gX_TourCamera_RailMove(i, -1) == 0) s_railSelected = i - 1;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("v")) {
                if (gX_TourCamera_RailMove(i, 1) == 0) s_railSelected = i + 1;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                if (gX_TourCamera_RailRemove(i) == 0) {
                    if (s_railSelected >= gX_TourCamera_RailCount()) {
                        s_railSelected = gX_TourCamera_RailCount() - 1;
                    }
                    RailEditor_Status("removed keyframe %d", i);
                }
                ImGui::PopID();
                break; // the list changed under the loop; redraw next frame
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        // ---- selected keyframe details ----
        gX_TourCamera_Keyframe *sel = gX_TourCamera_RailKey(s_railSelected);
        if (sel) {
            ImGui::SeparatorText("selected keyframe");
            ImGui::DragFloat3("pos", &sel->pos.x, 0.05f);
            ImGui::DragFloat3("target", &sel->target.x, 0.05f);
            ImGui::DragFloat("fov", &sel->fov, 0.5f, 1.0f, 179.0f, "%.1f");
            bool bezier = (sel->bezier != 0);
            if (ImGui::Checkbox("bezier segment (outgoing)", &bezier)) {
                sel->bezier = bezier ? 1 : 0;
            }
            if (bezier) {
                ImGui::DragFloat3("ctrl1", &sel->ctrl1.x, 0.05f);
                ImGui::DragFloat3("ctrl2", &sel->ctrl2.x, 0.05f);
            }
        }
        ImGui::TextUnformatted((const char *)s_railStatus);
    }
    ImGui::End();
    if (!open) *gX_Tools_RuntimeRailFlag() = 0;
}
