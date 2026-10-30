/**********************************************************************************************
*
*   [gX: src/renderer/gfx/draw.c]
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


//opencode: extracted from gfx.c DrawTo — per-pass draw wrappers
#include <string.h>
#include <math.h>
#include <stdio.h>

#include "../../common/log.h"
//ai: debug-only live depth allocation diagnostics; Release contains no probe hook.
#include "../../rhi/rhi_depth_probe.h"
#include "../../common/macros.h"
#include "../../common/profiler.h"
#include "../../common/path.h"
#include "../../common/utils.h"
#include "../../gxmath/matrix.h"
#include "../../gxmath/constants.h"
#include "../gfx.h"
#include "glass_contract.h"
#include "../renderer.h"
#include "../../main.h"
#include "../../camera.h"
#include "../../wrappers/wrappers.h"
//opencode: gX — game/ephemeris dropped
//#include "../../game/ephemeris.h"
//#include "../../game/game.h"
#include "../cubemap.h"
#include "draw.h"
#include "boilerplate.h"
#include "motion_path.h"
#include "bloom.h"
#include "dof.h"
#include "taa.h"
#include "motionblur.h"
#include "../../common/utils/billboard.h" //opencode: billboard markers
#include "../../rhi/rhi_fbo.h"
#include "../../rhi/rhi_gl.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_matrix.h"
#include "../../rhi/rhi_state.h"
#include "../../rhi/rhi_tex.h"
#include "../../rhi/rhi_buffer_audit.h"
#include "../../rhi/batch.h"
#include "reflection_plane.h"
//opencode: hunt - implicit-decl sweep

static gX_RendererGFX_Shader g_planarShader;
//opencode: v0.3.8 - planar pass depth as a SAMPLEABLE texture (was an RBO,
//   which the fog gen cannot read); owned here, consumed by PlanarFog
static gX_U32 g_planarDepthTex = 0;
#include <stdlib.h>
#include "../../rhi/gl_state.h"

//opencode: cubemap draw state — set by Draw_Reflections before rendering
static gX_RendererGFX *g_cubemap_gfx = NULL;
//ai: classify capture contents across all six callback invocations.
static gX_I1 g_cubemap_hasLocal = 0;

//ai: compile-isolated coverage experiment; default builds contain no readback hooks.
#if defined(GX_PROBE_COVERAGE_EXPERIMENT)
static gX_I1 gfx_probe_proxy_split_enabled(gX_I0)
{
    const gX_I8 *fixture = getenv("GX_PROBE_PROXY_FIXTURE");
    const gX_I8 *split = getenv("GX_PROBE_PROXY_SPLIT");
    const gX_I8 *coverage = getenv("GX_PROBE_COVERAGE");
    return fixture && strcmp((const char *)fixture, "1") == 0
        && split && strcmp((const char *)split, "1") == 0
        && coverage && strcmp((const char *)coverage, "2") == 0;
}

static gX_I32 gfx_probe_coverage_mode(gX_I0)
{
    const gX_I8 *hook = getenv("GX_PROBE_COVERAGE");
    if (hook && strcmp((const char *)hook, "2") == 0) return 2;
    return hook && strcmp((const char *)hook, "1") == 0 ? 1 : 0;
}

static gX_I0 gfx_probe_coverage_uniform(gX_Shader shader, gX_I32 mode)
{
    gX_RHI_Shader_Use(shader.id);
    gX_RHI_UniformInt(gX_RHI_GetUniformLocation(shader.id,
        (const gX_I8 *)"gXProbeCoverageCapture"), mode);
}

static gX_I0 gfx_probe_coverage_dump(gX_Camera3D *faceCam, gX_I32 faceIndex, gX_I32 mode)
{
    const gX_I8 *directory = getenv("GX_PROBE_COVERAGE_DIR");
    if (!directory || !directory[0]) return;
    if (maindata.frameCount < 30 || maindata.frameCount > 33) return;
    gX_I32 viewport[4];
    gX_RHI_GetIntegerv(gX_RHI_VIEWPORT, viewport);
    gX_U32 width = (gX_U32)viewport[2];
    gX_U32 height = (gX_U32)viewport[3];
    size_t count = (size_t)width * height * 4;
    gX_F32 *pixels = malloc(count * sizeof(gX_F32));
    if (!pixels)
    {
        gX_TraceLog_GFX(gX_LOG_ERROR, "PROBE_COVERAGE: readback allocation failed");
        return;
    }
    gX_RHI_ReadPixels(0, 0, width, height, gX_RHI_RGBA, gX_RHI_FLOAT, pixels);
    gX_I8 path[512];
    snprintf((char *)path, sizeof(path), "%s/%llu_%g_%g_%g_%d.bin",
        (const char *)directory, (unsigned long long)maindata.frameCount,
        faceCam->position.x, faceCam->position.y, faceCam->position.z, faceIndex);
    FILE *file = fopen((const char *)path, "wb");
    gX_I1 ok = 0;
    if (file)
    {
        const gX_I8 *magic = (const gX_I8 *)(mode == 2 ? "GXDVF32" : "GXCVF32");
        gX_F32 origin[3] = {faceCam->position.x, faceCam->position.y, faceCam->position.z};
        ok = fwrite(magic, 8, 1, file) == 1
            && fwrite(&width, 4, 1, file) == 1
            && fwrite(&height, 4, 1, file) == 1
            && fwrite(&faceIndex, 4, 1, file) == 1
            && fwrite(origin, sizeof(origin), 1, file) == 1
            && fwrite(pixels, sizeof(gX_F32), count, file) == count;
        fclose(file);
    }
    free(pixels);
    if (!ok) gX_TraceLog_GFX(gX_LOG_ERROR, "PROBE_COVERAGE: dump failed %s", path);
}
#endif

//opencode: v0.1.16 — forward decl: procedural sky inner draw (defined below the
//   cubemap face callback, which uses it)
static gX_I0 gfx_sky_upload_and_draw(gX_RendererGFX *gfx, gX_F32 w, gX_F32 h,
                                     gX_I1 equirect,
                                     gX_F32v3 cpos, gX_F32v3 ctarget, gX_F32v3 cup,
                                     gX_F32 fovy, gX_I1 ImageBackground);

//opencode: Phase 1 — MSAA resolve: copy the NAMED hdrMSAA target into
//   hdrScene. No handle swapping (the old MSAA_Bind rewrote hdrScene's
//   ids mid-frame); hdrScene keeps single-sample handles all frame, so
//   every hdrScene.texture reader stays valid. No-op when MSAA is off.
gX_I0 gX_RendererGFX_MSAA_Resolve(gX_RendererGFX *gfx)
{
    if (!gfx->bloom.hdrMSAA.wrapper.loadingState.loaded) return; // MSAA off
    gX_U32 msaaFbo = gfx->bloom.hdrMSAA.wrapper.rt.id;
    gX_U32 dstFbo = gfx->bloom.hdrScene.wrapper.rt.id;
    if (msaaFbo == 0 || dstFbo == 0) return;
    gX_U32 w = (gX_U32)maindata.renderWidth;
    gX_U32 h = (gX_U32)maindata.renderHeight;

    gX_Batch_Flush();
    //opencode: MSAA resolve as two single-aspect blits (color, then depth).
    //   A combined COLOR+DEPTH multisample-resolve blit raises
    //   GL_INVALID_OPERATION on llvmpipe (Mesa) — the dst kept its cleared
    //   black while the render itself was fine ("llvmpipe blank composite",
    //   2026-09-07). Split blits are legal on every driver; color lands
    //   first so a depth-blit failure can never blank the frame.
    gX_RHI_Blit(msaaFbo, dstFbo, 0, 0, w, h, 0, 0, w, h, gX_RHI_COLOR_BUFFER_BIT, gX_RHI_NEAREST);
    //opencode: Step-3-bis — Mesa software rasterizers (llvmpipe/softpipe/
    //   swrast) reject even the split DEPTH resolve (INVALID_OPERATION,
    //   verified via apitrace) — there the dst depth stays cleared and
    //   depth-driven effects (SSR march, GTAO) degrade gracefully while
    //   color stays correct. Skip the doomed blit (once-detected) so it
    //   doesn't spam the GL error state every frame.
    {
        static gX_I1 depthResolveProbed = 0;
        static gX_I1 depthResolveOk = 1;
        if (!depthResolveProbed) {
            depthResolveProbed = 1;
            const gX_I8 *rend = gX_RHI_GetString(gX_RHI_RENDERER);
            if (rend && (strstr((const char *)rend, "llvmpipe") ||
                         strstr((const char *)rend, "softpipe") ||
                         strstr((const char *)rend, "swrast"))) {
                depthResolveOk = 0;
                gX_TraceLog_GFX(gX_LOG_WARNING, "MSAA depth resolve unsupported on '%s' — depth effects degrade, color unaffected", (const char *)rend);
            }
        }
        if (depthResolveOk)
            gX_RHI_Blit(msaaFbo, dstFbo, 0, 0, w, h, 0, 0, w, h, gX_RHI_DEPTH_BUFFER_BIT, gX_RHI_NEAREST);
    }
    gX_RHI_UnbindFBO();
}

//opencode: cubemap face draw callback — renders GFX models + skybox for one face
static gX_I0 gfx_cubemap_draw_face(gX_Camera3D *faceCam, gX_I32 faceIndex)
{
    gX_RendererGFX *cgfx = g_cubemap_gfx;
    if (!cgfx) return;
#if defined(GX_PROBE_COVERAGE_EXPERIMENT)
    gX_I1 drawSky = cgfx->state.ENABLE_PROCEDURAL_SKY;
    if (gfx_probe_proxy_split_enabled() && !cgfx->state.ENABLE_SSR) drawSky = 0;
    gX_I32 coverage = gfx_probe_coverage_mode();
    if (coverage)
    {
        gX_RHI_ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT);
        gfx_probe_coverage_uniform(cgfx->proceduralSky.shader, coverage);
        gX_Drawing_BeginBlendCustomSeparate(gX_BLEND_ONE, gX_BLEND_ZERO,
            gX_BLEND_ONE, gX_BLEND_ZERO);
    }
#endif

    //opencode: v0.1.16 — procedural sky into the face (before the models).
    //   The old clear color was the only "sky" — flat (200,200,255) gray-blue,
    //   which washed the water + model reflections. The sky quad needs the 2D
    //   ortho state (BeginTexture's) and depth test/write off so it only fills
    //   the background; the models come after with Begin3D re-enabling depth.
#if defined(GX_PROBE_COVERAGE_EXPERIMENT)
    if (drawSky && cgfx->proceduralSky.shader.id > 0)
#else
    if (cgfx->state.ENABLE_PROCEDURAL_SKY && cgfx->proceduralSky.shader.id > 0)
#endif
    {
        gX_RHI_Disable(gX_RHI_DEPTH_TEST);
        gX_RHI_Depth_Write(0);
        gX_RHI_Matrix_Mode(GX_RHI_MATRIX_PROJECTION);
        gX_RHI_Matrix_LoadIdentity();
        gX_RHI_Matrix_Ortho(0, (gX_F32)cgfx->reflection.cubeSize,
                            (gX_F32)cgfx->reflection.cubeSize, 0, 0.0, 1.0);
        gX_RHI_Matrix_Mode(GX_RHI_MATRIX_MODELVIEW);
        gX_RHI_Matrix_LoadIdentity();
        gfx_sky_upload_and_draw(cgfx, (gX_F32)cgfx->reflection.cubeSize,
                                (gX_F32)cgfx->reflection.cubeSize, 0,
                                (gX_F32v3){0.0f, 0.0f, 0.0f},
                                gX_CUBEMAP_TARGETS[faceIndex],
                                gX_CUBEMAP_UPS[faceIndex], 90.0f, 0);
        gX_RHI_Enable(gX_RHI_DEPTH_TEST);
        gX_RHI_Depth_Write(1);
    }

#if defined(GX_PROBE_COVERAGE_EXPERIMENT)
    if (coverage)
    {
        gX_Batch_Flush();
        gfx_probe_coverage_uniform(cgfx->proceduralSky.shader, 0);
        if (coverage == 1) gX_Drawing_BeginBlend(0);
        gfx_probe_coverage_uniform(cgfx->shader.shader, coverage);
    }
#endif
    gX_Drawing_Begin3D(*faceCam);
    //opencode: cubemap faces use both matrix stacks because the raylib model
    //   path and the RHI shader path must see the same face camera.
    {
        gX_Matrix faceView = gX_Matrix_LookAt(faceCam->position, faceCam->target, faceCam->up);
        gX_Matrix faceProjection = gX_Matrix_Perspective(faceCam->fovy * gX_DEG2RAD,
            1.0f, 0.01f, 1000.0f);
        gX_RHI_Matrix_SetModelview(faceView);
        gX_RHI_Matrix_SetProjection(faceProjection);
        gX_GL_Matrix_SetModelView(faceView);
        gX_GL_Matrix_SetProjection(faceProjection);
        gX_RHI_Shader_Use(cgfx->shader.shader.id);
        if (cgfx->shader.locations.matViewLoc >= 0)
            gX_RHI_UniformMatrix(cgfx->shader.locations.matViewLoc, 1, 1, &faceView.m0);
        if (cgfx->shader.locations.matProjectionLoc >= 0)
            gX_RHI_UniformMatrix(cgfx->shader.locations.matProjectionLoc, 1, 1, &faceProjection.m0);
        if (cgfx->shader.locations.viewPosLoc >= 0)
            gX_Shader_SetValue_V3(cgfx->shader.shader,
                cgfx->shader.locations.viewPosLoc, faceCam->position);
    }

    gX_Camera3D mainCam = camera.camera3D;
    gX_I1 frustumCulling = cgfx->state.ENABLE_FRUSTUM_CULLING;
    camera.camera3D = *faceCam;
    cgfx->state.ENABLE_FRUSTUM_CULLING = false;
    GX_HEAP_FOREACH(cgfx->models, gX_RendererGFX_Model, m) {
        if (!m->loaded) continue;
        if (m->params.enableReflections || m->params.planarReflection) continue;
        g_cubemap_hasLocal = 1;
        gX_RendererGFX_Model_Draw(m, cgfx, false);
    }
    cgfx->state.ENABLE_FRUSTUM_CULLING = frustumCulling;
    camera.camera3D = mainCam;
    gX_Batch_Flush();

#if defined(GX_PROBE_COVERAGE_EXPERIMENT)
    if (coverage)
    {
        gfx_probe_coverage_dump(faceCam, faceIndex, coverage);
        gfx_probe_coverage_uniform(cgfx->shader.shader, 0);
        gX_Drawing_BeginBlend(0);
    }
#endif
    gX_Drawing_End3D();
    //opencode: leave raylib's matrix state aligned with the main camera; the
    //   following opaque pass still uses raylib_Mesh_Draw for model uploads.
    {
        gX_Matrix mainView = gX_Matrix_LookAt(camera.camera3D.position,
            camera.camera3D.target, camera.camera3D.up);
        gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
        gX_Matrix mainProjection = gX_Matrix_Perspective(
            camera.camera3D.fovy * gX_DEG2RAD, aspect,
            camera.frustum.near, camera.frustum.far);
        gX_GL_Matrix_SetModelView(mainView);
        gX_GL_Matrix_SetProjection(mainProjection);
    }
}

//opencode: point light shadow cubemap depth pass (6 faces, raw OpenGL)
static gX_I0 gX_RendererGFX_PointShadow_Draw(gX_RendererGFX *gfx, gX_RendererGFX_Light *light)
{
    if (!light->castShadows || light->shadowFboID == 0) return;
    gX_TraceLog(gX_LOG_TRACE2, "SHADOW: point pass at (%.1f,%.1f,%.1f) size=%u",
        (gX_F64)light->pos.x, (gX_F64)light->pos.y, (gX_F64)light->pos.z,
        light->shadowSize);
    if (!gfx->pointDepth.shader.loaded || gfx->pointDepth.shader.shader.id == 0) return;

    gX_U32 prevFbo = gX_RHI_GetDrawFBO();
    gX_RHI_UnbindFBO();

    // Save OpenGL state that we will modify
    gX_GL_State_Push();

    gX_RHI_BindFBO(light->shadowFboID);
    gX_RHI_Viewport(0, 0, light->shadowSize, light->shadowSize);

    gX_RHI_Enable(gX_RHI_DEPTH_TEST);
    gX_RHI_Depth_Func(gX_RHI_LESS);
    gX_RHI_Disable(gX_RHI_CULL_FACE); // render both sides for the shadow cube

    //opencode: the light's layer base in the shared 2D array — layer = lightIdx*6+face
    gX_U32 lightIdx = (gX_U32)(light - (gX_RendererGFX_Light*)gfx->lights.data);
    if (lightIdx >= gX_GFX_MAX_POINT_SHADOW_LIGHTS) lightIdx = gX_GFX_MAX_POINT_SHADOW_LIGHTS - 1;

    for (int face = 0; face < 6; face++)
    {
        //opencode: attach the (lightIdx*6+face) array layer as the depth target
        gX_RHI_FBO_AttachLayer(light->shadowDepth.id, gX_RHI_DEPTH_ATTACHMENT, 0,
            lightIdx * 6u + (gX_U32)face);

        gX_RHI_Clear(gX_RHI_DEPTH_BUFFER_BIT);

        //opencode: activate the point depth shader BEFORE uniform uploads —
        //   glUniform* targets the CURRENT program; with none active the
        //   uploads fail with GL_INVALID_OPERATION (shadows stayed empty)
        gX_RHI_Shader_Use(gfx->pointDepth.shader.shader.id);
        if (gfx->pointDepth.lightSpaceLoc >= 0)
            gX_Shader_SetValue_Matrix(gfx->pointDepth.shader.shader, gfx->pointDepth.lightSpaceLoc, light->shadowViews[face]);
        if (gfx->pointDepth.lightPosLoc >= 0)
            gX_Shader_SetValue_V3(gfx->pointDepth.shader.shader, gfx->pointDepth.lightPosLoc, light->pos);
        if (gfx->pointDepth.farPlaneLoc >= 0)
            gX_Shader_SetValue_Float(gfx->pointDepth.shader.shader, gfx->pointDepth.farPlaneLoc, light->shadowFar);

        GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, mp) {
            if (!mp->loaded) continue;
            gX_Model md2 = mp->model;
            gX_Model_AttachShader(&md2, gfx->pointDepth.shader.shader);
            //opencode: include rotation in SRT matrix including rotation
            gX_Matrix tmat = gX_Matrix_Translate(mp->params.pos.x, mp->params.pos.y, mp->params.pos.z);
            gX_Matrix rmat = gX_Matrix_RotateXYZ((gX_F32v3){
                mp->params.rotation.x * gX_DEG2RAD,
                mp->params.rotation.y * gX_DEG2RAD,
                mp->params.rotation.z * gX_DEG2RAD
            });
            gX_Matrix smat = gX_Matrix_Scale(mp->params.scale.x, mp->params.scale.y, mp->params.scale.z);
            gX_Matrix trsPoint = gX_Matrix_Multiply(smat, rmat);  // S·R
            trsPoint = gX_Matrix_Multiply(trsPoint, tmat);         // (S·R)·T
            //opencode: skip casters whose bounds contain the light — an
            //   enclosing caster (the lamp bulb around a point light is the
            //   common case) blocks every direction and blackens the whole
            //   scene. Test in the model's local space via the inverse TRS.
            if (mp->aabbValid) {
                gX_Matrix invTrs = gX_Matrix_Invert(trsPoint);
                gX_F32v3 lightLocal = gX_F32v3_Transform(light->pos, invTrs);
                if (lightLocal.x >= mp->aabbMin[0] && lightLocal.x <= mp->aabbMax[0]
                    && lightLocal.y >= mp->aabbMin[1] && lightLocal.y <= mp->aabbMax[1]
                    && lightLocal.z >= mp->aabbMin[2] && lightLocal.z <= mp->aabbMax[2]) {
                    continue;
                }
            }
            //opencode: upload matModel to point depth shader — same fix as sun shadow
            //   (matModel was left as zero matrix, causing empty cubemap depth → no point shadows)
            if (gfx->pointDepth.matModelLoc >= 0)
                gX_Shader_SetValue_Matrix(gfx->pointDepth.shader.shader, gfx->pointDepth.matModelLoc, trsPoint);
            gX_Matrix saveT = md2.transform;
            md2.transform = trsPoint;
            gX_Model_Parameters params = { 0 };
            params.scale = 1.0f;
            params.color = gX_WHITE;
            gX_Model_Draw_Parameters(md2, params);
                md2.transform = saveT;
        }
        gX_Batch_Flush();
    }

    //opencode: restore the main shader into the shared materials — the attach
    //   above wrote the pointDepth shader into the models' shared material
    //   structs (gX_Model is a shallow copy), which corrupted downstream draws
    //   (the floor rendered through the depth shader = dark)
    {
        GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, rmp) {
            if (!rmp->loaded || rmp->model.materialCount <= 0) continue;
            gX_Model_AttachShader(&rmp->model, gfx->shader.shader);
        }
    }

    // Restore state
    gX_RHI_UnbindFBO();
    gX_RHI_BindFBO(prevFbo);
    gX_GL_State_Pop();
}

//opencode: v0.3-M4 — SSGI gather pass (experimental, GX_SSGI=1)
//ai: SSGI shares the authored normal-separation bias, with no preset/value changes.
gX_I0 gX_RendererGFX_SSGI_Draw(gX_RendererGFX *gfx)
{
    if (!gfx->ssao.useSSGI) return;
    if (!gfx->ssao.ssgiShader.loaded || !gfx->ssao.ssgiMap.wrapper.loadingState.loaded) return;
    if (!gfx->bloom.hdrScene.wrapper.loadingState.loaded) return;

    GX_PROFILE_ZONE_BEGIN("gfx:ssgi");

    gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix proj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect,
        camera.frustum.near, camera.frustum.far);
    gX_Matrix invProj = gX_Matrix_Invert(proj);
    gX_I32 gw = gfx->ssao.ssgiMap.wrapper.width;
    gX_I32 gh = gfx->ssao.ssgiMap.wrapper.height;

    if (gfx->ssao.ssgiSceneColorLoc < 0)
        gfx->ssao.ssgiSceneColorLoc = gX_Shader_GetLocation(gfx->ssao.ssgiShader.shader, "sceneColor");

    gX_Drawing_BeginTexture(gfx->ssao.ssgiMap.wrapper.rt);
        gX_Drawing_ClearBG((gX_Color){0, 0, 0, 255});
        gX_Shader giS = gfx->ssao.ssgiShader.shader;
        gX_Drawing_BeginShader(giS);
        gX_Shader_SetValue_TextureSlot(giS, gfx->ssao.ssgiSceneColorLoc,
            gfx->bloom.hdrScene.wrapper.rt.texture, gX_GFX_TEXUNIT_SCENE);
        gX_Shader_SetValue_TextureSlot(giS, gfx->ssao.ssgiDepthLoc,
            gfx->bloom.hdrScene.wrapper.rt.depth, gX_GFX_TEXUNIT_DEPTH);
        gX_Shader_SetValue_Matrix(giS, gfx->ssao.ssgiInvProjLoc, invProj);
        gX_Shader_SetValue_Matrix(giS, gfx->ssao.ssgiProjLoc, proj);
        gX_Shader_SetValue_V2(giS, gfx->ssao.ssgiResLoc, (gX_F32v2){(gX_F32)gw, (gX_F32)gh});
        gX_Shader_SetValue_Float(giS, gfx->ssao.ssgiRadiusLoc, gfx->ssao.radius * 2.0f);
        if (gfx->ssao.ssgiBiasLoc >= 0)
            gX_Shader_SetValue_Float(giS, gfx->ssao.ssgiBiasLoc, gfx->ssao.bias);
        if (gfx->ssao.ssgiThicknessLoc >= 0)
            gX_Shader_SetValue_Float(giS, gfx->ssao.ssgiThicknessLoc, gfx->ssao.thickness);
        gX_Shader_SetValue_Float(giS, gfx->ssao.ssgiIntensityLoc, gfx->ssao.ssgiIntensity);
        //opencode: v0.3.4-fix - ensure the gather program is current before
        //   uniform uploads / draw (intervening batch activity can leave a
        //   different program bound; then uploads + rasterization no-op)
        gX_RHI_Shader_Use((gX_U32)giS.id);
        //opencode: Step 3 — previous frame's accumulated GI for multibounce
        //   (parity pair shared with the accum write below)
        if (gfx->ssao.ssgiPrevLoc >= 0
            && gfx->ssao.ssgiAccumA.wrapper.loadingState.loaded
            && gfx->ssao.ssgiAccumB.wrapper.loadingState.loaded) {
            gX_RendererGFX_RenderTexture *prevAcc =
                gX_RendererGFX_TemporalHistory_CurrentTarget(&gfx->ssao.ssgiTemporal,
                    &gfx->ssao.ssgiAccumA, &gfx->ssao.ssgiAccumB);
            gX_Shader_SetValue_TextureSlot(giS, gfx->ssao.ssgiPrevLoc,
                prevAcc->wrapper.rt.texture, gX_GFX_TEXUNIT_SSGI_PREV);
        }
        if (gfx->ssao.ssgiBounceLoc >= 0)
            gX_Shader_SetValue_Float(giS, gfx->ssao.ssgiBounceLoc, gfx->ssao.ssgiBounceGain);
        //opencode: oracle (Paper 1) - temporal gather rays. The v0.3-M4
        //   accum pass blends 80/20 history every frame, so static taps
        //   converge TO the gather noise instead of averaging it out (the
        //   A1/B1 checker hunt: SSGI cells halved only once rotation runs).
        //   Default ON (frameCount-seeded: bit-stable across runs, converging
        //   within a run); GX_T_ORACLESEED=0 forces static for audits.
        //   SSR/GTAO/PCF keep their own default-off hooks (no temporal
        //   consumer wired there).
        {
            const gX_I8 *seedHook = getenv("GX_T_ORACLESEED");
            gX_I1 seedOn = (seedHook && seedHook[0] == '0') ? 0 : 1;
            if (gfx->ssao.ssgiSeedLoc >= 0)
                gX_Shader_SetValue_Float(giS, gfx->ssao.ssgiSeedLoc,
                    (gX_F32)maindata.frameCount);
            if (gfx->ssao.ssgiSeedActiveLoc >= 0)
                gX_Shader_SetValue_Int(giS, gfx->ssao.ssgiSeedActiveLoc, seedOn);
        }

        gX_Drawing_DrawScreenQuad(0, 0,
            (gX_F32)gw, (gX_F32)gh);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();

    //opencode: Phase2a — depth-guided spatial blur into ssgiBlurMap
    if (gfx->ssao.ssgiBlurShader.loaded && gfx->ssao.ssgiBlurMap.wrapper.loadingState.loaded) {
        gX_Drawing_BeginTexture(gfx->ssao.ssgiBlurMap.wrapper.rt);
            gX_Drawing_ClearBG((gX_Color){0, 0, 0, 255});
            gX_Shader blS = gfx->ssao.ssgiBlurShader.shader;
            gX_Drawing_BeginShader(blS);
            gX_Shader_SetValue_TextureSlot(blS, gfx->ssao.ssgiBlurMapLoc,
                gfx->ssao.ssgiMap.wrapper.rt.texture, gX_GFX_TEXUNIT_SSGI_RAW);
            gX_Shader_SetValue_TextureSlot(blS, gfx->ssao.ssgiBlurDepthLoc,
                gfx->bloom.hdrScene.wrapper.rt.depth, gX_GFX_TEXUNIT_DEPTH);
            gX_Shader_SetValue_V2(blS, gfx->ssao.ssgiBlurResLoc,
                (gX_F32v2){(gX_F32)gfx->ssao.ssgiBlurMap.wrapper.width,
                           (gX_F32)gfx->ssao.ssgiBlurMap.wrapper.height});
            //opencode: v0.3.5-polish - linearized edge-stops need the real
            //   depth range (raw buffer values stop far surfaces too late)
            if (gfx->ssao.ssgiBlurNearLoc < 0)
                gfx->ssao.ssgiBlurNearLoc = gX_Shader_GetLocation(blS, "cameraNear");
            if (gfx->ssao.ssgiBlurFarLoc < 0)
                gfx->ssao.ssgiBlurFarLoc = gX_Shader_GetLocation(blS, "cameraFar");
            if (gfx->ssao.ssgiBlurNearLoc >= 0)
                gX_Shader_SetValue_Float(blS, gfx->ssao.ssgiBlurNearLoc,
                    camera.frustum.near);
            if (gfx->ssao.ssgiBlurFarLoc >= 0)
                gX_Shader_SetValue_Float(blS, gfx->ssao.ssgiBlurFarLoc,
                    camera.frustum.far);
            gX_Drawing_DrawScreenQuad(0, 0,
                (gX_F32)gfx->ssao.ssgiBlurMap.wrapper.width,
                (gX_F32)gfx->ssao.ssgiBlurMap.wrapper.height);
            gX_Drawing_EndShader();
        gX_Drawing_EndTexture();
    }

    //opencode: Phase2b — temporal accumulation (velocity reproject + clamp)
    if (gfx->ssao.ssgiAccumShader.loaded
        && gfx->ssao.ssgiAccumA.wrapper.loadingState.loaded
        && gfx->ssao.ssgiAccumB.wrapper.loadingState.loaded) {
        gX_RendererGFX_TemporalHistoryTargets temporalTargets;
        if (!gX_RendererGFX_TemporalHistory_BeginPass(&gfx->ssao.ssgiTemporal,
                &gfx->ssao.ssgiAccumA, &gfx->ssao.ssgiAccumB, &temporalTargets)) {
            gfx->ssao.ssgiFinalTex = gfx->ssao.ssgiBlurMap.wrapper.rt.texture;
            GX_PROFILE_ZONE_END("gfx:ssgi");
            return;
        }
        gX_RenderTexture *dst = &temporalTargets.write->wrapper.rt;
        gX_RenderTexture *hist = &temporalTargets.read->wrapper.rt;
        gX_Drawing_BeginTexture(*dst);
            gX_Shader acS = gfx->ssao.ssgiAccumShader.shader;
            gX_Drawing_BeginShader(acS);
            gX_Shader_SetValue_TextureSlot(acS, gfx->ssao.ssgiAccCurLoc,
                gfx->ssao.ssgiBlurMap.wrapper.rt.texture, gX_GFX_TEXUNIT_SSGI_BLUR);
            gX_Shader_SetValue_TextureSlot(acS, gfx->ssao.ssgiAccHistLoc,
                hist->texture, gX_GFX_TEXUNIT_SSGI_HIST);
            if (gfx->ssao.ssgiAccHistoryValidLoc >= 0)
                gX_Shader_SetValue_Int(acS, gfx->ssao.ssgiAccHistoryValidLoc,
                    gfx->ssao.ssgiTemporal.historyValid ? 1 : 0);
            if (gfx->velocity.map.wrapper.loadingState.loaded && gfx->velocity.hasPrevVP) {
                gX_Shader_SetValue_TextureSlot(acS, gfx->ssao.ssgiAccVelLoc,
                    gfx->velocity.map.wrapper.rt.texture, gX_GFX_TEXUNIT_VELOCITY);
                gX_Shader_SetValue_Int(acS, gfx->ssao.ssgiAccHasVelLoc, 1);
            } else {
                gX_Shader_SetValue_Int(acS, gfx->ssao.ssgiAccHasVelLoc, 0);
            }
            gX_Shader_SetValue_V2(acS, gfx->ssao.ssgiAccResLoc,
                (gX_F32v2){(gX_F32)dst->texture.width, (gX_F32)dst->texture.height});
            gX_Drawing_DrawScreenQuad(0, 0,
                (gX_F32)dst->texture.width, (gX_F32)dst->texture.height);
        gX_Drawing_EndShader();
        gX_Drawing_EndTexture();
        gX_RendererGFX_TemporalHistory_CommitPass(&gfx->ssao.ssgiTemporal);
        gfx->ssao.ssgiFinalTex = dst->texture;
    }
    else
        gfx->ssao.ssgiFinalTex = gfx->ssao.ssgiBlurMap.wrapper.rt.texture;

    GX_PROFILE_ZONE_END("gfx:ssgi");
}

//opencode: SSAO compute pass — reads scene depth, reconstructs view-space
//   normals from depth gradient, samples hemisphere points weighted by
//   per-pixel noise rotation, writes occlusion factor to ssaoMap. Followed
//   by a 4x4 edge-aware blur that uses depth to avoid bleeding across
//   silhouette edges.
//ai: GTAO normal-separation bias uses the existing authored value, with no preset changes.
//ai: upload temporal validity so the first pass after reset uses only its spatial input.
//ai: GTAO accumulation stores current depth and validates each reprojected history tap.
static gX_I0 gX_RendererGFX_SSAO_Draw(gX_RendererGFX *gfx)
{
    if (!gfx->ssao.blurShader.loaded) return;
    if (!gfx->ssao.map.wrapper.loadingState.loaded || !gfx->ssao.blurMap.wrapper.loadingState.loaded) return;
    if (!gfx->bloom.hdrScene.wrapper.loadingState.loaded) return; //opencode: need scene depth

    //opencode: compute view + projection matrices
    gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix proj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect,
        camera.frustum.near, camera.frustum.far);
    //opencode: invProj for view-space reconstruction from NDC.
    //   CRITICAL: must be inverse(projection) NOT inverse(view*projection).
    //   The AO shaders treat invProjection as invert(projection); passing
    //   invert(view*proj) gave world-space positions, making samples project
    //   to wrong screen positions → random noise → AO invisible.
    gX_Matrix invProj = gX_Matrix_Invert(proj);

    //opencode: v0.3-M2 — GTAO mode: horizon-scan AO into ssao.map, then the
    //   shared edge-aware blur below runs unchanged.
    if (gfx->ssao.useGTAO && gfx->ssao.gtaoShader.loaded) {
        gX_Matrix proj2 = proj; // same perspective as legacy path
        gX_Drawing_BeginTexture(gfx->ssao.map.wrapper.rt);
            gX_Drawing_ClearBG((gX_Color){255, 255, 255, 255});
            gX_Shader gtaoS = gfx->ssao.gtaoShader.shader;
            gX_Drawing_BeginShader(gtaoS);
            gX_Shader_SetValue_TextureSlot(gtaoS, gfx->ssao.gtaoDepthMapLoc,
                gfx->bloom.hdrScene.wrapper.rt.depth, gX_GFX_TEXUNIT_DEPTH);
            gX_Shader_SetValue_Matrix(gtaoS, gfx->ssao.gtaoInvProjLoc, invProj);
            gX_Shader_SetValue_Matrix(gtaoS, gfx->ssao.gtaoProjLoc, proj2);
            gX_Shader_SetValue_V2(gtaoS, gfx->ssao.gtaoResolutionLoc,
                (gX_F32v2){(gX_F32)gfx->ssao.map.wrapper.width,
                           (gX_F32)gfx->ssao.map.wrapper.height});
            gX_Shader_SetValue_Float(gtaoS, gfx->ssao.gtaoRadiusLoc, gfx->ssao.radius);
            if (gfx->ssao.gtaoBiasLoc >= 0)
                gX_Shader_SetValue_Float(gtaoS, gfx->ssao.gtaoBiasLoc, gfx->ssao.bias);
            if (gfx->ssao.gtaoThicknessLoc >= 0)
                gX_Shader_SetValue_Float(gtaoS, gfx->ssao.gtaoThicknessLoc, gfx->ssao.thickness);
            gX_Shader_SetValue_Float(gtaoS, gfx->ssao.gtaoStrengthLoc, gfx->ssao.strength);
            //opencode: oracle (Paper 1) - temporal AO rays. Default ON
            //   (frameCount-seeded, bit-stable; the accum below converges
            //   the rotation instead of flickering). GX_T_ORACLESEED=0
            //   forces static taps for audits (mirrors SSGI semantics;
            //   GTAO previously defaulted off with no temporal consumer).
            {
                const gX_I8 *seedHook = getenv("GX_T_ORACLESEED");
                gX_I1 seedOn = (seedHook && seedHook[0] == '0') ? 0 : 1;
                if (gfx->ssao.gtaoSeedLoc >= 0)
                    gX_Shader_SetValue_Float(gtaoS, gfx->ssao.gtaoSeedLoc,
                        (gX_F32)maindata.frameCount);
                if (gfx->ssao.gtaoSeedActiveLoc >= 0)
                    gX_Shader_SetValue_Int(gtaoS, gfx->ssao.gtaoSeedActiveLoc, seedOn);
            }
            gX_Drawing_DrawScreenQuad(0, 0,
                (gX_F32)gfx->ssao.map.wrapper.width, (gX_F32)gfx->ssao.map.wrapper.height);
            gX_Drawing_EndShader();
        gX_Drawing_EndTexture();
    }
    else
    //opencode: Step 3 — legacy kernel fallback deleted (A/B vs GTAO:
    //   pixel-identical, frac=0.0001; gfx_ssao.frag removed). Neutral white
    //   = no occlusion, the correct degraded state when GTAO is unavailable.
    {
        gX_Drawing_BeginTexture(gfx->ssao.map.wrapper.rt);
            gX_Drawing_ClearBG((gX_Color){255, 255, 255, 255});
        gX_Drawing_EndTexture();
    }

    //opencode: 2) 4x4 edge-aware blur
    gX_Drawing_BeginTexture(gfx->ssao.blurMap.wrapper.rt);
        gX_Drawing_ClearBG((gX_Color){255, 255, 255, 255}); // white = no occlusion (avoids flash)
        gX_Shader blurS = gfx->ssao.blurShader.shader;
        gX_Drawing_BeginShader(blurS);
        //opencode: bind uniforms AFTER BeginShader
        gX_Shader_SetValue_TextureSlot(blurS, gfx->ssao.blurMapLoc,
            gfx->ssao.map.wrapper.rt.texture, gX_GFX_TEXUNIT_AUX);
        gX_Shader_SetValue_TextureSlot(blurS, gfx->ssao.blurDepthMapLoc,
            gfx->bloom.hdrScene.wrapper.rt.depth, gX_GFX_TEXUNIT_DEPTH);
        gX_Shader_SetValue_V2(blurS, gfx->ssao.blurResolutionLoc,
            (gX_F32v2){(gX_F32)gfx->ssao.blurMap.wrapper.width,
                       (gX_F32)gfx->ssao.blurMap.wrapper.height});
        gX_Shader_SetValue_Float(blurS, gfx->ssao.blurEdgeThresholdLoc, gX_GFX_SSAO_EDGE_THRESHOLD);
            gX_Drawing_DrawScreenQuad(0, 0,
                (gX_F32)gfx->ssao.blurMap.wrapper.width, (gX_F32)gfx->ssao.blurMap.wrapper.height);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();

    //opencode: 3) GTAO temporal accumulation (SSGI-Phase2 mirror): blend
    //   the denoised current with reprojected history (parity pair).
    //   Converges the default-ON oracle rotation; falls back to blurMap.
    if (gfx->ssao.gtaoAccumShader.loaded
        && gfx->ssao.gtaoAccumA.wrapper.loadingState.loaded
        && gfx->ssao.gtaoAccumB.wrapper.loadingState.loaded) {
        gX_RendererGFX_TemporalHistoryTargets temporalTargets;
        if (!gX_RendererGFX_TemporalHistory_BeginPass(&gfx->ssao.gtaoTemporal,
                &gfx->ssao.gtaoAccumA, &gfx->ssao.gtaoAccumB, &temporalTargets)) {
            gfx->ssao.gtaoFinalTex = gfx->ssao.blurMap.wrapper.rt.texture;
            return;
        }
        gX_RenderTexture *dst = &temporalTargets.write->wrapper.rt;
        gX_RenderTexture *hist = &temporalTargets.read->wrapper.rt;
        gX_Drawing_BeginTexture(*dst);
            gX_Shader acS = gfx->ssao.gtaoAccumShader.shader;
            gX_Drawing_BeginShader(acS);
            gX_RHI_Shader_Use((gX_U32)acS.id);
            gX_Shader_SetValue_TextureSlot(acS, gfx->ssao.gtaoAccCurLoc,
                gfx->ssao.blurMap.wrapper.rt.texture, gX_GFX_TEXUNIT_GTAO_ACCUM_CUR);
            gX_Shader_SetValue_TextureSlot(acS, gfx->ssao.gtaoAccHistLoc,
                hist->texture, gX_GFX_TEXUNIT_GTAO_ACCUM_HIST);
            gX_Shader_SetValue_TextureSlot(acS, gfx->ssao.gtaoAccDepthMapLoc,
                gfx->bloom.hdrScene.wrapper.rt.depth, gX_GFX_TEXUNIT_DEPTH);
            if (gfx->ssao.gtaoAccHistoryValidLoc >= 0)
                gX_Shader_SetValue_Int(acS, gfx->ssao.gtaoAccHistoryValidLoc,
                    gfx->ssao.gtaoTemporal.historyValid ? 1 : 0);
            if (gfx->velocity.map.wrapper.loadingState.loaded && gfx->velocity.hasPrevVP) {
                gX_Shader_SetValue_TextureSlot(acS, gfx->ssao.gtaoAccVelLoc,
                    gfx->velocity.map.wrapper.rt.texture, gX_GFX_TEXUNIT_VELOCITY);
                gX_Shader_SetValue_Int(acS, gfx->ssao.gtaoAccHasVelLoc, 1);
            } else {
                gX_Shader_SetValue_Int(acS, gfx->ssao.gtaoAccHasVelLoc, 0);
            }
            if (gfx->ssao.gtaoAccCameraMovedLoc >= 0)
                gX_Shader_SetValue_Int(acS, gfx->ssao.gtaoAccCameraMovedLoc,
                    gfx->velocity.map.wrapper.loadingState.loaded
                        && gfx->velocity.hasPrevVP && gfx->velocity.cameraMoved ? 1 : 0);
            gX_Shader_SetValue_V2(acS, gfx->ssao.gtaoAccResLoc,
                (gX_F32v2){(gX_F32)dst->texture.width, (gX_F32)dst->texture.height});
            gX_Drawing_DrawScreenQuad(0, 0,
                (gX_F32)dst->texture.width, (gX_F32)dst->texture.height);
            gX_Drawing_EndShader();
        gX_Drawing_EndTexture();
        gX_RendererGFX_TemporalHistory_CommitPass(&gfx->ssao.gtaoTemporal);
        gfx->ssao.gtaoFinalTex = dst->texture;
    }
    else
        gfx->ssao.gtaoFinalTex = gfx->ssao.blurMap.wrapper.rt.texture;
}

gX_I0 gX_RendererGFX_Draw_Shadow(gX_RendererGFX *gfx)
{

    //opencode: shadow depth pass — skip entirely when shadows disabled
    if (gfx->state.ENABLE_SHADOWS)
    {
        //opencode: diagnostic — check that depth shader and shadow map are ready
        if (gfx->depth.shader.id == 0 || !gfx->depth.loaded) {
            gX_TraceLog(gX_LOG_WARNING, "gfx:shadow: depth shader not loaded (id=%u loaded=%d)",
                gfx->depth.shader.id, gfx->depth.loaded);
            return;
        }
        if (!gfx->sun.camera.map || gfx->sun.camera.map->depth.id == 0) {
            gX_TraceLog(gX_LOG_WARNING, "gfx:shadow: shadow map RT not ready");
            return;
        }
        //opencode: CSM-aware shadow depth pass. Cascade 0 renders into the
        //   regular sun shadow map (the shader samples shadowMap for layer 0);
        //   cascades 1..N-1 render into 2D-array layers 1..N-1.
        if (gfx->csm.cascadeCount > 1 && gfx->csm.fboId > 0 && gfx->csm.arrayTexId > 0)
        {
            GX_PROFILE_ZONE_BEGIN("gfx:csm_shadow");
            //opencode: Step-3-bis — scoped save/restore (NOT the MSAA class
            //   of bug): the 6 camera/rt saves below are all restored after
            //   the loop in this same block, with no early exits between —
            //   no cross-pass aliasing. Keep save/restore paired if touched.
            gX_F32 savedSize = gfx->sun.camera.size;
            gX_F32 savedNear = gfx->sun.camera.near;
            gX_F32 savedFar = gfx->sun.camera.far;
            gX_Matrix savedVP = gfx->sun.camera.viewProjection;
            gX_RenderTexture *savedRT = gfx->sun.camera.map;
            gX_RenderTexture *savedFirstRT = gfx->first.rt;

            gX_I32 count = gfx->csm.cascadeCount;
            if (count > 4) count = 4;

            gfx->first.rt = gfx->sun.camera.map;
            gfx->sun.camera.size = gfx->csm.frustumSizes[0];
            gfx->sun.camera.near = 0.1f;
            gfx->sun.camera.far = gfx->csm.frustumSizes[0] * 4.0f;
            gX_RendererGFX_Light_Update(gfx, &gfx->sun);
            gX_RendererGFX_Render_Draw(&gfx->first, gfx);
            gfx->csm.vps[0] = gfx->sun.camera.viewProjection;

            //opencode: cascades 1..N-1 render into array layers via the shared FBO
            for (gX_I32 ci = 1; ci < count; ci++)
            {
                gfx->sun.camera.size = gfx->csm.frustumSizes[ci];
                gfx->sun.camera.near = 0.1f;
                gfx->sun.camera.far = gfx->csm.frustumSizes[ci] * 4.0f;
                gX_RendererGFX_Light_Update(gfx, &gfx->sun);

                gfx->sun.camera.map = &gfx->csm.map.wrapper.rt;
                gfx->first.rt = gfx->sun.camera.map;
                gX_RHI_BindFBO(gfx->csm.fboId);
                gX_RHI_FBO_AttachLayer(gfx->csm.arrayTexId, gX_RHI_DEPTH_ATTACHMENT, 0, ci);
                gX_RendererGFX_Render_Draw(&gfx->first, gfx);

                gfx->csm.vps[ci] = gfx->sun.camera.viewProjection;
            }

            gfx->sun.camera.size = savedSize;
            gfx->sun.camera.near = savedNear;
            gfx->sun.camera.far = savedFar;
            gfx->sun.camera.viewProjection = savedVP;
            gfx->sun.camera.map = savedRT;
            gfx->first.rt = savedFirstRT;

            GX_PROFILE_ZONE_END("gfx:csm_shadow");
        }
        else
        {
            GX_PROFILE_ZONE_BEGIN("gfx:shadow");
            gX_RendererGFX_Render_Draw(&gfx->first, gfx); // sun shadow map (single cascade)
            GX_PROFILE_ZONE_END("gfx:shadow");
        }
    }

}

//opencode: reproduce the material-map portion of the main model draw for the raw planar-fill path.
static gX_I0 gfx_planar_capture_mesh_maps(gX_RendererGFX *gfx,
    gX_RendererGFX_Model *model, gX_Material *material, gX_I32 meshIndex)
{
    if (!gfx || !model) return;
    gX_Texture maps[gX_GFX_MAX_MATERIAL_MAPS] = {{0}};
    if (material && material->maps) {
        for (gX_I32 mapIndex = 0; mapIndex < gX_GFX_MAX_MATERIAL_MAPS; mapIndex++)
            maps[mapIndex] = material->maps[mapIndex].texture;
    }
    for (gX_I32 textureSlot = 0; textureSlot < gX_GFX_MODEL_TEXTURE_SLOTS; textureSlot++) {
        gX_I32 purpose = model->texturePurpose[textureSlot];
        gX_I32 textureIndex = model->textureID[textureSlot];
        if (purpose < 0 || purpose >= gX_GFX_MAX_MATERIAL_MAPS || textureIndex < 0
            || (gX_U32)textureIndex >= gfx->textures.amount
            || !gfx->textures.meta[textureIndex].USED) continue;
        gX_RendererGFX_Texture *texture = gX_Heap_SeekPtr_Index(&gfx->textures,
            (gX_U32)textureIndex);
        if (texture) maps[purpose] = texture->wrapper.texture;
    }
    if (model->params.injectedNormalMapID >= 0
        && (gX_U32)model->params.injectedNormalMapID < gfx->textures.amount
        && gfx->textures.meta[model->params.injectedNormalMapID].USED) {
        gX_RendererGFX_Texture *texture = gX_Heap_SeekPtr_Index(&gfx->textures,
            (gX_U32)model->params.injectedNormalMapID);
        if (texture) maps[gX_MATERIAL_MAP_NORMAL] = texture->wrapper.texture;
    }
    if (meshIndex >= 0 && meshIndex < gX_GFX_MODEL_MESH_MASK) {
        gX_I32 mask = model->params.materialMeshMask[meshIndex];
        if ((mask & gX_GFX_MMASK_ALBEDO_MAP)
            && model->params.effectiveAlbedoMap[meshIndex] > 0)
            maps[gX_MATERIAL_MAP_ALBEDO].id =
                (gX_U32)model->params.effectiveAlbedoMap[meshIndex];
        if ((mask & gX_GFX_MMASK_EMISSIVE_MAP)
            && model->params.effectiveEmissiveMap[meshIndex] > 0)
            maps[gX_MATERIAL_MAP_EMISSION].id =
                (gX_U32)model->params.effectiveEmissiveMap[meshIndex];
    }
    gX_I1 hasAlbedoMap = model->params.hasAlbedoMap
        && maps[gX_MATERIAL_MAP_ALBEDO].id > 0;
    gX_I1 hasMetalnessMap = model->params.hasMetalnessMap
        && maps[gX_MATERIAL_MAP_METALNESS].id > 0;
    gX_I1 hasNormalMap = model->params.hasNormalMap
        && maps[gX_MATERIAL_MAP_NORMAL].id > 0;
    gX_I1 hasHeightMap = model->params.hasHeightMap
        && maps[gX_MATERIAL_MAP_HEIGHT].id > 0;
    gX_I1 hasEmissiveMap = model->params.hasEmissiveMap
        && maps[gX_MATERIAL_MAP_EMISSION].id > 0;
    gX_I1 hasRoughnessMap = model->params.hasRoughnessMap
        && maps[gX_MATERIAL_MAP_ROUGHNESS].id > 0;
    gX_I1 hasAoMap = model->params.hasAoMap
        && maps[gX_MATERIAL_MAP_OCCLUSION].id > 0;
    gX_RendererGFX_ShaderLocs *locations = &gfx->shader.locations;
    gX_Shader shader = gfx->shader.shader;
    gX_Shader_SetValue_Int(shader, locations->hasAlbedoMapLoc, hasAlbedoMap);
    gX_Shader_SetValue_Int(shader, locations->hasMetalnessMapLoc, hasMetalnessMap);
    gX_Shader_SetValue_Int(shader, locations->hasNormalMapLoc, hasNormalMap);
    gX_Shader_SetValue_Int(shader, locations->hasHeightMapLoc, hasHeightMap);
    gX_Shader_SetValue_Int(shader, locations->hasEmissiveMapLoc, hasEmissiveMap);
    gX_Shader_SetValue_Int(shader, locations->hasRoughnessMapLoc, hasRoughnessMap);
    gX_Shader_SetValue_Int(shader, locations->hasAoMapLoc, hasAoMap);
    if (material && material->shader.locs) {
        for (gX_I32 mapIndex = 0; mapIndex < gX_GFX_MAX_MATERIAL_MAPS; mapIndex++) {
            gX_I32 location = material->shader.locs[15 + mapIndex];
            if (location < 0 || maps[mapIndex].id == 0) continue;
            if (mapIndex == gX_MATERIAL_MAP_CUBEMAP)
                gX_Shader_SetValue_CubemapSlot(shader, location, maps[mapIndex], mapIndex);
            else
                gX_Shader_SetValue_TextureSlot(shader, location, maps[mapIndex], mapIndex);
        }
    }
    if (model->params.hasGlossMap && model->params.glossMapID >= 0
        && (gX_U32)model->params.glossMapID < gfx->textures.amount
        && gfx->textures.meta[model->params.glossMapID].USED) {
        gX_RendererGFX_Texture *texture = gX_Heap_SeekPtr_Index(&gfx->textures,
            (gX_U32)model->params.glossMapID);
        if (texture) {
            gX_Shader_SetValue_Int(shader, locations->hasGlossMapLoc, 1);
            gX_RHI_Tex_BindOnUnit(texture->wrapper.texture.id,
                gX_GFX_TEXUNIT_GLOSS);
            gX_RHI_Shader_Use(shader.id);
            gX_RHI_UniformInt(locations->glossMapLoc, gX_GFX_TEXUNIT_GLOSS);
        }
    } else {
        gX_Shader_SetValue_Int(shader, locations->hasGlossMapLoc, 0);
    }
}

gX_I0 gX_RendererGFX_Draw_PointShadows(gX_RendererGFX *gfx)
{

    // Point light shadows: depth cubemap rendering
    if (gfx->state.ENABLE_POINT_SHADOWS)
    {
        GX_HEAP_FOREACH(gfx->lights, gX_RendererGFX_Light, light) {
            if (light->castShadows && !light->sun)
            {
                gX_RendererGFX_PointShadow_Draw(gfx, light);
            }
        }
    }

}

gX_I0 gX_RendererGFX_Draw_Reflections(gX_RendererGFX *gfx)
{

    //opencode: reflection audit — freshness flags reset EVERY frame, outside
    //   the ENABLE_REFLECTIONS gate: disabling reflections mid-run left
    //   filledThisFrame true forever, so the SSR aux path sampled a stale
    //   planar RT as if fresh (same class as the v0.1.25 cubeReady note).
    //   cubeUpdatedThisFrame was declared but never written at all.
    gfx->reflection.filledThisFrame = false;
    gfx->reflection.cubeUpdatedThisFrame = false;
#if defined(GX_PROBE_COVERAGE_EXPERIMENT)
    if (gfx_probe_proxy_split_enabled() && !gfx->state.ENABLE_SSR
        && gfx->state.ENABLE_PROCEDURAL_SKY && gfx->proceduralSky.shader.id > 0)
    {
        if (!gfx->ssr.skyLut.wrapper.loadingState.loaded)
        {
            gfx->ssr.skyLut.wrapper.width = 1024;
            gfx->ssr.skyLut.wrapper.height = 512;
            gX_RendererGFX_RenderTexture_Update(&gfx->ssr.skyLut);
        }
        if (gfx->ssr.skyLut.wrapper.loadingState.loaded)
        {
            gX_RHI_Tex_Param(gfx->ssr.skyLut.wrapper.rt.texture.id,
                gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_WRAP_S, gX_RHI_REPEAT);
            gX_RHI_Tex_Param(gfx->ssr.skyLut.wrapper.rt.texture.id,
                gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_EDGE);
            const gX_I8 *directionHook = getenv("GX_PROBE_PROXY_SKY_DIRECTION");
            gX_I1 directionOnly = directionHook && strcmp((const char *)directionHook, "1") == 0;
            gX_RHI_Shader_Use(gfx->proceduralSky.shader.id);
            gX_RHI_UniformInt(gX_RHI_GetUniformLocation(gfx->proceduralSky.shader.id,
                (const gX_I8 *)"gXProbeSkyDirectionCapture"), directionOnly ? 1 : 0);
            gX_RendererGFX_Sky_DrawTo(gfx, gfx->ssr.skyLut.wrapper.rt, 1);
            gX_RHI_Shader_Use(gfx->proceduralSky.shader.id);
            gX_RHI_UniformInt(gX_RHI_GetUniformLocation(gfx->proceduralSky.shader.id,
                (const gX_I8 *)"gXProbeSkyDirectionCapture"), 0);
        }
    }
#endif
    //opencode: planar capture is independent of environment reflections and SSR.
    gX_I1 planarCaptureEnabled = gfx->state.ENABLE_PLANAR_REFLECTIONS
        || (gfx->state.ENABLE_SSR && gfx->state.ENABLE_SSR_AUXVIEW);
    if (planarCaptureEnabled)
    {
        GX_PROFILE_ZONE_BEGIN("gfx:planar_reflection");
        gfx->reflection.filledThisFrame = false;
        gX_I1 needRefl = false;
        gX_F32 planeDist = -1.0f;
        gX_F32v3 planeNormal = {0, 1, 0};
        //opencode: two-mirror feedback validity runs before plane selection
        //   (camera/size/exposure compare, history clear on change).
        gX_RendererGFX_Mirror_BeginFrame(gfx);
        //opencode: v0.3.4-auxview - --reflectrow forces the mirror fill as an
        //   UNCONSUMED shadow auxiliary: its content feeds SSR miss pixels,
        //   standard.frag never samples it (no model opts in)
        extern gX_MainArguments arguments;
        if (!needRefl && arguments.showcaseReflectRow
            && gfx->state.ENABLE_SSR && gfx->state.ENABLE_SSR_AUXVIEW) {
            needRefl = true;
            planeDist = 0.075f;
            planeNormal.x = 0.0f; planeNormal.y = 1.0f; planeNormal.z = 0.0f;
        }
        //opencode: two-mirror pair — collect tagged planes in tag order when
        //   the feedback RTs are ready. Runs before legacy first-match (the
        //   pair slabs carry planarReflection too); single-plane scenes keep
        //   the legacy path, byte-identical (mirrorPair defaults 0).
        gX_F32 pairDist[2] = {-1.0f, -1.0f};
        gX_F32v3 pairNormal[2] = {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
        gX_I32 pairCount = 0;
        if (!needRefl && gfx->state.ENABLE_PLANAR_REFLECTIONS
            && gX_RendererGFX_Mirror_Active(gfx)) {
            for (gX_I32 tag = 1; tag <= 2; tag++) {
                GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, pmodel) {
                    if (!pmodel->loaded) continue;
                    if (!pmodel->params.planarReflection) continue;
                    if (pmodel->params.mirrorPair != tag) continue;
                    if (!gfx_reflection_plane_world(pmodel,
                            &pairNormal[pairCount], &pairDist[pairCount])) {
                        pairNormal[pairCount].x = 0.0f; pairNormal[pairCount].y = 1.0f; pairNormal[pairCount].z = 0.0f;
                        pairDist[pairCount] = 0.0f;
                    }
                    pairCount++;
                    break;
                }
            }
            if (pairCount == 2) needRefl = true;
            else pairCount = 0;
        }
        GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, rmodel) {
            //opencode: v0.3.7 - reflectionSource slabs also own the mirror
            //   view (showcase platform); their fill serves the aux routing
            //   and stays gated on ENABLE_SSR_AUXVIEW like the reflectrow
            //   force path
            if (rmodel->loaded
                && rmodel->params.mirrorPair == 0
                && ((rmodel->params.planarReflection
                        && gfx->state.ENABLE_PLANAR_REFLECTIONS)
                    || (rmodel->params.reflectionSource
                        && gfx->state.ENABLE_SSR
                        && gfx->state.ENABLE_SSR_AUXVIEW)))
            {
                needRefl = true;
                if (!gfx_reflection_plane_world(rmodel, &planeNormal, &planeDist)) {
                    planeNormal.x = 0.0f; planeNormal.y = 1.0f; planeNormal.z = 0.0f;
                    planeDist = 0.0f;
                }
                break;
            }
        }
        if (needRefl && gfx->reflection.planar.wrapper.loadingState.loaded)
        {
        //opencode: two-mirror plane loop — one render per facing plane.
        //   pairCount==0 keeps the legacy single render (planeDist/Normal
        //   from above); interior indentation is unchanged by the wrap.
        gX_I32 planeRenders = (pairCount == 2) ? 2 : 1;
        for (gX_I32 planeIndex = 0; planeIndex < planeRenders; planeIndex++) {
            if (pairCount == 2) {
                planeDist = pairDist[planeIndex];
                planeNormal = pairNormal[planeIndex];
                gfx->mirror.renderingPlane = planeIndex;
            }
            gX_RenderTexture *planeRT = (pairCount == 2)
                ? gX_RendererGFX_Mirror_PlaneTarget(gfx, planeIndex)
                : &gfx->reflection.planar.wrapper.rt;
            gX_Drawing_BeginTexture(*planeRT);
            gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT | gX_RHI_DEPTH_BUFFER_BIT | gX_RHI_STENCIL_BUFFER_BIT);
            //opencode: audit — clear to the scene bg (sky.color, same as the
            //   main pass ClearBG) instead of gray: empty mirror pixels that
            //   escape the depth mask read as scene, not a brown veil.
            gX_Drawing_ClearBG(gfx->sky.color);
            gX_Drawing_ClearBG(gfx->sky.color);

            // Mirror camera across the plane — general plane form: dot(n,x) = d
            gX_F32v3 camPos  = camera.camera3D.position;
            gX_F32v3 camTgt = camera.camera3D.target;
            gX_F32v3 camUp  = camera.camera3D.up;
            gX_F32v3 n = planeNormal;
            gX_Camera3D mirrorCam = camera.camera3D;
            gX_F32 dPos = gX_F32v3_Dot(camPos, n) - planeDist;
            gX_F32 dTgt = gX_F32v3_Dot(camTgt, n) - planeDist;
            gX_F32 dUp  = gX_F32v3_Dot(camUp, n);
            mirrorCam.position = gX_F32v3_Sub(camPos, gX_F32v3_MulScalar(n, 2.0f * dPos));
            mirrorCam.target   = gX_F32v3_Sub(camTgt, gX_F32v3_MulScalar(n, 2.0f * dTgt));
            mirrorCam.up       = gX_F32v3_Sub(camUp,  gX_F32v3_MulScalar(n, 2.0f * dUp));

            // Compute mirrored view-projection matrix for the shader
            gX_Matrix reflView = gX_Matrix_LookAt(mirrorCam.position, mirrorCam.target, mirrorCam.up);
            gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
            gX_Matrix reflProj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect,
                camera.frustum.near, camera.frustum.far);
            gX_Matrix reflMvp = gX_Matrix_Multiply(reflView, reflProj);
            //opencode: two-mirror — plane 0 keeps the legacy shared mvp for
            //   oracle parity; both planes store per-plane mvps for sampling.
            if (planeIndex == 0) gfx->reflection.mvp = reflMvp;
            gX_RendererGFX_Mirror_StoreMvp(gfx, planeIndex, &reflMvp);
            gfx->reflection.planeDist = planeDist;
            gfx->reflection.planeNormal = planeNormal;
            //opencode: oracle calibration - verbatim mirror matrix + camera +
            //   frustum so an offline CPU oracle can verify the planar chain
            //   without reimplementing gxmath (row-major m0..m15, upload uses
            //   transpose=1 so the shader sees these exact rows). Periodic
            //   (every 30 frames) so the oracle can detect camera drift
            //   between startup and the capture frame.
            {
                static gX_U64 oracleLastDump = 0;
                if (maindata.frameCount - oracleLastDump >= 30) {
                    oracleLastDump = maindata.frameCount;
                    gX_Matrix m = gfx->reflection.mvp;
                    gX_TraceLog(gX_LOG_NOTICE,
                        "ORACLEDUMP f=%llu mvp=%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f "
                        "plane=%.4f n=(%.2f,%.2f,%.2f) "
                        "cam=(%.4f,%.4f,%.4f) tgt=(%.4f,%.4f,%.4f) up=(%.2f,%.2f,%.2f) fovy=%.3f near=%.4f far=%.1f size=%dx%d",
                        (unsigned long long)maindata.frameCount,
                        m.m0, m.m1, m.m2, m.m3, m.m4, m.m5, m.m6, m.m7,
                        m.m8, m.m9, m.m10, m.m11, m.m12, m.m13, m.m14, m.m15,
                        planeDist, planeNormal.x, planeNormal.y, planeNormal.z,
                        camPos.x, camPos.y, camPos.z,
                        camTgt.x, camTgt.y, camTgt.z,
                        camUp.x, camUp.y, camUp.z, camera.camera3D.fovy,
                        camera.frustum.near, camera.frustum.far,
                        maindata.renderWidth, maindata.renderHeight);
                }
            }

            //opencode: v0.1.29 — REWRITTEN pass structure (single Begin3D/
            //   End3D pair). The old interleaving pushed 3D state before the
            //   sky and re-entered after it, leaving an unbalanced state-stack
            //   entry that corrupted every following pass (flat frames).
            //   Correct order:
            //     1. bind RT + clear
            //     2. sky (its own Begin/End texture cycle — ends unbound!)
            //     3. re-bind RT (Sky_DrawTo's EndTexture went to FBO 0,
            //        verified by staged readback — this unbind was THE reason
            //        models painted the screen instead of the reflection)
            //     4. ONE Begin3D(mirrorCam) + mirror-matrix upload
            //     5. models (cubemap-face pattern), flush
            //     6. End3D + EndTexture
//opencode: v0.1.33 — synchronous GL debug messages for this pass
             {
                 extern gX_MainArguments arguments;
                 static gX_I1 s_dbgInit = false;
                 if (!s_dbgInit && !arguments.noGlDebug) { //opencode: v0.3.8 — --no-gl-debug skips the callback
                     s_dbgInit = true;
                     extern gX_I0 gX_RHI_Debug_Init(gX_I0);
                     gX_RHI_Debug_Init();
                 }
             }

            gX_I32 prevCullRefl = gX_RHI_BACK;
            gX_RHI_GetIntegerv(gX_RHI_CULL_FACE_MODE, &prevCullRefl);
            gX_RendererGFX_Sky_DrawTo(gfx, *planeRT, 0);

            gX_Drawing_BeginTexture(*planeRT);
            gX_RHI_Viewport(0, 0, (gX_I32)planeRT->texture.width, (gX_I32)planeRT->texture.height);
            //opencode: v0.1.29 — mirror the PROVEN RenderEx context exactly:
            //   dedicated FBO with depth RBO + explicit draw/read buffers,
            //   color attachment = the SAME planar texture the wrapper FBO
            //   uses (legal: never bound simultaneously). The wrapper FBO
            //   served the sky fine but model draws produced zero fragments
            //   through it in every combination tested.
            //opencode: two-mirror — plane 1 renders into its own RT FBO
            //   (no shared depth-texture apparatus); plane 0 keeps the
            //   proven dedicated FBO path exactly.
            if (planeIndex == 0) {
                //opencode: v0.1.33 — re-attach EVERY frame: the wrapper can
                //   recreate its color texture (resize/reload), orphaning a
                //   once-cached attachment. Symptom was a FROZEN reflection
                //   image that never responded to shader/material changes.
                static gX_U32 s_planarFbo = 0;
                static gX_U32 s_attachedTexId = 0;
                if (s_planarFbo == 0) {
                    gX_RHI_FBO_Gen(1, &s_planarFbo);
                    gfx->reflection.planarFbo = s_planarFbo;
                    //opencode: v0.3.8 - depth TEXTURE (sampleable) instead of
                    //   an RBO: the planar-fog gen pass reads this depth
                    g_planarDepthTex = gX_RHI_Tex_Gen(gX_RHI_TEXTURE_2D, "planar_depth");
                    gX_RHI_Tex_Storage2D(g_planarDepthTex, gX_RHI_TEXTURE_2D, 1,
                        gX_RHI_DEPTH_COMPONENT24, (gX_U32)maindata.renderWidth,
                        (gX_U32)maindata.renderHeight);
                    gX_RHI_Tex_Param(g_planarDepthTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_NEAREST);
                    gX_RHI_Tex_Param(g_planarDepthTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_NEAREST);
                    gX_RHI_Tex_Param(g_planarDepthTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_COMPARE_MODE, gX_RHI_NONE);
                    gX_RHI_DrawBuffer(gX_RHI_COLOR_ATTACHMENT0);
                    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
                }
                gX_U32 curTex = gfx->reflection.planar.wrapper.rt.texture.id;
                if (curTex != 0 && curTex != s_attachedTexId) {
                    gX_RHI_BindFBO(s_planarFbo);
                    gX_RHI_FBO_AttachTexture(curTex, gX_RHI_COLOR_ATTACHMENT0, gX_RHI_TEXTURE_2D);
                    //opencode: v0.3.8 - depth attached as the sampleable
                    //   texture (replaces the old RBO attachment)
                    gX_RHI_FBO_AttachTexture(g_planarDepthTex, gX_RHI_DEPTH_ATTACHMENT, gX_RHI_TEXTURE_2D);
                    s_attachedTexId = curTex;
                    gX_TraceLog_GFX(gX_LOG_NOTICE, "PLANAR: fbo=%u tex=%u complete=%d",
                        s_planarFbo, curTex, gX_RHI_FBO_IsComplete());
                } else {
                    gX_RHI_BindFBO(s_planarFbo);
                }
                //opencode: audit — the depth texture is exclusive to this
                //   FBO (the wrapper-FBO clear above never touches it): clear
                //   it here or it holds garbage and the SSR depth-validity
                //   mask reads everywhere-valid. Color is already cleared.
                gX_RHI_Clear(gX_RHI_DEPTH_BUFFER_BIT);
            } else {
                //opencode: two-mirror plane-1 FBO — same proven recipe as
                //   plane 0 (wrapper FBOs yield zero model fragments), with
                //   its own half-res depth RBO (no sampleable depth: plane 1
                //   skips terrain march and fog composite by design).
                static gX_U32 s_mirrorFboB = 0;
                static gX_U32 s_mirrorDepthB = 0;
                static gX_U32 s_mirrorAttachedB = 0;
                static gX_I32 s_mirrorWB = 0;
                static gX_I32 s_mirrorHB = 0;
                gX_I32 bw = (gX_I32)planeRT->texture.width;
                gX_I32 bh = (gX_I32)planeRT->texture.height;
                if (s_mirrorFboB == 0 || bw != s_mirrorWB || bh != s_mirrorHB) {
                    if (s_mirrorFboB > 0) gX_RHI_FBO_Delete(s_mirrorFboB);
                    if (s_mirrorDepthB > 0) gX_RHI_RBO_Delete(s_mirrorDepthB);
                    s_mirrorFboB = 0;
                    s_mirrorDepthB = 0;
                    s_mirrorAttachedB = 0;
                    gX_RHI_FBO_Gen(1, &s_mirrorFboB);
                    s_mirrorDepthB = gX_RHI_RBO_Create((gX_U32)bw, (gX_U32)bh, gX_RHI_DEPTH_COMPONENT24);
                    gX_RHI_BindFBO(s_mirrorFboB);
                    gX_RHI_FBO_AttachDepthRBO(s_mirrorDepthB);
                    gX_RHI_DrawBuffer(gX_RHI_COLOR_ATTACHMENT0);
                    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
                    s_mirrorWB = bw;
                    s_mirrorHB = bh;
                }
                gX_U32 bTex = planeRT->texture.id;
                if (bTex != 0 && bTex != s_mirrorAttachedB) {
                    gX_RHI_BindFBO(s_mirrorFboB);
                    gX_RHI_FBO_AttachTexture(bTex, gX_RHI_COLOR_ATTACHMENT0, gX_RHI_TEXTURE_2D);
                    s_mirrorAttachedB = bTex;
                    gX_TraceLog_GFX(gX_LOG_NOTICE, "MIRROR-B: fbo=%u tex=%u complete=%d",
                        s_mirrorFboB, bTex, gX_RHI_FBO_IsComplete());
                } else {
                    gX_RHI_BindFBO(s_mirrorFboB);
                }
                gX_RHI_Clear(gX_RHI_DEPTH_BUFFER_BIT);
            }
            gX_RHI_Viewport(0, 0, (gX_I32)planeRT->texture.width, (gX_I32)planeRT->texture.height);
            gX_Drawing_Begin3D(mirrorCam);
            //opencode: two-mirror — Begin3D resets the viewport to window
            //   size; re-assert the plane RT size (plane 0 identical values).
            gX_RHI_Viewport(0, 0, (gX_I32)planeRT->texture.width, (gX_I32)planeRT->texture.height);

            {
                //opencode: v0.1.32 — NO Shader_Update here! Its hot-reload
                //   check can swap shader->shader.id MID-PASS: uniforms uploaded
                //   before the swap land on the dead program while the draw runs
                //   on the new one — default-material shading bug. Globals were
                //   already uploaded by the update phase; only the mirror
                //   view/projection need refreshing here.
                gX_RHI_Shader_Use(gfx->shader.shader.id);
                gX_Matrix rvM = gX_Matrix_LookAt(mirrorCam.position, mirrorCam.target, mirrorCam.up);
                gX_F32 ra = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
                gX_Matrix rpM = gX_Matrix_Perspective(mirrorCam.fovy * gX_DEG2RAD, ra,
                    camera.frustum.near, camera.frustum.far);
                if (gfx->shader.locations.matViewLoc >= 0)
                    gX_RHI_UniformMatrix(gfx->shader.locations.matViewLoc, 1, 1, &rvM.m0);
                if (gfx->shader.locations.matProjectionLoc >= 0)
                    gX_RHI_UniformMatrix(gfx->shader.locations.matProjectionLoc, 1, 1, &rpM.m0);
            }

            //opencode: v0.1.30 — break the FEEDBACK LOOP: unit 19 (reflTex)
            //   still holds the planar RT texture from the previous frame's
            //   floor draw; drawing INTO that RT while a sampler references it
            //   yields undefined content (the gray/khaki garbage seen in the
            //   RT dump) plus GL_INVALID_OPERATION.
            gX_RHI_Tex_BindOnUnit(0, gX_GFX_TEXUNIT_REFLECTION); //opencode: break feedback loop
            gX_RHI_InvalidateCaches();

            //opencode: v0.1.33 — RAW drawing through a dedicated program with
            //   ZERO raylib interference: only matrices + solid color. This
            //   isolates planar rasterization from all uniform/type-mismatch
            //   hazards (NVIDIA debug showed component-count errors from
            //   raylib_Mesh_Draw's loc-based uploads in this offscreen pass).
            {
                if (maindata.backendLoaded && !g_planarShader.loaded) {
                    gX_RendererGFX_Shader_Init(&g_planarShader,
                        GX_PATH("shader", "planar_debug.vert"),
                        GX_PATH("shader", "planar_debug.frag"));
                    g_planarShader.loadAsap = true;
                }
                if (maindata.backendLoaded && !g_planarShader.loaded) {
                    gX_RendererGFX_Shader_Load(&g_planarShader);
                    if (g_planarShader.shader.id > 0)
                        gX_GL_RawRegister(g_planarShader.shader.id);
                }
            }
            //opencode: mirror camera winding - LookAt re-orthogonalizes the
            //   reflected camera into a proper right-handed basis. The
            //   original mesh winding therefore remains CCW; changing it to
            //   CW selects the cube's interior/back faces in the mirror RT.
            //   Scoped save/set/restore around both model loops (solid +
            //   full-lit); sky already drawn above with default winding.
            gX_I32 prevFrontFace = (gX_I32)gX_RHI_CCW;
            gX_RHI_GetIntegerv(gX_RHI_FRONT_FACE, &prevFrontFace);
            gX_RHI_FrontFace(gX_RHI_CCW);
            //opencode: linear mirror fill - skip tonemap/exposure/SSAO in
            //   the RT so content is tonemapped once, in main. Scoped with
            //   the mirror front-face state (bind-then-set: model draws rebind programs).
            gX_RHI_Shader_Use(gfx->shader.shader.id);
            //opencode: lighting/specular view terms must use the mirrored camera in this pass.
            if (gfx->shader.locations.viewPosLoc >= 0)
                gX_Shader_SetValue_V3(gfx->shader.shader,
                    gfx->shader.locations.viewPosLoc, mirrorCam.position);
            if (gfx->shader.locations.cameraPositionLoc >= 0)
                gX_Shader_SetValue_V3(gfx->shader.shader,
                    gfx->shader.locations.cameraPositionLoc, mirrorCam.position);
            if (gfx->shader.locations.mirrorFillLoc >= 0)
                gX_RHI_UniformInt(gfx->shader.locations.mirrorFillLoc, 1);
            //opencode: the optional SSR normal pre-pass shares this shader and
            //   leaves ssrNormalPass=1 on its final model. Clear that sticky
            //   per-program flag before the next frame's planar fill, or the
            //   mirror RT is replaced with encoded normals.
            if (gfx->shader.locations.ssrNormalPassLoc >= 0)
                gX_RHI_UniformInt(gfx->shader.locations.ssrNormalPassLoc, 0);
            //opencode: v0.3.4-polish - full-lit mirror view by default
            //   (same lit-model pattern as the cubemap face callback), with
            //   the v0.1.33 solid-color path kept behind GX_PLANAR_DEBUG=1.
            if (getenv("GX_PLANAR_DEBUG")) {
                if (!g_planarShader.loaded || g_planarShader.shader.id == 0) {
                    gX_TraceLog_GFX(gX_LOG_WARNING, "PLANAR: shader not ready");
                } else {
                gX_U32 pid2 = g_planarShader.shader.id;
                gX_RHI_Shader_Use(pid2);
                gX_Matrix rvM = gX_Matrix_LookAt(mirrorCam.position, mirrorCam.target, mirrorCam.up);
                gX_F32 ra = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
                gX_Matrix rpM = gX_Matrix_Perspective(mirrorCam.fovy * gX_DEG2RAD, ra,
                    camera.frustum.near, camera.frustum.far);
                gX_I32 mvL = gX_RHI_GetUniformLocation(pid2, "matView");
                gX_I32 mpL = gX_RHI_GetUniformLocation(pid2, "matProjection");
                if (mvL >= 0) gX_RHI_UniformMatrix(mvL, 1, 1, &rvM.m0);
                if (mpL >= 0) gX_RHI_UniformMatrix(mpL, 1, 1, &rpM.m0);
                GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, rmodel) {
                    if (!rmodel->loaded) continue;
                    if (rmodel->params.excludeFromPlanarCapture) continue;
                    if (rmodel->params.planarReflection
                        && !gX_RendererGFX_Mirror_IsCounterpart(gfx, rmodel->params.mirrorPair)) continue;
                    if (rmodel->params.reflectionSource) continue;
                    {
                        gX_I32 ucL = gX_RHI_GetUniformLocation(pid2, "uColor");
                        if (ucL >= 0) {
                            gX_Color ec = rmodel->params.emissiveColor;
                            if (!ec.r && !ec.g && !ec.b)
                                ec = (gX_Color){230, 230, 235, 255};
                            gX_F32 uc[4] = { ec.r / 255.0f, ec.g / 255.0f,
                                             ec.b / 255.0f, 1.0f };
                            gX_RHI_UniformVec4(ucL, 1, uc);
                        }
                    }
                    gX_Matrix smat = gX_Matrix_Scale(rmodel->params.scale.x, rmodel->params.scale.y, rmodel->params.scale.z);
                    gX_Matrix rmat = gX_Matrix_RotateXYZ((gX_F32v3){
                        rmodel->params.rotation.x * gX_DEG2RAD,
                        rmodel->params.rotation.y * gX_DEG2RAD,
                        rmodel->params.rotation.z * gX_DEG2RAD});
                    gX_Matrix tmat = gX_Matrix_Translate(rmodel->params.pos.x, rmodel->params.pos.y, rmodel->params.pos.z);
                    gX_Matrix trs = gX_Matrix_Multiply(smat, rmat);
                    trs = gX_Matrix_Multiply(trs, tmat);
                    gX_I32 mmL = gX_RHI_GetUniformLocation(pid2, "gX_ModelMatrix");
                    if (mmL >= 0) gX_RHI_UniformMatrix(mmL, 1, 1, &trs.m0);
                    gX_Model m = rmodel->model;
                    for (gX_I32 mi = 0; mi < m.meshCount; mi++) {
                        if (m.meshes[mi].vaoId == 0) continue;
                        gX_Raw_Mesh_Draw(m.meshes[mi]);
                    }
                }
                }
            } else {
                // full-lit path — mirror camera already applied to
                // gfx->shader.id by the v0.1.32 block above; draw models with
                // the standard pipeline exactly like the cubemap faces do.
                //opencode: candidate A — RAW draws. gX_Model_Draw_Parameters
                //   routes DrawEx -> raylib_Mesh_Draw DIRECTLY, bypassing the
                //   raw-program check that saves the main pass; its rlgl-stack
                //   matrix uploads stomped the mirror view/proj with 2D-ortho
                //   leftovers (trace 20115-16) so the fill rasterized nothing
                //   (sky-only RT). Raw path mirrors the solid branch; mirror
                //   + model matrices uploaded above stay intact. Material
                //   diffuse color uploaded explicitly (raylib did it).
                //opencode: map flags OFF — the raw path skips Model_Draw's
                //   per-model flag uploads, leaving STALE hasAlbedoMap=1
                //   behind: albedo *= black (unbound map units) while flat
                //   ambient survives => hue-free ghosts (red-albedo probe:
                //   105 uploads, zero red px). Demo GLBs carry no textures,
                //   so forcing off loses nothing and restores albedo.
                if (gfx->shader.locations.hasAlbedoMapLoc >= 0)
                    gX_Shader_SetValue_Int(gfx->shader.shader,
                        gfx->shader.locations.hasAlbedoMapLoc, 0);
                if (gfx->shader.locations.hasMetalnessMapLoc >= 0)
                    gX_Shader_SetValue_Int(gfx->shader.shader,
                        gfx->shader.locations.hasMetalnessMapLoc, 0);
                if (gfx->shader.locations.hasNormalMapLoc >= 0)
                    gX_Shader_SetValue_Int(gfx->shader.shader,
                        gfx->shader.locations.hasNormalMapLoc, 0);
                if (gfx->shader.locations.hasHeightMapLoc >= 0)
                    gX_Shader_SetValue_Int(gfx->shader.shader,
                        gfx->shader.locations.hasHeightMapLoc, 0);
                if (gfx->shader.locations.hasRoughnessMapLoc >= 0)
                    gX_Shader_SetValue_Int(gfx->shader.shader,
                        gfx->shader.locations.hasRoughnessMapLoc, 0);
                if (gfx->shader.locations.hasAoMapLoc >= 0)
                    gX_Shader_SetValue_Int(gfx->shader.shader,
                        gfx->shader.locations.hasAoMapLoc, 0);
                if (gfx->shader.locations.hasEmissiveMapLoc >= 0)
                    gX_Shader_SetValue_Int(gfx->shader.shader,
                        gfx->shader.locations.hasEmissiveMapLoc, 0);
                if (gfx->shader.locations.hasGlossMapLoc >= 0)
                    gX_Shader_SetValue_Int(gfx->shader.shader,
                        gfx->shader.locations.hasGlossMapLoc, 0);
                //opencode: planar fill uses a raw draw path, so explicitly
                //   clear the per-model lightmap gate before its model loop.
                if (gfx->shader.locations.hasLightmapLoc >= 0) {
                    gX_Shader_SetValue_Int(gfx->shader.shader,
                        gfx->shader.locations.hasLightmapLoc, 0);
                }
                //opencode: mirror must WRITE (replace), not blend — the fill
                //   inherits leftover blend state, and the fragment alpha
                //   carries ssrRoughness (~0.02 for the mirror cube), so a
                //   blended fill contributes ~2% cube over 98% background
                //   (planar fidelity 0%, pale ghosts). Same save/disable/
                //   restore idiom as the Hi-Z chain below.
                gX_I1 fillPB_Blend = gX_RHI_IsEnabled(gX_RHI_BLEND);
                gX_RHI_Disable(gX_RHI_BLEND);
                GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, rmodel) {
                    if (!rmodel->loaded) continue;
                    if (rmodel->params.excludeFromPlanarCapture) continue;
                    if (rmodel->params.planarReflection
                        && !gX_RendererGFX_Mirror_IsCounterpart(gfx, rmodel->params.mirrorPair)) continue;
                    if (rmodel->params.reflectionSource) continue;
                    gX_Model md = rmodel->model;
                    gX_Material fillDefaultMat = {0};
                    static gX_MaterialMap fillDefaultMaps[12] = {0};
                    fillDefaultMat.maps = fillDefaultMaps;
                    fillDefaultMat.shader = gfx->shader.shader;
                    fillDefaultMat.maps[gX_MATERIAL_MAP_ALBEDO].color = (gX_Color){200, 200, 200, 255};
                    gX_I1 lightmapEnabled = rmodel->params.hasLightmap;
                    if (lightmapEnabled) {
                        for (gX_I32 lmi = 0; lmi < md.meshCount; lmi++) {
                            if (md.meshes[lmi].texcoords2 == NULL) {
                                lightmapEnabled = 0;
                                break;
                            }
                        }
                    }
                    gX_Model_AttachShader(&md, gfx->shader.shader);
                    //opencode: mirror raw draws bypass Model_Draw's lightmap
                    //   binding, so reproduce the explicit unit-26 setup here.
                    if (gfx->shader.locations.hasLightmapLoc >= 0)
                        gX_Shader_SetValue_Int(gfx->shader.shader,
                            gfx->shader.locations.hasLightmapLoc, 0);
                    if (gfx->shader.locations.lightmapStrengthLoc >= 0)
                        gX_Shader_SetValue_Float(gfx->shader.shader,
                            gfx->shader.locations.lightmapStrengthLoc,
                            rmodel->params.lightmapStrength);
                    if (gfx->shader.locations.lightmapScaleLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.lightmapScaleLoc,
                            rmodel->params.lightmapScale);
                    if (gfx->shader.locations.lightmapOffsetLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.lightmapOffsetLoc,
                            rmodel->params.lightmapOffset);
                    if (gfx->shader.locations.albedoMapScaleLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.albedoMapScaleLoc,
                            rmodel->params.albedoMapScale);
                    if (gfx->shader.locations.albedoMapOffsetLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.albedoMapOffsetLoc,
                            rmodel->params.albedoMapOffset);
                    gX_Shader_SetValue_Float(gfx->shader.shader,
                        gfx->shader.locations.albedoMapFactorLoc,
                        rmodel->params.albedoMapFactor);
                    if (gfx->shader.locations.metalnessMapScaleLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.metalnessMapScaleLoc,
                            rmodel->params.metalnessMapScale);
                    if (gfx->shader.locations.metalnessMapOffsetLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.metalnessMapOffsetLoc,
                            rmodel->params.metalnessMapOffset);
                    gX_Shader_SetValue_Float(gfx->shader.shader,
                        gfx->shader.locations.metalnessMapFactorLoc,
                        rmodel->params.metalnessMapFactor);
                    if (gfx->shader.locations.normalMapScaleLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.normalMapScaleLoc,
                            rmodel->params.normalMapScale);
                    if (gfx->shader.locations.normalMapOffsetLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.normalMapOffsetLoc,
                            rmodel->params.normalMapOffset);
                    gX_Shader_SetValue_Float(gfx->shader.shader,
                        gfx->shader.locations.normalMapFactorLoc,
                        rmodel->params.normalMapFactor);
                    if (gfx->shader.locations.heightMapScaleLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.heightMapScaleLoc,
                            rmodel->params.heightMapScale);
                    if (gfx->shader.locations.heightMapOffsetLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.heightMapOffsetLoc,
                            rmodel->params.heightMapOffset);
                    gX_Shader_SetValue_Float(gfx->shader.shader,
                        gfx->shader.locations.heightMapFactorLoc,
                        rmodel->params.heightMapFactor);
                    if (gfx->shader.locations.parallaxModeLoc >= 0)
                        gX_Shader_SetValue_Int(gfx->shader.shader,
                            gfx->shader.locations.parallaxModeLoc,
                            rmodel->params.parallaxMode);
                    if (gfx->shader.locations.roughnessMapScaleLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.roughnessMapScaleLoc,
                            rmodel->params.roughnessMapScale);
                    if (gfx->shader.locations.roughnessMapOffsetLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.roughnessMapOffsetLoc,
                            rmodel->params.roughnessMapOffset);
                    gX_Shader_SetValue_Float(gfx->shader.shader,
                        gfx->shader.locations.roughnessMapFactorLoc,
                        rmodel->params.roughnessMapFactor);
                    if (gfx->shader.locations.aoMapScaleLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.aoMapScaleLoc,
                            rmodel->params.aoMapScale);
                    if (gfx->shader.locations.aoMapOffsetLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.aoMapOffsetLoc,
                            rmodel->params.aoMapOffset);
                    gX_Shader_SetValue_Float(gfx->shader.shader,
                        gfx->shader.locations.aoMapFactorLoc,
                        rmodel->params.aoMapFactor);
                    if (gfx->shader.locations.emissiveMapScaleLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.emissiveMapScaleLoc,
                            rmodel->params.emissiveMapScale);
                    if (gfx->shader.locations.emissiveMapOffsetLoc >= 0)
                        gX_Shader_SetValue_V2(gfx->shader.shader,
                            gfx->shader.locations.emissiveMapOffsetLoc,
                            rmodel->params.emissiveMapOffset);
                    gX_Shader_SetValue_Float(gfx->shader.shader,
                        gfx->shader.locations.emissiveMapFactorLoc,
                        rmodel->params.emissiveMapFactor);
                    gX_Shader_SetValue_Int(gfx->shader.shader,
                        gfx->shader.locations.enableAlphaTestLoc,
                        rmodel->params.enableAlphaTest);
                    gX_Shader_SetValue_Float(gfx->shader.shader,
                        gfx->shader.locations.alphaCutoffLoc,
                        rmodel->params.alphaCutoff);
                    if (lightmapEnabled
                        && rmodel->params.lightmapID >= 0
                        && (gX_U32)rmodel->params.lightmapID < gfx->textures.amount
                        && gfx->textures.meta[rmodel->params.lightmapID].USED) {
                        gX_RendererGFX_Texture *lightmap = gX_Heap_SeekPtr_Index(&gfx->textures,
                            (gX_U32)rmodel->params.lightmapID);
                        if (lightmap && lightmap->wrapper.loadingState.loaded
                            && lightmap->wrapper.texture.id > 0
                            && gfx->shader.locations.lightmapMapLoc >= 0) {
                            gX_Texture_SetWrap(lightmap->wrapper.texture, gX_TEXTURE_WRAP_CLAMP);
                            gX_RHI_Tex_BindOnUnit(lightmap->wrapper.texture.id,
                                gX_GFX_TEXUNIT_LIGHTMAP);
                            gX_RHI_Shader_Use(gfx->shader.shader.id);
                            gX_RHI_UniformInt(gfx->shader.locations.lightmapMapLoc,
                                gX_GFX_TEXUNIT_LIGHTMAP);
                            if (gfx->shader.locations.hasLightmapLoc >= 0)
                                gX_RHI_UniformInt(gfx->shader.locations.hasLightmapLoc, 1);
                        }
                    }
                    //opencode: per-model shading uniforms — the raw path
                    //   skips Model_Draw's uploads, leaving STALE main-pass
                    //   values behind (emissive/metal/roughness of whatever
                    //   drew last). The mirror then shades wrong: the diag
                    //   magenta cube rendered dark olive, so planar fidelity
                    //   scored 0%. Same setters as the main path.
                    if (gfx->shader.locations.usePBRLoc >= 0)
                        gX_Shader_SetValue_Int(gfx->shader.shader,
                            gfx->shader.locations.usePBRLoc, rmodel->params.usePBR);
                    gX_Shader_SetValue_Float(gfx->shader.shader,
                        gfx->shader.locations.albedoFactorLoc, rmodel->params.albedo);
                    //opencode: planar fill carries the tinted albedo too —
                    //   without it, tinted models reflect stale main-pass
                    //   colors (same helper as the main draw, incl. GX_TINT).
                    {
                        gX_Material *fillMat = NULL;
                        if (md.meshMaterial != NULL && md.materialCount > 0) {
                            gX_U32 fillMatIdx = (gX_U32)md.meshMaterial[0];
                            if (fillMatIdx < (gX_U32)md.materialCount)
                                fillMat = &md.materials[fillMatIdx];
                        }
                        if (fillMat == NULL) fillMat = &fillDefaultMat;
                        gX_Shader_SetValue_Color(gfx->shader.shader,
                            gfx->shader.locations.albedoColorLoc,
                            gX_RendererGFX_AlbedoTint_Apply(
                                fillMat->maps[gX_MATERIAL_MAP_ALBEDO].color,
                                rmodel->params.albedo,
                                rmodel->params.useTopTint
                                    ? (gX_Color){255, 255, 255, 255}
                                    : rmodel->params.albedoTint));
                    }
                    gX_Shader_SetValue_Float(gfx->shader.shader,
                        gfx->shader.locations.metalnessFactorLoc, rmodel->params.metalness);
                    gX_Shader_SetValue_Float(gfx->shader.shader,
                        gfx->shader.locations.modelShininessLoc, rmodel->params.shininess);
                    gX_Shader_SetValue_Float(gfx->shader.shader,
                        gfx->shader.locations.roughnessLoc, rmodel->params.roughness);
                    gX_Shader_SetValue_Float(gfx->shader.shader,
                        gfx->shader.locations.emissiveFactorLoc, rmodel->params.emissiveFactor);
                    if (gfx->shader.locations.emissiveColorLoc >= 0)
                        gX_Shader_SetValue_Color(gfx->shader.shader,
                            gfx->shader.locations.emissiveColorLoc, rmodel->params.emissiveColor);
                    gX_Shader_SetValue_Float(gfx->shader.shader,
                        gfx->shader.locations.blendOpacityLoc, 1.0f);
                    if (gfx->shader.locations.specularColorLoc >= 0)
                        gX_Shader_SetValue_V3(gfx->shader.shader,
                            gfx->shader.locations.specularColorLoc, rmodel->params.specularColor);
                    gX_Shader_SetValue_Float(gfx->shader.shader,
                        gfx->shader.locations.aoLoc, rmodel->params.ao);
                    gX_Shader_SetValue_Int(gfx->shader.shader,
                        gfx->shader.locations.planarReflectionLoc, 0);
                    gX_Shader_SetValue_Int(gfx->shader.shader,
                        gfx->shader.locations.enableRefractionLoc, 0);
                    if (gfx->shader.locations.nonPlanarReflectionLoc >= 0)
                        gX_Shader_SetValue_Int(gfx->shader.shader,
                            gfx->shader.locations.nonPlanarReflectionLoc, 0);
                    gX_Shader_SetValue_Int(gfx->shader.shader,
                        gfx->shader.locations.enableReflectionsLoc,
                        rmodel->params.enableReflections && gfx->state.ENABLE_REFLECTIONS);
                    //opencode: two-mirror counterpart — the facing slab draws
                    //   in the other plane's view sampling its history bounce
                    //   (last frame) with that history's MVP. Full-lit fill
                    //   only; the solid debug path keeps flat colors.
                    if (gX_RendererGFX_Mirror_IsCounterpart(gfx, rmodel->params.mirrorPair)) {
                        gX_I32 other = (rmodel->params.mirrorPair == 1) ? 1 : 0;
                        if (gfx->shader.locations.planarReflectionLoc >= 0)
                            gX_Shader_SetValue_Int(gfx->shader.shader,
                                gfx->shader.locations.planarReflectionLoc, 1);
                        //opencode: the plane-membership gate (standard.frag)
                        //   tests the slab's own plane — the raw fill never
                        //   uploads it, so without this the sample always
                        //   falls back to the cubemap and the loop silently
                        //   measures nothing (energy arms identical).
                        gX_F32v3 counterpartNormal = rmodel->params.reflectionNormal;
                        gX_F32 counterpartDistance = rmodel->params.reflectionPlaneHeight;
                        if (!gfx_reflection_plane_world(rmodel,
                                &counterpartNormal, &counterpartDistance)) {
                            counterpartNormal = (gX_F32v3){0.0f, 1.0f, 0.0f};
                            counterpartDistance = 0.0f;
                        }
                        if (gfx->shader.locations.reflectionPlaneNormalLoc >= 0)
                            gX_Shader_SetValue_V3(gfx->shader.shader,
                                gfx->shader.locations.reflectionPlaneNormalLoc,
                                counterpartNormal);
                        if (gfx->shader.locations.reflectionPlaneHeightLoc >= 0)
                            gX_Shader_SetValue_Float(gfx->shader.shader,
                                gfx->shader.locations.reflectionPlaneHeightLoc,
                                counterpartDistance);
                        if (gfx->shader.locations.reflTexLoc >= 0)
                            gX_Shader_SetValue_TextureSlot(gfx->shader.shader,
                                gfx->shader.locations.reflTexLoc,
                                gX_RendererGFX_Mirror_HistTex(gfx, other),
                                gX_GFX_TEXUNIT_REFLECTION);
                        if (gfx->shader.locations.reflMVPLoc >= 0)
                            gX_Shader_SetValue_Matrix(gfx->shader.shader,
                                gfx->shader.locations.reflMVPLoc,
                                gX_RendererGFX_Mirror_HistMvp(gfx, other));
                    }
                    gX_Matrix smat = gX_Matrix_Scale(rmodel->params.scale.x, rmodel->params.scale.y, rmodel->params.scale.z);
                    gX_Matrix rmat = gX_Matrix_RotateXYZ((gX_F32v3){
                        rmodel->params.rotation.x * gX_DEG2RAD,
                        rmodel->params.rotation.y * gX_DEG2RAD,
                        rmodel->params.rotation.z * gX_DEG2RAD});
                    gX_Matrix tmat = gX_Matrix_Translate(rmodel->params.pos.x, rmodel->params.pos.y, rmodel->params.pos.z);
                    gX_Matrix trs = gX_Matrix_Multiply(smat, rmat);
                    trs = gX_Matrix_Multiply(trs, tmat);
                    //opencode: standard.vert positions planar-fill models through gX_ModelMatrix.
                    if (gfx->shader.locations.gX_ModelMatrixLoc >= 0)
                        gX_Shader_SetValue_Matrix(gfx->shader.shader,
                            gfx->shader.locations.gX_ModelMatrixLoc, trs);
                    //opencode: per-mesh diffuse color (raylib_Mesh_Draw did
                    //   this per mesh-material; one upload per model tinted
                    //   multi-material models monochrome).
                    for (gX_I32 mi = 0; mi < md.meshCount; mi++) {
                        if (md.meshes[mi].vaoId == 0) continue;
                        //opencode: per-mesh file base color (also the base
                        //   for the sidecar surface override below).
                        gX_Color dc = (gX_Color){200, 200, 200, 255};
                        if (md.materialCount > 0 && md.meshMaterial != NULL) {
                            gX_U32 dcIdx = (gX_U32)md.meshMaterial[mi];
                            if (dcIdx >= (gX_U32)md.materialCount) dcIdx = 0;
                            if (md.materials[dcIdx].maps)
                                dc = md.materials[dcIdx].maps[gX_MATERIAL_MAP_ALBEDO].color;
                        }
                        gX_Material *meshMat = &fillDefaultMat;
                        if (md.materialCount > 0 && md.meshMaterial != NULL) {
                            gX_U32 meshMaterialIndex = (gX_U32)md.meshMaterial[mi];
                            if (meshMaterialIndex < (gX_U32)md.materialCount)
                                meshMat = &md.materials[meshMaterialIndex];
                        }
                        gX_Shader_SetValue_Color(gfx->shader.shader,
                            gfx->shader.locations.albedoColorLoc,
                            gX_RendererGFX_AlbedoTint_Apply(dc,
                                rmodel->params.albedo,
                                rmodel->params.useTopTint
                                    ? (gX_Color){255, 255, 255, 255}
                                    : rmodel->params.albedoTint));
                        if (meshMat->maps)
                            gX_Shader_SetValue_Color(gfx->shader.shader,
                                gfx->shader.locations.metalnessColorLoc,
                                meshMat->maps[gX_MATERIAL_MAP_METALNESS].color);
                        if (md.materialCount > 0 && md.meshMaterial != NULL
                            && gfx->shader.shader.locs != NULL
                            && gfx->shader.shader.locs[12] >= 0) {
                            gX_Shader_SetValue_Color(gfx->shader.shader,
                                gfx->shader.shader.locs[12], dc);
                        }
                        gX_RendererGFX_Model_MeshSurface(rmodel, gfx->shader.shader,
                            dc, gfx->shader.locations.albedoColorLoc,
                            gfx->shader.locations.roughnessLoc,
                            gfx->shader.locations.metalnessFactorLoc, mi,
                            NULL, NULL, NULL);
                        gX_RendererGFX_Model_MeshEmissive(rmodel, gfx->shader.shader,
                            gfx->shader.locations.emissiveFactorLoc,
                            gfx->shader.locations.emissiveColorLoc, mi);
                        gfx_planar_capture_mesh_maps(gfx, rmodel, meshMat, mi);
                        gX_Raw_Mesh_Draw(md.meshes[mi]);
                    }
                }
                if (fillPB_Blend) gX_RHI_Enable(gX_RHI_BLEND);
                gX_Batch_Flush();
                //opencode: two-mirror — terrain march, stats, and fog stay
                //   plane-0-only (plane 1 documents the gap: unfogged fill,
                //   no terrain march). Legacy scenes always take this branch.
                if (planeIndex == 0) {
                //opencode: planar terrain — mirrored march into the planar
                //   FBO under the models (own FrontFace/ortho/blend save +
                //   restore). GX_T_TERRAINMIRROR=0 kills the mirror march
                //   only (perf knob for the +1 fullscreen march; stats
                //   then meter its absence).
                {
                    const gX_I8 *tm = getenv("GX_T_TERRAINMIRROR");
                    if (!tm || tm[0] != '0')
                        gX_RendererGFX_Terrain_DrawMirror(gfx, gfx->reflection.mvp);
                }
                }
            }
            gX_RHI_FrontFace((gX_U32)prevFrontFace);
            gX_RHI_Shader_Use(gfx->shader.shader.id);
            if (gfx->shader.locations.mirrorFillLoc >= 0)
                gX_RHI_UniformInt(gfx->shader.locations.mirrorFillLoc, 0);
            if (gfx->shader.locations.viewPosLoc >= 0)
                gX_Shader_SetValue_V3(gfx->shader.shader,
                    gfx->shader.locations.viewPosLoc, camera.camera3D.position);
            if (gfx->shader.locations.cameraPositionLoc >= 0)
                gX_Shader_SetValue_V3(gfx->shader.shader,
                    gfx->shader.locations.cameraPositionLoc, camera.camera3D.position);

            gX_Drawing_End3D();
            gX_RHI_Cull_Mode((gX_U32)prevCullRefl);
            gX_Drawing_EndTexture();
            gfx->reflection.filledThisFrame = true;
            if (planeIndex == 0) {
            //opencode: planar terrain stats — validate the mirrored march
            //   against the CPU oracle (before planar-fog modifies color).
            gX_RendererGFX_Terrain_PlanarStats(gfx, gfx->reflection.planarFbo);
            //opencode: v0.3.8 - fog the planar reflection: the mirrored view
            //   rendered unfogged, so the fogged direct view beside the
            //   unfogged reflection painted a hard seam at the mirror horizon.
            //   Re-run the fog gen with the mirrored camera and composite the
            //   delta into the planar RT BEFORE the model pass samples it.
            if (gfx->state.ENABLE_VOLUME_FOG && g_planarDepthTex > 0
                && gfx->reflection.planar.wrapper.loadingState.loaded) {
                gX_Matrix planarView = gX_Matrix_LookAt(mirrorCam.position, mirrorCam.target, mirrorCam.up);
                gX_F32 pa = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
                gX_Matrix planarProj = gX_Matrix_Perspective(mirrorCam.fovy * gX_DEG2RAD, pa,
                    camera.frustum.near, camera.frustum.far);
                gX_RendererGFX_FogVolume_PlanarFog(gfx,
                    mirrorCam.position, mirrorCam.fovy,
                    gX_Matrix_Invert(planarView), gX_Matrix_Invert(planarProj),
                    g_planarDepthTex);
            }
            }
        //opencode: two-mirror plane loop close (interior indent unchanged).
        }
        //opencode: two-mirror feedback commit — fresh bounces to history
        //   with energy decay (pair scenes only; legacy untouched).
        if (pairCount == 2) gX_RendererGFX_Mirror_Commit(gfx);
        }
        GX_PROFILE_ZONE_END("gfx:planar_reflection");
    }

    //opencode: dynamic cubemap render — update each frame for SSR fallback / model reflections.
    //   Gated on an actual reflective model existing: with ENABLE_REFLECTIONS on but no
    //   model sampling the cubemap (the demo scene has none), this still re-rendered the
    //   whole scene 6x per frame with the raw draw path — which never binds the SSAO map,
    //   leaving unit 12 stale and corrupting the following opaque pass (blue/purple speckle
    //   on surfaces when SSAO was on).
    //   v0.1.11: SSR is now a cubemap consumer too (env fallback) — render when SSR is on.
    if (gfx->reflection.cubeReady && (gfx->state.ENABLE_REFLECTIONS
        || gfx->state.ENABLE_PLANAR_REFLECTIONS || gfx->state.ENABLE_SSR))
    {
        //opencode: v0.1.36 — per-surface gating (reflection pack Phase B):
        //   classify WHY the cubemap would be consumed before rendering 6
        //   faces. Consumers: (a) models sampling it directly
        //   (params.enableReflections), (b) SSR env fallback whenever the
        //   SSR pass runs, (c) IBL fallback while ibl maps are absent.
        //   (hunt: the old (b) clause excluded planar scenes because the
        //   march was skipped there; the march runs everywhere now, so the
        //   exclusion is gone - otherwise planar scenes would starve the
        //   env fallback down to the sky LUT.)
        //   A planar-only scene with no reflective models previously rendered
        //   the cubemap every frame for zero visual effect.
        gX_I1 anyReflective = false;
        GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, cm) {
            if (!cm->loaded) continue;
            //opencode: planar models consume the env cubemap through the
            //   projective fallback (grazing angles), so they must trigger
            //   the cube render like env-reflective models do.
            if (cm->params.enableReflections || cm->params.planarReflection) { anyReflective = true; break; }
        }
        gX_I1 ssrPassWillRun = gfx->state.ENABLE_SSR;
        //opencode: v0.1.37 — the water pass samples the dynamic cubemap for
        //   its sky reflection (water.c skyBoxLoc path); a water-only scene
        //   with SSR off must still render the faces or the reflection goes
        //   black (skybox fallback missing).
        gX_I1 waterUsesCube = gfx->state.ENABLE_WATER && gfx->water.loadingstate.loaded;
        gX_I1 cubemapNeeded = anyReflective || ssrPassWillRun || !gfx->ibl.ready
                               || waterUsesCube;
        if (cubemapNeeded)
        {
        //opencode: v0.1.21 — cubemap updates EVERY frame (was every 2nd,
        //   v0.1.13). The cadence was the direct driver of an interactive
        //   period-2 flicker once sun shadows were enabled (v0.1.19): on
        //   update frames RenderEx leaves the main program current, on skip
        //   frames the depth shader stays current, and some downstream uniform
        //   upload without an explicit bind lands in different programs per
        //   parity — flipping shadow/AO output for exactly half the frames.
        //   Every-frame updates make the pipeline state identical per frame.
        //   Cost: reflections median ~1.36ms vs ~1.15ms avg before (+~0.4ms).
        static gX_U32 s_cubeFrame = 0;
        (void)s_cubeFrame;
        {
        GX_PROFILE_ZONE_BEGIN("gfx:cubemap");

        //opencode: cubemap position — reflect camera across the reflective plane
        gX_F32v3 cubePos = camera.camera3D.position;
        {
            GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, cm) {
                if (!cm->loaded) continue;
                if (cm->params.enableReflections || cm->params.planarReflection)
                {
                    gX_F32v3 n = cm->params.reflectionNormal;
                    gX_F32 planeDist = cm->params.reflectionPlaneHeight;
                    if (!gfx_reflection_plane_world(cm, &n, &planeDist)) {
                        n = (gX_F32v3){0.0f, 1.0f, 0.0f};
                        planeDist = 0.0f;
                    }
                    gX_F32 d = gX_F32v3_Dot(cubePos, n) - planeDist;
                    cubePos = gX_F32v3_Sub(cubePos, gX_F32v3_MulScalar(n, 2.0f * d));
                    break;
                }
            }
        }

        // Disable specular for cubemap render to avoid seam highlights.
        //opencode: v0.3.8 - explicit Use first: glUniform lands on the
        //   CURRENTLY bound program, and at this point the standard shader
        //   is not necessarily active (the gate silently no-opped otherwise).
        gX_RHI_Shader_Use(gfx->shader.shader.id);
        gX_Shader_SetValue_Int(gfx->shader.shader, gfx->shader.locations.enableLightingSpecularLoc, 0);

        //opencode: v0.3.8 - break the env-cubemap recursion: scene draws inside
        //   the cubemap pass sampled the PREVIOUS frame's cubemap through
        //   enableReflections, so a mirror floor baked a reflection of its own
        //   reflection every frame - the converged recursive "tunnel" read as
        //   concentric ghost rectangles on reflective surfaces (top-down view
        //   of the showcase slab). Sky-only env inside the pass; the main
        //   render's single-bounce env reflection is unaffected.
        gX_Shader_SetValue_Int(gfx->shader.shader, gfx->shader.locations.enableReflectionsLoc, 0);
        //opencode: v0.3.8 - IBL gate too: the slab's mirror term rides the
        //   split-sum prefilter map (derived from the same dynamic cubemap),
        //   which enableReflections does not cover.
        gX_Shader_SetValue_Float(gfx->shader.shader, gfx->shader.locations.envAmbientFactorLoc, 0.0f);

        //opencode: TAA jitter must not leak into the environment capture —
        //   the cubemap is view-independent (IBL/env fallback), so per-frame
        //   sub-pixel offsets made the env reflection shimmer under TAA
        //   (measured: y2k floor 98.6% pixels changing >2/frame with SSR off).
        //   Zero for the 6 faces, restore the main-frame jitter after.
        gX_Shader_SetValue_V2(gfx->shader.shader, gfx->shader.locations.taaJitterLoc,
            (gX_F32v2){0.0f, 0.0f});


        // Store gfx pointer for the static callback
        g_cubemap_gfx = gfx;
        g_cubemap_hasLocal = 0;

        gX_F32v3 clearCol = {
            (gX_F32)gfx->sky.color.r / 255.0f,
            (gX_F32)gfx->sky.color.g / 255.0f,
            (gX_F32)gfx->sky.color.b / 255.0f
        };

        gX_Cubemap_RenderEx(gfx->reflection.dynamicCubemap, gfx->reflection.cubeSize,
                            cubePos, clearCol,
                            &gfx_cubemap_draw_face,
                            gfx->reflection.cubeFboID, gfx->reflection.cubeDepthRbo);
        //ai: retain the capture origin rather than reconstructing it per receiver plane.
        gfx->reflection.dynamicCapturePosition = cubePos;
        gfx->reflection.dynamicSkyOnly = !g_cubemap_hasLocal;

        // Restore specular for main render
        gX_Shader_SetValue_Int(gfx->shader.shader, gfx->shader.locations.enableLightingSpecularLoc,
            gfx->state.ENABLE_LIGHTING_SPECULAR ? 1 : 0);
        gX_Shader_SetValue_Int(gfx->shader.shader, gfx->shader.locations.enableReflectionsLoc,
            gfx->state.ENABLE_REFLECTIONS ? 1 : 0);
        gX_Shader_SetValue_Float(gfx->shader.shader, gfx->shader.locations.envAmbientFactorLoc,
            gfx->state.ENABLE_ENV_AMBIENT ? gfx->state.envAmbientFactor : 0.0f);
        gX_Shader_SetValue_V2(gfx->shader.shader, gfx->shader.locations.taaJitterLoc,
            gfx->taa.jitter);
        //opencode: reflection audit — the 6 faces just rendered; consumers
        //   gate on this (not sticky cubeReady) so skipped frames fall back
        //   to the sky LUT instead of sampling stale faces.
        gfx->reflection.cubeUpdatedThisFrame = true;

        GX_PROFILE_ZONE_END("gfx:cubemap");
        }

        //opencode: probe grid MVP-1 — own guard, decoupled from cubemapNeeded:
        //   baking into empty early frames (deferred loads still in flight)
        //   then going static would freeze black faces (observed). Bake one
        //   probe per frame once a direct consumer is loaded AND past the
        //   load-settle floor (frame 30, deterministic); static afterwards
        //   unless GX_PROBE_REFRESH=N re-renders one probe every N frames.
        //   Mutes mirror the dynamic capture above (no recursive env).
        if (gfx->reflection.probeCount > 0 && maindata.frameCount >= 30) {
            //opencode: probe consumers mirror the cubemap gate (direct and
            //   planar models both sample the env cubemap now).
            gX_I1 haveConsumer = false;
            GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, pm) {
                if (!pm->loaded) continue;
                if (pm->params.enableReflections || pm->params.planarReflection) {
                    haveConsumer = true; break;
                }
            }
            gX_I1 needBake = !gfx->reflection.probesReady && haveConsumer;
            const gX_I8 *refreshHook = getenv("GX_PROBE_REFRESH");
            gX_I32 refreshEvery = (refreshHook && atoi((const char *)refreshHook) > 0)
                ? (gX_I32)atoi((const char *)refreshHook) : 0;
            gX_I1 needRefresh = (!needBake && refreshEvery > 0
                && (maindata.frameCount % (gX_U64)refreshEvery) == 0);
            if (needBake || needRefresh) {
                gX_U32 pi = gfx->reflection.nextProbe % gfx->reflection.probeCount;
                gX_RendererGFX_Probe *probe = &gfx->reflection.probes[pi];
                GX_PROFILE_ZONE_BEGIN("gfx:probe");
                gX_RHI_Shader_Use(gfx->shader.shader.id);
                gX_Shader_SetValue_Int(gfx->shader.shader, gfx->shader.locations.enableLightingSpecularLoc, 0);
                gX_Shader_SetValue_Int(gfx->shader.shader, gfx->shader.locations.enableReflectionsLoc, 0);
                gX_Shader_SetValue_Float(gfx->shader.shader, gfx->shader.locations.envAmbientFactorLoc, 0.0f);
                //opencode: probes are view-independent captures too — no TAA
                //   jitter (same leak class as the dynamic cubemap above).
                gX_Shader_SetValue_V2(gfx->shader.shader, gfx->shader.locations.taaJitterLoc,
                    (gX_F32v2){0.0f, 0.0f});
                gX_F32v3 probeClear = {
                    (gX_F32)gfx->sky.color.r / 255.0f,
                    (gX_F32)gfx->sky.color.g / 255.0f,
                    (gX_F32)gfx->sky.color.b / 255.0f
                };
                g_cubemap_gfx = gfx;
                g_cubemap_hasLocal = 0;
                gX_Cubemap_RenderEx(probe->cube, gfx->reflection.probeSize,
                    probe->pos, probeClear,
                    &gfx_cubemap_draw_face,
                    probe->fboID, probe->depthRbo);
                probe->skyOnly = !g_cubemap_hasLocal;
                gX_Shader_SetValue_Int(gfx->shader.shader, gfx->shader.locations.enableLightingSpecularLoc,
                    gfx->state.ENABLE_LIGHTING_SPECULAR ? 1 : 0);
                gX_Shader_SetValue_Int(gfx->shader.shader, gfx->shader.locations.enableReflectionsLoc,
                    gfx->state.ENABLE_REFLECTIONS ? 1 : 0);
                gX_Shader_SetValue_Float(gfx->shader.shader, gfx->shader.locations.envAmbientFactorLoc,
                    gfx->state.ENABLE_ENV_AMBIENT ? gfx->state.envAmbientFactor : 0.0f);
                gX_Shader_SetValue_V2(gfx->shader.shader, gfx->shader.locations.taaJitterLoc,
                    gfx->taa.jitter);
                GX_PROFILE_ZONE_END("gfx:probe");
                probe->baked = true;
                gfx->reflection.nextProbe = (pi + 1) % gfx->reflection.probeCount;
                if (!needRefresh) {
                    gX_I1 allBaked = true;
                    for (gX_U32 qi = 0; qi < gfx->reflection.probeCount; qi++) {
                        if (!gfx->reflection.probes[qi].baked) { allBaked = false; break; }
                    }
                    gfx->reflection.probesReady = allBaked;
                }
            }
        }
    }
    }

}

//opencode: shared procedural sky draw — fullscreen (equirect=0) or the SSR
//   equirect LUT pass (equirect=1, target size = LUT size, camera-independent).
//   res uniform = target size so gl_FragCoord/res matches the render target.
//   v0.1.16 — camera params are explicit (cpos/ctarget/cup/fovy, final FOV
//   INCLUDING any multiplier): the reflection cubemap faces pass their face
//   orientation with FOV exactly 90 (the main-view multiplier must not apply).
static gX_I0 gfx_sky_upload_and_draw(gX_RendererGFX *gfx, gX_F32 w, gX_F32 h,
                                     gX_I1 equirect,
                                     gX_F32v3 cpos, gX_F32v3 ctarget, gX_F32v3 cup,
                                     gX_F32 fovy, gX_I1 ImageBackground)
{
    if (gfx->proceduralSky.shader.id == 0) return;
    if (!gfx->state.ENABLE_PROCEDURAL_SKY && !ImageBackground) return;

    //opencode: ----- procedural Rayleigh sky (sky_new.frag) -----
    //opencode: cache procedural sky uniform locations once
    static gX_U32 skyCachedShaderId = 0;
    gX_I1 skyLocsCached = (skyCachedShaderId == gfx->proceduralSky.shader.id && skyCachedShaderId != 0);
    static gX_I32 skyTimeLoc, skyFovLoc, skyResLoc, skyCposLoc, skyCtargetLoc, skyCupLoc;
    static gX_I32 skySunDirLoc, skyAtmosLoc, skyDensityLoc, skyHueLoc, skyHorizonLoc, skySunSizeLoc, skySunIntensityLoc;
    static gX_I32 skyContrastLoc;
    static gX_I32 skyStarDensityLoc, skyStarBrightnessLoc, skyStarTwinkleLoc;
    static gX_I32 skyMoonDirLoc, skyMoonSizeLoc, skyMoonBrightnessLoc;
    static gX_I32 skyPlanetCountLoc;
    static gX_I32 skyPlanetDirLocs[4], skyPlanetSizeLocs[4], skyPlanetColorLocs[4], skyPlanetBrightnessLocs[4];
    static gX_I32 skyEquirectLoc = -1;
    if (!skyLocsCached)
    {
        gX_Shader shaC = gfx->proceduralSky.shader;
        skyTimeLoc        = gX_Shader_GetLocation(shaC, "time");
        skyFovLoc         = gX_Shader_GetLocation(shaC, "cfov");
        skyResLoc         = gX_Shader_GetLocation(shaC, "res");
        skyCposLoc        = gX_Shader_GetLocation(shaC, "cpos");
        skyCtargetLoc     = gX_Shader_GetLocation(shaC, "ctarget");
        skyCupLoc         = gX_Shader_GetLocation(shaC, "cup");
        skySunDirLoc      = gX_Shader_GetLocation(shaC, "sunDir");
        skyAtmosLoc       = gX_Shader_GetLocation(shaC, "atmosphereState");
        skyDensityLoc     = gX_Shader_GetLocation(shaC, "skyDensity");
        skyHueLoc         = gX_Shader_GetLocation(shaC, "skyHue");
        skyHorizonLoc     = gX_Shader_GetLocation(shaC, "horizonGlow");
        skySunSizeLoc     = gX_Shader_GetLocation(shaC, "sunDiskSize");
        skySunIntensityLoc = gX_Shader_GetLocation(shaC, "sunDiskIntensity");
        skyContrastLoc    = gX_Shader_GetLocation(shaC, "skyContrast");
        // Starfield locations
        skyStarDensityLoc    = gX_Shader_GetLocation(shaC, "starDensity");
        skyStarBrightnessLoc = gX_Shader_GetLocation(shaC, "starBrightness");
        skyStarTwinkleLoc    = gX_Shader_GetLocation(shaC, "starTwinkleSpeed");
        // Moon locations
        skyMoonDirLoc      = gX_Shader_GetLocation(shaC, "moonDir");
        skyMoonSizeLoc     = gX_Shader_GetLocation(shaC, "moonSize");
        skyMoonBrightnessLoc = gX_Shader_GetLocation(shaC, "moonBrightness");
        // Planet locations
        skyPlanetCountLoc = gX_Shader_GetLocation(shaC, "planetCount");
        for (gX_I32 pi = 0; pi < 4; pi++)
        {
            gX_I8 name[64];
            snprintf(name, sizeof(name), "planetDir[%i]", pi);
            skyPlanetDirLocs[pi] = gX_Shader_GetLocation(shaC, name);
            snprintf(name, sizeof(name), "planetSize[%i]", pi);
            skyPlanetSizeLocs[pi] = gX_Shader_GetLocation(shaC, name);
            snprintf(name, sizeof(name), "planetColor[%i]", pi);
            skyPlanetColorLocs[pi] = gX_Shader_GetLocation(shaC, name);
            snprintf(name, sizeof(name), "planetBrightness[%i]", pi);
            skyPlanetBrightnessLocs[pi] = gX_Shader_GetLocation(shaC, name);
        }
        skyEquirectLoc    = gX_Shader_GetLocation(shaC, "skyEquirect");
        skyCachedShaderId = gfx->proceduralSky.shader.id;
        skyLocsCached = true;
    }
    gX_Shader sha = gfx->proceduralSky.shader;
    gX_Drawing_BeginShader(sha);
    //ai: reset the image selector on every draw, including capture and LUT draws.
    gX_Shader_SetValue_Int(sha, gX_Shader_GetLocation(sha, "visibleImageSky"), ImageBackground);
    if (ImageBackground)
        gX_Shader_SetValue_CubemapSlot(sha, gX_Shader_GetLocation(sha, "visibleSkyCube"),
            gfx->sky.skybox.wrapper.texture, gX_GFX_TEXUNIT_SHADOW);
    //opencode: bind uniforms AFTER BeginShader
    gX_F32 t = (gX_F32)maindata.timeElapsed;
    gX_F32v2 res = { w, h };
    gX_Shader_SetValue_Float(sha, skyTimeLoc, t);
    gX_Shader_SetValue_Float(sha, skyFovLoc, fminf(fovy, GX_SKY_FOV_MAX));
    gX_Shader_SetValue_V2(   sha, skyResLoc, res);
    gX_Shader_SetValue_V3(   sha, skyCposLoc, cpos);
    gX_Shader_SetValue_V3(   sha, skyCtargetLoc, ctarget);
    gX_Shader_SetValue_V3(   sha, skyCupLoc, cup);
    //opencode: sky sun direction — always match scene light direction
    //   The sky shader expects direction FROM scene TO sun, which is -sun.dir.
    //   Using the ephemeris here would decouple sky from scene lighting.
    gX_F32v3 skySunDir = gX_F32v3_Scale(gfx->sun.dir, -1.0f);
    gX_Shader_SetValue_V3(   sha, skySunDirLoc, skySunDir);
    gX_Shader_SetValue_V2(sha, skyAtmosLoc, (gX_F32v2){gfx->sky.altitude, gfx->sky.turbidity});
    gX_Shader_SetValue_Float(sha, skyDensityLoc, gfx->sky.density);
    gX_Shader_SetValue_Float(sha, skyHueLoc, gfx->sky.hue);
    gX_Shader_SetValue_Float(sha, skyHorizonLoc, gfx->sky.horizon); //opencode: v0.1.20 — stronger limb brightening (was 0.5; the pale band read unfinished)
    gX_Shader_SetValue_Float(sha, skySunSizeLoc, gX_GFX_SUN_DISK_SIZE);
    gX_Shader_SetValue_Float(sha, skySunIntensityLoc, gfx->sky.sunIntensity); // subtle sun disk
    gX_Shader_SetValue_Float(sha, skyContrastLoc, gfx->sky.contrast); // moderate contrast
    gX_Shader_SetValue_Float(sha, skyStarDensityLoc, gfx->sky.starDensity);
    gX_Shader_SetValue_Float(sha, skyStarBrightnessLoc, gfx->sky.starBrightness);
    gX_Shader_SetValue_Float(sha, skyStarTwinkleLoc, gfx->sky.starTwinkle);
    gX_F32v3 moonDir;
    gX_F32 moonSz = gX_GFX_MOON_SIZE;
    gX_F32 moonBr = 0.5f;
    //opencode: gX — game/ephemeris dropped; static moon defaults
    moonDir = (gX_F32v3){0.0f, 0.3f, -0.9f};
    gX_Shader_SetValue_V3(sha, skyMoonDirLoc, moonDir);
    gX_Shader_SetValue_Float(sha, skyMoonSizeLoc, moonSz);
    gX_Shader_SetValue_Float(sha, skyMoonBrightnessLoc, moonBr);
    gX_I32 plCount = 0;
    gX_F32v3 plDirs[4];
    gX_F32 plSizes[4];
    gX_F32v3 plColors[4];
    gX_F32 plBrights[4];
    //opencode: gX — game/ephemeris dropped; static planet defaults
    const gX_I8 *plNames[] = {"Venus", "Mars", "Mercury", "Jupiter"};
    for (gX_I32 pi = 0; pi < 4; pi++)
    {
        (void)plNames[pi];
        plDirs[pi] = (gX_F32v3){0.0f, 0.0f, 1.0f};
        plSizes[pi] = 200.0f;
        plColors[pi] = (gX_F32v3){1.0f, 0.8f, 0.6f};
        plBrights[pi] = 1.0f;
    }
    gX_Shader_SetValue_Int(sha, skyPlanetCountLoc, plCount);
    for (gX_I32 pi = 0; pi < 4; pi++)
    {
        gX_Shader_SetValue_V3(sha, skyPlanetDirLocs[pi], plDirs[pi]);
        gX_Shader_SetValue_Float(sha, skyPlanetSizeLocs[pi], plSizes[pi]);
        gX_Shader_SetValue_V3(sha, skyPlanetColorLocs[pi], plColors[pi]);
        gX_Shader_SetValue_Float(sha, skyPlanetBrightnessLocs[pi], plBrights[pi]);
    }
    if (skyEquirectLoc >= 0)
        gX_Shader_SetValue_Int(sha, skyEquirectLoc, equirect ? 1 : 0);
    gX_Drawing_DrawScreenQuad(0, 0, w, h);
    gX_Drawing_EndShader();
}

//ai: only the scene background may opt into the loaded image; public LUT/capture calls remain procedural.
static gX_I0 gfx_sky_draw_to(gX_RendererGFX *gfx, gX_RenderTexture target, gX_I1 equirect, gX_I1 ImageBackground)
{
    if (gfx->proceduralSky.shader.id == 0) return;
    if (!gfx->state.ENABLE_PROCEDURAL_SKY && !ImageBackground) return;
    if (target.id == 0) return;

    gX_Drawing_BeginTexture(target);
        gX_Drawing_ClearBG(gfx->sky.color);
        //opencode: rotation-only sky camera — position at origin, direction from view matrix
        gX_Matrix viewNoTrans = gX_Camera_GetViewNoTranslate(&camera.camera3D);
        gX_F32v3 skyFwd = {-viewNoTrans.m2, -viewNoTrans.m6, -viewNoTrans.m10};
        gfx_sky_upload_and_draw(gfx, (gX_F32)target.texture.width, (gX_F32)target.texture.height,
                                equirect, (gX_F32v3){0.0f, 0.0f, 0.0f}, skyFwd,
                                (gX_F32v3){0.0f, 1.0f, 0.0f},
                                camera.camera3D.fovy * GX_SKY_FOV_MULTIPLIER, ImageBackground);
    gX_Drawing_EndTexture();
}

//ai: preserve the existing procedural offscreen sky contract.
gX_I0 gX_RendererGFX_Sky_DrawTo(gX_RendererGFX *gfx, gX_RenderTexture target, gX_I1 equirect)
{
    gfx_sky_draw_to(gfx, target, equirect, 0);
}

gX_I0 gX_RendererGFX_Draw_Sky(gX_RendererGFX *gfx)
{

    //opencode: determine which sky renderer to use — procedural takes priority over cubemap
    //opencode: procedural sky is the only sky path — the cubemap skybox
    //   (gfx_sky shader + skyboxModel) was removed: its shader was never
    //   initialized (id=0) and gfx_sky.frag no longer exists, so the branch
    //   was dead/broken anyway.
    gX_I1 drawProcedural = (gfx->state.ENABLE_PROCEDURAL_SKY && gfx->proceduralSky.shader.id > 0);
    //ai: unavailable images fall back to the legacy procedural/clear background.
    gX_I1 ImageBackground = gfx->sky.visibleImageSky
        && gfx->state.ENABLE_SKYBOX && gfx->sky.skybox.wrapper.loadingState.loaded
        && gfx->sky.skybox.wrapper.texture.id > 0 && gfx->proceduralSky.shader.id > 0;

    if (drawProcedural || ImageBackground)
    {
        GX_PROFILE_ZONE_BEGIN("gfx:sky");
        //opencode: draw sky to sceneColorLDR (8-bit) when available, falling
        //   back to hdrScene (float) for the HDR pipeline. The scene target
        //   ensures the sky appears in the LDR output without float→ubyte
        //   conversion. When bloom is enabled, the sky in hdrScene is used
        //   for the HDR effect pipeline.
        gX_RenderTexture skyTarget = gfx->sceneColorLDR.wrapper.loadingState.loaded
            ? gfx->sceneColorLDR.wrapper.rt
            : gfx->bloom.hdrScene.wrapper.rt;
        gfx_sky_draw_to(gfx, skyTarget, 0, ImageBackground);
        // Clear depth only — preserve sky color in HDR RT for model pass
        gfx->second.skipColorClear = true;
        GX_PROFILE_ZONE_END("gfx:sky");
    }
    else
    {
        //opencode: no sky drawn — the seed copy (sceneColorLDR → hdrScene) in the
        //   pipeline fills the target every frame, so a color clear is safe now.
        //   Keep skipColorClear=false so the opaque pass re-clears normally.
        gfx->second.skipColorClear = false;
    }

}

//ai: opt-in precision hook queries scene/MSAA allocations before the normal opaque draw.
gX_I0 gX_RendererGFX_Draw_Opaque(gX_RendererGFX *gfx)
{

#ifndef GX_LEAN_RELEASE
    static gX_I1 DepthProbed = false;
    const gX_I8 *DepthProbe = getenv("GX_DEPTH_PRECISION_PROBE");
    if (!DepthProbed && DepthProbe && DepthProbe[0] == '1'
        && gfx->bloom.hdrScene.wrapper.loadingState.loaded && gfx->bloom.hdrMSAA.wrapper.loadingState.loaded)
    {
        DepthProbed = true;
        gX_RHI_Depth_Probe(gfx->bloom.hdrScene.wrapper.rt.depth.id,gfx->bloom.hdrMSAA.wrapper.rt.depth.id);
    }
#endif

    GX_PROFILE_ZONE_BEGIN("gfx:opaque_pass");
    gX_RendererGFX_Render_Draw(&gfx->second, gfx);
    GX_PROFILE_ZONE_END("gfx:opaque_pass");

}

gX_I0 gX_RendererGFX_Draw_Water(gX_RendererGFX *gfx)
{
    if (!gfx->state.ENABLE_WATER || !gfx->water.loadingstate.loaded) return;
    GX_PROFILE_ZONE_BEGIN("gfx:water");

    // Set GFX reference for sun/cubemap access
    gfx->water.gfxRef = gfx;

    gX_Matrix viewMat = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
    gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix projMat = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect,
        camera.frustum.near, camera.frustum.far);

    gX_RendererWater_DrawTo(&gfx->water, &gfx->bloom.hdrScene.wrapper.rt,
                             &camera.camera3D, &viewMat, &projMat);

    GX_PROFILE_ZONE_END("gfx:water");
}

//opencode: opt-in SSR normal target readback for encoding/coverage probes.
//ai: the legacy payload is GXNRM1 + little-endian width/height/channels + an
//   RGBA8 conversion in bottom-left ReadPixels order. Use the native-float RT
//   inspector for storage precision; this compatibility dump is unchanged.
//   It is inert unless GX_SSR_NORMAL_DUMP is set.
static gX_I0 gX_RendererGFX_SSR_DumpNormals(gX_RendererGFX *gfx)
{
    const gX_I8 *hook = getenv("GX_SSR_NORMAL_DUMP");
    if (!hook || hook[0] == '0') return;
    static gX_U32 dumpedTexture = 0;
    gX_U32 textureId = gfx->ssr.normalMap.wrapper.rt.texture.id;
    if (textureId == 0 || textureId == dumpedTexture) return;
    gX_U32 width = (gX_U32)gfx->ssr.normalMap.wrapper.rt.texture.width;
    gX_U32 height = (gX_U32)gfx->ssr.normalMap.wrapper.rt.texture.height;
    gX_U64 byteCount = (gX_U64)width * (gX_U64)height * 4u;
    gX_U8 *pixels = (gX_U8 *)malloc((size_t)byteCount);
    if (!pixels) {
        gX_TraceLog_GFX(gX_LOG_ERROR, "SSR_NORMAL dump allocation failed (%ux%u)", width, height);
        dumpedTexture = textureId;
        return;
    }
    gX_RHI_BindReadFBO(gfx->ssr.normalMap.wrapper.rt.id);
    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
    gX_RHI_ReadPixels(0, 0, width, height, gX_RHI_RGBA, gX_RHI_UNSIGNED_BYTE, pixels);
    gX_RHI_UnbindReadFBO();
    const gX_I8 *path = hook;
    if (!strcmp((const char *)hook, "1")) path = "/tmp/opencode/gx_ssr_normals.rgba";
    FILE *file = fopen((const char *)path, "wb");
    if (!file) {
        gX_TraceLog_GFX(gX_LOG_ERROR, "SSR_NORMAL dump open failed: %s", path);
        free(pixels);
        dumpedTexture = textureId;
        return;
    }
    const gX_I8 magic[8] = {'G', 'X', 'N', 'R', 'M', '1', 0, 0};
    gX_U32 channels = 4;
    fwrite(magic, sizeof(magic), 1, file);
    fwrite(&width, sizeof(width), 1, file);
    fwrite(&height, sizeof(height), 1, file);
    fwrite(&channels, sizeof(channels), 1, file);
    fwrite(pixels, 1, (size_t)byteCount, file);
    fclose(file);
    free(pixels);
    dumpedTexture = textureId;
    gX_TraceLog_GFX(gX_LOG_NOTICE, "SSR_NORMAL dump path=%s size=%ux%u tex=%u",
        path, width, height, textureId);
}

//opencode: opt-in SSR shading-normal pass. It reuses the production model
//ai: shader and vertex path render into a separate RGBA16F target so exact
//   encoded zero components and interpolated normals survive reconstruction.
gX_I0 gX_RendererGFX_SSR_DrawNormals(gX_RendererGFX *gfx)
{
    if (!gfx->ssr.normalBufferEnabled
        || !gfx->ssr.normalMap.wrapper.loadingState.loaded) return;

    static gX_U32 filteredTexture = 0;
    gX_U32 normalTexture = gfx->ssr.normalMap.wrapper.rt.texture.id;
    if (normalTexture != filteredTexture) {
        gX_Texture_SetFilter(gfx->ssr.normalMap.wrapper.rt.texture, gX_TEXTURE_FILTER_POINT);
        filteredTexture = normalTexture;
    }

    gX_RendererGFX_Render normalRender = gfx->second;
    gX_Color clearNormal = (gX_Color){0, 0, 0, 0};
    normalRender.rt = &gfx->ssr.normalMap.wrapper.rt;
    normalRender.clearColor = &clearNormal;
    normalRender.skipBlended = true;
    //opencode: normal RTs have their own depth attachment. Clear both buffers
    //   before the shared render loop; otherwise its first depth-tested draw
    //   sees undefined depth and coverage varies by driver/frame.
    gX_Drawing_BeginTexture(*(normalRender.rt));
        gX_Drawing_ClearBG(clearNormal);
        gX_RHI_Clear(gX_RHI_DEPTH_BUFFER_BIT | gX_RHI_STENCIL_BUFFER_BIT);
    gX_Drawing_EndTexture();
    normalRender.skipColorClear = true;
    normalRender.depthMap = false;

    gX_I1 blendWasEnabled = gX_RHI_IsEnabled(gX_RHI_BLEND);
    gX_RHI_Disable(gX_RHI_BLEND);
    gfx->ssr.normalPass = true;
    gX_RendererGFX_Render_Draw(&normalRender, gfx);
    gfx->ssr.normalPass = false;
    if (blendWasEnabled) gX_RHI_Enable(gX_RHI_BLEND);
    //ai: SDF normals compete against raster normals through this target's private depth.
    if (renderer.state.raymarchDepth == 2 && renderer.raymarch.data && renderer.raymarch.amount > 0)
        gX_RendererRaymarch_DrawSharedNormals(renderer.raymarch.data,&gfx->ssr.normalMap.wrapper.rt);
    gX_RendererGFX_SSR_DumpNormals(gfx);
}

gX_I0 gX_RendererGFX_SSR_HiZ_Chain(gX_RendererGFX *gfx)
{
    if (!gfx->ssr.hizInited || gfx->ssr.hizTex.id == 0 || gfx->ssr.hizFbo == 0) return;
    if (!gfx->ssr.hizShader.loaded || !gfx->ssr.hizShader.shader.id) return;

    gX_U32 w = maindata.renderWidth;
    gX_U32 h = maindata.renderHeight;
    gX_Batch_Flush();

    //ai: whole-chain save/restore — blend alpha, viewport, program, active
    //    texture, and the previous FBO must all survive the Hi-Z pass.
    gX_GL_State_Push();
    gX_RHI_Disable(gX_RHI_BLEND);

    gX_Shader hz = gfx->ssr.hizShader.shader;
    gX_RHI_BindFBO(gfx->ssr.hizFbo);
    gX_RHI_DrawBuffer(gX_RHI_COLOR_ATTACHMENT0);
    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
    gX_RHI_Shader_Use(hz.id);
    gX_Shader_SetValue_TextureSlot(hz, gfx->ssr.hizDepthMapLoc, gfx->bloom.hdrScene.wrapper.rt.depth, gX_GFX_TEXUNIT_DEPTH);
    gX_Shader_SetValue_TextureSlot(hz, gfx->ssr.hizHizMapLoc, gfx->ssr.hizTex, gX_GFX_TEXUNIT_HIZ);

    //opencode: level 0 — exact copy of the hdrScene depth
    gX_RHI_FBO_AttachTextureLevel(gfx->ssr.hizTex.id, gX_RHI_COLOR_ATTACHMENT0, gX_RHI_TEXTURE_2D, 0);
    gX_RHI_Viewport(0, 0, (gX_I32)w, (gX_I32)h);
    gX_Shader_SetValue_Int(hz, gfx->ssr.hizModeLoc, 0);
    gX_Shader_SetValue_Int(hz, gfx->ssr.hizSrcLevelLoc, 0);
    gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)w, (gX_F32)h);

    //opencode: levels 1..N — 2x2 MIN reduction (conservative for ray marching)
    for (gX_I32 lvl = 1; lvl < gfx->ssr.hizLevels; lvl++)
    {
        gX_U32 lw = ((gX_U32)w >> lvl) > 0 ? ((gX_U32)w >> lvl) : 1;
        gX_U32 lh = ((gX_U32)h >> lvl) > 0 ? ((gX_U32)h >> lvl) : 1;
        gX_RHI_FBO_AttachTextureLevel(gfx->ssr.hizTex.id, gX_RHI_COLOR_ATTACHMENT0, gX_RHI_TEXTURE_2D, lvl);
        gX_RHI_Viewport(0, 0, (gX_I32)lw, (gX_I32)lh);
        gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT);
        gX_Shader_SetValue_Int(hz, gfx->ssr.hizModeLoc, 1);
        gX_Shader_SetValue_Int(hz, gfx->ssr.hizSrcLevelLoc, lvl - 1);
        gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)lw, (gX_F32)lh);
    }

    gX_GL_State_Pop();
    gX_GL_DumpState("after_hiz_chain");
}

gX_I0 gX_RendererGFX_Draw_SSR(gX_RendererGFX *gfx)
{
    //opencode: SSR — screen-space reflections pass
    //opencode: hunt - the M2-era !sceneHasPlanarRefl skip is GONE: it
    //   silently disabled the whole march (plus aux fallback, vp/invVP/
    //   planarVP uploads and every diagnostic mode) whenever any planar
    //   surface existed. The march runs whenever SSR is enabled; the
    //   composite's confidence-weighted replace blend lets march content
    //   win where confident and forward-planar show through elsewhere.
    //opencode: hunt - planar defer flag: 1 when a forward-planar mirror
    //   owns some surface, so confident march hits on plane pixels defer
    //   to the exact mirror (ssrPlanarDefer uniform, uploaded with the
    //   other SSR uniforms below).
    gX_I32 ssrPlanarDefer = 0;
    {
        GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, dm) {
            if (dm->loaded && dm->params.planarReflection) { ssrPlanarDefer = 1; break; }
        }
        //opencode: hunt - GX_T_DEFER forces the defer switch for A/B runs
        { const gX_I8 *td = getenv("GX_T_DEFER"); if (td && td[0]) ssrPlanarDefer = atoi(td); }
        //opencode: hunt - one-shot gate audit (dead visualizer triage)
        {
            static gX_I1 ssrGateAudit = 0;
            if (!ssrGateAudit) { ssrGateAudit = 1;
                gX_TraceLog(gX_LOG_NOTICE, "SSRAUDIT gate ssr=%d shloaded=%d maploaded=%d hiz=%d",
                    (int)gfx->state.ENABLE_SSR, (int)gfx->ssr.shader.loaded,
                    (int)gfx->ssr.map.wrapper.loadingState.loaded,
                    (int)gfx->ssr.hizInited);
            }
        }
        if (
            gfx->state.ENABLE_SSR && gfx->ssr.shader.loaded && gfx->ssr.map.wrapper.loadingState.loaded &&
            gfx->ssr.hizInited)
        {
        //opencode: v0.1.24 — record that the map RT holds fresh output; the
        //   composite pass reads this (see ssr.h note on drewThisFrame)
        gfx->ssr.drewThisFrame = true;
        GX_PROFILE_ZONE_BEGIN("gfx:ssr");
        //opencode: SSR ray-march must WRITE (replace), not blend — the shader
        //   outputs non-premultiplied RGBA with alpha=confidence; GL blending
        //   would premultiply the map (composite then double-blends → dim
        //   reflections) and bleed stale pixels into the cleared target.
        gX_I32 ssrPB_S, ssrPB_D, ssrPB_Eq;
        gX_I1 ssrPB_Blend = gX_RHI_IsEnabled(gX_RHI_BLEND);
        gX_RHI_GetIntegerv(gX_RHI_BLEND_SRC_RGB, &ssrPB_S);
        gX_RHI_GetIntegerv(gX_RHI_BLEND_DST_RGB, &ssrPB_D);
        gX_RHI_GetIntegerv(gX_RHI_BLEND_EQUATION_RGB, &ssrPB_Eq);
        gX_RHI_Disable(gX_RHI_BLEND);
        gX_Drawing_BeginTexture(gfx->ssr.map.wrapper.rt);
            gX_Drawing_ClearBG((gX_Color){0,0,0,0});

            // Compute view-projection and inverse
            // (GX_PROFILE_ZONE_END at line 809 after the texture draw)
            gX_Matrix view = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
            gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
            gX_Matrix proj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect,
                camera.frustum.near, camera.frustum.far);
            //opencode: TAA jitter — the G-buffer was rendered with the
            //   clip-space jitter applied (standard.vert), so reconstructing
            //   positions with an unjittered VP shifts the ray start and
            //   every buffer tap sub-pixel per frame; reflections churn and
            //   TAA shows the oscillation (y2k floor 17% pixels >2/frame).
            //   Fold the same NDC translation into the SSR VP so
            //   reconstruction and taps share the jittered frame.
            gX_Matrix jitterT = gX_Matrix_Translate(gfx->taa.jitter.x, gfx->taa.jitter.y, 0.0f);
            gX_Matrix projJ = gX_Matrix_Multiply(proj, jitterT);
            gX_Matrix vp = gX_Matrix_Multiply(view, projJ);
            gX_Matrix invVP = gX_Matrix_Invert(vp);

            gX_Shader sha = gfx->ssr.shader.shader;
            gX_Drawing_BeginShader(sha);
            //ai: preserve legacy sky termination; only shared-hybrid SSR traverses background gaps.
            gX_Shader_SetValue_Int(sha,gfx->ssr.hybridGapTraversalLoc,
                renderer.state.raymarchDepth == 2);
            //opencode: bind uniforms AFTER BeginShader
            gX_Shader_SetValue_TextureSlot(sha, gfx->ssr.depthMapLoc, gfx->bloom.hdrScene.wrapper.rt.depth, gX_GFX_TEXUNIT_DEPTH);
            gX_Shader_SetValue_TextureSlot(sha, gfx->ssr.colorMapLoc, gfx->bloom.hdrScene.wrapper.rt.texture, gX_GFX_TEXUNIT_COLOR);
            //opencode: SSR normal target lives on a high texture unit; bind it
            //   through raw GL because raylib's rlSetTexture path is limited
            //   to units 0-11 on the supported GL configurations.
            if (gfx->ssr.normalBufferEnabled
                && gfx->ssr.normalMap.wrapper.loadingState.loaded
                && gfx->ssr.normalMapLoc >= 0) {
                gX_RHI_Tex_BindOnUnit(gfx->ssr.normalMap.wrapper.rt.texture.id,
                    gX_GFX_TEXUNIT_SSR_NORMAL);
                gX_RHI_Shader_Use(sha.id);
                gX_RHI_UniformInt(gfx->ssr.normalMapLoc, gX_GFX_TEXUNIT_SSR_NORMAL);
                if (gfx->ssr.hasNormalMapLoc >= 0)
                    gX_RHI_UniformInt(gfx->ssr.hasNormalMapLoc, 1);
            } else if (gfx->ssr.hasNormalMapLoc >= 0) {
                gX_RHI_Shader_Use(sha.id);
                gX_RHI_UniformInt(gfx->ssr.hasNormalMapLoc, 0);
            }
            if (gfx->ssr.normalFallbackAngleLoc >= 0) {
                const gX_I8 *fallbackHook = getenv("GX_SSR_NORMAL_FALLBACK_DEG");
                if (fallbackHook && fallbackHook[0]) {
                    gX_F32 fallbackAngle = 0.0f;
                    fallbackAngle = (gX_F32)atof((const char *)fallbackHook) * gX_DEG2RAD;
                    if (fallbackAngle < 0.0f) fallbackAngle = 0.0f;
                    if (fallbackAngle > (90.0f * gX_DEG2RAD)) fallbackAngle = 90.0f * gX_DEG2RAD;
                    gX_RHI_Uniform_Float(gfx->ssr.normalFallbackAngleLoc, fallbackAngle);
                }
            }
            if (gfx->ssr.normalLiftModeLoc >= 0) {
                const gX_I8 *liftHook = getenv("GX_SSR_NORMAL_LIFTOFF_MODE");
                if (liftHook && liftHook[0]) {
                    gX_I32 liftMode = atoi((const char *)liftHook);
                    if (liftMode < 0) liftMode = 0;
                    if (liftMode > 4) liftMode = 4;
                    gX_RHI_UniformInt(gfx->ssr.normalLiftModeLoc, liftMode);
                }
            }
            if (gfx->ssr.normalLiftHybridAngleLoc >= 0) {
                const gX_I8 *hybridHook = getenv("GX_SSR_NORMAL_LIFTOFF_DEG");
                if (hybridHook && hybridHook[0]) {
                    gX_F32 hybridAngle = (gX_F32)atof((const char *)hybridHook) * gX_DEG2RAD;
                    if (hybridAngle < 0.0f) hybridAngle = 0.0f;
                    if (hybridAngle > (90.0f * gX_DEG2RAD)) hybridAngle = 90.0f * gX_DEG2RAD;
                    gX_RHI_Uniform_Float(gfx->ssr.normalLiftHybridAngleLoc, hybridAngle);
                }
            }
            if (gfx->ssr.normalAcceptModeLoc >= 0) {
                const gX_I8 *acceptHook = getenv("GX_SSR_NORMAL_ACCEPT_MODE");
                if (acceptHook && acceptHook[0]) {
                    gX_I32 acceptMode = atoi((const char *)acceptHook);
                    if (acceptMode < 0) acceptMode = 0;
                    if (acceptMode > 1) acceptMode = 1;
                    gX_RHI_UniformInt(gfx->ssr.normalAcceptModeLoc, acceptMode);
                }
            }
            //opencode: leave near-origin rejection enabled unless an edge-ray A/B opts out.
            if (gfx->ssr.selfPlaneRejectEnabledLoc >= 0) {
                const gX_I8 *selfPlaneHook = getenv("GX_SSR_SELFPLANE_REJECT");
                if (selfPlaneHook && selfPlaneHook[0]) {
                    gX_I32 selfPlaneEnabled = atoi((const char *)selfPlaneHook) != 0;
                    gX_RHI_UniformInt(gfx->ssr.selfPlaneRejectEnabledLoc, selfPlaneEnabled);
                }
            }
            if (gfx->ssr.hizMapLoc >= 0 && gfx->ssr.hizTex.id > 0)
                gX_Shader_SetValue_TextureSlot(sha, gfx->ssr.hizMapLoc, gfx->ssr.hizTex, gX_GFX_TEXUNIT_HIZ);
            if (gfx->ssr.skyLutMapLoc >= 0 && gfx->ssr.skyLut.wrapper.loadingState.loaded)
                gX_Shader_SetValue_TextureSlot(sha, gfx->ssr.skyLutMapLoc, gfx->ssr.skyLut.wrapper.rt.texture, gX_GFX_TEXUNIT_SKY_LUT);
            //opencode: v0.1.11 — dynamic reflection cubemap as the env fallback
            //   (captures behind-camera geometry; sky LUT used only when unavailable)
            gX_I1 ssrHasEnv = (gfx->reflection.cubeUpdatedThisFrame && gfx->reflection.dynamicCubemap.id > 0);
            gX_Texture ssrEnvTex = gfx->reflection.dynamicCubemap;
            //opencode: probe grid — SSR env fallback from the nearest baked
            //   probe (better-centered than the mirror-camera capture for
            //   off-screen near content; sky-dominated exits are unaffected).
            //   envColorAt samples by direction only, so rebinding the shared
            //   SSR_ENV slot needs no shader change and no new units.
            if (gfx->reflection.probesReady && gfx->reflection.probeCount > 0) {
                gX_F32 bestD = -1.0f;
                for (gX_U32 epi = 0; epi < gfx->reflection.probeCount; epi++) {
                    gX_RendererGFX_Probe *ep = &gfx->reflection.probes[epi];
                    if (!ep->baked || ep->cube.id == 0) continue;
                    gX_F32v3 ed = gX_F32v3_Sub(camera.camera3D.position, ep->pos);
                    gX_F32 dist = gX_F32v3_Length(ed);
                    if (bestD < 0.0f || dist < bestD) {
                        bestD = dist;
                        ssrEnvTex = ep->cube;
                    }
                }
                if (bestD >= 0.0f) ssrHasEnv = true;
            }
            //opencode: SSR lab - force the sky-LUT miss path without changing
            //   the scene's forward reflection state.
            { const gX_I8 *se = getenv("GX_T_SSRENV"); if (se && se[0] == '0') ssrHasEnv = false; }
            if (gfx->ssr.envMapLoc >= 0 && ssrHasEnv)
                gX_Shader_SetValue_CubemapSlot(sha, gfx->ssr.envMapLoc, ssrEnvTex, gX_GFX_TEXUNIT_SSR_ENV);
            if (gfx->ssr.hasEnvMapLoc >= 0)
                gX_Shader_SetValue_Int(sha, gfx->ssr.hasEnvMapLoc, ssrHasEnv ? 1 : 0);
            //opencode: M1-b — velocity.w glass flag for the env-escape
            //   (dedicated SSR unit: 21 collides with FOG_LUT later in
            //   this same draw; safe default when the map is missing =
            //   march everything, pre-M1-b).
            if (gfx->velocity.map.wrapper.loadingState.loaded) {
                gX_Shader_SetValue_TextureSlot(sha, gfx->ssr.velMapLoc,
                    gfx->velocity.map.wrapper.rt.texture, gX_GFX_TEXUNIT_SSR_VELOCITY);
                if (gfx->ssr.hasVelMapLoc >= 0)
                    gX_Shader_SetValue_Int(sha, gfx->ssr.hasVelMapLoc, 1);
            } else if (gfx->ssr.hasVelMapLoc >= 0) {
                gX_Shader_SetValue_Int(sha, gfx->ssr.hasVelMapLoc, 0);
            }
            {
                const gX_I8 *sg = getenv("GX_T_SSRGLASS");
                if (gfx->ssr.glassOnLoc >= 0)
                    gX_Shader_SetValue_Int(sha, gfx->ssr.glassOnLoc,
                        (sg && sg[0] == '0') ? 0 : 1);
            }
            //opencode: v0.3.8 - fog delta LUT binding (gated on fog being
            //   enabled: an unrendered LUT would add garbage to escapes)
            if (gfx->state.ENABLE_VOLUME_FOG && gfx->ssr.fogLutMapLoc >= 0
                && gfx->ssr.fogLut.wrapper.loadingState.loaded) {
                gX_Shader_SetValue_TextureSlot(sha, gfx->ssr.fogLutMapLoc,
                    gfx->ssr.fogLut.wrapper.rt.texture, gX_GFX_TEXUNIT_FOG_LUT);
                gX_RHI_UniformInt(gfx->ssr.fogLutMapLoc, gX_GFX_TEXUNIT_FOG_LUT);
                if (gfx->ssr.hasFogLutLoc >= 0)
                    gX_RHI_UniformInt(gfx->ssr.hasFogLutLoc, 1);
            } else if (gfx->ssr.hasFogLutLoc >= 0) {
                gX_Shader_SetValue_Int(sha, gfx->ssr.hasFogLutLoc, 0);
            }
            if (gfx->ssr.edgeFadeLoc >= 0)
                gX_Shader_SetValue_Float(sha, gfx->ssr.edgeFadeLoc, gfx->ssr.edgeFade);
            if (gfx->ssr.maxBouncesLoc >= 0)
                gX_Shader_SetValue_Int(sha, gfx->ssr.maxBouncesLoc, gfx->ssr.maxBounces);
            if (gfx->ssr.bounceAttenLoc >= 0)
                gX_Shader_SetValue_Float(sha, gfx->ssr.bounceAttenLoc, gfx->ssr.bounceAtten);
            if (gfx->ssr.sunDirLoc >= 0)
                gX_Shader_SetValue_V3(sha, gfx->ssr.sunDirLoc, gX_F32v3_Scale(gfx->sun.dir, -1.0f));
            if (gfx->ssr.sunSizeLoc >= 0)
                gX_Shader_SetValue_Float(sha, gfx->ssr.sunSizeLoc, 0.55f);
            //opencode: vec4 — the generic SetValues path uploads sunColor as a gX_Color
            if (gfx->ssr.sunColorLoc >= 0)
                gX_Shader_SetValue_Color(sha, gfx->ssr.sunColorLoc, (gX_Color){255, 242, 217, 255});
            //opencode: water exclusion — SSR must not paint over the water pass's
            //   own planar reflection
            gX_I1 ssrHasWater = (gfx->state.ENABLE_WATER && gfx->water.loadingstate.loaded);
            if (gfx->ssr.hasWaterLoc >= 0)
                gX_Shader_SetValue_Int(sha, gfx->ssr.hasWaterLoc, ssrHasWater ? 1 : 0);
            if (gfx->ssr.waterYLoc >= 0)
                gX_Shader_SetValue_Float(sha, gfx->ssr.waterYLoc, gfx->water.planeHeight);
            //opencode: v0.1.15 — water SSR wave/ripple uniforms (analytic normal).
            //   Wave dirs/params match water_upload_uniforms (dirs in radians).
            if (ssrHasWater) {
                if (gfx->ssr.waterWaveCountLoc >= 0) {
                    gX_I32 waveCount = gfx->water.waveCount;
                    //opencode: SSR uses only broad waves; excluding high-frequency
                    //   trains removes their unstable specular lattice while the
                    //   forward water pass retains all six waves.
                    if (waveCount > 3) waveCount = 3;
                    gX_Shader_SetValue_Int(sha, gfx->ssr.waterWaveCountLoc, waveCount);
                }
                if (gfx->ssr.waterTimeLoc >= 0)
                    gX_Shader_SetValue_Float(sha, gfx->ssr.waterTimeLoc, (gX_F32)maindata.timeElapsed);
                for (gX_I32 wi = 0; wi < gfx->water.waveCount && wi < GX_WATER_MAX_WAVES; wi++) {
                    gX_F32v2 wdir = { cosf(gfx->water.waveDirections[wi]), sinf(gfx->water.waveDirections[wi]) };
                    if (gfx->ssr.waterWaveDirsLoc >= 0)
                        gX_Shader_SetValue_V2(sha, gfx->ssr.waterWaveDirsLoc + wi, wdir);
                    if (gfx->ssr.waterWaveAmpsLoc >= 0)
                        gX_Shader_SetValue_Float(sha, gfx->ssr.waterWaveAmpsLoc + wi, gfx->water.waveAmplitudes[wi]);
                    if (gfx->ssr.waterWaveFreqsLoc >= 0)
                        gX_Shader_SetValue_Float(sha, gfx->ssr.waterWaveFreqsLoc + wi, gfx->water.waveFrequencies[wi]);
                    if (gfx->ssr.waterWaveSpeedsLoc >= 0)
                        gX_Shader_SetValue_Float(sha, gfx->ssr.waterWaveSpeedsLoc + wi, gfx->water.waveSpeeds[wi]);
                    if (gfx->ssr.waterWaveSteepsLoc >= 0)
                        gX_Shader_SetValue_Float(sha, gfx->ssr.waterWaveSteepsLoc + wi, gfx->water.waveSteepness[wi]);
                }
                if (gfx->ssr.waterRippleCountLoc >= 0)
                    gX_Shader_SetValue_Int(sha, gfx->ssr.waterRippleCountLoc, gfx->water.rippleCount);
                for (gX_I32 ri = 0; ri < gfx->water.rippleCount && ri < GX_WATER_MAX_RIPPLES; ri++) {
                    if (gfx->ssr.waterRipplePosLoc >= 0)
                        gX_Shader_SetValue_V2(sha, gfx->ssr.waterRipplePosLoc + ri, gfx->water.ripplePos[ri]);
                    if (gfx->ssr.waterRippleAmpLoc >= 0)
                        gX_Shader_SetValue_Float(sha, gfx->ssr.waterRippleAmpLoc + ri, gfx->water.rippleAmp[ri]);
                    if (gfx->ssr.waterRippleRadiusLoc >= 0)
                        gX_Shader_SetValue_Float(sha, gfx->ssr.waterRippleRadiusLoc + ri, gfx->water.rippleRadius[ri]);
                    if (gfx->ssr.waterRipplePhaseLoc >= 0)
                        gX_Shader_SetValue_Float(sha, gfx->ssr.waterRipplePhaseLoc + ri, gfx->water.ripplePhase[ri]);
                }
            }
            gX_Shader_SetValue_Matrix(sha, gfx->ssr.vpLoc, vp);
            gX_Shader_SetValue_Matrix(sha, gfx->ssr.invVPLoc, invVP);
            gX_Shader_SetValue_V3(sha, gfx->ssr.camPosLoc, camera.camera3D.position);
            if (gfx->ssr.maxStepsLoc >= 0)
                gX_Shader_SetValue_Int(sha, gfx->ssr.maxStepsLoc, gfx->ssr.maxSteps);
            if (gfx->ssr.stepSizeLoc >= 0)
                gX_Shader_SetValue_Float(sha, gfx->ssr.stepSizeLoc, gfx->ssr.stepSize);
            if (gfx->ssr.maxDistanceLoc >= 0)
                gX_Shader_SetValue_Float(sha, gfx->ssr.maxDistanceLoc, gfx->ssr.maxDistance);
            if (gfx->ssr.depthBiasLoc >= 0)
                gX_Shader_SetValue_Float(sha, gfx->ssr.depthBiasLoc, gfx->ssr.depthBias);
            if (gfx->ssr.opacityLoc >= 0)
                gX_Shader_SetValue_Float(sha, gfx->ssr.opacityLoc, gfx->ssr.opacity);
            if (gfx->ssr.roughnessLoc >= 0)
                gX_Shader_SetValue_Float(sha, gfx->ssr.roughnessLoc, gfx->ssr.roughness);
            if (gfx->ssr.maxLodLoc >= 0)
                gX_Shader_SetValue_Float(sha, gfx->ssr.maxLodLoc, gfx->ssr.maxLod);
            if (gfx->ssr.maxDepthDeltaLoc >= 0)
                gX_Shader_SetValue_Float(sha, gfx->ssr.maxDepthDeltaLoc, gfx->ssr.maxDepthDelta);
            //opencode: preserve the existing range scale; allow opt-in distance sweeps.
            if (gfx->ssr.maxDepthDistanceScaleLoc >= 0) {
                const gX_I8 *distanceScaleHook = getenv("GX_T_MAXDEPTH_SCALE");
                if (distanceScaleHook && distanceScaleHook[0]) {
                    gX_F32 distanceScale = (gX_F32)atof((const char *)distanceScaleHook);
                    if (distanceScale < 0.0f) distanceScale = 0.0f;
                    if (distanceScale > 4.0f) distanceScale = 4.0f;
                    gX_Shader_SetValue_Float(sha, gfx->ssr.maxDepthDistanceScaleLoc, distanceScale);
                }
            }
            if (gfx->ssr.worldGapThresholdLoc >= 0)
                gX_Shader_SetValue_Float(sha, gfx->ssr.worldGapThresholdLoc, gfx->ssr.worldGapThreshold);
            if (gfx->ssr.finalDepthThresholdLoc >= 0)
                gX_Shader_SetValue_Float(sha, gfx->ssr.finalDepthThresholdLoc, gfx->ssr.finalDepthThreshold);
            if (gfx->ssr.hitFacingThresholdLoc >= 0)
                gX_Shader_SetValue_Float(sha, gfx->ssr.hitFacingThresholdLoc, gfx->ssr.hitFacingThreshold);
            //opencode: v0.3.4-auxview - bind the mirrored-camera RT so SSR
            //   miss pixels on flat dominant planes can route there instead
            //   of the parallax-less env cubemap. UVs are the unflipped
            //   projective mapping (same convention as the verified direct
            //   path; the old y-flip compensated upside-down RT content and
            //   re-inverts it now that the fill winding is fixed). Opt-out
            //   via GX_PLANAR_AUX=0 (env fallback, v0.6.26 look).
            gX_I1 planarAuxOn = 1;
            {
                const char *ae = getenv("GX_PLANAR_AUX");
                if (ae != NULL && ae[0] == '0') planarAuxOn = 0;
            }
            if (planarAuxOn && gfx->state.ENABLE_SSR_AUXVIEW
                && gfx->reflection.filledThisFrame
                && gfx->ssr.planarMapLoc >= 0) {
                gX_Texture ptex = {0};
                ptex.id = gfx->reflection.planar.wrapper.rt.texture.id;
                ptex.width = (gX_I16)gfx->reflection.planar.wrapper.rt.texture.width;
                ptex.height = (gX_I16)gfx->reflection.planar.wrapper.rt.texture.height;
                gX_Shader_SetValue_TextureSlot(sha, gfx->ssr.planarMapLoc,
                    ptex, gX_GFX_TEXUNIT_SSR_PLANAR);
                //opencode: audit — planar depth validity mask: empty mirror
                //   pixels (depth clear = far) fall through to env instead of
                //   painting the clear color as a brown veil / dark tiles.
                if (gfx->ssr.planarDepthMapLoc >= 0 && g_planarDepthTex > 0) {
                    gX_Texture pdtex = {0};
                    pdtex.id = g_planarDepthTex;
                    pdtex.width = (gX_I16)maindata.renderWidth;
                    pdtex.height = (gX_I16)maindata.renderHeight;
                    gX_Shader_SetValue_TextureSlot(sha, gfx->ssr.planarDepthMapLoc,
                        pdtex, gX_GFX_TEXUNIT_SSR_PLANAR_DEPTH);
                }
                if (gfx->ssr.hasPlanarDepthLoc >= 0)
                    gX_Shader_SetValue_Int(sha, gfx->ssr.hasPlanarDepthLoc,
                        (g_planarDepthTex > 0) ? 1 : 0);
                if (gfx->ssr.hasPlanarLoc >= 0)
                    gX_Shader_SetValue_Int(sha, gfx->ssr.hasPlanarLoc, 1);
                gX_Shader_SetValue_Matrix(sha, gfx->ssr.planarVPLoc,
                    gfx->reflection.mvp);
                if (gfx->ssr.planeDistLoc >= 0)
                    gX_Shader_SetValue_Float(sha, gfx->ssr.planeDistLoc,
                        gfx->reflection.planeDist);
                if (gfx->ssr.planeNormalLoc >= 0)
                    gX_Shader_SetValue_V3(sha, gfx->ssr.planeNormalLoc,
                        gfx->reflection.planeNormal);
            } else if (gfx->ssr.hasPlanarLoc >= 0) {
                gX_Shader_SetValue_Int(sha, gfx->ssr.hasPlanarLoc, 0);
            }
            //opencode: hunt - planar defer switch (unconditional: governs
            //   march hits whether or not the aux fallback is armed)
            if (gfx->ssr.planarDeferLoc >= 0)
                gX_Shader_SetValue_Int(sha, gfx->ssr.planarDeferLoc, ssrPlanarDefer);
            //opencode: SSR debug mode — 0=normal, 1=normals, 2=reflectDir, 3=depth,
            //   4=hitQuality, 5=stateMachine, 7=confidence
            //   ssrDebugMode > 0 overrides SHOW_SSR_VISUALIZER for AI probing
            gX_I32 dbgMode = (gfx->state.ssrDebugMode > 0) ? gfx->state.ssrDebugMode :
                             (gfx->state.SHOW_SSR_VISUALIZER ? 1 : 0);
            //opencode: hunt - one-shot debug-path audit (dead visualizer triage)
            {
                static gX_I1 dbgAudited = 0;
                if (!dbgAudited) { dbgAudited = 1;
                    gX_TraceLog(gX_LOG_NOTICE, "SSRAUDIT state=%d loc=%d prog=%u",
                        (int)dbgMode, (int)gfx->ssr.debugModeLoc,
                        (unsigned)gfx->ssr.shader.shader.id);
                }
            }
            if (gfx->ssr.debugModeLoc >= 0)
                gX_Shader_SetValue_Int(sha, gfx->ssr.debugModeLoc, dbgMode);
            //opencode: GLFW — use direct GL quad instead of rlgl batch
            gX_Drawing_DrawScreenQuad(0, 0,
                (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
            gX_Drawing_EndShader();
        gX_Drawing_EndTexture();
        gX_RHI_Blend_Equation((gX_U32)ssrPB_Eq);
        gX_RHI_Blend_Func_Separate((gX_U32)ssrPB_S, (gX_U32)ssrPB_D, (gX_U32)ssrPB_S, (gX_U32)ssrPB_D);
        if (ssrPB_Blend) gX_RHI_Enable(gX_RHI_BLEND);
        GX_PROFILE_ZONE_END("gfx:ssr");
        } else {
            //opencode: v0.1.24 — skipped frames must clear the flag so the
            //   composite pass never blends stale map content (black-frame bug)
            gfx->ssr.drewThisFrame = false;
            gX_TraceLog_GFX(gX_LOG_DEBUG, "SSR draw: SKIPPED (enable=%d shader=%d rt=%d loaded=%d)",
                gfx->state.ENABLE_SSR, gfx->ssr.shader.loaded,
                gfx->ssr.map.wrapper.loadingState.loaded, gfx->ssr.shader.shader.id);
        }
    }

}

//opencode: mark the complete refraction-background copy before transparent samples.
//ai: all active refractive surfaces, including full opacity, draw only after the same-frame copy.
gX_I0 gX_RendererGFX_Draw_Transparent(gX_RendererGFX *gfx)
{
    gfx->debugVis.refractionReady = false;

    //opencode: refractive/transparent pass — depth-sorted back-to-front
    {
        GX_PROFILE_ZONE_BEGIN("gfx:transparent");
        // Copy scene to refraction background texture
        //opencode: Phase 1b — skip the full-res copy when no refractive
        //   model exists (sole readers are refractive models, model.c:748).
        {
            gX_I1 needRefraction = 0;
            GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, rm) {
                if (!needRefraction && rm->loaded && rm->params.enableRefraction) needRefraction = 1;
            }
            if (needRefraction && gfx->debugVis.refractionBg.wrapper.loadingState.loaded
                && gfx->bloom.hdrScene.wrapper.loadingState.loaded
                && gfx->debugVis.refractionBg.wrapper.rt.id > 0
                && gfx->bloom.hdrScene.wrapper.rt.id > 0
                && gfx->debugVis.refractionBg.wrapper.rt.texture.id > 0
                && gfx->bloom.hdrScene.wrapper.rt.texture.id > 0
                && gfx->debugVis.refractionBg.wrapper.rt.texture.id != gfx->bloom.hdrScene.wrapper.rt.texture.id
                && gfx->debugVis.refractionBg.wrapper.rt.id != gfx->bloom.hdrScene.wrapper.rt.id
                && gfx->debugVis.refractionBg.wrapper.rt.texture.width == gfx->bloom.hdrScene.wrapper.rt.texture.width
                && gfx->debugVis.refractionBg.wrapper.rt.texture.height == gfx->bloom.hdrScene.wrapper.rt.texture.height)
            {
                gX_Batch_Flush();
                gX_I32 Read = 0, Draw = 0;
                gX_RHI_GetIntegerv(gX_RHI_READ_FRAMEBUFFER_BINDING, &Read);
                gX_RHI_GetIntegerv(gX_RHI_DRAW_FRAMEBUFFER_BINDING, &Draw);
                gX_I1 Scissor = gX_RHI_IsEnabled(gX_RHI_SCISSOR_TEST);
                gX_RHI_Disable(gX_RHI_SCISSOR_TEST);
                gX_U32 Width = gfx->bloom.hdrScene.wrapper.rt.texture.width;
                gX_U32 Height = gfx->bloom.hdrScene.wrapper.rt.texture.height;
                gX_RHI_Blit(gfx->bloom.hdrScene.wrapper.rt.id, gfx->debugVis.refractionBg.wrapper.rt.id,
                    0, 0, Width, Height, 0, 0, Width, Height, gX_RHI_COLOR_BUFFER_BIT, gX_RHI_NEAREST);
                gX_RHI_BindReadFBO((gX_U32)Read);
                gX_RHI_BindDrawFBO((gX_U32)Draw);
                if (Scissor) gX_RHI_Enable(gX_RHI_SCISSOR_TEST);
                gX_RHI_BufferAudit_MarkFullWrite(
                    gfx->debugVis.refractionBg.wrapper.rt.id, gX_RHI_COLOR_BUFFER_BIT);
                gfx->debugVis.refractionReady = true;
            }
        }

        // Collect transparent models and sort by camera distance (back-to-front)
        gX_RendererGFX_Model *modelPtrs = (gX_RendererGFX_Model*)gfx->models.data;
        gX_U32 transparentIndices[gX_GFX_MAX_TRANSPARENT];
        gX_F32 transparentDists[gX_GFX_MAX_TRANSPARENT];
        gX_U32 transparentCount = 0;

        gX_F32v3 camPos = camera.camera3D.position;
        for (gX_U32 mi = 0; mi < gfx->models.amount && transparentCount < gX_GFX_MAX_TRANSPARENT; mi++)
        {
            if (!gfx->models.meta[mi].USED || !modelPtrs[mi].loaded) continue;
            if (!gfx_glass_post_capture(modelPtrs[mi].params.enableRefraction,
                modelPtrs[mi].params.enableBlending, modelPtrs[mi].params.blendOpacity,
                gfx->state.shaderQuality)) continue;
            //opencode: sort by squared distance to the NEAREST point of the
            //   world-space AABB — model-center distance mis-orders large
            //   transparent objects (wide buildings, tall glass walls) when
            //   the camera is near or inside their footprint, causing wrong
            //   overlap at scene boundaries.
            if (modelPtrs[mi].aabbValid)
            {
                gX_F32 mn[3] = {
                    modelPtrs[mi].params.pos.x + modelPtrs[mi].aabbMin[0] * modelPtrs[mi].params.scale.x,
                    modelPtrs[mi].params.pos.y + modelPtrs[mi].aabbMin[1] * modelPtrs[mi].params.scale.y,
                    modelPtrs[mi].params.pos.z + modelPtrs[mi].aabbMin[2] * modelPtrs[mi].params.scale.z
                };
                gX_F32 mx[3] = {
                    modelPtrs[mi].params.pos.x + modelPtrs[mi].aabbMax[0] * modelPtrs[mi].params.scale.x,
                    modelPtrs[mi].params.pos.y + modelPtrs[mi].aabbMax[1] * modelPtrs[mi].params.scale.y,
                    modelPtrs[mi].params.pos.z + modelPtrs[mi].aabbMax[2] * modelPtrs[mi].params.scale.z
                };
                gX_F32 dx = (camPos.x < mn[0]) ? mn[0] - camPos.x : (camPos.x > mx[0]) ? camPos.x - mx[0] : 0.0f;
                gX_F32 dy = (camPos.y < mn[1]) ? mn[1] - camPos.y : (camPos.y > mx[1]) ? camPos.y - mx[1] : 0.0f;
                gX_F32 dz = (camPos.z < mn[2]) ? mn[2] - camPos.z : (camPos.z > mx[2]) ? camPos.z - mx[2] : 0.0f;
                transparentDists[transparentCount] = dx*dx + dy*dy + dz*dz;
            }
            else
            {
                gX_F32v3 delta = gX_F32v3_Sub(modelPtrs[mi].params.pos, camPos);
                transparentDists[transparentCount] = gX_F32v3_Dot(delta, delta);
            }
            transparentIndices[transparentCount] = mi;
            transparentCount++;
        }

        // Bubble sort by distance (farthest first — small N, simple sort)
        for (gX_U32 a = 0; a < transparentCount; a++)
        {
            for (gX_U32 b = a + 1; b < transparentCount; b++)
            {
                if (transparentDists[a] < transparentDists[b])
                {
                    gX_F32 tmpD = transparentDists[a];
                    transparentDists[a] = transparentDists[b];
                    transparentDists[b] = tmpD;
                    gX_U32 tmpI = transparentIndices[a];
                    transparentIndices[a] = transparentIndices[b];
                    transparentIndices[b] = tmpI;
                }
            }
        }

        // Draw transparent models back-to-front
        if (transparentCount > 0)
        {
            gX_I1 prevBlend = gX_RHI_IsEnabled(gX_RHI_BLEND);
            gX_I1 prevDepthMask = 1;
            gX_I1 prevDepthTest = gX_RHI_IsEnabled(gX_RHI_DEPTH_TEST);
            gX_RHI_GetBooleanv(gX_RHI_DEPTH_WRITEMASK, &prevDepthMask);
            gX_I32 prevBlendSrc, prevBlendDst, prevBlendEq;
            gX_I32 prevBlendSrcA, prevBlendDstA;
            gX_RHI_GetIntegerv(gX_RHI_BLEND_SRC_RGB, &prevBlendSrc);
            gX_RHI_GetIntegerv(gX_RHI_BLEND_DST_RGB, &prevBlendDst);
            gX_RHI_GetIntegerv(gX_RHI_BLEND_EQUATION_RGB, &prevBlendEq);
            gX_RHI_GetIntegerv(gX_RHI_BLEND_SRC_ALPHA, &prevBlendSrcA);
            gX_RHI_GetIntegerv(gX_RHI_BLEND_DST_ALPHA, &prevBlendDstA);
            gX_RHI_Enable(gX_RHI_BLEND);
            gX_RHI_Blend_Func_Separate(gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA, gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA);
            gX_RHI_Depth_Write(0);

            gX_RHI_Enable(gX_RHI_DEPTH_TEST);
            gX_Drawing_BeginTexture(gfx->bloom.hdrScene.wrapper.rt);
            gX_Drawing_Begin3D(camera.camera3D);

            for (gX_U32 ti = 0; ti < transparentCount; ti++)
            {
                gX_U32 mi = transparentIndices[ti];
                gX_RendererGFX_Model_Draw(&modelPtrs[mi], gfx, false);
            }

            gX_Drawing_End3D();
            gX_Drawing_EndTexture();

            //opencode: M1 - roughness-alpha repair pass. Pass 1 leaves
            //   blended opacity in hdrScene alpha, which SSR reads as
            //   roughness (pxRough corruption). Pass 2 rewrites alpha with
            //   material roughness: RGB preserved (ZERO/ONE), alpha replaced
            //   (ONE/ZERO). The roughnessAlpha uniform makes standard.frag
            //   emit ssrRoughness while keeping pass-1 coverage exactly
            //   (same blendOpacity -> same alpha-test). Same back-to-front
            //   order, so the front surface's roughness wins. Depth test
            //   unchanged (no write), so identical pixels are covered.
            gfx->state.transparentRoughnessPass = 1;
            {
                gX_RHI_Blend_Func_Separate(gX_RHI_ZERO, gX_RHI_ONE,
                    gX_RHI_ONE, gX_RHI_ZERO);
                gX_Drawing_BeginTexture(gfx->bloom.hdrScene.wrapper.rt);
                gX_Drawing_Begin3D(camera.camera3D);
                for (gX_U32 ti = 0; ti < transparentCount; ti++) {
                    gX_U32 mi = transparentIndices[ti];
                    gX_RendererGFX_Model_Draw(&modelPtrs[mi], gfx, false);
                }
                gX_Drawing_End3D();
                gX_Drawing_EndTexture();
            }
            gfx->state.transparentRoughnessPass = 0;

            if (!prevDepthTest) gX_RHI_Disable(gX_RHI_DEPTH_TEST);
            gX_RHI_Blend_Equation((gX_U32)prevBlendEq);
            gX_RHI_Blend_Func_Separate((gX_U32)prevBlendSrc, (gX_U32)prevBlendDst, (gX_U32)prevBlendSrcA, (gX_U32)prevBlendDstA);
            if (!prevBlend) gX_RHI_Disable(gX_RHI_BLEND);
            gX_RHI_Depth_Write(prevDepthMask);
        }
        gfx->debugVis.refractionReady = false;
        GX_PROFILE_ZONE_END("gfx:transparent");
    }

}

gX_I0 gX_RendererGFX_Draw_Decals(gX_RendererGFX *gfx)
{

    //opencode: decal draw — alpha-blended quads for bullet holes, blood, etc.
    if (gfx->state.ENABLE_DECALS && gfx->decals.amount > 0)
    {
        GX_PROFILE_ZONE_BEGIN("gfx:decals");
        gX_I1 prevBlend = gX_RHI_IsEnabled(gX_RHI_BLEND);
        gX_I1 prevDepthTest = gX_RHI_IsEnabled(gX_RHI_DEPTH_TEST);
        gX_I1 prevDepthMask = 1;
        gX_RHI_GetBooleanv(gX_RHI_DEPTH_WRITEMASK, &prevDepthMask);
        gX_I32 prevBlendSrc, prevBlendDst, prevBlendEq;
        gX_RHI_GetIntegerv(gX_RHI_BLEND_SRC_RGB, &prevBlendSrc);
        gX_RHI_GetIntegerv(gX_RHI_BLEND_DST_RGB, &prevBlendDst);
        gX_RHI_GetIntegerv(gX_RHI_BLEND_EQUATION_RGB, &prevBlendEq);
        gX_RHI_Enable(gX_RHI_DEPTH_TEST);
        gX_RHI_Depth_Write(0);
        gX_RHI_Enable(gX_RHI_BLEND);
        gX_RHI_Blend_Func_Separate(gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA, gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA);
        gX_Drawing_Begin3D(camera.camera3D);
        //opencode: draw through the GFX main shader (marker pattern), NOT
        //   the default material: DrawModelEx binds the default program and
        //   leaks it, darkening every later pass (verified in-capture), and
        //   reads stale rlgl matrices the RHI migration no longer maintains.
        //   Raw draws keep the active program; uploads are explicit below.
        gX_Shader sha = gfx->shader.shader;
        gX_RendererGFX_ShaderLocs *locs = &gfx->shader.locations;
        gX_RHI_Shader_Use(sha.id);
        GX_HEAP_FOREACH(gfx->decals, gX_RendererGFX_Decal, d) {
            gX_Model decalCopy = gfx->decalQuad;
            //opencode: decal transform via Multiply constructors (same
            //   idiom as the light markers - hand-written brace literals
            //   scramble translation: gX_Matrix stores column-major with
            //   interleaved slots m0,m4,m8,m12.., so a row-major literal
            //   strands pos in the perspective row and T reads (0,0,0)).
            //   Align quad +Y onto the decal normal (axis-angle; identity
            //   when aligned, 180 deg about X when opposed), spin around
            //   the normal, scale, translate. det>0 throughout.
            gX_F32v3 yAxis = {0.0f, 1.0f, 0.0f};
            gX_F32v3 axis = gX_F32v3_Cross(yAxis, d->normal);
            gX_Matrix align = gX_Matrix_Identity();
            if (gX_F32v3_Dot(axis, axis) > 0.000001f) {
                axis = gX_F32v3_Normalize(axis);
                align = gX_Matrix_Rotate(axis, gX_F32v3_Angle(yAxis, d->normal));
            } else if (d->normal.y < 0.0f) {
                align = gX_Matrix_Rotate((gX_F32v3){1.0f, 0.0f, 0.0f}, 3.14159265f);
            }
            gX_Matrix spin = gX_Matrix_Rotate(d->normal, d->rotation);
            gX_Matrix smat = gX_Matrix_Scale(d->size, d->size, d->size);
            gX_Matrix tmat = gX_Matrix_Translate(d->pos.x, d->pos.y, d->pos.z);
            decalCopy.transform = gX_Matrix_Multiply(gX_Matrix_Multiply(gX_Matrix_Multiply(smat, align), spin), tmat);
            //opencode: deterministic matte-white material (the program
            //   otherwise keeps the last model's maps/factors: stale albedo
            //   maps tinted quads red, stale cutout flags discarded them).
            //   blendOpacity carries the decal alpha (capped below 1 so the
            //   shader writes color.a instead of the SSR-roughness data
            //   channel). Textured decals resolve textureIndex through the
            //   gfx->textures heap (resource-dir texture, harness-loaded):
            //   albedo override on unit 0 via locs[15], same binding as the
            //   material path (model.c maps loop, mi=ALBEDO). Unloaded/
            //   invalid index falls back to matte (never a stale map: the
            //   hasAlbedoMap flag gates the sampler).
            gX_RendererGFX_Texture *dtex = NULL;
            //opencode: GX_T_DECALTEX=0 forces the matte fallback (textured
            //   vs matte A/B: proves the sampler reads the texture rather
            //   than a stale/white map).
            gX_I1 decalTexOn = 1;
            {
                const char *dte = getenv("GX_T_DECALTEX");
                if (dte != NULL && dte[0] == '0') decalTexOn = 0;
            }
            if (decalTexOn && d->textureIndex >= 0) {
                gX_RendererGFX_Texture *cand = gX_Heap_SeekPtr_Index(&gfx->textures,
                    (gX_U32)d->textureIndex);
                if (cand && gfx->textures.meta[d->textureIndex].USED
                    && cand->wrapper.loadingState.loaded
                    && cand->wrapper.texture.id > 0) dtex = cand;
            }
            if (locs->gX_ModelMatrixLoc >= 0)
                gX_Shader_SetValue_Matrix(sha, locs->gX_ModelMatrixLoc, decalCopy.transform);
            gX_Shader_SetValue_Color(sha, locs->albedoColorLoc, d->color);
            gX_Shader_SetValue_Float(sha, locs->albedoFactorLoc, 1.0f);
            gX_Shader_SetValue_Int(sha, locs->hasAlbedoMapLoc, dtex ? 1 : 0);
            if (dtex) {
                gX_Shader_SetValue_V2(sha, locs->albedoMapScaleLoc,
                    (gX_F32v2){1.0f, 1.0f});
                gX_Shader_SetValue_V2(sha, locs->albedoMapOffsetLoc,
                    (gX_F32v2){0.0f, 0.0f});
                gX_Shader_SetValue_Float(sha, locs->albedoMapFactorLoc, 1.0f);
                if (gfx->shader.shader.locs != NULL
                    && gfx->shader.shader.locs[15] >= 0)
                    gX_Shader_SetValue_TextureSlot(sha, gfx->shader.shader.locs[15],
                        dtex->wrapper.texture, gX_MATERIAL_MAP_ALBEDO);
            }
            gX_Shader_SetValue_Int(sha, locs->hasMetalnessMapLoc, 0);
            gX_Shader_SetValue_Int(sha, locs->hasNormalMapLoc, 0);
            gX_Shader_SetValue_Int(sha, locs->hasRoughnessMapLoc, 0);
            gX_Shader_SetValue_Int(sha, locs->hasEmissiveMapLoc, 0);
            gX_Shader_SetValue_Int(sha, locs->hasAoMapLoc, 0);
            gX_Shader_SetValue_Int(sha, locs->hasHeightMapLoc, 0);
            if (locs->hasGlossMapLoc >= 0)
                gX_Shader_SetValue_Int(sha, locs->hasGlossMapLoc, 0);
            gX_Shader_SetValue_Float(sha, locs->metalnessFactorLoc, 0.0f);
            gX_Shader_SetValue_Float(sha, locs->roughnessLoc, 1.0f);
            gX_Shader_SetValue_Float(sha, locs->modelShininessLoc, 8.0f);
            gX_Shader_SetValue_Float(sha, locs->aoLoc, 1.0f);
            gX_Shader_SetValue_Color(sha, locs->emissiveColorLoc,
                d->snowLayer ? gX_WHITE : gX_BLACK);
            gX_Shader_SetValue_Float(sha, locs->emissiveFactorLoc,
                d->snowLayer ? 0.40f : 0.0f);
            gX_Shader_SetValue_Int(sha, locs->enableAlphaTestLoc, 0);
            gX_Shader_SetValue_Int(sha, locs->enableRefractionLoc, 0);
            gX_Shader_SetValue_Int(sha, locs->planarReflectionLoc, 0);
            if (locs->nonPlanarReflectionLoc >= 0)
                gX_Shader_SetValue_Int(sha, locs->nonPlanarReflectionLoc, 0);
            gX_Shader_SetValue_Int(sha, locs->enableReflectionsLoc, 0);
            if (locs->usePBRLoc >= 0)
                gX_Shader_SetValue_Int(sha, locs->usePBRLoc, 1);
            gX_Shader_SetValue_Float(sha, locs->blendOpacityLoc,
                (d->alpha >= 1.0f) ? 0.998f : d->alpha);
            for (gX_I32 mi = 0; mi < decalCopy.meshCount; mi++) {
                if (decalCopy.meshes[mi].vaoId == 0) continue;
                gX_Raw_Mesh_Draw(decalCopy.meshes[mi]);
            }
        }
        gX_Drawing_End3D();
        if (!prevDepthTest) gX_RHI_Disable(gX_RHI_DEPTH_TEST);
        gX_RHI_Blend_Equation((gX_U32)prevBlendEq);
        gX_RHI_Blend_Func_Separate((gX_U32)prevBlendSrc, (gX_U32)prevBlendDst, (gX_U32)prevBlendSrc, (gX_U32)prevBlendDst);
        if (!prevBlend) gX_RHI_Disable(gX_RHI_BLEND);
        gX_RHI_Depth_Write(prevDepthMask);
        GX_PROFILE_ZONE_END("gfx:decals");
    }

}

gX_I0 gX_RendererGFX_Draw_SSAO(gX_RendererGFX *gfx)
{

    //opencode: SSAO compute (reads hdrScene.depth, writes ssaoBlurMap)
    if (gfx->state.ENABLE_SSAO)
    {
        GX_PROFILE_ZONE_BEGIN("gfx:ssao");
        gX_RendererGFX_SSAO_Draw(gfx);
        GX_PROFILE_ZONE_END("gfx:ssao");
    }

}

//opencode: lazily load or replace a model's custom label font. A failed load
//   falls back to the engine default; the font is released on model unload.
static gX_Font gfx_model_label_font(gX_RendererGFX_Model *model)
{
    if (strcmp(model->textFontPathLoaded, model->params.textFontPath) != 0) {
        if (model->textFontLoaded) {
            gX_Font_Unload(model->textFont);
            model->textFont = (gX_Font){0};
            model->textFontLoaded = 0;
        }
        strncpy(model->textFontPathLoaded, model->params.textFontPath,
            sizeof(model->textFontPathLoaded) - 1);
        model->textFontPathLoaded[sizeof(model->textFontPathLoaded) - 1] = 0;
        if (model->params.textFontPath[0]) {
            gX_Font font = gX_Font_Load(model->params.textFontPath);
            if (gX_Font_IsReady(font)) {
                model->textFont = font;
                model->textFontLoaded = 1;
                gX_TraceLog(gX_LOG_INFO, "GfxEd: label font loaded '%s'",
                    model->params.textFontPath);
            } else {
                gX_TraceLog(gX_LOG_WARNING, "GfxEd: label font failed '%s'",
                    model->params.textFontPath);
            }
        }
    }
    return model->textFontLoaded ? model->textFont : gX_Font_GetDefault();
}

//opencode: 3D text labels — separate pass after opaque to avoid GL state corruption
//   from raylib's text/font rendering (changes shader, depth, blend, cull state).
//   NOTE: text may not appear on MSAA FBO due to raylib's internal batch flushing.
//   If missing, use the DebugOverlays overlay target instead.
//ai: optionally fit the quad to the raster texture aspect instead of stretching its glyphs.
static gX_F32v2 gfx_label_size(const gX_RendererGFX_Model *Model)
{
    gX_F32v2 Size = Model->params.textWorldSize;
    if (!isfinite(Size.x) || Size.x <= 0.0f) Size.x = 2.0f;
    if (!isfinite(Size.y) || Size.y <= 0.0f) Size.y = 0.5f;
    if (Model->params.textFit)
    {
        if (Size.x / Size.y > 8.0f) Size.x = Size.y * 8.0f;
        else Size.y = Size.x / 8.0f;
    }
    return Size;
}

//ai: transformed local AABB corners supply a moving/rotating model's top; existing anchor mode is unchanged.
static gX_F32v3 gfx_label_position(const gX_RendererGFX_Model *Model, gX_F32v2 Size)
{
    gX_F32v3 Position = Model->params.pos;
    if (Model->params.textAnchorTop)
    {
        if (Model->aabbValid)
        {
            gX_Matrix World = gX_RendererGFX_Model_WorldMatrix(Model);
            gX_F32v3 Min = {INFINITY, INFINITY, INFINITY};
            gX_F32v3 Max = {-INFINITY, -INFINITY, -INFINITY};
            for (gX_U32 Corner = 0; Corner < 8; Corner++)
            {
                gX_F32v3 Point = {
                    Corner & 1 ? Model->aabbMax[0] : Model->aabbMin[0],
                    Corner & 2 ? Model->aabbMax[1] : Model->aabbMin[1],
                    Corner & 4 ? Model->aabbMax[2] : Model->aabbMin[2]};
                Point = gX_F32v3_Transform(Point, World);
                Min.x = fminf(Min.x, Point.x); Min.z = fminf(Min.z, Point.z);
                Max.x = fmaxf(Max.x, Point.x); Max.y = fmaxf(Max.y, Point.y);
                Max.z = fmaxf(Max.z, Point.z);
            }
            Position = (gX_F32v3){(Min.x + Max.x) * 0.5f, Max.y, (Min.z + Max.z) * 0.5f};
        }
        Position.y += fmaxf(0.0f, Model->params.textGap) + Size.y * 0.5f;
    }
    return gX_F32v3_Add(Position, Model->params.textOffset);
}

//ai: billboard and flat labels share the same authored world-axis offset and optional top anchor/style.
gX_I0 gX_RendererGFX_Draw_TextLabels(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Model *modelPtrs = (gX_RendererGFX_Model*)gfx->models.data;
    //ai: restore the caller's depth/write/blend/program/FBO/viewport state,
    //    not a guessed opaque-pass default.
    gX_GL_State_Push();
    gX_Drawing_Begin3D(camera.camera3D);
    //opencode: the billboard quads are raylib mesh draws, which read rlgl's
    //   matrices; Begin3D only sets the RHI stack, so sync rlgl to the camera
    //   state or the quads transform with stale 2D/GUI matrices.
    gX_GL_Matrix_SetProjection(gX_RHI_Matrix_GetProjection());
    gX_GL_Matrix_SetModelView(gX_RHI_Matrix_GetModelview());
    //opencode: labels are annotations — draw them with depth test and write
    //   off so a label centered on a model's origin is not hidden inside its
    //   own mesh (and cannot pollute the scene depth the SSR Hi-Z reads).
    gX_RHI_Disable(gX_RHI_DEPTH_TEST);
    gX_RHI_Depth_Write(0);
    for (gX_U32 bi = 0; bi < gfx->models.amount; bi++)
    {
        if (!gfx->models.meta[bi].USED || !modelPtrs[bi].loaded) continue;
        if (!modelPtrs[bi].params.textVisible) continue;
        if (modelPtrs[bi].params.textString[0] == '\0') continue;
        gX_Font labelFont = gfx_model_label_font(&modelPtrs[bi]);
        gX_F32v2 LabelSize = gfx_label_size(&modelPtrs[bi]);
        gX_F32v3 LabelPosition = gfx_label_position(&modelPtrs[bi], LabelSize);
        if (modelPtrs[bi].params.textBillboard) {
            gX_Billboard_Text_Draw_Styled(LabelPosition, LabelSize,
                modelPtrs[bi].params.textString, modelPtrs[bi].params.textFontSize,
                modelPtrs[bi].params.textColor, camera.camera3D.position, labelFont,
                modelPtrs[bi].params.textShadowColor, modelPtrs[bi].params.textShadowPixels);
        } else {
            gX_Text3D_Draw_Styled(LabelPosition, LabelSize,
                modelPtrs[bi].params.rotation, modelPtrs[bi].params.textString,
                modelPtrs[bi].params.textFontSize, modelPtrs[bi].params.textColor,
                labelFont, modelPtrs[bi].params.textShadowColor, modelPtrs[bi].params.textShadowPixels);
        }
    }
    gX_Drawing_End3D();
    gX_GL_State_Pop();
}

//opencode: preview one authored waypoint path — straight spans, or sampled
//   Bezier spans with dim handle lines (out and in) when the handles are
//   enabled. The in side mirrors the out side unless the handles are authored
//   independently, matching the evaluator.
static gX_I0 gfx_motion_path_preview(const gX_F32 *waypoints,
    const gX_F32 *tangents, const gX_F32 *tangentsIn, gX_I1 independent,
    gX_I1 bezier, gX_I32 count, gX_I32 maxWaypoints, gX_Color color)
{
    if (!waypoints || count < 2) return;
    if (count > maxWaypoints) count = maxWaypoints;
    for (gX_I32 wi = 0; wi + 1 < count; wi++)
    {
        gX_F32v3 from = {waypoints[wi * 3], waypoints[wi * 3 + 1],
            waypoints[wi * 3 + 2]};
        gX_F32v3 to = {waypoints[(wi + 1) * 3], waypoints[(wi + 1) * 3 + 1],
            waypoints[(wi + 1) * 3 + 2]};
        if (bezier && tangents)
        {
            gX_F32v3 handle0 = {tangents[wi * 3], tangents[wi * 3 + 1],
                tangents[wi * 3 + 2]};
            const gX_F32 *inHandle = (independent && tangentsIn)
                ? &tangentsIn[(wi + 1) * 3] : &tangents[(wi + 1) * 3];
            gX_F32v3 handle1 = {inHandle[0], inHandle[1], inHandle[2]};
            gX_F32v3 previous = from;
            for (gX_I32 step = 1; step <= 12; step++)
            {
                gX_F32 t = (gX_F32)step / 12.0f;
                gX_F32v3 point = gX_MotionPath_Bezier_Point(from, to, handle0,
                    handle1, t);
                gX_Line3D_Draw((gX_Line3D){previous, point, 0.02f}, color);
                previous = point;
            }
        }
        else
        {
            gX_Line3D_Draw((gX_Line3D){from, to, 0.02f}, color);
        }
    }
    if (bezier && tangents)
    {
        gX_Color handleColor = {color.r, color.g, color.b, 90};
        for (gX_I32 wi = 0; wi < count; wi++)
        {
            gX_F32v3 point = {waypoints[wi * 3], waypoints[wi * 3 + 1],
                waypoints[wi * 3 + 2]};
            gX_F32v3 out = {point.x + tangents[wi * 3],
                point.y + tangents[wi * 3 + 1],
                point.z + tangents[wi * 3 + 2]};
            gX_Line3D_Draw((gX_Line3D){point, out, 0.02f}, handleColor);
            const gX_F32 *inHandle = (independent && tangentsIn)
                ? &tangentsIn[wi * 3] : &tangents[wi * 3];
            gX_F32v3 in = {point.x - inHandle[0], point.y - inHandle[1],
                point.z - inHandle[2]};
            gX_Line3D_Draw((gX_Line3D){point, in, 0.02f}, handleColor);
        }
    }
}

//opencode: bind-pose bone world matrix, resolving parents in any array order
static gX_Matrix gfx_skeleton_bone_world(gX_ModelSkeleton *skel, gX_I32 bone,
    gX_Matrix *world, gX_I1 *resolved, gX_I32 count, gX_I32 depth)
{
    if (bone < 0 || bone >= count || depth > count) return gX_Matrix_Identity();
    if (resolved[bone]) return world[bone];
    gX_Transform *bind = &skel->bindPose[bone];
    gX_Matrix local = gX_Matrix_Multiply(
        gX_Matrix_Multiply(
            gX_Matrix_Scale(bind->scale.x, bind->scale.y, bind->scale.z),
            gX_F32Qua_ToMatrix(bind->rotation)),
        gX_Matrix_Translate(bind->translation.x, bind->translation.y,
            bind->translation.z));
    gX_Matrix parent = gfx_skeleton_bone_world(skel, skel->bones[bone].parent,
        world, resolved, count, depth + 1);
    world[bone] = gX_Matrix_Multiply(local, parent);
    resolved[bone] = 1;
    return world[bone];
}

//opencode: enabled point-light circle paths are previewed with other 3D helpers.
gX_I0 gX_RendererGFX_Draw_DebugOverlays(gX_RendererGFX *gfx, gX_RenderTexture *overlayTgt)
{
    gX_Drawing_BeginTexture(*overlayTgt);
        //opencode: clear canvas depth — stale from bloom composite (2D, no depth write)
        gX_RHI_Clear(gX_RHI_DEPTH_BUFFER_BIT);
        //opencode: 3D light markers — emissive spheres using the GFX main shader.
        //   Lighting and blending are disabled for readable overlay colors, then
        //   restored so subsequent 3D draws are unaffected.
        if (gfx->state.SHOW_LIGHT_MARKERS && gfx->common.sphereMarkerLoaded)
        {
            gX_Drawing_Begin3D(camera.camera3D);
            gX_RendererGFX_Light *lightPtrs = gfx->lights.data;
                gX_Shader sha = gfx->shader.shader;
                gX_RendererGFX_ShaderLocs *locs = &gfx->shader.locations;

                //opencode: disable lighting and enable emissive for markers
                gX_I1 prevAmbient  = gfx->state.ENABLE_LIGHTING_AMBIENT;
                gX_I1 prevDiffuse  = gfx->state.ENABLE_LIGHTING_DIFFUSE;
                gX_I1 prevSpecular = gfx->state.ENABLE_LIGHTING_SPECULAR;
                gX_I1 prevBlend = gX_RHI_IsEnabled(gX_RHI_BLEND);
                gX_Shader_SetValue_Int(sha, locs->enableLightingAmbientLoc, 0);
                gX_Shader_SetValue_Int(sha, locs->enableLightingDiffuseLoc, 0);
                gX_Shader_SetValue_Int(sha, locs->enableLightingSpecularLoc, 0);
                gX_Shader_SetValue_Int(sha, locs->lightMarkerModeLoc, 1);
                gX_RHI_Disable(gX_RHI_BLEND);

                for (gX_U32 i = 0; i < gfx->lights.amount; i++)
                {
                    if (!gfx->lights.meta[i].USED) continue;

                    //opencode: compact size and emissive response follow light brightness.
                    gX_F32 brightness = lightPtrs[i].brightness;
                    if (brightness < 0.0f) brightness = 0.0f;
                    gX_F32 sizeFactor = logf(1.0f + brightness)
                        / logf(1.0f + gX_GFX_MARKER_BRIGHTNESS_MAX);
                    if (sizeFactor > 1.0f) sizeFactor = 1.0f;
                    gX_F32 s = gX_GFX_MARKER_SIZE_MIN
                        + (gX_GFX_MARKER_SIZE - gX_GFX_MARKER_SIZE_MIN) * sizeFactor;
                    gX_Model sphere = gfx->common.sphereMarker;
                    gX_Matrix smat = gX_Matrix_Scale(s, s, s);
                    gX_Matrix tmat = gX_Matrix_Translate(lightPtrs[i].pos.x, lightPtrs[i].pos.y, lightPtrs[i].pos.z);
                    sphere.transform = gX_Matrix_Multiply(smat, tmat);

                    //opencode: standard.vert positions markers through gX_ModelMatrix.
                    if (locs->gX_ModelMatrixLoc >= 0)
                        gX_Shader_SetValue_Matrix(sha, locs->gX_ModelMatrixLoc, sphere.transform);

                    //opencode: set emissive glow + flat albedo
                    gX_Color litColor = lightPtrs[i].color;
                    litColor.a = 255;
                    gX_Shader_SetValue_Color(sha, locs->emissiveColorLoc, litColor);
                    gX_F32 emissive = 0.6f * brightness;
                    if (emissive > gX_GFX_MARKER_EMISSIVE_MAX)
                        emissive = gX_GFX_MARKER_EMISSIVE_MAX;
                    gX_Shader_SetValue_Float(sha, locs->emissiveFactorLoc, emissive);
                    gX_Shader_SetValue_Color(sha, locs->albedoColorLoc, litColor);
                    gX_Shader_SetValue_Float(sha, locs->albedoFactorLoc, 0.3f);

                    //opencode: draw directly so DrawModelEx cannot replace the active shader state.
                    for (gX_I32 mi = 0; mi < sphere.meshCount; mi++) {
                        if (sphere.meshes[mi].vaoId == 0) continue;
                        gX_Raw_Mesh_Draw(sphere.meshes[mi]);
                    }
                }

                //opencode: restore lighting state
                gX_Shader_SetValue_Int(sha, locs->lightMarkerModeLoc, 0);
                gX_Shader_SetValue_Int(sha, locs->enableLightingAmbientLoc, prevAmbient);
                gX_Shader_SetValue_Int(sha, locs->enableLightingDiffuseLoc, prevDiffuse);
                gX_Shader_SetValue_Int(sha, locs->enableLightingSpecularLoc, prevSpecular);
                if (prevBlend) gX_RHI_Enable(gX_RHI_BLEND);
            gX_Drawing_End3D();
        }

        //opencode: geometry shader demo — renders a test cube with geo explode effect
        if (gfx->state.ENABLE_GEO_DEMO && gfx->geoDemo.loaded && gfx->geoDemo.program.id > 0)
        {
            gX_Drawing_Begin3D(camera.camera3D);
            gX_Model m = gfx->geoDemo.model;
            // Position the cube to the right of the scene center
            gX_Matrix modelMat = gX_Matrix_Multiply(
                gX_Matrix_Scale(1.2f, 1.2f, 1.2f),
                gX_Matrix_Translate(2.5f, 0.0f, 0.0f));
            gX_Matrix viewMat = gX_Matrix_LookAt(
                camera.camera3D.position,
                camera.camera3D.target,
                camera.camera3D.up);
            gX_F32 fov = camera.camera3D.fovy * gX_DEG2RAD;
            gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
            gX_Matrix projMat = gX_Matrix_Perspective(fov, aspect, 0.1f, 100.0f);

            gX_RHI_Shader_Use(gfx->geoDemo.program.id);
            gX_Shader_SetValue_Matrix(gfx->geoDemo.program, gfx->geoDemo.matModelLoc, modelMat);
            gX_Shader_SetValue_Matrix(gfx->geoDemo.program, gfx->geoDemo.matViewLoc, viewMat);
            gX_Shader_SetValue_Matrix(gfx->geoDemo.program, gfx->geoDemo.matProjectionLoc, projMat);
            gX_Shader_SetValue_Float(gfx->geoDemo.program, gfx->geoDemo.explodeLoc, gfx->geoDemo.explodeFactor);
            gX_Shader_SetValue_V3(gfx->geoDemo.program, gfx->geoDemo.viewPosLoc, camera.camera3D.position);

            // Attach geo shader and draw
            gX_Model_AttachShader(&m, gfx->geoDemo.program);
            gX_Model_Draw_Parameters(m, (gX_Model_Parameters){
                .position = {0, 0, 0},
                .scale    = 1.0f,
                .color    = gX_WHITE
            });
            gX_Drawing_End3D();
        }

        //opencode: debug visualizations (grid, gizmo, bounding boxes)
        {
            gX_Drawing_Begin3D(camera.camera3D);
            if (gfx->state.SHOW_GRID)
            {
                gX_Grid_Draw(20, 1.0f);
            }
            gX_RendererGFX_Light *orbitLights = gfx->lights.data;
            for (gX_U32 li = 0; li < gfx->lights.amount; li++) {
                if (!gfx->lights.meta[li].USED || !orbitLights[li].orbitEnabled
                    || orbitLights[li].type != 1)
                    continue;
                gX_Color pathColor = orbitLights[li].color;
                pathColor.a = 160;
                if (orbitLights[li].pathMode == 1
                    && orbitLights[li].pathWaypointCount >= 2) {
                    gfx_motion_path_preview(orbitLights[li].pathWaypoints,
                        orbitLights[li].pathTangents,
                        orbitLights[li].pathTangentsIn,
                        orbitLights[li].pathHandlesIndependent,
                        orbitLights[li].pathBezier,
                        orbitLights[li].pathWaypointCount, gX_GFX_LIGHT_WAYPOINTS,
                        pathColor);
                    continue;
                }
                if (orbitLights[li].orbitRadius <= 0.0f) continue;
                gX_F32v3 axis = orbitLights[li].orbitPlane == 0
                    ? (gX_F32v3){0.0f, 0.0f, 1.0f}
                    : orbitLights[li].orbitPlane == 2
                        ? (gX_F32v3){1.0f, 0.0f, 0.0f}
                        : (gX_F32v3){0.0f, 1.0f, 0.0f};
                gX_Circle3D_Draw((gX_Circle3D){orbitLights[li].orbitCenter,
                    orbitLights[li].orbitRadius, axis, 0.0f}, pathColor);
            }
            //opencode: authored model motion paths share the light preview style.
            gX_RendererGFX_Model *orbitModels = (gX_RendererGFX_Model*)gfx->models.data;
            for (gX_U32 mi = 0; mi < gfx->models.amount; mi++) {
                if (!gfx->models.meta[mi].USED || !orbitModels[mi].loaded
                    || !orbitModels[mi].params.orbitEnabled)
                    continue;
                gX_RendererGFX_Model_Parameters *pathParams = &orbitModels[mi].params;
                if (pathParams->pathMode == 1 && pathParams->pathWaypointCount >= 2) {
                    gfx_motion_path_preview(pathParams->pathWaypoints,
                        pathParams->pathTangents, pathParams->pathTangentsIn,
                        pathParams->pathHandlesIndependent,
                        pathParams->pathBezier,
                        pathParams->pathWaypointCount, gX_GFX_MODEL_WAYPOINTS,
                        (gX_Color){120, 200, 255, 160});
                    continue;
                }
                if (pathParams->orbitRadius <= 0.0f) continue;
                gX_F32v3 axis = pathParams->orbitPlane == 0
                    ? (gX_F32v3){0.0f, 0.0f, 1.0f}
                    : pathParams->orbitPlane == 2
                        ? (gX_F32v3){1.0f, 0.0f, 0.0f}
                        : (gX_F32v3){0.0f, 1.0f, 0.0f};
                gX_Circle3D_Draw((gX_Circle3D){pathParams->orbitCenter,
                    pathParams->orbitRadius, axis, 0.0f},
                    (gX_Color){120, 200, 255, 160});
            }
            if (gfx->state.SHOW_GIZMO)
            {
                gX_F32 gs = 2.0f;
                gX_Line3D_Draw((gX_Line3D){{-gs,0,0},{gs,0,0},0.05f}, (gX_Color){255,0,0,255});
                gX_Line3D_Draw((gX_Line3D){{0,-gs,0},{0,gs,0},0.05f}, (gX_Color){0,255,0,255});
                gX_Line3D_Draw((gX_Line3D){{0,0,-gs},{0,0,gs},0.05f}, (gX_Color){0,0,255,255});
            }
            if (gfx->state.SHOW_BOUNDING_BOX)
            {
                gX_RendererGFX_Model *modelPtrs = (gX_RendererGFX_Model*)gfx->models.data;
                for (gX_U32 bi = 0; bi < gfx->models.amount; bi++)
                {
                    if (!gfx->models.meta[bi].USED || !modelPtrs[bi].loaded) continue;
                    gX_Cube bc = { modelPtrs[bi].params.pos, modelPtrs[bi].params.scale };
                    gX_Cube_Draw(bc, (gX_Color){255, 255, 0, 80});
                }
            }
            //opencode: bind-pose skeleton overlay for rigged models (joint cubes
            //   plus parent links), matching the inspector's read-only scope.
            if (gfx->state.SHOW_SKELETON)
            {
                gX_Matrix boneWorld[gX_GFX_SKELETON_MAX_BONES];
                gX_I1 boneResolved[gX_GFX_SKELETON_MAX_BONES];
                gX_RendererGFX_Model *skelModels = (gX_RendererGFX_Model*)gfx->models.data;
                for (gX_U32 mi = 0; mi < gfx->models.amount; mi++)
                {
                    if (!gfx->models.meta[mi].USED || !skelModels[mi].loaded) continue;
                    gX_ModelSkeleton *skel = &skelModels[mi].model.skeleton;
                    if (skel->boneCount <= 0 || !skel->bones || !skel->bindPose) continue;
                    gX_I32 count = skel->boneCount;
                    if (count > gX_GFX_SKELETON_MAX_BONES) count = gX_GFX_SKELETON_MAX_BONES;
                    for (gX_I32 bi = 0; bi < count; bi++) boneResolved[bi] = 0;
                    for (gX_I32 bi = 0; bi < count; bi++)
                        gfx_skeleton_bone_world(skel, bi, boneWorld, boneResolved, count, 0);
                    gX_Matrix trs = gX_RendererGFX_Model_WorldMatrix(&skelModels[mi]);
                    gX_F32 jointSize = gX_GFX_SKELETON_JOINT_SIZE;
                    if (skelModels[mi].aabbValid)
                    {
                        gX_F32 extent = 0.0f;
                        const gX_F32 axisScale[3] = {
                            fabsf(skelModels[mi].params.scale.x),
                            fabsf(skelModels[mi].params.scale.y),
                            fabsf(skelModels[mi].params.scale.z)
                        };
                        for (gX_I32 axis = 0; axis < 3; axis++)
                        {
                            gX_F32 size = skelModels[mi].aabbMax[axis]
                                - skelModels[mi].aabbMin[axis];
                            if (size * axisScale[axis] > extent)
                                extent = size * axisScale[axis];
                        }
                        jointSize = extent * gX_GFX_SKELETON_JOINT_RATIO;
                        if (jointSize < gX_GFX_SKELETON_JOINT_MIN)
                            jointSize = gX_GFX_SKELETON_JOINT_MIN;
                        if (jointSize > gX_GFX_SKELETON_JOINT_MAX)
                            jointSize = gX_GFX_SKELETON_JOINT_MAX;
                    }
                    for (gX_I32 bi = 0; bi < count; bi++)
                    {
                        gX_F32v3 pos = gX_F32v3_Transform((gX_F32v3){0, 0, 0},
                            boneWorld[bi]);
                        pos = gX_F32v3_Transform(pos, trs);
                        gX_Cube_Draw((gX_Cube){pos, {jointSize, jointSize, jointSize}},
                            (gX_Color){255, 196, 64, 255});
                        gX_I32 parentIndex = skel->bones[bi].parent;
                        if (parentIndex >= 0 && parentIndex < count)
                        {
                            gX_F32v3 parentPos = gX_F32v3_Transform((gX_F32v3){0, 0, 0},
                                boneWorld[parentIndex]);
                            parentPos = gX_F32v3_Transform(parentPos, trs);
                            gX_Line3D_Draw((gX_Line3D){parentPos, pos, jointSize * 0.2f},
                                (gX_Color){120, 200, 255, 255});
                        }
                    }
                }
            }
            gX_Drawing_End3D();
        }
    gX_Drawing_EndTexture();

}
