/**********************************************************************************************
*
*   [gX: src/renderer/gfx/lensflare.c]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu
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


//opencode: lens flares — procedural screen-space flare based on sun position
#include <string.h>
#include <math.h>

#include "../gfx.h"
#include "lensflare.h"
#include "../renderer.h"

#include "../../wrappers/wrappers.h"

#include "../../main.h"
#include "../../camera.h"

#include "../../common/log.h"
#include "../../common/utils/shader.h"
//opencode: hunt - SetValue_* prototypes (implicit decl promoted float args to double, zeroing uploads)
#include "../../common/macros.h"
#include "../../common/profiler.h"


#include "../../gxmath/matrix.h"
#include "sun_uv.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_state.h"
//opencode: hunt - implicit-decl sweep

//ai: normalize flare distances and fullscreen coverage against the actual composite target.
gX_I0 gX_RendererGFX_LensFlare_Draw(gX_RendererGFX *gfx)
{
    if (!gfx->state.ENABLE_LENS_FLARE) return;
    if (!gfx->lensflare.shader.loaded) {
        gX_TraceLog_GFX(gX_LOG_WARNING, "lensflare: shader not loaded");
        return;
    }
    if (!gfx->bloom.scene.wrapper.loadingState.loaded) {
        gX_TraceLog_GFX(gX_LOG_WARNING, "lensflare: bloom.scene not loaded");
        return;
    }

    GX_PROFILE_ZONE_BEGIN("gfx:lensflare");

    //opencode: compute sun screen-space UV (shared helper — same projection as godray)
    gX_I1 sunBehind = false;
    gX_F32v2 sunUV = gX_RendererGFX_GetSunScreenUV(gfx, &sunBehind);
    if (sunBehind) { GX_PROFILE_ZONE_END("gfx:lensflare"); return; }
    {
        static gX_I1 logged = false;
        if (!logged) {
            gX_TraceLog_GFX(gX_LOG_NOTICE, "lensflare: sunUV=(%.3f,%.3f) intensity=%.2f dispersal=%.2f hdrSceneDepth=%d bloomScene=%d",
                sunUV.x, sunUV.y, gfx->lensflare.intensity, gfx->lensflare.dispersal,
                gfx->bloom.hdrScene.wrapper.loadingState.loaded,
                gfx->bloom.scene.wrapper.loadingState.loaded);
            logged = true;
        }
    }

    //opencode: single-pass full-screen lens flare — additive blend into hdrScene (HDR space)
    gX_RenderTexture *tgt = gfx->common.compositeTarget ? gfx->common.compositeTarget : &gfx->bloom.hdrScene.wrapper.rt;
    gX_Drawing_BeginTexture(*tgt);
        gX_I1 prevBlendFlare = gX_RHI_IsEnabled(gX_RHI_BLEND);
        gX_I32 prevBlendSrc, prevBlendDst, prevBlendEq;
        gX_RHI_GetIntegerv(gX_RHI_BLEND_SRC_RGB, &prevBlendSrc);
        gX_RHI_GetIntegerv(gX_RHI_BLEND_DST_RGB, &prevBlendDst);
        gX_RHI_GetIntegerv(gX_RHI_BLEND_EQUATION_RGB, &prevBlendEq);
        gX_RHI_Enable(gX_RHI_BLEND);
        gX_RHI_Blend_Func_Separate(gX_RHI_ONE, gX_RHI_ONE, gX_RHI_ONE, gX_RHI_ONE);
        gX_Shader sha = gfx->lensflare.shader.shader;
        gX_Drawing_BeginShader(sha);
        //opencode: bind uniforms AFTER BeginShader
        gX_Shader_SetValue_TextureSlot(sha, gfx->lensflare.sceneColorLoc,
            gfx->sceneColorLDR.wrapper.rt.texture, gX_GFX_TEXUNIT_SCENE);
        gX_Shader_SetValue_TextureSlot(sha, gfx->lensflare.sceneDepthLoc,
            gfx->bloom.hdrScene.wrapper.rt.depth, gX_GFX_TEXUNIT_DEPTH);
        gX_Shader_SetValue_V2(sha, gfx->lensflare.sunScreenPosLoc, sunUV);
        gX_Shader_SetValue_Float(sha, gfx->lensflare.intensityLoc, gfx->lensflare.intensity);
        gX_Shader_SetValue_Float(sha, gfx->lensflare.dispersalLoc, gfx->lensflare.dispersal);
        gX_Shader_SetValue_V2(sha, gfx->lensflare.resolutionLoc,
            (gX_F32v2){(gX_F32)tgt->texture.width, (gX_F32)tgt->texture.height});
            gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)tgt->texture.width, (gX_F32)tgt->texture.height);
        gX_Drawing_EndShader();
        gX_RHI_Blend_Equation((gX_U32)prevBlendEq);
        gX_RHI_Blend_Func_Separate((gX_U32)prevBlendSrc, (gX_U32)prevBlendDst, (gX_U32)prevBlendSrc, (gX_U32)prevBlendDst);
        if (!prevBlendFlare) gX_RHI_Disable(gX_RHI_BLEND);
    gX_Drawing_EndTexture();

    GX_PROFILE_ZONE_END("gfx:lensflare");
}

gX_I0 gX_RendererGFX_LensFlare_CacheLocs(gX_RendererGFX *gfx)
{
    if (gfx->lensflare.locsCached && !gfx->lensflare.shader.locsStale) return;
    if (!gfx->lensflare.shader.loaded) return;

    gX_Shader sha = gfx->lensflare.shader.shader;
    gfx->lensflare.sceneColorLoc  = gX_Shader_GetLocation(sha, "sceneColor");
    gfx->lensflare.sceneDepthLoc  = gX_Shader_GetLocation(sha, "sceneDepth");
    gfx->lensflare.sunScreenPosLoc = gX_Shader_GetLocation(sha, "sunScreenPos");
    gfx->lensflare.intensityLoc   = gX_Shader_GetLocation(sha, "intensity");
    gfx->lensflare.dispersalLoc   = gX_Shader_GetLocation(sha, "dispersal");
    gfx->lensflare.resolutionLoc  = gX_Shader_GetLocation(sha, "resolution");

    gfx->lensflare.locsCached = true;
    gfx->lensflare.shader.locsStale = false;
}
