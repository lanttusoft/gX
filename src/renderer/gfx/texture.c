/**********************************************************************************************
*
*   [gX: src/renderer/gfx/texture.c]
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


#include "texture.h"

#include "../gfx.h"
#include "boilerplate.h"

#include "../../main.h"

#include "../../wrappers/wrappers.h"

#include "../../common/enum.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_tex.h"
#include "../../rhi/rhi_fbo.h"
//opencode: hunt - implicit-decl sweep

//opencode: GL for glTexParameterf (LOD bias)

//opencode: helper to apply filter + LOD bias to one GL texture.
//   Updates raylib's filter mode AND sets the mipmap LOD bias.
//   NOTE: depth textures are NOT passed through this function — they must
//   always use GL_NEAREST to avoid depth interpolation artifacts at edges.
static gX_I0 gX_ApplyTexParamsToGL(gX_Texture tex, gX_I32 filter, gX_F32 lodBias)
{
    gX_Texture_SetFilter(tex, filter);
    if (tex.id > 0) {
        gX_RHI_Tex_Bind(tex.id, gX_RHI_TEXTURE_2D);
        gX_RHI_Tex_Paramf(tex.id, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_LOD_BIAS, lodBias);
    }
}

//opencode: apply the global texture filter to all loaded render textures
//   and heap textures. Enumerate every render texture field in the gfx
//   struct, set its .filter field, and re-apply the filter if already loaded.
gX_I0 gX_RendererGFX_Textures_ApplyFilter(gX_RendererGFX *gfx)
{
    gX_I32 filter = gfx->state.textureFilter;
    gX_F32 lodBias = gfx->state.mipmapBias;

    //opencode: helper lambda (well, macro-like pattern) to sync one RT.
    //   Only applies filter to COLOR textures — depth must stay GL_NEAREST.
    //   Depth textures store non-linear Z values; linear filtering interpolates
    //   between foreground and background at edges, producing garbage depth.
#define SYNC_RT(rtField) \
    do { \
        gX_RendererGFX_RenderTexture *rt_ = &gfx->rtField; \
        gX_I32 oldFilter_ = rt_->filter; \
        rt_->filter = filter; \
        if (rt_->wrapper.loadingState.loaded && oldFilter_ != filter) { \
            gX_ApplyTexParamsToGL(rt_->wrapper.rt.texture, filter, lodBias); \
        } \
    } while(0)

    //opencode: HDR scene + bloom chain (hdrMSAA excluded: multisample
    //   textures take no sampler/filter params — SetFilter would GL-error)
    SYNC_RT(bloom.hdrScene);
    SYNC_RT(bloom.extract);
    SYNC_RT(bloom.ping);
    SYNC_RT(bloom.pong);
    SYNC_RT(bloom.scene);

    //opencode: planar reflection
    SYNC_RT(reflection.planar);

    //opencode: SSR
    SYNC_RT(ssr.map);
    SYNC_RT(ssr.normalMap);

    //opencode: SSAO chain
    SYNC_RT(ssao.map);
    SYNC_RT(ssao.blurMap);
    SYNC_RT(ssao.gtaoAccumA);
    SYNC_RT(ssao.gtaoAccumB);
    SYNC_RT(ssao.ssgiMap);
    SYNC_RT(ssao.ssgiBlurMap);
    SYNC_RT(ssao.ssgiAccumA);
    SYNC_RT(ssao.ssgiAccumB);

    //opencode: refraction background
    SYNC_RT(debugVis.refractionBg);

    //opencode: god ray gen map
    SYNC_RT(godray.genMap);

    //opencode: volumetric fog gen map
    SYNC_RT(fogVolume.genMap);

    //opencode: motion blur
    SYNC_RT(motionBlur.map);

    //opencode: DOF chain
    SYNC_RT(dof.cocMap);
    SYNC_RT(dof.blurMap);
    SYNC_RT(dof.blurPing);

    //opencode: TAA chain
    SYNC_RT(taa.historyMap);
    SYNC_RT(taa.resolveMap);

    //opencode: CSM cascade shadow map (array texture — no individual RT to sync)

    //opencode: sun shadow map (stored in maps heap at index 0)
    {
        gX_RendererGFX_RenderTexture *sunMap = (gX_RendererGFX_RenderTexture*)gfx->maps.data;
        if (sunMap && gfx->maps.amount > 0) {
            gX_I32 oldF = sunMap->filter;
            sunMap->filter = filter;
            if (sunMap->wrapper.loadingState.loaded && oldF != filter) {
                if (gfx->sun.camera.map) {
                    //opencode: color only — depth stays GL_NEAREST
                    gX_ApplyTexParamsToGL(gfx->sun.camera.map->texture, filter, lodBias);
                }
            }
        }
    }

#undef SYNC_RT

    //opencode: apply filter + LOD bias to loaded file textures in the textures heap
    {
        GX_HEAP_FOREACH(gfx->textures, gX_RendererGFX_Texture, tex) {
            gX_Texture_Wrapper *tw = &tex->wrapper;
            if (tw->loadingState.loaded) {
                //opencode: use deferred filter change via wrapper
                tw->filter = filter;
                tw->filterChanged = true;
                //opencode: apply LOD bias directly (not tracked by wrapper)
                if (tw->texture.id > 0) {
                    gX_RHI_Tex_Bind(tw->texture.id, gX_RHI_TEXTURE_2D);
                    gX_RHI_Tex_Paramf(tw->texture.id, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_LOD_BIAS, lodBias);
                }
            }
        }
    }

    //opencode: apply to GFX-owned textures (detail map, cookie fallback)
    if (gfx->detail.map.id > 0) {
        gX_ApplyTexParamsToGL(gfx->detail.map, filter, lodBias);
    }
    if (gfx->common.cookieFallback.id > 0) {
        gX_ApplyTexParamsToGL(gfx->common.cookieFallback, filter, lodBias);
    }
}

gX_I32 gX_RendererGFX_Texture_Init(gX_RendererGFX_Texture *texture) {
    texture->wrapper.loadingState.loadAsap = true;
    //opencode: mirror the RenderTexture_Init guard — a stale loaded=true from
    //   reused heap memory blocked the load (preset textures never loaded).
    texture->wrapper.loadingState.loaded = false;
    texture->wrapper.loadingState.reload = false;
    return 0;
}

gX_I32 gX_RendererGFX_Texture_Load(gX_RendererGFX_Texture *texture) {
    if (texture->wrapper.isSkybox) {
        texture->wrapper.texture = gX_Texture_Load_Cubemap(texture->wrapper.path,texture->wrapper.skyboxLayout);
        //ai: failed cubemap loads must remain unavailable to the image background.
        texture->wrapper.loadingState.loaded = texture->wrapper.texture.id > 0;
        return texture->wrapper.loadingState.loaded ? 0 : -1;
    }
    else { texture->wrapper.texture = gX_Texture_Load(texture->wrapper.path); }
    texture->wrapper.loadingState.loaded = true;
    return 0;
}

//opencode: explicitly set loaded=false to prevent uninitialized heap memory from blocking Load
gX_I32 gX_RendererGFX_RenderTexture_Init(gX_RendererGFX_RenderTexture *map) {
    map->wrapper.loadingState.loadAsap = true;
    map->wrapper.loadingState.loaded = false;
    map->wrapper.loadingState.reload = false;
    map->wrapper.width = gX_GFX_SHADOW_CUBE_SIZE; //opencode: increased from 1024 for sharper shadows
    map->wrapper.height = gX_GFX_SHADOW_CUBE_SIZE;
    map->filter = gX_TEXTURE_FILTER_BILINEAR; //opencode: default, overridden by gfx->state.textureFilter
    return 0;
}

gX_I32 gX_RendererGFX_RenderTexture_Load(gX_RendererGFX_RenderTexture *map) {
    if (map->hdr) map->wrapper.rt = gX_RenderTexture_Load_HDR(map->wrapper.width, map->wrapper.height);
    else          map->wrapper.rt = gX_RenderTexture_Load(map->wrapper.width, map->wrapper.height);
    //opencode: wrap + filter setup.
    //   CRITICAL: depth texture MUST use GL_NEAREST filtering. Linear filtering
    //   of depth values interpolates between foreground and background depth at
    //   silhouette edges, producing incorrect intermediate depths that break
    //   SSAO (white occlusion at edges), SSR (stripey reflections), DOF CoC
    //   (wrong blur radius), and TAA (depth rejection fails).
    //   The color texture uses map->filter (from gfx->state.textureFilter).
    gX_Texture_SetWrap(map->wrapper.rt.depth, gX_TEXTURE_WRAP_CLAMP);
    gX_Texture_SetFilter(map->wrapper.rt.depth, gX_TEXTURE_FILTER_POINT);
    //opencode: ensure depth texture returns raw depth, not shadow comparison result.
    //   rlgl may set GL_TEXTURE_COMPARE_MODE = GL_COMPARE_R_TO_TEXTURE on depth
    //   textures (for shadow map samplers), causing sampler2D reads to return 0/1.
    if (map->wrapper.rt.depth.id > 0) {
        gX_RHI_Tex_Param(map->wrapper.rt.depth.id, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_COMPARE_MODE, gX_RHI_NONE);
    }
    gX_Texture_SetWrap(map->wrapper.rt.texture, gX_TEXTURE_WRAP_CLAMP);
    gX_Texture_SetFilter(map->wrapper.rt.texture, map->filter);

    //opencode: add stencil renderbuffer to HDR scene FBO (for stencil testing)
    //   Re-binds the FBO to attach a stencil renderbuffer alongside the depth texture.
    //opencode: stencil renderbuffer disabled — separate GL_STENCIL_INDEX8
    //   renderbuffer + depth TEXTURE is unsupported on many GL implementations
    //   (FBO becomes incomplete). Planar reflections fall back gracefully.

    map->wrapper.loadingState.loaded = true;
    return 0;
}

gX_I32 gX_RendererGFX_Texture_Unload(gX_RendererGFX_Texture *texture) {
    if (texture->wrapper.loadingState.loaded) gX_Texture_Unload(texture->wrapper.texture);
    texture->wrapper.loadingState.loaded = false;
    return 0;
}

gX_I32 gX_RendererGFX_RenderTexture_Unload(gX_RendererGFX_RenderTexture *map) {
    if (map->wrapper.loadingState.loaded) gX_RenderTexture_Unload(map->wrapper.rt);
    //opencode: delete stencil renderbuffer
    if (map->stencilRbo > 0) { gX_RHI_RBO_Delete(map->stencilRbo); map->stencilRbo = 0; }
    map->wrapper.loadingState.loaded = false;
    return 0;
}


gX_I0 gX_RendererGFX_Texture_Update(gX_RendererGFX_Texture *texture) {
    if (texture->wrapper.loadingState.loadAsap && maindata.backendLoaded && !(texture->wrapper.loadingState.loaded)) {
        gX_RendererGFX_Texture_Load(texture);
    }
    //ai: GFX owns this lifecycle, so deferred sampler changes cannot depend on the generic wrapper updater.
    if (texture->wrapper.loadingState.loaded && texture->wrapper.texture.id > 0)
    {
        if (texture->wrapper.filterChanged)
        {
            gX_Texture_SetFilter(texture->wrapper.texture, texture->wrapper.filter);
            texture->wrapper.filterChanged = false;
        }
        if (texture->wrapper.wrapChanged)
        {
            gX_Texture_SetWrap(texture->wrapper.texture, texture->wrapper.wrap);
            texture->wrapper.wrapChanged = false;
        }
    }
}

gX_I0 gX_RendererGFX_RenderTexture_Update(gX_RendererGFX_RenderTexture *map) {
    if (map->wrapper.loadingState.loadAsap && maindata.backendLoaded && !(map->wrapper.loadingState.loaded)) {
        gX_RendererGFX_RenderTexture_Load(map);
    }
}

//opencode: dynamic scene management via heap
gX_I32 gX_RendererGFX_Texture_Add(gX_RendererGFX *gfx, const gX_I8 *path) {
    gX_RendererGFX_Texture *tex = gX_Heap_Add_FirstEmpty_NoPtr(&gfx->textures);
    if (!tex) return -1;
    gX_RendererGFX_Texture_Init(tex);
    strcpy(tex->wrapper.path, path);
    return (gX_I32)((gX_U8*)tex - (gX_U8*)gfx->textures.data) / (gX_I32)sizeof(gX_RendererGFX_Texture);
}

gX_I32 gX_RendererGFX_Texture_Remove(gX_RendererGFX *gfx, gX_U32 index) {
    if (index >= gfx->textures.amount) return -1;
    gX_RendererGFX_Texture *tex = gX_Heap_SeekPtr_Index(&gfx->textures, index);
    if (tex && tex->wrapper.loadingState.loaded) gX_RendererGFX_Texture_Unload(tex);
    gX_Heap_Remove_Index_NoPtr(&gfx->textures, index);
    return 0;
}

gX_I32 gX_RendererGFX_Texture_RemoveAll(gX_RendererGFX *gfx) {
    GX_HEAP_FOREACH(gfx->textures, gX_RendererGFX_Texture, tex) {
        if (tex->wrapper.loadingState.loaded) gX_RendererGFX_Texture_Unload(tex);
    }
    GX_HEAP_REMOVE_ALL(gfx->textures);
    return 0;
}
