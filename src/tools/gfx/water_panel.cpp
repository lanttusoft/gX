/**********************************************************************************************
*
*   [gX: src/tools/gfx/water_panel.cpp]
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

//ai: raylib precedes gX's C-linkage include chain, matching the parent editor.
#include "raylib.h"
#include "raymath.h"
#include "../../common/types.h"
#if gX_ENABLE_RENDERER == 1
#include "surface_panels.h"
#include "../../renderer/renderer.h"
#include "../imgui/imgui.h"
#include <cstdlib>

//ai: authored water controls stay separate from diagnostic hooks and transient GPU ownership.
void gfx_editor_water_panel(gX_RendererGFX *gfx, gX_I32 *SelectedWave,
    gX_I0 (*BrowseNormal)(gX_RendererGFX *),
    gX_I1 (*ColorEdit)(const gX_I8 *, gX_Color *),
    gX_I0 (*TextWrapped)(const gX_I8 *))
{
    if (!ImGui::CollapsingHeader("Water", ImGuiTreeNodeFlags_DefaultOpen)) return;
    gX_I1 water = gfx->state.ENABLE_WATER ? 1 : 0;
    if (ImGui::Checkbox("enable water", (bool *)&water)) gfx->state.ENABLE_WATER = water;
    ImGui::SeparatorText("Surface");
    ImGui::DragFloat("plane height", &gfx->water.planeHeight, 0.05f, -10.0f, 10.0f);
    ColorEdit("water tint", &gfx->water.tint);
#ifdef GX_WATER_REFRACTION_EXPERIMENT
    gX_I1 sceneRefraction = gfx->water.sceneRefractionEnabled ? 1 : 0;
    const char *refractionOverride = getenv("GX_WATER_REFRACTION");
    ImGui::BeginDisabled(refractionOverride != NULL);
    if (ImGui::Checkbox("scene refraction (experimental)", (bool *)&sceneRefraction))
        gfx->water.sceneRefractionEnabled = sceneRefraction;
    ImGui::EndDisabled();
    TextWrapped("Above-water scene transmission; default off. Capture and diagnostic state are not saved.");
    if (refractionOverride)
        ImGui::TextDisabled("Launch override active: GX_WATER_REFRACTION=%s", refractionOverride);
#else
    TextWrapped("Scene refraction is available only in the experimental build.");
#endif
    ImGui::SeparatorText("Normal-map detail");
    ImGui::DragFloat("normal strength", &gfx->water.normalMapStrength, 0.02f, 0.0f, 2.0f);
    ImGui::DragFloat2("normal UV scale", (float *)&gfx->water.normalMapScale,
        0.05f, 0.01f, 100.0f);
    ImGui::DragFloat2("normal scroll U/V", (float *)&gfx->water.normalMapScroll,
        0.001f, -1.0f, 1.0f, "%.3f");
    TextWrapped("UV units per second. Negative reverses direction; zero stops that axis.");
    ImGui::DragFloat("procedural normal strength", &gfx->water.proceduralNormalStrength,
        0.02f, 0.0f, 2.0f, "%.2f");
    ImGui::DragFloat("procedural normal scale", &gfx->water.proceduralNormalScale,
        0.05f, 0.1f, 8.0f, "%.2f");
    TextWrapped("Procedural detail is shader-generated; scale sets its world-space frequency.");
    ImGui::TextWrapped("normal map: %s", gfx->water.normalMapPath[0]
        ? gfx->water.normalMapPath : "none");
    if (ImGui::Button("Browse water normal map...")) BrowseNormal(gfx);
    ImGui::SameLine();
    if (ImGui::Button("Clear normal map"))
    {
        if (gfx->water.normalMapTex.id > 0) gX_Texture_Unload(gfx->water.normalMapTex);
        gfx->water.normalMapTex = (gX_Texture){0};
        gfx->water.normalMapPath[0] = 0;
    }
    TextWrapped("Normal detail changes shading, not geometry.");
    ImGui::SeparatorText("Geometry waves");
    int wc = gfx->water.waveCount;
    if (wc < 0) wc = 0;
    if (wc > GX_WATER_MAX_WAVES) wc = GX_WATER_MAX_WAVES;
    if (ImGui::SliderInt("wave count", &wc, 0, GX_WATER_MAX_WAVES)) gfx->water.waveCount = wc;
    if (wc > 0)
    {
        if (*SelectedWave >= wc) *SelectedWave = wc - 1;
        if (*SelectedWave < 0) *SelectedWave = 0;
        ImGui::SliderInt("selected wave", SelectedWave, 0, wc - 1);
        ImGui::PushID(*SelectedWave);
        ImGui::SliderAngle("direction", &gfx->water.waveDirections[*SelectedWave], -180.0f, 180.0f);
        ImGui::DragFloat("amplitude", &gfx->water.waveAmplitudes[*SelectedWave], 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("frequency", &gfx->water.waveFrequencies[*SelectedWave], 0.01f, 0.0f, 8.0f);
        ImGui::DragFloat("speed", &gfx->water.waveSpeeds[*SelectedWave], 0.01f, -8.0f, 8.0f);
        ImGui::DragFloat("steepness", &gfx->water.waveSteepness[*SelectedWave], 0.01f, 0.0f, 1.0f);
        ImGui::PopID();
    }
    ImGui::SeparatorText("Ripple sources");
    gX_I1 automaticRipples = gfx->water.automaticRipples ? 1 : 0;
    if (ImGui::Checkbox("automatic water ripples", (bool *)&automaticRipples))
        gfx->water.automaticRipples = automaticRipples;
    TextWrapped("Stops new automatic ripples; existing ripples decay and splash ripples remain enabled.");
}
#endif
