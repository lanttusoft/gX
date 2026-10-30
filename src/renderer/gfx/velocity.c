/**********************************************************************************************
*
*   [gX: src/renderer/gfx/velocity.c]
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


//opencode: v0.1.38 — per-pixel motion vector buffer (TAA/SSGI input).
//   Re-renders opaque geometry with a minimal shader writing the screen-space
//   UV delta between this frame and last (camera + per-model motion). Runs
//   after the opaque pass; consumers read .rg of the velocity RT.
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "../gfx.h"
#include "boilerplate.h"
#include "velocity.h"
#include "../renderer.h"

#include "../../wrappers/wrappers.h"

#include "../../main.h"
#include "../../camera.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/profiler.h"
#include "../../rhi/rhi_fbo.h"
#include "../../rhi/rhi_gl.h"
#include "../../rhi/rhi_state.h"

#include "../../gxmath/matrix.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_fbo.h"
//opencode: hunt - implicit-decl sweep

//opencode: build the SRT model matrix exactly like every other consumer
//   (model.c frustum path, planar raw path) so conventions stay identical.
static gX_Matrix velocity_model_matrix(gX_RendererGFX_Model *m)
{
    gX_Matrix smat = gX_Matrix_Scale(m->params.scale.x, m->params.scale.y, m->params.scale.z);
    gX_Matrix rmat = gX_Matrix_RotateXYZ((gX_F32v3){
        m->params.rotation.x * gX_DEG2RAD,
        m->params.rotation.y * gX_DEG2RAD,
        m->params.rotation.z * gX_DEG2RAD});
    gX_Matrix tmat = gX_Matrix_Translate(m->params.pos.x, m->params.pos.y, m->params.pos.z);
    gX_Matrix trs = gX_Matrix_Multiply(smat, rmat);
    trs = gX_Matrix_Multiply(trs, tmat);
    return trs;
}

//ai: detect camera transform changes independently of per-pixel UV thresholds.
static gX_I1 velocity_matrix_changed(gX_Matrix current, gX_Matrix previous)
{
    gX_F32 currentValues[16] = {
        current.m0, current.m1, current.m2, current.m3,
        current.m4, current.m5, current.m6, current.m7,
        current.m8, current.m9, current.m10, current.m11,
        current.m12, current.m13, current.m14, current.m15};
    gX_F32 previousValues[16] = {
        previous.m0, previous.m1, previous.m2, previous.m3,
        previous.m4, previous.m5, previous.m6, previous.m7,
        previous.m8, previous.m9, previous.m10, previous.m11,
        previous.m12, previous.m13, previous.m14, previous.m15};
    for (gX_U32 index = 0; index < 16; index++) {
        if (currentValues[index] != previousValues[index]) return true;
    }
    return false;
}

//ai: GTAO temporal accumulation also consumes this shared per-pixel velocity pass.
//ai: resolve model vectors by visible depth rather than submission order.
//opencode: optional velocity matrix/model audit trace at a selected frame.
gX_I0 gX_RendererGFX_Velocity_Draw(gX_RendererGFX *gfx)
{
    GX_PROFILE_ZONE_BEGIN("gfx:velocity");

    gX_I1 gtaoTemporalActive = gfx->state.ENABLE_SSAO && gfx->ssao.useGTAO
        && gfx->ssao.gtaoAccumShader.loaded
        && gfx->ssao.gtaoAccumA.wrapper.loadingState.loaded
        && gfx->ssao.gtaoAccumB.wrapper.loadingState.loaded;
    if (!gfx->state.ENABLE_TAA && !gfx->ssao.useSSGI && !gfx->state.ENABLE_MOTION_BLUR
        && !gtaoTemporalActive)
    {
        gfx->velocity.hasPrevVP = false;
        gfx->velocity.cameraMoved = false;
        GX_PROFILE_ZONE_END("gfx:velocity");
        return;
    }
    if (!gfx->velocity.shader.loaded || !gfx->velocity.map.wrapper.loadingState.loaded)
    {
        GX_PROFILE_ZONE_END("gfx:velocity");
        return;
    }

    extern gX_Camera camera;
    gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    //opencode: UNJITTERED VP — standard.vert adds taaJitter in clip space at
    //   render time; keeping these matrices clean lets the resolve subtract
    //   0.5*(jitter - prevJitter) explicitly.
    gX_Matrix view = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
    gX_Matrix proj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect,
        camera.frustum.near, camera.frustum.far);
    gX_Matrix currVP = gX_Matrix_Multiply(view, proj);
    gfx->velocity.cameraMoved = gfx->velocity.hasPrevVP
        && velocity_matrix_changed(currVP, gfx->velocity.prevVP);
    const gX_I8 *traceHook = getenv("GX_T_VELOCITY_TRACE");
    gX_I1 traceActive = traceHook && traceHook[0]
        && maindata.frameCount == (gX_U64)strtoull((const char *)traceHook, NULL, 10);
    if (traceActive) {
        gX_TraceLog_GFX(gX_LOG_NOTICE, "VELOCITY_TRACE cameraMoved=%d",
            (gX_I32)gfx->velocity.cameraMoved);
        gX_Matrix prevVP = gfx->velocity.hasPrevVP ? gfx->velocity.prevVP : currVP;
        gX_TraceLog_GFX(gX_LOG_NOTICE,
            "VELOCITY_TRACE frame=%llu hasPrev=%d camera=(%.9g,%.9g,%.9g) target=(%.9g,%.9g,%.9g) fovy=%.9g",
            (unsigned long long)maindata.frameCount,
            (gX_I32)gfx->velocity.hasPrevVP,
            camera.camera3D.position.x, camera.camera3D.position.y, camera.camera3D.position.z,
            camera.camera3D.target.x, camera.camera3D.target.y, camera.camera3D.target.z,
            camera.camera3D.fovy);
        gX_TraceLog_GFX(gX_LOG_NOTICE,
            "VELOCITY_TRACE currVP=[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]",
            currVP.m0, currVP.m1, currVP.m2, currVP.m3,
            currVP.m4, currVP.m5, currVP.m6, currVP.m7,
            currVP.m8, currVP.m9, currVP.m10, currVP.m11,
            currVP.m12, currVP.m13, currVP.m14, currVP.m15);
        gX_TraceLog_GFX(gX_LOG_NOTICE,
            "VELOCITY_TRACE prevVP=[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]",
            prevVP.m0, prevVP.m1, prevVP.m2, prevVP.m3,
            prevVP.m4, prevVP.m5, prevVP.m6, prevVP.m7,
            prevVP.m8, prevVP.m9, prevVP.m10, prevVP.m11,
            prevVP.m12, prevVP.m13, prevVP.m14, prevVP.m15);
    }

    gX_Drawing_BeginTexture(gfx->velocity.map.wrapper.rt);
    gX_RHI_Viewport(0, 0, (gX_I32)maindata.renderWidth, (gX_I32)maindata.renderHeight);
    gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT);
    gX_Drawing_ClearBG((gX_Color){0, 0, 0, 255});
    //opencode: M1-b — REPLACE, not blend: the buffer carries data in
    //   alpha (glass flag) and motion must overwrite regardless of it.
    //   Blended writes with alpha<1 kept the clear color (glass floor's
    //   motion + flag silently dropped).
    gX_I1 velPrevBlend = gX_RHI_IsEnabled(gX_RHI_BLEND);
    gX_I1 velPrevDepthTest = gX_RHI_IsEnabled(gX_RHI_DEPTH_TEST);
    gX_I1 velPrevDepthWrite = true;
    gX_RHI_GetBooleanv(gX_RHI_DEPTH_WRITEMASK, &velPrevDepthWrite);
    gX_RHI_Disable(gX_RHI_BLEND);
    gX_RHI_Disable(gX_RHI_DEPTH_TEST);
    gX_RHI_Depth_Write(false);

    //opencode: Paper 2 spike 3 — terrain-velocity quad under the models
    //   (camera motion for non-model pixels; models overwrite afterwards).
    gX_RendererGFX_Terrain_VelocityQuad(gfx);
    gX_RHI_Depth_Write(true);
    gX_RHI_Clear(gX_RHI_DEPTH_BUFFER_BIT);
    gX_RHI_Enable(gX_RHI_DEPTH_TEST);

    //opencode: water replay — its Gerstner/ripple mesh is drawn after this
    //   pass in the beauty pipeline, so paint its current/previous positions
    //   before models; the model pass then overwrites floating objects.
    if (gfx->state.ENABLE_WATER)
        gX_RendererWater_DrawVelocity(&gfx->water, &currVP,
            &gfx->velocity.prevVP, gfx->velocity.hasPrevVP);

    if (!gfx->velocity.locsCached || gfx->velocity.shader.locsStale)
    {
        gX_Shader sh = gfx->velocity.shader.shader;
        gfx->velocity.currVPLoc   = gX_Shader_GetLocation(sh, "matCurrVP");
        gfx->velocity.prevVPLoc   = gX_Shader_GetLocation(sh, "matPrevVP");
        gfx->velocity.modelLoc    = gX_Shader_GetLocation(sh, "gX_ModelMatrix");
        gfx->velocity.prevModelLoc= gX_Shader_GetLocation(sh, "uPrevModel");
        gfx->velocity.glassLoc    = gX_Shader_GetLocation(sh, "uGlass");
        gfx->velocity.locsCached = true;
        gfx->velocity.shader.locsStale = false;
    }

    gX_RHI_Shader_Use(gfx->velocity.shader.shader.id);

    if (gfx->velocity.currVPLoc >= 0)
        gX_RHI_UniformMatrix(gfx->velocity.currVPLoc, 1, 1, &currVP.m0);
    if (gfx->velocity.prevVPLoc >= 0 && gfx->velocity.hasPrevVP)
        gX_RHI_UniformMatrix(gfx->velocity.prevVPLoc, 1, 1, &gfx->velocity.prevVP.m0);
    else if (gfx->velocity.prevVPLoc >= 0)
        gX_RHI_UniformMatrix(gfx->velocity.prevVPLoc, 1, 1, &currVP.m0);
    if (traceActive && gfx->velocity.currVPLoc >= 0
        && gfx->velocity.prevVPLoc >= 0) {
        gX_F32 currUniform[16];
        gX_F32 prevUniform[16];
        gX_RHI_GetUniformfv(gfx->velocity.shader.shader.id,
            gfx->velocity.currVPLoc, 1, currUniform);
        gX_RHI_GetUniformfv(gfx->velocity.shader.shader.id,
            gfx->velocity.prevVPLoc, 1, prevUniform);
        gX_TraceLog_GFX(gX_LOG_NOTICE,
            "VELOCITY_TRACE uniforms program=%u loc=(%d,%d) curr=[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]",
            gfx->velocity.shader.shader.id, gfx->velocity.currVPLoc,
            gfx->velocity.prevVPLoc,
            currUniform[0], currUniform[1], currUniform[2], currUniform[3],
            currUniform[4], currUniform[5], currUniform[6], currUniform[7],
            currUniform[8], currUniform[9], currUniform[10], currUniform[11],
            currUniform[12], currUniform[13], currUniform[14], currUniform[15]);
        gX_TraceLog_GFX(gX_LOG_NOTICE,
            "VELOCITY_TRACE prevUniform=[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]",
            prevUniform[0], prevUniform[1], prevUniform[2], prevUniform[3],
            prevUniform[4], prevUniform[5], prevUniform[6], prevUniform[7],
            prevUniform[8], prevUniform[9], prevUniform[10], prevUniform[11],
            prevUniform[12], prevUniform[13], prevUniform[14], prevUniform[15]);
    }

    gX_F32 maxModelDelta = 0.0f;
    gX_U32 changedModels = 0;
    gX_U32 trackedModels = 0;
    GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, vm)
    {
        if (!vm->loaded) continue;
        gX_Matrix trs = velocity_model_matrix(vm);
        if (traceActive && vm->prevModelValid) {
            gX_F32 currModelValues[16] = {
                trs.m0, trs.m1, trs.m2, trs.m3, trs.m4, trs.m5, trs.m6, trs.m7,
                trs.m8, trs.m9, trs.m10, trs.m11, trs.m12, trs.m13, trs.m14, trs.m15};
            gX_F32 prevModelValues[16] = {
                vm->prevModelMat.m0, vm->prevModelMat.m1, vm->prevModelMat.m2,
                vm->prevModelMat.m3, vm->prevModelMat.m4, vm->prevModelMat.m5,
                vm->prevModelMat.m6, vm->prevModelMat.m7, vm->prevModelMat.m8,
                vm->prevModelMat.m9, vm->prevModelMat.m10, vm->prevModelMat.m11,
                vm->prevModelMat.m12, vm->prevModelMat.m13, vm->prevModelMat.m14,
                vm->prevModelMat.m15};
            gX_F32 modelDelta = 0.0f;
            for (gX_U32 mi = 0; mi < 16; mi++) {
                gX_F32 value = currModelValues[mi] - prevModelValues[mi];
                if (value < 0.0f) value = -value;
                if (value > modelDelta) modelDelta = value;
            }
            trackedModels++;
            if (modelDelta > maxModelDelta) maxModelDelta = modelDelta;
            if (modelDelta > 1e-6f) changedModels++;
        }
        //opencode: M1-b — transparent models flag velocity.w = 0.
        if (gfx->velocity.glassLoc >= 0)
            gX_RHI_Uniform_Float(gfx->velocity.glassLoc,
                (vm->params.enableBlending && vm->params.blendOpacity < 0.999f)
                    ? 0.0f : 1.0f);
        if (gfx->velocity.modelLoc >= 0)
            gX_RHI_UniformMatrix(gfx->velocity.modelLoc, 1, 1, &trs.m0);
        //opencode: first frame a model appears → zero object motion
        if (gfx->velocity.prevModelLoc >= 0)
        {
            if (vm->prevModelValid)
                gX_RHI_UniformMatrix(gfx->velocity.prevModelLoc, 1, 1, &vm->prevModelMat.m0);
            else
                gX_RHI_UniformMatrix(gfx->velocity.prevModelLoc, 1, 1, &trs.m0);
        }
        gX_Model m = vm->model;
        for (gX_I32 mi = 0; mi < m.meshCount; mi++) {
            if (m.meshes[mi].vaoId == 0) continue;
            gX_Raw_Mesh_Draw(m.meshes[mi]);
        }
        vm->prevModelMat = trs;
        vm->prevModelValid = true;
    }
    if (traceActive)
        gX_TraceLog_GFX(gX_LOG_NOTICE,
            "VELOCITY_TRACE models=%u changed=%u maxModelDelta=%.9g",
            trackedModels, changedModels, maxModelDelta);

    //opencode: particles render after present, so paint their current/previous
    //   point sprites after models to match the beauty-pass visibility order.
    gX_RendererGFX_Particle_DrawVelocity(gfx, &currVP,
        &gfx->velocity.prevVP, gfx->velocity.hasPrevVP);

    //opencode: store this frame's VP as next frame's previous
    gfx->velocity.prevVP = currVP;
    gfx->velocity.hasPrevVP = true;
    if (velPrevDepthTest) gX_RHI_Enable(gX_RHI_DEPTH_TEST);
    else gX_RHI_Disable(gX_RHI_DEPTH_TEST);
    gX_RHI_Depth_Write(velPrevDepthWrite);
    if (velPrevBlend) gX_RHI_Enable(gX_RHI_BLEND);

    gX_Drawing_EndTexture();
    GX_PROFILE_ZONE_END("gfx:velocity");
}
