/**********************************************************************************************
*
*   [gX: src/renderer/gfx/godray.c]
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


//opencode: god ray post-process — screen-space volumetric light scattering
#include <string.h>
#include <math.h>

#include "../gfx.h"
#include "godray.h"
#include "../renderer.h"

#include "../../wrappers/wrappers.h"

#include "../../main.h"
#include "../../camera.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/profiler.h"
#include "../../rhi/batch.h"


#include "../../gxmath/matrix.h"
#include "sun_uv.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_state.h"
#include "../../rhi/rhi_fbo.h"
//opencode: hunt - implicit-decl sweep

gX_I0 gX_RendererGFX_Godray_Draw(gX_RendererGFX *gfx)
{
    //opencode: skip if disabled or RTs not loaded
    if (!gfx->state.ENABLE_GODRAYS) return;
    if (!gfx->godray.genMap.wrapper.loadingState.loaded) {
        gX_TraceLog_GFX(gX_LOG_WARNING, "godray: genMap not loaded");
        return;
    }
    if (!gfx->godray.genShader.loaded) {
        gX_TraceLog_GFX(gX_LOG_WARNING, "godray: gen shader not loaded (id=%u)",
            gfx->godray.genShader.shader.id);
        return;
    }
    if (!gfx->bloom.hdrScene.wrapper.loadingState.loaded) {
        gX_TraceLog_GFX(gX_LOG_WARNING, "godray: hdrScene not loaded");
        return;
    }

    GX_PROFILE_ZONE_BEGIN("gfx:godray");

    gX_I32 gw = (gX_I32)gfx->godray.genMap.wrapper.width;
    gX_I32 gh = (gX_I32)gfx->godray.genMap.wrapper.height;
    if (gw < 1 || gh < 1) { GX_PROFILE_ZONE_END("gfx:godray"); return; }

    //opencode: compute sun screen-space UV (shared helper)
    gX_I1 sunBehind = false;
    gX_F32v2 sunUV = gX_RendererGFX_GetSunScreenUV(gfx, &sunBehind);
    if (sunBehind) {
        //opencode: log once — this fires every frame during orbit with godrays on
        static gX_I1 loggedSunBehind = false;
        if (!loggedSunBehind) {
            gX_TraceLog_GFX(gX_LOG_WARNING, "godray: sun behind (w<=0) — skipping");
            loggedSunBehind = true;
        }
        GX_PROFILE_ZONE_END("gfx:godray");
        return;
    }

    //opencode: 1) Generate godrays — half-res ray-march toward sun
    gX_Drawing_BeginTexture(gfx->godray.genMap.wrapper.rt);
        gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT | gX_RHI_DEPTH_BUFFER_BIT | gX_RHI_STENCIL_BUFFER_BIT);
        gX_Drawing_ClearBG((gX_Color){0, 0, 0, 255});
        gX_Shader genS = gfx->godray.genShader.shader;
        gX_Drawing_BeginShader(genS);
        //opencode: bind textures after BeginShader to avoid batch flush issues
        gX_Shader_SetValue_TextureSlot(genS, gfx->godray.genSceneColorLoc,
            gfx->bloom.hdrScene.wrapper.rt.texture, gX_GFX_TEXUNIT_SCENE);
        //opencode: upload sun screen position via direct GL (bypasses rlgl SetValue
        //   which has been observed to produce black output for float/int uniforms
        //   — see the R-2 hard-bug note). Direct glUniform is always correct.
        if (gfx->godray.genSunScreenPosLoc >= 0) {
            gX_RHI_Shader_Use((gX_U32)genS.id);
            gX_RHI_Uniform_Vec2(gfx->godray.genSunScreenPosLoc, sunUV.x, sunUV.y);
        }
        //opencode: upload the established godray controls; defaults preserve
        //   the former hardcoded four-sample response.
        gX_RHI_UniformInt(gfx->godray.genDensityLoc, gfx->godray.density);
        gX_RHI_Uniform_Float(gfx->godray.genDecayLoc, gfx->godray.decay);
        gX_RHI_Uniform_Float(gfx->godray.genWeightLoc, gfx->godray.weight);
        gX_RHI_Uniform_Float(gfx->godray.genExposureLoc, gfx->godray.exposure);
        //opencode: v0.3.3-audit — DrawScreenQuad instead of manual batch quads
        //   (same as fog_volume.c): the rlgl batch pipeline rebinds ITS OWN
        //   default program/texture for gX_Batch_ calls, so the godray gen
        //   shader never ran — genMap was filled by the batch's plain textured
        //   quad (white) and the additive composite whiteout the whole frame.
        gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)gw, (gX_F32)gh);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();


    //opencode: 2) Composite godrays over scene — draw genMap additive onto bloom.scene
    {
        gX_RenderTexture *tgt = gfx->common.compositeTarget ? gfx->common.compositeTarget : &gfx->bloom.hdrScene.wrapper.rt;
        gX_Drawing_BeginTexture(*tgt);
            gX_I1 prevBlendGod = gX_RHI_IsEnabled(gX_RHI_BLEND);
            gX_I32 prevBlendSrc, prevBlendDst, prevBlendEq;
            gX_RHI_GetIntegerv(gX_RHI_BLEND_SRC_RGB, &prevBlendSrc);
            gX_RHI_GetIntegerv(gX_RHI_BLEND_DST_RGB, &prevBlendDst);
            gX_RHI_GetIntegerv(gX_RHI_BLEND_EQUATION_RGB, &prevBlendEq);
            gX_RHI_Enable(gX_RHI_BLEND);
            gX_RHI_Blend_Func_Separate(gX_RHI_ONE, gX_RHI_ONE, gX_RHI_ONE, gX_RHI_ONE);
            gX_Texture_Draw(gfx->godray.genMap.wrapper.rt.texture, (gX_Texture_Draw_Params){
                .dst = (gX_Rectangle){0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight},
                .tint = gX_WHITE,
                .scale = {1, 1}
            });
            gX_Batch_Flush();
            gX_RHI_Blend_Equation((gX_U32)prevBlendEq);
            gX_RHI_Blend_Func_Separate((gX_U32)prevBlendSrc, (gX_U32)prevBlendDst, (gX_U32)prevBlendSrc, (gX_U32)prevBlendDst);
            if (!prevBlendGod) gX_RHI_Disable(gX_RHI_BLEND);
        gX_Drawing_EndTexture();
    }

    GX_PROFILE_ZONE_END("gfx:godray");
}

gX_I0 gX_RendererGFX_Godray_CacheLocs(gX_RendererGFX *gfx)
{
    if (gfx->godray.locsCached && !gfx->godray.genShader.locsStale) return;
    if (!gfx->godray.genShader.loaded) return;

    gX_Shader genS = gfx->godray.genShader.shader;
    gfx->godray.genSceneColorLoc  = gX_Shader_GetLocation(genS, "sceneColor");
    gfx->godray.genSceneDepthLoc  = gX_Shader_GetLocation(genS, "sceneDepth");
    gfx->godray.genSunScreenPosLoc= gX_Shader_GetLocation(genS, "sunScreenPos");
    gfx->godray.genDensityLoc     = gX_Shader_GetLocation(genS, "rayDensity");
    gfx->godray.genDecayLoc       = gX_Shader_GetLocation(genS, "rayDecay");
    gfx->godray.genWeightLoc      = gX_Shader_GetLocation(genS, "rayWeight");
    gfx->godray.genExposureLoc    = gX_Shader_GetLocation(genS, "rayExposure");

    gfx->godray.locsCached = true;
    gfx->godray.genShader.locsStale = false;
}
