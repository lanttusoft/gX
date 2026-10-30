/**********************************************************************************************
*
*   [gX: src/tools/synth/wavetable_editor.cpp]
*   Wavetable editor — draw/generate synth wavetables + raw float32 fixtures
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


//opencode: engine-native surface for the synth's wavetable slots (like the
//   texture generator panel is for textures): generate a shape, draw on the
//   waveform, preview it, and round-trip the slot as a raw float32 fixture
//   (gX_Synth_WT_LoadFile / gX_Synth_WT_SaveFile). Opened by
//   --wavetable-editor, GX_TOOLS_OPEN=wavetable, or the title EXTRAS menu;
//   exclusive with the other runtime editors.
#include "../imgui/imgui.h"

#include "../../audio/synth.h"
#include "../../common/log.h"
#include "../../wrappers/file.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern "C" gX_I1 *gX_Tools_RuntimeWavetableFlag(gX_I0);

#define GX_WT_EDITOR_PATH_MAX 256
#define GX_WT_EDITOR_FILE_MAX 16
#define GX_WT_EDITOR_DIR "resources/audio/wavetable"

static gX_I32 s_wtSlot = 0;
static gX_I32 s_wtShape = 0;  // 0 sine .. 3 triangle (GX_SYNTH_WAVE_*)
static gX_I32 s_wtPreviewVoice = -1;
static gX_I8  s_wtPath[GX_WT_EDITOR_PATH_MAX] = GX_WT_EDITOR_DIR "/my_table.f32";
static gX_I8  s_wtStatus[192] = "ready";
static gX_I32 s_wtLastIndex = -1;
static gX_F32 s_wtLastValue = 0.5f;
static gX_I8  s_wtFiles[GX_WT_EDITOR_FILE_MAX][128];
static gX_I32 s_wtFileCount = 0;

static gX_I0 WtEditor_Status(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf((char *)s_wtStatus, sizeof(s_wtStatus), fmt, args);
    va_end(args);
}

static const char *WtEditor_ShapeName(gX_I32 shape) {
    switch (shape) {
        case GX_SYNTH_WAVE_SQUARE:   return "square";
        case GX_SYNTH_WAVE_SAWTOOTH: return "saw";
        case GX_SYNTH_WAVE_TRIANGLE: return "triangle";
        default:                     return "sine";
    }
}

static gX_Synth_Wavetable *WtEditor_Slot(void) {
    if (s_wtSlot < 0 || s_wtSlot >= GX_SYNTH_MAX_WT) s_wtSlot = 0;
    return &g_synth.wavetables[s_wtSlot];
}

static gX_I0 WtEditor_StopPreview(void) {
    if (s_wtPreviewVoice >= 0) {
        gX_Synth_NoteOff((gX_U32)s_wtPreviewVoice);
        s_wtPreviewVoice = -1;
    }
}

static gX_I0 WtEditor_RefreshFiles(void) {
    s_wtFileCount = 0;
    gX_File_Directory listing;
    if (gX_File_Directory_List(GX_WT_EDITOR_DIR, &listing) > 0) {
        for (gX_I32 i = 0; i < listing.count && s_wtFileCount < GX_WT_EDITOR_FILE_MAX; i++) {
            const gX_I8 *name = listing.names[i];
            const gX_I8 *dot = strrchr((const char *)name, '.');
            if (!dot || strcmp((const char *)dot, ".f32") != 0) continue;
            strncpy((char *)s_wtFiles[s_wtFileCount], (const char *)name, 127);
            s_wtFiles[s_wtFileCount][127] = '\0';
            s_wtFileCount++;
        }
    }
}

//opencode: an empty slot gets a sine so the canvas is drawable right away.
static gX_I0 WtEditor_EnsureLoaded(void) {
    gX_Synth_Wavetable *wt = WtEditor_Slot();
    if (!wt->loaded) {
        gX_Synth_WT_Generate((gX_U32)s_wtSlot, GX_SYNTH_WAVE_SINE);
        WtEditor_Status("slot %d generated (sine)", s_wtSlot);
    }
}

extern "C" gX_I0 gX_Tools_Wavetable_Reset(gX_I0);
extern "C" gX_I0 gX_Tools_Wavetable_Reset(gX_I0) {
    s_wtPreviewVoice = -1;
    s_wtLastIndex = -1;
    WtEditor_RefreshFiles();
    WtEditor_EnsureLoaded();
    WtEditor_Status("ready — drag on the waveform, Save writes raw .f32");
}

extern "C" gX_I0 gX_Tools_Wavetable_Shutdown(gX_I0);
extern "C" gX_I0 gX_Tools_Wavetable_Shutdown(gX_I0) {
    WtEditor_StopPreview();
}

extern "C" gX_I0 gX_Tools_Wavetable_Draw(gX_I0);
extern "C" gX_I0 gX_Tools_Wavetable_Draw(gX_I0) {
    gX_I1 open = *gX_Tools_RuntimeWavetableFlag();
    if (!open) {
        WtEditor_StopPreview();
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("gX Tools - Wavetable Editor", &open)) {
        gX_Synth_Wavetable *wt = WtEditor_Slot();

        static const char *slotItems = "0\0" "1\0" "2\0" "3\0";
        gX_I32 prevSlot = s_wtSlot;
        ImGui::SetNextItemWidth(60.0f);
        ImGui::Combo("slot", &s_wtSlot, slotItems);
        if (s_wtSlot != prevSlot) {
            WtEditor_StopPreview();
            WtEditor_EnsureLoaded();
            WtEditor_Status("slot %d", s_wtSlot);
            wt = WtEditor_Slot();
        }
        ImGui::SameLine();
        ImGui::Text("(%s, %u samples)", wt->loaded ? "loaded" : "empty", wt->size);

        static const char *shapeItems = "sine\0" "square\0" "saw\0" "triangle\0";
        ImGui::SetNextItemWidth(110.0f);
        ImGui::Combo("shape", &s_wtShape, shapeItems);
        ImGui::SameLine();
        if (ImGui::Button("Generate")) {
            gX_Synth_WT_Generate((gX_U32)s_wtSlot, (gX_I32)s_wtShape);
            WtEditor_Status("generated %s in slot %d", WtEditor_ShapeName(s_wtShape), s_wtSlot);
        }
        ImGui::SameLine();
        if (ImGui::Button("Normalize")) {
            if (wt->loaded && wt->size > 1) {
                gX_F32 lo = wt->data[0];
                gX_F32 hi = wt->data[0];
                for (gX_U32 i = 1; i < wt->size; i++) {
                    if (wt->data[i] < lo) lo = wt->data[i];
                    if (wt->data[i] > hi) hi = wt->data[i];
                }
                gX_F32 span = hi - lo;
                if (span > 1e-6f) {
                    for (gX_U32 i = 0; i < wt->size; i++) wt->data[i] = (wt->data[i] - lo) / span;
                }
                WtEditor_Status("normalized slot %d", s_wtSlot);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply to synth")) {
            g_synth.defaultWaveform = GX_SYNTH_WAVE_WAVETABLE;
            g_synth.defaultWavetableIndex = s_wtSlot;
            if (s_wtSlot >= (gX_I32)g_synth.wavetableCount) {
                g_synth.wavetableCount = (gX_U32)(s_wtSlot + 1);
            }
            WtEditor_Status("applied slot %d — new notes use WAVETABLE", s_wtSlot);
        }

        // ---- waveform canvas (drag to draw) ----
        ImGui::SeparatorText("waveform (drag to draw)");
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        gX_F32 canvasW = ImGui::GetContentRegionAvail().x;
        gX_F32 canvasH = 150.0f;
        ImGui::InvisibleButton("wt_canvas", ImVec2(canvasW, canvasH));
        gX_I1 active = ImGui::IsItemActive();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 canvasMax(canvasPos.x + canvasW, canvasPos.y + canvasH);
        dl->AddRectFilled(canvasPos, canvasMax, IM_COL32(20, 24, 34, 235));
        dl->AddLine(ImVec2(canvasPos.x, canvasPos.y + canvasH * 0.5f),
                    ImVec2(canvasMax.x, canvasPos.y + canvasH * 0.5f),
                    IM_COL32(70, 90, 120, 110));
        dl->AddRect(canvasPos, canvasMax, IM_COL32(70, 90, 120, 200));
        if (wt->loaded && wt->size > 1) {
            const gX_I32 segs = 192;
            ImVec2 prev(canvasPos.x, canvasPos.y + canvasH);
            for (gX_I32 i = 0; i <= segs; i++) {
                gX_U32 si = (gX_U32)((gX_I64)i * (gX_I64)(wt->size - 1) / segs);
                ImVec2 p(canvasPos.x + canvasW * (gX_F32)i / (gX_F32)segs,
                         canvasPos.y + canvasH * (1.0f - wt->data[si]));
                if (i > 0) dl->AddLine(prev, p, IM_COL32(120, 220, 180, 255), 1.5f);
                prev = p;
            }
        }

        if (active && wt->loaded && wt->size > 1) {
            ImVec2 m = ImGui::GetIO().MousePos;
            gX_F32 t = (m.x - canvasPos.x) / canvasW;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            gX_F32 v = 1.0f - (m.y - canvasPos.y) / canvasH;
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            gX_I32 idx = (gX_I32)(t * (gX_F32)(wt->size - 1));
            if (s_wtLastIndex >= 0 && s_wtLastIndex != idx) {
                //opencode: fill the gap so fast drags leave no unpainted run.
                gX_I32 step = (idx > s_wtLastIndex) ? 1 : -1;
                gX_I32 span = (idx > s_wtLastIndex) ? (idx - s_wtLastIndex) : (s_wtLastIndex - idx);
                for (gX_I32 k = 1; k <= span; k++) {
                    gX_I32 ii = s_wtLastIndex + k * step;
                    gX_F32 f = (gX_F32)k / (gX_F32)span;
                    wt->data[ii] = s_wtLastValue + (v - s_wtLastValue) * f;
                }
            } else {
                wt->data[idx] = v;
            }
            s_wtLastIndex = idx;
            s_wtLastValue = v;
        } else {
            s_wtLastIndex = -1;
        }

        // ---- preview ----
        if (s_wtPreviewVoice < 0) {
            if (ImGui::Button("Preview note (220 Hz)")) {
                g_synth.defaultWavetableIndex = s_wtSlot;
                s_wtPreviewVoice = gX_Synth_NoteOn(220.0f, 0.7f, GX_SYNTH_WAVE_WAVETABLE);
                WtEditor_Status("preview playing (slot %d)", s_wtSlot);
            }
        } else {
            if (ImGui::Button("Stop preview")) {
                WtEditor_StopPreview();
                WtEditor_Status("preview stopped");
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("preview running");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("muted output stays muted");

        // ---- fixture round trip ----
        ImGui::SeparatorText("file (raw little-endian float32)");
        ImGui::SetNextItemWidth(-150.0f);
        ImGui::InputText("##wtpath", (char *)s_wtPath, sizeof(s_wtPath));
        ImGui::SameLine();
        if (ImGui::Button("Save .f32")) {
            gX_I32 rc = gX_Synth_WT_SaveFile((gX_U32)s_wtSlot, s_wtPath);
            if (rc == 0) {
                WtEditor_Status("saved slot %d -> %s", s_wtSlot, s_wtPath);
                WtEditor_RefreshFiles();
            } else {
                WtEditor_Status("SAVE FAILED -> %s", s_wtPath);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load .f32")) {
            gX_I32 rc = gX_Synth_WT_LoadFile((gX_U32)s_wtSlot, s_wtPath);
            if (rc == 0) WtEditor_Status("loaded %s -> slot %d", s_wtPath, s_wtSlot);
            else WtEditor_Status("LOAD FAILED <- %s", s_wtPath);
            wt = WtEditor_Slot();
        }

        ImGui::BeginChild("WtEditorFiles", ImVec2(0, 110), true);
        if (s_wtFileCount == 0) {
            ImGui::TextUnformatted("(no .f32 fixtures found)");
        }
        for (gX_I32 i = 0; i < s_wtFileCount; i++) {
            if (ImGui::Selectable((const char *)s_wtFiles[i])) {
                snprintf((char *)s_wtPath, sizeof(s_wtPath), GX_WT_EDITOR_DIR "/%s",
                         (const char *)s_wtFiles[i]);
                if (gX_Synth_WT_LoadFile((gX_U32)s_wtSlot, s_wtPath) == 0) {
                    WtEditor_Status("loaded fixture %s -> slot %d", s_wtFiles[i], s_wtSlot);
                } else {
                    WtEditor_Status("LOAD FAILED <- %s", s_wtPath);
                }
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) WtEditor_RefreshFiles();
        ImGui::TextUnformatted((const char *)s_wtStatus);
    }
    ImGui::End();
    if (!open) *gX_Tools_RuntimeWavetableFlag() = 0;
}
