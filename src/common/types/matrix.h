/**********************************************************************************************
* 
*   [common/types_matrix.h]
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

#ifndef TYPES_MATRIX_H
#define TYPES_MATRIX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "primary.h"

typedef struct gX_F32m2x2 {
    gX_F32 m0, m2;
    gX_F32 m1, m3;
} gX_F32m2x2;

typedef struct gX_F32m2x3 {
    gX_F32 m0, m3;
    gX_F32 m1, m4;
    gX_F32 m2, m5;
} gX_F32m2x3;

typedef struct gX_F32m2x4 {
    gX_F32 m0, m4;
    gX_F32 m1, m5;
    gX_F32 m2, m6;
    gX_F32 m3, m7;
} gX_F32m2x4;

typedef struct gX_F32m3x2 {
    gX_F32 m0, m2, m4;
    gX_F32 m1, m3, m5;
} gX_F32m3x2;

typedef struct gX_F32m3x3 {
    gX_F32 m0, m3, m6;
    gX_F32 m1, m4, m7;
    gX_F32 m2, m5, m8;
} gX_F32m3x3;

typedef struct gX_F32m3x4 {
    gX_F32 m0, m4, m8;
    gX_F32 m1, m5, m9;
    gX_F32 m2, m6, m10;
    gX_F32 m3, m7, m11;
} gX_F32m3x4;

typedef struct gX_F32m4x2 {
    gX_F32 m0, m2, m4, m6;
    gX_F32 m1, m3, m5, m7;
} gX_F32m4x2;

typedef struct gX_F32m4x3 {
    gX_F32 m0, m3, m6, m9;
    gX_F32 m1, m4, m7, m10;
    gX_F32 m2, m5, m8, m11;
} gX_F32m4x3;

// [raylib] borrowed
// Matrix, 4x4 components, column major, OpenGL style, right-handed
typedef struct gX_F32m4x4 {
    gX_F32 m0, m4, m8,  m12; // Matrix first row (4 components)
    gX_F32 m1, m5, m9,  m13; // Matrix second row (4 components)
    gX_F32 m2, m6, m10, m14; // Matrix third row (4 components)
    gX_F32 m3, m7, m11, m15; // Matrix fourth row (4 components)
} gX_F32m4x4;
typedef gX_F32m4x4 gX_Matrix;

typedef struct gX_F64m4x4 {
    gX_F64 m0, m4, m8,  m12; // Matrix first row (4 components)
    gX_F64 m1, m5, m9,  m13; // Matrix second row (4 components)
    gX_F64 m2, m6, m10, m14; // Matrix third row (4 components)
    gX_F64 m3, m7, m11, m15; // Matrix fourth row (4 components)
} gX_F64m4x4;

typedef struct gX_F32a16 {
    gX_F32 v[16];
} gX_F32a16;

#ifdef __cplusplus
}
#endif

#endif //TYPES_MATRIX_H
