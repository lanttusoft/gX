/**********************************************************************************************
*
*   [gX: src/renderer/gfx/gfx_render.c]
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


//opencode: Render_Draw — per-pass model iteration loop
//   Extracted from gfx.c. Iterates all models in the renderer's heap,
//   handles skipBlended/skipColorClear/depthMap state, and draws
//   each model with occlusion culling via gX_RendererGFX_Model_Draw_Occluded.
#include <string.h>
#include <math.h>

#include "../gfx.h"
#include "model.h"
#include "boilerplate.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_state.h"
#include "../../rhi/rhi_fbo.h"
#include "../../rhi/rhi_buffer_audit.h"
//opencode: hunt - implicit-decl sweep
#include "../../main.h"
#include "../../camera.h"
#include "../../gxmath/matrix.h"
#include "../../gxmath/constants.h"
#include "../../main.h"
#include "../../camera.h"
#include "../../gxmath/matrix.h"
#include "../../gxmath/constants.h"
#include "gl_level.h"
#include "glass_contract.h"

gX_I0 gX_RendererGFX_Render_Draw(gX_RendererGFX_Render *render, gX_RendererGFX *gfx) {
    gX_Camera3D cam = *render->camera;
    if (render->useLightCamera) {
        if (render->lightCameraSun) cam = gfx->sun.camera.camera3D;
    }
    gX_Drawing_BeginTexture(*(render->rt));
        gX_I1 prevCullFaceEnable = 1;
        gX_I32 prevCullFaceMode = (gX_I32)gX_RHI_BACK;
        if (render->depthMap) {
            gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT | gX_RHI_DEPTH_BUFFER_BIT | gX_RHI_STENCIL_BUFFER_BIT);
            prevCullFaceEnable = gX_RHI_IsEnabled(gX_RHI_CULL_FACE);
            gX_RHI_GetIntegerv(gX_RHI_CULL_FACE_MODE, &prevCullFaceMode);
            gX_RHI_Cull_Mode(gX_RHI_FRONT);
        } else if (!render->skipColorClear) {
            gX_Drawing_ClearBG(*(render->clearColor));
        }
        if (render->skipColorClear) {
            gX_RHI_Clear(gX_RHI_DEPTH_BUFFER_BIT);
        }
        gX_Drawing_Begin3D(cam);
            gX_RHI_Shader_Use(gfx->shader.shader.id);
            //opencode: re-upload shader uniforms at draw time
            {
                extern gX_I0 gX_RendererGFX_Shader_Update(gX_RendererGFX_Shader*, gX_RendererGFX*);
                gX_RendererGFX_Shader_Update(&gfx->shader, gfx);
                gX_Matrix vM = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
                gX_F32 a = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
                gX_Matrix pM = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, a, camera.frustum.near, camera.frustum.far);
                gX_RHI_UniformMatrix(gfx->shader.locations.matViewLoc, 1, 1, &vM.m0);
                gX_RHI_UniformMatrix(gfx->shader.locations.matProjectionLoc, 1, 1, &pM.m0);
                //opencode: verify key uniforms at draw time

            }
            {
                GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, model) {
                    gX_I1 skip = render->depthMap
                        ? (model->params.enableBlending && model->params.blendOpacity < 0.999f)
                        : (render->skipBlended && gfx_glass_post_capture(
                            model->params.enableRefraction, model->params.enableBlending,
                            model->params.blendOpacity, gfx->state.shaderQuality));
                    if (model->loaded && !skip) {
                        if (!render->depthMap) {
                            gX_RendererGFX_Model_Draw_Occluded(model, gfx, render->depthMap);
                        } else {
                            gX_RendererGFX_Model_Draw(model, gfx, render->depthMap);
                        }
                        //opencode: account for model submissions through the GFX renderer path.
                        gX_RHI_BufferAudit_Draw((const gX_I8 *)"GFXModel", 1);
                    }
                }
            }
        gX_Drawing_End3D();
        //opencode: Phase 1 — drain-and-count (was a silent swallow): the
        //   opaque pass raises one benign 0x502/frame on all drivers, but a
        //   silent drain would hide NEW errors too. Count them; report
        //   sparingly so regressions surface in the log.
        {
            static gX_U32 opaqueErrCount = 0;
            static gX_U32 opaqueErrTick = 0;
            static gX_U32 opaqueErrFirst = 0;
            gX_U32 e;
            while ((e = gX_RHI_GetError()) != gX_RHI_NO_ERROR) {
                if (opaqueErrCount == 0) opaqueErrFirst = e;
                opaqueErrCount++;
            }
            if (opaqueErrCount > 0 && (opaqueErrTick++ % 600) == 0)
                gX_TraceLog(gX_LOG_WARNING, "GFX: opaque-pass GL errors total=%u first=0x%x (one 0x502/frame is known-benign)",
                    opaqueErrCount, opaqueErrFirst);
        }
        if (render->depthMap) {
            if (prevCullFaceEnable) { gX_RHI_Enable(gX_RHI_CULL_FACE); gX_RHI_Cull_Mode((gX_U32)prevCullFaceMode); }
            else gX_RHI_Disable(gX_RHI_CULL_FACE);
        }
    gX_Drawing_EndTexture();
}
