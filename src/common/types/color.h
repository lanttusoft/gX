/**********************************************************************************************
* 
*   [common/types_color.h]
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

#ifndef TYPES_COLOR_H
#define TYPES_COLOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "primary.h"

// [raylib] borrowed
// Color, 4 components, R8G8B8A8 (32bit) — storage format, compatible with pixel data
typedef struct gX_Color {
    gX_U8 r;
    gX_U8 g;
    gX_U8 b;
    gX_U8 a;
} gX_Color;

//opencode: float color for shader work (no /255 conversion needed)
typedef struct gX_FColor {
    gX_F32 r;
    gX_F32 g;
    gX_F32 b;
    gX_F32 a;
} gX_FColor;

//opencode: conversion helpers
static inline gX_FColor gX_Color_ToF32(gX_Color c) {
    gX_FColor fc = { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f };
    return fc;
}
static inline gX_Color gX_Color_FromF32(gX_FColor fc) {
    gX_Color c = { (gX_U8)(fc.r * 255.0f), (gX_U8)(fc.g * 255.0f), (gX_U8)(fc.b * 255.0f), (gX_U8)(fc.a * 255.0f) };
    return c;
}

typedef struct gX_ColorGradient {
    gX_Color start;
    gX_Color end;
    gX_I32   easing;
} gX_ColorGradient;

typedef struct gX_ColorGradientArray {
    gX_U32  amount;
    gX_ColorGradient gradients[8];
    gX_F32  pivots[8];
} gX_ColorGradientArray;

#ifdef __cplusplus
}
#endif

#endif //TYPES_COLOR_H
