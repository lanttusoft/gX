/**********************************************************************************************
*
*   gxmath/vector2.c
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

//lanttu: original gxmath module based on raymath v1.5

#include <math.h>

#include "vector2.h"
#include "scalar.h"

gX_F32v2 gX_F32v2_Make(gX_F32 x, gX_F32 y) {
    return (gX_F32v2){x, y};
}

gX_F32v2 gX_F32v2_MakeValue(gX_F32 val) {
    return (gX_F32v2){val, val};
}

gX_I0 gX_F32v2_Set(gX_F32v2 *vo, gX_F32v2 vn) {
    vo->x = vn.x;
    vo->y = vn.y;
}

gX_F32v2 gX_F32v2_Inc(gX_F32v2 v) {
    return (gX_F32v2){v.x+1.0f, v.y+1.0f};
}

gX_F32v2 gX_F32v2_Dec(gX_F32v2 v) {
    return (gX_F32v2){v.x-1.0f, v.y-1.0f};
}

gX_F32 gX_F32v2_GetX(gX_F32v2 v) {
    return v.x;
}

gX_F32 gX_F32v2_GetY(gX_F32v2 v) {
    return v.y;
}

GXMATH_VECTOR_UNARY_DEF(F32,2)
GXMATH_VECTOR_FIELD_DEF(F32,2)

GXMATH_VECTOR_LENGTHFUNC_DEF(F32,2)

// Calculate distance between two vectors
gX_F32 gX_F32v2_Distance(gX_F32v2 v1, gX_F32v2 v2) {
    gX_F32 result = 0.0f;
    gX_F32 dx = v2.x - v1.x;
    gX_F32 dy = v2.y - v1.y;
    result = sqrtf(dx*dx + dy*dy);
    return result;
}

// Calculate square distance between two vectors
gX_F32 gX_F32v2_DistanceSqr(gX_F32v2 v1, gX_F32v2 v2) {
    gX_F32 result = 0.0f;
    gX_F32 dx = v2.x - v1.x;
    gX_F32 dy = v2.y - v1.y;
    result = dx*dx + dy*dy;
    return result;
}

// Normalize provided vector
gX_F32v2 gX_F32v2_Normalize(gX_F32v2 v) {
    gX_F32v2 result = v;
    gX_F32 length = sqrtf(v.x*v.x + v.y*v.y);
    if (!gX_F32_Equals(length,0.0f)) {
        gX_F32 ilength = 1.0f/length;
        result.x *= ilength;
        result.y *= ilength;
    }
    return result;
}

// Calculate two vectors dot product
gX_F32 gX_F32v2_Dot(gX_F32v2 v1, gX_F32v2 v2) {
    gX_F32 result = (v1.x*v2.x + v1.y*v2.y);
    return result;
}

//opencode: gX_F32v2_MulAdd
gX_F32v2 gX_F32v2_MulAdd(gX_F32v2 v1, gX_F32v2 v2, gX_F32v2 v) {
    return (gX_F32v2){ v1.x * v2.x + v.x, v1.y * v2.y + v.y };
}

//opencode: gX_F32v2_Angle
gX_F32 gX_F32v2_Angle(gX_F32v2 v1, gX_F32v2 v2) {
    gX_F32 dot = v1.x*v2.x + v1.y*v2.y;
    gX_F32 cross = v1.x*v2.y - v1.y*v2.x;
    return atan2f(cross, dot);
}

//opencode: gX_F32v2_Lerp
gX_F32v2 gX_F32v2_Lerp(gX_F32v2 v1, gX_F32v2 v2, gX_F32 t) {
    return (gX_F32v2){ v1.x + t*(v2.x - v1.x), v1.y + t*(v2.y - v1.y) };
}

//opencode: gX_F32v2_Min
gX_F32v2 gX_F32v2_Min(gX_F32v2 v1, gX_F32v2 v2) {
    return (gX_F32v2){ fminf(v1.x, v2.x), fminf(v1.y, v2.y) };
}

//opencode: gX_F32v2_Max
gX_F32v2 gX_F32v2_Max(gX_F32v2 v1, gX_F32v2 v2) {
    return (gX_F32v2){ fmaxf(v1.x, v2.x), fmaxf(v1.y, v2.y) };
}

//opencode: gX_F32v2_Reflect
gX_F32v2 gX_F32v2_Reflect(gX_F32v2 v, gX_F32v2 n) {
    gX_F32 dot = v.x*n.x + v.y*n.y;
    return (gX_F32v2){ v.x - 2.0f*dot*n.x, v.y - 2.0f*dot*n.y };
}

//opencode: gX_F32v2_Rotate
gX_F32v2 gX_F32v2_Rotate(gX_F32v2 v, gX_F32 angle) {
    gX_F32 c = cosf(angle);
    gX_F32 s = sinf(angle);
    return (gX_F32v2){ v.x*c - v.y*s, v.x*s + v.y*c };
}

//opencode: gX_F32v2_Clamp
gX_F32v2 gX_F32v2_Clamp(gX_F32v2 v, gX_F32v2 min, gX_F32v2 max) {
    return (gX_F32v2){
        fminf(max.x, fmaxf(min.x, v.x)),
        fminf(max.y, fmaxf(min.y, v.y))
    };
}

//opencode: gX_F32v2_ClampValue
gX_F32v2 gX_F32v2_ClampValue(gX_F32v2 v, gX_F32 min, gX_F32 max) {
    gX_F32v2 result = v;
    gX_F32 length = v.x*v.x + v.y*v.y;
    if (length > 0.0f) {
        length = sqrtf(length);
        if (length < min) {
            gX_F32 scale = min/length;
            result.x *= scale;
            result.y *= scale;
        } else if (length > max) {
            gX_F32 scale = max/length;
            result.x *= scale;
            result.y *= scale;
        }
    }
    return result;
}

//opencode: gX_F32v2_Equals
gX_I32 gX_F32v2_Equals(gX_F32v2 v1, gX_F32v2 v2) {
    return ((fabsf(v1.x - v2.x)) <= (gX_F32_EPSILON*fmaxf(1.0f, fmaxf(fabsf(v1.x), fabsf(v2.x))))) &&
           ((fabsf(v1.y - v2.y)) <= (gX_F32_EPSILON*fmaxf(1.0f, fmaxf(fabsf(v1.y), fabsf(v2.y)))));
}

//opencode: gX_F32v2_Abs
gX_F32v2 gX_F32v2_Abs(gX_F32v2 v) {
    return (gX_F32v2){ fabsf(v.x), fabsf(v.y) };
}

//opencode: gX_F32v2_Floor
gX_F32v2 gX_F32v2_Floor(gX_F32v2 v) {
    return (gX_F32v2){ floorf(v.x), floorf(v.y) };
}

//opencode: gX_F32v2_Ceil
gX_F32v2 gX_F32v2_Ceil(gX_F32v2 v) {
    return (gX_F32v2){ ceilf(v.x), ceilf(v.y) };
}

//opencode: gX_I32v2_Mod
gX_I32v2 gX_I32v2_Mod(gX_I32v2 v1, gX_I32v2 v2) {
    return (gX_I32v2){ v1.x % v2.x, v1.y % v2.y };
}

//opencode: gX_F32v2_Mod
gX_F32v2 gX_F32v2_Mod(gX_F32v2 v1, gX_F32v2 v2) {
    return (gX_F32v2){ fmodf(v1.x, v2.x), fmodf(v1.y, v2.y) };
}

//opencode: gX_I32v2_And
gX_I32v2 gX_I32v2_And(gX_I32v2 v1, gX_I32v2 v2) {
    return (gX_I32v2){ v1.x & v2.x, v1.y & v2.y };
}

//opencode: gX_I32v2_Or
gX_I32v2 gX_I32v2_Or(gX_I32v2 v1, gX_I32v2 v2) {
    return (gX_I32v2){ v1.x | v2.x, v1.y | v2.y };
}

//opencode: gX_I32v2_Xor
gX_I32v2 gX_I32v2_Xor(gX_I32v2 v1, gX_I32v2 v2) {
    return (gX_I32v2){ v1.x ^ v2.x, v1.y ^ v2.y };
}

//opencode: gX_I32v2_LeftBitShift
gX_I32v2 gX_I32v2_LeftBitShift(gX_I32v2 v, gX_I32 s) {
    return (gX_I32v2){ v.x << s, v.y << s };
}

//opencode: gX_I32v2_RightBitShift
gX_I32v2 gX_I32v2_RightBitShift(gX_I32v2 v, gX_I32 s) {
    return (gX_I32v2){ v.x >> s, v.y >> s };
}
