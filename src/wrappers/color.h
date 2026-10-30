/**********************************************************************************************
* 
*   [wrappers/color.h]
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
//opencode: color utility functions
#ifndef WRAPPERS_COLOR_H
#define WRAPPERS_COLOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/config.h"
#include "../common/types.h"

gX_Color  gX_Color_FromHSV(       gX_F32 hue, gX_F32 saturation, gX_F32 value);
gX_Color  gX_Color_FromHex(       gX_U32 hexValue);
gX_Color  gX_Color_Alpha(         gX_Color color, gX_F32 alpha);
gX_Color  gX_Color_AlphaBlend(    gX_Color dst, gX_Color src, gX_Color tint);
gX_Color  gX_Color_Fade(          gX_Color color, gX_F32 alpha);
gX_I1     gX_Color_IsEqual(       gX_Color col1, gX_Color col2);

gX_F32v3  gX_Color_ToHSV(         gX_Color color);
gX_F32v4  gX_Color_Normalize(     gX_Color color);
gX_Color  gX_Color_FromNormalized(gX_F32v4 normalized);

gX_Color  gX_Color_GetPixel(      gX_I0 *srcPtr, gX_I32 format);
gX_I0     gX_Color_SetPixel(      gX_I0 *dstPtr, gX_Color color, gX_I32 format);

gX_I32    gX_Color_GetPixelDataSize(gX_I32 width, gX_I32 height, gX_I32 format);

#ifdef __cplusplus
}
#endif

#endif //WRAPPERS_COLOR_H
