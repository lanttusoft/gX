/**********************************************************************************************
*
*   gxmath/vector4.h
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

#ifndef GXMATH_VECTOR4_H
#define GXMATH_VECTOR4_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types/vectors.h"
    
#include "vector.h"
    
gX_F32v4    gX_F32v4_Make(          gX_F32 x,       gX_F32 y,       gX_F32 z,       gX_F32 w);
gX_F32v4    gX_F32v4_MakeValue(     gX_F32 val);
gX_I0       gX_F32v4_Set(           gX_F32v4 *vo,   gX_F32v4 vn);
gX_F32v4    gX_F32v4_Inc(           gX_F32v4 v);
gX_F32v4    gX_F32v4_Dec(           gX_F32v4 v);
gX_F32      gX_F32v4_GetX(          gX_F32v4 v);
gX_F32      gX_F32v4_GetY(          gX_F32v4 v);
gX_F32      gX_F32v4_GetZ(          gX_F32v4 v);
gX_F32      gX_F32v4_GetW(          gX_F32v4 v);
gX_F32v2    gX_F32v4_GetXY(         gX_F32v4 v);
gX_F32v3    gX_F32v4_GetXYZ(        gX_F32v4 v);

GXMATH_VECTOR_UNARY_DEC(F32,4)
GXMATH_VECTOR_FIELD_DEC(F32,4)

gX_F32v4    gX_F32v4_MulAdd(        gX_F32v4 v1,    gX_F32v4 v2,    gX_F32v4 v);

GXMATH_VECTOR_LENGTHFUNC_DEC(F32,4)

gX_F32      gX_F32v4_Dot(           gX_F32v4 v1,    gX_F32v4 v2);


gX_F32      gX_F32v4_Angle(         gX_F32v4 v1,    gX_F32v4 v2);

gX_F32v4    gX_F32v4_Lerp(          gX_F32v4 v1,    gX_F32v4 v2,    gX_F32 t);
gX_F64v4    gX_F64v4_Lerp(          gX_F64v4 v1,    gX_F64v4 v2,    gX_F64 t);

gX_F32v4    gX_F32v4_Min(           gX_F32v4 v1,    gX_F32v4 v2);

gX_F32v4    gX_F32v4_Max(           gX_F32v4 v1,    gX_F32v4 v2);


gX_F32v4    gX_F32v4_Reflect(       gX_F32v4 v,     gX_F32v4 n);

gX_F32v4    gX_F32v4_Rotate(        gX_F32v4 v,     gX_F32   angle);

//opencode: Clamp, ClampValue, Equals, Abs, Floor, Ceil — promoted to all vector types
gX_F32v4    gX_F32v4_Clamp(         gX_F32v4 v,    gX_F32v4 min, gX_F32v4 max);
gX_F32v4    gX_F32v4_ClampValue(    gX_F32v4 v,    gX_F32 min,   gX_F32 max);
gX_I32      gX_F32v4_Equals(        gX_F32v4 v1,   gX_F32v4 v2);
gX_F32v4    gX_F32v4_Abs(           gX_F32v4 v);
gX_F32v4    gX_F32v4_Floor(         gX_F32v4 v);
gX_F32v4    gX_F32v4_Ceil(          gX_F32v4 v);

gX_I32v4    gX_I32v4_Mod(           gX_I32v4 v1,    gX_I32v4 v2);
gX_F32v4    gX_F32v4_Mod(           gX_F32v4 v1,    gX_F32v4 v2);

gX_I32v4    gX_I32v4_And(           gX_I32v4 v1,    gX_I32v4 v2);

gX_I32v4    gX_I32v4_Or(            gX_I32v4 v1,    gX_I32v4 v2);

gX_I32v4    gX_I32v4_Xor(           gX_I32v4 v1,    gX_I32v4 v2);

gX_I32v4    gX_I32v4_LeftBitShift(  gX_I32v4 v,     gX_I32 s);

gX_I32v4    gX_I32v4_RightBitShift( gX_I32v4 v,     gX_I32 s);

#ifdef __cplusplus
}
#endif

#endif //GXMATH_VECTOR3_H
