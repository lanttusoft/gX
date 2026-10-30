/**********************************************************************************************
* 
*   [common/enum/texture.h]
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
#ifndef ENUM_TEXTURE_H
#define ENUM_TEXTURE_H

#include "../types/primary.h"

//raylib
// Texture parameters: filter mode
// NOTE 1: Filtering considers mipmaps if available in the texture
// NOTE 2: Filter is accordingly set for minification and magnification
#define gX_TEXTUREFILTER \
    X(POINT)            /* // No filter, just pixel approximation */ \
    X(BILINEAR)         /* // Linear filtering */ \
    X(TRILINEAR)        /* // Trilinear filtering (linear with mipmaps) */ \
    X(ANISOTROPIC_4X)   /* // Anisotropic filtering 4x */ \
    X(ANISOTROPIC_8X)   /* // Anisotropic filtering 8x */ \
    X(ANISOTROPIC_16X)  /* // Anisotropic filtering 16x */ \
    
//raylib
// Texture parameters: wrap mode
#define gX_TEXTUREWRAP \
    X(REPEAT)           /* // Repeats texture in tiled moden */ \
    X(CLAMP)            /* // Clamps texture to edge pixel in tiled mode */ \
    X(MIRROR_REPEAT)    /* // Mirrors and repeats the texture in tiled mode*/ \
    X(MIRROR_CLAMP)     /* // Mirrors and clamps to border the texture in tiled mode */ \
    
//raylib
// Cubemap layouts
#define gX_CUBEMAP_LAYOUTS \
    X(AUTO_DETECT)          /* // Automatically detect layout typen */ \
    X(LINE_VERTICAL)        /* // Layout is defined by a vertical line with faces */ \
    X(LINE_HORIZONTAL)      /* // Layout is defined by a horizontal line with faces */ \
    X(CROSS_THREE_BY_FOUR)  /* // Layout is defined by a 3x4 cross with cubemap faces */ \
    X(CROSS_FOUR_BY_THREE)  /* // Layout is defined by a 4x3 cross with cubemap faces */ \
    X(PANORAMA)             /* // Layout is defined by a panorama image (equirrectangular map) */ \

    
typedef enum {
#define X(fil) gX_TEXTURE_FILTER_##fil,
    gX_TEXTUREFILTER
#undef X
} gX_TextureFilter;
const gX_I8 *gX_TextureFilter_String(gX_I32 filter);

typedef enum {
#define X(wra) gX_TEXTURE_WRAP_##wra,
    gX_TEXTUREWRAP
#undef X
} gX_TextureWrap;
const gX_I8 *gX_TextureWrap_String(gX_I32 wrap);

typedef enum {
#define X(lay) gX_CUBEMAP_LAYOUT_##lay,
    gX_CUBEMAP_LAYOUTS
#undef X
} gX_CubemapLayout;
const gX_I8 *gX_CubemapLayout_String(gX_I32 layout);

#endif //ENUM_TEXTURE_H
