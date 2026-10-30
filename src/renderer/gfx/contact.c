/**********************************************************************************************
*
*   [gX: src/renderer/gfx/contact.c]
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


//opencode: screen-space contact shadows (spike 1). Sun-direction linear
//   depth march over a 0-2u band; metrology only (no forward integration).
//   Hook GX_T_CONTACT=1 enables (default OFF); GX_T_CONTACTSTATS=1 dumps
//   one-shot map stats. Consumed forward by later spikes (standard.frag).
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../../rhi/rhi_fbo.h"
#include "../../rhi/rhi_gl.h"

#include "../gfx.h"
#include "contact.h"
#include "../renderer.h"

#include "../../wrappers/wrappers.h"

#include "../../main.h"
#include "../../camera.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/profiler.h"
#include "../../common/utils.h"

#include "../../gxmath/matrix.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_matrix.h"

//opencode: spike-1 stats - one-shot half-res map readback (1.5MB, same
//   precedent as the composite TEX_STATS dump): contact-band coverage
//   (pixels neither fully lit nor fully dark = the signal), mean lit.
static gX_I0 Contact_DumpStats(gX_RendererGFX *gfx)
{
    gX_RenderTexture *rt = &gfx->contact.map.wrapper.rt;
    gX_U32 w = gfx->contact.map.wrapper.width;
    gX_U32 h = gfx->contact.map.wrapper.height;
    gX_U64 nPix = (gX_U64)w * (gX_U64)h;
    gX_U8 *pixels = (gX_U8 *)malloc((size_t)(nPix * 4));
    if (!pixels) return;
    gX_RHI_BindReadFBO(rt->id);
    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, (gX_I32)w, (gX_I32)h, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, pixels);
    gX_RHI_UnbindReadFBO();
    gX_U64 band = 0;
    gX_U64 dark = 0;
    gX_F64 sum = 0.0;
    for (gX_U64 i = 0; i < nPix; i++) {
        gX_F32 lit = (gX_F32)pixels[i * 4] / 255.0f;
        sum += (gX_F64)lit;
        if (lit < 0.05f) dark++;
        else if (lit < 0.95f) band++;
    }
    gX_TraceLog_GFX(gX_LOG_NOTICE,
        "CONTACT_STATS pixels=%llu meanLit=%.4f bandFrac=%.4f darkFrac=%.4f",
        (unsigned long long)nPix, sum / (gX_F64)nPix,
        (gX_F64)band / (gX_F64)nPix, (gX_F64)dark / (gX_F64)nPix);
    free(pixels);
}

gX_I0 gX_RendererGFX_Contact_CacheLocs(gX_RendererGFX *gfx)
{
    gX_Shader genS = gfx->contact.genShader.shader;
    gfx->contact.genDepthMapLoc = gX_Shader_GetLocation(genS, "sceneDepth");
    gfx->contact.genInvProjLoc  = gX_Shader_GetLocation(genS, "invProjection");
    gfx->contact.genInvViewLoc  = gX_Shader_GetLocation(genS, "invView");
    gfx->contact.genViewProjLoc = gX_Shader_GetLocation(genS, "viewProj");
    gfx->contact.genCamPosLoc   = gX_Shader_GetLocation(genS, "cameraPos");
    gfx->contact.genSunDirLoc   = gX_Shader_GetLocation(genS, "sunDir");
    gfx->contact.genResLoc      = gX_Shader_GetLocation(genS, "resolution");
    gfx->contact.genNearLoc     = gX_Shader_GetLocation(genS, "nearPlane");
    gfx->contact.genFarLoc      = gX_Shader_GetLocation(genS, "farPlane");
    gfx->contact.locsCached = true;
    gfx->contact.genShader.locsStale = false;
}

gX_I0 gX_RendererGFX_Contact_Draw(gX_RendererGFX *gfx)
{
    const gX_I8 *hook = getenv("GX_T_CONTACT");
    if (!hook || hook[0] == '\0' || hook[0] == '0') return;
    if (!gfx->contact.map.wrapper.loadingState.loaded) return;
    if (!gfx->contact.genShader.loaded) return;
    if (!gfx->bloom.hdrScene.wrapper.loadingState.loaded) return;

    GX_PROFILE_ZONE_BEGIN("gfx:contact");

    gX_I32 gw = (gX_I32)gfx->contact.map.wrapper.width;
    gX_I32 gh = (gX_I32)gfx->contact.map.wrapper.height;
    if (gw < 1 || gh < 1) { GX_PROFILE_ZONE_END("gfx:contact"); return; }

    gX_Matrix view = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
    gX_Matrix invView = gX_Matrix_Invert(view);
    gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix proj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect,
        camera.frustum.near, camera.frustum.far);
    gX_Matrix invProj = gX_Matrix_Invert(proj);
    //opencode: view then projection (row-major storage: matrices transpose
    //   in GLSL, so multiply order is reversed - same as light.c:194).
    gX_Matrix viewProj = gX_Matrix_Multiply(view, proj);
    //opencode: sun direction toward light (negate sun.dir which is direction light travels)
    gX_F32v3 sunDir = gX_F32v3_Scale(gfx->sun.dir, -1.0f);

    if (!gfx->contact.locsCached || gfx->contact.genShader.locsStale)
        gX_RendererGFX_Contact_CacheLocs(gfx);

    gX_Drawing_BeginTexture(gfx->contact.map.wrapper.rt);
        gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT | gX_RHI_DEPTH_BUFFER_BIT | gX_RHI_STENCIL_BUFFER_BIT);
        gX_Drawing_ClearBG((gX_Color){255, 255, 255, 255});
        gX_Shader genS = gfx->contact.genShader.shader;
        gX_Drawing_BeginShader(genS);
        gX_RHI_Tex_BindOnUnit(gfx->bloom.hdrScene.wrapper.rt.depth.id, gX_GFX_TEXUNIT_DEPTH);
        gX_RHI_UniformInt(gfx->contact.genDepthMapLoc, gX_GFX_TEXUNIT_DEPTH);
        gX_Shader_SetValue_Matrix(genS, gfx->contact.genInvProjLoc, invProj);
        gX_Shader_SetValue_Matrix(genS, gfx->contact.genInvViewLoc, invView);
        gX_Shader_SetValue_Matrix(genS, gfx->contact.genViewProjLoc, viewProj);
        gX_Shader_SetValue_V3(genS, gfx->contact.genCamPosLoc, camera.camera3D.position);
        gX_Shader_SetValue_V3(genS, gfx->contact.genSunDirLoc, sunDir);
        gX_Shader_SetValue_V2(genS, gfx->contact.genResLoc,
            (gX_F32v2){(gX_F32)gw, (gX_F32)gh});
        gX_Shader_SetValue_Float(genS, gfx->contact.genNearLoc, camera.frustum.near);
        gX_Shader_SetValue_Float(genS, gfx->contact.genFarLoc, camera.frustum.far);
        gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)gw, (gX_F32)gh);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();

    {
        const gX_I8 *st = getenv("GX_T_CONTACTSTATS");
        static gX_I1 dumped = false;
        if (st && st[0] != '\0' && st[0] != '0' && !dumped
            && maindata.frameCount >= 30) {
            dumped = true;
            Contact_DumpStats(gfx);
        }
    }

    GX_PROFILE_ZONE_END("gfx:contact");
}
