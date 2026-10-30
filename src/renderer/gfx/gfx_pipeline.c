/**********************************************************************************************
*
*   [gX: src/renderer/gfx/gfx_pipeline.c]
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


//opencode: DrawTo — full render pipeline orchestrator
//   Extracted from gfx.c to split the 700-line file into logical units.
//   Draws shadow maps, reflections, opaque/transparent passes, effects,
//   and the final blit to the output target.
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#include "../gfx.h"
#include "../renderer.h"
#include "gl_level.h"
#include "fullscreen_pass.h"
//ai: opt-in complete query sets retain native source frame/draw identity.
#include "profile.h"

#include "../../wrappers/wrappers.h"

#include "../../main.h"
#include "../../camera.h"

#include "../../common/log.h"
#include "../../common/utils/shader.h"
//opencode: hunt - SetValue_* prototypes (implicit decl promoted float args to double, zeroing uploads)
#include "../../common/macros.h"
#include "../../common/profiler.h"

#include "../../gxmath/matrix.h"
#include "../../gxmath/constants.h"
#include "../../rhi/rhi_matrix.h" //opencode: v0.1.18 — FXAA pass mvp upload

#include "load.h"
#include "draw.h"
#include "boilerplate.h"
#include "bloom.h"
#include "dof.h"
#include "taa.h"
#include "motionblur.h"
#include "velocity.h"
#include "viewport_grid.h"
#include "../../rhi/gl_state.h"
#include "../../rhi/rhi_state.h"
#include "../../rhi/rhi_fbo.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_tex.h"
#include "../../rhi/rhi_buffer_audit.h"
//opencode: native RT readback uses the bundled GL pixel-transfer entry points.
#include "../../rhi/rhi_gl.h"
#include "../../rhi/batch.h"
#include "../../common/shader_compiler.h"

//opencode: showcase HUD legend (drawn after the debug overlays)
extern gX_I0 gX_Showcase_DrawHUD(gX_RendererGFX *gfx, gX_RenderTexture *overlayTgt);

//opencode: Step 2 — frame-graph pass table. DrawTo was ~600 lines of inline
//   orchestration; every pass is now one table row {name, enable-predicate,
//   run-fn, input/output tags}, executed in table order. Predicates mirror
//   the old call-site conditions verbatim; run bodies are the old inline
//   blocks moved unchanged (bodies keep their own internal gates, profile
//   zones and debug groups). The five spanning GPU timers (+ the opaque
//   profile span) stay in DrawTo around index ranges — see the segments in
//   DrawTo. reads/writes tags come from docs/gfx_inventory_2026-09.md and
//   feed the load-time table validation (uniqueness/completeness; full
//   dataflow checking is future work).
typedef gX_I0 (*gX_GFX_Pass_Run)(gX_RendererGFX *gfx, gX_RenderTexture *);
typedef gX_I1 (*gX_GFX_Pass_Enabled)(gX_RendererGFX *gfx);
typedef struct gX_GFX_Pass {
    const gX_I8 *name;
    gX_GFX_Pass_Enabled enabled; // NULL = always (body self-gates)
    gX_GFX_Pass_Run run;
    const gX_I8 *reads;
    const gX_I8 *writes;
} gX_GFX_Pass;

//ai: numeric boundaries must track execution-table order, including transparency before SSR inputs.
typedef enum gX_GFX_PassId {
    PASS_SHADOW = 0,
    PASS_REFLECTIONS,
    PASS_SCENE_SETUP,
    PASS_OPAQUE,
    PASS_TERRAIN,
    PASS_VELOCITY,
    PASS_RESOLVE,
    PASS_RAYMARCH_DEPTH,
    PASS_WATER,
    PASS_LABELS,
    PASS_TRANSPARENT,
    PASS_ORACLE_GPU,
    PASS_SSR_INPUTS,
    PASS_SSR,
    PASS_SSR_VIS,
    PASS_CLUSTERS,
    PASS_SCENE_COPY,
    PASS_SSGI,
    PASS_BLOOM,
    PASS_GODRAY,
    PASS_LENSFLARE,
    PASS_SSR_COMP,
    PASS_FOG,
    PASS_POSTTARGET,
    PASS_DEPTHVIS,
    PASS_PRESENT,
    //opencode: transparent-background alpha mask — after present so the layer
    //   carries the final frame, before overlays/HUD (they draw on top with
    //   their own alpha, leaving sky pixels transparent).
    PASS_SCENE_MASK,
    PASS_PARTICLES,
    PASS_WEATHER,
    PASS_SPLAT,
    PASS_MOTIONBLUR,
    PASS_DOF,
    PASS_TAA,
    PASS_SSAO,
    PASS_CONTACT,
    //opencode: insert preview after final raster composition, before overlays.
    PASS_ORACLE_PREVIEW,
    PASS_OVERLAYS,
    PASS_HUD,
    //opencode: opt-in final quantization after all presentation overlays.
    PASS_PALETTE,
    //opencode: opt-in final scanline/dot treatment after palette output.
    PASS_INTERLACE,
    PASS_RTVIEW,
    PASS_COUNT
} gX_GFX_PassId;

//opencode: SSGI is a post-lighting producer whose result must enter the
//   composite chain even when bloom itself is disabled.
static gX_I1 Pass_HasSsgi(gX_RendererGFX *gfx)
{
    return (gfx->state.ENABLE_SSAO && gfx->ssao.useSSGI);
}

//opencode: composite-chain membership (scene copy + final-blit source).
//   NOTE: deliberately different from Pass_NeedHdr below — the chain feeds
//   any effect reading compositeTarget (FOG/PARTICLES included), while HDR
//   depth is needed by a different subset (TAA/SSAO included). Do not merge.
static gX_I1 Pass_HasEffects(gX_RendererGFX *gfx)
{
    return (gfx->state.ENABLE_BLOOM || gfx->state.ENABLE_GODRAYS ||
        gfx->state.ENABLE_FOG || gfx->state.ENABLE_LENS_FLARE ||
        gfx->state.ENABLE_SSR || gfx->state.ENABLE_VOLUME_FOG ||
        gfx->state.ENABLE_MOTION_BLUR || gfx->state.ENABLE_DOF ||
        gfx->state.ENABLE_PARTICLES || Pass_HasSsgi(gfx));
}
static gX_I1 Pass_NeedHdr(gX_RendererGFX *gfx)
{
    return (gfx->state.ENABLE_BLOOM || gfx->state.ENABLE_GODRAYS ||
        gfx->state.ENABLE_LENS_FLARE || gfx->state.ENABLE_SSR ||
        gfx->state.ENABLE_DOF || gfx->state.ENABLE_MOTION_BLUR ||
        gfx->state.ENABLE_TAA || gfx->state.ENABLE_SSAO ||
        gfx->state.ENABLE_VOLUME_FOG);
}
static gX_I1 Pass_On_SsrVis(gX_RendererGFX *gfx)
{
    return (!gfx->viewportGrid.rendering && gfx->state.SHOW_SSR_VISUALIZER
        && gfx->ssr.map.wrapper.loadingState.loaded);
}
static gX_I1 Pass_On_SceneCopy(gX_RendererGFX *gfx)
{
    return Pass_HasEffects(gfx);
}
//opencode: TAA runs after present; keep compositor-layer resolves on the
//   caller's output RT even when no earlier effect engaged the chain.
static gX_I1 Pass_On_PostTarget(gX_RendererGFX *gfx)
{
    return (Pass_HasEffects(gfx) || gfx->state.ENABLE_TAA);
}
static gX_I1 Pass_On_SsrComp(gX_RendererGFX *gfx)
{
    return (gfx->state.ENABLE_SSR && gfx->ssr.drewThisFrame && gfx->ssr.map.wrapper.loadingState.loaded);
}
static gX_I1 Pass_On_DepthVis(gX_RendererGFX *gfx)
{
    return !gfx->viewportGrid.rendering && gfx->state.SHOW_DEPTH_VISUALIZER;
}
static gX_I1 Pass_On_Particles(gX_RendererGFX *gfx)
{
    return gfx->state.ENABLE_PARTICLES;
}
static gX_I1 Pass_On_Weather(gX_RendererGFX *gfx)
{
    return gfx->state.ENABLE_SNOWFALL || gfx->state.ENABLE_RAIN || gfx->state.ENABLE_FOGSPRITE;
}
static gX_I1 Pass_On_Splat(gX_RendererGFX *gfx)
{
    return gfx->state.ENABLE_SPLAT;
}
static gX_I1 Pass_On_MotionBlur(gX_RendererGFX *gfx)
{
    return gfx->state.ENABLE_MOTION_BLUR;
}
static gX_I1 Pass_On_Dof(gX_RendererGFX *gfx)
{
    return gfx->state.ENABLE_DOF;
}
static gX_I1 Pass_On_Taa(gX_RendererGFX *gfx)
{
    return gfx->state.ENABLE_TAA;
}
static gX_I1 Pass_On_RtView(gX_RendererGFX *gfx)
{
    const gX_I8 *hook = getenv("GX_RT_VIEW");
    return (!gfx->viewportGrid.rendering && hook && hook[0]) ? 1 : 0;
}
static gX_I1 Pass_On_Ssao(gX_RendererGFX *gfx)
{
    return gfx->state.ENABLE_SSAO;
}
//opencode: contact shadows (spike 1) — hook-gated (default OFF).
static gX_I1 Pass_On_Contact(gX_RendererGFX *gfx)
{
    const gX_I8 *hook = getenv("GX_T_CONTACT");
    return (!gfx->viewportGrid.rendering && hook && hook[0] != '\0' && hook[0] != '0'
        && gfx->contact.map.wrapper.loadingState.loaded) ? 1 : 0;
}
static gX_I1 Pass_On_Ssgi(gX_RendererGFX *gfx)
{
    return Pass_HasSsgi(gfx);
}

//opencode: Step 2 — pass run bodies, moved verbatim from the DrawTo inline
//   sequence (order preserved). Second parameter unnamed unless used.
static gX_I0 Pass_Run_Shadow(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Debug_PushGroup("GFX_Shadow");
    GX_PROFILE_ZONE_BEGIN("gfx:shadow");
    gX_RendererGFX_Draw_Shadow(gfx);
    gX_RendererGFX_Draw_PointShadows(gfx);
    GX_PROFILE_ZONE_END("gfx:shadow");
    gX_RHI_Debug_PopGroup();
    return;
}
static gX_I0 Pass_Run_Reflections(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Debug_PushGroup("GFX_Reflections");
    GX_PROFILE_ZONE_BEGIN("gfx:reflections");
    gX_RendererGFX_Draw_Reflections(gfx);
    GX_PROFILE_ZONE_END("gfx:reflections");
    gX_RHI_Debug_PopGroup();
    return;
}

//opencode: paired source capture for the opt-in procedural-sky precision A/B.
//   The alternate HDR draw is taken in this frame, then normal scene setup
//   continues with the selected LDR-seeded or direct-HDR route.
static gX_I1 Pass_WriteSkyPrecisionTarget(const gX_I8 *directory,
    const gX_I8 *name, gX_U64 frame, gX_RenderTexture *target)
{
    gX_U32 width;
    gX_U32 height;
    gX_U32 channels = 4;
    gX_U64 valueCount;
    gX_F32 *pixels;
    gX_I8 path[512];
    FILE *file;
    const gX_I8 magic[8] = {'G', 'X', 'R', 'T', 'F', '3', '2', 0};
    gX_I1 writeOk;
    if (!directory || !directory[0] || !target || target->id == 0) return 0;
    width = (gX_U32)target->texture.width;
    height = (gX_U32)target->texture.height;
    if (width == 0 || height == 0) return 0;
    valueCount = (gX_U64)width * (gX_U64)height * channels;
    pixels = (gX_F32 *)malloc((size_t)(valueCount * sizeof(gX_F32)));
    if (!pixels) return 0;
    snprintf((char *)path, sizeof(path), "%s/%s_%06llu.gxrt",
        (const char *)directory, (const char *)name,
        (unsigned long long)frame);
    file = fopen((const char *)path, "wb");
    if (!file) {
        free(pixels);
        gX_TraceLog_GFX(gX_LOG_WARNING, "SKY_PAIR: cannot open %s", path);
        return 0;
    }
    gX_RHI_BindReadFBO(target->id);
    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
    gX_RHI_ReadPixels(0, 0, width, height, gX_RHI_RGBA, gX_RHI_FLOAT, pixels);
    gX_RHI_UnbindReadFBO();
    writeOk = fwrite(magic, sizeof(magic), 1, file) == 1
        && fwrite(&width, sizeof(width), 1, file) == 1
        && fwrite(&height, sizeof(height), 1, file) == 1
        && fwrite(&channels, sizeof(channels), 1, file) == 1
        && fwrite(pixels, sizeof(gX_F32), (size_t)valueCount, file) == valueCount;
    fclose(file);
    free(pixels);
    if (!writeOk) {
        remove((const char *)path);
        gX_TraceLog_GFX(gX_LOG_WARNING, "SKY_PAIR: incomplete write %s", path);
        return 0;
    }
    return 1;
}

static gX_I1 Pass_DumpSkyPrecisionPair(gX_RendererGFX *gfx)
{
    static gX_U64 lastPairFrame = (gX_U64)-1;
    const gX_I8 *directory = getenv("GX_SKY_PRECISION_PAIR_DIR");
    const gX_I8 *frameHook = getenv("GX_SKY_PRECISION_PAIR_FRAME");
    gX_U64 frame = frameHook && frameHook[0]
        ? (gX_U64)strtoull((const char *)frameHook, NULL, 10)
        : maindata.frameCount;
    if (!directory || !directory[0] || maindata.frameCount != frame
        || frame == lastPairFrame || !gfx->state.ENABLE_PROCEDURAL_SKY
        || gfx->proceduralSky.shader.id == 0
        || !gfx->sceneColorLDR.wrapper.loadingState.loaded
        || !gfx->bloom.hdrScene.wrapper.loadingState.loaded) return 0;
    gX_RendererGFX_Sky_DrawTo(gfx, gfx->bloom.hdrScene.wrapper.rt, 0);
    lastPairFrame = frame;
    if (!Pass_WriteSkyPrecisionTarget(directory, "skyDirectHDR", frame,
            &gfx->bloom.hdrScene.wrapper.rt)
        || !Pass_WriteSkyPrecisionTarget(directory, "skyLdrRGBA8", frame,
            &gfx->sceneColorLDR.wrapper.rt)) {
        gX_TraceLog_GFX(gX_LOG_WARNING, "SKY_PAIR: incomplete source pair at frame %llu",
            (unsigned long long)frame);
    } else {
        gX_TraceLog_GFX(gX_LOG_NOTICE, "SKY_PAIR: captured direct HDR and LDR sources at frame %llu",
            (unsigned long long)frame);
    }
    return 1;
}

static gX_I0 Pass_Run_SceneSetup(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    if (gfx->sceneColorLDR.wrapper.loadingState.loaded) {
        gX_Drawing_BeginTexture(gfx->sceneColorLDR.wrapper.rt);
            gX_Drawing_ClearBG(gfx->sky.color);
        gX_Drawing_EndTexture();
    }
    gX_RendererGFX_Draw_Sky(gfx);
    gX_I1 pairedSkyDraw = Pass_DumpSkyPrecisionPair(gfx);

    gX_I1 needHdrSceneDepth = Pass_NeedHdr(gfx);
    if (needHdrSceneDepth && !gfx->bloom.hdrScene.wrapper.loadingState.loaded) {
        gX_TraceLog_GFX(gX_LOG_WARNING, "DrawTo: needHdrSceneDepth but hdrScene not loaded — falling back to sceneColorLDR");
        needHdrSceneDepth = false;
    }
    //opencode: Phase 1 — single scene-target selection (was 4 second.rt
    //   writes plus the MSAA handle swap). The seed fills the target with
    //   sky content so water/transparent/labels/decals accumulate there even
    //   with effects off — the final blit reads hdrScene, so this keeps
    //   those passes visible. MSAA renders into the named hdrMSAA target;
    //   resolve copies it to hdrScene (no handle mutation, so every
    //   hdrScene.texture reader stays valid all frame).
    gX_RenderTexture *sceneTarget = &gfx->sceneColorLDR.wrapper.rt;
    if (gfx->bloom.hdrScene.wrapper.loadingState.loaded) {
        sceneTarget = &gfx->bloom.hdrScene.wrapper.rt;
        //opencode: MSAA override needs a loaded hdrScene — resolve has no
        //   destination otherwise, so fall back to direct render instead.
        if (gfx->bloom.hdrMSAA.wrapper.loadingState.loaded)
            sceneTarget = &gfx->bloom.hdrMSAA.wrapper.rt;
    }
    const gX_I8 *directSkyHook = getenv("GX_T_SKY_DIRECT_HDR");
    gX_I1 directHdrSky = directSkyHook && directSkyHook[0] != '0'
        && gfx->state.ENABLE_PROCEDURAL_SKY
        && gfx->proceduralSky.shader.id > 0
        && gfx->bloom.hdrScene.wrapper.loadingState.loaded;
    if (directHdrSky) {
        if (sceneTarget != &gfx->bloom.hdrScene.wrapper.rt || !pairedSkyDraw)
            gX_RendererGFX_Sky_DrawTo(gfx, *sceneTarget, 0);
        gfx->second.skipColorClear = true;
    } else if (gfx->bloom.hdrScene.wrapper.loadingState.loaded) {
        gX_Drawing_BeginTexture(*sceneTarget);
            gX_Drawing_ClearBG(gfx->sky.color);
            gX_Texture_Draw(gfx->sceneColorLDR.wrapper.rt.texture, (gX_Texture_Draw_Params){
                .dst = (gX_Rectangle){0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight},
                .tint = gX_WHITE,
                .scale = {1, 1}
            });
        gX_Drawing_EndTexture();
    }
    gfx->second.rt = sceneTarget;
    return;
}
static gX_I0 Pass_Run_Opaque(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    //opencode: no MRT drawbuffers — per-pixel roughness rides the color alpha
    //   (the MRT location-1 output is dropped by the NVIDIA driver with the
    //   full VS_OUT interface; see update.c note)
    gX_RendererGFX_Draw_Opaque(gfx);
    return;
}
static gX_I0 Pass_Run_Velocity(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    //opencode: v0.1.38 — motion vectors right after opaque (models' params +
    //   camera are current; consumers TAA/SSGI read it later)
    gX_RendererGFX_Velocity_Draw(gfx);
    //opencode: Paper 2 spike 3 — validate the baked terrain velocity
    //   against the CPU reproject (fresh buffer, hook-gated inside).
    gX_RendererGFX_Terrain_VelocityStats(gfx);
    return;
}
static gX_I0 Pass_Run_Terrain(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    //opencode: Paper 2 spike — marched terrain depth into the scene target
    //   (self-gates on GX_T_TERRAINDEPTH inside; absent by default).
    gX_RendererGFX_Terrain_Draw(gfx);
    return;
}
static gX_I0 Pass_Run_Resolve(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    //opencode: resolve MSAA BEFORE water — water samples scene color as a
    //   sampler2D, which is invalid on the multisample texture (Phase 1:
    //   the named hdrMSAA target is never sampled, only resolved here).
    gX_RendererGFX_MSAA_Resolve(gfx);
    return;
}
//ai: reuse the native RT float-readback format for the focused shared-depth oracle.
static gX_I0 Pass_Dump_RtViewTarget(const gX_I8 *name,
    gX_RenderTexture *target, gX_I1 depthTarget);

//ai: shared SDF depth enters after MSAA resolve and before water/screen-space consumers.
static gX_I1 Pass_On_RaymarchDepth(gX_RendererGFX *gfx)
{
    return renderer.state.raymarchDepth == 2 && renderer.raymarch.data
        && renderer.raymarch.amount > 0 && gfx->bloom.hdrScene.wrapper.loadingState.loaded;
}

//ai: depth-tested writes avoid reading any attachment of the active HDR framebuffer.
static gX_I0 Pass_Run_RaymarchDepth(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    //ai: shared depth does not need a full-res shaded march. The hybrid fill
    //   owns SDF color at the module render scale; the depth-only pass keeps
    //   the full-res attachment exact for water/SSR/SSAO/TAA consumers.
    gX_RenderTexture *sceneRT = &gfx->bloom.hdrScene.wrapper.rt;
    gX_RendererRaymarch_DrawHybrid(renderer.raymarch.data,sceneRT,sceneRT->depth);
    gX_RendererRaymarch_DrawSharedDepthOnly(renderer.raymarch.data,sceneRT);
    //ai: ownership follows the resolved shared depth, not raster geometry hidden behind an SDF.
    const gX_I8 *temporalHook = getenv("GX_RAYMARCH_TEMPORAL");
    if (temporalHook && temporalHook[0] != '0' && gfx->state.ENABLE_TAA
        && gfx->velocity.hasPrevVP && gfx->velocity.map.wrapper.loadingState.loaded)
        gX_RendererRaymarch_DrawSharedTemporal(renderer.raymarch.data,
            &gfx->velocity.map.wrapper.rt,gfx->bloom.hdrScene.wrapper.rt.depth);
    if (!getenv("GX_RT_VIEW"))
    {
        gX_I1 surface = getenv("GX_RAYMARCH_SURFACE_DUMP") != NULL;
        Pass_Dump_RtViewTarget(surface ? "hybrid_surface" : "hybrid_depth",
            &gfx->bloom.hdrScene.wrapper.rt,!surface);
    }
}

static gX_I0 Pass_Run_Water(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Enable(gX_RHI_DEPTH_TEST);
    gX_RendererGFX_Draw_Water(gfx);
    return;
}
static gX_I0 Pass_Run_Labels(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Debug_PushGroup("GFX_TextLabels");
    {
        gX_Drawing_BeginTexture(gfx->bloom.hdrScene.wrapper.rt);
            gX_RendererGFX_Draw_TextLabels(gfx);
        gX_Drawing_EndTexture();
    }
    gX_RHI_Debug_PopGroup();
    return;
}
static gX_I0 Pass_Run_SsrInputs(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    //opencode: SSR inputs — color mips (rough-surface blur), sky LUT, Hi-Z chain.
    //   The hdrScene depth itself is NOT mipmapped anymore: the SSR march uses
    //   the dedicated min-reduced Hi-Z chain (R32F) for correct conservative
    //   ray marching (averaged depth mips let rays tunnel through thin walls).
    {
        gX_U32 depthId = gfx->bloom.hdrScene.wrapper.rt.depth.id;
        if (depthId > 0) {
            gX_RHI_Tex_Param(depthId, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_NEAREST);
            gX_RHI_Tex_Param(depthId, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_NEAREST);
            gX_RHI_Tex_Param(depthId, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_COMPARE_MODE, gX_RHI_NONE);
        }
    }
    if (gfx->state.ENABLE_SSR)
    {
        gX_RendererGFX_SSR_DrawNormals(gfx);
        gX_U32 colorId = gfx->bloom.hdrScene.wrapper.rt.texture.id;
        if (colorId > 0)
        {
            gX_RHI_Tex_Param(colorId, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_LINEAR_MIPMAP_LINEAR);
            gX_RHI_Tex_GenMipmap(colorId, gX_RHI_TEXTURE_2D);
        }
        if (gfx->ssr.skyLut.wrapper.loadingState.loaded)
            gX_RendererGFX_Sky_DrawTo(gfx, gfx->ssr.skyLut.wrapper.rt, 1);
        //opencode: v0.3.8 - fog delta LUT: sky escapes in the SSR carry the
        //   same fog as the direct view (kills the mirror-horizon seam)
        gX_RendererGFX_FogVolume_LutPass(gfx);
        gX_RendererGFX_SSR_HiZ_Chain(gfx);
    }
    gX_GL_DumpState("after_depth_mip");
    return;
}
static gX_I0 Pass_Run_Ssr(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Debug_PushGroup("GFX_SSR");
    GX_PROFILE_ZONE_BEGIN("gfx:ssr");
    gX_RendererGFX_Draw_SSR(gfx);
    GX_PROFILE_ZONE_END("gfx:ssr");
    gX_GL_DumpState("after_ssr_draw");

    //opencode: close the GFX_SSR debug group here — its pop was inside the
    //   ENABLE_BLOOM-gated effects block, leaking 1 push per frame whenever
    //   bloom was disabled. After 64 frames the debug-group stack overflowed
    //   (GL_STACK_OVERFLOW every frame thereafter).
    gX_RHI_Debug_PopGroup(); // GFX_SSR
    return;
}
static gX_I0 Pass_Run_Transparent(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Debug_PushGroup("GFX_Transparent");
    GX_PROFILE_ZONE_BEGIN("gfx:transparent");
    gX_RendererGFX_Draw_Transparent(gfx);
    //opencode: decals render into hdrScene (with its depth) — previously the
    //   default framebuffer, which never reached the presented canvas
    if (gfx->state.ENABLE_DECALS && gfx->decals.amount > 0 && gfx->bloom.hdrScene.wrapper.loadingState.loaded) {
        gX_Drawing_BeginTexture(gfx->bloom.hdrScene.wrapper.rt);
            gX_RendererGFX_Draw_Decals(gfx);
        gX_Drawing_EndTexture();
    } else {
        gX_RendererGFX_Draw_Decals(gfx);
    }
    GX_PROFILE_ZONE_END("gfx:transparent");
    gX_RHI_Debug_PopGroup();
    gX_GL_DumpState("before_effects");
    return;
}
static gX_I1 Pass_On_OracleGPU(gX_RendererGFX *gfx)
{
    return (gX_RendererGFX_OracleGPU_Enabled()
        || (gfx->oracleGpu.previewMode != GX_GFX_RAY_PREVIEW_RASTER
            && gX_RendererGFX_OracleGPU_Available()))
        && gfx->bloom.hdrScene.wrapper.loadingState.loaded;
}
static gX_I0 Pass_Run_OracleGPU(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    GX_PROFILE_ZONE_BEGIN("gfx:oracle_gpu");
    gX_RendererGFX_OracleGPU_Draw(gfx);
    GX_PROFILE_ZONE_END("gfx:oracle_gpu");
    return;
}
//opencode: preview is an editor-only override and never runs on the GL33 path.
static gX_I1 Pass_On_OraclePreview(gX_RendererGFX *gfx)
{
    return gfx->oracleGpu.previewMode != GX_GFX_RAY_PREVIEW_RASTER
        && gX_RendererGFX_OracleGPU_Available();
}
static gX_I0 Pass_Run_OraclePreview(gX_RendererGFX *gfx,
    gX_RenderTexture *outputRT)
{
    gX_RenderTexture *target = outputRT ? outputRT : &renderer.canvas;
    gX_RendererGFX_OracleGPU_Present(gfx, target);
    return;
}
static gX_I0 Pass_Run_SsrVis(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    GX_PROFILE_ZONE_BEGIN("gfx:ssr_visualizer");
    gX_Drawing_BeginTexture(gfx->bloom.hdrScene.wrapper.rt);
        gX_Rectangle ssrRect = {0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight};
        gX_Texture_Draw(gfx->ssr.map.wrapper.rt.texture, (gX_Texture_Draw_Params){
            .dst = ssrRect, .tint = gX_WHITE, .scale = {1, 1}
        });
    gX_Drawing_EndTexture();
    GX_PROFILE_ZONE_END("gfx:ssr_visualizer");
    return;
}
static gX_I0 Pass_Run_Clusters(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    //opencode: v0.5.0 — rebuild the tile light grid every frame OUTSIDE the
    //   effects gate: it feeds the LIGHTING (shader.c binds it when
    //   ENABLE_CLUSTERED), not the post chain. Was inside the gfx_hasEffects
    //   block since v0.3.2, so with the default all-effects-off state the
    //   clustered path never ran (no grid, no consumer — legacy in disguise).
    gX_RendererGFX_LightClusters_Build(gfx);
    return;
}
static gX_I0 Pass_Run_SceneCopy(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    //opencode: hunt - the composite chain (scene copy + compositeTarget)
    //   must engage for ANY effect that reads/writes it, not just the
    //   classic list: motion blur reads compositeTarget (empty canvas
    //   without the chain) and motionblur/dof/particles write back to it,
    //   so with everything else off their output was silently discarded
    //   by the final blit (which never reads canvas). TAA/FXAA are
    //   self-sufficient (write into hdrScene/LDR, the blit sources).
    if (!gfx->bloom.scene.wrapper.loadingState.loaded) {
        gX_TraceLog_GFX(gX_LOG_WARNING, "DrawTo: gfx_hasEffects but bloom.scene not loaded — effects may read stale data");
    }
    GX_PROFILE_ZONE_BEGIN("gfx:scene_copy");
    gX_Texture srcTex;
    if (Pass_NeedHdr(gfx) && gfx->bloom.hdrScene.wrapper.loadingState.loaded) {
        srcTex = gfx->bloom.hdrScene.wrapper.rt.texture;
    } else if (gfx->sceneColorLDR.wrapper.loadingState.loaded) {
        srcTex = gfx->sceneColorLDR.wrapper.rt.texture;
    } else {
        srcTex = gfx->bloom.hdrScene.wrapper.rt.texture;
    }
    //opencode: REPLACE write — the hdrScene alpha carries the per-pixel SSR
    //   roughness; a blend would darken the copy (dst*(1-a) + src*a with
    //   a=roughness<1) and the SSR already reads hdrScene directly
    gX_I32 scPB_S, scPB_D, scPB_Eq;
    gX_I1 scPB_Blend = gX_RHI_IsEnabled(gX_RHI_BLEND);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_SRC_RGB, &scPB_S);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_DST_RGB, &scPB_D);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_EQUATION_RGB, &scPB_Eq);
    gX_RHI_Disable(gX_RHI_BLEND);
    gX_Drawing_BeginTexture(gfx->bloom.scene.wrapper.rt);
        gX_Texture_Draw(srcTex, (gX_Texture_Draw_Params){
            .dst = (gX_Rectangle){0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight},
            .tint = gX_WHITE, .scale = {1, 1}
        });
    gX_Drawing_EndTexture();
    gX_RHI_Blend_Equation((gX_U32)scPB_Eq);
    gX_RHI_Blend_Func_Separate((gX_U32)scPB_S, (gX_U32)scPB_D, (gX_U32)scPB_S, (gX_U32)scPB_D);
    if (scPB_Blend) gX_RHI_Enable(gX_RHI_BLEND);
    GX_PROFILE_ZONE_END("gfx:scene_copy");

    gfx->common.compositeTarget = &gfx->bloom.scene.wrapper.rt;
    return;
}
static gX_I0 Pass_Run_Bloom(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Debug_PushGroup("GFX_Bloom");
    GX_PROFILE_ZONE_BEGIN("gfx:bloom");
    gX_RendererGFX_Bloom_Draw(gfx);
    GX_PROFILE_ZONE_END("gfx:bloom");
    gX_RHI_Debug_PopGroup();
    gX_GL_DumpState("after_bloom");
    return;
}
static gX_I0 Pass_Run_Ssgi(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Debug_PushGroup("GFX_SSGI");
    gX_RendererGFX_SSGI_Draw(gfx);
    gX_RHI_Debug_PopGroup();
    return;
}
static gX_I0 Pass_Run_Godray(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Debug_PushGroup("GFX_Godray");
    GX_PROFILE_ZONE_BEGIN("gfx:godrays");
    gX_RendererGFX_Godray_Draw(gfx);
    GX_PROFILE_ZONE_END("gfx:godrays");
    gX_RHI_Debug_PopGroup();
    return;
}
static gX_I0 Pass_Run_Lensflare(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Debug_PushGroup("GFX_LensFlare");
    GX_PROFILE_ZONE_BEGIN("gfx:lensflare");
    gX_RendererGFX_LensFlare_Draw(gfx);
    GX_PROFILE_ZONE_END("gfx:lensflare");
    gX_RHI_Debug_PopGroup();
    return;
}
static gX_I0 Pass_Run_SsrComp(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Debug_PushGroup("GFX_SSR_Composite");
    GX_PROFILE_ZONE_BEGIN("gfx:ssr_composite");
    //opencode: v0.1.24 — gate on drewThisFrame, not just ENABLE_SSR: when
    //   the march is skipped the map RT holds stale content and
    //   compositing it painted opaque black over the whole frame.
    //   (The old planar-scene skip is gone: the march runs everywhere
    //   now; the confidence-weighted replace blend lets march content
    //   win where confident and forward-planar show through elsewhere.)
    gX_I32 pbS, pbD, pbEq;
    gX_I1 pbBlend = gX_RHI_IsEnabled(gX_RHI_BLEND);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_SRC_RGB, &pbS);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_DST_RGB, &pbD);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_EQUATION_RGB, &pbEq);
    gX_RHI_Enable(gX_RHI_BLEND);
    gX_RHI_Blend_Func_Separate(gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA, gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA);
    gX_Drawing_BeginTexture(gfx->bloom.scene.wrapper.rt);
        gX_Texture_Draw(gfx->ssr.map.wrapper.rt.texture, (gX_Texture_Draw_Params){
            .dst = (gX_Rectangle){0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight},
            //opencode: NO y-flip here. The SSR map holds true-orientation
            //   content (its shader reads true rows after the depth-tap
            //   orientation fix), and it is a standard y-up RT, so the
            //   backend's RT auto-invert already displays it correctly. The
            //   old scale.y=-1 rested on "Y-down ortho renders row 0 at
            //   visual top", which confuses projection with storage: GL
            //   always stores NDC+1 in the last row, so the -1 painted the
            //   map upside-down — mirrored march garbage on the floor,
            //   floor-black on the sky, cancelling the mirrored reads.
            .tint = gX_WHITE, .scale = {1, 1}
        });
        gX_Batch_Flush();
    gX_Drawing_EndTexture();
    gX_RHI_Blend_Equation((gX_U32)pbEq);
    gX_RHI_Blend_Func_Separate((gX_U32)pbS, (gX_U32)pbD, (gX_U32)pbS, (gX_U32)pbD);
    if (!pbBlend) gX_RHI_Disable(gX_RHI_BLEND);
    GX_PROFILE_ZONE_END("gfx:ssr_composite");
    gX_RHI_Debug_PopGroup(); // GFX_SSR_Composite
    return;
}
static gX_I0 Pass_Run_Fog(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    //opencode: v0.3-M3 — fog must composite AFTER the SSR pass: SSR
    //   rewrites bloom.scene from its own map (pre-fog content), which
    //   silently buried the fog blend when both were enabled.
    gX_RHI_Debug_PushGroup("GFX_FogVolume");
    GX_PROFILE_ZONE_BEGIN("gfx:fogvolume");
    gX_RendererGFX_FogVolume_Draw(gfx);
    GX_PROFILE_ZONE_END("gfx:fogvolume");
    gX_RHI_Debug_PopGroup();
    return;
}
static gX_I0 Pass_Run_PostTarget(gX_RendererGFX *gfx, gX_RenderTexture *outputRT)
{
    gfx->common.compositeTarget = outputRT;
    return;
}
static gX_I0 Pass_Run_DepthVis(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    //opencode: depth visualizer — draw to bloom.scene if available, else hdrScene (post-effects)
    gX_Texture depthTex = {0};
    if (gfx->state.depthVisSource == 2) {
        GX_HEAP_FOREACH(gfx->lights, gX_RendererGFX_Light, dl) {
            if (dl->castShadows && dl->shadowDepth.id > 0) depthTex = dl->shadowDepth;
        }
    } else if (gfx->state.depthVisSource == 1) {
        depthTex = gfx->bloom.hdrScene.wrapper.rt.depth;
    } else if (gfx->sun.camera.map) {
        depthTex = gfx->sun.camera.map->depth;
    }
    gX_RenderTexture *vizRt = NULL;
    if (gfx->bloom.scene.wrapper.loadingState.loaded)
        vizRt = &gfx->bloom.scene.wrapper.rt;
    else if (gfx->bloom.hdrScene.wrapper.loadingState.loaded)
        vizRt = &gfx->bloom.hdrScene.wrapper.rt;
    if (depthTex.id > 0 && vizRt) {
        gX_Drawing_BeginTexture(*vizRt);
            gX_Drawing_BeginShader(gfx->depthVisualizer.shader);
                gX_Shader_SetValue_Float(gfx->depthVisualizer.shader,
                    gfx->depthVisualizer.locations.nearPlaneLoc, camera.frustum.near);
                gX_Shader_SetValue_Float(gfx->depthVisualizer.shader,
                    gfx->depthVisualizer.locations.farPlaneLoc, camera.frustum.far);
                gX_Shader_SetValue_TextureSlot(gfx->depthVisualizer.shader,
                    gfx->depthVisualizer.locations.depthMapLoc, depthTex, gX_GFX_TEXUNIT_SHADOW);
                gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
            gX_Drawing_EndShader();
        gX_Drawing_EndTexture();
    }
    return;
}
//opencode: Paper 1 M2 — opt-in linear HDR readback for raster/oracle parity.
//   The dump is taken from hdrScene before the final blit and tonemap. The
//   binary payload is GXHDR1 + little-endian width/height/channels + RGBA32F,
//   bottom-left row order as returned by the FBO. Default path is untouched.
static gX_I0 gX_RendererGFX_DumpHDR(gX_RendererGFX *gfx)
{
    const gX_I8 *hook = getenv("GX_T_HDRDUMP");
    if (!hook || hook[0] == '0') return;
    static gX_I1 dumped = false;
    if (dumped || !gfx->bloom.hdrScene.wrapper.loadingState.loaded) return;
    gX_I32 targetFrame = 1;
    const gX_I8 *frameHook = getenv("GX_HDRDUMP_FRAME");
    if (frameHook && frameHook[0]) targetFrame = atoi((const char *)frameHook);
    if ((gX_I32)maindata.frameCount < targetFrame) return;

    gX_RenderTexture *rt = &gfx->bloom.hdrScene.wrapper.rt;
    const gX_I8 *rtHook = getenv("GX_HDRDUMP_RT");
    if (rtHook && strcmp((const char *)rtHook, "ldr") == 0
        && gfx->sceneColorLDR.wrapper.loadingState.loaded)
        rt = &gfx->sceneColorLDR.wrapper.rt;
    gX_U32 width = (gX_U32)rt->texture.width;
    gX_U32 height = (gX_U32)rt->texture.height;
    gX_U64 pixelsCount = (gX_U64)width * (gX_U64)height * 4u;
    gX_F32 *pixels = (gX_F32 *)malloc((size_t)(pixelsCount * sizeof(gX_F32)));
    if (!pixels) {
        gX_TraceLog_GFX(gX_LOG_ERROR, "HDRDUMP allocation failed (%ux%u)", width, height);
        dumped = true;
        return;
    }
    gX_RHI_BindReadFBO(rt->id);
    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
    gX_RHI_ReadPixels(0, 0, width, height, gX_RHI_RGBA, gX_RHI_FLOAT, pixels);
    gX_RHI_UnbindReadFBO();

    const gX_I8 *path = getenv("GX_HDRDUMP_PATH");
    if (!path || !path[0]) path = "/tmp/opencode/gx_hdr.bin";
    FILE *file = fopen((const char *)path, "wb");
    if (!file) {
        gX_TraceLog_GFX(gX_LOG_ERROR, "HDRDUMP open failed: %s", path);
        free(pixels);
        dumped = true;
        return;
    }
    const gX_I8 magic[8] = {'G', 'X', 'H', 'D', 'R', '1', 0, 0};
    gX_U32 channels = 4;
    fwrite(magic, sizeof(magic), 1, file);
    fwrite(&width, sizeof(width), 1, file);
    fwrite(&height, sizeof(height), 1, file);
    fwrite(&channels, sizeof(channels), 1, file);
    fwrite(pixels, sizeof(gX_F32), (size_t)pixelsCount, file);
    fclose(file);
    free(pixels);
    dumped = true;
    gX_TraceLog_GFX(gX_LOG_NOTICE, "HDRDUMP path=%s size=%ux%u frame=%d",
        path, width, height, (gX_I32)maindata.frameCount);
}

//ai: transparent-background mask predicate — Battle or the opt-in GFX mode
//   use a loaded mask shader; correctness is bound to the compositor path.
static gX_I1 Pass_On_SceneMask(gX_RendererGFX *gfx)
{
    return gfx->transparentBackground && gfx->alphaMaskShader.loaded;
}

//opencode: writes the gfx layer's alpha from the scene depth (geometry 1,
//   sky/far 0). The separate blend keeps RGB (ZERO/ONE) and replaces alpha
//   (ONE/ZERO), so no scratch read-back is needed; the raymarch compositor
//   then ALPHA-blends the layer over the backdrop (true compositing).
static gX_I0 Pass_Run_SceneMask(gX_RendererGFX *gfx, gX_RenderTexture *outputRT)
{
    if (!outputRT || outputRT->id == 0) return;
    gX_Texture depthTex = gfx->bloom.hdrScene.wrapper.rt.depth;
    if (depthTex.id == 0 || !gfx->alphaMaskShader.loaded) return;
    gX_F32 width = (gX_F32)outputRT->texture.width;
    gX_F32 height = (gX_F32)outputRT->texture.height;
    if (width <= 0.0f || height <= 0.0f) return;
    gX_Shader shader = gfx->alphaMaskShader.shader;
    gX_RHI_Debug_PushGroup("GFX_SceneMask");
    GX_PROFILE_ZONE_BEGIN("gfx:scene_mask");
    gX_Batch_Flush();
    gX_RendererGFX_Fullscreen_Begin(*outputRT, shader);
        gX_Drawing_BeginBlendCustomSeparate(gX_BLEND_ZERO, gX_BLEND_ONE,
            gX_BLEND_ONE, gX_BLEND_ZERO);
        gX_Matrix mvp = gX_RHI_Matrix_GetProjection();
        gX_RHI_UniformMatrix(gX_RHI_GetUniformLocation(shader.id, "mvp"), 1, 1, &mvp.m0);
        gX_Shader_SetValue_TextureSlot(shader,
            gX_RHI_GetUniformLocation(shader.id, "maskDepth"),
            depthTex, gX_GFX_TEXUNIT_DEPTH);
        gX_RendererGFX_Fullscreen_Draw(0.0f, 0.0f, width, height);
        gX_Drawing_BeginBlend(0);
    gX_RendererGFX_Fullscreen_End();
    GX_PROFILE_ZONE_END("gfx:scene_mask");
    gX_RHI_Debug_PopGroup();
}

static gX_I0 Pass_Run_Present(gX_RendererGFX *gfx, gX_RenderTexture *outputRT)
{
    gX_RenderTexture *outTgt = outputRT ? outputRT : &renderer.canvas;
    gX_U32 srcId = 0;
    gX_U32 srcTexId = 0;
    //opencode: blit source priority — bloom.scene (final effect composite),
    //   then hdrScene (sky+opaque+water+transparent+labels+decals+particles),
    //   then sceneColorLDR fallback. hdrScene must win when effects are off
    //   or water/transparent/text content is dropped from the output.
    if (Pass_HasEffects(gfx) && gfx->bloom.scene.wrapper.loadingState.loaded) {
        srcId = gfx->bloom.scene.wrapper.rt.id;
        srcTexId = gfx->bloom.scene.wrapper.rt.texture.id;
    } else if (gfx->bloom.hdrScene.wrapper.loadingState.loaded) {
        srcId = gfx->bloom.hdrScene.wrapper.rt.id;
        srcTexId = gfx->bloom.hdrScene.wrapper.rt.texture.id;
    } else if (gfx->sceneColorLDR.wrapper.loadingState.loaded) {
        srcId = gfx->sceneColorLDR.wrapper.rt.id;
        srcTexId = gfx->sceneColorLDR.wrapper.rt.texture.id;
    }
    gX_RendererGFX_DumpHDR(gfx);
    if (srcId == 0) {
        gX_TraceLog_GFX(gX_LOG_ERROR, "DrawTo: no source RT available for final blit — canvas will be undefined");
    }
    //opencode: v0.1.18 — FXAA on the final composite, rendered into
    //   sceneColorLDR (stale since the seed copy; fully overwritten by next
    //   frame's sky draw). Skipped when the source IS sceneColorLDR (would
    //   be a same-texture read/write feedback loop).
    if (gfx->state.ENABLE_FXAA && gfx->fxaaShader.loaded &&
        srcId > 0 && srcId != gfx->sceneColorLDR.wrapper.rt.id)
    {
        gX_Drawing_BeginTexture(gfx->sceneColorLDR.wrapper.rt);
            gX_Drawing_BeginShader(gfx->fxaaShader.shader);
            {
                gX_Matrix mvp = gX_RHI_Matrix_GetProjection();
                gX_I32 mvpLoc = gX_RHI_GetUniformLocation(gfx->fxaaShader.shader.id, "mvp");
                gX_RHI_UniformMatrix(mvpLoc, 1, 1, &mvp.m0);
            }
            gX_Shader_SetValue_TextureSlot(gfx->fxaaShader.shader,
                gX_RHI_GetUniformLocation(gfx->fxaaShader.shader.id, "sceneTex"),
                (gX_Texture){ .id = srcTexId, .width = maindata.renderWidth, .height = maindata.renderHeight },
                gX_GFX_TEXUNIT_SCENE);
            gX_RHI_Uniform_Vec2(gX_RHI_GetUniformLocation(gfx->fxaaShader.shader.id, "resolution"),
                (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
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
                //opencode: the final palette pass owns quantization dither.
                if (paletteMode > 0) ditherMode = 0;
                gX_I32 modeLoc = gX_RHI_GetUniformLocation(gfx->fxaaShader.shader.id, "ditherMode");
                gX_I32 seedLoc = gX_RHI_GetUniformLocation(gfx->fxaaShader.shader.id, "ditherSeed");
                gX_I32 strengthLoc = gX_RHI_GetUniformLocation(gfx->fxaaShader.shader.id, "ditherStrength");
                if (modeLoc >= 0) gX_RHI_UniformInt(modeLoc, ditherMode);
                if (seedLoc >= 0) gX_RHI_Uniform_Float(seedLoc, ditherSeed);
                if (strengthLoc >= 0) gX_RHI_Uniform_Float(strengthLoc, ditherStrength);
            }
            gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
            gX_Drawing_EndShader();
        gX_Drawing_EndTexture();
        srcId = gfx->sceneColorLDR.wrapper.rt.id;
    }
    if (srcId > 0) {
        gX_U32 w = maindata.renderWidth;
        gX_U32 h = maindata.renderHeight;
        gX_Batch_Flush();
        gX_RHI_BindReadFBO(srcId);
        gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
        gX_RHI_BindDrawFBO(outTgt->id);
        gX_RHI_DrawBuffer(gX_RHI_COLOR_ATTACHMENT0);
        gX_U32 dstW = outputRT ? (gX_U32)outTgt->texture.width : w;
        gX_U32 dstH = outputRT ? (gX_U32)outTgt->texture.height : h;
        gX_RHI_Blit(srcId, outTgt->id, 0, 0, w, h, 0, 0, dstW, dstH,
            gX_RHI_COLOR_BUFFER_BIT, gX_RHI_NEAREST);
        gX_RHI_UnbindReadFBO();
        gX_RHI_UnbindDrawFBO();
        if (!outputRT) {
            gX_Renderer_SetCanvasDrawn(1);
            //opencode: the GFX pipeline composites its own content into the
            //   canvas (final blit + effects) — mark the compositor as drawn
            //   so gX_Renderer_Draw skips gX_Compositor_Draw, which would
            //   otherwise OVERWRITE the canvas with its empty layers/rtPool
            //   → the gfx output vanished (black canvas).
            gX_Renderer_SetCompositorDrawn(1);
        }
        //opencode: Step-0 planar diagnostic — GX_PLANAR_VIEW=1 presents
        //   the mirror RT instead of the composite (headless RT
        //   inspection; --debug-view 8 is showcase-only). Same blit
        //   idiom + destination as the composite above.
        if (!gfx->viewportGrid.rendering && getenv("GX_PLANAR_VIEW")
            && gfx->reflection.planar.wrapper.loadingState.loaded
            && gfx->reflection.planar.wrapper.rt.texture.id > 0
            && gfx->reflection.planar.wrapper.rt.id > 0) {
            gX_Batch_Flush();
            gX_RHI_BindReadFBO(gfx->reflection.planar.wrapper.rt.id);
            gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
            gX_RHI_BindDrawFBO(outTgt->id);
            gX_RHI_DrawBuffer(gX_RHI_COLOR_ATTACHMENT0);
            gX_RHI_Blit(gfx->reflection.planar.wrapper.rt.id, outTgt->id,
                0, 0, w, h, 0, 0, dstW, dstH,
                gX_RHI_COLOR_BUFFER_BIT, gX_RHI_NEAREST);
            gX_RHI_UnbindReadFBO();
            gX_RHI_UnbindDrawFBO();
        }
    }
    return;
}
static gX_I0 Pass_Run_Particles(gX_RendererGFX *gfx, gX_RenderTexture *outputRT)
{
    //opencode: set compositeTarget for post-blit effects chain — each effect reads the cumulative
    //   result of the previous effect from this target and writes its output back to it.
    //   Previously each effect fell back to hdrScene (the original scene), skipping previous
    //   effects in the chain (e.g., DOF overwrote motion blur).
    gfx->common.compositeTarget = outputRT ? outputRT : &renderer.canvas;

    gX_RHI_Debug_PushGroup("GFX_Particles");
    GX_PROFILE_ZONE_BEGIN("gfx:particles");
    //opencode: draw particles into the composite target (canvas), not the
    //   default framebuffer — otherwise they never reach the presented output
    gX_RenderTexture *particleTgt = gfx->common.compositeTarget ? gfx->common.compositeTarget : &renderer.canvas;
    gX_Drawing_BeginTexture(*particleTgt);
        gX_RendererGFX_Particle_Draw(gfx);
    gX_Drawing_EndTexture();
    GX_PROFILE_ZONE_END("gfx:particles");
    gX_RHI_Debug_PopGroup();
    return;
}
static gX_I0 Pass_Run_Weather(gX_RendererGFX *gfx, gX_RenderTexture *outputRT)
{
    //opencode: CPU snowfall composites like particles — same post-present
    //   target chaining so earlier effects are preserved underneath.
    gfx->common.compositeTarget = outputRT ? outputRT : &renderer.canvas;
    gX_RHI_Debug_PushGroup("GFX_Weather");
    GX_PROFILE_ZONE_BEGIN("gfx:weather");
    gX_RenderTexture *weatherTgt = gfx->common.compositeTarget ? gfx->common.compositeTarget : &renderer.canvas;
    gX_Drawing_BeginTexture(*weatherTgt);
        gX_RendererGFX_Weather_Draw(gfx);
    gX_Drawing_EndTexture();
    GX_PROFILE_ZONE_END("gfx:weather");
    gX_RHI_Debug_PopGroup();
    return;
}
static gX_I0 Pass_Run_Splat(gX_RendererGFX *gfx, gX_RenderTexture *outputRT)
{
    //opencode: splat fixture composites like weather — same post-present
    //   target chaining so earlier effects are preserved underneath.
    gfx->common.compositeTarget = outputRT ? outputRT : &renderer.canvas;
    gX_RHI_Debug_PushGroup("GFX_Splat");
    GX_PROFILE_ZONE_BEGIN("gfx:splat");
    gX_RenderTexture *splatTgt = gfx->common.compositeTarget ? gfx->common.compositeTarget : &renderer.canvas;
    gX_Drawing_BeginTexture(*splatTgt);
        gX_RendererGFX_Splat_Draw(gfx);
    gX_Drawing_EndTexture();
    GX_PROFILE_ZONE_END("gfx:splat");
    gX_RHI_Debug_PopGroup();
    return;
}
static gX_I0 Pass_Run_MotionBlur(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Debug_PushGroup("GFX_MotionBlur");
    GX_PROFILE_ZONE_BEGIN("gfx:motionblur");
    gX_RendererGFX_MotionBlur_Draw(gfx);
    GX_PROFILE_ZONE_END("gfx:motionblur");
    gX_RHI_Debug_PopGroup();
    return;
}
static gX_I0 Pass_Run_Dof(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Debug_PushGroup("GFX_DOF");
    GX_PROFILE_ZONE_BEGIN("gfx:dof");
    gX_RendererGFX_DOF_Draw(gfx);
    GX_PROFILE_ZONE_END("gfx:dof");
    gX_RHI_Debug_PopGroup();
    return;
}
static gX_I0 Pass_Run_Taa(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Debug_PushGroup("GFX_TAA");
    GX_PROFILE_ZONE_BEGIN("gfx:taa");
    gX_RendererGFX_TAA_Resolve(gfx);
    GX_PROFILE_ZONE_END("gfx:taa");
    gX_RHI_Debug_PopGroup();
    return;
}
static gX_I0 Pass_Run_Ssao(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Debug_PushGroup("GFX_SSAO");
    GX_PROFILE_ZONE_BEGIN("gfx:ssao");
    gX_RendererGFX_Draw_SSAO(gfx);
    GX_PROFILE_ZONE_END("gfx:ssao");
    gX_RHI_Debug_PopGroup();
    gX_GL_DumpState("after_ssao");
    return;
}
//opencode: contact shadows (spike 1) — metrology only, no forward use yet.
static gX_I0 Pass_Run_Contact(gX_RendererGFX *gfx, gX_RenderTexture *)
{
    gX_RHI_Debug_PushGroup("GFX_Contact");
    GX_PROFILE_ZONE_BEGIN("gfx:contact");
    gX_RendererGFX_Contact_Draw(gfx);
    GX_PROFILE_ZONE_END("gfx:contact");
    gX_RHI_Debug_PopGroup();
    return;
}
static gX_I0 Pass_Run_Overlays(gX_RendererGFX *gfx, gX_RenderTexture *outputRT)
{
    gX_RenderTexture *overlayTgt = outputRT ? outputRT : &renderer.canvas;
    if (!gfx->viewportGrid.rendering)
        gX_RendererGFX_Draw_DebugOverlays(gfx, overlayTgt);
    return;
}
static gX_I0 Pass_Run_Hud(gX_RendererGFX *gfx, gX_RenderTexture *outputRT)
{
    //opencode: showcase HUD legend (--showcase) — drawn after the debug
    //   overlays into the same overlay target
    gX_RenderTexture *overlayTgt = outputRT ? outputRT : &renderer.canvas;
    if (!gfx->viewportGrid.rendering)
        gX_Showcase_DrawHUD(gfx, overlayTgt);
    return;
}
static gX_I1 Pass_On_Palette(gX_RendererGFX *gfx)
{
    const gX_I8 *paletteHook = getenv("GX_T_PALETTE_MODE");
    return paletteHook && paletteHook[0] && atoi((const char *)paletteHook) > 0
        && gfx->paletteShader.loaded && gfx->sceneColorLDR.wrapper.loadingState.loaded;
}
//opencode: final opt-in palette quantizer. It runs after present, post-blit
//   effects, overlays, and HUD, using sceneColorLDR as a non-feedback scratch.
static gX_I0 Pass_Run_Palette(gX_RendererGFX *gfx, gX_RenderTexture *outputRT)
{
    gX_RenderTexture *target = outputRT ? outputRT : &renderer.canvas;
    gX_RenderTexture *scratch = &gfx->sceneColorLDR.wrapper.rt;
    const gX_I8 *paletteHook = getenv("GX_T_PALETTE_MODE");
    gX_I32 paletteMode = paletteHook ? atoi((const char *)paletteHook) : 0;
    gX_I32 ditherMode = 1;
    gX_F32 ditherSeed = 0.0f;
    gX_F32 ditherStrength = 1.5f;
    const gX_I8 *modeHook = getenv("GX_T_DITHER_MODE");
    const gX_I8 *seedHook = getenv("GX_T_DITHER_SEED");
    const gX_I8 *strengthHook = getenv("GX_T_DITHER_STRENGTH");
    const gX_I8 *animateHook = getenv("GX_T_DITHER_ANIMATE");
    if (target->id == 0 || scratch->id == 0 || target->id == scratch->id) return;
    if (paletteMode < 1) paletteMode = 1;
    if (paletteMode > 2) paletteMode = 2;
    if (modeHook && modeHook[0]) ditherMode = atoi((const char *)modeHook);
    if (seedHook && seedHook[0]) ditherSeed = (gX_F32)atof((const char *)seedHook);
    if (strengthHook && strengthHook[0]) ditherStrength = (gX_F32)atof((const char *)strengthHook);
    if (animateHook && animateHook[0] && atoi((const char *)animateHook) != 0)
        ditherSeed += (gX_F32)maindata.frameCount;
    if (ditherMode < 0) ditherMode = 0;
    if (ditherMode > 3) ditherMode = 3;
    if (ditherStrength < 0.0f) ditherStrength = 0.0f;
    if (ditherStrength > 4.0f) ditherStrength = 4.0f;
    gX_Batch_Flush();
    gX_U32 targetWidth = (gX_U32)target->texture.width;
    gX_U32 targetHeight = (gX_U32)target->texture.height;
    gX_U32 scratchWidth = (gX_U32)scratch->texture.width;
    gX_U32 scratchHeight = (gX_U32)scratch->texture.height;
    if (targetWidth == 0 || targetHeight == 0 || scratchWidth == 0 || scratchHeight == 0) return;
    gX_RHI_BindReadFBO(target->id);
    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
    gX_RHI_BindDrawFBO(scratch->id);
    gX_RHI_DrawBuffer(gX_RHI_COLOR_ATTACHMENT0);
    gX_RHI_Blit(target->id, scratch->id, 0, 0, targetWidth, targetHeight,
        0, 0, scratchWidth, scratchHeight,
        gX_RHI_COLOR_BUFFER_BIT, gX_RHI_NEAREST);
    gX_RHI_UnbindReadFBO();
    gX_RHI_UnbindDrawFBO();
    gX_Drawing_BeginTexture(*target);
        gX_Drawing_BeginShader(gfx->paletteShader.shader);
        {
            gX_Matrix mvp = gX_RHI_Matrix_GetProjection();
            gX_RHI_UniformMatrix(gX_RHI_GetUniformLocation(gfx->paletteShader.shader.id, "mvp"),
                1, 1, &mvp.m0);
        }
        gX_Shader_SetValue_TextureSlot(gfx->paletteShader.shader,
            gX_RHI_GetUniformLocation(gfx->paletteShader.shader.id, "sceneTex"),
            scratch->texture, gX_GFX_TEXUNIT_SCENE);
        gX_RHI_UniformInt(gX_RHI_GetUniformLocation(gfx->paletteShader.shader.id, "paletteMode"), paletteMode);
        gX_RHI_UniformInt(gX_RHI_GetUniformLocation(gfx->paletteShader.shader.id, "ditherMode"), ditherMode);
        gX_RHI_Uniform_Float(gX_RHI_GetUniformLocation(gfx->paletteShader.shader.id, "ditherSeed"), ditherSeed);
        gX_RHI_Uniform_Float(gX_RHI_GetUniformLocation(gfx->paletteShader.shader.id, "ditherStrength"), ditherStrength);
        gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)targetWidth, (gX_F32)targetHeight);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();
    return;
}
static gX_I1 Pass_On_Interlace(gX_RendererGFX *gfx)
{
    const gX_I8 *modeHook = getenv("GX_T_INTERLACE_MODE");
    return modeHook && modeHook[0] && atoi((const char *)modeHook) > 0
        && gfx->interlaceShader.loaded && gfx->sceneColorLDR.wrapper.loadingState.loaded;
}
//opencode: final opt-in interlacing. It consumes the presented LDR target and
//   uses sceneColorLDR as a non-feedback scratch, like the palette pass.
static gX_I0 Pass_Run_Interlace(gX_RendererGFX *gfx, gX_RenderTexture *outputRT)
{
    gX_RenderTexture *target = outputRT ? outputRT : &renderer.canvas;
    gX_RenderTexture *scratch = &gfx->sceneColorLDR.wrapper.rt;
    const gX_I8 *modeHook = getenv("GX_T_INTERLACE_MODE");
    const gX_I8 *strengthHook = getenv("GX_T_INTERLACE_STRENGTH");
    const gX_I8 *phaseHook = getenv("GX_T_INTERLACE_PHASE");
    const gX_I8 *animateHook = getenv("GX_T_INTERLACE_ANIMATE");
    gX_I32 interlaceMode = modeHook ? atoi((const char *)modeHook) : 1;
    gX_F32 interlaceStrength = 0.35f;
    gX_F32 interlacePhase = 0.0f;
    if (target->id == 0 || scratch->id == 0 || target->id == scratch->id) return;
    if (strengthHook && strengthHook[0])
        interlaceStrength = (gX_F32)atof((const char *)strengthHook);
    if (phaseHook && phaseHook[0])
        interlacePhase = (gX_F32)atof((const char *)phaseHook);
    if (animateHook && animateHook[0] && atoi((const char *)animateHook) != 0)
        interlacePhase += (gX_F32)maindata.frameCount;
    if (interlaceMode < 1) interlaceMode = 1;
    if (interlaceMode > 3) interlaceMode = 3;
    if (interlaceStrength < 0.0f) interlaceStrength = 0.0f;
    if (interlaceStrength > 1.0f) interlaceStrength = 1.0f;
    gX_U32 targetWidth = (gX_U32)target->texture.width;
    gX_U32 targetHeight = (gX_U32)target->texture.height;
    gX_U32 scratchWidth = (gX_U32)scratch->texture.width;
    gX_U32 scratchHeight = (gX_U32)scratch->texture.height;
    if (targetWidth == 0 || targetHeight == 0 || scratchWidth == 0 || scratchHeight == 0) return;
    gX_Batch_Flush();
    gX_RHI_BindReadFBO(target->id);
    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
    gX_RHI_BindDrawFBO(scratch->id);
    gX_RHI_DrawBuffer(gX_RHI_COLOR_ATTACHMENT0);
    gX_RHI_Blit(target->id, scratch->id, 0, 0, targetWidth, targetHeight,
        0, 0, scratchWidth, scratchHeight,
        gX_RHI_COLOR_BUFFER_BIT, gX_RHI_NEAREST);
    gX_RHI_UnbindReadFBO();
    gX_RHI_UnbindDrawFBO();
    gX_Drawing_BeginTexture(*target);
        gX_Drawing_BeginShader(gfx->interlaceShader.shader);
        {
            gX_Matrix mvp = gX_RHI_Matrix_GetProjection();
            gX_RHI_UniformMatrix(gX_RHI_GetUniformLocation(gfx->interlaceShader.shader.id, "mvp"),
                1, 1, &mvp.m0);
        }
        gX_Shader_SetValue_TextureSlot(gfx->interlaceShader.shader,
            gX_RHI_GetUniformLocation(gfx->interlaceShader.shader.id, "sceneTex"),
            scratch->texture, gX_GFX_TEXUNIT_SCENE);
        gX_RHI_UniformInt(gX_RHI_GetUniformLocation(gfx->interlaceShader.shader.id, "interlaceMode"),
            interlaceMode);
        gX_RHI_Uniform_Float(gX_RHI_GetUniformLocation(gfx->interlaceShader.shader.id, "interlaceStrength"),
            interlaceStrength);
        gX_RHI_Uniform_Float(gX_RHI_GetUniformLocation(gfx->interlaceShader.shader.id, "interlacePhase"),
            interlacePhase);
        gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)targetWidth, (gX_F32)targetHeight);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();
    return;
}
//opencode: Optional native float readback for the RT inspector. Values are
//   dumped only in the requested frame interval; normal renderer runs do no
//   allocation or readback when GX_RT_DUMP_DIR is absent.
static gX_I0 Pass_Dump_RtViewTarget(const gX_I8 *name,
    gX_RenderTexture *target, gX_I1 depthTarget)
{
    static gX_U64 lastDumpFrame = (gX_U64)-1;
    const gX_I8 *dumpDir = getenv("GX_RT_DUMP_DIR");
    const gX_I8 *frameHook = getenv("GX_RT_DUMP_FRAME");
    const gX_I8 *countHook = getenv("GX_RT_DUMP_COUNT");
    gX_U64 firstFrame;
    gX_U64 frameCount;
    gX_U32 width;
    gX_U32 height;
    gX_U32 channels;
    size_t valueCount;
    gX_F32 *pixels;
    gX_I8 path[512];
    FILE *file;
    const gX_I8 magic[8] = {'G', 'X', 'R', 'T', 'F', '3', '2', 0};
    gX_I1 writeOk;
    if (!dumpDir || !dumpDir[0] || !target || target->id == 0) return;
    firstFrame = frameHook && frameHook[0]
        ? (gX_U64)strtoull((const char *)frameHook, NULL, 10)
        : maindata.frameCount;
    frameCount = countHook && countHook[0]
        ? (gX_U64)strtoull((const char *)countHook, NULL, 10) : 1;
    if (frameCount == 0) frameCount = 1;
    if (maindata.frameCount < firstFrame
        || maindata.frameCount - firstFrame >= frameCount
        || maindata.frameCount == lastDumpFrame) return;
    width = target->texture.width;
    height = target->texture.height;
    channels = depthTarget ? 1u : 4u;
    if (width == 0 || height == 0) return;
    valueCount = (size_t)width * (size_t)height * (size_t)channels;
    pixels = (gX_F32 *)malloc(valueCount * sizeof(gX_F32));
    if (!pixels) return;
    snprintf((char *)path, sizeof(path), "%s/%s_%06llu.gxrt",
        (const char *)dumpDir, (const char *)name,
        (unsigned long long)maindata.frameCount);
    file = fopen((const char *)path, "wb");
    if (!file) {
        free(pixels);
        gX_TraceLog_GFX(gX_LOG_WARNING, "RT_DUMP: cannot open %s", path);
        return;
    }
    gX_RHI_BindReadFBO(target->id);
    if (!depthTarget) gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, (gX_I32)width, (gX_I32)height,
        depthTarget ? GL_DEPTH_COMPONENT : GL_RGBA, GL_FLOAT, pixels);
    gX_RHI_UnbindReadFBO();
    writeOk = fwrite(magic, sizeof(magic), 1, file) == 1
        && fwrite(&width, sizeof(width), 1, file) == 1
        && fwrite(&height, sizeof(height), 1, file) == 1
        && fwrite(&channels, sizeof(channels), 1, file) == 1
        && fwrite(pixels, sizeof(gX_F32), valueCount, file) == valueCount;
    fclose(file);
    free(pixels);
    if (!writeOk) {
        remove((const char *)path);
        gX_TraceLog_GFX(gX_LOG_WARNING, "RT_DUMP: incomplete write %s", path);
        return;
    }
    lastDumpFrame = maindata.frameCount;
    gX_TraceLog_GFX(gX_LOG_INFO, "RT_DUMP: %s frame=%llu %ux%u channels=%u",
        name, (unsigned long long)maindata.frameCount, width, height, channels);
}

static gX_I0 Pass_Run_RtView(gX_RendererGFX *gfx, gX_RenderTexture *outputRT)
{
    //opencode: RT inspector — GX_RT_VIEW=name[:gain[:bias]] draws a debug RT
    //   fullscreen. Named views include SSR, AO/SSGI stages, temporal targets,
    //   and fog/LUTs. GX_RT_DUMP_DIR adds opt-in native float readbacks.
    const gX_I8 *hook = getenv("GX_RT_VIEW");
    if (!hook || !hook[0]) return;
    if (!gfx->rtview.shader.loaded || !gfx->rtview.locsCached) return;
    gX_I8 name[32] = {0};
    gX_F32 gain = 1.0f, bias = 0.0f;
    {
        const gX_I8 *colon = strchr((const char *)hook, ':');
        gX_U64 namelen = colon ? (gX_U64)(colon - hook) : strlen((const char *)hook);
        if (namelen == 0 || namelen >= sizeof(name)) return;
        memcpy(name, hook, (size_t)namelen);
        if (colon) {
            gain = (gX_F32)atof(colon + 1);
            const gX_I8 *colon2 = strchr((const char *)colon + 1, ':');
            if (colon2) bias = (gX_F32)atof(colon2 + 1);
        } else if (!strcmp((const char *)name, "velocity")) {
            gain = 20.0f; bias = 0.5f;
        }
    }
    gX_Texture srcTex = {0};
    gX_RenderTexture *srcTarget = NULL;
    gX_I1 haveTex = 0;
    gX_I1 depthTarget = 0;
    gX_I32 channelMode = 0;
    if ((!strcmp((const char *)name, "ssr")
            || !strcmp((const char *)name, "ssrNoTAA"))
        && gfx->ssr.map.wrapper.loadingState.loaded) {
        srcTex = gfx->ssr.map.wrapper.rt.texture;
        srcTarget = &gfx->ssr.map.wrapper.rt; haveTex = 1;
    } else if (!strcmp((const char *)name, "normal")
        && gfx->ssr.normalMap.wrapper.loadingState.loaded) {
        //opencode: SSR normal-buffer inspector (default-on evaluation
        //   follow-up) — same LAST-pass present pattern as the other names.
        srcTex = gfx->ssr.normalMap.wrapper.rt.texture;
        srcTarget = &gfx->ssr.normalMap.wrapper.rt; haveTex = 1;
    } else if ((!strcmp((const char *)name, "velocity")
            || !strcmp((const char *)name, "velocityStatic"))
        && gfx->velocity.map.wrapper.loadingState.loaded) {
        srcTex = gfx->velocity.map.wrapper.rt.texture;
        srcTarget = &gfx->velocity.map.wrapper.rt; haveTex = 1; channelMode = 2;
    } else if (!strcmp((const char *)name, "ssao")
        && gfx->ssao.map.wrapper.loadingState.loaded) {
        srcTex = gfx->ssao.map.wrapper.rt.texture;
        srcTarget = &gfx->ssao.map.wrapper.rt; haveTex = 1; channelMode = 1;
    } else if (!strcmp((const char *)name, "ssaoBlur")
        && gfx->ssao.blurMap.wrapper.loadingState.loaded) {
        srcTex = gfx->ssao.blurMap.wrapper.rt.texture;
        srcTarget = &gfx->ssao.blurMap.wrapper.rt; haveTex = 1; channelMode = 1;
    } else if (!strcmp((const char *)name, "gtao")
        && gfx->ssao.gtaoFinalTex.id != 0) {
        srcTex = gfx->ssao.gtaoFinalTex; haveTex = 1; channelMode = 1;
        if (srcTex.id == gfx->ssao.gtaoAccumA.wrapper.rt.texture.id)
            srcTarget = &gfx->ssao.gtaoAccumA.wrapper.rt;
        else if (srcTex.id == gfx->ssao.gtaoAccumB.wrapper.rt.texture.id)
            srcTarget = &gfx->ssao.gtaoAccumB.wrapper.rt;
        else srcTarget = &gfx->ssao.blurMap.wrapper.rt;
    } else if (!strcmp((const char *)name, "gtaoHist")) {
        gX_RendererGFX_RenderTexture *target =
            gX_RendererGFX_TemporalHistory_CurrentTarget(&gfx->ssao.gtaoTemporal,
                &gfx->ssao.gtaoAccumA, &gfx->ssao.gtaoAccumB);
        if (target && target->wrapper.loadingState.loaded) {
            srcTex = target->wrapper.rt.texture;
            srcTarget = &target->wrapper.rt; haveTex = 1; channelMode = 1;
        }
    } else if (!strcmp((const char *)name, "gtaoA")
        && gfx->ssao.gtaoAccumA.wrapper.loadingState.loaded) {
        srcTex = gfx->ssao.gtaoAccumA.wrapper.rt.texture;
        srcTarget = &gfx->ssao.gtaoAccumA.wrapper.rt;
        haveTex = 1; channelMode = 1;
    } else if (!strcmp((const char *)name, "gtaoB")
        && gfx->ssao.gtaoAccumB.wrapper.loadingState.loaded) {
        srcTex = gfx->ssao.gtaoAccumB.wrapper.rt.texture;
        srcTarget = &gfx->ssao.gtaoAccumB.wrapper.rt;
        haveTex = 1; channelMode = 1;
    } else if (!strcmp((const char *)name, "ssgiRaw")
        && gfx->ssao.ssgiMap.wrapper.loadingState.loaded) {
        srcTex = gfx->ssao.ssgiMap.wrapper.rt.texture;
        srcTarget = &gfx->ssao.ssgiMap.wrapper.rt; haveTex = 1;
    } else if (!strcmp((const char *)name, "ssgiBlur")
        && gfx->ssao.ssgiBlurMap.wrapper.loadingState.loaded) {
        srcTex = gfx->ssao.ssgiBlurMap.wrapper.rt.texture;
        srcTarget = &gfx->ssao.ssgiBlurMap.wrapper.rt; haveTex = 1;
    } else if (!strcmp((const char *)name, "ssgi")
        && gfx->ssao.ssgiFinalTex.id != 0) {
        srcTex = gfx->ssao.ssgiFinalTex; haveTex = 1;
        if (srcTex.id == gfx->ssao.ssgiAccumA.wrapper.rt.texture.id)
            srcTarget = &gfx->ssao.ssgiAccumA.wrapper.rt;
        else if (srcTex.id == gfx->ssao.ssgiAccumB.wrapper.rt.texture.id)
            srcTarget = &gfx->ssao.ssgiAccumB.wrapper.rt;
        else srcTarget = &gfx->ssao.ssgiBlurMap.wrapper.rt;
    } else if (!strcmp((const char *)name, "ssgiHist")) {
        gX_RendererGFX_RenderTexture *target =
            gX_RendererGFX_TemporalHistory_CurrentTarget(&gfx->ssao.ssgiTemporal,
                &gfx->ssao.ssgiAccumA, &gfx->ssao.ssgiAccumB);
        if (target && target->wrapper.loadingState.loaded) {
            srcTex = target->wrapper.rt.texture;
            srcTarget = &target->wrapper.rt; haveTex = 1;
        }
    } else if (!strcmp((const char *)name, "ssgiA")
        && gfx->ssao.ssgiAccumA.wrapper.loadingState.loaded) {
        srcTex = gfx->ssao.ssgiAccumA.wrapper.rt.texture;
        srcTarget = &gfx->ssao.ssgiAccumA.wrapper.rt; haveTex = 1;
    } else if (!strcmp((const char *)name, "ssgiB")
        && gfx->ssao.ssgiAccumB.wrapper.loadingState.loaded) {
        srcTex = gfx->ssao.ssgiAccumB.wrapper.rt.texture;
        srcTarget = &gfx->ssao.ssgiAccumB.wrapper.rt; haveTex = 1;
    } else if ((!strcmp((const char *)name, "fog")
            || !strcmp((const char *)name, "fogNoTAA"))
        && gfx->fogVolume.genMap.wrapper.loadingState.loaded) {
        srcTex = gfx->fogVolume.genMap.wrapper.rt.texture;
        srcTarget = &gfx->fogVolume.genMap.wrapper.rt; haveTex = 1;
    } else if (!strcmp((const char *)name, "skyLut")
        && gfx->ssr.skyLut.wrapper.loadingState.loaded) {
        srcTex = gfx->ssr.skyLut.wrapper.rt.texture;
        srcTarget = &gfx->ssr.skyLut.wrapper.rt; haveTex = 1;
    } else if (!strcmp((const char *)name, "fogLut")
        && gfx->ssr.fogLut.wrapper.loadingState.loaded) {
        srcTex = gfx->ssr.fogLut.wrapper.rt.texture;
        srcTarget = &gfx->ssr.fogLut.wrapper.rt; haveTex = 1;
    } else if (!strcmp((const char *)name, "history")) {
        gX_RendererGFX_RenderTexture *taaHistory =
            gX_RendererGFX_TemporalHistory_CurrentTarget(&gfx->taa.temporal,
                &gfx->taa.historyMap, &gfx->taa.resolveMap);
        if (taaHistory && taaHistory->wrapper.loadingState.loaded) {
            srcTex = taaHistory->wrapper.rt.texture;
            srcTarget = &taaHistory->wrapper.rt; haveTex = 1;
        }
    } else if (!strcmp((const char *)name, "taaA")
        && gfx->taa.historyMap.wrapper.loadingState.loaded) {
        srcTex = gfx->taa.historyMap.wrapper.rt.texture;
        srcTarget = &gfx->taa.historyMap.wrapper.rt; haveTex = 1;
    } else if (!strcmp((const char *)name, "taaB")
        && gfx->taa.resolveMap.wrapper.loadingState.loaded) {
        srcTex = gfx->taa.resolveMap.wrapper.rt.texture;
        srcTarget = &gfx->taa.resolveMap.wrapper.rt; haveTex = 1;
    } else if (!strcmp((const char *)name, "planar")
        && gfx->reflection.planar.wrapper.loadingState.loaded) {
        srcTex = gfx->reflection.planar.wrapper.rt.texture;
        srcTarget = &gfx->reflection.planar.wrapper.rt; haveTex = 1;
    } else if (!strcmp((const char *)name, "mirrorB")
        && gfx->mirror.freshB.wrapper.loadingState.loaded) {
        //opencode: two-mirror inspector — plane-1 bounce RT.
        srcTex = gfx->mirror.freshB.wrapper.rt.texture;
        srcTarget = &gfx->mirror.freshB.wrapper.rt; haveTex = 1;
    } else if (!strcmp((const char *)name, "mirrorHist")
        && gfx->mirror.histB.wrapper.loadingState.loaded) {
        //opencode: two-mirror inspector — plane-1 history RT.
        srcTex = gfx->mirror.histB.wrapper.rt.texture;
        srcTarget = &gfx->mirror.histB.wrapper.rt; haveTex = 1;
    } else if (!strcmp((const char *)name, "mirrorHistA")
        && gfx->mirror.histA.wrapper.loadingState.loaded) {
        //opencode: two-mirror inspector — plane-0 history RT.
        srcTex = gfx->mirror.histA.wrapper.rt.texture;
        srcTarget = &gfx->mirror.histA.wrapper.rt; haveTex = 1;
    } else if (!strcmp((const char *)name, "depth")
        && gfx->bloom.hdrScene.wrapper.loadingState.loaded) {
        srcTex = gfx->bloom.hdrScene.wrapper.rt.depth;
        srcTarget = &gfx->bloom.hdrScene.wrapper.rt;
        haveTex = 1; depthTarget = 1;
    } else if (!strcmp((const char *)name, "resolve")) {
        gX_RendererGFX_RenderTexture *taaResolve =
            gX_RendererGFX_TemporalHistory_CurrentTarget(&gfx->taa.temporal,
                &gfx->taa.historyMap, &gfx->taa.resolveMap);
        if (taaResolve && taaResolve->wrapper.loadingState.loaded) {
            srcTex = taaResolve->wrapper.rt.texture;
            srcTarget = &taaResolve->wrapper.rt; haveTex = 1;
        }
    } else if (!strcmp((const char *)name, "cube")
        && gfx->reflection.cubeReady
        && gfx->reflection.dynamicCubemap.id != 0) {
        srcTex = gfx->reflection.dynamicCubemap; haveTex = 1;
    } else if (!strncmp((const char *)name, "probe", 5)) {
        //opencode: probe grid inspector — GX_RT_VIEW=probe0..probe3.
        unsigned long pi = strtoul((const char *)name + 5, NULL, 10);
        if (pi < (unsigned long)GFX_PROBE_MAX
            && pi < (unsigned long)gfx->reflection.probeCount
            && gfx->reflection.probes[pi].baked
            && gfx->reflection.probes[pi].cube.id != 0) {
            srcTex = gfx->reflection.probes[pi].cube; haveTex = 1;
        }
    }
    if (!haveTex || srcTex.id == 0) return;
    Pass_Dump_RtViewTarget(name, srcTarget, depthTarget);
    gX_RenderTexture *outTgt = outputRT ? outputRT : &renderer.canvas;
    GX_PROFILE_ZONE_BEGIN("gfx:rtview");
    gX_Drawing_BeginTexture(*outTgt);
        gX_Shader vs = gfx->rtview.shader.shader;
        gX_Drawing_BeginShader(vs);
        gX_Shader_SetValue_TextureSlot(vs, gfx->rtview.colorMapLoc,
            srcTex, gX_GFX_TEXUNIT_COLOR);
        gX_I1 isCube = (strncmp((const char *)name, "probe", 5) == 0
            || !strcmp((const char *)name, "cube")) ? 1 : 0;
        if (isCube) {
            //opencode: cubemap textures need the cube bind path (2D bind
            //   leaves the sampler incomplete -> black).
            gX_Shader_SetValue_CubemapSlot(vs, gfx->rtview.cubeMapLoc,
                srcTex, gX_GFX_TEXUNIT_COLOR);
        }
        gX_Shader_SetValue_Int(vs, gfx->rtview.cubeModeLoc, isCube ? 1 : 0);
        gX_Shader_SetValue_Int(vs, gfx->rtview.channelModeLoc, channelMode);
        gX_Shader_SetValue_Float(vs, gfx->rtview.gainLoc, gain);
        gX_Shader_SetValue_Float(vs, gfx->rtview.biasLoc, bias);
        gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth,
            (gX_F32)maindata.renderHeight);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();
    GX_PROFILE_ZONE_END("gfx:rtview");
    return;
}

//opencode: Step 2 — the frame graph. Order = execution order = the old
//   DrawTo inline sequence. See the header comment above the pass types.
static const gX_GFX_Pass s_gfxPasses[PASS_COUNT] = {
    {"shadow",      NULL,               Pass_Run_Shadow,      "models",               "sun.map,csm,pointarray"},
    {"reflections", NULL,               Pass_Run_Reflections, "models,sky",           "planar,cubemap"},
    {"scene_setup", NULL,               Pass_Run_SceneSetup,  "-",                    "LDR,sky,seed,second.rt"},
    {"opaque",      NULL,               Pass_Run_Opaque,      "models",               "second.rt"},
    {"terrain",     NULL,               Pass_Run_Terrain,     "-",                    "second.rt depth"},
    {"velocity",    NULL,               Pass_Run_Velocity,    "VP matrices",          "velocity.map"},
    {"resolve",     NULL,               Pass_Run_Resolve,     "hdrMSAA",              "hdrScene"},
    {"raymarch_depth", Pass_On_RaymarchDepth, Pass_Run_RaymarchDepth,
        "SDF,camera,hdrScene depth-test", "hdrScene color,depth"},
    {"water",       NULL,               Pass_Run_Water,       "hdrScene,cube,planar", "hdrScene"},
    {"labels",      NULL,               Pass_Run_Labels,      "-",                    "hdrScene"},
    //opencode: transparent before ssr_inputs (was after ssr): the water
    //   precedent - transparents must be IN hdrScene color when the mips
    //   and the march read it, so rays hitting glass sample glass (not
    //   the background behind it) and origins read its color. Backdrop is
    //   unchanged (opaque+water+labels either way); depth still has no
    //   glass (no depth write - origins keep background depth, documented
    //   limitation); clusters/scene_copy/ssgi all run later, unaffected.
    {"transparent", NULL,               Pass_Run_Transparent, "hdrScene",             "hdrScene,refractionBg"},
    {"oracle_gpu",   Pass_On_OracleGPU,  Pass_Run_OracleGPU,   "hdrScene,models",      "hdrScene"},
    {"ssr_inputs",  NULL,               Pass_Run_SsrInputs,   "hdrScene",             "normalMap,mips,skyLut,fogLut,hiz"},
    {"ssr",         NULL,               Pass_Run_Ssr,         "hdrScene,hiz,LUTs",    "ssr.map"},
    {"ssr_vis",     Pass_On_SsrVis,     Pass_Run_SsrVis,      "ssr.map",              "hdrScene"},
    {"clusters",    NULL,               Pass_Run_Clusters,    "lights",               "tile grid"},
    {"scene_copy",  Pass_On_SceneCopy,  Pass_Run_SceneCopy,   "hdrScene/LDR",         "bloom.scene"},
    {"ssgi",        Pass_On_Ssgi,       Pass_Run_Ssgi,        "hdrScene.depth,color", "ssgi.map,accum"},
    {"bloom",       NULL,               Pass_Run_Bloom,       "hdrScene",             "bloom.scene"},
    {"godray",      NULL,               Pass_Run_Godray,      "hdrScene",             "compositeTarget"},
    {"lensflare",   NULL,               Pass_Run_Lensflare,   "LDR,hdrScene.depth",   "compositeTarget"},
    {"ssr_comp",    Pass_On_SsrComp,    Pass_Run_SsrComp,     "ssr.map",              "bloom.scene"},
    {"fog",         NULL,               Pass_Run_Fog,         "bloom.scene,depth",    "compositeTarget"},
    //opencode: Step 2 — compositeTarget retarget (was DrawTo line 367 inside
    //   the effects block): post-blit effects must accumulate on outputRT,
    //   not on bloom.scene. The TAA-only exception also needs this retarget
    //   because TAA runs after present.
    {"posttarget",  Pass_On_PostTarget, Pass_Run_PostTarget, "outputRT",           "compositeTarget"},
    {"depthvis",    Pass_On_DepthVis,   Pass_Run_DepthVis,    "depth",                "bloom.scene/hdrScene"},
    {"present",     NULL,               Pass_Run_Present,     "bloom.scene/hdr/LDR",  "canvas"},
    {"scene_mask",  Pass_On_SceneMask,  Pass_Run_SceneMask,   "hdrScene.depth",       "canvas alpha"},
    {"particles",   Pass_On_Particles,  Pass_Run_Particles,   "compositeTarget",      "compositeTarget"},
    {"weather",     Pass_On_Weather,    Pass_Run_Weather,     "compositeTarget",      "compositeTarget"},
    {"splat",       Pass_On_Splat,      Pass_Run_Splat,       "compositeTarget",      "compositeTarget"},
    {"motionblur",  Pass_On_MotionBlur, Pass_Run_MotionBlur,  "compositeTarget",      "compositeTarget"},
    {"dof",         Pass_On_Dof,        Pass_Run_Dof,         "compositeTarget",      "compositeTarget"},
    {"taa",         Pass_On_Taa,        Pass_Run_Taa,         "compositeTarget",      "compositeTarget"},
    {"ssao",        Pass_On_Ssao,       Pass_Run_Ssao,        "hdrScene.depth",       "hdrScene"},
    {"contact",     Pass_On_Contact,    Pass_Run_Contact,    "hdrScene.depth",       "contact.map"},
    {"oracle_preview", Pass_On_OraclePreview, Pass_Run_OraclePreview,
        "rayPreview,raster.present", "canvas.preview"},
    {"overlays",    NULL,               Pass_Run_Overlays,    "-",                    "overlayTgt"},
    {"hud",         NULL,               Pass_Run_Hud,         "-",                    "overlayTgt"},
    {"palette",     Pass_On_Palette,    Pass_Run_Palette,     "canvas,sceneColorLDR",   "canvas"},
    {"interlace",   Pass_On_Interlace,  Pass_Run_Interlace,   "canvas,sceneColorLDR",   "canvas"},
    //opencode: inspector is last so it sees this frame's temporal/fog outputs
    //   and captures a clean buffer view without overlays or presentation FX.
    {"rtview",      Pass_On_RtView,     Pass_Run_RtView,       "debug RT",              "canvas"},
};
static gX_I0 Pass_RunRange(gX_RendererGFX *gfx, gX_RenderTexture *outputRT, gX_I32 from, gX_I32 to)
{
    if (from < 0 || to > (gX_I32)PASS_COUNT || from >= to) {
        gX_TraceLog_GFX(gX_LOG_ERROR, "Pass_RunRange: invalid range %d..%d", from, to);
        return;
    }
    for (gX_I32 i = from; i < to; i++) {
        const gX_GFX_Pass *p = &s_gfxPasses[i];
        if (!p->enabled || p->enabled(gfx)) {
            gX_RHI_BufferAudit_BeginPass(p->name);
            p->run(gfx, outputRT);
            gX_RHI_BufferAudit_EndPass();
        }
    }
    return;
}

//opencode: name the known GFX render targets in the RHI lifecycle audit so
//   target IDs can be followed across producers, clears, blits, and samples.
static gX_I0 Pass_AuditTarget(const gX_I8 *name,
    gX_RendererGFX_RenderTexture *target)
{
    if (!target || !target->wrapper.loadingState.loaded) return;
    gX_RenderTexture *rt = &target->wrapper.rt;
    gX_RHI_BufferAudit_RegisterTarget(rt->id, rt->texture.id, rt->depth.id,
        rt->texture.width, rt->texture.height, name);
}

static gX_I0 Pass_AuditTargets(gX_RendererGFX *gfx, gX_RenderTexture *outputRT)
{
    if (!gX_RHI_BufferAudit_IsEnabled()) return;
    if (outputRT) {
        gX_RHI_BufferAudit_RegisterTarget(outputRT->id, outputRT->texture.id,
            outputRT->depth.id, outputRT->texture.width, outputRT->texture.height,
            (const gX_I8 *)"outputRT");
    } else {
        gX_RHI_BufferAudit_RegisterTarget(renderer.canvas.id, renderer.canvas.texture.id,
            renderer.canvas.depth.id, renderer.canvas.texture.width,
            renderer.canvas.texture.height, (const gX_I8 *)"renderer.canvas");
    }
    if (gfx->sun.camera.map) {
        gX_RenderTexture *shadowMap = gfx->sun.camera.map;
        gX_RHI_BufferAudit_RegisterTarget(shadowMap->id, shadowMap->texture.id,
            shadowMap->depth.id, shadowMap->texture.width,
            shadowMap->texture.height, (const gX_I8 *)"sun.shadowMap");
    }
    Pass_AuditTarget((const gX_I8 *)"sceneColorLDR", &gfx->sceneColorLDR);
    Pass_AuditTarget((const gX_I8 *)"hdrScene", &gfx->bloom.hdrScene);
    Pass_AuditTarget((const gX_I8 *)"hdrMSAA", &gfx->bloom.hdrMSAA);
    Pass_AuditTarget((const gX_I8 *)"bloom.extract", &gfx->bloom.extract);
    Pass_AuditTarget((const gX_I8 *)"bloom.ping", &gfx->bloom.ping);
    Pass_AuditTarget((const gX_I8 *)"bloom.pong", &gfx->bloom.pong);
    Pass_AuditTarget((const gX_I8 *)"bloom.scene", &gfx->bloom.scene);
    Pass_AuditTarget((const gX_I8 *)"bloom.luminance", &gfx->bloom.luminanceRT);
    Pass_AuditTarget((const gX_I8 *)"reflection.planar", &gfx->reflection.planar);
    Pass_AuditTarget((const gX_I8 *)"ssr.map", &gfx->ssr.map);
    Pass_AuditTarget((const gX_I8 *)"ssr.normal", &gfx->ssr.normalMap);
    Pass_AuditTarget((const gX_I8 *)"ssr.skyLut", &gfx->ssr.skyLut);
    Pass_AuditTarget((const gX_I8 *)"ssr.fogLut", &gfx->ssr.fogLut);
    Pass_AuditTarget((const gX_I8 *)"velocity.map", &gfx->velocity.map);
    Pass_AuditTarget((const gX_I8 *)"debugVis.refractionBg", &gfx->debugVis.refractionBg);
    Pass_AuditTarget((const gX_I8 *)"csm.map", &gfx->csm.map);
    Pass_AuditTarget((const gX_I8 *)"taa.history", &gfx->taa.historyMap);
    Pass_AuditTarget((const gX_I8 *)"taa.resolve", &gfx->taa.resolveMap);
    Pass_AuditTarget((const gX_I8 *)"motionBlur.map", &gfx->motionBlur.map);
    Pass_AuditTarget((const gX_I8 *)"dof.coc", &gfx->dof.cocMap);
    Pass_AuditTarget((const gX_I8 *)"dof.blur", &gfx->dof.blurMap);
    Pass_AuditTarget((const gX_I8 *)"dof.ping", &gfx->dof.blurPing);
    Pass_AuditTarget((const gX_I8 *)"dof.composite", &gfx->dof.compositeMap);
    Pass_AuditTarget((const gX_I8 *)"ssao.map", &gfx->ssao.map);
    Pass_AuditTarget((const gX_I8 *)"ssao.blur", &gfx->ssao.blurMap);
    Pass_AuditTarget((const gX_I8 *)"gtao.accumA", &gfx->ssao.gtaoAccumA);
    Pass_AuditTarget((const gX_I8 *)"gtao.accumB", &gfx->ssao.gtaoAccumB);
    Pass_AuditTarget((const gX_I8 *)"ssgi.map", &gfx->ssao.ssgiMap);
    Pass_AuditTarget((const gX_I8 *)"ssgi.blur", &gfx->ssao.ssgiBlurMap);
    Pass_AuditTarget((const gX_I8 *)"ssgi.accumA", &gfx->ssao.ssgiAccumA);
    Pass_AuditTarget((const gX_I8 *)"ssgi.accumB", &gfx->ssao.ssgiAccumB);
    Pass_AuditTarget((const gX_I8 *)"fogVolume.gen", &gfx->fogVolume.genMap);
    Pass_AuditTarget((const gX_I8 *)"godray.gen", &gfx->godray.genMap);
    Pass_AuditTarget((const gX_I8 *)"contact.map", &gfx->contact.map);
    Pass_AuditTarget((const gX_I8 *)"mirror.fresh", &gfx->mirror.freshB);
    Pass_AuditTarget((const gX_I8 *)"mirror.histA", &gfx->mirror.histA);
    Pass_AuditTarget((const gX_I8 *)"mirror.histB", &gfx->mirror.histB);
}
//opencode: Step 2 — load-time table validation (uniqueness/completeness of
//   the graph wiring; full input-produced-by-earlier-pass dataflow checking
//   is future work). Runs once on the first frame.
static gX_I0 Pass_ValidateTable(gX_I0)
{
    for (gX_I32 i = 0; i < (gX_I32)PASS_COUNT; i++) {
        if (!s_gfxPasses[i].run) {
            gX_TraceLog_GFX(gX_LOG_ERROR, "Pass table: entry %d has no run fn", i);
            return;
        }
        if (!s_gfxPasses[i].name || !s_gfxPasses[i].reads || !s_gfxPasses[i].writes) {
            gX_TraceLog_GFX(gX_LOG_ERROR, "Pass table: entry %d missing name/reads/writes tags", i);
            return;
        }
        for (gX_I32 j = i + 1; j < (gX_I32)PASS_COUNT; j++) {
            if (!strcmp(s_gfxPasses[i].name, s_gfxPasses[j].name)) {
                gX_TraceLog_GFX(gX_LOG_ERROR, "Pass table: duplicate pass name '%s'", s_gfxPasses[i].name);
                return;
            }
        }
    }
    return;
}
//opencode: Step-3-bis — living frame-graph inventory (GX_PASS_DUMP=1 dumps
//   the table once with live enable-state). String-tag dataflow validation
//   was deliberately NOT built: every historical wiring bug (stale planar
//   sampling, disengaged composite chain, swapped MSAA target) would pass a
//   tag check — the tags document, the gates (matrix/suite/A-B) verify.
static gX_I0 Pass_Dump(gX_RendererGFX *gfx)
{
    const gX_I8 *pd = getenv("GX_PASS_DUMP");
    if (!pd || pd[0] != '1') return;
    for (gX_I32 i = 0; i < (gX_I32)PASS_COUNT; i++) {
        const gX_GFX_Pass *p = &s_gfxPasses[i];
        gX_TraceLog_GFX(gX_LOG_NOTICE, "PASS %02d %-11s %s  reads:{%s} writes:{%s}",
            i, p->name, (!p->enabled || p->enabled(gfx)) ? "ON " : "off", p->reads, p->writes);
    }
}

//ai: profiling partitions the existing ordered pass stream without reordering or repeating any draw.
gX_I0 gX_RendererGFX_DrawTo(gX_RendererGFX *gfx, gX_RenderTexture *outputRT) {
    //opencode: GUI grid owns final presentation for this opt-in mode. Its
    //   inner DrawTo calls supply a tile target and take the normal pipeline.
    if (!outputRT && gX_RendererGFX_ViewportGrid_Enabled()) {
        gX_RendererGFX_ViewportGrid_Draw(gfx);
        return;
    }
    //opencode: defer shader hot-reload swaps during the render pass — the
    //   swap deletes the old program, which may be the one currently bound
    //   (Shader_Update runs inside Render_Draw per pass)
    gX_Shader_Compiler_SetDeferSwaps(1);
    { while (gX_RHI_GetError() != gX_RHI_NO_ERROR) {} }
    GX_GL_CHECK();

    //opencode: deferred init for GPU timers (needs GL context)
    if (!gfx->timersInited && maindata.backendLoaded) {
        gX_RHI_Query_Timer_Init(&gfx->timerShadow,     "gfx:shadow");
        gX_RHI_Query_Timer_Init(&gfx->timerReflections, "gfx:reflections");
        gX_RHI_Query_Timer_Init(&gfx->timerOpaque,      "gfx:opaque");
        gX_RHI_Query_Timer_Init(&gfx->timerBloom,       "gfx:bloom");
        gX_RHI_Query_Timer_Init(&gfx->timerSSR,         "gfx:ssr");
        gX_RHI_Query_Timer_Init(&gfx->timerTotal,       "gfx:total");
        gfx->timersInited = 1;
        gX_TraceLog_GFX(gX_LOG_INFO, "GPU timers initialized (ARB_timer_query=%d)",
            gX_RHI_Query_IsAvailable(gX_RHI_TIMESTAMP));
    }
    if (arguments.gfxProfile && !gfx->profileAttempted)
    {
        gfx->profileAttempted = 1;
        gfx->profile = gX_RendererGFX_Profile_Init(arguments.gfxProfilePath);
    }
    //opencode: reset per-frame counters at start of each draw call
    gX_RHI_ResetErrorCount();
    gX_RHI_ResetAllCounters();
    gX_RHI_EventLog_NextFrame();
    gX_RHI_BufferAudit_BeginFrame(maindata.frameCount);
    Pass_AuditTargets(gfx, outputRT);
    //opencode: raylib/rlgl mutates GL state behind RHI's back (batch flushes,
    //   rlSetBlendMode, texture draws). Invalidate the RHI state caches each
    //   frame so cached skips never fire on stale values. Re-prime is cheap
    //   (6 glIsEnabled on first use).
    gX_RHI_InvalidateCaches();

    if (gfx->timersInited) {
        //opencode: poll last frame before reusing its timestamp query pair.
        gX_RHI_Query_Timer_Get(&gfx->timerShadow);
        gX_RHI_Query_Timer_Get(&gfx->timerReflections);
        gX_RHI_Query_Timer_Get(&gfx->timerOpaque);
        gX_RHI_Query_Timer_Get(&gfx->timerBloom);
        gX_RHI_Query_Timer_Get(&gfx->timerSSR);
        gX_RHI_Query_Timer_Get(&gfx->timerTotal);
        gX_RHI_Query_Timer_Begin(&gfx->timerTotal);
    }

    gX_RendererGFX_Profile_Begin(gfx->profile, maindata.frameCount);
    gX_RHI_Enable(gX_RHI_DEPTH_TEST);

    //opencode: Step 2 — frame-graph validation runs once (uniqueness +
    //   completeness of the pass table; failures log, execution continues).
    //   GX_PASS_DUMP=1 also prints the table with live enable-state.
    {
        static gX_I1 passValidated = 0;
        if (!passValidated) {
            passValidated = 1;
            Pass_ValidateTable();
            Pass_Dump(gfx);
        }
    }

    gX_RendererGFX_Profile_StageBegin(gfx->profile, GX_GFX_PROFILE_SHADOW);
    if (gfx->timersInited) gX_RHI_Query_Timer_Begin(&gfx->timerShadow);
    Pass_RunRange(gfx, outputRT, PASS_SHADOW, PASS_REFLECTIONS);
    if (gfx->timersInited) gX_RHI_Query_Timer_End(&gfx->timerShadow);
    gX_RendererGFX_Profile_StageEnd(gfx->profile, GX_GFX_PROFILE_SHADOW);
    GX_GL_CHECK();

    gX_RendererGFX_Profile_StageBegin(gfx->profile, GX_GFX_PROFILE_REFLECTIONS);
    if (gfx->timersInited) gX_RHI_Query_Timer_Begin(&gfx->timerReflections);
    Pass_RunRange(gfx, outputRT, PASS_REFLECTIONS, PASS_SCENE_SETUP);
    if (gfx->timersInited) gX_RHI_Query_Timer_End(&gfx->timerReflections);
    gX_RendererGFX_Profile_StageEnd(gfx->profile, GX_GFX_PROFILE_REFLECTIONS);
    GX_GL_CHECK();

    gX_RendererGFX_Profile_StageBegin(gfx->profile, GX_GFX_PROFILE_OPAQUE);
    if (gfx->timersInited) gX_RHI_Query_Timer_Begin(&gfx->timerOpaque);
    gX_RHI_Debug_PushGroup("GFX_Scene");
    GX_PROFILE_ZONE_BEGIN("gfx:sky_and_opaque");
    Pass_RunRange(gfx, outputRT, PASS_SCENE_SETUP, PASS_LABELS);

    GX_PROFILE_ZONE_END("gfx:sky_and_opaque");
    gX_RHI_Debug_PopGroup();
    if (gfx->timersInited) gX_RHI_Query_Timer_End(&gfx->timerOpaque);
    gX_RendererGFX_Profile_StageEnd(gfx->profile, GX_GFX_PROFILE_OPAQUE);
    GX_GL_CHECK();

    gX_RendererGFX_Profile_StageBegin(gfx->profile, GX_GFX_PROFILE_PRE_SSR);
    Pass_RunRange(gfx, outputRT, PASS_LABELS, PASS_SSR_INPUTS);
    gX_RendererGFX_Profile_StageEnd(gfx->profile, GX_GFX_PROFILE_PRE_SSR);

    gX_RendererGFX_Profile_StageBegin(gfx->profile, GX_GFX_PROFILE_SSR_INPUTS);
    Pass_RunRange(gfx, outputRT, PASS_SSR_INPUTS, PASS_SSR);
    gX_RendererGFX_Profile_StageEnd(gfx->profile, GX_GFX_PROFILE_SSR_INPUTS);


    gX_RendererGFX_Profile_StageBegin(gfx->profile, GX_GFX_PROFILE_SSR);
    if (gfx->timersInited) gX_RHI_Query_Timer_Begin(&gfx->timerSSR);
    Pass_RunRange(gfx, outputRT, PASS_SSR, PASS_SSR_VIS);
    //opencode: end the SSR timer here (was inside the gated effects block —
    //   when bloom was disabled the End query was never issued and its
    //   availability check errored every frame).
    if (gfx->timersInited) gX_RHI_Query_Timer_End(&gfx->timerSSR);
    gX_RendererGFX_Profile_StageEnd(gfx->profile, GX_GFX_PROFILE_SSR);

    gX_RendererGFX_Profile_StageBegin(gfx->profile, GX_GFX_PROFILE_PRE_BLOOM);
    Pass_RunRange(gfx, outputRT, PASS_SSR_VIS, PASS_BLOOM);
    gX_RendererGFX_Profile_StageEnd(gfx->profile, GX_GFX_PROFILE_PRE_BLOOM);


    gX_RendererGFX_Profile_StageBegin(gfx->profile, GX_GFX_PROFILE_BLOOM);
    if (gfx->timersInited) gX_RHI_Query_Timer_Begin(&gfx->timerBloom);
    Pass_RunRange(gfx, outputRT, PASS_BLOOM, PASS_GODRAY);
    if (gfx->timersInited) gX_RHI_Query_Timer_End(&gfx->timerBloom);
    gX_RendererGFX_Profile_StageEnd(gfx->profile, GX_GFX_PROFILE_BLOOM);

    gX_RendererGFX_Profile_StageBegin(gfx->profile, GX_GFX_PROFILE_POST);
    Pass_RunRange(gfx, outputRT, PASS_GODRAY, PASS_COUNT);
    gX_RendererGFX_Profile_StageEnd(gfx->profile, GX_GFX_PROFILE_POST);

    gX_RHI_BufferAudit_EndFrame();


    if (gfx->timersInited) {
        gX_RHI_Query_Timer_End(&gfx->timerTotal);
    }
    gX_RendererGFX_Profile_End(gfx->profile);

    gX_Shader_Compiler_SetDeferSwaps(0);

    {
        gX_Matrix curView = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
        gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
        gX_Matrix curProj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect, camera.frustum.near, camera.frustum.far);
        gfx->motionBlurPrevVP = gX_Matrix_Multiply(curView, curProj);
        //opencode: TAA's private snapshot AFTER all passes ran (motion blur
        //   overwrote the shared one mid-pipeline) - next frame's fallback
        //   reprojection reads true previous-frame VP from taa.c.
        gfx->taaPrevVP = gfx->motionBlurPrevVP;
    }
}
