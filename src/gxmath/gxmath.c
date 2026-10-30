/**********************************************************************************************
*
*   gxmath/gxmath.c
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

//opencode: fast trig, rsqrt, and utility functions ported from gamemath to gxmath

#include <math.h>
#include <string.h>

#include "gxmath.h"

#define INV2FAC  5.00000000000000000000e-1f
#define INV3FAC  1.66666666666666666667e-1f
#define INV4FAC  4.16666666666666666667e-2f
#define INV5FAC  8.33333333333333333333e-3f
#define INV6FAC  1.38888888888888888889e-3f
#define INV7FAC  1.98412698412698412698e-4f
#define INV8FAC  2.48015873015873015873e-5f
#define INV9FAC  2.75573192239858906526e-6f

//opencode: gX_F32_FastCos_Taylor3
gX_F32 gX_F32_FastCos_Taylor3(gX_F32 angle) {
    gX_I32 addsign = 1;
    angle = fmodf(angle, 2.0f * gX_PI);
    if (angle >= 1.5f * gX_PI && angle < 2.0f * gX_PI) angle -= 2.0f * gX_PI;
    if (angle >= 0.5f * gX_PI && angle < 1.5f * gX_PI) { angle = gX_PI - angle; addsign = -1; }
    gX_F32 a2 = angle * angle;
    return (gX_F32)addsign * (1.0f - INV2FAC * a2 + INV4FAC * a2 * a2);
}

//opencode: gX_F32_FastCos_Taylor4
gX_F32 gX_F32_FastCos_Taylor4(gX_F32 angle) {
    gX_I32 addsign = 1;
    angle = fmodf(angle, 2.0f * gX_PI);
    if (angle >= 1.5f * gX_PI && angle < 2.0f * gX_PI) angle -= 2.0f * gX_PI;
    if (angle >= 0.5f * gX_PI && angle < 1.5f * gX_PI) { angle = gX_PI - angle; addsign = -1; }
    gX_F32 a2 = angle * angle;
    gX_F32 a4 = a2 * a2;
    return (gX_F32)addsign * (1.0f - INV2FAC * a2 + INV4FAC * a4 - INV6FAC * a2 * a4);
}

//opencode: gX_F32_FastCos_Taylor5
gX_F32 gX_F32_FastCos_Taylor5(gX_F32 angle) {
    gX_I32 addsign = 1;
    angle = fmodf(angle, 2.0f * gX_PI);
    if (angle >= 1.5f * gX_PI && angle < 2.0f * gX_PI) angle -= 2.0f * gX_PI;
    if (angle >= 0.5f * gX_PI && angle < 1.5f * gX_PI) { angle = gX_PI - angle; addsign = -1; }
    gX_F32 a2 = angle * angle;
    gX_F32 a4 = a2 * a2;
    return (gX_F32)addsign * (1.0f - INV2FAC * a2 + INV4FAC * a4 - INV6FAC * a2 * a4 + INV8FAC * a4 * a4);
}

//opencode: gX_F32_FastSin_Taylor3
gX_F32 gX_F32_FastSin_Taylor3(gX_F32 angle) {
    gX_I32 addsign = 1;
    angle = fmodf(angle, 2.0f * gX_PI);
    if (angle >= gX_PI && angle < 2.0f * gX_PI) { angle -= gX_PI; addsign = -1; }
    if (angle >= 0.5f * gX_PI && angle < gX_PI) angle = gX_PI - angle;
    gX_F32 a2 = angle * angle;
    return (gX_F32)addsign * angle * (1.0f - INV3FAC * a2 + INV5FAC * a2 * a2);
}

//opencode: gX_F32_FastSin_Taylor4
gX_F32 gX_F32_FastSin_Taylor4(gX_F32 angle) {
    gX_I32 addsign = 1;
    angle = fmodf(angle, 2.0f * gX_PI);
    if (angle >= gX_PI && angle < 2.0f * gX_PI) { angle -= gX_PI; addsign = -1; }
    if (angle >= 0.5f * gX_PI && angle < gX_PI) angle = gX_PI - angle;
    gX_F32 a2 = angle * angle;
    gX_F32 a4 = a2 * a2;
    return (gX_F32)addsign * angle * (1.0f - INV3FAC * a2 + INV5FAC * a4 - INV7FAC * a2 * a4);
}

//opencode: gX_F32_FastSin_Taylor5
gX_F32 gX_F32_FastSin_Taylor5(gX_F32 angle) {
    gX_I32 addsign = 1;
    angle = fmodf(angle, 2.0f * gX_PI);
    if (angle >= gX_PI && angle < 2.0f * gX_PI) { angle -= gX_PI; addsign = -1; }
    if (angle >= 0.5f * gX_PI && angle < gX_PI) angle = gX_PI - angle;
    gX_F32 a2 = angle * angle;
    gX_F32 a4 = a2 * a2;
    return (gX_F32)addsign * angle * (1.0f - INV3FAC * a2 + INV5FAC * a4 - INV7FAC * a2 * a4 + INV9FAC * a4 * a4);
}

//opencode: gX_F32_FastRSqrt_Taylor
gX_F32 gX_F32_FastRSqrt_Taylor(gX_F32 x) {
    if (x > 4.0f && x < 18.0f) {
        gX_F32 x10 = x - 10.0f;
        gX_F32 x10_2 = x10 * x10;
        return 0.316227766017f
            - 0.0158113883008f * x10
            + 0.00237170824513f * x10_2 * INV2FAC
            - 0.000592927061282f * x10_2 * x10 * INV3FAC
            + 0.0000592927061282f * x10_2 * x10_2 * INV4FAC;
    }
    return 0.0f;
}

//opencode: gX_F32_FastRSqrt_QuakeUB
gX_F32 gX_F32_FastRSqrt_QuakeUB(gX_F32 x) {
    gX_U32 i;
    memcpy(&i, &x, sizeof(i));
    i = 0x5f3759df - (i >> 1);
    gX_F32 y;
    memcpy(&y, &i, sizeof(y));
    y = y * (1.5f - (0.5f * x * y * y));
    return y;
}

//opencode: gX_F32_FastRSqrt_Quake
gX_F32 gX_F32_FastRSqrt_Quake(gX_F32 x) {
    union { gX_F32 f; gX_U32 i; } conv = { .f = x };
    conv.i = 0x5f3759df - (conv.i >> 1);
    conv.f *= 1.5f - (x * 0.5f * conv.f * conv.f);
    return conv.f;
}

//opencode: gX_F32v3_RayDirection
gX_F32v3 gX_F32v3_RayDirection(gX_F32 fov, gX_F32v2 res, gX_F32v2 coord) {
    gX_F32v2 xy;
    xy.x = coord.x - res.x * 0.5f;
    xy.y = coord.y - res.y * 0.5f;
    gX_F32 a = tanf(0.5f * gX_PI - 0.5f * fov);
    gX_F32 z = 0.5f * res.y * a;
    gX_F32v3 result;
    result.x = xy.x;
    result.y = xy.y;
    result.z = -z;
    gX_F32 len = sqrtf(result.x * result.x + result.y * result.y + result.z * result.z);
    if (len > 0.0f) {
        result.x /= len;
        result.y /= len;
        result.z /= len;
    }
    return result;
}
