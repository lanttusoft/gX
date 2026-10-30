/**********************************************************************************************
*
*   [gX: src/renderer/gfx/motionblur.c]
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


//opencode: screen-space motion blur — velocity from depth, post-process blur
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "../gfx.h"
#include "motionblur.h"
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
#include "../../rhi/rhi_state.h"
//opencode: hunt - implicit-decl sweep

//ai: opt-in snapshot probe stamps actual motion-blur completion before the TAA consumer.
gX_I0 gX_RendererGFX_MotionBlur_Draw(gX_RendererGFX *gfx)
{
    GX_PROFILE_ZONE_BEGIN("gfx:motionblur");

    if (!gfx->state.ENABLE_MOTION_BLUR || !gfx->motionBlur.shader.loaded
        || !gfx->motionBlur.map.wrapper.loadingState.loaded
        || !gfx->bloom.hdrScene.wrapper.loadingState.loaded)
    {
        GX_PROFILE_ZONE_END("gfx:motionblur");
        return;
    }

    gX_RenderTexture *mblurSrc = gfx->common.compositeTarget ? gfx->common.compositeTarget : &renderer.canvas;

    // Compute inverse VP for world position reconstruction
    gX_Matrix view = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
    gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix proj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect,
        camera.frustum.near, camera.frustum.far);
    gX_Matrix vp = gX_Matrix_Multiply(view, proj);
    gX_Matrix invVP = gX_Matrix_Invert(vp);

    gX_Drawing_BeginTexture(gfx->motionBlur.map.wrapper.rt);
        gX_Shader mbS = gfx->motionBlur.shader.shader;
        gX_Drawing_BeginShader(mbS);
        //opencode: bind uniforms AFTER BeginShader
        gX_Shader_SetValue_TextureSlot(mbS, gfx->motionBlur.colorMapLoc, mblurSrc->texture, gX_GFX_TEXUNIT_COLOR);
        gX_Shader_SetValue_TextureSlot(mbS, gfx->motionBlur.depthMapLoc, gfx->bloom.hdrScene.wrapper.rt.depth, gX_GFX_TEXUNIT_DEPTH);
        gX_Shader_SetValue_Matrix(mbS, gfx->motionBlur.prevVPLoc, gfx->motionBlurPrevVP);
        gX_Shader_SetValue_Matrix(mbS, gfx->motionBlur.invVPLoc, invVP);
        gX_Shader_SetValue_V2(mbS, gfx->motionBlur.resolutionLoc,
            (gX_F32v2){(gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight});
        gX_Shader_SetValue_Int(mbS, gfx->motionBlur.samplesLoc, gfx->motionBlur.maxSamples);
        gX_Shader_SetValue_Float(mbS, gfx->motionBlur.strengthLoc, gfx->motionBlur.strength);
        //opencode: per-object motion — velocity buffer when this frame
        //   rendered it (same gate as the TAA resolve); the depth path
        //   stays as fallback for first frames, glass, and buffer-off.
        //   GX_T_NOVELOCITY=1 forces the fallback (velocity-vs-fallback
        //   A/B, same hook as the TAA resolve).
        gX_I1 noVelocity = (getenv("GX_T_NOVELOCITY") != NULL
            && getenv("GX_T_NOVELOCITY")[0] != '0');
        if (!noVelocity && gfx->velocity.map.wrapper.loadingState.loaded && gfx->velocity.hasPrevVP) {
            if (gfx->motionBlur.velocityMapLoc >= 0)
                gX_Shader_SetValue_TextureSlot(mbS, gfx->motionBlur.velocityMapLoc,
                    gfx->velocity.map.wrapper.rt.texture, gX_GFX_TEXUNIT_VELOCITY);
            if (gfx->motionBlur.hasVelocityLoc >= 0)
                gX_Shader_SetValue_Int(mbS, gfx->motionBlur.hasVelocityLoc, 1);
        } else if (gfx->motionBlur.hasVelocityLoc >= 0) {
            gX_Shader_SetValue_Int(mbS, gfx->motionBlur.hasVelocityLoc, 0);
        }
            gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();

    // Copy motion blur result back to composited target (defensively disable blend)
    gX_Drawing_BeginTexture(*mblurSrc);
        gX_I1 prevBlendMB = gX_RHI_IsEnabled(gX_RHI_BLEND);
        gX_RHI_Disable(gX_RHI_BLEND);
        gX_Texture_Draw(gfx->motionBlur.map.wrapper.rt.texture, (gX_Texture_Draw_Params){
            .dst = (gX_Rectangle){0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight},
            .tint = gX_WHITE,
            .scale = {1, 1}
        });
        if (prevBlendMB) gX_RHI_Enable(gX_RHI_BLEND);
    gX_Drawing_EndTexture();

    // Store current VP as prevVP for next frame
    gfx->motionBlurPrevVP = vp;

    const gX_I8 *SnapshotTrace = getenv("GX_T_TAA_SNAPSHOT_TRACE");
    if (SnapshotTrace && maindata.frameCount == (gX_U64)strtoull((const char *)SnapshotTrace,NULL,10))
        gX_TraceLog_GFX(gX_LOG_NOTICE,"TAA_SNAPSHOT_TRACE frame=%llu motionBlurCommitted=1",
            (unsigned long long)maindata.frameCount);

    GX_PROFILE_ZONE_END("gfx:motionblur");
}
