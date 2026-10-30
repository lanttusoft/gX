/**********************************************************************************************
*
*   [gX: src/renderer/gfx/dof.c]
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


//opencode: depth of field — CoC compute + separable blur + composite
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "../gfx.h"
#include "dof.h"
#include "fullscreen_pass.h"
#include "../renderer.h"

#include "../../wrappers/wrappers.h"

#include "../../main.h"
#include "../../camera.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/profiler.h"
#include "../../common/utils/shader.h"
//opencode: hunt - SetValue_* prototypes (missing this header caused
//   implicit-declaration float->double promotion: focal/range/near/far
//   arrived as 0/0/-0/0 and DOF was a zero-effect no-op)


#include "../../rhi/rhi.h"
#include "../../rhi/rhi_fbo.h"
#include "../../rhi/rhi_gl.h"
#include "../../rhi/rhi_state.h"
#include "../../rhi/batch.h"
//opencode: hunt - implicit-decl sweep
//opencode: hunt - implicit-decl sweep (FBO forensics decls)
//opencode: Step 3 — one directional blur (was 2 near-identical H/V blocks
//   differing only in src/dir). Quad + res follow the DST size.
static gX_I0 Dof_BlurPass(gX_RendererGFX *gfx, gX_RenderTexture *dstRT, gX_Texture srcTex, gX_F32v2 dir, gX_F32v2 res, gX_F32 scaledRadius)
{
    gX_F32 w = (gX_F32)dstRT->texture.width;
    gX_F32 h = (gX_F32)dstRT->texture.height;
    gX_Shader bs = gfx->dof.blurShader.shader;
    gX_RendererGFX_Fullscreen_Begin(*dstRT, bs);
        //opencode: bind uniforms AFTER BeginShader
        gX_Shader_SetValue_TextureSlot(bs, gfx->dof.blurColorMapLoc, srcTex, gX_GFX_TEXUNIT_COLOR);
        gX_Shader_SetValue_TextureSlot(bs, gfx->dof.blurCocMapLoc, gfx->dof.cocMap.wrapper.rt.texture, gX_GFX_TEXUNIT_AUX);
        gX_Shader_SetValue_V2(bs, gfx->dof.blurDirLoc, dir);
        gX_Shader_SetValue_V2(bs, gfx->dof.blurResLoc, res);
        gX_Shader_SetValue_Float(bs, gfx->dof.blurMaxRadiusLoc, scaledRadius);
        gX_RendererGFX_Fullscreen_Draw(0, 0, w, h);
    gX_RendererGFX_Fullscreen_End();
}

//opencode: reduced DOF blur resolution is an opt-in experiment; the CoC and
//   final composite remain full resolution for focus classification and edges.
static gX_U32 Dof_ResolutionDivisor(gX_I0)
{
    const gX_I8 *hook = getenv("GX_T_DOF_RES");
    if (hook && atoi((const char *)hook) >= 2) return 4;
    if (hook && atoi((const char *)hook) == 1) return 2;
    return 1;
}

//opencode: one-shot float readback distinguishes non-finite DOF inputs
//   from black pixels introduced by the DOF shaders (bloom item-11 class:
//   NaN poisons convex math via 0*NaN or max(NaN,x)).
static gX_I0 Dof_DumpRTStats(const gX_I8 *name, gX_RenderTexture *rt)
{
    gX_U64 pixelCount = (gX_U64)rt->texture.width * (gX_U64)rt->texture.height;
    gX_F32 *pixels = (gX_F32 *)malloc((size_t)(pixelCount * 4 * sizeof(gX_F32)));
    if (!pixels) {
        gX_TraceLog_GFX(gX_LOG_ERROR, "DOF_TEX_STATS %s allocation failed", name);
        return;
    }
    gX_RHI_BindReadFBO(rt->id);
    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, rt->texture.width, rt->texture.height, GL_RGBA, GL_FLOAT, pixels);
    gX_RHI_UnbindReadFBO();
    gX_U64 nonFinite = 0;
    gX_F32 minValue = 1e30f, maxValue = -1e30f;
    for (gX_U64 i = 0; i < pixelCount * 4; i++) {
        if (!isfinite(pixels[i])) {
            nonFinite++;
            continue;
        }
        minValue = fminf(minValue, pixels[i]);
        maxValue = fmaxf(maxValue, pixels[i]);
    }
    gX_TraceLog_GFX(gX_LOG_NOTICE,
        "DOF_TEX_STATS %s pixels=%llu finite=[%.6f,%.6f] nonfinite=%llu",
        name, (unsigned long long)pixelCount, minValue, maxValue,
        (unsigned long long)nonFinite);
    free(pixels);
}

gX_I0 gX_RendererGFX_DOF_Draw(gX_RendererGFX *gfx)
{
    GX_PROFILE_ZONE_BEGIN("gfx:dof");

    if (!gfx->state.ENABLE_DOF || !gfx->dof.locsCached
        || !gfx->dof.cocMap.wrapper.loadingState.loaded
        || !gfx->dof.blurMap.wrapper.loadingState.loaded
        || !gfx->dof.blurPing.wrapper.loadingState.loaded
        || (Dof_ResolutionDivisor() > 1
            && !gfx->dof.compositeMap.wrapper.loadingState.loaded)
        || !gfx->bloom.hdrScene.wrapper.loadingState.loaded)
    {
        GX_PROFILE_ZONE_END("gfx:dof");
        return;
    }

    //opencode: after present, the direct renderer path has no composite target;
    //   use the presented canvas rather than re-reading pre-tonemap hdrScene.
    gX_RenderTexture *dofSrc = gfx->common.compositeTarget
        ? gfx->common.compositeTarget : &renderer.canvas;
    //opencode: hunt - one-shot location/RT audit (DOF zero-effect triage)
    {
        static gX_I1 dofAudited = 0;
        if (!dofAudited) { dofAudited = 1;
            gX_TraceLog(gX_LOG_NOTICE, "DOFAUDIT cached=%d coc=%d blur=%d,%d comp=%d,%d,%d,%d progs=%u,%u,%u",
                (int)gfx->dof.locsCached,
                (int)gfx->dof.cocDepthMapLoc,
                (int)gfx->dof.blurColorMapLoc, (int)gfx->dof.blurCocMapLoc,
                (int)gfx->dof.compColorMapLoc, (int)gfx->dof.compBlurMapLoc, (int)gfx->dof.compCocMapLoc,
                (int)gfx->dof.compDepthMapLoc,
                (unsigned)gfx->dof.cocShader.shader.id, (unsigned)gfx->dof.blurShader.shader.id,
                (unsigned)gfx->dof.compositeShader.shader.id);
            gX_TraceLog(gX_LOG_NOTICE, "DOFAUDIT rt coc=%dx%d blur=%dx%d ping=%dx%d",
                (int)gfx->dof.cocMap.wrapper.width, (int)gfx->dof.cocMap.wrapper.height,
                (int)gfx->dof.blurMap.wrapper.width, (int)gfx->dof.blurMap.wrapper.height,
                (int)gfx->dof.blurPing.wrapper.width, (int)gfx->dof.blurPing.wrapper.height);
            if (Dof_ResolutionDivisor() > 1)
                gX_TraceLog(gX_LOG_NOTICE, "DOFAUDIT composite=%dx%d",
                    (int)gfx->dof.compositeMap.wrapper.width,
                    (int)gfx->dof.compositeMap.wrapper.height);
            gX_TraceLog(gX_LOG_NOTICE, "DOFAUDIT ids cocrt=%u coctex=%u blurmaprt=%u blurmaptex=%u pingrt=%u pingtex=%u",
                (unsigned)gfx->dof.cocMap.wrapper.rt.id, (unsigned)gfx->dof.cocMap.wrapper.rt.texture.id,
                (unsigned)gfx->dof.blurMap.wrapper.rt.id, (unsigned)gfx->dof.blurMap.wrapper.rt.texture.id,
                (unsigned)gfx->dof.blurPing.wrapper.rt.id, (unsigned)gfx->dof.blurPing.wrapper.rt.texture.id);
            //opencode: hunt - FBO attachment audit (planar v0.1.33 class:
            //   wrapper recreates color texture, orphaning FBO attachments).
            //   Gated on GX_VALIDATE: raw GL calls here crashed under
            //   RenderDoc interception (leave the capture path clean).
            if (getenv("GX_VALIDATE")) {
                gX_U32 att0 = gX_RHI_FBO_ColorName(gfx->dof.cocMap.wrapper.rt.id);
                gX_U32 att1 = gX_RHI_FBO_ColorName(gfx->dof.blurMap.wrapper.rt.id);
                gX_U32 att2 = gX_RHI_FBO_ColorName(gfx->dof.blurPing.wrapper.rt.id);
                gX_TraceLog(gX_LOG_NOTICE, "DOFAUDIT attach coc=%u blurmap=%u ping=%u",
                    att0, att1, att2);
            }
            gX_TraceLog(gX_LOG_NOTICE, "DOFAUDIT coclocs res=%d focal=%d range=%d near=%d far=%d",
                (int)gfx->dof.cocResLoc, (int)gfx->dof.cocFocalDistLoc,
                (int)gfx->dof.cocFocalRangeLoc, (int)gfx->dof.cocNearLoc, (int)gfx->dof.cocFarLoc);
            gX_TraceLog(gX_LOG_NOTICE, "DOFAUDIT depthtex=%u samples=%u",
                (unsigned)gfx->bloom.hdrScene.wrapper.rt.depth.id,
                (unsigned)gX_RHI_Tex_Samples(gfx->bloom.hdrScene.wrapper.rt.depth.id));
            gX_TraceLog(gX_LOG_NOTICE, "DOFAUDIT cocvals focal=%.2f range=%.2f near=%.3f far=%.1f",
                (double)gfx->dof.focalDistance, (double)gfx->dof.focalRange,
                (double)camera.frustum.near, (double)camera.frustum.far);
            }
        }
    gX_F32v2 res = {(gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight};
    gX_F32v2 blurRes = {
        (gX_F32)gfx->dof.blurMap.wrapper.rt.texture.width,
        (gX_F32)gfx->dof.blurMap.wrapper.rt.texture.height};
    gX_U32 dofDivisor = Dof_ResolutionDivisor();
    //opencode: scale blur radius by the active blur resolution so the reduced
    //   path preserves the same full-resolution physical radius.
    gX_F32 scaledRadius = gfx->dof.maxRadius * (blurRes.x / 1920.0f);
    if (scaledRadius < 1.0f) scaledRadius = 1.0f;

    // 1. Compute CoC from depth
    gX_Shader cs = gfx->dof.cocShader.shader;
    gX_RendererGFX_Fullscreen_Begin(gfx->dof.cocMap.wrapper.rt, cs);
        gX_Drawing_ClearBG((gX_Color){0,0,0,0});
        //opencode: bind uniforms AFTER BeginShader
        gX_Shader_SetValue_TextureSlot(cs, gfx->dof.cocDepthMapLoc, gfx->bloom.hdrScene.wrapper.rt.depth, gX_GFX_TEXUNIT_DEPTH);
        gX_Shader_SetValue_V2(cs, gfx->dof.cocResLoc, res);
        gX_Shader_SetValue_Float(cs, gfx->dof.cocFocalDistLoc, gfx->dof.focalDistance);
        gX_Shader_SetValue_Float(cs, gfx->dof.cocFocalRangeLoc, gfx->dof.focalRange);
        gX_Shader_SetValue_Float(cs, gfx->dof.cocNearLoc, camera.frustum.near);
        gX_Shader_SetValue_Float(cs, gfx->dof.cocFarLoc, camera.frustum.far);
        gX_RendererGFX_Fullscreen_Draw(0, 0, res.x, res.y);
    gX_RendererGFX_Fullscreen_End();

    // 2. Blur H: color + CoC -> ping
    Dof_BlurPass(gfx, &gfx->dof.blurPing.wrapper.rt, dofSrc->texture,
        (gX_F32v2){1.0f, 0.0f}, blurRes, scaledRadius);

    // 3. Blur V: ping + CoC -> blurMap
    Dof_BlurPass(gfx, &gfx->dof.blurMap.wrapper.rt,
        gfx->dof.blurPing.wrapper.rt.texture, (gX_F32v2){0.0f, -1.0f},
        blurRes, scaledRadius);

    // 4. Composite: sharp + blur + CoC -> a separate full-resolution target
    gX_Shader comp = gfx->dof.compositeShader.shader;
    gX_RenderTexture *compositeRT = dofDivisor > 1
        ? &gfx->dof.compositeMap.wrapper.rt : &gfx->dof.blurPing.wrapper.rt;
    gX_RendererGFX_Fullscreen_Begin(*compositeRT, comp);
        //opencode: bind uniforms AFTER BeginShader
        gX_Shader_SetValue_TextureSlot(comp, gfx->dof.compColorMapLoc, dofSrc->texture, gX_GFX_TEXUNIT_SCENE);
        gX_Shader_SetValue_TextureSlot(comp, gfx->dof.compBlurMapLoc, gfx->dof.blurMap.wrapper.rt.texture, gX_GFX_TEXUNIT_COLOR);
        gX_Shader_SetValue_TextureSlot(comp, gfx->dof.compCocMapLoc, gfx->dof.cocMap.wrapper.rt.texture, gX_GFX_TEXUNIT_AUX);
        gX_Shader_SetValue_TextureSlot(comp, gfx->dof.compDepthMapLoc, gfx->bloom.hdrScene.wrapper.rt.depth, gX_GFX_TEXUNIT_DEPTH);
        gX_RendererGFX_Fullscreen_Draw(0, 0, res.x, res.y);
    gX_RendererGFX_Fullscreen_End();

    //opencode: 5. Copy the composite output -> canvas (or outputRT). Overwrites the bloom-tonemapped
    //   canvas with the DOF-composited result. This is safe because no subsequent pass
    //   (TAA, SSAO) reads the canvas — TAA reads hdrScene, SSAO reads hdrScene depth.
    //   The canvas is only read by the screen composite at the end of the frame.
    {
        gX_RenderTexture *dofOut = gfx->common.compositeTarget ? gfx->common.compositeTarget : &renderer.canvas;
        //opencode: raw FBO copy preserves the same HDR/LDR transfer as the
        //   presentation blit; a texture-draw copy applies a different path.
        gX_Batch_Flush();
        gX_RHI_BindReadFBO(compositeRT->id);
        gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
        gX_RHI_BindDrawFBO(dofOut->id);
        gX_RHI_DrawBuffer(gX_RHI_COLOR_ATTACHMENT0);
        gX_RHI_Blit(compositeRT->id, dofOut->id,
            0, 0, compositeRT->texture.width, compositeRT->texture.height,
            0, 0, dofOut->texture.width, dofOut->texture.height,
            gX_RHI_COLOR_BUFFER_BIT, gX_RHI_NEAREST);
        gX_RHI_UnbindReadFBO();
        gX_RHI_UnbindDrawFBO();
    }

    if (getenv("GX_DOF_TEX_STATS") && maindata.frameCount >= 30) {
        static gX_I1 dumped = false;
        if (!dumped) {
            dumped = true;
            Dof_DumpRTStats("hdrScene", &gfx->bloom.hdrScene.wrapper.rt);
            Dof_DumpRTStats("dof.coc", &gfx->dof.cocMap.wrapper.rt);
            Dof_DumpRTStats("dof.blurPing", &gfx->dof.blurPing.wrapper.rt);
            Dof_DumpRTStats("dof.blurMap", &gfx->dof.blurMap.wrapper.rt);
            if (dofDivisor > 1)
                Dof_DumpRTStats("dof.composite", &gfx->dof.compositeMap.wrapper.rt);
        }
    }

    GX_PROFILE_ZONE_END("gfx:dof");
}

gX_I0 gX_RendererGFX_DOF_CacheLocs(gX_RendererGFX *gfx)
{
    if (!gfx->dof.locsCached && gfx->dof.cocShader.loaded && gfx->dof.blurShader.loaded && gfx->dof.compositeShader.loaded)
    {
        gX_Shader cocS = gfx->dof.cocShader.shader;
        gfx->dof.cocDepthMapLoc  = gX_Shader_GetLocation(cocS, "depthMap");
        gfx->dof.cocResLoc       = gX_Shader_GetLocation(cocS, "resolution");
        gfx->dof.cocFocalDistLoc = gX_Shader_GetLocation(cocS, "focalDistance");
        gfx->dof.cocFocalRangeLoc = gX_Shader_GetLocation(cocS, "focalRange");
        gfx->dof.cocNearLoc      = gX_Shader_GetLocation(cocS, "nearPlane");
        gfx->dof.cocFarLoc       = gX_Shader_GetLocation(cocS, "farPlane");
        gX_Shader blrS = gfx->dof.blurShader.shader;
        gfx->dof.blurColorMapLoc  = gX_Shader_GetLocation(blrS, "colorMap");
        gfx->dof.blurCocMapLoc    = gX_Shader_GetLocation(blrS, "cocMap");
        gfx->dof.blurDirLoc       = gX_Shader_GetLocation(blrS, "direction");
        gfx->dof.blurResLoc       = gX_Shader_GetLocation(blrS, "resolution");
        gfx->dof.blurMaxRadiusLoc = gX_Shader_GetLocation(blrS, "maxRadius");
        gX_Shader cmpS = gfx->dof.compositeShader.shader;
        gfx->dof.compColorMapLoc = gX_Shader_GetLocation(cmpS, "colorMap");
        gfx->dof.compBlurMapLoc  = gX_Shader_GetLocation(cmpS, "blurMap");
        gfx->dof.compCocMapLoc   = gX_Shader_GetLocation(cmpS, "cocMap");
        gfx->dof.compDepthMapLoc = gX_Shader_GetLocation(cmpS, "depthMap");
        gfx->dof.locsCached = true;
    }
}
