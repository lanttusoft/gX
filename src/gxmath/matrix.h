/**********************************************************************************************
*
*   gxmath/matrix.h
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

#ifndef GXMATH_MATRIX_H
#define GXMATH_MATRIX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

//from raymath.h from raylib
//and gamemath.h from game3

// Compute matrix determinant
gX_F32 gX_F32m2x2_Determinant(gX_F32m2x2 mat);
gX_F32 gX_F32m3x3_Determinant(gX_F32m3x3 mat);
gX_F32 gX_F32m4x4_Determinant(gX_F32m4x4 mat);
#define gX_Matrix_Determinant gX_F32m4x4_Determinant

// Get the trace of the matrix (sum of the values along the diagonal)
gX_F32 gX_F32m2x2_Trace(gX_F32m2x2 mat);
gX_F32 gX_F32m3x3_Trace(gX_F32m3x3 mat);
gX_F32 gX_F32m4x4_Trace(gX_F32m4x4 mat);
#define gX_Matrix_Trace gX_F32m4x4_Trace

// Transposes provided matrix
gX_Matrix gX_Matrix_Transpose(gX_Matrix mat);

// Invert provided matrix
gX_Matrix gX_Matrix_Invert(gX_Matrix mat);

// Get cofactor matrix
gX_Matrix gX_Matrix_Cofactor(gX_Matrix mat);

// Get adjugate matrix (transpose of the cofactor matrix)
gX_Matrix gX_Matrix_Adjucant(gX_Matrix mat);

// Get identity matrix
gX_Matrix gX_Matrix_Identity(gX_I0);

// Add two matrices
gX_Matrix gX_Matrix_Add(gX_Matrix left, gX_Matrix right);

// Subtract two matrices (left - right)
gX_Matrix gX_Matrix_Subtract(gX_Matrix left, gX_Matrix right);

// Get two matrix multiplication
// NOTE: When multiplying matrices... the order matters!
gX_Matrix gX_Matrix_Multiply(gX_Matrix left, gX_Matrix right);

// Multiply matrix indices
gX_Matrix gX_Matrix_MultiplyIndices(gX_Matrix left, gX_Matrix right);

// Divide matrix indices
gX_Matrix gX_Matrix_DivideIndices(gX_Matrix left, gX_Matrix right);

// Get translation matrix
gX_Matrix gX_Matrix_Translate(gX_F32 x, gX_F32 y, gX_F32 z);

// Create rotation matrix from axis and angle
// NOTE: Angle should be provided in radians
gX_Matrix gX_Matrix_Rotate(gX_F32v3 axis, gX_F32 angle);

// Get x-rotation matrix
// NOTE: Angle must be provided in radians
gX_Matrix gX_Matrix_RotateX(gX_F32 angle);

// Get y-rotation matrix
// NOTE: Angle must be provided in radians
gX_Matrix gX_Matrix_RotateY(gX_F32 angle);

// Get z-rotation matrix
// NOTE: Angle must be provided in radians
gX_Matrix gX_Matrix_RotateZ(gX_F32 angle);

// Get xyz-rotation matrix
// NOTE: Angle must be provided in radians
gX_Matrix gX_Matrix_RotateXYZ(gX_F32v3 angle);

// Get zyx-rotation matrix
// NOTE: Angle must be provided in radians
gX_Matrix gX_Matrix_RotateZYX(gX_F32v3 angle);

// Get scaling matrix
gX_Matrix gX_Matrix_Scale(gX_F32 x, gX_F32 y, gX_F32 z);

// Get frustum matrix
gX_Matrix gX_Matrix_Frustum(gX_F32 left, gX_F32 right, gX_F32 bottom, gX_F32 top, gX_F32 near, gX_F32 far);

// Get perspective projection matrix
// NOTE: Fovy angle must be provided in radians
gX_Matrix gX_Matrix_Perspective(gX_F32 fovY, gX_F32 aspect, gX_F32 nearPlane, gX_F32 farPlane);

// Get orthographic projection matrix
gX_Matrix gX_Matrix_Ortho(gX_F32 left, gX_F32 right, gX_F32 bottom, gX_F32 top, gX_F32 nearPlane, gX_F32 farPlane);

// Get camera look-at matrix (view matrix)
gX_Matrix gX_Matrix_LookAt(gX_F32v3 eye, gX_F32v3 target, gX_F32v3 up);

// Get float array of matrix data
gX_F32a16 gX_Matrix_To_F32a16(gX_Matrix mat);

// Get matrix from four row vectors (x, y, z, translation)
gX_Matrix gX_Matrix_From_F32v4(gX_F32v4 v1, gX_F32v4 v2, gX_F32v4 v3, gX_F32v4 v4);

#ifdef __cplusplus
}
#endif

#endif //GXMATH_MATRIX_H
