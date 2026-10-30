/**********************************************************************************************
*
*   [gX: src/renderer/gfx/bloom.c]
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


//opencode: bloom post-process pipeline — extract bright, blur H+V, composite with HDR scene
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "../../rhi/rhi_fbo.h"
#include "../../rhi/rhi_gl.h"
//opencode: hunt - implicit-decl sweep (raw glReadPixels needs GL decls)

#include "../gfx.h"
#include "bloom.h"
#include "fullscreen_pass.h"
#include "../renderer.h"

#include "../../wrappers/wrappers.h"

#include "../../main.h"
#include "../../camera.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/profiler.h"


#include "../../gxmath/matrix.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_matrix.h"
#include "../../rhi/rhi_state.h"
#include "../../rhi/batch.h"

//opencode: v0.3-M1 — auto exposure. Renders a 64x64 luminance downsample of
//   the HDR scene, readbacks every 10th frame (~16KB), log-averages,
//   Krawczyk key estimation and asymmetric temporal adaptation.
static gX_I0 Bloom_AutoExposure_Update(gX_RendererGFX *gfx)
{
    const char* dbgEnv = getenv("GX_NO_AE");
    if (dbgEnv && dbgEnv[0]=='1') return;
    if (!gfx->bloom.autoExposure) return;
    //opencode: Phase 1b — exposure feeds only the bloom composite shader,
    //   so skip the downsample + CPU readback stall when BLOOM is off
    //   (exposure stays at its init 1.0, consumed nowhere).
    if (!gfx->state.ENABLE_BLOOM) return;
    if (!gfx->bloom.luminanceRT.wrapper.loadingState.loaded) return;
    if (!gfx->bloom.luminanceShader.loaded) return;

    gX_U32 lumW = gfx->bloom.luminanceRT.wrapper.width;
    gX_U32 lumH = gfx->bloom.luminanceRT.wrapper.height;

    // 1. downsample pass — HDR scene -> luminance RT
    if (gfx->bloom.luminanceInputLoc < 0)
        gfx->bloom.luminanceInputLoc = gX_RHI_GetUniformLocation(
            gfx->bloom.luminanceShader.shader.id, "sceneTex");
    gX_Drawing_BeginTexture(gfx->bloom.luminanceRT.wrapper.rt);
        gX_Drawing_BeginShader(gfx->bloom.luminanceShader.shader);
            gX_RHI_Tex_BindOnUnit(gfx->bloom.hdrScene.wrapper.rt.texture.id, gX_GFX_TEXUNIT_SCENE);
            if (gfx->bloom.luminanceInputLoc >= 0)
                gX_RHI_UniformInt(gfx->bloom.luminanceInputLoc, gX_GFX_TEXUNIT_SCENE);
            gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)lumW, (gX_F32)lumH);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();

    // 2. cadenced readback + adaptation
    gfx->bloom.luminanceFrame++;
    //opencode: v0.3.5-polish - readback every 4 frames (was 10): the stale
    //   window made exposure visibly lag scene changes
    if ((gfx->bloom.luminanceFrame % 4) != 0) return;

    static float pixels[64 * 64 * 4];
    gX_RHI_BindReadFBO(gfx->bloom.luminanceRT.wrapper.rt.id);
    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, lumW, lumH, 0x1908 /*GL_RGBA*/, 0x1406 /*GL_FLOAT*/, pixels);
    gX_RHI_UnbindReadFBO();

    //opencode: v0.3.6 - specular-trimmed log-average: isolated bright
    //   highlights dominated the 64x64 average and pulsed exposure; samples
    //   above 6x the linear mean are clamped before the log domain
    double linSum = 0.0;
    gX_U32 nPix = lumW * lumH;
    for (gX_U32 i = 0; i < nPix; i++)
        linSum += pixels[i * 4 + 0];
    double linMean = linSum / (double)nPix;
    double specCap = fmax(linMean * 6.0, 0.02);
    double logSum = 0.0;
    for (gX_U32 i = 0; i < nPix; i++) {
        float l = pixels[i * 4 + 0];
        if ((double)l > specCap) l = (float)specCap;
        logSum += log2((double)l + 1e-4);
    }
    gfx->bloom.luminanceAvg = (gX_F32)exp2(logSum / (double)nPix);

    // Krawczyk key from measured average
    float key = 1.03f - 2.0f / (2.0f + log2f(gfx->bloom.luminanceAvg + 1.0f));
    float target = key / fmaxf(gfx->bloom.luminanceAvg, 1e-4f);
    target *= gfx->bloom.exposureCompensation;
    target = fminf(fmaxf(target, gfx->bloom.exposureMin), gfx->bloom.exposureMax);

    // asymmetric temporal adaptation: adapt fast toward bright, slow toward dark
    float rate = (target < gfx->bloom.adaptedExposure) ? 1.5f : 0.6f;
    gfx->bloom.adaptedExposure += (target - gfx->bloom.adaptedExposure)
                                  * fminf(fmaxf(rate * (float)gX_FrameTime_Get(), 0.0f), 1.0f);
    gfx->bloom.exposure = gfx->bloom.adaptedExposure;
}

//opencode: one-shot float readback distinguishes non-finite composite inputs
//   from black pixels introduced by the composite shader itself.
static gX_I0 Bloom_DumpCompositeRTStats(const gX_I8 *name, gX_RenderTexture *rt)
{
    gX_U64 pixelCount = (gX_U64)rt->texture.width * (gX_U64)rt->texture.height;
    gX_F32 *pixels = (gX_F32 *)malloc((size_t)(pixelCount * 4 * sizeof(gX_F32)));
    if (!pixels) {
        gX_TraceLog_GFX(gX_LOG_ERROR, "COMPOSITE_TEX_STATS %s allocation failed", name);
        return;
    }
    gX_RHI_BindReadFBO(rt->id);
    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, rt->texture.width, rt->texture.height, GL_RGBA, GL_FLOAT, pixels);
    gX_RHI_UnbindReadFBO();
    gX_U64 nonFinite = 0;
    gX_F32 minValue = 1e30f, maxValue = -1e30f;
    gX_F64 sumValue = 0.0;
    gX_U64 finiteCount = 0;
    for (gX_U64 i = 0; i < pixelCount * 4; i++) {
        if (!isfinite(pixels[i])) {
            nonFinite++;
            continue;
        }
        //opencode: oracle (Paper 1) - HDR mean: unbiasedness must be judged
        //   in linear space (LDR captures clip estimator spikes).
        sumValue += (gX_F64)pixels[i];
        finiteCount++;
        minValue = fminf(minValue, pixels[i]);
        maxValue = fmaxf(maxValue, pixels[i]);
    }
    gX_TraceLog_GFX(gX_LOG_NOTICE,
        "COMPOSITE_TEX_STATS %s pixels=%llu finite=[%.6f,%.6f] mean=%.6f nonfinite=%llu",
        name, (unsigned long long)pixelCount, minValue, maxValue,
        finiteCount ? sumValue / (gX_F64)finiteCount : 0.0,
        (unsigned long long)nonFinite);
    //opencode: NaN hunt - spatial locator: 16x10 grid histogram so a few
    //   hundred non-finites can be attributed to scene regions (silhouette
    //   edges? sky? specific models?) instead of just counted.
    if (nonFinite > 0) {
        gX_U64 grid[10][16] = {{0}};
        gX_I32 w = rt->texture.width, h = rt->texture.height;
        for (gX_I32 y = 0; y < h; y++) {
            for (gX_I32 x = 0; x < w; x++) {
                gX_U64 i = ((gX_U64)y * (gX_U64)w + (gX_U64)x) * 4;
                if (!isfinite(pixels[i]) || !isfinite(pixels[i+1])
                    || !isfinite(pixels[i+2]) || !isfinite(pixels[i+3]))
                    grid[(y * 10) / h][(x * 16) / w]++;
            }
        }
        for (gX_I32 gy = 0; gy < 10; gy++)
            for (gX_I32 gx = 0; gx < 16; gx++)
                if (grid[gy][gx] > 0)
                    gX_TraceLog_GFX(gX_LOG_NOTICE,
                        "COMPOSITE_TEX_STATS %s nan-cell x[%d-%d] y[%d-%d] n=%llu",
                        name, gx, gx, gy, gy,
                        (unsigned long long)grid[gy][gx]);
        //opencode: NaN hunt - first bad pixel forensics: channel pattern
        //   (rgb-only = shading add like specular/rim; alpha too = vertex
        //   stage) + depth at the same location (NaN depth = geometry
        //   stage, finite depth = shading stage). Depth read from the RT's
        //   own depth attachment when present.
        for (gX_U64 i = 0; i < pixelCount * 4; i += 4) {
            if (!isfinite(pixels[i]) || !isfinite(pixels[i+1])
                || !isfinite(pixels[i+2]) || !isfinite(pixels[i+3])) {
                gX_U64 px = (i / 4);
                gX_I32 px_x = (gX_I32)(px % (gX_U64)w);
                gX_I32 px_y = (gX_I32)(px / (gX_U64)w);
                gX_TraceLog_GFX(gX_LOG_NOTICE,
                    "COMPOSITE_TEX_STATS %s first-bad xy=(%d,%d) bits=%08x%08x%08x%08x",
                    name, px_x, h - 1 - px_y,
                    (unsigned)((gX_U32 *)&pixels[i])[0],
                    (unsigned)((gX_U32 *)&pixels[i])[1],
                    (unsigned)((gX_U32 *)&pixels[i])[2],
                    (unsigned)((gX_U32 *)&pixels[i])[3]);
                break;
            }
        }
    }
    free(pixels);
}

//opencode: Step 3 — screen-shader prologue shared by all bloom passes (was
//   6 near-identical mvp-upload blocks). Upload AFTER BeginShader —
//   glUniform targets the current program.
static gX_I0 Bloom_BeginScreenShader(gX_Shader shader)
{
    gX_Drawing_BeginShader(shader);
    gX_Matrix mvp = gX_RHI_Matrix_GetProjection();
    gX_RHI_UniformMatrix(gX_RHI_GetUniformLocation(shader.id, "mvp"), 1, 1, &mvp.m0);
}
//opencode: Step 3 — one blur iteration (was 4 near-identical H/V blocks
//   differing only in dst/shader/src). Quad + res follow the DST size.
static gX_I0 Bloom_BlurPass(gX_RendererGFX *gfx, gX_RenderTexture *dstRT, gX_Shader shader, gX_Texture srcTex)
{
    gX_F32 w = (gX_F32)dstRT->texture.width;
    gX_F32 h = (gX_F32)dstRT->texture.height;
    gX_RendererGFX_Fullscreen_Begin(*dstRT, shader);
        gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT | gX_RHI_DEPTH_BUFFER_BIT | gX_RHI_STENCIL_BUFFER_BIT);
        gX_Drawing_ClearBG((gX_Color){0,0,0,255});
        gX_Shader_SetValue_TextureSlot(shader, gfx->bloom.blurInputLoc, srcTex, 0);
        gX_RHI_Uniform_Vec2(gfx->bloom.blurResLoc, w, h);
        gX_RHI_Uniform_Float(gfx->bloom.blurAmountLoc, gfx->bloom.blurAmount);
        gX_RendererGFX_Fullscreen_Draw(0, 0, w, h);
    gX_RendererGFX_Fullscreen_End();
}

//opencode: SSGI fallback composite — bloom's shader normally adds the
//   accumulated GI, but SSGI must remain visible when bloom is disabled.
static gX_I0 Bloom_AddSsgi(gX_RendererGFX *gfx)
{
    if (!gfx->state.ENABLE_SSAO || !gfx->ssao.useSSGI
        || gfx->ssao.ssgiFinalTex.id == 0) return;
    gX_RenderTexture *tgt = gfx->common.compositeTarget
        ? gfx->common.compositeTarget : &renderer.canvas;
    if (!tgt || tgt->id == 0) return;
    gX_Drawing_BeginTexture(*tgt);
        gX_I1 prevBlend = gX_RHI_IsEnabled(gX_RHI_BLEND);
        gX_I32 prevSrc, prevDst, prevEq;
        gX_RHI_GetIntegerv(gX_RHI_BLEND_SRC_RGB, &prevSrc);
        gX_RHI_GetIntegerv(gX_RHI_BLEND_DST_RGB, &prevDst);
        gX_RHI_GetIntegerv(gX_RHI_BLEND_EQUATION_RGB, &prevEq);
        gX_RHI_Enable(gX_RHI_BLEND);
        gX_RHI_Blend_Func_Separate(gX_RHI_ONE, gX_RHI_ONE, gX_RHI_ONE, gX_RHI_ONE);
        gX_Texture_Draw(gfx->ssao.ssgiFinalTex, (gX_Texture_Draw_Params){
            .dst = (gX_Rectangle){0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight},
            .tint = gX_WHITE,
            .scale = {1, 1}
        });
        gX_Batch_Flush();
        gX_RHI_Blend_Equation((gX_U32)prevEq);
        gX_RHI_Blend_Func_Separate((gX_U32)prevSrc, (gX_U32)prevDst,
            (gX_U32)prevSrc, (gX_U32)prevDst);
        if (!prevBlend) gX_RHI_Disable(gX_RHI_BLEND);
    gX_Drawing_EndTexture();
    return;
}

gX_I0 gX_RendererGFX_Bloom_Draw(gX_RendererGFX *gfx)
{
    GX_PROFILE_ZONE_BEGIN("gfx:bloom");

    Bloom_AutoExposure_Update(gfx);

    //opencode: always copy hdrScene to composite target when available.
    //   This runs regardless of bloom state so the scene is always visible.
    //   The bloom pipeline (if enabled) will overwrite this copy with its
    //   own composite; if disabled, this is the only copy to canvas.
    if (gfx->bloom.hdrScene.wrapper.loadingState.loaded) {
        gX_RenderTexture *tgt = gfx->common.compositeTarget ? gfx->common.compositeTarget : &renderer.canvas;
        gX_RHI_BindReadFBO(gfx->bloom.hdrScene.wrapper.rt.id);
        gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
        gX_RHI_BindDrawFBO(tgt->id);
        gX_RHI_DrawBuffer(gX_RHI_COLOR_ATTACHMENT0);
        gX_RHI_Blit(gfx->bloom.hdrScene.wrapper.rt.id, tgt->id,
            0, 0,
            (gX_U32)maindata.renderWidth, (gX_U32)maindata.renderHeight,
            0, 0,
            (gX_U32)tgt->texture.width, (gX_U32)tgt->texture.height,
            gX_RHI_COLOR_BUFFER_BIT, gX_RHI_NEAREST);
        gX_RHI_UnbindReadFBO();
        gX_RHI_UnbindDrawFBO();
    }

    if (!gfx->state.ENABLE_BLOOM) {
        Bloom_AddSsgi(gfx);
        GX_PROFILE_ZONE_END("gfx:bloom");
        return;
    }
    if (!gfx->bloom.extract.wrapper.loadingState.loaded ||
        !gfx->bloom.ping.wrapper.loadingState.loaded ||
        !gfx->bloom.pong.wrapper.loadingState.loaded ||
        !gfx->bloom.scene.wrapper.loadingState.loaded)
    {
        //opencode: bloom RTs not loaded yet — skip, scene copy already in bloom.scene
        Bloom_AddSsgi(gfx);
        GX_PROFILE_ZONE_END("gfx:bloom");
        return;
    }
    //opencode: shader-based bloom pipeline: extract → blurH → blurV → composite.
    //   This replaces the old blit bypass (removed 2026-07-27) which copied raw
    //   scene to compositeTarget with an unconditional return, skipping ACES
    //   tonemapping, gamma correction, vignette, and all bloom effects.

    //opencode: upload orthographic projection as mvp for all bloom shaders.
    //   BeginTexture sets an ortho projection in the RHI matrix stack, but the
    //   default vertex shader needs it as the mvp uniform. Without this, the
    //   screen quad renders with identity transform → NDC coordinates outside
    //   [-1,1] → fully clipped → black output.
    gX_Shader extractS = gfx->bloom.extractShader.shader;
    gX_Shader blurHS = gfx->bloom.blurHShader.shader;
    gX_Shader blurVS = gfx->bloom.blurVShader.shader;
    gX_Shader compositeS = gfx->bloom.compositeShader.shader;

    gX_I32 extractSceneLoc = gfx->bloom.extractInputLoc;
    gX_I32 extractThresholdLoc = gfx->bloom.extractThresholdLoc;
    gX_I32 extractKneeLoc = gfx->bloom.extractKneeLoc;

    gX_I32 compositeSceneLoc = gfx->bloom.compositeSceneLoc;
    gX_I32 compositeBloomLoc = gfx->bloom.compositeBloomLoc;
    gX_I32 compositeIntensityLoc = gfx->bloom.compositeIntensityLoc;
    gX_I32 compositeExposureLoc = gfx->bloom.compositeExposureLoc;
    gX_I32 compositeVignetteLoc = gfx->bloom.compositeVignetteLoc;
    gX_I32 compositeSaturationLoc = gfx->bloom.compositeSaturationLoc;
    gX_I32 compositeContrastLoc = gfx->bloom.compositeContrastLoc;
    gX_I32 compositeACESLoc = gfx->bloom.compositeACESLoc;
    gX_I32 compositeAgXLoc  = gfx->bloom.compositeAgXLoc;
    gX_I32 compositeGammaLoc = gfx->bloom.compositeGammaLoc;
    gX_I32 compositeDitherModeLoc = gfx->bloom.compositeDitherModeLoc;
    gX_I32 compositeDitherSeedLoc = gfx->bloom.compositeDitherSeedLoc;
    gX_I32 compositeDitherStrengthLoc = gfx->bloom.compositeDitherStrengthLoc;

    // 1. Extract bright pixels from scene into extract RT
    gX_Drawing_BeginTexture(gfx->bloom.extract.wrapper.rt);
        gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT | gX_RHI_DEPTH_BUFFER_BIT | gX_RHI_STENCIL_BUFFER_BIT);
        gX_Drawing_ClearBG((gX_Color){0,0,0,255});
        Bloom_BeginScreenShader(extractS);
        //opencode: bind uniforms AFTER BeginShader — batch flush can lose bindings
        gX_Shader_SetValue_TextureSlot(extractS, extractSceneLoc, gfx->bloom.hdrScene.wrapper.rt.texture, gX_GFX_TEXUNIT_SCENE);
        gX_RHI_Uniform_Float(extractThresholdLoc, gfx->bloom.threshold);
        gX_RHI_Uniform_Float(extractKneeLoc, gfx->bloom.knee);
            gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)gfx->bloom.extract.wrapper.width, (gX_F32)gfx->bloom.extract.wrapper.height);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();

    // 2. Horizontal blur: extract -> ping
    Bloom_BlurPass(gfx, &gfx->bloom.ping.wrapper.rt, blurHS, gfx->bloom.extract.wrapper.rt.texture);

    // 3. Vertical blur: ping -> pong
    Bloom_BlurPass(gfx, &gfx->bloom.pong.wrapper.rt, blurVS, gfx->bloom.ping.wrapper.rt.texture);

    //opencode: v0.1.18 — second blur iteration (pong -> ping -> pong). One
    //   9-tap pass at half-res spreads a specular glint over ~14 screen px:
    //   the halo peak retains too little energy to read at any sane intensity.
    //   The second iteration doubles the effective radius and smooths the
    //   falloff (classic dual-iteration bloom); composite still reads pong.
    Bloom_BlurPass(gfx, &gfx->bloom.ping.wrapper.rt, blurHS, gfx->bloom.pong.wrapper.rt.texture);
    Bloom_BlurPass(gfx, &gfx->bloom.pong.wrapper.rt, blurVS, gfx->bloom.ping.wrapper.rt.texture);

    // 4. Composite: scene + bloom -> bloom.scene
    gX_Drawing_BeginTexture(gfx->bloom.scene.wrapper.rt);
        Bloom_BeginScreenShader(compositeS);
        //opencode: bind textures on dedicated units
        gX_RHI_Tex_BindOnUnit(gfx->bloom.hdrScene.wrapper.rt.texture.id, gX_GFX_TEXUNIT_SCENE);
        gX_RHI_Tex_BindOnUnit(gfx->bloom.pong.wrapper.rt.texture.id, gX_GFX_TEXUNIT_BLOOM_BLUR);
        //opencode: v0.3-M4 Phase2 — SSGI additive reads the ACCUMULATED tex
        //opencode: v0.3.4-fix - the -1 location trick skipped rlSetUniform so
        //   the GLSL sampler never learned its unit and read whatever sat on
        //   unit 0 (black in practice). Bind slot AND assign sampler unit.
        if (gfx->ssao.useSSGI && gfx->ssao.ssgiMap.wrapper.loadingState.loaded) {
            gX_Shader_SetValue_TextureSlot(gfx->bloom.compositeShader.shader,
                -1, gfx->ssao.ssgiFinalTex, gX_GFX_TEXUNIT_SSGI_HIST);
            if (gfx->bloom.compositeSsgiTexLoc >= 0) {
                gX_RHI_UniformInt(gfx->bloom.compositeSsgiTexLoc,
                    gX_GFX_TEXUNIT_SSGI_HIST);
            }
        }
        gX_RHI_UniformInt(compositeSceneLoc, gX_GFX_TEXUNIT_SCENE);
        gX_RHI_UniformInt(compositeBloomLoc, gX_GFX_TEXUNIT_BLOOM_BLUR);
        gX_RHI_Uniform_Float(compositeIntensityLoc, gfx->bloom.intensity);
        gX_RHI_Uniform_Float(compositeExposureLoc, gfx->bloom.exposure);
        gX_RHI_Uniform_Float(compositeVignetteLoc, gfx->bloom.vignette);
        gX_RHI_Uniform_Float(compositeSaturationLoc, gfx->bloom.saturation);
        gX_RHI_Uniform_Float(compositeContrastLoc, gfx->bloom.contrast);
        //opencode: 0x502 hunt — the composite shader declares no `resolution`
        //   uniform (removed with it: compositeResLoc field/local/upload, which
        //   sat at 0 and uploaded a vec2 into location 0, the vertex mvp mat4).
        gX_RHI_UniformInt(compositeACESLoc, gfx->state.ENABLE_ACES_TONEMAP ? 1 : 0);
        //opencode: v0.3.4-fix - fresh uniform queries here: cached struct
        //   fields came back 0/0 for these two uniforms while the same live
        //   program reported real locations (recreation ordering race);
        //   per-frame query costs nothing and is order-proof.
        {
            gX_U32 cgId = gfx->bloom.compositeShader.shader.id;
            gX_I32 texLoc = gX_RHI_GetUniformLocation(cgId, "ssgiTex");
            gX_I32 gainLoc = gX_RHI_GetUniformLocation(cgId, "ssgiGain");
            if (texLoc >= 0)
                gX_RHI_UniformInt(texLoc, gX_GFX_TEXUNIT_SSGI_HIST);
            if (gainLoc >= 0)
                gX_RHI_Uniform_Float(gainLoc,
                    (gfx->ssao.useSSGI && gfx->ssao.ssgiMap.wrapper.loadingState.loaded)
                        ? 1.0f : 0.0f);
        }
        if (compositeAgXLoc >= 0) gX_RHI_UniformInt(compositeAgXLoc, gfx->state.ENABLE_AGX_TONEMAP ? 1 : 0);
        gX_RHI_UniformInt(compositeGammaLoc, gfx->state.ENABLE_GAMMA_CORRECTION ? 1 : 0);
        //opencode: bloom audit hook — one-shot tonemap state echo so scripts can
        //   pin which operator actually consumed the frame (locs confirm the
        //   uniforms exist; a missing loc silently falls back to ACES).
        if (getenv("GX_TONEMAP_TRACE")) {
            static gX_I1 tonemapTraced = 0;
            if (!tonemapTraced) {
                tonemapTraced = 1;
                gX_TraceLog(gX_LOG_NOTICE,
                    "BLOOM: tonemap aces=%d agx=%d gamma=%d locs=%d/%d/%d no_agx=%s",
                    gfx->state.ENABLE_ACES_TONEMAP, gfx->state.ENABLE_AGX_TONEMAP,
                    gfx->state.ENABLE_GAMMA_CORRECTION, compositeACESLoc,
                    compositeAgXLoc, compositeGammaLoc,
                    getenv("GX_NO_AGX") ? getenv("GX_NO_AGX") : "-");
            }
        }
        {
            gX_I32 ditherMode = 1;
            gX_F32 ditherSeed = 0.0f;
            gX_F32 ditherStrength = 1.5f;
            gX_I32 paletteMode = 0;
            const gX_I8 *modeHook = getenv("GX_T_DITHER_MODE");
            const gX_I8 *seedHook = getenv("GX_T_DITHER_SEED");
            const gX_I8 *strengthHook = getenv("GX_T_DITHER_STRENGTH");
            const gX_I8 *animateHook = getenv("GX_T_DITHER_ANIMATE");
            const gX_I8 *paletteHook = getenv("GX_T_PALETTE_MODE");
            if (modeHook && modeHook[0]) ditherMode = atoi((const char *)modeHook);
            if (seedHook && seedHook[0]) ditherSeed = (gX_F32)atof((const char *)seedHook);
            if (strengthHook && strengthHook[0]) ditherStrength = (gX_F32)atof((const char *)strengthHook);
            if (paletteHook && paletteHook[0]) paletteMode = atoi((const char *)paletteHook);
            if (animateHook && animateHook[0] && atoi((const char *)animateHook) != 0)
                ditherSeed += (gX_F32)maindata.frameCount;
            if (ditherMode < 0) ditherMode = 0;
            if (ditherMode > 3) ditherMode = 3;
            if (ditherStrength < 0.0f) ditherStrength = 0.0f;
            if (ditherStrength > 4.0f) ditherStrength = 4.0f;
            if (paletteMode < 0) paletteMode = 0;
            if (paletteMode > 2) paletteMode = 2;
            //opencode: the final palette pass owns quantization dither; avoid
            //   applying the same pattern before tonemapping.
            if (paletteMode > 0) ditherMode = 0;
            if (compositeDitherModeLoc >= 0) gX_RHI_UniformInt(compositeDitherModeLoc, ditherMode);
            if (compositeDitherSeedLoc >= 0) gX_RHI_Uniform_Float(compositeDitherSeedLoc, ditherSeed);
            if (compositeDitherStrengthLoc >= 0) gX_RHI_Uniform_Float(compositeDitherStrengthLoc, ditherStrength);
        }

            gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();

    if (getenv("GX_COMPOSITE_TEX_STATS") && maindata.frameCount >= 30) {
        static gX_I1 dumped = false;
        if (!dumped) {
            dumped = true;
            Bloom_DumpCompositeRTStats("hdrScene", &gfx->bloom.hdrScene.wrapper.rt);
            Bloom_DumpCompositeRTStats("pong", &gfx->bloom.pong.wrapper.rt);
            Bloom_DumpCompositeRTStats("bloom.scene", &gfx->bloom.scene.wrapper.rt);
        }
    }

    GX_PROFILE_ZONE_END("gfx:bloom");
}
