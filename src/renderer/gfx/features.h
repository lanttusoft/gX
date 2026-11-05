/**********************************************************************************************
*
*   [gX: src/renderer/gfx/features.h]
*   gX — shared GFX feature predicates
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


//ai: one source of truth for the scene feature closure. The pass table and
//   the render-target lifecycle both ask these predicates instead of keeping
//   separate hand-maintained ENABLE_* lists that drift (DOF/motion/particles
//   used the composite target but did not allocate it).
#ifndef RENDERER_GFX_FEATURES_H
#define RENDERER_GFX_FEATURES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "../../wrappers/texture.h"

typedef struct gX_RendererGFX gX_RendererGFX;

//ai: public manifest row for the editor and tests.
typedef struct gX_RendererGFX_FeatureInfo {
    const gX_I8 *id;
    const gX_I8 *label;
    gX_U32 dependencies;
    gX_I1 needsHdr;
    gX_I1 hasEffects;
    gX_I1 contextOnly;   // scene-driven, not a user toggle
    gX_I32 cost;         // 0 cheap, 1 medium, 2 expensive
    const gX_I8 *passes;
    const gX_I8 *targets;
} gX_RendererGFX_FeatureInfo;

//opencode: any pre-present effect that needs the composite chain.
gX_I1 gX_RendererGFX_Feature_HasEffects(gX_RendererGFX *gfx);
//opencode: scene-linear HDR color/depth is required.
gX_I1 gX_RendererGFX_Feature_NeedHdr(gX_RendererGFX *gfx);
//opencode: SSGI is enabled inside the SSAO group.
gX_I1 gX_RendererGFX_Feature_HasSsgi(gX_RendererGFX *gfx);
//opencode: current scene color target — HDR when allocated, LDR otherwise.
gX_RenderTexture *gX_RendererGFX_Feature_SceneColorTarget(gX_RendererGFX *gfx);
//opencode: GX_GFX_FEATURE_TRACE=1 one-shot closure readout.
gX_I0 gX_RendererGFX_Feature_Trace(gX_RendererGFX *gfx);
//ai: GX_GFX_PLAN=1 one-shot resolved feature plan (enabled set, HDR/effect
//   closure, missing dependency mask and per-feature passes/targets).
gX_I0 gX_RendererGFX_Feature_PlanLog(gX_RendererGFX *gfx);
//ai: manifest accessors and the dependency-aware setter used by the editor.
gX_U32 gX_RendererGFX_Feature_Count(gX_I0);
const gX_RendererGFX_FeatureInfo *gX_RendererGFX_Feature_InfoAt(gX_U32 id);
gX_I1 gX_RendererGFX_Feature_IsEnabled(gX_RendererGFX *gfx, gX_U32 id);
gX_I0 gX_RendererGFX_Feature_Set(gX_RendererGFX *gfx, gX_U32 id,
    gX_I1 enabled);
gX_I0 gX_RendererGFX_Feature_ApplyTemplate(gX_RendererGFX *gfx,
    const gX_I8 *name);
//ai: one-click Console 240p Max — quality tier of the scene's authored
//   features. Full adds the experimental all-effects template on top.
gX_I0 gX_RendererGFX_ConsoleMax_Apply(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_ConsoleMaxFull_Apply(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_FEATURES_H
