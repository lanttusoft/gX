/**********************************************************************************************
*
*   [gX: src/renderer/gfx/features.c]
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


#include <stdlib.h>

#include "../gfx.h"
#include "../../common/log.h"
#include "features.h"

gX_I1 gX_RendererGFX_Feature_HasSsgi(gX_RendererGFX *gfx)
{
    return (gfx->state.ENABLE_SSAO && gfx->ssao.useSSGI) ? 1 : 0;
}

gX_I1 gX_RendererGFX_Feature_HasEffects(gX_RendererGFX *gfx)
{
    return (gfx->state.ENABLE_BLOOM || gfx->state.ENABLE_GODRAYS ||
        gfx->state.ENABLE_FOG || gfx->state.ENABLE_LENS_FLARE ||
        gfx->state.ENABLE_SSR || gfx->state.ENABLE_VOLUME_FOG ||
        gfx->state.ENABLE_MOTION_BLUR || gfx->state.ENABLE_DOF ||
        gfx->state.ENABLE_PARTICLES || gX_RendererGFX_Feature_HasSsgi(gfx)) ? 1 : 0;
}

gX_RenderTexture *gX_RendererGFX_Feature_SceneColorTarget(gX_RendererGFX *gfx)
{
    if (!gfx) return NULL;
    if (gfx->bloom.hdrScene.wrapper.loadingState.loaded
        && gfx->bloom.hdrScene.wrapper.rt.texture.id > 0)
        return &gfx->bloom.hdrScene.wrapper.rt;
    if (gfx->sceneColorLDR.wrapper.loadingState.loaded
        && gfx->sceneColorLDR.wrapper.rt.texture.id > 0)
        return &gfx->sceneColorLDR.wrapper.rt;
    return NULL;
}

gX_I1 gX_RendererGFX_Feature_NeedHdr(gX_RendererGFX *gfx)
{
    return (gfx->state.ENABLE_BLOOM || gfx->state.ENABLE_GODRAYS ||
        gfx->state.ENABLE_LENS_FLARE || gfx->state.ENABLE_SSR ||
        gfx->state.ENABLE_DOF || gfx->state.ENABLE_MOTION_BLUR ||
        gfx->state.ENABLE_TAA || gfx->state.ENABLE_SSAO ||
        gfx->state.ENABLE_VOLUME_FOG) ? 1 : 0;
}

gX_I0 gX_RendererGFX_Feature_Trace(gX_RendererGFX *gfx)
{
    const gX_I8 *trace = getenv("GX_GFX_FEATURE_TRACE");
    static gX_I1 traced = 0;
    if (!trace || trace[0] == '0' || traced) return;
    traced = 1;
    gX_TraceLog_GFX(gX_LOG_NOTICE,
        "GFX_FEATURES hasEffects=%d needHdr=%d ssgi=%d bloom=%d ssr=%d fog=%d "
        "dof=%d motion=%d particles=%d | hdrLoaded=%d bloomSceneLoaded=%d",
        gX_RendererGFX_Feature_HasEffects(gfx),
        gX_RendererGFX_Feature_NeedHdr(gfx),
        gX_RendererGFX_Feature_HasSsgi(gfx),
        gfx->state.ENABLE_BLOOM ? 1 : 0,
        gfx->state.ENABLE_SSR ? 1 : 0,
        gfx->state.ENABLE_FOG ? 1 : 0,
        gfx->state.ENABLE_DOF ? 1 : 0,
        gfx->state.ENABLE_MOTION_BLUR ? 1 : 0,
        gfx->state.ENABLE_PARTICLES ? 1 : 0,
        gfx->bloom.hdrScene.wrapper.loadingState.loaded ? 1 : 0,
        gfx->bloom.scene.wrapper.loadingState.loaded ? 1 : 0);
}
