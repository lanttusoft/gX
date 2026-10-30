/**********************************************************************************************
* 
*   [common/types_vectors.h]
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

#ifndef TYPES_VECTORS_H
#define TYPES_VECTORS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "primary.h"
    
#define gX_TYPES_VECTOR_INT
#define gX_TYPES_VECTOR_UINT

// [raylib] borrowed
// Vector2, 2 components
#define gX_TYPES_V2_DEF(dt) \
typedef struct gX_##dt##v2 { \
    gX_##dt x; \
    gX_##dt y; \
} gX_##dt##v2; \

// [raylib] borrowed
// Vector3, 3 components
#define gX_TYPES_V3_DEF(dt) \
typedef struct gX_##dt##v3 { \
    gX_##dt x; \
    gX_##dt y; \
    gX_##dt z; \
} gX_##dt##v3; \

// [raylib] borrowed
// Vector4, 4 components
#define gX_TYPES_V4_DEF(dt) \
typedef struct gX_##dt##v4 { \
    gX_##dt x; \
    gX_##dt y; \
    gX_##dt z; \
    gX_##dt w; \
} gX_##dt##v4; \

#define gX_TYPES_V234_DEF(dt) \
    gX_TYPES_V2_DEF(dt) \
    gX_TYPES_V3_DEF(dt) \
    gX_TYPES_V4_DEF(dt) \

#if defined(gX_TYPES_VECTOR_INT)
gX_TYPES_V234_DEF(I8)
gX_TYPES_V234_DEF(I16)
gX_TYPES_V234_DEF(I32)
gX_TYPES_V234_DEF(I64)
#endif //defined(gX_TYPES_VECTOR_INT)
#if defined(gX_TYPES_VECTOR_UINT)
gX_TYPES_V234_DEF(U8)
gX_TYPES_V234_DEF(U16)
gX_TYPES_V234_DEF(U32)
gX_TYPES_V234_DEF(U64)
#endif //defined(gX_TYPES_VECTOR_UINT)
gX_TYPES_V234_DEF(F32)
gX_TYPES_V234_DEF(F64)

typedef gX_F32v4 gX_F32Qua;
#define gX_Quaternion gX_F32Qua

#ifdef __cplusplus
}
#endif

#endif //TYPES_VECTORS_H
