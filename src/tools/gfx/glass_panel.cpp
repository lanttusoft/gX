/**********************************************************************************************
*
*   [gX: src/tools/gfx/glass_panel.cpp]
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

//ai: preserve the editor's raylib/C++ include ordering.
#include "raylib.h"
#include "raymath.h"
#include "../../common/types.h"
#if gX_ENABLE_RENDERER == 1
#include "surface_panels.h"
#include "../imgui/imgui.h"

//ai: one model-local transparency section reuses existing fields; no material preset is applied implicitly.
void gfx_editor_glass_panel(gX_RendererGFX_Model *model, gX_I32 ShaderQuality,
    gX_I1 (*ColorEdit)(const gX_I8 *, gX_Color *))
{
    if (!ImGui::CollapsingHeader("Transparency / Glass", ImGuiTreeNodeFlags_DefaultOpen)) return;
    gX_I1 blending = model->params.enableBlending ? 1 : 0;
    if (ImGui::Checkbox("blending", (bool *)&blending)) model->params.enableBlending = blending ? 1 : 0;
    ImGui::DragFloat("blend opacity", &model->params.blendOpacity, 0.01f, 0.0f, 1.0f);
    if (ShaderQuality < 1)
        ImGui::TextWrapped("Refraction is unavailable at Low shader quality. Choose Medium or High under Render > Quality. Saved refraction settings are preserved.");
    ImGui::BeginDisabled(ShaderQuality < 1);
    gX_I1 refraction = model->params.enableRefraction ? 1 : 0;
    if (ImGui::Checkbox("refraction", (bool *)&refraction)) model->params.enableRefraction = refraction;
    ImGui::DragFloat("refractive index", &model->params.refractiveIndex, 0.01f, 1.0f, 2.5f);
    ImGui::EndDisabled();
    ColorEdit("glass / surface tint", &model->params.albedoTint);
    ImGui::BeginDisabled(!model->params.usePBR);
    ImGui::DragFloat("roughness", &model->params.roughness, 0.01f, 0.0f, 1.0f);
    ImGui::EndDisabled();
    if (!model->params.usePBR) ImGui::TextDisabled("Enable PBR in Model > Material to edit roughness.");
    gX_I1 normal = model->params.hasNormalMap ? 1 : 0;
    if (ImGui::Checkbox("use normal map", (bool *)&normal)) model->params.hasNormalMap = normal ? 1 : 0;
    ImGui::DragFloat("normal strength", &model->params.normalMapFactor, 0.02f, 0.0f, 2.0f);
    ImGui::DragFloat2("normal UV scale", (float *)&model->params.normalMapScale, 0.05f, 0.01f, 100.0f);
    ImGui::DragFloat2("normal UV offset", (float *)&model->params.normalMapOffset, 0.01f, -10.0f, 10.0f);
    ImGui::TextWrapped("Normal texture assignment remains in Model > Normal map. Refraction uses the existing scene capture; layered glass is not supported.");
    if (model->params.enableRefraction && !model->params.enableBlending)
        ImGui::TextWrapped("Enable blending to use the transparent glass render path.");
}
#endif
