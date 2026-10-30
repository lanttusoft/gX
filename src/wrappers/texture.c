/**********************************************************************************************
*
*   [gX: src/wrappers/texture.c]
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

#include "wrappers_header.h"
#include "../common/path.h"
#include "texture.h"
#include "../common/config.h"
#include "../main.h"

#include "../rhi/rhi_tex.h"
#include "../rhi/rhi_fbo.h"
#include "../rhi/rhi.h"
#include "../rhi/rhi_bindless.h"
#include "../rhi/rhi_buffer_audit.h"

//opencode: bindless texture support flag — set once at GL init
gX_I1 gX_BindlessSupported = 0;

//opencode: PS1 texture tracker (TIM textures in VRAM)

//opencode: PS2 texture side-table (GS VRAM textures) — non-static for wrappers_ps2.c

//opencode: N64 sprite side-table

//opencode: fallback texture for missing files — 4×4 pink checkerboard
static const gX_U8 g_fallbackTexturePixels[64] = {
    255,0,255,255,  0,0,0,255,   255,0,255,255,  0,0,0,255,
    0,0,0,255,     255,0,255,255,  0,0,0,255,   255,0,255,255,
    255,0,255,255,  0,0,0,255,   255,0,255,255,  0,0,0,255,
    0,0,0,255,     255,0,255,255,  0,0,0,255,   255,0,255,255,
};

gX_Texture gX_Texture_Load(const gX_I8 *path) {
    //opencode: D12 — backend loader (raylib_Texture_Load handles the GLFW
    //   immutable-storage re-upload internally)
    gX_Texture textu = raylib_Texture_Load(path);
    //opencode: v0.3.8 - guest fallback: registered guest roots may carry the
    //   texture when gX's own resources lack it (loaded from GX_ASSETS dirs)
    if (textu.id == 0) {
        const gX_I8 *guest = gX_Path_GuestFallback("texture", path);
        if (guest) {
            gX_TraceLog(gX_LOG_INFO, "Texture guest fallback '%s' -> '%s'", path ? path : "?", guest);
            textu = raylib_Texture_Load(guest);
        }
    }
    //opencode: fallback — if load failed (id==0), generate pink checkerboard
    if (textu.id == 0) {
        gX_TraceLog(gX_LOG_WARNING, "Texture load failed: %s — using fallback checkerboard", path ? path : "?");
        textu = gX_Texture_LoadFromData(4, 4, g_fallbackTexturePixels);
    }
    return textu;
}

gX_Texture gX_Texture_Load_Cubemap(const gX_I8 *path, gX_I32 layout) {
    //opencode: D12 — backend loader
    return raylib_Texture_LoadCubemap(path, layout);
}

gX_I0 gX_Texture_Unload(gX_Texture tex) {
    //opencode: D12 — backend loader
    raylib_Texture_Unload(tex);
}

gX_I0 gX_Texture_SetFilter(gX_Texture texture, gX_I32 filter) {
    raylib_Texture_SetFilter(texture, filter);
}

gX_I0 gX_Texture_SetWrap(gX_Texture texture, gX_I32 wrap) {
    raylib_Texture_SetWrap(texture, wrap);
}

//opencode: gX_Texture_Draw, Draw_At, Draw_Fullscreen, Draw_Sprite
//   implemented after the RT tracker below.

//opencode: create texture from raw RGBA8888 pixel data
gX_Texture gX_Texture_LoadFromData(gX_U32 width, gX_U32 height, const gX_U8 *data) {
    Image img = { 0 };
    img.data = (void*)data;
    img.width = (int)width;
    img.height = (int)height;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    //opencode: D12 — backend loader
    gX_Image gimg = gX_Image_ConvertFromBackend(img);
    return raylib_Texture_LoadFromImage(gimg);
//opencode: PS2 — upload RGBA8888 data to GS VRAM
}

//opencode: texture update wrapper for analog CRT threaded processing
gX_I0 gX_Texture_Update(gX_Texture texture, const gX_I0 *pixels) {
    raylib_Texture_Update(texture, pixels);
}

//opencode: RT texture ID tracker — auto-invert for render textures
static gX_U32 g_rtTextureIds[gX_TEXTURE_RT_TRACKER_MAX];
static gX_U32 g_rtTextureCount = 0;

gX_I0 gX_Texture_RT_Track(gX_U32 texId) {
    if (texId == 0) return;
    if (g_rtTextureCount >= gX_TEXTURE_RT_TRACKER_MAX) {
        gX_TraceLog(gX_LOG_WARNING, "RT texture tracker full (%d max)", gX_TEXTURE_RT_TRACKER_MAX);
        return;
    }
    g_rtTextureIds[g_rtTextureCount++] = texId;
}

gX_I0 gX_Texture_RT_Untrack(gX_U32 texId) {
    for (gX_U32 i = 0; i < g_rtTextureCount; i++) {
        if (g_rtTextureIds[i] == texId) {
            g_rtTextureIds[i] = g_rtTextureIds[--g_rtTextureCount];
            return;
        }
    }
}

gX_I1 gX_Texture_IsRT(gX_U32 texId) {
    for (gX_U32 i = 0; i < g_rtTextureCount; i++) {
        if (g_rtTextureIds[i] == texId) return 1;
    }
    return 0;
}

//opencode: new texture draw API — center-origin convention
gX_I0 gX_Texture_Draw(gX_Texture texture, gX_Texture_Draw_Params params) {
    //opencode: raylib's queued texture quad bypasses the direct RHI bind hooks.
    gX_RHI_BufferAudit_TextureBind(texture.id, 0, gX_RHI_TEXTURE_2D);
    gX_RHI_BufferAudit_Draw((const gX_I8 *)"TextureDraw", 1);
    raylib_Texture_Draw(texture, params);
}

//opencode: draw texture filling the screen (center-origin, fullscreen size)
gX_I0 gX_Texture_Draw_Fullscreen(gX_Texture texture) {
    gX_Texture_Draw_Params p = {
        .dst = {0, 0, (gX_F32)maindata.screenWidth, (gX_F32)maindata.screenHeight},
        .tint = {255, 255, 255, 255},
        .scale = {1.0f, 1.0f}
    };
    gX_Texture_Draw(texture, p);
}

//opencode: draw texture at position at native size
gX_I0 gX_Texture_Draw_At(gX_Texture texture, gX_F32v2 pos) {
    gX_Texture_Draw_Params p = {
        .dst = {pos.x, pos.y, (gX_F32)texture.width, (gX_F32)texture.height},
        .tint = {255, 255, 255, 255},
        .scale = {1.0f, 1.0f}
    };
    gX_Texture_Draw(texture, p);
}

//opencode: sprite draw — center at pos, explicit size, rotation
gX_I0 gX_Texture_Draw_Sprite(gX_Texture texture, gX_F32v2 pos, gX_F32v2 size, gX_F32 rotation) {
    gX_Texture_Draw_Params p = {
        .dst = {pos.x, pos.y, size.x, size.y},
        .tint = {255, 255, 255, 255},
        .scale = {1.0f, 1.0f},
        .rotation = rotation
    };
    gX_Texture_Draw(texture, p);
}

gX_I0 gX_Texture_Draw3D_Params(gX_Texture texture, gX_Texture_Draw_Params params, gX_F32v3 pos) {
    raylib_Texture_Draw3D(texture, params, pos);
}

gX_RenderTexture gX_RenderTexture_Load(gX_U32 width, gX_U32 height) {
    RenderTexture rt = raylib_RenderTexture_Load((gX_I32)width,(gX_I32)height,true,false);
    gX_RenderTexture gxrt = gX_RenderTexture_ConvertFromBackend(rt);
    gX_Texture_RT_Track(gxrt.texture.id);
    //opencode: register backend-created targets for later lifecycle attribution.
    gX_RHI_BufferAudit_RegisterTarget(gxrt.id, gxrt.texture.id, gxrt.depth.id,
        gxrt.texture.width, gxrt.texture.height, NULL);
    return gxrt;
}

//opencode: HDR float16 render texture for bloom/tonemap pipeline
gX_RenderTexture gX_RenderTexture_Load_HDR(gX_U32 width, gX_U32 height) {
    RenderTexture rt = raylib_RenderTexture_Load((gX_I32)width,(gX_I32)height,true,true);
    gX_RenderTexture gxrt = gX_RenderTexture_ConvertFromBackend(rt);
    gX_Texture_RT_Track(gxrt.texture.id);
    //opencode: register HDR backend targets for lifecycle attribution.
    gX_RHI_BufferAudit_RegisterTarget(gxrt.id, gxrt.texture.id, gxrt.depth.id,
        gxrt.texture.width, gxrt.texture.height, NULL);
    return gxrt;
}

gX_I0 gX_RenderTexture_Unload(gX_RenderTexture rt) {
    //opencode: release audit metadata with the underlying render target.
    gX_RHI_BufferAudit_UnregisterTarget(rt.id);
    gX_Texture_RT_Untrack(rt.texture.id);
    //opencode: D12 — backend loader
    raylib_RenderTexture_Unload(rt);
}

gX_I0 gX_RenderTexture_Draw(gX_RenderTexture rt) {
    //opencode: D10 — draw via the RHI texture path (replaces raylib DrawTexture)
    gX_Texture_Draw_Params p = {0};
    p.dst = (gX_Rectangle){0, 0, (gX_F32)rt.texture.width, (gX_F32)rt.texture.height};
    p.scale = (gX_F32v2){1.0f, 1.0f};
    p.tint = (gX_Color){255, 255, 255, 255};
    gX_Texture_Draw(rt.texture, p);
}

//opencode: Texture_Wrapper implementation (moved from common/utils.c)

gX_I0 gX_Texture_Wrapper_Load(gX_Texture_Wrapper *tw) {
    gX_TraceLog(gX_LOG_TRACE3,"Texture_Wrapper_Load");
    if (tw->isSkybox) {
        tw->texture = gX_Texture_Load_Cubemap(tw->path,tw->skyboxLayout);
    }
    else {
        tw->texture = gX_Texture_Load(tw->path);
    }
    tw->loadingState.loaded = true;
}

gX_I0 gX_Texture_Wrapper_Unload(gX_Texture_Wrapper *tw) {
    gX_TraceLog(gX_LOG_TRACE3,"Texture_Wrapper_Unload");
    //opencode: release bindless handle before unloading the GL texture
    if (tw->handleResident && tw->bindlessHandle != 0) {
        gX_Texture_ReleaseHandle(tw->bindlessHandle);
    }
    tw->bindlessHandle = 0;
    tw->handleResident = false;
    gX_Texture_Unload(tw->texture);
    tw->loadingState.loaded = false;
}

gX_I0 gX_Texture_Wrapper_Update(gX_Texture_Wrapper *tw) {
    gX_I1 loadasapstate = (maindata.backendLoaded && tw->loadingState.loadAsap);
    gX_I1 loadbeforestate = false;
    gX_I1 unloadafterstate = false;
    if ((tw->loadingState.load || loadasapstate || loadbeforestate) && !(tw->loadingState.loaded)) {
        gX_Texture_Wrapper_Load(tw);
    }
    if ((tw->loadingState.unload || unloadafterstate) && tw->loadingState.loaded) {
        gX_Texture_Wrapper_Unload(tw);
    }
    if (tw->loadingState.reload && tw->loadingState.loaded) {
        gX_Texture_Wrapper_Unload(tw);
        gX_Texture_Wrapper_Load(tw);
    }
    if (tw->filterChanged && tw->loadingState.loaded) {
        gX_Texture_SetFilter(tw->texture,tw->filter);
        tw->filterChanged = false;
    }
    if (tw->wrapChanged && tw->loadingState.loaded) {
        gX_Texture_SetWrap(tw->texture,tw->wrap);
        tw->wrapChanged = false;
    }

    //opencode: generate bindless handle after texture is first loaded
    if (tw->loadingState.loaded && tw->bindlessHandle == 0 && gX_BindlessSupported && tw->texture.id > 0) {
        tw->bindlessHandle = gX_Texture_GetHandle(tw->texture);
        if (tw->bindlessHandle != 0) {
            gX_Texture_MakeResident(tw->bindlessHandle);
            tw->handleResident = true;
        }
    }
}

//opencode: bindless texture — machinery lives in RHI (rhi_bindless.c);
//   the extension's function pointers are loaded via the backend
//   proc-address callback. These wrappers only delegate.

gX_I0 gX_Bindless_CheckSupport(gX_I0)
{
    gX_BindlessSupported = gX_RHI_Bindless_Init();
}

gX_U64 gX_Texture_GetHandle(gX_Texture texture)
{
    if (!gX_BindlessSupported || texture.id == 0) return 0;
    return gX_RHI_Bindless_GetHandle(texture.id, 0);
}

gX_I1 gX_Texture_MakeResident(gX_U64 handle)
{
    if (!gX_BindlessSupported) return 0;
    return gX_RHI_Bindless_MakeResident(handle);
}

gX_I0 gX_Texture_ReleaseHandle(gX_U64 handle)
{
    if (!gX_BindlessSupported) return;
    gX_RHI_Bindless_Release(handle);
}

gX_I0 gX_Shader_SetValue_Handle64(gX_Shader shader, gX_I32 loc, gX_U64 handle)
{
    if (loc < 0 || shader.id == 0) return;
    gX_RHI_Shader_Use(shader.id);
    gX_RHI_Bindless_UniformHandle64(loc, handle);
}
