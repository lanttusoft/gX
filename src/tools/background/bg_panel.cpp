/**********************************************************************************************
*
*   [gX: src/tools/background/bg_panel.cpp]
*   Background tuning panel — shared body (extracted from gfx_editor.cpp)
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


//opencode: palette systems (Mother 2-style modes + the imgpro image
//   decomposer) and preset save/load, visible whenever a background module
//   instance exists (--background, and any scene wired to it). Shared by the
//   GFX editor and the standalone background editor.
//opencode: raylib headers FIRST with C++ linkage — wrapper headers reached
//   through renderer.h would give raymath's operator overloads C linkage
//   (see src/tools/gfx/gfx_editor.cpp).
#include "raylib.h"
#include "raymath.h"

#include "../imgui/imgui.h"

#include "../../common/log.h"
#include "../../renderer/renderer.h"
#include "bg_panel.h"

//opencode: kernel profile — the background module lives in the renderer
//   cluster, so the whole panel is guarded and both entry points become
//   no-ops there (previously the panel's renderer references broke
//   `make check-kernel`; same shape as the physics editor).
#if gX_ENABLE_RENDERER == 1

//ai: background panel — palette systems (Mother 2-style modes + the imgpro
//   image decomposer) and preset save/load. Visible whenever a background
//   module instance exists (--background, and any scene wired to it).
static gX_I8 s_bgImgProPath[512] = {0};
static gX_I8 s_bgPresetPath[512] = {0};
static gX_I8 s_bgPanelStatus[256] = {0};
static gX_I32 s_bgColorIndex = 0;
static gX_I1 s_bgPanelTraced = 0;

gX_RendererBackground *gX_Tools_BG_Active(gX_I0) {
    if (!renderer.background.data || renderer.background.amount == 0) return 0;
    return (gX_RendererBackground *)renderer.background.data;
}

static gX_I0 bg_color_edit(const gX_I8 *label,
    gX_RenBG_ParamV4 *param) {
    gX_F32v4 value = param->constant;
    float rgba[4] = {value.x, value.y, value.z, value.w};
    if (ImGui::ColorEdit4(label, rgba, ImGuiColorEditFlags_NoInputs))
        param->constant = (gX_F32v4){rgba[0], rgba[1], rgba[2], rgba[3]};
}

gX_I0 gX_Tools_BG_PanelBody(gX_I1 withHeader, gX_I1 withPresets) {
    gX_RendererBackground *bg = gX_Tools_BG_Active();
    if (!bg) return;
    //ai: trace availability (not expansion) — the gate asserts the panel path.
    if (!s_bgPanelTraced) {
        gX_TraceLog(gX_LOG_INFO, "GFX_BG_UI: panel=1");
        s_bgPanelTraced = 1;
    }
    if (withHeader && !ImGui::CollapsingHeader("Background")) return;

    ImGui::SeparatorText("Palette");
    bool paletteOn = bg->state.paletteEnabled != 0;
    if (ImGui::Checkbox("enabled##bgPalette", &paletteOn))
        bg->state.paletteEnabled = paletteOn ? 1 : 0;
    static const char *paletteModes[] = {
        "0 colors cycle", "1 wave posterize", "2 image index",
        "3 hue cycle", "4 bayer dither", "5 colors heatmap",
        "6 HSV split", "7 SNES row cycle"
    };
    gX_I32 paletteMode = bg->palette.mode;
    if (paletteMode < 0 || paletteMode > 7) paletteMode = 0;
    ImGui::SameLine();
    if (ImGui::BeginCombo("mode##bgPalette", paletteModes[paletteMode])) {
        for (gX_I32 mi = 0; mi < 8; mi++) {
            if (ImGui::Selectable(paletteModes[mi], mi == paletteMode)) {
                bg->palette.mode = mi;
                bg->state.paletteEnabled = 1;
            }
        }
        ImGui::EndCombo();
    }
    gX_F32 paletteSize = bg->palette.size.constant;
    if (ImGui::DragFloat("size##bgPalette", &paletteSize, 1.0f, 2.0f,
            256.0f, "%.0f"))
        bg->palette.size.constant = paletteSize;
    ImGui::SameLine();
    gX_I32 rowCount = bg->palette.rowCount;
    if (ImGui::DragInt("rows##bgPalette", &rowCount, 0.5f, 1, 256))
        bg->palette.rowCount = rowCount;
    gX_F32 amount = bg->palette.amount.constant;
    if (ImGui::DragFloat("amount##bgPalette", &amount, 0.5f, 1.0f, 64.0f,
            "%.0f"))
        bg->palette.amount.constant = amount;
    ImGui::SameLine();
    gX_F32 paletteSpeed = bg->palette.speed.constant;
    if (ImGui::DragFloat("speed##bgPalette", &paletteSpeed, 0.005f, -4.0f,
            4.0f, "%.3f"))
        bg->palette.speed.constant = paletteSpeed;
    gX_F32 paletteFrequency = bg->palette.frequency.constant;
    if (ImGui::DragFloat("frequency##bgPalette", &paletteFrequency, 0.01f,
            -16.0f, 16.0f, "%.3f"))
        bg->palette.frequency.constant = paletteFrequency;
    ImGui::SameLine();
    gX_F32 bgBoundary = bg->palette.bgBoundary.constant;
    if (ImGui::DragFloat("bg cutoff##bgPalette", &bgBoundary, 0.005f, 0.0f,
            1.0f, "%.3f"))
        bg->palette.bgBoundary.constant = bgBoundary;
    gX_F32 fgBoundary = bg->palette.fgBoundary.constant;
    if (ImGui::DragFloat("fg cutoff##bgPalette", &fgBoundary, 0.005f, 0.0f,
            1.0f, "%.3f"))
        bg->palette.fgBoundary.constant = fgBoundary;
    bg_color_edit("color1##bgPalette", &bg->palette.color1);
    ImGui::SameLine();
    bg_color_edit("color2##bgPalette", &bg->palette.color2);
    ImGui::SameLine();
    bg_color_edit("bg##bgPalette", &bg->palette.bgColor);
    ImGui::SameLine();
    bg_color_edit("fg##bgPalette", &bg->palette.fgColor);
    {
        gX_I32 count = (gX_I32)bg->palette.size.constant;
        if (count < 2) count = 2;
        if (count > (gX_I32)gX_BG_PALETTE_COLORS_MAX)
            count = (gX_I32)gX_BG_PALETTE_COLORS_MAX;
        ImVec2 stripSize(ImGui::GetContentRegionAvail().x, 14.0f);
        ImVec2 stripPos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##bgPaletteStrip", stripSize);
        ImDrawList *draw = ImGui::GetWindowDrawList();
        for (gX_I32 ci = 0; ci < count; ci++) {
            gX_F32v4 entry = bg->palette.colors[ci].constant;
            draw->AddRectFilled(
                ImVec2(stripPos.x + stripSize.x * (gX_F32)ci / (gX_F32)count,
                    stripPos.y),
                ImVec2(stripPos.x + stripSize.x * (gX_F32)(ci + 1) / (gX_F32)count
                    + 1.0f, stripPos.y + stripSize.y),
                IM_COL32((gX_I32)(entry.x * 255.0f), (gX_I32)(entry.y * 255.0f),
                    (gX_I32)(entry.z * 255.0f), 255));
        }
        if (s_bgColorIndex >= count) s_bgColorIndex = count - 1;
        ImGui::DragInt("color index##bgPalette", &s_bgColorIndex, 0.5f, 0,
            count - 1);
        ImGui::SameLine();
        gX_F32v4 entry = bg->palette.colors[s_bgColorIndex].constant;
        float rgba[4] = {entry.x, entry.y, entry.z, entry.w};
        if (ImGui::ColorEdit4("##bgPaletteColorEdit", rgba,
                ImGuiColorEditFlags_NoInputs)) {
            bg->palette.colors[s_bgColorIndex].constant =
                (gX_F32v4){rgba[0], rgba[1], rgba[2], rgba[3]};
            bg->state.colorsDirty = true;
        }
    }
    {
        static const char *blendModes[] = {"alpha", "add", "subtract",
            "multiply", "screen", "difference", "overlay", "dodge"};
        gX_I32 blendMode = bg->palette.blendMode;
        if (blendMode < 0 || blendMode > 7) blendMode = 0;
        if (ImGui::BeginCombo("blend##bgPalette", blendModes[blendMode])) {
            for (gX_I32 bi = 0; bi < 8; bi++)
                if (ImGui::Selectable(blendModes[bi], bi == blendMode))
                    bg->palette.blendMode = bi;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        static const char *colorMathModes[] = {"off", "add", "subtract",
            "half add"};
        gX_I32 colorMath = bg->palette.colorMathMode;
        if (colorMath < 0 || colorMath > 3) colorMath = 0;
        if (ImGui::BeginCombo("color math##bgPalette",
                colorMathModes[colorMath])) {
            for (gX_I32 cmi = 0; cmi < 4; cmi++)
                if (ImGui::Selectable(colorMathModes[cmi], cmi == colorMath))
                    bg->palette.colorMathMode = cmi;
            ImGui::EndCombo();
        }
    }

    ImGui::SeparatorText("Image decomposer");
    ImGui::InputTextWithHint("##bgImgProPath", "source image path",
        (char *)s_bgImgProPath, sizeof(s_bgImgProPath));
    gX_I32 paletteMax = (gX_I32)bg->imgpro.paletteMax;
    if (ImGui::DragInt("max colors##bgImgPro", &paletteMax, 1.0f, 2, 256))
        bg->imgpro.paletteMax = (gX_U32)paletteMax;
    ImGui::SameLine();
    if (ImGui::Button("Run decomposer##bgImgPro")) {
        if (s_bgImgProPath[0]) {
            gX_RendererBackground_ImgPro_Run(bg, s_bgImgProPath);
            snprintf((char *)s_bgPanelStatus, sizeof(s_bgPanelStatus),
                "decomposed: %u colors (mode %d)", bg->imgpro.paletteSize,
                bg->palette.mode);
            gX_TraceLog(gX_LOG_INFO, "GFX_BG_UI: imgpro run size=%u",
                bg->imgpro.paletteSize);
        } else {
            snprintf((char *)s_bgPanelStatus, sizeof(s_bgPanelStatus),
                "set a source image path first");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear##bgImgPro")) {
        gX_RendererBackground_ImgPro_Unload(bg);
        bg->state.paletteEnabled = 0;
        snprintf((char *)s_bgPanelStatus, sizeof(s_bgPanelStatus),
            "decomposer cleared");
    }
    ImGui::Text("palette texture %u | decomposed %u colors",
        bg->palette.paletteTexture.id, bg->imgpro.paletteSize);

    if (withPresets) {
        ImGui::SeparatorText("Preset");
        static const gX_I8 *bgPresetNames[] = {
    #define X(preset) (const gX_I8 *)#preset,
            gX_BG_PRESETS
    #undef X
        };
        static gX_I32 s_bgPreset = 0;
        const gX_I32 bgPresetCount =
            (gX_I32)(sizeof(bgPresetNames) / sizeof(bgPresetNames[0]));
        if (s_bgPreset >= bgPresetCount) s_bgPreset = 0;
        if (ImGui::BeginCombo("built-in##bgPreset",
                (const char *)bgPresetNames[s_bgPreset])) {
            for (gX_I32 pi = 0; pi < bgPresetCount; pi++)
                if (ImGui::Selectable((const char *)bgPresetNames[pi],
                        pi == s_bgPreset))
                    s_bgPreset = pi;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load##bgPreset")) {
            gX_RendererBackground_Preset_BuiltIn(bg, s_bgPreset);
            bg->state.colorsDirty = true;
            bg->state.sinusoidDirty = true;
            bg->state.paramsDirty = true;
            snprintf((char *)s_bgPanelStatus, sizeof(s_bgPanelStatus),
                "built-in %s loaded", (const char *)bgPresetNames[s_bgPreset]);
        }
        ImGui::InputTextWithHint("##bgPresetPath", "preset json path",
            (char *)s_bgPresetPath, sizeof(s_bgPresetPath));
        if (ImGui::Button("Save##bgPresetFile")) {
            if (s_bgPresetPath[0]) {
                gX_RendererBackground_Preset_Save(bg, s_bgPresetPath);
                snprintf((char *)s_bgPanelStatus, sizeof(s_bgPanelStatus),
                    "saved %.*s", (gX_I32)sizeof(s_bgPanelStatus) - 16,
                    (const char *)s_bgPresetPath);
            } else {
                snprintf((char *)s_bgPanelStatus, sizeof(s_bgPanelStatus),
                    "set a preset path first");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load##bgPresetFile")) {
            if (s_bgPresetPath[0]) {
                gX_RendererBackground_Preset_Load(bg, s_bgPresetPath);
                bg->state.colorsDirty = true;
                bg->state.sinusoidDirty = true;
                bg->state.paramsDirty = true;
                snprintf((char *)s_bgPanelStatus, sizeof(s_bgPanelStatus),
                    "loaded %.*s", (gX_I32)sizeof(s_bgPanelStatus) - 16,
                    (const char *)s_bgPresetPath);
            } else {
                snprintf((char *)s_bgPanelStatus, sizeof(s_bgPanelStatus),
                    "set a preset path first");
            }
        }
    }
    if (s_bgPanelStatus[0]) ImGui::TextWrapped("%s", s_bgPanelStatus);
}

#else //!gX_ENABLE_RENDERER — no background module to tune (kernel profile)

gX_RendererBackground *gX_Tools_BG_Active(gX_I0) { return 0; }
gX_I0 gX_Tools_BG_PanelBody(gX_I1, gX_I1) { }

#endif
