/**********************************************************************************************
*
*   [gX: src/renderer/gfx/fog_volume.c]
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


//opencode: volumetric fog — screen-space ray-march through height-based density
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../gfx.h"
#include "fog_volume.h"
#include "../renderer.h"

#include "../../wrappers/wrappers.h"

#include "../../main.h"
#include "../../camera.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/profiler.h"


#include "../../gxmath/matrix.h"
#include "../../gxmath/constants.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_state.h"
#include "../../rhi/rhi_tex.h"
#include "../../rhi/rhi_fbo.h"
#include "../../rhi/batch.h"
//opencode: hunt - implicit-decl sweep
//opencode: hunt - implicit-decl sweep
#include "../../rhi/renderdoc_hook.h"
#include "../../rhi/rhi_gl.h"
//opencode: v0.3.8 - per-frame cascade copies (extracted so the fog LUT
//   pass and the main gen share one refresh per frame)
gX_I0 gX_RendererGFX_FogVolume_UpdateCascadeCopies(gX_RendererGFX *gfx)
{
    if (!gfx->state.ENABLE_VOLUME_FOG) return;
    if (gfx->fogVolume.copiesFrame == maindata.frameCount) return;
    gfx->fogVolume.copiesFrame = maindata.frameCount;
    if (!gfx->csm.inited || gfx->csm.cascadeCount < 1) return;
    if (!gfx->fogVolume.copiesInited) {
        for (int ci = 0; ci < 4; ci++) {
            gX_U32 tex = gX_RHI_Tex_Gen(gX_RHI_TEXTURE_2D, "fog_csm_copy");
            gX_RHI_Tex_Storage2D(tex, gX_RHI_TEXTURE_2D, 1, gX_RHI_DEPTH_COMPONENT24,
                gX_GFX_SHADOW_MAP_SIZE, gX_GFX_SHADOW_MAP_SIZE);
            gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_NEAREST);
            gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_NEAREST);
            gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_BORDER);
            gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_BORDER);
            gX_F32 borderC[] = {1.0f, 1.0f, 1.0f, 1.0f};
            gX_RHI_Tex_Paramfv(tex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_BORDER_COLOR, borderC);
            gX_U32 fbo = 0;
            gX_RHI_FBO_Gen(1, &fbo);
            gX_RHI_BindFBO(fbo);
            gX_RHI_FBO_AttachTexture(tex, gX_RHI_DEPTH_ATTACHMENT, gX_RHI_TEXTURE_2D);
            gfx->fogVolume.csmCopyTex[ci] = tex;
            gfx->fogVolume.csmCopyFbo[ci] = fbo;
        }
        gX_RHI_FBO_Gen(1, &gfx->fogVolume.csmSrcFbo);
        gfx->fogVolume.copiesInited = true;
    }
    gX_F32 blitW = (gX_F32)gX_GFX_SHADOW_MAP_SIZE, blitH = (gX_F32)gX_GFX_SHADOW_MAP_SIZE;
    //opencode: cascade 0 lives in sun.camera.map, not csm.array layer 0.
    for (int ci = 1; ci < 4; ci++) {
        int layer = ci;
        if (layer >= gfx->csm.cascadeCount) break;
        gX_RHI_BindFBO(gfx->fogVolume.csmSrcFbo);
        gX_RHI_FBO_AttachLayer(gfx->csm.arrayTexId, gX_RHI_DEPTH_ATTACHMENT, 0, layer);
        gX_RHI_Blit(gfx->fogVolume.csmSrcFbo, gfx->fogVolume.csmCopyFbo[ci],
            0, 0, (gX_U32)blitW, (gX_U32)blitH,
            0, 0, (gX_U32)blitW, (gX_U32)blitH,
            gX_RHI_DEPTH_BUFFER_BIT, gX_RHI_NEAREST);
    }
}

//opencode: point-shadow atlas refresh — plain-2D tile copies of the point
//   array layers for shadow-casting dense lights (the fog gen program
//   cannot declare sampler2DArray — M3.1 poison — so layers are blitted
//   per frame like the CSM cascade copies above). The dense->heap walk
//   mirrors FogVolume_UploadPoints order exactly; beyond
//   GX_FOGVOL_MAX_PT_SHADOW casters the rest stay unshadowed (tile.w=0).
//   GX_T_FOGPTSHADOW=0 zeroes the tiles (clean unshadowed A/B).
static gX_I0 FogVolume_UpdatePointShadowCopies(gX_RendererGFX *gfx)
{
    gX_I32 n;
    gX_I32 slots;
    const gX_I8 *hook;
    gX_U32 p;
    if (!gfx->state.ENABLE_VOLUME_FOG) return;
    hook = getenv("GX_T_FOGPTSHADOW");
    if (hook && hook[0] == '0')
    {
        for (n = 0; n < GX_FOGVOL_MAX_POINT_LIGHTS; n++)
        {
            gfx->fogVolume.ptShadowTiles[n * 4 + 0] = 0.0f;
            gfx->fogVolume.ptShadowTiles[n * 4 + 1] = 0.0f;
            gfx->fogVolume.ptShadowTiles[n * 4 + 2] = 0.0f;
            gfx->fogVolume.ptShadowTiles[n * 4 + 3] = 0.0f;
            gfx->fogVolume.ptShadowHeapIdx[n] = -1;
        }
        return;
    }
    if (gfx->fogVolume.ptShadowFrame == maindata.frameCount) return;
    gfx->fogVolume.ptShadowFrame = maindata.frameCount;
    for (n = 0; n < GX_FOGVOL_MAX_POINT_LIGHTS; n++)
    {
        gfx->fogVolume.ptShadowTiles[n * 4 + 0] = 0.0f;
        gfx->fogVolume.ptShadowTiles[n * 4 + 1] = 0.0f;
        gfx->fogVolume.ptShadowTiles[n * 4 + 2] = 0.0f;
        gfx->fogVolume.ptShadowTiles[n * 4 + 3] = 0.0f;
        gfx->fogVolume.ptShadowHeapIdx[n] = -1;
    }
    if (!gfx->state.ENABLE_POINT_SHADOWS) return;
    if (gfx->common.pointShadowPlaceholder) return;
    if (gfx->common.pointShadowArray == 0) return;
    p = (gX_U32)gX_GFX_POINT_SHADOW_ARRAY_SIZE;
    if (!gfx->fogVolume.ptShadowInited)
    {
        gX_U32 w = 6u * (gX_U32)GX_FOGVOL_MAX_PT_SHADOW * p;
        gX_U32 tex = gX_RHI_Tex_Gen(gX_RHI_TEXTURE_2D, "fog_ptshadow_atlas");
        gX_RHI_Tex_Storage2D(tex, gX_RHI_TEXTURE_2D, 1, gX_RHI_DEPTH_COMPONENT24,
            (gX_I32)w, (gX_I32)p);
        gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_NEAREST);
        gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_NEAREST);
        gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_BORDER);
        gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_BORDER);
        {
            gX_F32 borderC[] = {1.0f, 1.0f, 1.0f, 1.0f};
            gX_RHI_Tex_Paramfv(tex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_BORDER_COLOR, borderC);
        }
        {
            gX_U32 fbo = 0;
            gX_RHI_FBO_Gen(1, &fbo);
            gX_RHI_BindFBO(fbo);
            gX_RHI_FBO_AttachTexture(tex, gX_RHI_DEPTH_ATTACHMENT, gX_RHI_TEXTURE_2D);
            gfx->fogVolume.ptShadowAtlasFbo = fbo;
        }
        gX_RHI_FBO_Gen(1, &gfx->fogVolume.ptShadowSrcFbo);
        gfx->fogVolume.ptShadowAtlasTex = tex;
        gfx->fogVolume.ptShadowInited = 1;
    }
    slots = 0;
    n = 0;
    if (gfx->fogVolume.enablePointLights)
    {
        gX_RendererGFX_Light *lptr = (gX_RendererGFX_Light*)gfx->lights.data;
        for (gX_U32 li = 0;
             li < gfx->lights.amount && n < GX_FOGVOL_MAX_POINT_LIGHTS; li++)
        {
            gX_RendererGFX_Light *l;
            gX_I1 has;
            if (!gfx->lights.meta[li].USED) continue;
            l = &lptr[li];
            if (l->sun || (l->type != 1 && l->type != 2)) continue;
            has = (l->castShadows && l->shadowDepth.id > 0
                && ((gX_I32)li * 6 + 5) < ((gX_I32)gX_GFX_MAX_POINT_SHADOW_LIGHTS * 6)) ? 1 : 0;
            if (has && slots < GX_FOGVOL_MAX_PT_SHADOW)
            {
                gfx->fogVolume.ptShadowTiles[n * 4 + 0] = (gX_F32)slots / (gX_F32)GX_FOGVOL_MAX_PT_SHADOW;
                gfx->fogVolume.ptShadowTiles[n * 4 + 1] = l->shadowFar;
                gfx->fogVolume.ptShadowTiles[n * 4 + 2] = (gX_F32)((gX_I32)li * 6);
                gfx->fogVolume.ptShadowTiles[n * 4 + 3] = 1.0f;
                gfx->fogVolume.ptShadowHeapIdx[n] = (gX_I32)li;
                slots++;
            }
            n++;
        }
    }
    for (n = 0; n < GX_FOGVOL_MAX_POINT_LIGHTS; n++)
    {
        gX_I32 heap;
        gX_I32 slot;
        if (gfx->fogVolume.ptShadowTiles[n * 4 + 3] < 0.5f) continue;
        heap = gfx->fogVolume.ptShadowHeapIdx[n];
        slot = (gX_I32)(gfx->fogVolume.ptShadowTiles[n * 4 + 0] * (gX_F32)GX_FOGVOL_MAX_PT_SHADOW);        for (gX_I32 f = 0; f < 6; f++)
        {
            gX_U32 dx = (gX_U32)((slot * 6 + f)) * p;
            gX_RHI_BindFBO(gfx->fogVolume.ptShadowSrcFbo);
            gX_RHI_FBO_AttachLayer(gfx->common.pointShadowArray,
                gX_RHI_DEPTH_ATTACHMENT, 0, heap * 6 + f);
            gX_RHI_Blit(gfx->fogVolume.ptShadowSrcFbo, gfx->fogVolume.ptShadowAtlasFbo,
                0, 0, p, p, dx, 0, p, p,
                gX_RHI_DEPTH_BUFFER_BIT, gX_RHI_NEAREST);
        }
    }
    {
        static gX_I1 logged = 0;
        if (!logged && slots > 0)
        {
            logged = 1;
            gX_TraceLog_GFX(gX_LOG_NOTICE, "PTSHADOW: atlas=%u slots=%d",
                gfx->fogVolume.ptShadowAtlasTex, slots);
        }
    }
    return;
}

//opencode: fog inscatter shares directional sun radiance in every gen pass.
static gX_I0 FogVolume_SetSunRadiance(gX_RendererGFX *gfx)
{
    gX_RHI_Uniform_Vec3(gfx->fogVolume.genSunColorLoc,
        (gX_F32)gfx->sun.color.r / 255.0f,
        (gX_F32)gfx->sun.color.g / 255.0f,
        (gX_F32)gfx->sun.color.b / 255.0f);
    gX_RHI_Uniform_Float(gfx->fogVolume.genSunBrightnessLoc, gfx->sun.brightness);
}

//opencode: item-13 - pack USED non-sun point (type 1) and spot (type 2)
//   lights for the fog march, dense heap-order up to
//   GX_FOGVOL_MAX_POINT_LIGHTS. Dense packing is safe here (this program
//   is the only consumer - no shadow-layer or cookie coupling like
//   standard.frag's heap-indexed arrays). Spots carry axis + cosines for
//   the cone falloff (mirrors standard.frag); points carry the -2.0
//   sentinel and skip the cone. Raw RHI uploads: the wrapper SetValue
//   path writes garbage for this program (see Draw note).
static gX_I0 FogVolume_UploadPoints(gX_RendererGFX *gfx)
{
    gX_F32 poss[GX_FOGVOL_MAX_POINT_LIGHTS * 4] = {0};
    gX_F32 cols[GX_FOGVOL_MAX_POINT_LIGHTS * 4] = {0};
    gX_F32 atts[GX_FOGVOL_MAX_POINT_LIGHTS * 4] = {0};
    gX_F32 sdirs[GX_FOGVOL_MAX_POINT_LIGHTS * 4] = {0};
    gX_I32 n = 0;
    if (gfx->fogVolume.enablePointLights) {
        gX_RendererGFX_Light *lptr = (gX_RendererGFX_Light*)gfx->lights.data;
        for (gX_U32 li = 0;
             li < gfx->lights.amount && n < GX_FOGVOL_MAX_POINT_LIGHTS; li++) {
            if (!gfx->lights.meta[li].USED) continue;
            gX_RendererGFX_Light *l = &lptr[li];
            if (l->sun || (l->type != 1 && l->type != 2)) continue;
            poss[n * 4 + 0] = l->pos.x;
            poss[n * 4 + 1] = l->pos.y;
            poss[n * 4 + 2] = l->pos.z;
            cols[n * 4 + 0] = (gX_F32)l->color.r / 255.0f;
            cols[n * 4 + 1] = (gX_F32)l->color.g / 255.0f;
            cols[n * 4 + 2] = (gX_F32)l->color.b / 255.0f;
            cols[n * 4 + 3] = l->brightness;
            atts[n * 4 + 0] = l->attenConst;
            atts[n * 4 + 1] = l->attenLinear;
            atts[n * 4 + 2] = l->attenQuad;
            if (l->type == 2) {
                gX_F32 halfA = l->fov * 0.5f * gX_DEG2RAD;
                sdirs[n * 4 + 0] = l->dir.x;
                sdirs[n * 4 + 1] = l->dir.y;
                sdirs[n * 4 + 2] = l->dir.z;
                sdirs[n * 4 + 3] = cosf(halfA);
                atts[n * 4 + 3] = cosf(halfA * 0.9f);
            } else {
                sdirs[n * 4 + 3] = -2.0f;
            }
            n++;
        }
    }
    if (gfx->fogVolume.genPointCountLoc >= 0)
        gX_RHI_UniformInt(gfx->fogVolume.genPointCountLoc, n);
    if (gfx->fogVolume.genPointPosLoc >= 0)
        gX_RHI_UniformVec4(gfx->fogVolume.genPointPosLoc,
            GX_FOGVOL_MAX_POINT_LIGHTS, poss);
    if (gfx->fogVolume.genPointColorLoc >= 0)
        gX_RHI_UniformVec4(gfx->fogVolume.genPointColorLoc,
            GX_FOGVOL_MAX_POINT_LIGHTS, cols);
    if (gfx->fogVolume.genPointAttenLoc >= 0)
        gX_RHI_UniformVec4(gfx->fogVolume.genPointAttenLoc,
            GX_FOGVOL_MAX_POINT_LIGHTS, atts);
    if (gfx->fogVolume.genSpotDirLoc >= 0)
        gX_RHI_UniformVec4(gfx->fogVolume.genSpotDirLoc,
            GX_FOGVOL_MAX_POINT_LIGHTS, sdirs);
    //opencode: point-shadow tiles (staged by UpdatePointShadowCopies earlier
    //   this frame; zeros when the hook is off or nothing casts).
    if (gfx->fogVolume.genPointShadowTileLoc >= 0)
        gX_RHI_UniformVec4(gfx->fogVolume.genPointShadowTileLoc,
            GX_FOGVOL_MAX_POINT_LIGHTS, gfx->fogVolume.ptShadowTiles);
    if (gfx->fogVolume.genPointShadowTileWLoc >= 0)
        gX_RHI_Uniform_Float(gfx->fogVolume.genPointShadowTileWLoc,
            (gX_F32)gX_GFX_POINT_SHADOW_ARRAY_SIZE / (gX_F32)(6 * GX_FOGVOL_MAX_PT_SHADOW * gX_GFX_POINT_SHADOW_ARRAY_SIZE));
    if (gfx->fogVolume.genPointShadowBiasLoc >= 0)
        gX_RHI_Uniform_Float(gfx->fogVolume.genPointShadowBiasLoc, gfx->state.pointShadowBias);
    if (gfx->fogVolume.genPointShadowAtlasLoc >= 0 && gfx->fogVolume.ptShadowAtlasTex > 0)
    {
        gX_RHI_Tex_BindOnUnit(gfx->fogVolume.ptShadowAtlasTex, gX_GFX_TEXUNIT_FOG_PTSHADOW);
        gX_RHI_UniformInt(gfx->fogVolume.genPointShadowAtlasLoc, gX_GFX_TEXUNIT_FOG_PTSHADOW);
    }
    return;
}

gX_I0 gX_RendererGFX_FogVolume_Draw(gX_RendererGFX *gfx)
{
    if (!gfx->state.ENABLE_VOLUME_FOG) return;
    if (!gfx->fogVolume.genMap.wrapper.loadingState.loaded) return;
    if (!gfx->fogVolume.genShader.loaded || !gfx->fogVolume.blendShader.loaded) return;
    if (!gfx->bloom.hdrScene.wrapper.loadingState.loaded) return;

    GX_PROFILE_ZONE_BEGIN("gfx:fog_volume");



    gX_I32 gw = (gX_I32)gfx->fogVolume.genMap.wrapper.width;
    gX_I32 gh = (gX_I32)gfx->fogVolume.genMap.wrapper.height;
    if (gw < 1 || gh < 1) { GX_PROFILE_ZONE_END("gfx:fog_volume"); return; }

    //opencode: compute view + inverse view matrices for reconstructing world pos from depth
    gX_Matrix view = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
    gX_Matrix invView = gX_Matrix_Invert(view);
    gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix proj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect,
        camera.frustum.near, camera.frustum.far);
    gX_Matrix invProj = gX_Matrix_Invert(proj);

    //opencode: sun direction toward light (negate sun.dir which is direction light travels)
    gX_F32v3 sunDir = gX_F32v3_Scale(gfx->sun.dir, -1.0f);

    //opencode: v0.3.8 - cascade copies refreshed once per frame (shared with
    //   the fog LUT pass)
    gX_RendererGFX_FogVolume_UpdateCascadeCopies(gfx);
    //opencode: point-shadow atlas tiles (same once-per-frame discipline)
    FogVolume_UpdatePointShadowCopies(gfx);

    //opencode: 1) Generate — half-res fog accumulation
    gX_Drawing_BeginTexture(gfx->fogVolume.genMap.wrapper.rt);
        gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT | gX_RHI_DEPTH_BUFFER_BIT | gX_RHI_STENCIL_BUFFER_BIT);
        gX_Drawing_ClearBG((gX_Color){0, 0, 0, 255});
        gX_Shader genS = gfx->fogVolume.genShader.shader;
        //opencode: v0.3.8 - RAW RHI uploads: the gX_Shader_SetValue_* wrapper
        //   path writes garbage for this program (uniform readback showed
        //   2^65 bit patterns); raw calls after an explicit Use are verified
        gX_RHI_Shader_Use(genS.id);
        //opencode: startup race: CacheLocs can run before the GPU program is
        //   linked (deferred load) and cache -1 forever. Any dead location
        //   re-runs the whole query pass (sceneDepth -1 silently collapsed
        //   every march to a 0.1-unit path: fog was invisible)
        if (gfx->fogVolume.genSceneColorLoc < 0
            || gfx->fogVolume.genSceneDepthLoc < 0
            || gfx->fogVolume.genDensityLoc < 0
            || gfx->fogVolume.genInvProjLoc < 0
            || gfx->fogVolume.genInvViewLoc < 0
            || gfx->fogVolume.genCamPosLoc < 0
            || gfx->fogVolume.genSunColorLoc < 0
            || gfx->fogVolume.genSunBrightnessLoc < 0
            || gfx->fogVolume.genShadowMapLoc < 0
            || gfx->fogVolume.genPointCountLoc < 0
            || gfx->fogVolume.genSpotDirLoc < 0) {
            gfx->fogVolume.locsCached = false;
            gX_RendererGFX_FogVolume_CacheLocs(gfx);
        }
        gX_Drawing_BeginShader(genS);
        gX_RHI_Shader_Use(genS.id);
        //opencode: bind uniforms AFTER BeginShader
        gX_RHI_Tex_BindOnUnit(gfx->bloom.scene.wrapper.rt.texture.id, gX_GFX_TEXUNIT_SCENE);
        gX_RHI_UniformInt(gfx->fogVolume.genSceneColorLoc, gX_GFX_TEXUNIT_SCENE);
        gX_RHI_Tex_BindOnUnit(gfx->bloom.hdrScene.wrapper.rt.depth.id, gX_GFX_TEXUNIT_DEPTH);
        gX_RHI_UniformInt(gfx->fogVolume.genSceneDepthLoc, gX_GFX_TEXUNIT_DEPTH);
        gX_RHI_Uniform_Vec3(gfx->fogVolume.genSunDirLoc, sunDir.x, sunDir.y, sunDir.z);
        FogVolume_SetSunRadiance(gfx);
        FogVolume_UploadPoints(gfx);   //opencode: item-13
        gX_RHI_Uniform_Vec3(gfx->fogVolume.genCamPosLoc, camera.camera3D.position.x,
            camera.camera3D.position.y, camera.camera3D.position.z);
        gX_RHI_Uniform_Float(gfx->fogVolume.genDensityLoc, gfx->fogVolume.density);
        gX_RHI_Uniform_Float(gfx->fogVolume.genHeightFalloffLoc, gfx->fogVolume.heightFalloff);
        gX_RHI_Uniform_Float(gfx->fogVolume.genScatteringLoc, gfx->fogVolume.scattering);
        gX_RHI_Uniform_Vec2(gfx->fogVolume.genResLoc, (gX_F32)gw, (gX_F32)gh);
        gX_RHI_UniformMatrix(gfx->fogVolume.genInvProjLoc, 1, 1, &invProj.m0);
        gX_RHI_UniformMatrix(gfx->fogVolume.genInvViewLoc, 1, 1, &invView.m0);
        //opencode: cascade 0 is the dedicated 2D sun map; array copies begin at 1.
        {
            gX_U32 smId = 0;
            if (gfx->sun.camera.map)
                smId = gfx->sun.camera.map->depth.id;
            if (smId > 0 && gfx->fogVolume.genShadowMapLoc >= 0) {
                gX_RHI_Tex_BindOnUnit(smId, gX_GFX_TEXUNIT_SHADOW);
                gX_RHI_UniformInt(gfx->fogVolume.genShadowMapLoc, gX_GFX_TEXUNIT_SHADOW);
            }
        }
        {
            gX_Matrix layer0 = (gfx->csm.cascadeCount > 1) ? gfx->csm.vps[0]
                                                           : gfx->sun.camera.viewProjection;
            gX_RHI_UniformMatrix(gfx->fogVolume.genLayer0MatLoc, 1, 1, &layer0.m0);
        }
        //opencode: v0.3.8 - cascade copies 1..3 + split distances: far-field
        //   fog shadows over the whole CSM range (was hard-capped at 40u)
        if (gfx->csm.cascadeCount > 1) {
            gX_RHI_Tex_BindOnUnit(gfx->fogVolume.csmCopyTex[1], gX_GFX_TEXUNIT_FOG_CSM1);
            gX_RHI_UniformInt(gfx->fogVolume.genCsmTex1Loc, gX_GFX_TEXUNIT_FOG_CSM1);
            gX_RHI_Tex_BindOnUnit(gfx->fogVolume.csmCopyTex[2], gX_GFX_TEXUNIT_FOG_CSM2);
            gX_RHI_UniformInt(gfx->fogVolume.genCsmTex2Loc, gX_GFX_TEXUNIT_FOG_CSM2);
            gX_RHI_Tex_BindOnUnit(gfx->fogVolume.csmCopyTex[3], gX_GFX_TEXUNIT_FOG_CSM3);
            gX_RHI_UniformInt(gfx->fogVolume.genCsmTex3Loc, gX_GFX_TEXUNIT_FOG_CSM3);
            gX_RHI_UniformMatrix(gfx->fogVolume.genCsmVp1Loc, 1, 1, &gfx->csm.vps[1].m0);
            gX_RHI_UniformMatrix(gfx->fogVolume.genCsmVp2Loc, 1, 1, &gfx->csm.vps[2].m0);
            gX_RHI_UniformMatrix(gfx->fogVolume.genCsmVp3Loc, 1, 1, &gfx->csm.vps[3].m0);
            gX_RHI_Uniform_Vec3(gfx->fogVolume.genCsmSplitsLoc,
                gfx->csm.splits[0], gfx->csm.splits[1], gfx->csm.splits[2]);
            gX_RHI_UniformInt(gfx->fogVolume.genCascadeCountLoc, gfx->csm.cascadeCount);
        }

        gX_RHI_Uniform_Float(gfx->fogVolume.genHgLoc, gfx->fogVolume.hgAsymmetry);
        gX_RHI_UniformInt(gfx->fogVolume.genLutModeLoc, 0);   //opencode: v0.3.8 - the LUT pass shares this program
        //opencode: O-blowout - main-view delta lands on post-ACES content
        if (gfx->fogVolume.genClampLdrLoc >= 0)
            gX_RHI_UniformInt(gfx->fogVolume.genClampLdrLoc, 1);

        gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)gw, (gX_F32)gh);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();

    //opencode: 2) Composite — additive Texture_Draw of the precomputed delta
    //   (godray.c recipe: the rlgl batch path is the proven-reliable route
    //   into bloom.scene; custom BeginShader+DrawScreenQuad/raw quads have
    //   been observed to silently no-op in this context)
    {
        gX_RenderTexture *tgt = gfx->common.compositeTarget ? gfx->common.compositeTarget : &gfx->bloom.hdrScene.wrapper.rt;
        gX_Drawing_BeginTexture(*tgt);
            gX_I1 prevBlendFog = gX_RHI_IsEnabled(gX_RHI_BLEND);
            gX_I32 prevBlendSrc, prevBlendDst, prevBlendEq;
            gX_RHI_GetIntegerv(gX_RHI_BLEND_SRC_RGB, &prevBlendSrc);
            gX_RHI_GetIntegerv(gX_RHI_BLEND_DST_RGB, &prevBlendDst);
            gX_RHI_GetIntegerv(gX_RHI_BLEND_EQUATION_RGB, &prevBlendEq);
            gX_RHI_Enable(gX_RHI_BLEND);
            gX_RHI_Blend_Func_Separate(gX_RHI_ONE, gX_RHI_ONE, gX_RHI_ONE, gX_RHI_ONE);
            gX_Texture_Draw(gfx->fogVolume.genMap.wrapper.rt.texture, (gX_Texture_Draw_Params){
                .dst = (gX_Rectangle){0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight},
                .tint = gX_WHITE,
                .scale = {1, 1}
            });
            gX_Batch_Flush();
            gX_RHI_Blend_Equation((gX_U32)prevBlendEq);
            gX_RHI_Blend_Func_Separate((gX_U32)prevBlendSrc, (gX_U32)prevBlendDst, (gX_U32)prevBlendSrc, (gX_U32)prevBlendDst);
            if (!prevBlendFog) gX_RHI_Disable(gX_RHI_BLEND);
        gX_Drawing_EndTexture();
    }




    GX_PROFILE_ZONE_END("gfx:fog_volume");
}

//opencode: v0.3.8 - fog delta LUT. Runs the gen program's sky branch per
//   equirect texel (lutMode 1): the delta for a sky ray along each direction,
//   anchored at the main camera. The SSR escape path adds it to the env
//   fallback so reflected sky carries the same fog as direct sky.
gX_I0 gX_RendererGFX_FogVolume_LutPass(gX_RendererGFX *gfx)
{
    if (!gfx->state.ENABLE_VOLUME_FOG) return;
    if (!gfx->fogVolume.genShader.loaded) return;
    if (!gfx->ssr.fogLut.wrapper.loadingState.loaded) return;
    if (!gfx->ssr.skyLut.wrapper.loadingState.loaded) return;

    gX_I32 lw = gfx->ssr.fogLut.wrapper.width;
    gX_I32 lh = gfx->ssr.fogLut.wrapper.height;
    if (lw < 1 || lh < 1) return;

    gX_RendererGFX_FogVolume_UpdateCascadeCopies(gfx);

    gX_F32v3 sunDir = gX_F32v3_Scale(gfx->sun.dir, -1.0f);

    gX_Matrix lutView = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
    gX_F32 lutAspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix lutProj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, lutAspect,
        camera.frustum.near, camera.frustum.far);
    gX_Matrix lutInvView = gX_Matrix_Invert(lutView);
    gX_Matrix lutInvProj = gX_Matrix_Invert(lutProj);

    gX_Drawing_BeginTexture(gfx->ssr.fogLut.wrapper.rt);
        gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT | gX_RHI_DEPTH_BUFFER_BIT | gX_RHI_STENCIL_BUFFER_BIT);
        gX_Drawing_ClearBG((gX_Color){0, 0, 0, 255});
        gX_Shader genS = gfx->fogVolume.genShader.shader;
        gX_RHI_Shader_Use(genS.id);
        if (gfx->fogVolume.genSceneColorLoc < 0
            || gfx->fogVolume.genSceneDepthLoc < 0
            || gfx->fogVolume.genDensityLoc < 0
            || gfx->fogVolume.genInvProjLoc < 0
            || gfx->fogVolume.genInvViewLoc < 0
            || gfx->fogVolume.genCamPosLoc < 0
            || gfx->fogVolume.genSunColorLoc < 0
            || gfx->fogVolume.genSunBrightnessLoc < 0) {
            gfx->fogVolume.locsCached = false;
            gX_RendererGFX_FogVolume_CacheLocs(gfx);
        }
        gX_Drawing_BeginShader(genS);
        gX_RHI_Shader_Use(genS.id);
        //opencode: color source = the sky LUT (the sceneCol term of the delta
        //   becomes the sky color at that direction); depth unused (all sky)
        gX_RHI_Tex_BindOnUnit(gfx->ssr.skyLut.wrapper.rt.texture.id, gX_GFX_TEXUNIT_SCENE);
        gX_RHI_UniformInt(gfx->fogVolume.genSceneColorLoc, gX_GFX_TEXUNIT_SCENE);
        gX_RHI_Tex_BindOnUnit(gfx->bloom.hdrScene.wrapper.rt.depth.id, gX_GFX_TEXUNIT_DEPTH);
        gX_RHI_UniformInt(gfx->fogVolume.genSceneDepthLoc, gX_GFX_TEXUNIT_DEPTH);
        gX_RHI_UniformInt(gfx->fogVolume.genLutModeLoc, 1);
        gX_RHI_Uniform_Vec3(gfx->fogVolume.genSunDirLoc, sunDir.x, sunDir.y, sunDir.z);
        FogVolume_SetSunRadiance(gfx);
        //opencode: item-13 - LUT stays sun-only directional: point glow is
        //   positional (baked per-direction-at-camera would misattribute it
        //   at every other reflection point), so force count 0 here.
        if (gfx->fogVolume.genPointCountLoc >= 0)
            gX_RHI_UniformInt(gfx->fogVolume.genPointCountLoc, 0);
        gX_RHI_Uniform_Vec3(gfx->fogVolume.genCamPosLoc, camera.camera3D.position.x,
            camera.camera3D.position.y, camera.camera3D.position.z);
        gX_RHI_Uniform_Float(gfx->fogVolume.genDensityLoc, gfx->fogVolume.density);
        gX_RHI_Uniform_Float(gfx->fogVolume.genHeightFalloffLoc, gfx->fogVolume.heightFalloff);
        gX_RHI_Uniform_Float(gfx->fogVolume.genScatteringLoc, gfx->fogVolume.scattering);
        gX_RHI_Uniform_Vec2(gfx->fogVolume.genResLoc, (gX_F32)lw, (gX_F32)lh);
        gX_RHI_UniformMatrix(gfx->fogVolume.genInvProjLoc, 1, 1, &lutInvProj.m0);
        gX_RHI_UniformMatrix(gfx->fogVolume.genInvViewLoc, 1, 1, &lutInvView.m0);
        {
            gX_U32 smId = 0;
            if (gfx->sun.camera.map)
                smId = gfx->sun.camera.map->depth.id;
            if (smId > 0 && gfx->fogVolume.genShadowMapLoc >= 0) {
                gX_RHI_Tex_BindOnUnit(smId, gX_GFX_TEXUNIT_SHADOW);
                gX_RHI_UniformInt(gfx->fogVolume.genShadowMapLoc, gX_GFX_TEXUNIT_SHADOW);
            }
        }
        {
            gX_Matrix layer0 = (gfx->csm.cascadeCount > 1) ? gfx->csm.vps[0]
                                                           : gfx->sun.camera.viewProjection;
            gX_RHI_UniformMatrix(gfx->fogVolume.genLayer0MatLoc, 1, 1, &layer0.m0);
        }
        if (gfx->csm.cascadeCount > 1) {
            gX_RHI_Tex_BindOnUnit(gfx->fogVolume.csmCopyTex[1], gX_GFX_TEXUNIT_FOG_CSM1);
            gX_RHI_UniformInt(gfx->fogVolume.genCsmTex1Loc, gX_GFX_TEXUNIT_FOG_CSM1);
            gX_RHI_Tex_BindOnUnit(gfx->fogVolume.csmCopyTex[2], gX_GFX_TEXUNIT_FOG_CSM2);
            gX_RHI_UniformInt(gfx->fogVolume.genCsmTex2Loc, gX_GFX_TEXUNIT_FOG_CSM2);
            gX_RHI_Tex_BindOnUnit(gfx->fogVolume.csmCopyTex[3], gX_GFX_TEXUNIT_FOG_CSM3);
            gX_RHI_UniformInt(gfx->fogVolume.genCsmTex3Loc, gX_GFX_TEXUNIT_FOG_CSM3);
            gX_RHI_UniformMatrix(gfx->fogVolume.genCsmVp1Loc, 1, 1, &gfx->csm.vps[1].m0);
            gX_RHI_UniformMatrix(gfx->fogVolume.genCsmVp2Loc, 1, 1, &gfx->csm.vps[2].m0);
            gX_RHI_UniformMatrix(gfx->fogVolume.genCsmVp3Loc, 1, 1, &gfx->csm.vps[3].m0);
            gX_RHI_Uniform_Vec3(gfx->fogVolume.genCsmSplitsLoc,
                gfx->csm.splits[0], gfx->csm.splits[1], gfx->csm.splits[2]);
            gX_RHI_UniformInt(gfx->fogVolume.genCascadeCountLoc, gfx->csm.cascadeCount);
        }
        gX_RHI_Uniform_Float(gfx->fogVolume.genHgLoc, gfx->fogVolume.hgAsymmetry);
        //opencode: O-blowout - LUT feeds SSR escapes + IBL globally: keep HDR
        //   (the LDR cap visibly darkened all specular reflections); the sun
        //   lobe itself is bounded by the HG peak cap in-shader instead.
        if (gfx->fogVolume.genClampLdrLoc >= 0)
            gX_RHI_UniformInt(gfx->fogVolume.genClampLdrLoc, 0);
        gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)lw, (gX_F32)lh);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();
}

//opencode: v0.3.8 - planar-reflection fog. The planar RT renders the
//   mirrored view WITHOUT fog, so a fogged direct view next to an unfogged
//   reflection painted a hard luminance seam at the mirror horizon. This
//   runs the same gen program with the MIRRORED camera matrices and the
//   planar pass's own depth texture, then composites additively into the
//   planar RT before the main model pass samples it. The genMap is reused
//   sequentially (the main-view gen overwrites it later in the frame).
gX_I0 gX_RendererGFX_FogVolume_PlanarFog(gX_RendererGFX *gfx, gX_F32v3 camPos,
    gX_F32 fovy, gX_Matrix invView, gX_Matrix invProj, gX_U32 depthTexId)
{
    if (!gfx->state.ENABLE_VOLUME_FOG) return;
    if (!gfx->fogVolume.genMap.wrapper.loadingState.loaded) return;
    if (!gfx->fogVolume.genShader.loaded) return;
    if (!gfx->reflection.planar.wrapper.loadingState.loaded) return;
    if (depthTexId == 0) return;

    //opencode: point-shadow atlas tiles (world-space lights: mirror-valid)
    FogVolume_UpdatePointShadowCopies(gfx);

    gX_I32 gw = (gX_I32)gfx->fogVolume.genMap.wrapper.width;
    gX_I32 gh = (gX_I32)gfx->fogVolume.genMap.wrapper.height;
    if (gw < 1 || gh < 1) return;

    gX_F32v3 sunDir = gX_F32v3_Scale(gfx->sun.dir, -1.0f);

    gX_Drawing_BeginTexture(gfx->fogVolume.genMap.wrapper.rt);
        gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT | gX_RHI_DEPTH_BUFFER_BIT | gX_RHI_STENCIL_BUFFER_BIT);
        gX_Drawing_ClearBG((gX_Color){0, 0, 0, 255});
        gX_Shader genS = gfx->fogVolume.genShader.shader;
        gX_RHI_Shader_Use(genS.id);
        if (gfx->fogVolume.genSceneColorLoc < 0
            || gfx->fogVolume.genSceneDepthLoc < 0
            || gfx->fogVolume.genDensityLoc < 0
            || gfx->fogVolume.genInvProjLoc < 0
            || gfx->fogVolume.genInvViewLoc < 0
            || gfx->fogVolume.genCamPosLoc < 0
            || gfx->fogVolume.genSunColorLoc < 0
            || gfx->fogVolume.genSunBrightnessLoc < 0
            || gfx->fogVolume.genShadowMapLoc < 0
            || gfx->fogVolume.genPointCountLoc < 0
            || gfx->fogVolume.genSpotDirLoc < 0) {
            gfx->fogVolume.locsCached = false;
            gX_RendererGFX_FogVolume_CacheLocs(gfx);
        }
        gX_Drawing_BeginShader(genS);
        gX_RHI_Shader_Use(genS.id);
        //opencode: color source = the planar RT itself (the mirrored scene);
        //   genMap is a separate RT so sampling it here is legal
        gX_RHI_Tex_BindOnUnit(gfx->reflection.planar.wrapper.rt.texture.id, gX_GFX_TEXUNIT_SCENE);
        gX_RHI_UniformInt(gfx->fogVolume.genSceneColorLoc, gX_GFX_TEXUNIT_SCENE);
        gX_RHI_Tex_BindOnUnit(depthTexId, gX_GFX_TEXUNIT_DEPTH);
        gX_RHI_UniformInt(gfx->fogVolume.genSceneDepthLoc, gX_GFX_TEXUNIT_DEPTH);
        gX_RHI_Uniform_Vec3(gfx->fogVolume.genSunDirLoc, sunDir.x, sunDir.y, sunDir.z);
        FogVolume_SetSunRadiance(gfx);
        FogVolume_UploadPoints(gfx);   //opencode: item-13 (world-space: valid for the mirrored camera too)
        gX_RHI_Uniform_Vec3(gfx->fogVolume.genCamPosLoc, camPos.x, camPos.y, camPos.z);
        gX_RHI_Uniform_Float(gfx->fogVolume.genDensityLoc, gfx->fogVolume.density);
        gX_RHI_Uniform_Float(gfx->fogVolume.genHeightFalloffLoc, gfx->fogVolume.heightFalloff);
        gX_RHI_Uniform_Float(gfx->fogVolume.genScatteringLoc, gfx->fogVolume.scattering);
        gX_RHI_Uniform_Vec2(gfx->fogVolume.genResLoc, (gX_F32)gw, (gX_F32)gh);
        gX_RHI_UniformMatrix(gfx->fogVolume.genInvProjLoc, 1, 1, &invProj.m0);
        gX_RHI_UniformMatrix(gfx->fogVolume.genInvViewLoc, 1, 1, &invView.m0);
        {
            gX_U32 smId = 0;
            if (gfx->sun.camera.map)
                smId = gfx->sun.camera.map->depth.id;
            if (smId > 0 && gfx->fogVolume.genShadowMapLoc >= 0) {
                gX_RHI_Tex_BindOnUnit(smId, gX_GFX_TEXUNIT_SHADOW);
                gX_RHI_UniformInt(gfx->fogVolume.genShadowMapLoc, gX_GFX_TEXUNIT_SHADOW);
            }
        }
        {
            gX_Matrix layer0 = (gfx->csm.cascadeCount > 1) ? gfx->csm.vps[0]
                                                           : gfx->sun.camera.viewProjection;
            gX_RHI_UniformMatrix(gfx->fogVolume.genLayer0MatLoc, 1, 1, &layer0.m0);
        }
        if (gfx->csm.cascadeCount > 1) {
            gX_RHI_Tex_BindOnUnit(gfx->fogVolume.csmCopyTex[1], gX_GFX_TEXUNIT_FOG_CSM1);
            gX_RHI_UniformInt(gfx->fogVolume.genCsmTex1Loc, gX_GFX_TEXUNIT_FOG_CSM1);
            gX_RHI_Tex_BindOnUnit(gfx->fogVolume.csmCopyTex[2], gX_GFX_TEXUNIT_FOG_CSM2);
            gX_RHI_UniformInt(gfx->fogVolume.genCsmTex2Loc, gX_GFX_TEXUNIT_FOG_CSM2);
            gX_RHI_Tex_BindOnUnit(gfx->fogVolume.csmCopyTex[3], gX_GFX_TEXUNIT_FOG_CSM3);
            gX_RHI_UniformInt(gfx->fogVolume.genCsmTex3Loc, gX_GFX_TEXUNIT_FOG_CSM3);
            gX_RHI_UniformMatrix(gfx->fogVolume.genCsmVp1Loc, 1, 1, &gfx->csm.vps[1].m0);
            gX_RHI_UniformMatrix(gfx->fogVolume.genCsmVp2Loc, 1, 1, &gfx->csm.vps[2].m0);
            gX_RHI_UniformMatrix(gfx->fogVolume.genCsmVp3Loc, 1, 1, &gfx->csm.vps[3].m0);
            gX_RHI_Uniform_Vec3(gfx->fogVolume.genCsmSplitsLoc,
                gfx->csm.splits[0], gfx->csm.splits[1], gfx->csm.splits[2]);
            gX_RHI_UniformInt(gfx->fogVolume.genCascadeCountLoc, gfx->csm.cascadeCount);
        }
        gX_RHI_Uniform_Float(gfx->fogVolume.genHgLoc, gfx->fogVolume.hgAsymmetry);
        gX_RHI_UniformInt(gfx->fogVolume.genLutModeLoc, 0);   //opencode: v0.3.8 - the LUT pass shares this program
        //opencode: O-blowout - planar RT is pre-tonemap HDR: keep full range
        if (gfx->fogVolume.genClampLdrLoc >= 0)
            gX_RHI_UniformInt(gfx->fogVolume.genClampLdrLoc, 0);
        gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)gw, (gX_F32)gh);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();

    //opencode: composite the mirrored-view delta additively into the planar RT
    //   (same ONE/ONE recipe as FogVolume_Draw's composite)
    gX_Drawing_BeginTexture(gfx->reflection.planar.wrapper.rt);
        gX_I1 prevBlendFog = gX_RHI_IsEnabled(gX_RHI_BLEND);
        gX_I32 prevBlendSrc, prevBlendDst, prevBlendEq;
        gX_RHI_GetIntegerv(gX_RHI_BLEND_SRC_RGB, &prevBlendSrc);
        gX_RHI_GetIntegerv(gX_RHI_BLEND_DST_RGB, &prevBlendDst);
        gX_RHI_GetIntegerv(gX_RHI_BLEND_EQUATION_RGB, &prevBlendEq);
        gX_RHI_Enable(gX_RHI_BLEND);
        gX_RHI_Blend_Func_Separate(gX_RHI_ONE, gX_RHI_ONE, gX_RHI_ONE, gX_RHI_ONE);
        gX_Texture_Draw(gfx->fogVolume.genMap.wrapper.rt.texture, (gX_Texture_Draw_Params){
            .dst = (gX_Rectangle){0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight},
            .tint = gX_WHITE,
            .scale = {1, 1}
        });
        gX_Batch_Flush();
        gX_RHI_Blend_Equation((gX_U32)prevBlendEq);
        gX_RHI_Blend_Func_Separate((gX_U32)prevBlendSrc, (gX_U32)prevBlendDst, (gX_U32)prevBlendSrc, (gX_U32)prevBlendDst);
        if (!prevBlendFog) gX_RHI_Disable(gX_RHI_BLEND);
    gX_Drawing_EndTexture();
}

gX_I0 gX_RendererGFX_FogVolume_CacheLocs(gX_RendererGFX *gfx)
{
    if (gfx->fogVolume.locsCached && !gfx->fogVolume.genShader.locsStale && !gfx->fogVolume.blendShader.locsStale) return;
    if (!gfx->fogVolume.genShader.loaded || !gfx->fogVolume.blendShader.loaded) return;

    gX_Shader genS = gfx->fogVolume.genShader.shader;
    gfx->fogVolume.genSceneColorLoc = gX_Shader_GetLocation(genS, "sceneColor");
    gfx->fogVolume.genSceneDepthLoc = gX_Shader_GetLocation(genS, "sceneDepth");
    gfx->fogVolume.genSunDirLoc     = gX_Shader_GetLocation(genS, "sunDir");
    gfx->fogVolume.genSunColorLoc   = gX_Shader_GetLocation(genS, "fogSunColor");
    gfx->fogVolume.genSunBrightnessLoc = gX_Shader_GetLocation(genS, "fogSunBrightness");
    gfx->fogVolume.genDensityLoc    = gX_Shader_GetLocation(genS, "density");
    gfx->fogVolume.genHeightFalloffLoc = gX_Shader_GetLocation(genS, "heightFalloff");
    gfx->fogVolume.genScatteringLoc = gX_Shader_GetLocation(genS, "scattering");
    gfx->fogVolume.genResLoc        = gX_Shader_GetLocation(genS, "resolution");
    gfx->fogVolume.genCamPosLoc     = gX_Shader_GetLocation(genS, "cameraPos");
    gfx->fogVolume.genInvProjLoc    = gX_Shader_GetLocation(genS, "invProjection");
    gfx->fogVolume.genInvViewLoc    = gX_Shader_GetLocation(genS, "invView");
    //opencode: v0.3-M3 — god rays
    gfx->fogVolume.genCsmArrayLoc   = gX_Shader_GetLocation(genS, "csmShadowMapArray");
    gfx->fogVolume.genShadowMapLoc  = gX_Shader_GetLocation(genS, "sunShadowMap");
    gfx->fogVolume.genCsmVPLoc      = gX_Shader_GetLocation(genS, "csmVP");
    gfx->fogVolume.genCsmSplitsLoc  = gX_Shader_GetLocation(genS, "csmSplits");
    gfx->fogVolume.genCascadeCountLoc = gX_Shader_GetLocation(genS, "cascadeCount");
    gfx->fogVolume.genLayer0MatLoc  = gX_Shader_GetLocation(genS, "layer0Mat");
    gfx->fogVolume.genCsmTex1Loc    = gX_Shader_GetLocation(genS, "csmTex1");
    gfx->fogVolume.genCsmTex2Loc    = gX_Shader_GetLocation(genS, "csmTex2");
    gfx->fogVolume.genCsmTex3Loc    = gX_Shader_GetLocation(genS, "csmTex3");
    gfx->fogVolume.genCsmVp1Loc     = gX_Shader_GetLocation(genS, "csmVp1");
    gfx->fogVolume.genCsmVp2Loc     = gX_Shader_GetLocation(genS, "csmVp2");
    gfx->fogVolume.genCsmVp3Loc     = gX_Shader_GetLocation(genS, "csmVp3");
    gfx->fogVolume.genHgLoc         = gX_Shader_GetLocation(genS, "hgAsymmetry");
    gfx->fogVolume.genLutModeLoc   = gX_Shader_GetLocation(genS, "lutMode");   //opencode: v0.3.8
    gfx->fogVolume.genClampLdrLoc  = gX_Shader_GetLocation(genS, "clampLdr");  //opencode: O-blowout
    gfx->fogVolume.genPointCountLoc = gX_Shader_GetLocation(genS, "fogPointCount");   //opencode: item-13
    gfx->fogVolume.genPointPosLoc   = gX_Shader_GetLocation(genS, "fogPointPos");
    gfx->fogVolume.genPointColorLoc = gX_Shader_GetLocation(genS, "fogPointColor");
    gfx->fogVolume.genPointAttenLoc = gX_Shader_GetLocation(genS, "fogPointAtten");
    gfx->fogVolume.genSpotDirLoc    = gX_Shader_GetLocation(genS, "fogSpotDir");   //opencode: spot cone
    gfx->fogVolume.genPointShadowAtlasLoc = gX_Shader_GetLocation(genS, "fogPointShadowAtlas");
    gfx->fogVolume.genPointShadowTileLoc  = gX_Shader_GetLocation(genS, "fogPointShadowTile");
    gfx->fogVolume.genPointShadowTileWLoc = gX_Shader_GetLocation(genS, "fogPointShadowTileW");
    gfx->fogVolume.genPointShadowBiasLoc  = gX_Shader_GetLocation(genS, "fogPointShadowBias");

    gX_Shader blendS = gfx->fogVolume.blendShader.shader;
    gfx->fogVolume.blendSceneColorLoc = gX_Shader_GetLocation(blendS, "sceneColor");
    gfx->fogVolume.blendFogMapLoc     = gX_Shader_GetLocation(blendS, "fogMap");
    gfx->fogVolume.blendSunColorLoc   = gX_Shader_GetLocation(blendS, "sunColor");

    gfx->fogVolume.locsCached = true;
    gfx->fogVolume.genShader.locsStale = false;
    gfx->fogVolume.blendShader.locsStale = false;
}
