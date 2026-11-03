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

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_FEATURES_H
