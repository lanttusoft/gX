/**********************************************************************************************
*
*   [gX: src/gxmath/quaternion.c]
*   gxmath/quaternion.c
*   Ported from game3 gamemath_quaternion.c (2026-06-10)
*   //opencode: full implementation ported from game3
*
*   Copyright (c) 2025 lanttu
*
*
*   This software is provided "as-is", without any express or implied warranty.
*   In no event will the authors be held liable for any damages arising from
*   the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose,
*   including commercial applications, and to alter it and redistribute it
*   freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not
*     claim that you wrote the original software. If you use this software
*     in a product, an acknowledgment would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not
*     be misrepresented as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
*
*   gX addition to gxmath; distributed under the gxmath zlib/libpng terms.
*
**********************************************************************************************/

#include "quaternion.h"
#include "scalar.h"
#include "macros.h"
#include <math.h>

gX_F32Qua gX_F32Qua_Add(gX_F32Qua q1, gX_F32Qua q2) {
    gX_F32Qua r = {q1.x + q2.x, q1.y + q2.y, q1.z + q2.z, q1.w + q2.w};
    return r;
}

gX_F32Qua gX_F32Qua_AddValue(gX_F32Qua q, gX_F32 add) {
    gX_F32Qua r = {q.x + add, q.y + add, q.z + add, q.w + add};
    return r;
}

gX_F32Qua gX_F32Qua_Subtract(gX_F32Qua q1, gX_F32Qua q2) {
    gX_F32Qua r = {q1.x - q2.x, q1.y - q2.y, q1.z - q2.z, q1.w - q2.w};
    return r;
}

gX_F32Qua gX_F32Qua_SubtractValue(gX_F32Qua q, gX_F32 sub) {
    gX_F32Qua r = {q.x - sub, q.y - sub, q.z - sub, q.w - sub};
    return r;
}

gX_F32Qua gX_F32Qua_Identity(gX_I0) {
    gX_F32Qua r = {0.0f, 0.0f, 0.0f, 1.0f};
    return r;
}

gX_F32 gX_F32Qua_Length(gX_F32Qua q) {
    return sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
}

gX_F32Qua gX_F32Qua_Normalize(gX_F32Qua q) {
    gX_F32Qua r = {0};
    gX_F32 len = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (len == 0.0f) len = 1.0f;
    gX_F32 ilen = 1.0f / len;
    r.x = q.x * ilen;
    r.y = q.y * ilen;
    r.z = q.z * ilen;
    r.w = q.w * ilen;
    return r;
}

gX_F32Qua gX_F32Qua_Invert(gX_F32Qua q) {
    gX_F32Qua r = q;
    gX_F32 lenSq = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    if (lenSq != 0.0f) {
        gX_F32 invLen = 1.0f / lenSq;
        r.x *= -invLen;
        r.y *= -invLen;
        r.z *= -invLen;
        r.w *= invLen;
    }
    return r;
}

gX_F32Qua gX_F32Qua_Multiply(gX_F32Qua q1, gX_F32Qua q2) {
    gX_F32Qua r = {0};
    gX_F32 q1x = q1.x, q1y = q1.y, q1z = q1.z, q1w = q1.w;
    gX_F32 q2x = q2.x, q2y = q2.y, q2z = q2.z, q2w = q2.w;
    r.x = q1x*q2w + q1w*q2x + q1y*q2z - q1z*q2y;
    r.y = q1y*q2w + q1w*q2y + q1z*q2x - q1x*q2z;
    r.z = q1z*q2w + q1w*q2z + q1x*q2y - q1y*q2x;
    r.w = q1w*q2w - q1x*q2x - q1y*q2y - q1z*q2z;
    return r;
}

gX_F32Qua gX_F32Qua_Scale(gX_F32Qua q, gX_F32 mul) {
    gX_F32Qua r = {q.x*mul, q.y*mul, q.z*mul, q.w*mul};
    return r;
}

gX_F32Qua gX_F32Qua_Divide(gX_F32Qua q1, gX_F32Qua q2) {
    gX_F32Qua r = {q1.x/q2.x, q1.y/q2.y, q1.z/q2.z, q1.w/q2.w};
    return r;
}

gX_F32Qua gX_F32Qua_Lerp(gX_F32Qua q1, gX_F32Qua q2, gX_F32 amount) {
    gX_F32Qua r = {0};
    r.x = q1.x + amount*(q2.x - q1.x);
    r.y = q1.y + amount*(q2.y - q1.y);
    r.z = q1.z + amount*(q2.z - q1.z);
    r.w = q1.w + amount*(q2.w - q1.w);
    return r;
}

gX_F32Qua gX_F32Qua_Nlerp(gX_F32Qua q1, gX_F32Qua q2, gX_F32 amount) {
    gX_F32Qua r = gX_F32Qua_Lerp(q1, q2, amount);
    return gX_F32Qua_Normalize(r);
}

gX_F32Qua gX_F32Qua_Slerp(gX_F32Qua q1, gX_F32Qua q2, gX_F32 amount) {
    gX_F32Qua r = {0};
    gX_F32 cosHalfTheta = q1.x*q2.x + q1.y*q2.y + q1.z*q2.z + q1.w*q2.w;
    if (cosHalfTheta < 0) {
        q2.x = -q2.x; q2.y = -q2.y; q2.z = -q2.z; q2.w = -q2.w;
        cosHalfTheta = -cosHalfTheta;
    }
    if (fabsf(cosHalfTheta) >= 1.0f) r = q1;
    else if (cosHalfTheta > 0.95f) r = gX_F32Qua_Nlerp(q1, q2, amount);
    else {
        gX_F32 halfTheta = acosf(cosHalfTheta);
        gX_F32 sinHalfTheta = sqrtf(1.0f - cosHalfTheta*cosHalfTheta);
        if (fabsf(sinHalfTheta) < gX_F32_EPSILON) {
            r.x = (q1.x + q2.x) * 0.5f;
            r.y = (q1.y + q2.y) * 0.5f;
            r.z = (q1.z + q2.z) * 0.5f;
            r.w = (q1.w + q2.w) * 0.5f;
        } else {
            gX_F32 ratioA = sinf((1.0f - amount) * halfTheta) / sinHalfTheta;
            gX_F32 ratioB = sinf(amount * halfTheta) / sinHalfTheta;
            r.x = q1.x*ratioA + q2.x*ratioB;
            r.y = q1.y*ratioA + q2.y*ratioB;
            r.z = q1.z*ratioA + q2.z*ratioB;
            r.w = q1.w*ratioA + q2.w*ratioB;
        }
    }
    return r;
}

gX_F32Qua gX_F32Qua_FromVector3ToVector3(gX_F32v3 from, gX_F32v3 to) {
    gX_F32Qua r = {0};
    gX_F32 cos2Theta = from.x*to.x + from.y*to.y + from.z*to.z;
    gX_F32v3 cross = {
        from.y*to.z - from.z*to.y,
        from.z*to.x - from.x*to.z,
        from.x*to.y - from.y*to.x
    };
    r.x = cross.x;
    r.y = cross.y;
    r.z = cross.z;
    r.w = 1.0f + cos2Theta;
    return gX_F32Qua_Normalize(r);
}

gX_F32Qua gX_F32Qua_FromAxisAngle(gX_F32v3 axis, gX_F32 angle) {
    gX_F32Qua r = {0.0f, 0.0f, 0.0f, 1.0f};
    gX_F32 axisLen = sqrtf(axis.x*axis.x + axis.y*axis.y + axis.z*axis.z);
    if (axisLen != 0.0f) {
        angle *= 0.5f;
        gX_F32v3 v = axis;
        gX_F32 len = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
        if (len == 0.0f) len = 1.0f;
        gX_F32 ilen = 1.0f / len;
        axis.x *= ilen; axis.y *= ilen; axis.z *= ilen;
        r.x = axis.x * sinf(angle);
        r.y = axis.y * sinf(angle);
        r.z = axis.z * sinf(angle);
        r.w = cosf(angle);
        r = gX_F32Qua_Normalize(r);
    }
    return r;
}

gX_I0 gX_F32Qua_ToAxisAngle(gX_F32Qua q, gX_F32v3 *outAxis, gX_F32 *outAngle) {
    if (fabsf(q.w) > 1.0f) q = gX_F32Qua_Normalize(q);
    gX_F32v3 resAxis = {0.0f, 0.0f, 0.0f};
    gX_F32 resAngle = 2.0f * acosf(q.w);
    gX_F32 den = sqrtf(1.0f - q.w*q.w);
    if (den > gX_F32_EPSILON) {
        resAxis.x = q.x / den;
        resAxis.y = q.y / den;
        resAxis.z = q.z / den;
    } else {
        resAxis.x = 1.0f;
    }
    *outAxis = resAxis;
    *outAngle = resAngle;
}

gX_F32Qua gX_F32Qua_FromEuler(gX_F32 pitch, gX_F32 yaw, gX_F32 roll) {
    gX_F32Qua r = {0};
    gX_F32 x0 = cosf(pitch * 0.5f);
    gX_F32 x1 = sinf(pitch * 0.5f);
    gX_F32 y0 = cosf(yaw * 0.5f);
    gX_F32 y1 = sinf(yaw * 0.5f);
    gX_F32 z0 = cosf(roll * 0.5f);
    gX_F32 z1 = sinf(roll * 0.5f);
    r.x = x1*y0*z0 - x0*y1*z1;
    r.y = x0*y1*z0 + x1*y0*z1;
    r.z = x0*y0*z1 - x1*y1*z0;
    r.w = x0*y0*z0 + x1*y1*z1;
    return r;
}

gX_F32v3 gX_F32Qua_ToEuler(gX_F32Qua q) {
    gX_F32v3 r = {0};
    gX_F32 x0 = 2.0f*(q.w*q.x + q.y*q.z);
    gX_F32 x1 = 1.0f - 2.0f*(q.x*q.x + q.y*q.y);
    r.x = atan2f(x0, x1);
    gX_F32 y0 = 2.0f*(q.w*q.y - q.z*q.x);
    y0 = (y0 > 1.0f) ? 1.0f : (y0 < -1.0f) ? -1.0f : y0;
    r.y = asinf(y0);
    gX_F32 z0 = 2.0f*(q.w*q.z + q.x*q.y);
    gX_F32 z1 = 1.0f - 2.0f*(q.y*q.y + q.z*q.z);
    r.z = atan2f(z0, z1);
    return r;
}

gX_F32Qua gX_F32Qua_FromMatrix(gX_Matrix mat) {
    gX_F32Qua r = {0};
    gX_F32 fourWSqMinus1 = mat.m0  + mat.m5 + mat.m10;
    gX_F32 fourXSqMinus1 = mat.m0  - mat.m5 - mat.m10;
    gX_F32 fourYSqMinus1 = mat.m5  - mat.m0 - mat.m10;
    gX_F32 fourZSqMinus1 = mat.m10 - mat.m0 - mat.m5;
    gX_I32 biggestIdx = 0;
    gX_F32 fourBiggestSqMinus1 = fourWSqMinus1;
    if (fourXSqMinus1 > fourBiggestSqMinus1) { fourBiggestSqMinus1 = fourXSqMinus1; biggestIdx = 1; }
    if (fourYSqMinus1 > fourBiggestSqMinus1) { fourBiggestSqMinus1 = fourYSqMinus1; biggestIdx = 2; }
    if (fourZSqMinus1 > fourBiggestSqMinus1) { fourBiggestSqMinus1 = fourZSqMinus1; biggestIdx = 3; }
    gX_F32 biggestVal = sqrtf(fourBiggestSqMinus1 + 1.0f) * 0.5f;
    gX_F32 mult = 0.25f / biggestVal;
    switch (biggestIdx) {
        case 0: r.w = biggestVal; r.x = (mat.m6  - mat.m9) *mult; r.y = (mat.m8  - mat.m2) *mult; r.z = (mat.m1  - mat.m4) *mult; break;
        case 1: r.x = biggestVal; r.w = (mat.m6  - mat.m9) *mult; r.y = (mat.m1  + mat.m4) *mult; r.z = (mat.m8  + mat.m2) *mult; break;
        case 2: r.y = biggestVal; r.w = (mat.m8  - mat.m2) *mult; r.x = (mat.m1  + mat.m4) *mult; r.z = (mat.m6  + mat.m9) *mult; break;
        case 3: r.z = biggestVal; r.w = (mat.m1  - mat.m4) *mult; r.x = (mat.m8  + mat.m2) *mult; r.y = (mat.m6  + mat.m9) *mult; break;
        default: break;
    }
    return r;
}

gX_Matrix gX_F32Qua_ToMatrix(gX_F32Qua q) {
    gX_Matrix r = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    gX_F32 a2 = q.x*q.x, b2 = q.y*q.y, c2 = q.z*q.z;
    gX_F32 ac = q.x*q.z, ab = q.x*q.y, bc = q.y*q.z;
    gX_F32 ad = q.w*q.x, bd = q.w*q.y, cd = q.w*q.z;
    r.m0  = 1 - 2*(b2 + c2);
    r.m1  = 2*(ab + cd);
    r.m2  = 2*(ac - bd);
    r.m4  = 2*(ab - cd);
    r.m5  = 1 - 2*(a2 + c2);
    r.m6  = 2*(bc + ad);
    r.m8  = 2*(ac + bd);
    r.m9  = 2*(bc - ad);
    r.m10 = 1 - 2*(a2 + b2);
    return r;
}

gX_F32Qua gX_F32Qua_Transform(gX_F32Qua q, gX_Matrix mat) {
    gX_F32Qua r = {0};
    r.x = mat.m0*q.x + mat.m4*q.y + mat.m8*q.z  + mat.m12*q.w;
    r.y = mat.m1*q.x + mat.m5*q.y + mat.m9*q.z  + mat.m13*q.w;
    r.z = mat.m2*q.x + mat.m6*q.y + mat.m10*q.z + mat.m14*q.w;
    r.w = mat.m3*q.x + mat.m7*q.y + mat.m11*q.z + mat.m15*q.w;
    return r;
}

gX_I32 gX_F32Qua_Equals(gX_F32Qua p, gX_F32Qua q) {
    return (((fabsf(p.x - q.x)) <= (gX_F32_EPSILON * fmaxf(1.0f, fmaxf(fabsf(p.x), fabsf(q.x))))) &&
            ((fabsf(p.y - q.y)) <= (gX_F32_EPSILON * fmaxf(1.0f, fmaxf(fabsf(p.y), fabsf(q.y))))) &&
            ((fabsf(p.z - q.z)) <= (gX_F32_EPSILON * fmaxf(1.0f, fmaxf(fabsf(p.z), fabsf(q.z))))) &&
            ((fabsf(p.w - q.w)) <= (gX_F32_EPSILON * fmaxf(1.0f, fmaxf(fabsf(p.w), fabsf(q.w)))))) ||
           (((fabsf(p.x + q.x)) <= (gX_F32_EPSILON * fmaxf(1.0f, fmaxf(fabsf(p.x), fabsf(q.x))))) &&
            ((fabsf(p.y + q.y)) <= (gX_F32_EPSILON * fmaxf(1.0f, fmaxf(fabsf(p.y), fabsf(q.y))))) &&
            ((fabsf(p.z + q.z)) <= (gX_F32_EPSILON * fmaxf(1.0f, fmaxf(fabsf(p.z), fabsf(q.z))))) &&
            ((fabsf(p.w + q.w)) <= (gX_F32_EPSILON * fmaxf(1.0f, fmaxf(fabsf(p.w), fabsf(q.w))))));
}
