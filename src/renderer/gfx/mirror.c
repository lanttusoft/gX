/**********************************************************************************************
*
*   [gX: src/renderer/gfx/mirror.c]
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


//opencode: two-mirror temporal feedback — bounded ping-pong regress.
//   Recursion budget is 1 stored bounce per mirror; regress depth grows
//   with settled frames and is energy-capped every commit. Validity is
//   frame-global and conservative (camera VP, size, exposure); per-pixel
//   velocity masking is deferred. Plane 0 reuses the legacy planar RT.
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "../gfx.h"
#include "mirror.h"
#include "fullscreen_pass.h"
#include "../renderer.h"

#include "../../wrappers/wrappers.h"

#include "../../main.h"
#include "../../camera.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/profiler.h"
#include "../../common/utils/shader.h"

#include "../../rhi/rhi.h"
#include "../../rhi/rhi_state.h"

#include "../../gxmath/matrix.h"
#include "../../gxmath/gxmath.h"

//opencode: pair fixture hook — scene slabs and render logic share it.
static gX_I1 Mirror_WantPair(gX_I0)
{
    const gX_I8 *hook = getenv("GX_MIRROR_PAIR");
    return (hook && hook[0] == '1') ? 1 : 0;
}

gX_I1 gX_RendererGFX_Mirror_Active(gX_RendererGFX *gfx)
{
    if (!Mirror_WantPair()) return 0;
    if (!gfx->mirror.freshB.wrapper.loadingState.loaded) return 0;
    if (!gfx->mirror.histA.wrapper.loadingState.loaded) return 0;
    if (!gfx->mirror.histB.wrapper.loadingState.loaded) return 0;
    if (!gfx->mirror.feedbackShader.loaded) return 0;
    return 1;
}

//opencode: validity gate — exact compare is deterministic under fixed
//   cameras and trips on any drift, resize, or exposure change.
gX_I0 gX_RendererGFX_Mirror_BeginFrame(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Mirror *mirror = &gfx->mirror;
    if (!Mirror_WantPair()) return;
    const gX_I8 *energyHook = getenv("GX_MIRROR_ENERGY");
    gX_F32 energy = energyHook ? (gX_F32)atof((const char *)energyHook) : 0.90f;
    if (energy < 0.0f) energy = 0.0f;
    if (energy > 1.0f) energy = 1.0f;
    mirror->energy = energy;
    gX_Matrix view = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
    gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix proj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect, camera.frustum.near, camera.frustum.far);
    gX_Matrix vp = gX_Matrix_Multiply(view, proj);
    gX_I1 same = 1;
    const gX_F32 *have = &vp.m0;
    const gX_F32 *last = &mirror->lastVP.m0;
    for (gX_I32 i = 0; i < 16; i++)
    {
        if (have[i] != last[i]) { same = 0; break; }
    }
    if (mirror->lastWidth != (gX_I32)maindata.renderWidth) same = 0;
    if (mirror->lastHeight != (gX_I32)maindata.renderHeight) same = 0;
    if (mirror->lastExposure != gfx->state.exposure) same = 0;
    mirror->lastVP = vp;
    mirror->lastWidth = (gX_I32)maindata.renderWidth;
    mirror->lastHeight = (gX_I32)maindata.renderHeight;
    mirror->lastExposure = gfx->state.exposure;
    if (!same && gX_RendererGFX_Mirror_Active(gfx))
    {
        gX_Drawing_BeginTexture(mirror->histA.wrapper.rt);
            gX_Drawing_ClearBG((gX_Color){0, 0, 0, 255});
        gX_Drawing_EndTexture();
        gX_Drawing_BeginTexture(mirror->histB.wrapper.rt);
            gX_Drawing_ClearBG((gX_Color){0, 0, 0, 255});
        gX_Drawing_EndTexture();
        gX_TraceLog_GFX(gX_LOG_NOTICE, "MIRROR: history cleared (camera/size/exposure change)");
    }
    mirror->historyValid = same;
    mirror->renderingPlane = -1;
    return;
}

gX_RenderTexture *gX_RendererGFX_Mirror_PlaneTarget(gX_RendererGFX *gfx, gX_I32 plane)
{
    if (plane == 1) return &gfx->mirror.freshB.wrapper.rt;
    return &gfx->reflection.planar.wrapper.rt;
}

//opencode: plane 0 keeps the legacy planar RT out of this module — the
//   caller (draw.c) owns it for oracle parity. Only plane 1+ live here.
gX_I0 gX_RendererGFX_Mirror_StoreMvp(gX_RendererGFX *gfx, gX_I32 plane, const gX_Matrix *mvp)
{
    if (plane == 1) gfx->mirror.mvpB = *mvp;
    else gfx->mirror.mvpA = *mvp;
    return;
}

gX_I1 gX_RendererGFX_Mirror_IsCounterpart(gX_RendererGFX *gfx, gX_I32 mirrorPair)
{
    if (!gX_RendererGFX_Mirror_Active(gfx)) return 0;
    if (gfx->mirror.renderingPlane < 0) return 0;
    if (mirrorPair <= 0) return 0;
    if ((mirrorPair - 1) == gfx->mirror.renderingPlane) return 0;
    return 1;
}

//opencode: feedback commit — fresh bounces to history with energy decay.
//   Runs after both plane renders, inside the reflections stage.
gX_I0 gX_RendererGFX_Mirror_Commit(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Mirror *mirror = &gfx->mirror;
    if (!gX_RendererGFX_Mirror_Active(gfx)) return;
    if (!mirror->locsCached)
    {
        gX_Shader feedback = mirror->feedbackShader.shader;
        mirror->histMapLoc = gX_Shader_GetLocation(feedback, "colorMap");
        mirror->energyLoc = gX_Shader_GetLocation(feedback, "energy");
        mirror->locsCached = 1;
    }
    GX_PROFILE_ZONE_BEGIN("gfx:mirror_feedback");
    for (gX_I32 plane = 0; plane < 2; plane++)
    {
        gX_RenderTexture *fresh = (plane == 1)
            ? &mirror->freshB.wrapper.rt : &gfx->reflection.planar.wrapper.rt;
        gX_RenderTexture *hist = (plane == 1)
            ? &mirror->histB.wrapper.rt : &mirror->histA.wrapper.rt;
        if (plane == 0 && !gfx->reflection.planar.wrapper.loadingState.loaded) continue;
        gX_Shader feedback = mirror->feedbackShader.shader;
        gX_RendererGFX_Fullscreen_Begin(*hist, feedback);
            gX_Shader_SetValue_TextureSlot(feedback, mirror->histMapLoc,
                fresh->texture, gX_GFX_TEXUNIT_SCENE);
            gX_Shader_SetValue_Float(feedback, mirror->energyLoc, mirror->energy);
            gX_RendererGFX_Fullscreen_Draw(0, 0,
                (gX_F32)hist->texture.width, (gX_F32)hist->texture.height);
        gX_RendererGFX_Fullscreen_End();
    }
    mirror->renderingPlane = -1;
    GX_PROFILE_ZONE_END("gfx:mirror_feedback");
    return;
}

gX_Texture gX_RendererGFX_Mirror_FreshTex(gX_RendererGFX *gfx, gX_I32 plane)
{
    if (plane == 1) return gfx->mirror.freshB.wrapper.rt.texture;
    return gfx->reflection.planar.wrapper.rt.texture;
}

gX_Matrix gX_RendererGFX_Mirror_FreshMvp(gX_RendererGFX *gfx, gX_I32 plane)
{
    if (plane == 1) return gfx->mirror.mvpB;
    return gfx->reflection.mvp;
}

gX_Texture gX_RendererGFX_Mirror_HistTex(gX_RendererGFX *gfx, gX_I32 plane)
{
    if (plane == 1) return gfx->mirror.histB.wrapper.rt.texture;
    return gfx->mirror.histA.wrapper.rt.texture;
}

gX_Matrix gX_RendererGFX_Mirror_HistMvp(gX_RendererGFX *gfx, gX_I32 plane)
{
    if (plane == 1) return gfx->mirror.mvpB;
    return gfx->mirror.mvpA;
}
