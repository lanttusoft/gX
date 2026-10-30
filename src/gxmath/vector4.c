/**********************************************************************************************
*
*   gxmath/vector4.c
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

#include "vector4.h"
#include "scalar.h"

gX_F32v4 gX_F32v4_Make(gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    return (gX_F32v4){x, y, z, w};
}

gX_F32v4 gX_F32v4_MakeValue(gX_F32 val) {
    return (gX_F32v4){val, val, val, val};
}

gX_I0 gX_F32v4_Set(gX_F32v4 *vo, gX_F32v4 vn) {
    vo->x = vn.x;
    vo->y = vn.y;
    vo->z = vn.z;
    vo->w = vn.w;
}

gX_F32v4 gX_F32v4_Inc(gX_F32v4 v) {
    return (gX_F32v4){v.x+1.0f, v.y+1.0f, v.z+1.0f, v.w+1.0f};
}

gX_F32v4 gX_F32v4_Dec(gX_F32v4 v) {
    return (gX_F32v4){v.x-1.0f, v.y-1.0f, v.z-1.0f, v.w-1.0f};
}

gX_F32 gX_F32v4_GetX(gX_F32v4 v) {
    return v.x;
}

gX_F32 gX_F32v4_GetY(gX_F32v4 v) {
    return v.y;
}

gX_F32 gX_F32v4_GetZ(gX_F32v4 v) {
    return v.z;
}

gX_F32 gX_F32v4_GetW(gX_F32v4 v) {
    return v.w;
}

gX_F32v2 gX_F32v4_GetXY(gX_F32v4 v) {
    return (gX_F32v2){v.x, v.y};
}

gX_F32v3 gX_F32v4_GetXYZ(gX_F32v4 v) {
    return (gX_F32v3){v.x, v.y, v.z};
}

GXMATH_VECTOR_UNARY_DEF(F32,4)
GXMATH_VECTOR_FIELD_DEF(F32,4)

GXMATH_VECTOR_LENGTHFUNC_DEF(F32,4)

// Calculate distance between two vectors
gX_F32 gX_F32v4_Distance(gX_F32v4 v1, gX_F32v4 v2) {
    gX_F32 result = 0.0f;
    gX_F32 dx = v2.x - v1.x;
    gX_F32 dy = v2.y - v1.y;
    gX_F32 dz = v2.z - v1.z;
    gX_F32 dw = v2.w - v1.w;
    result = sqrtf(dx*dx + dy*dy + dz*dz + dw*dw);
    return result;
}

// Calculate square distance between two vectors
gX_F32 gX_F32v4_DistanceSqr(gX_F32v4 v1, gX_F32v4 v2) {
    gX_F32 result = 0.0f;
    gX_F32 dx = v2.x - v1.x;
    gX_F32 dy = v2.y - v1.y;
    gX_F32 dz = v2.z - v1.z;
    gX_F32 dw = v2.w - v1.w;
    result = dx*dx + dy*dy + dz*dz + dw*dw;
    return result;
}

// Normalize provided vector
gX_F32v4 gX_F32v4_Normalize(gX_F32v4 v) {
    gX_F32v4 result = v;
    gX_F32 length = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z + v.w*v.w);
    if (!gX_F32_Equals(length,0.0f)) {
        gX_F32 ilength = 1.0f/length;
        result.x *= ilength;
        result.y *= ilength;
        result.z *= ilength;
        result.w *= ilength;
    }
    return result;
}

// Calculate two vectors dot product
gX_F32 gX_F32v4_Dot(gX_F32v4 v1, gX_F32v4 v2) {
    gX_F32 result = (v1.x*v2.x + v1.y*v2.y + v1.z*v2.z + v1.w*v2.w);
    return result;
}

inline gX_F32v4 gX_F32v4_Lerp(gX_F32v4 v1, gX_F32v4 v2, gX_F32 t) {
    gX_F32v4 result = { 0 };
    result.x = v1.x + t*(v2.x - v1.x);
    result.y = v1.y + t*(v2.y - v1.y);
    result.z = v1.z + t*(v2.z - v1.z);
    result.w = v1.w + t*(v2.w - v1.w);
    return result;
}

//ai: double-precision counterpart of gX_F32v4_Lerp.
gX_F64v4 gX_F64v4_Lerp(gX_F64v4 v1, gX_F64v4 v2, gX_F64 t) {
    gX_F64v4 result = { 0 };
    result.x = v1.x + t*(v2.x - v1.x);
    result.y = v1.y + t*(v2.y - v1.y);
    result.z = v1.z + t*(v2.z - v1.z);
    result.w = v1.w + t*(v2.w - v1.w);
    return result;
}

//opencode: gX_F32v4_MulAdd
gX_F32v4 gX_F32v4_MulAdd(gX_F32v4 v1, gX_F32v4 v2, gX_F32v4 v) {
    return (gX_F32v4){ v1.x * v2.x + v.x, v1.y * v2.y + v.y, v1.z * v2.z + v.z, v1.w * v2.w + v.w };
}

//opencode: gX_F32v4_Angle
gX_F32 gX_F32v4_Angle(gX_F32v4 v1, gX_F32v4 v2) {
    gX_F32 dot = v1.x*v2.x + v1.y*v2.y + v1.z*v2.z + v1.w*v2.w;
    gX_F32 len1 = sqrtf(v1.x*v1.x + v1.y*v1.y + v1.z*v1.z + v1.w*v1.w);
    gX_F32 len2 = sqrtf(v2.x*v2.x + v2.y*v2.y + v2.z*v2.z + v2.w*v2.w);
    return acosf(dot / (len1 * len2));
}

//opencode: gX_F32v4_Min
gX_F32v4 gX_F32v4_Min(gX_F32v4 v1, gX_F32v4 v2) {
    return (gX_F32v4){ fminf(v1.x, v2.x), fminf(v1.y, v2.y), fminf(v1.z, v2.z), fminf(v1.w, v2.w) };
}

//opencode: gX_F32v4_Max
gX_F32v4 gX_F32v4_Max(gX_F32v4 v1, gX_F32v4 v2) {
    return (gX_F32v4){ fmaxf(v1.x, v2.x), fmaxf(v1.y, v2.y), fmaxf(v1.z, v2.z), fmaxf(v1.w, v2.w) };
}

//opencode: gX_F32v4_Reflect
gX_F32v4 gX_F32v4_Reflect(gX_F32v4 v, gX_F32v4 n) {
    gX_F32 dot = v.x*n.x + v.y*n.y + v.z*n.z + v.w*n.w;
    return (gX_F32v4){ v.x - 2.0f*dot*n.x, v.y - 2.0f*dot*n.y, v.z - 2.0f*dot*n.z, v.w - 2.0f*dot*n.w };
}

//opencode: gX_F32v4_Rotate
gX_F32v4 gX_F32v4_Rotate(gX_F32v4 v, gX_F32 angle) {
    gX_F32 c = cosf(angle);
    gX_F32 s = sinf(angle);
    return (gX_F32v4){ v.x*c - v.y*s, v.x*s + v.y*c, v.z, v.w };
}

//opencode: gX_F32v4_Clamp
gX_F32v4 gX_F32v4_Clamp(gX_F32v4 v, gX_F32v4 min, gX_F32v4 max) {
    return (gX_F32v4){
        fminf(max.x, fmaxf(min.x, v.x)),
        fminf(max.y, fmaxf(min.y, v.y)),
        fminf(max.z, fmaxf(min.z, v.z)),
        fminf(max.w, fmaxf(min.w, v.w))
    };
}

//opencode: gX_F32v4_ClampValue
gX_F32v4 gX_F32v4_ClampValue(gX_F32v4 v, gX_F32 min, gX_F32 max) {
    gX_F32v4 result = v;
    gX_F32 length = v.x*v.x + v.y*v.y + v.z*v.z + v.w*v.w;
    if (length > 0.0f) {
        length = sqrtf(length);
        if (length < min) {
            gX_F32 scale = min/length;
            result.x *= scale; result.y *= scale;
            result.z *= scale; result.w *= scale;
        } else if (length > max) {
            gX_F32 scale = max/length;
            result.x *= scale; result.y *= scale;
            result.z *= scale; result.w *= scale;
        }
    }
    return result;
}

//opencode: gX_F32v4_Equals
gX_I32 gX_F32v4_Equals(gX_F32v4 v1, gX_F32v4 v2) {
    return ((fabsf(v1.x - v2.x)) <= (gX_F32_EPSILON*fmaxf(1.0f, fmaxf(fabsf(v1.x), fabsf(v2.x))))) &&
           ((fabsf(v1.y - v2.y)) <= (gX_F32_EPSILON*fmaxf(1.0f, fmaxf(fabsf(v1.y), fabsf(v2.y))))) &&
           ((fabsf(v1.z - v2.z)) <= (gX_F32_EPSILON*fmaxf(1.0f, fmaxf(fabsf(v1.z), fabsf(v2.z))))) &&
           ((fabsf(v1.w - v2.w)) <= (gX_F32_EPSILON*fmaxf(1.0f, fmaxf(fabsf(v1.w), fabsf(v2.w)))));
}

//opencode: gX_F32v4_Abs
gX_F32v4 gX_F32v4_Abs(gX_F32v4 v) {
    return (gX_F32v4){ fabsf(v.x), fabsf(v.y), fabsf(v.z), fabsf(v.w) };
}

//opencode: gX_F32v4_Floor
gX_F32v4 gX_F32v4_Floor(gX_F32v4 v) {
    return (gX_F32v4){ floorf(v.x), floorf(v.y), floorf(v.z), floorf(v.w) };
}

//opencode: gX_F32v4_Ceil
gX_F32v4 gX_F32v4_Ceil(gX_F32v4 v) {
    return (gX_F32v4){ ceilf(v.x), ceilf(v.y), ceilf(v.z), ceilf(v.w) };
}

//opencode: gX_I32v4_Mod
gX_I32v4 gX_I32v4_Mod(gX_I32v4 v1, gX_I32v4 v2) {
    return (gX_I32v4){ v1.x % v2.x, v1.y % v2.y, v1.z % v2.z, v1.w % v2.w };
}

//opencode: gX_F32v4_Mod
gX_F32v4 gX_F32v4_Mod(gX_F32v4 v1, gX_F32v4 v2) {
    return (gX_F32v4){ fmodf(v1.x, v2.x), fmodf(v1.y, v2.y), fmodf(v1.z, v2.z), fmodf(v1.w, v2.w) };
}

//opencode: gX_I32v4_And
gX_I32v4 gX_I32v4_And(gX_I32v4 v1, gX_I32v4 v2) {
    return (gX_I32v4){ v1.x & v2.x, v1.y & v2.y, v1.z & v2.z, v1.w & v2.w };
}

//opencode: gX_I32v4_Or
gX_I32v4 gX_I32v4_Or(gX_I32v4 v1, gX_I32v4 v2) {
    return (gX_I32v4){ v1.x | v2.x, v1.y | v2.y, v1.z | v2.z, v1.w | v2.w };
}

//opencode: gX_I32v4_Xor
gX_I32v4 gX_I32v4_Xor(gX_I32v4 v1, gX_I32v4 v2) {
    return (gX_I32v4){ v1.x ^ v2.x, v1.y ^ v2.y, v1.z ^ v2.z, v1.w ^ v2.w };
}

//opencode: gX_I32v4_LeftBitShift
gX_I32v4 gX_I32v4_LeftBitShift(gX_I32v4 v, gX_I32 s) {
    return (gX_I32v4){ v.x << s, v.y << s, v.z << s, v.w << s };
}

//opencode: gX_I32v4_RightBitShift
gX_I32v4 gX_I32v4_RightBitShift(gX_I32v4 v, gX_I32 s) {
    return (gX_I32v4){ v.x >> s, v.y >> s, v.z >> s, v.w >> s };
}
