/**********************************************************************************************
* 
*   [wrappers/color.c]
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
#include "wrappers_header.h"
#include "color.h"

//opencode: gX_Color_FromHSV — plain HSV→RGB math
gX_Color gX_Color_FromHSV(gX_F32 hue, gX_F32 saturation, gX_F32 value) {
    Color col = ColorFromHSV(hue, saturation, value);
    return gX_Color_ConvertFromBackend(col);
}

//opencode: gX_Color_FromHex — parse 0xRRGGBBAA or 0xRRGGBB
gX_Color gX_Color_FromHex(gX_U32 hexValue) {
    Color col = GetColor(hexValue);
    return gX_Color_ConvertFromBackend(col);
}

//opencode: gX_Color_Alpha — multiply alpha channel
gX_Color gX_Color_Alpha(gX_Color color, gX_F32 alpha) {
    Color col = gX_Color_ConvertToBackend(color);
    col = ColorAlpha(col, alpha);
    return gX_Color_ConvertFromBackend(col);
}

//opencode: gX_Color_AlphaBlend — simple alpha blend
gX_Color gX_Color_AlphaBlend(gX_Color dst, gX_Color src, gX_Color tint) {
    Color d = gX_Color_ConvertToBackend(dst);
    Color s = gX_Color_ConvertToBackend(src);
    Color t = gX_Color_ConvertToBackend(tint);
    Color col = ColorAlphaBlend(d, s, t);
    return gX_Color_ConvertFromBackend(col);
}

//opencode: gX_Color_Fade — fade (multiply alpha)
gX_Color gX_Color_Fade(gX_Color color, gX_F32 alpha) {
    Color col = gX_Color_ConvertToBackend(color);
    col = Fade(col, alpha);
    return gX_Color_ConvertFromBackend(col);
}

//opencode: gX_Color_IsEqual — compares RGBA channels
gX_I1 gX_Color_IsEqual(gX_Color col1, gX_Color col2) {
    return (col1.r == col2.r && col1.g == col2.g && col1.b == col2.b && col1.a == col2.a);
}

//opencode: gX_Color_ToHSV — plain RGB→HSV math
gX_F32v3 gX_Color_ToHSV(gX_Color color) {
    Color col = gX_Color_ConvertToBackend(color);
    Vector3 hsv = ColorToHSV(col);
    return gX_F32v3_ConvertFromBackend(hsv);
}

//opencode: gX_Color_Normalize — divides channels by 255
gX_F32v4 gX_Color_Normalize(gX_Color color) {
    gX_F32v4 result = (gX_F32v4){0};
    result.x = (gX_F32)color.r / 255.0f;
    result.y = (gX_F32)color.g / 255.0f;
    result.z = (gX_F32)color.b / 255.0f;
    result.w = (gX_F32)color.a / 255.0f;
    return result;
}

//opencode: gX_Color_FromNormalized — multiplies channels by 255
gX_Color gX_Color_FromNormalized(gX_F32v4 normalized) {
    gX_Color result = (gX_Color){0};
    result.r = (gX_U8)(normalized.x * 255.0f);
    result.g = (gX_U8)(normalized.y * 255.0f);
    result.b = (gX_U8)(normalized.z * 255.0f);
    result.a = (gX_U8)(normalized.w * 255.0f);
    return result;
}

//opencode: gX_Color_GetPixel — memcpy from RGBA pixel
gX_Color gX_Color_GetPixel(gX_I0 *srcPtr, gX_I32 format) {
    Color col = GetPixelColor(srcPtr, format);
    return gX_Color_ConvertFromBackend(col);
}

//opencode: gX_Color_SetPixel — memcpy RGBA pixel
gX_I0 gX_Color_SetPixel(gX_I0 *dstPtr, gX_Color color, gX_I32 format) {
    Color col = gX_Color_ConvertToBackend(color);
    SetPixelColor(dstPtr, col, format);
}

//opencode: gX_Color_GetPixelDataSize — RGBA = 4 bytes per pixel
gX_I32 gX_Color_GetPixelDataSize(gX_I32 width, gX_I32 height, gX_I32 format) {
    return raylib_Color_GetPixelDataSize(width, height, format);
}
