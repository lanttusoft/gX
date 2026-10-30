/**********************************************************************************************
*
*   gxmath/calculus.c
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

//opencode: ported calculus functions from gamemath to gxmath

#include <math.h>

#include "calculus.h"
#include "scalar.h"

//opencode: gX_F32_Calculus_Differentiate
gX_F32 gX_F32_Calculus_Differentiate(gX_F32 (*func)(gX_F32), gX_F32 x, gX_F32 d) {
    if (!func) return 0.0f;
    if (gX_F32_Equals(d, 0.0f)) return 0.0f;
    return (func(x + d) - func(x)) / d;
}

//opencode: gX_F32v2_Calculus_Nabla
gX_F32v2 gX_F32v2_Calculus_Nabla(gX_F32 (*func)(gX_F32, gX_F32), gX_F32v2 x, gX_F32 d) {
    gX_F32v2 result = { 0.0f, 0.0f };
    gX_F32 ref = func(x.x, x.y);
    result.x = (func(x.x + d, x.y) - ref) / d;
    result.y = (func(x.x, x.y + d) - ref) / d;
    return result;
}

//opencode: gX_F32v3_Calculus_Nabla
gX_F32v3 gX_F32v3_Calculus_Nabla(gX_F32 (*func)(gX_F32, gX_F32, gX_F32), gX_F32v3 x, gX_F32 d) {
    gX_F32v3 result = { 0.0f, 0.0f, 0.0f };
    gX_F32 ref = func(x.x, x.y, x.z);
    result.x = (func(x.x + d, x.y, x.z) - ref) / d;
    result.y = (func(x.x, x.y + d, x.z) - ref) / d;
    result.z = (func(x.x, x.y, x.z + d) - ref) / d;
    return result;
}

//opencode: gX_F32v4_Calculus_Nabla
gX_F32v4 gX_F32v4_Calculus_Nabla(gX_F32 (*func)(gX_F32, gX_F32, gX_F32, gX_F32), gX_F32v4 x, gX_F32 d) {
    gX_F32v4 result = { 0.0f, 0.0f, 0.0f, 0.0f };
    gX_F32 ref = func(x.x, x.y, x.z, x.w);
    result.x = (func(x.x + d, x.y, x.z, x.w) - ref) / d;
    result.y = (func(x.x, x.y + d, x.z, x.w) - ref) / d;
    result.z = (func(x.x, x.y, x.z + d, x.w) - ref) / d;
    result.w = (func(x.x, x.y, x.z, x.w + d) - ref) / d;
    return result;
}

//opencode: gX_F32_Calculus_Diverge
gX_F32 gX_F32_Calculus_Diverge(gX_F32v3 (*vfield)(gX_F32, gX_F32, gX_F32), gX_F32v3 x, gX_F32 d) {
    gX_F32 result = 0.0f;
    gX_F32v3 ref = vfield(x.x, x.y, x.z);
    gX_F32v3 vx = vfield(x.x + d, x.y, x.z);
    gX_F32v3 vy = vfield(x.x, x.y + d, x.z);
    gX_F32v3 vz = vfield(x.x, x.y, x.z + d);
    result += (vx.x - ref.x) / d;
    result += (vy.y - ref.y) / d;
    result += (vz.z - ref.z) / d;
    return result;
}

//opencode: gX_F32v3_Calculus_Curl
gX_F32v3 gX_F32v3_Calculus_Curl(gX_F32v3 (*vfield)(gX_F32, gX_F32, gX_F32), gX_F32v3 x, gX_F32 d) {
    gX_F32v3 result = { 0.0f, 0.0f, 0.0f };
    gX_F32v3 ref = vfield(x.x, x.y, x.z);
    gX_F32v3 vx = vfield(x.x + d, x.y, x.z);
    gX_F32v3 vy = vfield(x.x, x.y + d, x.z);
    gX_F32v3 vz = vfield(x.x, x.y, x.z + d);
    result.x = (vy.z - ref.z) / d - (vz.y - ref.y) / d;
    result.y = (vz.x - ref.x) / d - (vx.z - ref.z) / d;
    result.z = (vx.y - ref.y) / d - (vy.x - ref.x) / d;
    return result;
}

//opencode: gX_F32_Calculus_Simpson — fixed off-by-one (was missing endpoint at b)
gX_F32 gX_F32_Calculus_Simpson(gX_F32 (*func)(gX_F32), gX_U32 steps, gX_F32 a, gX_F32 b) {
    if (!func || steps == 0) return 0.0f;
    if (steps == 1) return (b - a) * 0.5f * (func(a) + func(b));
    gX_F32 h = (b - a) / (gX_F32)steps;
    gX_F32 result = func(a) + func(b);
    for (gX_U32 i = 1; i < steps; i++) {
        gX_F32 x = a + (gX_F32)i * h;
        if (i % 2) result += 4.0f * func(x);
        else       result += 2.0f * func(x);
    }
    result *= h / 3.0f;
    return result;
}

//opencode: gX_F32_Newton
gX_F32 gX_F32_Newton(gX_F32 (*func)(gX_F32), gX_F32 x0, gX_F32 d, gX_U32 steps) {
    if (!func) return x0;
    if (gX_F32_Equals(d, 0.0f)) return x0;
    gX_F32 x = x0;
    for (gX_U32 i = 0; i < steps; i++) {
        gX_F32 f = func(x);
        gX_F32 df = gX_F32_Calculus_Differentiate(func, x, d);
        x -= f / df;
    }
    return x;
}
