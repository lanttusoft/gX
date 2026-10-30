/**********************************************************************************************
*
*   gxmath/quaternion.h
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

//lanttu: original gxmath module for quaternion operations
//opencode: added From/ToAxisAngle, From/ToEuler, From/ToMatrix, Transform, Equals

#ifndef GXMATH_QUATERNION_H
#define GXMATH_QUATERNION_H

#include "../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Basic operations ---
gX_F32Qua gX_F32Qua_Add(          gX_F32Qua q1, gX_F32Qua q2);
gX_F32Qua gX_F32Qua_AddValue(     gX_F32Qua q, gX_F32 add);
gX_F32Qua gX_F32Qua_Subtract(     gX_F32Qua q1, gX_F32Qua q2);
gX_F32Qua gX_F32Qua_SubtractValue(gX_F32Qua q, gX_F32 sub);
gX_F32Qua gX_F32Qua_Identity(     gX_I0);
gX_F32    gX_F32Qua_Length(       gX_F32Qua q);
gX_F32Qua gX_F32Qua_Normalize(    gX_F32Qua q);
gX_F32Qua gX_F32Qua_Invert(       gX_F32Qua q);
gX_F32Qua gX_F32Qua_Multiply(     gX_F32Qua q1, gX_F32Qua q2);
gX_F32Qua gX_F32Qua_Scale(        gX_F32Qua q, gX_F32 mul);
gX_F32Qua gX_F32Qua_Divide(       gX_F32Qua q1, gX_F32Qua q2);
gX_I32    gX_F32Qua_Equals(       gX_F32Qua p, gX_F32Qua q);

// --- Interpolation ---
gX_F32Qua gX_F32Qua_Lerp(  gX_F32Qua q1, gX_F32Qua q2, gX_F32 amount);
gX_F32Qua gX_F32Qua_Nlerp( gX_F32Qua q1, gX_F32Qua q2, gX_F32 amount);
gX_F32Qua gX_F32Qua_Slerp( gX_F32Qua q1, gX_F32Qua q2, gX_F32 amount);

// --- Construction from other representations ---
gX_F32Qua gX_F32Qua_FromVector3ToVector3(gX_F32v3 from, gX_F32v3 to);
gX_F32Qua gX_F32Qua_FromAxisAngle(       gX_F32v3 axis, gX_F32 angle);
gX_F32Qua gX_F32Qua_FromEuler(           gX_F32 pitch, gX_F32 yaw, gX_F32 roll);
gX_F32Qua gX_F32Qua_FromMatrix(          gX_Matrix mat);

// --- Decomposition ---
gX_I0    gX_F32Qua_ToAxisAngle(gX_F32Qua q, gX_F32v3 *outAxis, gX_F32 *outAngle);
gX_F32v3 gX_F32Qua_ToEuler(    gX_F32Qua q);
gX_Matrix gX_F32Qua_ToMatrix(  gX_F32Qua q);

// --- Transform ---
gX_F32Qua gX_F32Qua_Transform(gX_F32Qua q, gX_Matrix mat);

#ifdef __cplusplus
}
#endif

#endif //GXMATH_QUATERNION_H
