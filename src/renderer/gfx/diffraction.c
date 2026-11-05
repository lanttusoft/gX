/**********************************************************************************************
*
*   [gX: src/renderer/gfx/diffraction.c]
*   gX — a game engine
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


//ai: aperture diffraction spikes — generate a half-res star map from HDR
//   highlights, then add it back to hdrScene before bloom/tone mapping.
#include <string.h>
#include <math.h>

#include "../gfx.h"
#include "diffraction.h"
#include "../renderer.h"

#include "../../wrappers/wrappers.h"
#include "../../main.h"
#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/profiler.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_state.h"
#include "../../rhi/batch.h"

gX_I0 gX_RendererGFX_Diffraction_CacheLocs(gX_RendererGFX *gfx)
{
    if (gfx->diffraction.locsCached
        && !gfx->diffraction.shader.locsStale) return;
    if (!gfx->diffraction.shader.loaded) return;

    gX_Shader shader = gfx->diffraction.shader.shader;
    gfx->diffraction.sceneColorLoc =
        gX_Shader_GetLocation(shader, "sceneColor");
    gfx->diffraction.resolutionLoc =
        gX_Shader_GetLocation(shader, "resolution");
    gfx->diffraction.thresholdLoc =
        gX_Shader_GetLocation(shader, "threshold");
    gfx->diffraction.kneeLoc = gX_Shader_GetLocation(shader, "knee");
    gfx->diffraction.intensityLoc =
        gX_Shader_GetLocation(shader, "intensity");
    gfx->diffraction.lengthLoc =
        gX_Shader_GetLocation(shader, "lengthUV");
    gfx->diffraction.chromaticLoc =
        gX_Shader_GetLocation(shader, "chromatic");
    gfx->diffraction.rotationLoc =
        gX_Shader_GetLocation(shader, "rotation");
    gfx->diffraction.sparkleLoc =
        gX_Shader_GetLocation(shader, "sparkle");
    gfx->diffraction.bladesLoc = gX_Shader_GetLocation(shader, "blades");
    gfx->diffraction.tapsLoc = gX_Shader_GetLocation(shader, "taps");
    gfx->diffraction.timeLoc = gX_Shader_GetLocation(shader, "time");

    gfx->diffraction.locsCached = true;
    gfx->diffraction.shader.locsStale = false;
}

gX_I0 gX_RendererGFX_Diffraction_Draw(gX_RendererGFX *gfx)
{
    if (!gfx->state.ENABLE_DIFFRACTION) return;
    if (!gfx->diffraction.shader.loaded) return;
    if (!gfx->bloom.hdrScene.wrapper.loadingState.loaded) return;
    if (!gfx->diffraction.map.wrapper.loadingState.loaded) return;

    GX_PROFILE_ZONE_BEGIN("gfx:diffraction");
    gX_RendererGFX_Diffraction_CacheLocs(gfx);
    {
        static gX_I1 logged = 0;
        if (!logged) {
            gX_TraceLog_GFX(gX_LOG_NOTICE,
                "diffraction: threshold=%.2f knee=%.2f intensity=%.2f "
                "length=%.3f chromatic=%.2f blades=%d taps=%d map=%dx%d",
                gfx->diffraction.threshold, gfx->diffraction.knee,
                gfx->diffraction.intensity, gfx->diffraction.length,
                gfx->diffraction.chromatic, gfx->diffraction.blades,
                gfx->diffraction.taps,
                (gX_I32)gfx->diffraction.map.wrapper.width,
                (gX_I32)gfx->diffraction.map.wrapper.height);
            logged = 1;
        }
    }

    gX_RenderTexture *source = &gfx->bloom.hdrScene.wrapper.rt;
    gX_RenderTexture *target = &gfx->diffraction.map.wrapper.rt;

    //ai: generate the half-res star map from the current HDR scene.
    gX_Drawing_BeginTexture(*target);
        gX_Drawing_ClearBG((gX_Color){0, 0, 0, 255});
        gX_Shader shader = gfx->diffraction.shader.shader;
        gX_Drawing_BeginShader(shader);
        gX_Shader_SetValue_TextureSlot(shader,
            gfx->diffraction.sceneColorLoc, source->texture,
            gX_GFX_TEXUNIT_SCENE);
        gX_RHI_Uniform_Vec2(gfx->diffraction.resolutionLoc,
            (gX_F32)target->texture.width, (gX_F32)target->texture.height);
        gX_RHI_Uniform_Float(gfx->diffraction.thresholdLoc,
            gfx->diffraction.threshold);
        gX_RHI_Uniform_Float(gfx->diffraction.kneeLoc,
            gfx->diffraction.knee);
        gX_RHI_Uniform_Float(gfx->diffraction.intensityLoc,
            gfx->diffraction.intensity);
        gX_RHI_Uniform_Float(gfx->diffraction.lengthLoc,
            gfx->diffraction.length);
        gX_RHI_Uniform_Float(gfx->diffraction.chromaticLoc,
            gfx->diffraction.chromatic);
        gX_RHI_Uniform_Float(gfx->diffraction.rotationLoc,
            gfx->diffraction.rotation);
        gX_RHI_Uniform_Float(gfx->diffraction.sparkleLoc,
            gfx->diffraction.sparkle);
        gX_RHI_UniformInt(gfx->diffraction.bladesLoc,
            gfx->diffraction.blades);
        gX_RHI_UniformInt(gfx->diffraction.tapsLoc,
            gfx->diffraction.taps);
        gX_RHI_Uniform_Float(gfx->diffraction.timeLoc,
            (gX_F32)maindata.timeElapsed);
        gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)target->texture.width,
            (gX_F32)target->texture.height);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();

    //ai: add the star map to the presented composite; running after bloom
    //   keeps the spikes sharp instead of blurring them into the halo.
    gX_RenderTexture *composite = gfx->common.compositeTarget
        ? gfx->common.compositeTarget : &gfx->bloom.scene.wrapper.rt;
    gX_I1 previousBlend = gX_RHI_IsEnabled(gX_RHI_BLEND);
    gX_I32 previousSrc = 0, previousDst = 0, previousEq = 0;
    gX_RHI_GetIntegerv(gX_RHI_BLEND_SRC_RGB, &previousSrc);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_DST_RGB, &previousDst);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_EQUATION_RGB, &previousEq);
    gX_Drawing_BeginTexture(*composite);
        gX_RHI_Enable(gX_RHI_BLEND);
        gX_RHI_Blend_Func_Separate(gX_RHI_ONE, gX_RHI_ONE,
            gX_RHI_ONE, gX_RHI_ONE);
        gX_Texture_Draw(target->texture, (gX_Texture_Draw_Params){
            .dst = (gX_Rectangle){0, 0, (gX_F32)maindata.renderWidth,
                (gX_F32)maindata.renderHeight},
            .tint = gX_WHITE, .scale = {1, 1}
        });
        gX_Batch_Flush();
    gX_Drawing_EndTexture();
    gX_RHI_Blend_Equation((gX_U32)previousEq);
    gX_RHI_Blend_Func_Separate((gX_U32)previousSrc, (gX_U32)previousDst,
        (gX_U32)previousSrc, (gX_U32)previousDst);
    if (!previousBlend) gX_RHI_Disable(gX_RHI_BLEND);

    GX_PROFILE_ZONE_END("gfx:diffraction");
}
