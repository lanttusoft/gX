/**********************************************************************************************
*
*   [gX: src/renderer/gfx/taa.c]
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


//opencode: TAA resolve — temporal accumulation with jitter + history
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../gfx.h"
#include "taa.h"
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
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_fbo.h"
#include "../../rhi/rhi_gl.h"
#include "../../rhi/rhi_state.h"
//ai: flush pending presentation draws before changing data-target blend state.
#include "../../rhi/batch.h"
//ai: opt-in sampler-identity trace uses the RHI texture query path.
#include "../../rhi/rhi_tex.h"
//opencode: hunt - implicit-decl sweep

//ai: inspect object bytes via memcpy, not pointer arithmetic across individual matrix members.
static gX_I1 gX_RendererGFX_TAA_MatrixFinite(gX_Matrix Matrix)
{
    gX_F32 Values[16];
    memcpy(Values,&Matrix,sizeof(Values));
    for (gX_U32 Index = 0; Index < 16; Index++)
        if (!isfinite(Values[Index])) return false;
    return true;
}

//ai: oracle count converts before incrementing; unsafe matrices bypass reprojection without non-finite uploads.
// Opt-in inverse and skipped/aborted-pass fixtures never change the beauty camera.
// Retained history requires a consecutive committed resolve, not merely a previous pipeline save.
gX_I0 gX_RendererGFX_TAA_Resolve(gX_RendererGFX *gfx)
{
    GX_PROFILE_ZONE_BEGIN("gfx:taa");

    if (!gfx->state.ENABLE_TAA || !gfx->taa.locsCached
        || !gfx->taa.historyMap.wrapper.loadingState.loaded
        || !gfx->taa.resolveMap.wrapper.loadingState.loaded
        || !gfx->bloom.hdrScene.wrapper.loadingState.loaded)
    {
        GX_PROFILE_ZONE_END("gfx:taa");
        return;
    }

    const gX_I8 *GapHook = getenv("GX_TAA_HISTORY_GAP");
    gX_I32 GapProbe = GapHook && maindata.frameCount == 19 ? atoi((const char *)GapHook) : 0;
    if (GapProbe == 1)
    {
        gX_TraceLog_GFX(gX_LOG_NOTICE,"TAA_GAP_TRACE kind=1 last=%llu",(unsigned long long)gfx->taa.lastResolveFrame);
        GX_PROFILE_ZONE_END("gfx:taa");
        return;
    }

    const gX_I8 *temporalHook = getenv("GX_RAYMARCH_TEMPORAL");
    gX_I1 hybridActive = temporalHook && temporalHook[0] != '0'
        && renderer.state.raymarchDepth == 2 && renderer.raymarch.data && renderer.raymarch.amount;
    gX_U64 signature = hybridActive
        ? gX_RendererRaymarch_Temporal_Signature(renderer.raymarch.data) : 0;
    gX_I1 historyReset = hybridActive != gfx->taa.hybridHistoryActive
        || (hybridActive && signature != gfx->taa.hybridSignature)
        || (gfx->taa.temporal.historyValid && maindata.frameCount != gfx->taa.lastResolveFrame + 1);
    if (historyReset)
    {
        gX_RendererGFX_TemporalHistory_Reset(&gfx->taa.temporal);
        gX_Drawing_BeginTexture(gfx->taa.historyMap.wrapper.rt);
        gX_Drawing_ClearBG((gX_Color){0,0,0,0});
        gX_Drawing_EndTexture();
        gX_Drawing_BeginTexture(gfx->taa.resolveMap.wrapper.rt);
        gX_Drawing_ClearBG((gX_Color){0,0,0,0});
        gX_Drawing_EndTexture();
        gX_RendererGFX_TemporalHistory_MarkCleared(&gfx->taa.temporal);
    }
    gfx->taa.hybridHistoryActive = hybridActive;
    gfx->taa.hybridSignature = signature;

    gX_RendererGFX_TemporalHistoryTargets temporalTargets;
    if (!gX_RendererGFX_TemporalHistory_BeginPass(&gfx->taa.temporal,
            &gfx->taa.historyMap, &gfx->taa.resolveMap, &temporalTargets)) {
        GX_PROFILE_ZONE_END("gfx:taa");
        return;
    }
    gX_RendererGFX_RenderTexture *taaHistory = temporalTargets.read;
    gX_RendererGFX_RenderTexture *taaResolve = temporalTargets.write;
    if (GapProbe == 2)
    {
        gX_RendererGFX_TemporalHistory_AbortPass(&gfx->taa.temporal);
        gX_TraceLog_GFX(gX_LOG_NOTICE,"TAA_GAP_TRACE kind=2 last=%llu",(unsigned long long)gfx->taa.lastResolveFrame);
        GX_PROFILE_ZONE_END("gfx:taa");
        return;
    }

    //opencode: post-blit source convention (see Particles/MotionBlur/Dof):
    //   posttarget retargets compositeTarget to outputRT (NULL on the canvas
    //   path), and each post-blit pass re-resolves NULL to the canvas itself.
    //   The old hdrScene fallback resolved the pre-composite buffer, so TAA
    //   silently discarded every composite-stage effect (SSR/bloom/fog) and
    //   pasted the raw frame over the presented canvas (measured 2026-09-15:
    //   TAA-on output pixel-identical to SSR-off). The canvas holds this
    //   frame's presented composite here (present runs before TAA;
    //   overlays/HUD run after), which is the correct accumulation input.
    gX_RenderTexture *taaSrc = gfx->common.compositeTarget ? gfx->common.compositeTarget : &renderer.canvas;
    gX_F32v2 res = {(gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight};
    //opencode: oracle (Paper 1) - INPUT witness: taaSrc RGB mean BEFORE the
    //   resolve draws (the routing copy overwrites taaSrc post-resolve, so a
    //   late read would witness our own output, not the fresh render).
    static gX_F64 s_oracleInputMean = -1.0;
    //opencode: oracle (Paper 1) - full snapshots for the hull check
    //   (entry input + entry history; resolve read at end).
    static gX_F32 *s_snapIn = NULL;
    static gX_F32 *s_snapHist = NULL;
    static gX_U64 s_snapN = 0;
    {
        const gX_I8 *oracleStats = getenv("GX_T_ORACLE");
        const gX_I8 *taaStats = getenv("GX_T_TAASTATS");
        if (((oracleStats && oracleStats[0] != '0') || (taaStats && taaStats[0] != '0'))
            && (maindata.frameCount % 10) == 0 && maindata.frameCount > 0 && taaSrc) {
            gX_U64 inW = taaSrc->texture.width;
            gX_U64 inH = taaSrc->texture.height;
            gX_U64 inN = inW * inH;
            gX_F32 *inPx = (gX_F32 *)malloc((size_t)(inN * 4 * sizeof(gX_F32)));
            if (inPx) {
                gX_RHI_BindReadFBO(taaSrc->id);
                gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
                glReadPixels(0, 0, (gX_I32)inW, (gX_I32)inH,
                    GL_RGBA, GL_FLOAT, inPx);
                gX_RHI_UnbindReadFBO();
                gX_F64 inAcc = 0.0;
                for (gX_U64 i = 0; i < inN; i++)
                    inAcc += (gX_F64)inPx[i * 4]
                        + (gX_F64)inPx[i * 4 + 1]
                        + (gX_F64)inPx[i * 4 + 2];
                s_oracleInputMean = inAcc / (gX_F64)(inN * 3);
                //opencode: oracle (Paper 1) - retain full input snapshot.
                if (s_snapN != inN) {
                    free(s_snapIn);
                    free(s_snapHist);
                    s_snapIn = (gX_F32 *)malloc((size_t)(inN * 4 * sizeof(gX_F32)));
                    s_snapHist = (gX_F32 *)malloc((size_t)(inN * 4 * sizeof(gX_F32)));
                    s_snapN = (s_snapIn && s_snapHist) ? inN : 0;
                }
                if (s_snapN == inN && s_snapIn)
                    memcpy(s_snapIn, inPx, (size_t)(inN * 4 * sizeof(gX_F32)));
                free(inPx);
            }
        }
    }
    //opencode: oracle (Paper 1) - snapshot historyMap as the shader will
    //   read it (before the resolve draws + copies overwrite it).
    {
        const gX_I8 *oracleStats = getenv("GX_T_ORACLE");
        const gX_I8 *taaStats = getenv("GX_T_TAASTATS");
        if (((oracleStats && oracleStats[0] != '0') || (taaStats && taaStats[0] != '0'))
            && (maindata.frameCount % 10) == 0 && maindata.frameCount > 0
            && s_snapN > 0
            && gfx->taa.historyMap.wrapper.loadingState.loaded) {
            gX_U64 hW = gfx->taa.historyMap.wrapper.rt.texture.width;
            gX_U64 hH = gfx->taa.historyMap.wrapper.rt.texture.height;
            if (hW * hH == s_snapN) {
                gX_RHI_BindReadFBO(gfx->taa.historyMap.wrapper.rt.id);
                gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
                glReadPixels(0, 0, (gX_I32)hW, (gX_I32)hH,
                    GL_RGBA, GL_FLOAT, s_snapHist);
                gX_RHI_UnbindReadFBO();
            }
        }
    }

    // Compute inverse VP for world position reconstruction
    gX_Matrix curView = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
    gX_F32 curAspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix curProj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, curAspect,
        camera.frustum.near, camera.frustum.far);
    gX_Matrix curVP = gX_Matrix_Multiply(curView, curProj);
    const gX_I8 *UnsafeProbe = getenv("GX_TAA_UNSAFE_REPROJECTION");
    if (UnsafeProbe && strcmp((const char *)UnsafeProbe,"singular") == 0) curVP = (gX_Matrix){0};
    gX_Matrix invVP = gX_Matrix_Invert(curVP);
    gX_Matrix PreviousVP = gfx->taaPrevVP;
    if (UnsafeProbe)
    {
        if (strcmp((const char *)UnsafeProbe,"worldzero") == 0) invVP = (gX_Matrix){0};
        if (strcmp((const char *)UnsafeProbe,"worldtiny") == 0)
        {
            invVP = gX_Matrix_Identity();
            invVP.m15 = 1.0e-5f;
        }
        if (strcmp((const char *)UnsafeProbe,"prevzero") == 0) PreviousVP = (gX_Matrix){0};
        if (strcmp((const char *)UnsafeProbe,"prevnegative") == 0) PreviousVP.m15 = -1000.0f;
        if (strcmp((const char *)UnsafeProbe,"worldoverflow") == 0
            || strcmp((const char *)UnsafeProbe,"prevoverflow") == 0)
        {
            gX_Matrix Overflow = gX_Matrix_Identity();
            Overflow.m0 = Overflow.m5 = Overflow.m10 = 1.0e38f;
            Overflow.m15 = 1.0e-4f;
            if (strcmp((const char *)UnsafeProbe,"worldoverflow") == 0) invVP = Overflow;
            else PreviousVP = Overflow;
        }
        if (strcmp((const char *)UnsafeProbe,"nan") == 0) invVP.m0 = NAN;
        if (strcmp((const char *)UnsafeProbe,"inf") == 0) PreviousVP.m0 = INFINITY;
    }
    gX_I1 ReprojectionValid = gX_RendererGFX_TAA_MatrixFinite(curVP)
        && gX_RendererGFX_TAA_MatrixFinite(invVP) && gX_RendererGFX_TAA_MatrixFinite(PreviousVP);
    if (!ReprojectionValid)
    {
        invVP = gX_Matrix_Identity();
        PreviousVP = gX_Matrix_Identity();
    }

    // TAA resolve: read from composited output, blend with history
    //ai: history alpha is depth metadata, never a coverage weight for framebuffer blending.
    gX_Batch_Flush();
    gX_I1 prevBlendTAA = gX_RHI_IsEnabled(gX_RHI_BLEND);
    //ai: fullscreen color/depth-metadata writes must not test against stale private RT depth.
    gX_I1 prevDepthTAA = gX_RHI_IsEnabled(gX_RHI_DEPTH_TEST);
    //ai: fullscreen data writes have no meaningful front/back surface ownership.
    gX_I1 prevCullTAA = gX_RHI_IsEnabled(gX_RHI_CULL_FACE);
    gX_I1 prevDepthWriteTAA = false;
    gX_RHI_GetBooleanv(gX_RHI_DEPTH_WRITEMASK,&prevDepthWriteTAA);
    gX_RHI_Disable(gX_RHI_BLEND);
    gX_RHI_Disable(gX_RHI_DEPTH_TEST);
    gX_RHI_Disable(gX_RHI_CULL_FACE);
    gX_RHI_Depth_Write(0);
    gX_Drawing_BeginTexture(taaResolve->wrapper.rt);
        gX_Shader ts = gfx->taa.resolveShader.shader;
        gX_Drawing_BeginShader(ts);
        //opencode: bind uniforms AFTER BeginShader
        gX_Shader_SetValue_TextureSlot(ts, gfx->taa.colorMapLoc, taaSrc->texture, gX_GFX_TEXUNIT_COLOR);
        gX_Shader_SetValue_TextureSlot(ts, gfx->taa.depthMapLoc, gfx->bloom.hdrScene.wrapper.rt.depth, gX_GFX_TEXUNIT_DEPTH);
        gX_Shader_SetValue_TextureSlot(ts, gfx->taa.historyMapLoc, taaHistory->wrapper.rt.texture, gX_GFX_TEXUNIT_AUX);
        gX_Shader_SetValue_Int(ts, gfx->taa.reprojectionValidLoc, (gX_I32)ReprojectionValid);
        gX_Shader_SetValue_Matrix(ts, gfx->taa.prevVPLoc, PreviousVP);
        gX_Shader_SetValue_Matrix(ts, gfx->taa.invVPLoc, invVP);
        gX_Shader_SetValue_V2(ts, gfx->taa.resolutionLoc, res);
        gX_Shader_SetValue_Float(ts, gfx->taa.blendFactorLoc, gfx->taa.blendFactor);
        //opencode: oracle spike (Paper 1) - GX_T_ORACLE=1 turns the EMA into
        //   a running average (weight 1-1/N): static scenes must converge
        //   pixel-stable (variance -> 0). Jitter keeps running (Halton), so
        //   this accumulates genuinely varying samples, not identical
        //   frames. History starts empty at launch; enable from launch.
        {
            const gX_I8 *oracle = getenv("GX_T_ORACLE");
            if (oracle && oracle[0] != '0') {
                gX_F32 n = (gX_F32)gX_RendererGFX_TemporalHistory_FrameIndex(&gfx->taa.temporal) + 1.0f;
                if (n < 1.0f) n = 1.0f;
                gX_Shader_SetValue_Float(ts, gfx->taa.blendFactorLoc,
                    1.0f - 1.0f / n);
                //opencode: oracle (Paper 1) - clamp bypass rides the same
                //   hook: production TAA (hook off) keeps clamping.
                if (gfx->taa.oracleAccumLoc >= 0)
                    gX_Shader_SetValue_Int(ts, gfx->taa.oracleAccumLoc, 1);
                static gX_I1 oracleLogged = 0;
                if (!oracleLogged) { oracleLogged = 1;
                    gX_TraceLog(gX_LOG_NOTICE, "ORACLE spike active: running-average resolve"); }
            } else if (gfx->taa.oracleAccumLoc >= 0) {
                gX_Shader_SetValue_Int(ts, gfx->taa.oracleAccumLoc, 0);
            }
        }
        gX_Shader_SetValue_V2(ts, gfx->taa.jitterLoc, gfx->taa.jitter);
        gX_Shader_SetValue_V2(ts, gfx->taa.prevJitterLoc, gfx->taa.prevJitter);
        //ai: default resolve output is unchanged; native probes inspect actual consumer coordinates.
        const gX_I8 *hybridDebug = getenv("GX_TAA_REPROJECTION_DEBUG");
        const gX_I8 *hybridDebugFrame = getenv("GX_TAA_REPROJECTION_DEBUG_FRAME");
        gX_I1 debugFrameActive = !hybridDebugFrame
            || maindata.frameCount == (gX_U64)strtoull(hybridDebugFrame,NULL,10);
        gX_Shader_SetValue_Int(ts,gfx->taa.hybridReprojectionDebugLoc,
            hybridDebug && debugFrameActive ? atoi(hybridDebug) : 0);
        //ai: pin the consumer's jitter phase in retained analytic reprojection probes.
        const gX_I8 *reprojectionTrace = getenv("GX_T_VELOCITY_TRACE");
        if (reprojectionTrace && maindata.frameCount == (gX_U64)strtoull(reprojectionTrace,NULL,10))
            gX_TraceLog_GFX(gX_LOG_NOTICE,
                "TAA_REPROJECTION_TRACE jitter=(%.9g,%.9g) prevJitter=(%.9g,%.9g)",
                gfx->taa.jitter.x,gfx->taa.jitter.y,gfx->taa.prevJitter.x,gfx->taa.prevJitter.y);
        //opencode: v0.1.38 — motion vectors from the dedicated buffer when it
        //   rendered this frame; the depth-reconstruction fallback stays.
        //opencode: velocity-path probe (GX_T_NOVELOCITY=1 forces the
        //   depth-reconstruction fallback; the velocity pass re-sets
        //   hasPrevVP every draw, so the hook must live here, not in Update).
        gX_I1 noVelocity = (getenv("GX_T_NOVELOCITY") != NULL
            && getenv("GX_T_NOVELOCITY")[0] != '0');
        if (gfx->velocity.map.wrapper.loadingState.loaded && gfx->velocity.hasPrevVP) {
            if (gfx->taa.velocityMapLoc >= 0)
                gX_Shader_SetValue_TextureSlot(ts, gfx->taa.velocityMapLoc,
                    gfx->velocity.map.wrapper.rt.texture, gX_GFX_TEXUNIT_VELOCITY);
            if (gfx->taa.hasVelocityLoc >= 0)
                gX_Shader_SetValue_Int(ts, gfx->taa.hasVelocityLoc, noVelocity ? 2 : 1);
        } else if (gfx->taa.hasVelocityLoc >= 0) {
            gX_Shader_SetValue_Int(ts, gfx->taa.hasVelocityLoc, 0);
        }
        //ai: retain sampler/FBO identity to distinguish history sampling from an analytic rejection error.
        if (reprojectionTrace && maindata.frameCount == (gX_U64)strtoull(reprojectionTrace,NULL,10))
        {
            gX_I32 historyUnit = -1;
            gX_I32 historyBinding = 0;
            gX_RHI_GetUniformiv(ts.id,gfx->taa.historyMapLoc,1,&historyUnit);
            gX_RHI_InvalidateCaches();
            gX_RHI_ActiveTexture(gX_RHI_TEXTURE0 + gX_GFX_TEXUNIT_AUX);
            gX_RHI_GetIntegerv(gX_RHI_TEXTURE_BINDING_2D,&historyBinding);
            gX_RHI_ActiveTexture(gX_RHI_TEXTURE0);
            gX_TraceLog_GFX(gX_LOG_NOTICE,
                "TAA_REPROJECTION_TRACE history read=%u write=%u unit=%d bound=%d reset=%d attachment=(%u,%u)",
                taaHistory->wrapper.rt.texture.id,taaResolve->wrapper.rt.texture.id,
                historyUnit,historyBinding,(gX_I32)historyReset,
                gX_RHI_FBO_ColorName(taaHistory->wrapper.rt.id),
                gX_RHI_FBO_ColorName(taaResolve->wrapper.rt.id));
        }
        //ai: isolate pending setup errors from the immediate fullscreen draw in retained probes.
        if (reprojectionTrace && maindata.frameCount == (gX_U64)strtoull(reprojectionTrace,NULL,10))
            gX_TraceLog_GFX(gX_LOG_NOTICE,"TAA_REPROJECTION_TRACE before draw error=0x%x",gX_RHI_GetError());
        gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
        if (reprojectionTrace && maindata.frameCount == (gX_U64)strtoull(reprojectionTrace,NULL,10))
            gX_TraceLog_GFX(gX_LOG_NOTICE,"TAA_REPROJECTION_TRACE after draw error=0x%x",gX_RHI_GetError());
        //ai: compare the immediate resolve output with the later inspector, before any history copies.
        if (reprojectionTrace && maindata.frameCount == (gX_U64)strtoull(reprojectionTrace,NULL,10))
        {
            gX_I32 debugValue = -1;
            gX_I32 actualProgram = 0;
            gX_F32 sample[4] = {0};
            gX_U32 savedRead = gX_RHI_GetReadFBO();
            gX_RHI_GetUniformiv(ts.id,gfx->taa.hybridReprojectionDebugLoc,1,&debugValue);
            gX_RHI_GetIntegerv(gX_RHI_CURRENT_PROGRAM,&actualProgram);
            gX_RHI_BindReadFBO(taaResolve->wrapper.rt.id);
            gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
            gX_RHI_ReadPixels(maindata.renderWidth/2,maindata.renderHeight/2,1,1,gX_RHI_RGBA,gX_RHI_FLOAT,sample);
            gX_RHI_BindReadFBO(savedRead);
            gX_TraceLog_GFX(gX_LOG_NOTICE,
                "TAA_REPROJECTION_TRACE immediate debug=%d loc=%d program=%u actual=%d fbo=%u depth=%d stencil=%d scissor=%d center=(%.9g,%.9g,%.9g,%.9g)",
                debugValue,gfx->taa.hybridReprojectionDebugLoc,ts.id,actualProgram,
                gX_RHI_GetDrawFBOResolved(),(gX_I32)gX_RHI_IsEnabled(gX_RHI_DEPTH_TEST),
                (gX_I32)gX_RHI_IsEnabled(gX_RHI_STENCIL_TEST),(gX_I32)gX_RHI_IsEnabled(gX_RHI_SCISSOR_TEST),
                sample[0],sample[1],sample[2],sample[3]);
        }
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();

    // Copy TAA resolve result to composited output AND to history buffer (defensively disable blend)
    gX_RHI_Disable(gX_RHI_BLEND);
    // First copy to output
    gX_Drawing_BeginTexture(*taaSrc);
        gX_Texture_Draw(taaResolve->wrapper.rt.texture, (gX_Texture_Draw_Params){
            .dst = (gX_Rectangle){0, 0, res.x, res.y},
            .tint = gX_WHITE,
            .scale = {1, 1}
        });
    gX_Drawing_EndTexture();
    // Then copy to history for next frame
    if (taaResolve != &gfx->taa.historyMap) {
        gX_Drawing_BeginTexture(gfx->taa.historyMap.wrapper.rt);
            gX_Texture_Draw(taaResolve->wrapper.rt.texture, (gX_Texture_Draw_Params){
                .dst = (gX_Rectangle){0, 0, res.x, res.y},
                .tint = gX_WHITE,
                .scale = {1, 1}
            });
        gX_Drawing_EndTexture();
    }
    //opencode: post-present output routing - present already ran this frame,
    //   so when there is no compositeTarget (no HasEffects members: TAA-only
    //   and similar configs) the resolve must ALSO land on the canvas or it
    //   is silently dead (hdrScene gets repainted by next frame's opaque).
    //   With a compositeTarget the taaSrc copy above already hits canvas.
    if (taaSrc != &renderer.canvas) {
        gX_Drawing_BeginTexture(renderer.canvas);
            gX_Texture_Draw(taaResolve->wrapper.rt.texture, (gX_Texture_Draw_Params){
                .dst = (gX_Rectangle){0, 0, res.x, res.y},
                .tint = gX_WHITE,
                .scale = {1, 1}
            });
        gX_Drawing_EndTexture();
    }
    if (prevBlendTAA) gX_RHI_Enable(gX_RHI_BLEND);
    if (prevDepthTAA) gX_RHI_Enable(gX_RHI_DEPTH_TEST);
    if (prevCullTAA) gX_RHI_Enable(gX_RHI_CULL_FACE);
    gX_RHI_Depth_Write(prevDepthWriteTAA);

    gX_RendererGFX_TemporalHistory_CommitPass(&gfx->taa.temporal);
    gfx->taa.lastResolveFrame = maindata.frameCount;

    //opencode: oracle spike (Paper 1) - per-10th-frame resolveMap float
    //   readback: mean abs update magnitude vs the previous snapshot.
    //   Gated on GX_T_ORACLE or GX_T_TAASTATS (stats without the weight
    //   override, for EMA-vs-oracle series contrast). Default runs pay
    //   nothing; the pipeline stall is accepted (correctness instrument).
    {
        const gX_I8 *oracleStats = getenv("GX_T_ORACLE");
        const gX_I8 *taaStats = getenv("GX_T_TAASTATS");
        if (((oracleStats && oracleStats[0] != '0') || (taaStats && taaStats[0] != '0'))
            && (maindata.frameCount % 10) == 0 && maindata.frameCount > 0) {
            static gX_F32 *prevSnap = NULL;
            static gX_U64 prevCount = 0;
             gX_U64 pxCount = (gX_U64)taaResolve->wrapper.rt.texture.width
                 * (gX_U64)taaResolve->wrapper.rt.texture.height;
            if (prevCount != pxCount) {
                free(prevSnap);
                prevSnap = (gX_F32 *)calloc((size_t)(pxCount * 4), sizeof(gX_F32));
                prevCount = prevSnap ? pxCount : 0;
            }
            if (prevSnap) {
                gX_F32 *pixels = (gX_F32 *)malloc((size_t)(pxCount * 4 * sizeof(gX_F32)));
                if (pixels) {
                     gX_RHI_BindReadFBO(taaResolve->wrapper.rt.id);
                    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
                     glReadPixels(0, 0, taaResolve->wrapper.rt.texture.width,
                         taaResolve->wrapper.rt.texture.height,
                        GL_RGBA, GL_FLOAT, pixels);
                    gX_RHI_UnbindReadFBO();
                    gX_F64 acc = 0.0;
                    gX_F64 meanAcc = 0.0;
                    for (gX_U64 i = 0; i < pxCount * 4; i++)
                        acc += fabs((gX_F64)pixels[i] - (gX_F64)prevSnap[i]);
                    //opencode: oracle (Paper 1) - resolveMap RGB mean per
                    //   snapshot: drift-down = per-frame leak, rise-to-low-
                    //   limit = biased mixing. RGB only (alpha = roughness).
                    for (gX_U64 i = 0; i < pxCount; i++)
                        meanAcc += (gX_F64)pixels[i * 4]
                            + (gX_F64)pixels[i * 4 + 1]
                            + (gX_F64)pixels[i * 4 + 2];
                    meanAcc /= (gX_F64)(pxCount * 3);
                    //opencode: oracle (Paper 1) - historyMap RGB mean, read at
                    //   ENTRY (as the shader samples it). NOTE: an end-of-pass
                    //   read aliases the just-written copy (tautological);
                    //   entry-vs-prior-resolve is the genuine roundtrip check.
                    gX_F64 histMean = -1.0;
                    if (s_snapN == pxCount && s_snapHist) {
                        gX_F64 hAcc = 0.0;
                        for (gX_U64 i = 0; i < pxCount; i++)
                            hAcc += (gX_F64)s_snapHist[i * 4]
                                + (gX_F64)s_snapHist[i * 4 + 1]
                                + (gX_F64)s_snapHist[i * 4 + 2];
                        histMean = hAcc / (gX_F64)(pxCount * 3);
                    }
                    //opencode: oracle (Paper 1) - convex-hull membership per
                    //   pixel: output must lie in [min(c,h), max(c,h)] + F16
                    //   slack, for ANY weight in [0,1], ANY path (rejects
                    //   output current = inside hull). Violations = the mix
                    //   did not execute as written. Weight-free, robust.
                    gX_U64 hullViol = 0;
                    gX_F64 hullDevSum = 0.0;
                    if (s_snapN == pxCount && s_snapIn && s_snapHist) {
                        for (gX_U64 i = 0; i < pxCount; i++) {
                            for (gX_U64 ch = 0; ch < 3; ch++) {
                                gX_F64 c = (gX_F64)s_snapIn[i * 4 + ch];
                                gX_F64 h = (gX_F64)s_snapHist[i * 4 + ch];
                                gX_F64 o = (gX_F64)pixels[i * 4 + ch];
                                gX_F64 lo = c < h ? c : h;
                                gX_F64 hi = c > h ? c : h;
                                gX_F64 slack = 0.001 * (1.0 + (hi > lo ? hi : lo));
                                if (o < lo - slack || o > hi + slack) {
                                    hullViol++;
                                    hullDevSum += (o < lo) ? (lo - o) : (o - hi);
                                }
                            }
                        }
                    }
                    gX_TraceLog(gX_LOG_NOTICE, "ORACLESTATS f=%llu updatemag=%.6f rgbmean=%.6f inputmean=%.6f histmean=%.6f hullviol=%.6f hulldev=%.6f",
                        (unsigned long long)maindata.frameCount,
                        acc / (gX_F64)(pxCount * 4), meanAcc, s_oracleInputMean, histMean,
                        (gX_F64)hullViol / (gX_F64)(pxCount * 3),
                        hullViol ? hullDevSum / (gX_F64)hullViol : 0.0);
                    memcpy(prevSnap, pixels, (size_t)(pxCount * 4 * sizeof(gX_F32)));
                    free(pixels);
                }
            }
        }
    }

    GX_PROFILE_ZONE_END("gfx:taa");
}

gX_I0 gX_RendererGFX_TAA_CacheLocs(gX_RendererGFX *gfx)
{
    if (!gfx->taa.locsCached && gfx->taa.resolveShader.loaded)
    {
        gX_Shader ts = gfx->taa.resolveShader.shader;
        gfx->taa.colorMapLoc    = gX_Shader_GetLocation(ts, "colorMap");
        gfx->taa.depthMapLoc    = gX_Shader_GetLocation(ts, "depthMap");
        gfx->taa.historyMapLoc  = gX_Shader_GetLocation(ts, "historyMap");
        gfx->taa.prevVPLoc      = gX_Shader_GetLocation(ts, "prevVP");
        gfx->taa.invVPLoc       = gX_Shader_GetLocation(ts, "invVP");
        gfx->taa.reprojectionValidLoc = gX_Shader_GetLocation(ts, "reprojectionValid");
        gfx->taa.resolutionLoc  = gX_Shader_GetLocation(ts, "resolution");
        gfx->taa.blendFactorLoc = gX_Shader_GetLocation(ts, "blendFactor");
        gfx->taa.jitterLoc      = gX_Shader_GetLocation(ts, "jitter");
        gfx->taa.prevJitterLoc  = gX_Shader_GetLocation(ts, "prevJitter");
        gfx->taa.velocityMapLoc = gX_Shader_GetLocation(ts, "velocityMap");
        gfx->taa.hasVelocityLoc = gX_Shader_GetLocation(ts, "hasVelocityBuffer");
        gfx->taa.oracleAccumLoc = gX_Shader_GetLocation(ts, "oracleAccum");
        gfx->taa.locsCached = true;
    }
}
