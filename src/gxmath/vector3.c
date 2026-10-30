/**********************************************************************************************
*
*   gxmath/vector3.c
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

#include "vector3.h"
#include "scalar.h"

gX_F32v3 gX_F32v3_Make(gX_F32 x, gX_F32 y, gX_F32 z) {
    return (gX_F32v3){x, y, z};
}

gX_F32v3 gX_F32v3_MakeValue(gX_F32 val) {
    return (gX_F32v3){val, val, val};
}

gX_I0 gX_F32v3_Set(gX_F32v3 *vo, gX_F32v3 vn) {
    vo->x = vn.x;
    vo->y = vn.y;
    vo->z = vn.z;
}

gX_F32v3 gX_F32v3_Inc(gX_F32v3 v) {
    return (gX_F32v3){v.x+1.0f, v.y+1.0f, v.z+1.0f};
}

gX_F32v3 gX_F32v3_Dec(gX_F32v3 v) {
    return (gX_F32v3){v.x-1.0f, v.y-1.0f, v.z-1.0f};
}

gX_F32 gX_F32v3_GetX(gX_F32v3 v) {
    return v.x;
}

gX_F32 gX_F32v3_GetY(gX_F32v3 v) {
    return v.y;
}

gX_F32 gX_F32v3_GetZ(gX_F32v3 v) {
    return v.z;
}

gX_F32v2 gX_F32v3_GetXY(gX_F32v3 v) {
    return (gX_F32v2){v.x, v.y};
}

GXMATH_VECTOR_UNARY_DEF(F32,3)
GXMATH_VECTOR_FIELD_DEF(F32,3)

// Multiply vector by vector and add another vector
gX_F32v3 gX_F32v3_MulAdd(gX_F32v3 v1, gX_F32v3 v2, gX_F32v3 v) {
    return (gX_F32v3){ v1.x * v2.x + v.x, v1.y * v2.y + v.y, v1.z * v2.z + v.z };
}

GXMATH_VECTOR_LENGTHFUNC_DEF(F32,3)

// Calculate distance between two vectors
gX_F32 gX_F32v3_Distance(gX_F32v3 v1, gX_F32v3 v2) {
    gX_F32 result = 0.0f;
    gX_F32 dx = v2.x - v1.x;
    gX_F32 dy = v2.y - v1.y;
    gX_F32 dz = v2.z - v1.z;
    result = sqrtf(dx*dx + dy*dy + dz*dz);
    return result;
}

// Calculate square distance between two vectors
gX_F32 gX_F32v3_DistanceSqr(gX_F32v3 v1, gX_F32v3 v2) {
    gX_F32 result = 0.0f;
    gX_F32 dx = v2.x - v1.x;
    gX_F32 dy = v2.y - v1.y;
    gX_F32 dz = v2.z - v1.z;
    result = dx*dx + dy*dy + dz*dz;
    return result;
}

// Normalize provided vector
gX_F32v3 gX_F32v3_Normalize(gX_F32v3 v) {
    gX_F32v3 result = v;
    gX_F32 length = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    if (!gX_F32_Equals(length,0.0f)) {
        gX_F32 ilength = 1.0f/length;
        result.x *= ilength;
        result.y *= ilength;
        result.z *= ilength;
    }
    return result;
}

// Calculate two vectors dot product
gX_F32 gX_F32v3_Dot(gX_F32v3 v1, gX_F32v3 v2) {
    gX_F32 result = (v1.x*v2.x + v1.y*v2.y + v1.z*v2.z);
    return result;
}

// Calculate two vectors cross product
gX_F32v3 gX_F32v3_Cross(gX_F32v3 v1, gX_F32v3 v2) {
    gX_F32v3 result = { v1.y*v2.z - v1.z*v2.y, v1.z*v2.x - v1.x*v2.z, v1.x*v2.y - v1.y*v2.x };
    return result;
}

// Calculate one vector perpendicular vector
gX_F32v3 gX_F32v3_Perpendicular(gX_F32v3 v) {
    gX_F32v3 result = { 0 };
    gX_F32 min = (gX_F32)fabsf(v.x);
    gX_F32v3 cardinalAxis = {1.0f, 0.0f, 0.0f};
    if (fabsf(v.y) < min) {
        min = (gX_F32)fabsf(v.y);
        gX_F32v3 tmp = {0.0f, 1.0f, 0.0f};
        cardinalAxis = tmp;
    }
    if (fabsf(v.z) < min) {
        gX_F32v3 tmp = {0.0f, 0.0f, 1.0f};
        cardinalAxis = tmp;
    }
    // Cross product between vectors
    result.x = v.y*cardinalAxis.z - v.z*cardinalAxis.y;
    result.y = v.z*cardinalAxis.x - v.x*cardinalAxis.z;
    result.z = v.x*cardinalAxis.y - v.y*cardinalAxis.x;
    return result;
}

// Calculate angle between two vectors
gX_F32 gX_F32v3_Angle(gX_F32v3 v1, gX_F32v3 v2) {
    gX_F32 result = 0.0;
    gX_F32v3 cross = { v1.y*v2.z - v1.z*v2.y, v1.z*v2.x - v1.x*v2.z, v1.x*v2.y - v1.y*v2.x };
    gX_F32 len = sqrtf(cross.x*cross.x + cross.y*cross.y + cross.z*cross.z);
    gX_F32 dot = (v1.x*v2.x + v1.y*v2.y + v1.z*v2.z);
    result = atan2f(len, dot);
    return result;
}

// Calculate linear interpolation between two vectors
gX_F32v3 gX_F32v3_Lerp(gX_F32v3 v1, gX_F32v3 v2, gX_F32 t) {
    gX_F32v3 result = { 0 };
    result.x = v1.x + t*(v2.x - v1.x);
    result.y = v1.y + t*(v2.y - v1.y);
    result.z = v1.z + t*(v2.z - v1.z);
    return result;
}

// Get min value for each pair of components
gX_F32v3 gX_F32v3_Min(gX_F32v3 v1, gX_F32v3 v2) {
    gX_F32v3 result = { 0 };
    result.x = fminf(v1.x, v2.x);
    result.y = fminf(v1.y, v2.y);
    result.z = fminf(v1.z, v2.z);
    return result;
}

// Get max value for each pair of components
gX_F32v3 gX_F32v3_Max(gX_F32v3 v1, gX_F32v3 v2) {
    gX_F32v3 result = { 0 };
    result.x = fmaxf(v1.x, v2.x);
    result.y = fmaxf(v1.y, v2.y);
    result.z = fmaxf(v1.z, v2.z);
    return result;
}

//Calculate the projection of the vector v1 on to v2
gX_F32v3 gX_F32v3_Project(gX_F32v3 v1, gX_F32v3 v2) {
    gX_F32v3 result = { 0 };
    gX_F32 v1dv2 = (v1.x*v2.x + v1.y*v2.y + v1.z*v2.z);
    gX_F32 v2dv2 = (v2.x*v2.x + v2.y*v2.y + v2.z*v2.z);
    gX_F32 mag = v1dv2/v2dv2;
    result.x = v2.x*mag;
    result.y = v2.y*mag;
    result.z = v2.z*mag;
    return result;
}

//Calculate the rejection of the vector v1 on to v2
gX_F32v3 gX_F32v3_Reject(gX_F32v3 v1, gX_F32v3 v2) {
    gX_F32v3 result = { 0 };
    gX_F32 v1dv2 = (v1.x*v2.x + v1.y*v2.y + v1.z*v2.z);
    gX_F32 v2dv2 = (v2.x*v2.x + v2.y*v2.y + v2.z*v2.z);
    gX_F32 mag = v1dv2/v2dv2;
    result.x = v1.x - (v2.x*mag);
    result.y = v1.y - (v2.y*mag);
    result.z = v1.z - (v2.z*mag);
    return result;
}

// Projects a Vector3 from screen space into object space
// NOTE: We are avoiding calling other raymath functions despite available
gX_F32v3 gX_F32v3_Unproject(gX_F32v3 v, gX_Matrix projection, gX_Matrix view) {
    gX_F32v3 result = { 0 };
    // Calculate unprojected matrix (multiply view matrix by projection matrix) and invert it
    gX_Matrix matViewProj = {      // gX_Matrix_Multiply(view, projection);
        view.m0*projection.m0 + view.m1*projection.m4 + view.m2*projection.m8 + view.m3*projection.m12,
        view.m0*projection.m1 + view.m1*projection.m5 + view.m2*projection.m9 + view.m3*projection.m13,
        view.m0*projection.m2 + view.m1*projection.m6 + view.m2*projection.m10 + view.m3*projection.m14,
        view.m0*projection.m3 + view.m1*projection.m7 + view.m2*projection.m11 + view.m3*projection.m15,
        view.m4*projection.m0 + view.m5*projection.m4 + view.m6*projection.m8 + view.m7*projection.m12,
        view.m4*projection.m1 + view.m5*projection.m5 + view.m6*projection.m9 + view.m7*projection.m13,
        view.m4*projection.m2 + view.m5*projection.m6 + view.m6*projection.m10 + view.m7*projection.m14,
        view.m4*projection.m3 + view.m5*projection.m7 + view.m6*projection.m11 + view.m7*projection.m15,
        view.m8*projection.m0 + view.m9*projection.m4 + view.m10*projection.m8 + view.m11*projection.m12,
        view.m8*projection.m1 + view.m9*projection.m5 + view.m10*projection.m9 + view.m11*projection.m13,
        view.m8*projection.m2 + view.m9*projection.m6 + view.m10*projection.m10 + view.m11*projection.m14,
        view.m8*projection.m3 + view.m9*projection.m7 + view.m10*projection.m11 + view.m11*projection.m15,
        view.m12*projection.m0 + view.m13*projection.m4 + view.m14*projection.m8 + view.m15*projection.m12,
        view.m12*projection.m1 + view.m13*projection.m5 + view.m14*projection.m9 + view.m15*projection.m13,
        view.m12*projection.m2 + view.m13*projection.m6 + view.m14*projection.m10 + view.m15*projection.m14,
        view.m12*projection.m3 + view.m13*projection.m7 + view.m14*projection.m11 + view.m15*projection.m15 };

    // Calculate inverted matrix -> MatrixInvert(matViewProj);
    // Cache the matrix values (speed optimization)
    gX_F32 a00 = matViewProj.m0, a01 = matViewProj.m1, a02 = matViewProj.m2, a03 = matViewProj.m3;
    gX_F32 a10 = matViewProj.m4, a11 = matViewProj.m5, a12 = matViewProj.m6, a13 = matViewProj.m7;
    gX_F32 a20 = matViewProj.m8, a21 = matViewProj.m9, a22 = matViewProj.m10, a23 = matViewProj.m11;
    gX_F32 a30 = matViewProj.m12, a31 = matViewProj.m13, a32 = matViewProj.m14, a33 = matViewProj.m15;
    gX_F32 b00 = a00*a11 - a01*a10;
    gX_F32 b01 = a00*a12 - a02*a10;
    gX_F32 b02 = a00*a13 - a03*a10;
    gX_F32 b03 = a01*a12 - a02*a11;
    gX_F32 b04 = a01*a13 - a03*a11;
    gX_F32 b05 = a02*a13 - a03*a12;
    gX_F32 b06 = a20*a31 - a21*a30;
    gX_F32 b07 = a20*a32 - a22*a30;
    gX_F32 b08 = a20*a33 - a23*a30;
    gX_F32 b09 = a21*a32 - a22*a31;
    gX_F32 b10 = a21*a33 - a23*a31;
    gX_F32 b11 = a22*a33 - a23*a32;
    // Calculate the invert determinant (inlined to avoid double-caching)
    gX_F32 invDet = 1.0f/(b00*b11 - b01*b10 + b02*b09 + b03*b08 - b04*b07 + b05*b06);
    gX_Matrix matViewProjInv = {
        (a11*b11 - a12*b10 + a13*b09)*invDet,
        (-a01*b11 + a02*b10 - a03*b09)*invDet,
        (a31*b05 - a32*b04 + a33*b03)*invDet,
        (-a21*b05 + a22*b04 - a23*b03)*invDet,
        (-a10*b11 + a12*b08 - a13*b07)*invDet,
        (a00*b11 - a02*b08 + a03*b07)*invDet,
        (-a30*b05 + a32*b02 - a33*b01)*invDet,
        (a20*b05 - a22*b02 + a23*b01)*invDet,
        (a10*b10 - a11*b08 + a13*b06)*invDet,
        (-a00*b10 + a01*b08 - a03*b06)*invDet,
        (a30*b04 - a31*b02 + a33*b00)*invDet,
        (-a20*b04 + a21*b02 - a23*b00)*invDet,
        (-a10*b09 + a11*b07 - a12*b06)*invDet,
        (a00*b09 - a01*b07 + a02*b06)*invDet,
        (-a30*b03 + a31*b01 - a32*b00)*invDet,
        (a20*b03 - a21*b01 + a22*b00)*invDet };
    // Create quaternion from source point
    gX_F32Qua quat = { v.x, v.y, v.z, 1.0 };
    // Multiply quat point by unprojecte matrix
    gX_F32Qua qtransformed = {     // game_F64_qua_Transform(quat, matViewProjInv)
        matViewProjInv.m0*quat.x + matViewProjInv.m4*quat.y + matViewProjInv.m8*quat.z + matViewProjInv.m12*quat.w,
        matViewProjInv.m1*quat.x + matViewProjInv.m5*quat.y + matViewProjInv.m9*quat.z + matViewProjInv.m13*quat.w,
        matViewProjInv.m2*quat.x + matViewProjInv.m6*quat.y + matViewProjInv.m10*quat.z + matViewProjInv.m14*quat.w,
        matViewProjInv.m3*quat.x + matViewProjInv.m7*quat.y + matViewProjInv.m11*quat.z + matViewProjInv.m15*quat.w };
    // Normalized world points in vectors
    result.x = qtransformed.x/qtransformed.w;
    result.y = qtransformed.y/qtransformed.w;
    result.z = qtransformed.z/qtransformed.w;
    return result;
}

// Orthonormalize provided vectors
// Makes vectors normalized and orthogonal to each other
// Gram-Schmidt function implementation
gX_I0 gX_F32v3_OrthoNormalize(gX_F32v3 *v1,   gX_F32v3 *v2) {
    gX_F32 length = 0.0f;
    gX_F32 ilength = 0.0f;
    // gX_F32v3_Normalize(*v1);
    gX_F32v3 v = *v1;
    length = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    if (gX_F32_Equals(length,0.0f)) length = 1.0f;
    ilength = 1.0f/length;
    v1->x *= ilength;
    v1->y *= ilength;
    v1->z *= ilength;
    // gX_F32v3_CrossProduct(*v1, *v2)
    gX_F32v3 vn1 = { v1->y*v2->z - v1->z*v2->y, v1->z*v2->x - v1->x*v2->z, v1->x*v2->y - v1->y*v2->x };
    // gX_F32v3_Normalize(vn1);
    v = vn1;
    length = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    if (gX_F32_Equals(length,0.0f)) length = 1.0f;
    ilength = 1.0f/length;
    vn1.x *= ilength;
    vn1.y *= ilength;
    vn1.z *= ilength;
    // gX_F32v3_CrossProduct(vn1, *v1)
    gX_F32v3 vn2 = { vn1.y*v1->z - vn1.z*v1->y, vn1.z*v1->x - vn1.x*v1->z, vn1.x*v1->y - vn1.y*v1->x };
    *v2 = vn2;
}

// Transforms a Vector3 by a given Matrix
gX_F32v3 gX_F32v3_Transform(gX_F32v3 v, gX_Matrix mat) {
    gX_F32v3 result = { 0 };
    gX_F32 x = v.x;
    gX_F32 y = v.y;
    gX_F32 z = v.z;
    result.x = mat.m0*x + mat.m4*y + mat.m8*z + mat.m12;
    result.y = mat.m1*x + mat.m5*y + mat.m9*z + mat.m13;
    result.z = mat.m2*x + mat.m6*y + mat.m10*z + mat.m14;
    return result;
}

// Transform a vector by quaternion rotation
gX_F32v3 gX_F32v3_RotateByQuaternion(gX_F32v3 v, gX_F32Qua q) {
    gX_F32v3 result = { 0 };
    result.x = v.x*(q.x*q.x + q.w*q.w - q.y*q.y - q.z*q.z) + v.y*(2*q.x*q.y - 2*q.w*q.z) + v.z*(2*q.x*q.z + 2*q.w*q.y);
    result.y = v.x*(2*q.w*q.z + 2*q.x*q.y) + v.y*(q.w*q.w - q.x*q.x + q.y*q.y - q.z*q.z) + v.z*(-2*q.w*q.x + 2*q.y*q.z);
    result.z = v.x*(-2*q.w*q.y + 2*q.x*q.z) + v.y*(2*q.w*q.x + 2*q.y*q.z)+ v.z*(q.w*q.w - q.x*q.x - q.y*q.y + q.z*q.z);
    return result;
}

// Rotates a vector around an axis
gX_F32v3 gX_F32v3_RotateByAxisAngle(gX_F32v3 v, gX_F32v3 axis, gX_F32 angle) {
    // Using Euler-Rodrigues Formula
    // Ref.: https://en.wikipedia.org/w/index.php?title=Euler%E2%80%93Rodrigues_formula
    gX_F32v3 result = v;
    // gX_F32v3_Normalize(axis);
    gX_F32 length = sqrtf(axis.x*axis.x + axis.y*axis.y + axis.z*axis.z);
    if (gX_F32_Equals(length,0.0f)) length = 1.0f;
    gX_F32 ilength = 1.0f / length;
    axis.x *= ilength;
    axis.y *= ilength;
    axis.z *= ilength;
    angle /= 2.0f;
    gX_F32 a = sinf(angle);
    gX_F32 b = axis.x*a;
    gX_F32 c = axis.y*a;
    gX_F32 d = axis.z*a;
    a = cosf(angle);
    gX_F32v3 w = { b, c, d };
    // gX_F32v3_CrossProduct(w, v)
    gX_F32v3 wv = { w.y*v.z - w.z*v.y, w.z*v.x - w.x*v.z, w.x*v.y - w.y*v.x };
    // gX_F32v3_CrossProduct(w, wv)
    gX_F32v3 wwv = { w.y*wv.z - w.z*wv.y, w.z*wv.x - w.x*wv.z, w.x*wv.y - w.y*wv.x };
    // gX_F32v3_Scale(wv, 2*a)
    a *= 2;
    wv.x *= a;
    wv.y *= a;
    wv.z *= a;
    // gX_F32v3_Scale(wwv, 2)
    wwv.x *= 2;
    wwv.y *= 2;
    wwv.z *= 2;
    result.x += wv.x;
    result.y += wv.y;
    result.z += wv.z;
    result.x += wwv.x;
    result.y += wwv.y;
    result.z += wwv.z;
    return result;
}

// Compute barycenter coordinates (u, v, w) for point p with respect to triangle (a, b, c)
// NOTE: Assumes P is on the plane of the triangle
gX_F32v3 gX_F32v3_Barycenter(gX_F32v3 p, gX_F32v3 a, gX_F32v3 b, gX_F32v3 c) {
    gX_F32v3 result = { 0 };
    gX_F32v3 v0 = { b.x - a.x, b.y - a.y, b.z - a.z };   // gX_F32v3_Subtract(b, a)
    gX_F32v3 v1 = { c.x - a.x, c.y - a.y, c.z - a.z };   // gX_F32v3_Subtract(c, a)
    gX_F32v3 v2 = { p.x - a.x, p.y - a.y, p.z - a.z };   // gX_F32v3_Subtract(p, a)
    gX_F32 d00 = (v0.x*v0.x + v0.y*v0.y + v0.z*v0.z);    // gX_F32v3_DotProduct(v0, v0)
    gX_F32 d01 = (v0.x*v1.x + v0.y*v1.y + v0.z*v1.z);    // gX_F32v3_DotProduct(v0, v1)
    gX_F32 d11 = (v1.x*v1.x + v1.y*v1.y + v1.z*v1.z);    // gX_F32v3_DotProduct(v1, v1)
    gX_F32 d20 = (v2.x*v0.x + v2.y*v0.y + v2.z*v0.z);    // gX_F32v3_DotProduct(v2, v0)
    gX_F32 d21 = (v2.x*v1.x + v2.y*v1.y + v2.z*v1.z);    // gX_F32v3_DotProduct(v2, v1)
    gX_F32 denom = d00*d11 - d01*d01;
    result.y = (d11*d20 - d01*d21)/denom;
    result.z = (d00*d21 - d01*d20)/denom;
    result.x = 1.0f - (result.z + result.y);
    return result;
}

// Compute the direction of a refracted ray
// v: normalized direction of the incoming ray
// n: normalized normal vector of the interface of two optical media
// r: ratio of the refractive index of the medium from where the ray comes
//    to the refractive index of the medium on the other side of the surface
gX_F32v3 gX_F32v3_Refract(gX_F32v3 v, gX_F32v3 n, gX_F32 r) {
    gX_F32v3 result = { 0 };
    gX_F32 dot = v.x*n.x + v.y*n.y + v.z*n.z;
    gX_F32 d = 1.0f - r*r*(1.0f - dot*dot);
    if (d >= 0.0f) {
        d = sqrtf(d);
        v.x = r*v.x - (r*dot + d)*n.x;
        v.y = r*v.y - (r*dot + d)*n.y;
        v.z = r*v.z - (r*dot + d)*n.z;
        result = v;
    }
    return result;
}

// Clamp the components of the vector between
// min and max values specified by the given vectors
gX_F32v3 gX_F32v3_Clamp(gX_F32v3 v, gX_F32v3 min, gX_F32v3 max) {
    gX_F32v3 result = { 0 };
    result.x = fminf(max.x, fmaxf(min.x, v.x));
    result.y = fminf(max.y, fmaxf(min.y, v.y));
    result.z = fminf(max.z, fmaxf(min.z, v.z));
    return result;
}

// Clamp the magnitude of the vector between two values
gX_F32v3 gX_F32v3_ClampValue(gX_F32v3 v, gX_F32 min, gX_F32 max) {
    gX_F32v3 result = v;
    gX_F32 length = (v.x*v.x) + (v.y*v.y) + (v.z*v.z);
    if (length > 0.0f) {
        length = sqrtf(length);
        if (length < min) {
            gX_F32 scale = min/length;
            result.x = v.x*scale;
            result.y = v.y*scale;
            result.z = v.z*scale;
        }
        else if (length > max) {
            gX_F32 scale = max/length;
            result.x = v.x*scale;
            result.y = v.y*scale;
            result.z = v.z*scale;
        }
    }
    return result;
}

// Check whether two given vectors are almost equal
gX_I32 gX_F32v3_Equals(gX_F32v3 v1, gX_F32v3 v2) {
    gX_I32 result = ((fabsf(v1.x - v2.x)) <= (gX_F32_EPSILON*fmaxf(1.0f, fmaxf(fabsf(v1.x), fabsf(v2.x))))) &&
                    ((fabsf(v1.y - v2.y)) <= (gX_F32_EPSILON*fmaxf(1.0f, fmaxf(fabsf(v1.y), fabsf(v2.y))))) &&
                    ((fabsf(v1.z - v2.z)) <= (gX_F32_EPSILON*fmaxf(1.0f, fmaxf(fabsf(v1.z), fabsf(v2.z)))));
    return result;   
}

//opencode: gX_F32v3_Reflect
gX_F32v3 gX_F32v3_Reflect(gX_F32v3 v, gX_F32v3 n) {
    gX_F32 dot = v.x*n.x + v.y*n.y + v.z*n.z;
    return (gX_F32v3){ v.x - 2.0f*dot*n.x, v.y - 2.0f*dot*n.y, v.z - 2.0f*dot*n.z };
}

//opencode: gX_F32v3_Abs
gX_F32v3 gX_F32v3_Abs(gX_F32v3 v) {
    return (gX_F32v3){ fabsf(v.x), fabsf(v.y), fabsf(v.z) };
}

//opencode: gX_F32v3_Floor
gX_F32v3 gX_F32v3_Floor(gX_F32v3 v) {
    return (gX_F32v3){ floorf(v.x), floorf(v.y), floorf(v.z) };
}

//opencode: gX_F32v3_Ceil
gX_F32v3 gX_F32v3_Ceil(gX_F32v3 v) {
    return (gX_F32v3){ ceilf(v.x), ceilf(v.y), ceilf(v.z) };
}

//opencode: gX_I32v3_Mod
gX_I32v3 gX_I32v3_Mod(gX_I32v3 v1, gX_I32v3 v2) {
    return (gX_I32v3){ v1.x % v2.x, v1.y % v2.y, v1.z % v2.z };
}

//opencode: gX_F32v3_Mod
gX_F32v3 gX_F32v3_Mod(gX_F32v3 v1, gX_F32v3 v2) {
    return (gX_F32v3){ fmodf(v1.x, v2.x), fmodf(v1.y, v2.y), fmodf(v1.z, v2.z) };
}

//opencode: gX_I32v3_And
gX_I32v3 gX_I32v3_And(gX_I32v3 v1, gX_I32v3 v2) {
    return (gX_I32v3){ v1.x & v2.x, v1.y & v2.y, v1.z & v2.z };
}

//opencode: gX_I32v3_Or
gX_I32v3 gX_I32v3_Or(gX_I32v3 v1, gX_I32v3 v2) {
    return (gX_I32v3){ v1.x | v2.x, v1.y | v2.y, v1.z | v2.z };
}

//opencode: gX_I32v3_Xor
gX_I32v3 gX_I32v3_Xor(gX_I32v3 v1, gX_I32v3 v2) {
    return (gX_I32v3){ v1.x ^ v2.x, v1.y ^ v2.y, v1.z ^ v2.z };
}

//opencode: gX_I32v3_LeftBitShift
gX_I32v3 gX_I32v3_LeftBitShift(gX_I32v3 v, gX_I32 s) {
    return (gX_I32v3){ v.x << s, v.y << s, v.z << s };
}

//opencode: gX_I32v3_RightBitShift
gX_I32v3 gX_I32v3_RightBitShift(gX_I32v3 v, gX_I32 s) {
    return (gX_I32v3){ v.x >> s, v.y >> s, v.z >> s };
}
