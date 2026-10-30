/**********************************************************************************************
*
*   [gX: src/tools/sky/sky_editor.cpp]
*   Sky editor — look parameters + the ephemeris clock over the sky scene
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


//opencode: the sky renderer's sun/moon/planet directions are computed from
//   gamedata.ephemeris, so this editor drives two things: the look parameters
//   (density/hue/glow/contrast/altitude/turbidity/disks/stars/moon/helpers)
//   and the ephemeris clock (scrub + play at a speed). Presets go through
//   gX_RendererSky_Preset_Save/Load (JSON, shipped paths refused under the
//   harness guard). Opened by --sky-editor (routes to the sky scene),
//   GX_TOOLS_OPEN=sky, or the title EXTRAS card.
//opencode: raylib headers FIRST with C++ linkage — wrapper headers reached
//   through renderer.h would give raymath's operator overloads C linkage
//   (see src/tools/gfx/gfx_editor.cpp).
#include "raylib.h"
#include "raymath.h"

#include "../imgui/imgui.h"

#include "../../common/log.h"
#include "../../game/game.h"
#include "../../renderer/renderer.h"
#include "../../renderer/sky.h"
#include "../../wrappers/file.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" gX_I1 *gX_Tools_RuntimeSkyFlag(gX_I0);
extern "C" gX_F64 gX_FrameTime_Get(gX_I0);

//opencode: kernel profile — the sky scene lives in the renderer cluster, so
//   the whole implementation is guarded and the entry point becomes a no-op
//   there (previously the editor's renderer references broke
//   `make check-kernel`; same shape as the physics editor).
#if gX_ENABLE_RENDERER == 1

#define GX_SKYE_PATH_MAX 256
#define GX_SKYE_FILE_MAX 16
#define GX_SKYE_DIR "resources/tools/sky"
#define GX_SKYE_YEAR_SEC 36000.0  // ephemeris reference period (Earth orbit)

static gX_I8  s_skyePath[GX_SKYE_PATH_MAX] = GX_SKYE_DIR "/editor.json";
static gX_I8  s_skyeStatus[192] = "ready";
static gX_I8  s_skyeFiles[GX_SKYE_FILE_MAX][128];
static gX_I32 s_skyeFileCount = 0;
static gX_I1  s_skyeTraced = 0;
static gX_I1  s_skyeFilesInit = 0;
static gX_I1  s_skyePlaying = 0;
static gX_F32 s_skyeSpeed = 600.0f;   // ephemeris seconds per real second
static gX_F64 s_skyeTimeSec = 0.0;

static gX_RendererSky *Skye_Active(void) {
    if (!renderer.sky.data || renderer.sky.amount == 0) return NULL;
    return (gX_RendererSky *)renderer.sky.data;
}

static gX_I0 Skye_Status(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf((char *)s_skyeStatus, sizeof(s_skyeStatus), fmt, args);
    va_end(args);
}

static gX_I0 Skye_RefreshFiles(void) {
    s_skyeFileCount = 0;
    gX_File_Directory listing;
    if (gX_File_Directory_List(GX_SKYE_DIR, &listing) > 0) {
        for (gX_I32 i = 0; i < listing.count && s_skyeFileCount < GX_SKYE_FILE_MAX; i++) {
            const gX_I8 *name = listing.names[i];
            const gX_I8 *dot = strrchr((const char *)name, '.');
            if (!dot || strcmp((const char *)dot, ".json") != 0) continue;
            strncpy((char *)s_skyeFiles[s_skyeFileCount], (const char *)name, 127);
            s_skyeFiles[s_skyeFileCount][127] = '\0';
            s_skyeFileCount++;
        }
    }
}

//opencode: apply the editor clock to the ephemeris and refresh body directions.
static gX_I0 Skye_ApplyTime(gX_F64 t) {
    s_skyeTimeSec = t;
    gamedata.ephemeris.timeSec = t;
    gX_Ephemeris_Update(&gamedata.ephemeris, t);
}

static gX_F32 Skye_SunElevation(void) {
    gX_Ephemeris_Body *sun = gX_Ephemeris_GetBody(&gamedata.ephemeris, "Sun");
    return sun ? sun->direction.y : 0.0f;
}

static gX_I0 Skye_Load(gX_RendererSky *sky, const gX_I8 *path) {
    if (gX_RendererSky_Preset_Load(sky, path) == 0) {
        s_skyeTimeSec = gamedata.ephemeris.timeSec;
        Skye_Status("loaded %s (t=%.0fs)", path, s_skyeTimeSec);
    } else {
        Skye_Status("LOAD FAILED <- %s", path);
    }
}

extern "C" gX_I0 gX_Tools_Sky_Draw(gX_I0);
extern "C" gX_I0 gX_Tools_Sky_Draw(gX_I0) {
    gX_I1 open = *gX_Tools_RuntimeSkyFlag();
    if (!open) return;

    //ai: availability trace (not expansion) — the check gate asserts it.
    if (!s_skyeTraced) {
        gX_TraceLog(gX_LOG_INFO, "SKY_EDITOR_UI: panel=1");
        s_skyeTraced = 1;
    }
    //opencode: lazy init/fixture scan on first draw (the exe opener cannot
    //   call into the panel).
    if (!s_skyeFilesInit) {
        s_skyeFilesInit = 1;
        Skye_RefreshFiles();
        s_skyeTimeSec = gamedata.ephemeris.timeSec;
    }

    ImGui::SetNextWindowSize(ImVec2(560, 660), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("gX Tools - Sky Editor", &open)) {
        gX_RendererSky *sky = Skye_Active();
        if (!sky) {
            ImGui::TextUnformatted("sky module unavailable — run the --sky scene");
        } else {
            // ---- ephemeris clock ----
            ImGui::SeparatorText("time (ephemeris clock)");
            if (s_skyePlaying) {
                Skye_ApplyTime(s_skyeTimeSec + gX_FrameTime_Get() * (gX_F64)s_skyeSpeed);
            }
            gX_F32 tSec = (gX_F32)s_skyeTimeSec;
            ImGui::SetNextItemWidth(-170.0f);
            if (ImGui::SliderFloat("sec", &tSec, 0.0f, (gX_F32)GX_SKYE_YEAR_SEC * 2.0f, "%.0f")) {
                Skye_ApplyTime((gX_F64)tSec);
            }
            ImGui::SameLine();
            ImGui::Text("%.1f yr", s_skyeTimeSec / GX_SKYE_YEAR_SEC);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("speed (s/s)", &s_skyeSpeed, 10.0f, 0.0f, 7200.0f, "%.0f");
            ImGui::SameLine();
            if (ImGui::Button(s_skyePlaying ? "Pause" : "Play")) {
                s_skyePlaying = s_skyePlaying ? 0 : 1;
                Skye_Status(s_skyePlaying ? "clock running" : "clock paused");
            }
            ImGui::SameLine();
            ImGui::Text("sun elev %.2f", Skye_SunElevation());

            // ---- look parameters ----
            ImGui::SeparatorText("sky look");
            ImGui::SliderFloat("density", &sky->skyDensity, 0.0f, 3.0f);
            ImGui::SliderFloat("hue", &sky->skyHue, -1.0f, 1.0f);
            ImGui::SliderFloat("horizon glow", &sky->horizonGlow, 0.0f, 3.0f);
            ImGui::SliderFloat("contrast", &sky->skyContrast, 0.5f, 2.5f);
            ImGui::SliderFloat("altitude (km)", &sky->altitude, 0.0f, 5.0f);
            ImGui::SliderFloat("turbidity", &sky->turbidity, 1.0f, 10.0f);
            ImGui::SliderFloat("sun disk size", &sky->sunDiskSize, 100.0f, 2000.0f, "%.0f");
            ImGui::SliderFloat("sun intensity", &sky->sunDiskIntensity, 0.0f, 10.0f);

            ImGui::SeparatorText("stars + moon");
            ImGui::SliderFloat("star density", &sky->starDensity, 0.0f, 3.0f);
            ImGui::SliderFloat("star brightness", &sky->starBrightness, 0.0f, 3.0f);
            ImGui::SliderFloat("twinkle speed", &sky->starTwinkleSpeed, 0.0f, 5.0f);
            ImGui::SliderFloat("moon size", &sky->moonSize, 100.0f, 2000.0f, "%.0f");
            ImGui::SliderFloat("moon brightness", &sky->moonBrightness, 0.0f, 2.0f);
            bool helpers = sky->showHelpers != 0;
            if (ImGui::Checkbox("show helpers (horizon/grid/orbits)", &helpers)) {
                sky->showHelpers = helpers ? 1 : 0;
            }

            // ---- preset files ----
            ImGui::SeparatorText("presets (" GX_SKYE_DIR ")");
            ImGui::SetNextItemWidth(-215.0f);
            ImGui::InputText("##skyepath", (char *)s_skyePath, sizeof(s_skyePath));
            ImGui::SameLine();
            if (ImGui::Button("Load .json")) Skye_Load(sky, s_skyePath);
            ImGui::SameLine();
            if (ImGui::Button("Save .json")) {
                if (gX_RendererSky_Preset_Save(sky, s_skyePath) == 0) {
                    Skye_Status("saved -> %s", s_skyePath);
                    Skye_RefreshFiles();
                } else {
                    Skye_Status("SAVE FAILED -> %s", s_skyePath);
                }
            }
            ImGui::BeginChild("SkyeFiles", ImVec2(0, 66), true);
            if (s_skyeFileCount == 0) ImGui::TextUnformatted("(no sky presets found)");
            for (gX_I32 i = 0; i < s_skyeFileCount; i++) {
                if (ImGui::Selectable((const char *)s_skyeFiles[i])) {
                    snprintf((char *)s_skyePath, sizeof(s_skyePath), GX_SKYE_DIR "/%s",
                             (const char *)s_skyeFiles[i]);
                    Skye_Load(sky, s_skyePath);
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) Skye_RefreshFiles();
        }
        ImGui::TextUnformatted((const char *)s_skyeStatus);
    }
    ImGui::End();
    if (!open) *gX_Tools_RuntimeSkyFlag() = 0;
}

#else //!gX_ENABLE_RENDERER — no sky scene to edit (kernel profile)

extern "C" gX_I0 gX_Tools_Sky_Draw(gX_I0) { }

#endif
