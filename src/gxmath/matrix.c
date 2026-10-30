/**********************************************************************************************
*
*   gxmath/matrix.c
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

#include "matrix.h"
#include "scalar.h"

//ai: small-matrix determinant/trace helpers declared in matrix.h.
gX_F32 gX_F32m2x2_Determinant(gX_F32m2x2 mat) {
    return mat.m0*mat.m3 - mat.m1*mat.m2;
}

gX_F32 gX_F32m3x3_Determinant(gX_F32m3x3 mat) {
    return mat.m0*(mat.m4*mat.m8 - mat.m5*mat.m7)
         - mat.m1*(mat.m3*mat.m8 - mat.m5*mat.m6)
         + mat.m2*(mat.m3*mat.m7 - mat.m4*mat.m6);
}

gX_F32 gX_F32m2x2_Trace(gX_F32m2x2 mat) {
    return mat.m0 + mat.m3;
}

gX_F32 gX_F32m3x3_Trace(gX_F32m3x3 mat) {
    return mat.m0 + mat.m4 + mat.m8;
}

// Compute matrix determinant
gX_F32 gX_F32m4x4_Determinant(gX_F32m4x4 mat) {
    gX_F32 result = 0.0f;
    // Cache the matrix values (speed optimization)
    gX_F32 a00 = mat.m0,  a01 = mat.m1,  a02 = mat.m2,  a03 = mat.m3;
    gX_F32 a10 = mat.m4,  a11 = mat.m5,  a12 = mat.m6,  a13 = mat.m7;
    gX_F32 a20 = mat.m8,  a21 = mat.m9,  a22 = mat.m10, a23 = mat.m11;
    gX_F32 a30 = mat.m12, a31 = mat.m13, a32 = mat.m14, a33 = mat.m15;
    result = a30*a21*a12*a03 - a20*a31*a12*a03 - a30*a11*a22*a03 + a10*a31*a22*a03 +
             a20*a11*a32*a03 - a10*a21*a32*a03 - a30*a21*a02*a13 + a20*a31*a02*a13 +
             a30*a01*a22*a13 - a00*a31*a22*a13 - a20*a01*a32*a13 + a00*a21*a32*a13 +
             a30*a11*a02*a23 - a10*a31*a02*a23 - a30*a01*a12*a23 + a00*a31*a12*a23 +
             a10*a01*a32*a23 - a00*a11*a32*a23 - a20*a11*a02*a33 + a10*a21*a02*a33 +
             a20*a01*a12*a33 - a00*a21*a12*a33 - a10*a01*a22*a33 + a00*a11*a22*a33;
    return result;
}

// Get the trace of the matrix (sum of the values along the diagonal)
gX_F32 gX_F32m4x4_Trace(gX_F32m4x4 mat) {
    gX_F32 result = (mat.m0 + mat.m5 + mat.m10 + mat.m15);
    return result;
}

// Transposes provided matrix
gX_Matrix gX_Matrix_Transpose(gX_Matrix mat) {
    gX_Matrix result = { 0 };
    result.m0 = mat.m0;
    result.m1 = mat.m4;
    result.m2 = mat.m8;
    result.m3 = mat.m12;
    result.m4 = mat.m1;
    result.m5 = mat.m5;
    result.m6 = mat.m9;
    result.m7 = mat.m13;
    result.m8 = mat.m2;
    result.m9 = mat.m6;
    result.m10 = mat.m10;
    result.m11 = mat.m14;
    result.m12 = mat.m3;
    result.m13 = mat.m7;
    result.m14 = mat.m11;
    result.m15 = mat.m15;
    return result;
}

//ai: 3x3 minor used by the cofactor and adjugate helpers.
static gX_F32 gX_Matrix_Minor3(const gX_F32 *values, gX_I32 skipRow, gX_I32 skipCol) {
    gX_F32 minor[9];
    gX_I32 count = 0;
    for (gX_I32 row = 0; row < 4; row++) {
        if (row == skipRow) continue;
        for (gX_I32 column = 0; column < 4; column++) {
            if (column == skipCol) continue;
            minor[count++] = values[row * 4 + column];
        }
    }
    return minor[0] * (minor[4] * minor[8] - minor[5] * minor[7])
         - minor[1] * (minor[3] * minor[8] - minor[5] * minor[6])
         + minor[2] * (minor[3] * minor[7] - minor[4] * minor[6]);
}

//ai: cofactor matrix in row-major field order; used by the adjugate and tests.
gX_Matrix gX_Matrix_Cofactor(gX_Matrix mat) {
    gX_F32 values[16] = {
        mat.m0, mat.m1, mat.m2, mat.m3,
        mat.m4, mat.m5, mat.m6, mat.m7,
        mat.m8, mat.m9, mat.m10, mat.m11,
        mat.m12, mat.m13, mat.m14, mat.m15,
    };
    gX_F32 cofactor[16];
    for (gX_I32 row = 0; row < 4; row++) {
        for (gX_I32 column = 0; column < 4; column++) {
            gX_F32 sign = ((row + column) & 1) ? -1.0f : 1.0f;
            cofactor[row * 4 + column] = sign * gX_Matrix_Minor3(values, row, column);
        }
    }
    gX_Matrix result = { 0 };
    result.m0 = cofactor[0]; result.m1 = cofactor[1];
    result.m2 = cofactor[2]; result.m3 = cofactor[3];
    result.m4 = cofactor[4]; result.m5 = cofactor[5];
    result.m6 = cofactor[6]; result.m7 = cofactor[7];
    result.m8 = cofactor[8]; result.m9 = cofactor[9];
    result.m10 = cofactor[10]; result.m11 = cofactor[11];
    result.m12 = cofactor[12]; result.m13 = cofactor[13];
    result.m14 = cofactor[14]; result.m15 = cofactor[15];
    return result;
}

gX_Matrix gX_Matrix_Adjucant(gX_Matrix mat) {
    return gX_Matrix_Transpose(gX_Matrix_Cofactor(mat));
}

// Invert provided matrix
gX_Matrix gX_Matrix_Invert(gX_Matrix mat) {
    gX_Matrix result = { 0 };
    // Cache the matrix values (speed optimization)
    gX_F32 a00 = mat.m0,  a01 = mat.m1,  a02 = mat.m2,  a03 = mat.m3;
    gX_F32 a10 = mat.m4,  a11 = mat.m5,  a12 = mat.m6,  a13 = mat.m7;
    gX_F32 a20 = mat.m8,  a21 = mat.m9,  a22 = mat.m10, a23 = mat.m11;
    gX_F32 a30 = mat.m12, a31 = mat.m13, a32 = mat.m14, a33 = mat.m15;
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
    result.m0  = (+a11*b11 - a12*b10 + a13*b09)*invDet;
    result.m1  = (-a01*b11 + a02*b10 - a03*b09)*invDet;
    result.m2  = (+a31*b05 - a32*b04 + a33*b03)*invDet;
    result.m3  = (-a21*b05 + a22*b04 - a23*b03)*invDet;
    result.m4  = (-a10*b11 + a12*b08 - a13*b07)*invDet;
    result.m5  = (+a00*b11 - a02*b08 + a03*b07)*invDet;
    result.m6  = (-a30*b05 + a32*b02 - a33*b01)*invDet;
    result.m7  = (+a20*b05 - a22*b02 + a23*b01)*invDet;
    result.m8  = (+a10*b10 - a11*b08 + a13*b06)*invDet;
    result.m9  = (-a00*b10 + a01*b08 - a03*b06)*invDet;
    result.m10 = (+a30*b04 - a31*b02 + a33*b00)*invDet;
    result.m11 = (-a20*b04 + a21*b02 - a23*b00)*invDet;
    result.m12 = (-a10*b09 + a11*b07 - a12*b06)*invDet;
    result.m13 = (+a00*b09 - a01*b07 + a02*b06)*invDet;
    result.m14 = (-a30*b03 + a31*b01 - a32*b00)*invDet;
    result.m15 = (+a20*b03 - a21*b01 + a22*b00)*invDet;
    return result;
}

// Get identity matrix
gX_Matrix gX_Matrix_Identity(gX_I0) {
    gX_Matrix result = { 1.0f, 0.0f, 0.0f, 0.0f,
                         0.0f, 1.0f, 0.0f, 0.0f,
                         0.0f, 0.0f, 1.0f, 0.0f,
                         0.0f, 0.0f, 0.0f, 1.0f };
    return result;
}

// Add two matrices
gX_Matrix gX_Matrix_Add(gX_Matrix left, gX_Matrix right) {
    gX_Matrix result = { 0 };
    result.m0  = left.m0  + right.m0;
    result.m1  = left.m1  + right.m1;
    result.m2  = left.m2  + right.m2;
    result.m3  = left.m3  + right.m3;
    result.m4  = left.m4  + right.m4;
    result.m5  = left.m5  + right.m5;
    result.m6  = left.m6  + right.m6;
    result.m7  = left.m7  + right.m7;
    result.m8  = left.m8  + right.m8;
    result.m9  = left.m9  + right.m9;
    result.m10 = left.m10 + right.m10;
    result.m11 = left.m11 + right.m11;
    result.m12 = left.m12 + right.m12;
    result.m13 = left.m13 + right.m13;
    result.m14 = left.m14 + right.m14;
    result.m15 = left.m15 + right.m15;
    return result;
}

// Subtract two matrices (left - right)
gX_Matrix gX_Matrix_Subtract(gX_Matrix left, gX_Matrix right) {
    gX_Matrix result = { 0 };
    result.m0  = left.m0  - right.m0;
    result.m1  = left.m1  - right.m1;
    result.m2  = left.m2  - right.m2;
    result.m3  = left.m3  - right.m3;
    result.m4  = left.m4  - right.m4;
    result.m5  = left.m5  - right.m5;
    result.m6  = left.m6  - right.m6;
    result.m7  = left.m7  - right.m7;
    result.m8  = left.m8  - right.m8;
    result.m9  = left.m9  - right.m9;
    result.m10 = left.m10 - right.m10;
    result.m11 = left.m11 - right.m11;
    result.m12 = left.m12 - right.m12;
    result.m13 = left.m13 - right.m13;
    result.m14 = left.m14 - right.m14;
    result.m15 = left.m15 - right.m15;
    return result;
}

// Get two matrix multiplication
// NOTE: When multiplying matrices... the order matters!
gX_Matrix gX_Matrix_Multiply(gX_Matrix left, gX_Matrix right) {
    gX_Matrix result = { 0 };
    result.m0  = left.m0* right.m0 + left.m1*right.m4  + left.m2*right.m8   + left.m3*right.m12;
    result.m1  = left.m0* right.m1 + left.m1*right.m5  + left.m2*right.m9   + left.m3*right.m13;
    result.m2  = left.m0* right.m2 + left.m1*right.m6  + left.m2*right.m10  + left.m3*right.m14;
    result.m3  = left.m0* right.m3 + left.m1*right.m7  + left.m2*right.m11  + left.m3*right.m15;
    result.m4  = left.m4* right.m0 + left.m5*right.m4  + left.m6*right.m8   + left.m7*right.m12;
    result.m5  = left.m4* right.m1 + left.m5*right.m5  + left.m6*right.m9   + left.m7*right.m13;
    result.m6  = left.m4* right.m2 + left.m5*right.m6  + left.m6*right.m10  + left.m7*right.m14;
    result.m7  = left.m4* right.m3 + left.m5*right.m7  + left.m6*right.m11  + left.m7*right.m15;
    result.m8  = left.m8* right.m0 + left.m9*right.m4  + left.m10*right.m8  + left.m11*right.m12;
    result.m9  = left.m8* right.m1 + left.m9*right.m5  + left.m10*right.m9  + left.m11*right.m13;
    result.m10 = left.m8* right.m2 + left.m9*right.m6  + left.m10*right.m10 + left.m11*right.m14;
    result.m11 = left.m8* right.m3 + left.m9*right.m7  + left.m10*right.m11 + left.m11*right.m15;
    result.m12 = left.m12*right.m0 + left.m13*right.m4 + left.m14*right.m8  + left.m15*right.m12;
    result.m13 = left.m12*right.m1 + left.m13*right.m5 + left.m14*right.m9  + left.m15*right.m13;
    result.m14 = left.m12*right.m2 + left.m13*right.m6 + left.m14*right.m10 + left.m15*right.m14;
    result.m15 = left.m12*right.m3 + left.m13*right.m7 + left.m14*right.m11 + left.m15*right.m15;
    return result;
}

gX_Matrix gX_Matrix_MultiplyIndices(gX_Matrix left, gX_Matrix right) {
    gX_Matrix result = { 0 };
    result.m0  = left.m0  * right.m0;
    result.m1  = left.m1  * right.m1;
    result.m2  = left.m2  * right.m2;
    result.m3  = left.m3  * right.m3;
    result.m4  = left.m4  * right.m4;
    result.m5  = left.m5  * right.m5;
    result.m6  = left.m6  * right.m6;
    result.m7  = left.m7  * right.m7;
    result.m8  = left.m8  * right.m8;
    result.m9  = left.m9  * right.m9;
    result.m10 = left.m10 * right.m10;
    result.m11 = left.m11 * right.m11;
    result.m12 = left.m12 * right.m12;
    result.m13 = left.m13 * right.m13;
    result.m14 = left.m14 * right.m14;
    result.m15 = left.m15 * right.m15;
    return result;
}

gX_Matrix gX_Matrix_DivideIndices(gX_Matrix left, gX_Matrix right) {
    gX_Matrix result = { 0 };
    result.m0  = left.m0  / right.m0;
    result.m1  = left.m1  / right.m1;
    result.m2  = left.m2  / right.m2;
    result.m3  = left.m3  / right.m3;
    result.m4  = left.m4  / right.m4;
    result.m5  = left.m5  / right.m5;
    result.m6  = left.m6  / right.m6;
    result.m7  = left.m7  / right.m7;
    result.m8  = left.m8  / right.m8;
    result.m9  = left.m9  / right.m9;
    result.m10 = left.m10 / right.m10;
    result.m11 = left.m11 / right.m11;
    result.m12 = left.m12 / right.m12;
    result.m13 = left.m13 / right.m13;
    result.m14 = left.m14 / right.m14;
    result.m15 = left.m15 / right.m15;
    return result;
}

// Get translation matrix
gX_Matrix gX_Matrix_Translate(gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_Matrix result = { 1.0f, 0.0f, 0.0f, x,
                         0.0f, 1.0f, 0.0f, y,
                         0.0f, 0.0f, 1.0f, z,
                         0.0f, 0.0f, 0.0f, 1.0f };
    return result;
}

// Create rotation matrix from axis and angle
// NOTE: Angle should be provided in radians
gX_Matrix gX_Matrix_Rotate(gX_F32v3 axis, gX_F32 angle) {
    gX_Matrix result = { 0 };
    gX_F32 x = axis.x, y = axis.y, z = axis.z;
    gX_F32 lengthSquared = x*x + y*y + z*z;
    if (!gX_F32_Equals(lengthSquared,1.0f) && !gX_F32_Equals(lengthSquared,0.0f)) {
        gX_F32 ilength = 1.0f/sqrtf(lengthSquared);
        x *= ilength;
        y *= ilength;
        z *= ilength;
    }
    gX_F32 sinres = sinf(angle);
    gX_F32 cosres = cosf(angle);
    gX_F32 t = 1.0f - cosres;
    result.m0 = x*x*t + cosres;
    result.m1 = y*x*t + z*sinres;
    result.m2 = z*x*t - y*sinres;
    result.m3 = 0.0f;
    result.m4 = x*y*t - z*sinres;
    result.m5 = y*y*t + cosres;
    result.m6 = z*y*t + x*sinres;
    result.m7 = 0.0f;
    result.m8 = x*z*t + y*sinres;
    result.m9 = y*z*t - x*sinres;
    result.m10 = z*z*t + cosres;
    result.m11 = 0.0f;
    result.m12 = 0.0f;
    result.m13 = 0.0f;
    result.m14 = 0.0f;
    result.m15 = 1.0f;
    return result;
}

// Get x-rotation matrix
// NOTE: Angle must be provided in radians
gX_Matrix gX_Matrix_RotateX(gX_F32 angle) {
    gX_Matrix result = { 1.0f, 0.0f, 0.0f, 0.0f,
                         0.0f, 1.0f, 0.0f, 0.0f,
                         0.0f, 0.0f, 1.0f, 0.0f,
                         0.0f, 0.0f, 0.0f, 1.0f }; // gX_Matrix_Identity()
    gX_F32 cosres = cosf(angle);
    gX_F32 sinres = sinf(angle);
    result.m5 = cosres;
    result.m6 = sinres;
    result.m9 = -sinres;
    result.m10 = cosres;
    return result;
}

// Get y-rotation matrix
// NOTE: Angle must be provided in radians
gX_Matrix gX_Matrix_RotateY(gX_F32 angle) {
    gX_Matrix result = { 1.0f, 0.0f, 0.0f, 0.0f,
                         0.0f, 1.0f, 0.0f, 0.0f,
                         0.0f, 0.0f, 1.0f, 0.0f,
                         0.0f, 0.0f, 0.0f, 1.0f }; // gX_Matrix_Identity()
    gX_F32 cosres = cosf(angle);
    gX_F32 sinres = sinf(angle);
    result.m0 = cosres;
    result.m2 = -sinres;
    result.m8 = sinres;
    result.m10 = cosres;
    return result;
}

// Get z-rotation matrix
// NOTE: Angle must be provided in radians
gX_Matrix gX_Matrix_RotateZ(gX_F32 angle) {
    gX_Matrix result = { 1.0f, 0.0f, 0.0f, 0.0f,
                         0.0f, 1.0f, 0.0f, 0.0f,
                         0.0f, 0.0f, 1.0f, 0.0f,
                         0.0f, 0.0f, 0.0f, 1.0f }; // gX_Matrix_Identity()
    gX_F32 cosres = cosf(angle);
    gX_F32 sinres = sinf(angle);
    result.m0 = cosres;
    result.m1 = sinres;
    result.m4 = -sinres;
    result.m5 = cosres;
    return result;
}

// Get xyz-rotation matrix
// NOTE: Angle must be provided in radians
gX_Matrix gX_Matrix_RotateXYZ(gX_F32v3 angle) {
    gX_Matrix result = { 1.0f, 0.0f, 0.0f, 0.0f,
                         0.0f, 1.0f, 0.0f, 0.0f,
                         0.0f, 0.0f, 1.0f, 0.0f,
                         0.0f, 0.0f, 0.0f, 1.0f }; // gX_Matrix_Identity()
    gX_F32 cosz = cosf(angle.z);
    gX_F32 sinz = sinf(angle.z);
    gX_F32 cosy = cosf(angle.y);
    gX_F32 siny = sinf(angle.y);
    gX_F32 cosx = cosf(angle.x);
    gX_F32 sinx = sinf(angle.x);
    result.m0 = cosz*cosy;
    result.m1 = (cosz*siny*sinx) - (sinz*cosx);
    result.m2 = (cosz*siny*cosx) + (sinz*sinx);
    result.m4 = sinz*cosy;
    result.m5 = (sinz*siny*sinx) + (cosz*cosx);
    result.m6 = (sinz*siny*cosx) - (cosz*sinx);
    result.m8 = -siny;
    result.m9 = cosy*sinx;
    result.m10= cosy*cosx;
    return result;
}

// Get zyx-rotation matrix
// NOTE: Angle must be provided in radians
gX_Matrix gX_Matrix_RotateZYX(gX_F32v3 angle) {
    gX_Matrix result = { 0 };
    gX_F32 cz = cosf(angle.z);
    gX_F32 sz = sinf(angle.z);
    gX_F32 cy = cosf(angle.y);
    gX_F32 sy = sinf(angle.y);
    gX_F32 cx = cosf(angle.x);
    gX_F32 sx = sinf(angle.x);
    result.m0 = cz*cy;
    result.m4 = cz*sy*sx - cx*sz;
    result.m8 = sz*sx + cz*cx*sy;
    result.m12 = 0.0f;
    result.m1 = cy*sz;
    result.m5 = cz*cx + sz*sy*sx;
    result.m9 = cx*sz*sy - cz*sx;
    result.m13 = 0.0f;
    result.m2 = -sy;
    result.m6 = cy*sx;
    result.m10 = cy*cx;
    result.m14 = 0.0f;
    result.m3 = 0.0f;
    result.m7 = 0.0f;
    result.m11 = 0.0f;
    result.m15 = 1.0f;
    return result;
}

// Get scaling matrix
gX_Matrix gX_Matrix_Scale(gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_Matrix result = {   x, 0.0f, 0.0f, 0.0f,
                        0.0f,    y, 0.0f, 0.0f,
                        0.0f, 0.0f,    z, 0.0f,
                        0.0f, 0.0f, 0.0f, 1.0f };
    return result;
}

// Get frustum matrix
gX_Matrix gX_Matrix_Frustum(gX_F32 left, gX_F32 right, gX_F32 bottom, gX_F32 top, gX_F32 near, gX_F32 far) {
    gX_Matrix result = { 0 };
    gX_F32 rl = right - left;
    gX_F32 tb = top - bottom;
    gX_F32 fn = far - near;
    result.m0 = (near*2.0f)/rl;
    result.m1 = 0.0f; result.m2 = 0.0f; result.m3 = 0.0f; result.m4 = 0.0f;
    result.m5 = (near*2.0f)/tb;
    result.m6 = 0.0f; result.m7 = 0.0f;
    result.m8 = (right + left)/rl;
    result.m9 = (top + bottom)/tb;
    result.m10 = -(far + near)/fn;
    result.m11 = -1.0f;
    result.m12 = 0.0f; result.m13 = 0.0f;
    result.m14 = -(far*near*2.0f)/fn;
    result.m15 = 0.0f;
    return result;
}

// Get perspective projection matrix
// NOTE: Fovy angle must be provided in radians
gX_Matrix gX_Matrix_Perspective(gX_F32 fovY, gX_F32 aspect, gX_F32 nearPlane, gX_F32 farPlane) {
    gX_Matrix result = { 0 };
    gX_F32 top = nearPlane*tanf(fovY*0.5f);
    gX_F32 bottom = -top;
    gX_F32 right = top*aspect;
    gX_F32 left = -right;
    // gX_Matrix_Frustum(-right, right, -top, top, near, far);
    gX_F32 rl = right - left;
    gX_F32 tb = top - bottom;
    gX_F32 fn = farPlane - nearPlane;
    result.m0 = (nearPlane*2.0f)/rl;
    result.m5 = (nearPlane*2.0f)/tb;
    result.m8 = (right + left)/rl;
    result.m9 = (top + bottom)/tb;
    result.m10 = -(farPlane + nearPlane)/fn;
    result.m11 = -1.0f;
    result.m14 = -(farPlane*nearPlane*2.0f)/fn;
    return result;
}

// Get orthographic projection matrix
gX_Matrix gX_Matrix_Ortho(gX_F32 left, gX_F32 right, gX_F32 bottom, gX_F32 top, gX_F32 nearPlane, gX_F32 farPlane) {
    gX_Matrix result = { 0 };
    gX_F32 rl = right - left;
    gX_F32 tb = top - bottom;
    gX_F32 fn = farPlane - nearPlane;
    result.m0 = 2.0f/rl;
    result.m1 = 0.0f; result.m2 = 0.0f; result.m3 = 0.0f; result.m4 = 0.0f;
    result.m5 = 2.0f/tb;
    result.m6 = 0.0f; result.m7 = 0.0f; result.m8 = 0.0f; result.m9 = 0.0f;
    result.m10 = -2.0f/fn;
    result.m11 = 0.0f;
    result.m12 = -(left + right)/rl;
    result.m13 = -(top + bottom)/tb;
    result.m14 = -(farPlane + nearPlane)/fn;
    result.m15 = 1.0f;
    return result;
}

// Get camera look-at matrix (view matrix)
gX_Matrix gX_Matrix_LookAt(gX_F32v3 eye, gX_F32v3 target, gX_F32v3 up) {
    gX_Matrix result = { 0 };
    gX_F32 length = 0.0f;
    gX_F32 ilength = 0.0f;
    // gX_F32v3_Sub(eye, target)
    gX_F32v3 vz = { eye.x - target.x, eye.y - target.y, eye.z - target.z };
    // gX_F32v3_Normalize(vz)
    gX_F32v3 v = vz;
    length = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    //if (gX_F32_Equals(length,0.0f)) length = 1.0f;
    ilength = 1.0f/length;
    vz.x *= ilength; vz.y *= ilength; vz.z *= ilength;
    // gX_F32v3_Cross(up, vz)
    gX_F32v3 vx = { up.y*vz.z - up.z*vz.y, up.z*vz.x - up.x*vz.z, up.x*vz.y - up.y*vz.x };
    // gX_F32v3_Normalize(x)
    v = vx;
    length = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    //if (gX_F32_Equals(length,0.0f)) length = 1.0f;
    ilength = 1.0f/length;
    vx.x *= ilength; vx.y *= ilength; vx.z *= ilength;
    // gX_F32v3_Cross(vz, vx)
    gX_F32v3 vy = { vz.y*vx.z - vz.z*vx.y, vz.z*vx.x - vz.x*vx.z, vz.x*vx.y - vz.y*vx.x };
    result.m0 = vx.x; result.m1 = vy.x; result.m2 = vz.x; result.m3 = 0.0f;
    result.m4 = vx.y; result.m5 = vy.y; result.m6 = vz.y; result.m7 = 0.0f;
    result.m8 = vx.z; result.m9 = vy.z; result.m10 = vz.z; result.m11 = 0.0f;
    result.m12 = -(vx.x*eye.x + vx.y*eye.y + vx.z*eye.z);   // gX_F32v3_Dot(vx, eye)
    result.m13 = -(vy.x*eye.x + vy.y*eye.y + vy.z*eye.z);   // gX_F32v3_Dot(vy, eye)
    result.m14 = -(vz.x*eye.x + vz.y*eye.y + vz.z*eye.z);   // gX_F32v3_Dot(vz, eye)
    result.m15 = 1.0f;
    return result;
}

// Get float array of matrix data
gX_F32a16 gX_Matrix_To_F32a16(gX_Matrix mat) {
    gX_F32a16 result = { 0 };
    result.v[0] = mat.m0;
    result.v[1] = mat.m1;
    result.v[2] = mat.m2;
    result.v[3] = mat.m3;
    result.v[4] = mat.m4;
    result.v[5] = mat.m5;
    result.v[6] = mat.m6;
    result.v[7] = mat.m7;
    result.v[8] = mat.m8;
    result.v[9] = mat.m9;
    result.v[10] = mat.m10;
    result.v[11] = mat.m11;
    result.v[12] = mat.m12;
    result.v[13] = mat.m13;
    result.v[14] = mat.m14;
    result.v[15] = mat.m15;
    return result;
}

//ai: four row vectors map to the transform convention used by gX_F32v3_Transform.
gX_Matrix gX_Matrix_From_F32v4(gX_F32v4 v1, gX_F32v4 v2, gX_F32v4 v3, gX_F32v4 v4) {
    gX_Matrix result = { 0 };
    result.m0 = v1.x; result.m1 = v1.y; result.m2 = v1.z; result.m3 = v1.w;
    result.m4 = v2.x; result.m5 = v2.y; result.m6 = v2.z; result.m7 = v2.w;
    result.m8 = v3.x; result.m9 = v3.y; result.m10 = v3.z; result.m11 = v3.w;
    result.m12 = v4.x; result.m13 = v4.y; result.m14 = v4.z; result.m15 = v4.w;
    return result;
}
