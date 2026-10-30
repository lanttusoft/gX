/**********************************************************************************************
* 
*   [wrappers/texture.h]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025-2026 lanttu, opencode
* 
*   [raylib]
*   LICENSE: zlib/libpng
*
*   raylib is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software:
*
*   Copyright (c) 2013-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/
//lanttu: original wrapper for raylib texture/rendertarget functions
#ifndef WRAPPERS_TEXTURE_H
#define WRAPPERS_TEXTURE_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "../common/config.h"
#include "../common/types.h"

//opencode: global bindless texture support flag — set at init time after
//   checking GL_ARB_bindless_texture. When true, gX_Texture_MakeResident
//   and related functions are available. Platform/GPU-dependent.
extern gX_I1 gX_BindlessSupported;

//opencode: Texture_Wrapper — deferred-loading texture with path + filter state
typedef struct gX_Texture_Wrapper {
    gX_LoadingState loadingState;
    
    gX_I1       filterChanged;
    gX_I1       wrapChanged;
    gX_I32      filter;
    gX_I32      wrap;
    
    gX_I1       isSkybox;
    gX_I32      skyboxLayout;
    
    gX_TextLine path;
    
    gX_Texture  texture;

    //opencode: bindless texture handle — 64-bit handle for GL_ARB_bindless_texture.
    //   0 = not yet generated or not supported. Generated after texture is loaded
    //   when gX_BindlessSupported is true. Must be made resident before use.
    gX_U64      bindlessHandle;
    gX_I1       handleResident;
} gX_Texture_Wrapper;

typedef struct gX_RenderTexture_Wrapper {
    gX_LoadingState loadingState;
    
    gX_U32 width;
    gX_U32 height;
    
    gX_RenderTexture  rt;
} gX_RenderTexture_Wrapper;

gX_I0 gX_Texture_Wrapper_Load(gX_Texture_Wrapper *tw);
gX_I0 gX_Texture_Wrapper_Unload(gX_Texture_Wrapper *tw);
gX_I0 gX_Texture_Wrapper_Update(gX_Texture_Wrapper *tw);

gX_Texture  gX_Texture_Load(            const gX_I8 *path);
gX_Texture  gX_Texture_Load_Cubemap(    const gX_I8 *path, gX_I32 layout);
//opencode: create texture from raw RGBA8888 pixel data
gX_Texture  gX_Texture_LoadFromData(    gX_U32 width, gX_U32 height, const gX_U8 *data);
gX_I0       gX_Texture_Unload(          gX_Texture texture);
gX_I0       gX_Texture_GenMipmaps(      gX_Texture *texture);
gX_I0       gX_Texture_SetFilter(       gX_Texture texture, gX_I32 filter);
gX_I0       gX_Texture_SetWrap(         gX_Texture texture, gX_I32 wrap);
gX_I0       gX_Texture_Update(          gX_Texture texture, const gX_I0 *pixels);

//opencode: texture draw API — center-origin convention, auto-invert for RTs
gX_I0       gX_Texture_Draw(            gX_Texture texture, gX_Texture_Draw_Params params);
gX_I0       gX_Texture_Draw_Fullscreen( gX_Texture texture);
gX_I0       gX_Texture_Draw_At(         gX_Texture texture, gX_F32v2 pos);
gX_I0       gX_Texture_Draw_Sprite(     gX_Texture texture, gX_F32v2 pos, gX_F32v2 size, gX_F32 rotation);
gX_I0       gX_Texture_Draw3D_Params(   gX_Texture texture, gX_Texture_Draw_Params params, gX_F32v3 pos);

gX_RenderTexture gX_RenderTexture_Load(     gX_U32 width, gX_U32 height);
gX_RenderTexture gX_RenderTexture_Load_HDR( gX_U32 width, gX_U32 height);
gX_I0            gX_RenderTexture_Unload(   gX_RenderTexture rt);
gX_I0            gX_RenderTexture_Draw(     gX_RenderTexture rt);

//opencode: bindless texture API — GL_ARB_bindless_texture wrappers.
//   Generate a 64-bit handle for a loaded texture, make resident for shader
//   access, and release when done. Handles are automatically cleaned up on
//   texture unload. All functions are no-ops when gX_BindlessSupported is false.
//   Implementation delegates to src/rhi/rhi_bindless.c (the extension's
//   function pointers live in RHI, loaded via the backend proc-address fn).
gX_I0  gX_Bindless_CheckSupport(gX_I0);    //opencode: set gX_BindlessSupported flag
gX_U64 gX_Texture_GetHandle(gX_Texture texture);
gX_I1  gX_Texture_MakeResident(gX_U64 handle);
gX_I0  gX_Texture_ReleaseHandle(gX_U64 handle);

//opencode: RT texture ID tracker — auto-invert for render textures.
//   Render textures (FBO color attachments) have UV(0,0) = bottom of visual
//   content due to raylib's Y-down ortho projection. The draw backend
//   auto-negates src.height for RT sources so they display correctly.
//   File-loaded textures have UV(0,0) = top of image (no flip at load time).
#define gX_TEXTURE_RT_TRACKER_MAX 128
gX_I0 gX_Texture_RT_Track(gX_U32 texId);
gX_I0 gX_Texture_RT_Untrack(gX_U32 texId);
gX_I1 gX_Texture_IsRT(gX_U32 texId);
    
#ifdef __cplusplus
}
#endif

#endif //WRAPPERS_TEXTURE_H
