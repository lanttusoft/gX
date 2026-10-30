/**********************************************************************************************
* 
*   [common/types_texture.h]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025 lanttu
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

#ifndef TYPES_TEXTURE_H
#define TYPES_TEXTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "color.h"
#include "primary.h"
#include "shape.h"
#include "vectors.h"

// [raylib] borrowed
// Image, pixel data stored in CPU memory (RAM)
typedef struct gX_Image {
    gX_I0 *data;    // Image raw data
    gX_I32 width;   // Image base width
    gX_I32 height;  // Image base height
    gX_I32 mipmaps; // Mipmap levels, 1 by default
    gX_I32 format;  // Data format (PixelFormat type)
} gX_Image;

// [raylib] borrowed
// Texture, tex data stored in GPU memory (VRAM)
typedef struct gX_Texture {
    gX_U32 id;      // OpenGL texture id
    gX_I32 width;   // Texture base width
    gX_I32 height;  // Texture base height
    gX_I32 mipmaps; // Mipmap levels, 1 by default
    gX_I32 format;  // Data format (PixelFormat type)
} gX_Texture;

//opencode: clean texture draw params — center-origin convention.
//   Pipeline: src(texture rect) -> dst(screen rect) -> rotate -> offset+origin -> tint
//   src:     texture sub-rect to sample (w==0||h==0 = full texture)
//   dst:     destination rect on screen/RT (top-left position + size)
//   rotation: degrees around pivot
//   origin:  pivot offset from CENTER of dst (default {0,0} = center)
//   offset:  extra translation after rotation
//   scale:   size multiplier. Negative = flip around center (replaces old invertX/Y)
//   tint:    color modulation (default gX_WHITE)
typedef struct gX_Texture_Draw_Params {
    gX_Rectangle    src;
    gX_Rectangle    dst;
    gX_F32          rotation;
    gX_F32v2        origin;
    gX_F32v2        offset;
    gX_F32v2        scale;
    gX_Color        tint;
} gX_Texture_Draw_Params;

// [raylib] borrowed
// RenderTexture, fbo for texture rendering
typedef struct gX_RenderTexture {
    gX_U32      id;         // OpenGL framebuffer object id
    gX_Texture  texture;    // Color buffer attachment texture
    gX_Texture  depth;      // Depth buffer attachment texture
} gX_RenderTexture;

typedef struct gX_RT_PingPong {
    gX_RenderTexture ping;
    gX_RenderTexture pong;
} gX_RT_PingPong;

typedef struct gX_RT_DownSample {
    gX_U32 startWidth;
    gX_U32 startHeight;
    gX_U32 amount; //each cycle halves resolution
    gX_RenderTexture rt[16];
} gX_RT_DownSample;

typedef struct gX_GBuffer {
    gX_RenderTexture position;
    
    gX_RenderTexture texcoord;
    gX_RenderTexture texcoord2;
    
    gX_RenderTexture normal;
    gX_RenderTexture tangent;
    
    gX_RenderTexture diffuse;
    gX_RenderTexture specular;
    
    gX_RenderTexture color;
} gX_GBuffer;

#ifdef __cplusplus
}
#endif

#endif //TYPES_TEXTURE_H
