/**********************************************************************************************
*
*   gxmath/color.c
*   Copyright (c) 2025 nauris
* 
*   Uses functions from: raymath v1.5
* 
* 
*   <raymath> LICENSE: zlib/libpng
* 
*   Copyright (c) 2015-2023 Ramon Santamaria (@raysan5)
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

//lanttu: original gxmath implementation for color operations

#include "color.h"

//opencode: per-channel lerp in signed arithmetic, clamped to [0,255] —
//   casting a negative float to gX_U8 is undefined behavior (C11 6.3.1.4);
//   the old wrap happened to give the right value on x86 but is not portable.
static inline gX_U8 gX_Color_LerpChannel(gX_U8 a, gX_U8 b, gX_F32 t) {
    gX_I32 v = (gX_I32)a + (gX_I32)( t * (gX_F32)((gX_I32)b - (gX_I32)a) );
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (gX_U8)v;
}

inline gX_Color gX_Color_Lerp(gX_Color c1, gX_Color c2, gX_F32 t) {
    gX_Color result = { 0 };
    result.r = gX_Color_LerpChannel(c1.r, c2.r, t);
    result.g = gX_Color_LerpChannel(c1.g, c2.g, t);
    result.b = gX_Color_LerpChannel(c1.b, c2.b, t);
    result.a = gX_Color_LerpChannel(c1.a, c2.a, t);
    return result;
}

inline gX_Color gX_Color_LerpRGB(gX_Color c1, gX_Color c2, gX_F32 t) {
    gX_Color result = { 0 };
    result.r = gX_Color_LerpChannel(c1.r, c2.r, t);
    result.g = gX_Color_LerpChannel(c1.g, c2.g, t);
    result.b = gX_Color_LerpChannel(c1.b, c2.b, t);
    result.a = 255;
    return result;
}

inline gX_Color gX_Color_Mul(gX_Color c1, gX_Color c2) {
    gX_Color result = { 0 };
    gX_F32v4 temp1 = gX_Color_ToV4(c1);
    gX_F32v4 temp2 = gX_Color_ToV4(c2);
    temp1.x *= temp2.x;
    temp1.y *= temp2.y;
    temp1.z *= temp2.z;
    temp1.w *= temp2.w;
    result = gX_Color_FromV4(temp1);
    return result;
}

inline gX_Color gX_Color_FromV4(gX_F32v4 v4) {
    gX_Color result = { 0 };
    result.r = (gX_U8)(v4.x*255.0f);
    result.g = (gX_U8)(v4.y*255.0f);
    result.b = (gX_U8)(v4.z*255.0f);
    result.a = (gX_U8)(v4.w*255.0f);
    return result;
}

inline gX_F32v4 gX_Color_ToV4(gX_Color col) {
    gX_F32v4 result = { 0 };
    result.x = ((gX_F32)(col.r))/255.0f;
    result.y = ((gX_F32)(col.g))/255.0f;
    result.z = ((gX_F32)(col.b))/255.0f;
    result.w = ((gX_F32)(col.a))/255.0f;
    return result;
}

inline gX_Color gX_Color_FromV4Raw(gX_F32v4 v4) {
    gX_Color result = { 0 };
    result.r = (gX_U8)(v4.x);
    result.g = (gX_U8)(v4.y);
    result.b = (gX_U8)(v4.z);
    result.a = (gX_U8)(v4.w);
    return result;
}

inline gX_F32v4 gX_Color_ToV4Raw(gX_Color col) {
    gX_F32v4 result = { 0 };
    result.x = ((gX_F32)(col.r));
    result.y = ((gX_F32)(col.g));
    result.z = ((gX_F32)(col.b));
    result.w = ((gX_F32)(col.a));
    return result;
}
