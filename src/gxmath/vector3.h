/**********************************************************************************************
*
*   gxmath/vector3.h
*   Copyright (c) 2025 lanttu
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

#ifndef GXMATH_VECTOR3_H
#define GXMATH_VECTOR3_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types/matrix.h"
#include "../common/types/vectors.h"
    
#include "vector.h"
    
gX_F32v3    gX_F32v3_Make(          gX_F32 x,       gX_F32 y,       gX_F32 z);
gX_F32v3    gX_F32v3_MakeValue(     gX_F32 val);
gX_I0       gX_F32v3_Set(           gX_F32v3 *vo,   gX_F32v3 vn);
gX_F32v3    gX_F32v3_Inc(           gX_F32v3 v);
gX_F32v3    gX_F32v3_Dec(           gX_F32v3 v);
gX_F32      gX_F32v3_GetX(          gX_F32v3 v);
gX_F32      gX_F32v3_GetY(          gX_F32v3 v);
gX_F32      gX_F32v3_GetZ(          gX_F32v3 v);
gX_F32v2    gX_F32v3_GetXY(         gX_F32v3 v);
    
GXMATH_VECTOR_UNARY_DEC(F32,3)
GXMATH_VECTOR_FIELD_DEC(F32,3)
#define gX_F32v3_Scale gX_F32v3_MulScalar

// Multiply vector by vector and add another vector
gX_F32v3    gX_F32v3_MulAdd(        gX_F32v3 v1,    gX_F32v3 v2,    gX_F32v3 v);

GXMATH_VECTOR_LENGTHFUNC_DEC(F32,3)

// Calculate two vectors dot product
gX_F32      gX_F32v3_Dot(           gX_F32v3 v1,    gX_F32v3 v2);

// Calculate two vectors cross product
gX_F32v3    gX_F32v3_Cross(         gX_F32v3 v1,    gX_F32v3 v2);


// Calculate one vector perpendicular vector
gX_F32v3    gX_F32v3_Perpendicular( gX_F32v3 v);

// Calculate angle between two vectors
gX_F32      gX_F32v3_Angle(         gX_F32v3 v1,    gX_F32v3 v2);

// Calculate linear interpolation between two vectors
gX_F32v3    gX_F32v3_Lerp(          gX_F32v3 v1,    gX_F32v3 v2,    gX_F32 t);

// Get min value for each pair of components
gX_F32v3    gX_F32v3_Min(           gX_F32v3 v1,    gX_F32v3 v2);

// Get max value for each pair of components
gX_F32v3    gX_F32v3_Max(           gX_F32v3 v1,    gX_F32v3 v2);


//Calculate the projection of the vector v1 on to v2
gX_F32v3    gX_F32v3_Project(       gX_F32v3 v1,    gX_F32v3 v2);

//Calculate the rejection of the vector v1 on to v2
gX_F32v3    gX_F32v3_Reject(        gX_F32v3 v1,    gX_F32v3 v2);

// Projects a Vector3 from screen space into object space
// NOTE: We are avoiding calling other raymath functions despite available
gX_F32v3    gX_F32v3_Unproject(    gX_F32v3 v, gX_Matrix projection, gX_Matrix view);


// Orthonormalize provided vectors
// Makes vectors normalized and orthogonal to each other
// Gram-Schmidt function implementation
gX_I0       gX_F32v3_OrthoNormalize(gX_F32v3 *v1,   gX_F32v3 *v2);

// Transforms a Vector3 by a given Matrix
gX_F32v3    gX_F32v3_Transform(     gX_F32v3 v,     gX_Matrix mat);

// Transform a vector by quaternion rotation
gX_F32v3    gX_F32v3_RotateByQuaternion(gX_F32v3 v, gX_F32Qua q);

// Rotates a vector around an axis
gX_F32v3    gX_F32v3_RotateByAxisAngle(gX_F32v3 v, gX_F32v3 axis, gX_F32 angle);

// Compute barycenter coordinates (u, v, w) for point p with respect to triangle (a, b, c)
// NOTE: Assumes P is on the plane of the triangle
gX_F32v3    gX_F32v3_Barycenter(    gX_F32v3 p, gX_F32v3 a, gX_F32v3 b, gX_F32v3 c);

// Compute the direction of a reflected ray
gX_F32v3    gX_F32v3_Reflect(       gX_F32v3 v,     gX_F32v3 n);

// Compute the direction of a refracted ray
// v: normalized direction of the incoming ray
// n: normalized normal vector of the interface of two optical media
// r: ratio of the refractive index of the medium from where the ray comes
//    to the refractive index of the medium on the other side of the surface
gX_F32v3    gX_F32v3_Refract(       gX_F32v3 v,     gX_F32v3 n, gX_F32 r);

// Clamp the components of the vector between
// min and max values specified by the given vectors
gX_F32v3    gX_F32v3_Clamp(         gX_F32v3 v,    gX_F32v3 min, gX_F32v3 max);

// Clamp the magnitude of the vector between two values
gX_F32v3    gX_F32v3_ClampValue(    gX_F32v3 v,    gX_F32 min,   gX_F32 max);

// Check whether two given vectors are almost equal
gX_I32      gX_F32v3_Equals(       gX_F32v3 v1,    gX_F32v3 v2);

// Absolute value
gX_F32v3    gX_F32v3_Abs(           gX_F32v3 v);

// Round down
gX_F32v3    gX_F32v3_Floor(         gX_F32v3 v);

// Round up
gX_F32v3    gX_F32v3_Ceil(          gX_F32v3 v);

//opencode: integer bitwise and shift functions for I32v3
gX_I32v3    gX_I32v3_Mod(           gX_I32v3 v1,   gX_I32v3 v2);
gX_F32v3    gX_F32v3_Mod(           gX_F32v3 v1,   gX_F32v3 v2);
gX_I32v3    gX_I32v3_And(           gX_I32v3 v1,   gX_I32v3 v2);
gX_I32v3    gX_I32v3_Or(            gX_I32v3 v1,   gX_I32v3 v2);
gX_I32v3    gX_I32v3_Xor(           gX_I32v3 v1,   gX_I32v3 v2);
gX_I32v3    gX_I32v3_LeftBitShift(  gX_I32v3 v,    gX_I32 s);
gX_I32v3    gX_I32v3_RightBitShift( gX_I32v3 v,    gX_I32 s);

#ifdef __cplusplus
}
#endif

#endif //GXMATH_VECTOR3_H
