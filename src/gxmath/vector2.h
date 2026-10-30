/**********************************************************************************************
*
*   gxmath/vector2.h
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

#ifndef GXMATH_VECTOR2_H
#define GXMATH_VECTOR2_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types/vectors.h"
    
#include "vector.h"
    
gX_F32v2    gX_F32v2_Make(          gX_F32 x,       gX_F32 y);
gX_F32v2    gX_F32v2_MakeValue(     gX_F32 val);
gX_I0       gX_F32v2_Set(           gX_F32v2 *vo,   gX_F32v2 vn);
gX_F32v2    gX_F32v2_Inc(           gX_F32v2 v);
gX_F32v2    gX_F32v2_Dec(           gX_F32v2 v);
gX_F32      gX_F32v2_GetX(          gX_F32v2 v);
gX_F32      gX_F32v2_GetY(          gX_F32v2 v);

GXMATH_VECTOR_UNARY_DEC(F32,2)
GXMATH_VECTOR_FIELD_DEC(F32,2)

gX_F32v2    gX_F32v2_MulAdd(        gX_F32v2 v1,    gX_F32v2 v2,    gX_F32v2 v);

GXMATH_VECTOR_LENGTHFUNC_DEC(F32,2)

gX_F32      gX_F32v2_Dot(           gX_F32v2 v1,    gX_F32v2 v2);

gX_F32      gX_F32v2_Angle(         gX_F32v2 v1,    gX_F32v2 v2);

gX_F32v2    gX_F32v2_Lerp(          gX_F32v2 v1,    gX_F32v2 v2,    gX_F32 t);

gX_F32v2    gX_F32v2_Min(           gX_F32v2 v1,    gX_F32v2 v2);

gX_F32v2    gX_F32v2_Max(           gX_F32v2 v1,    gX_F32v2 v2);

gX_F32v2    gX_F32v2_Reflect(       gX_F32v2 v,     gX_F32v2 n);

gX_F32v2    gX_F32v2_Rotate(        gX_F32v2 v,     gX_F32   angle);

//opencode: Clamp, ClampValue, Equals, Abs, Floor, Ceil — promoted to all vector types
gX_F32v2    gX_F32v2_Clamp(         gX_F32v2 v,    gX_F32v2 min, gX_F32v2 max);
gX_F32v2    gX_F32v2_ClampValue(    gX_F32v2 v,    gX_F32 min,   gX_F32 max);
gX_I32      gX_F32v2_Equals(        gX_F32v2 v1,   gX_F32v2 v2);
gX_F32v2    gX_F32v2_Abs(           gX_F32v2 v);
gX_F32v2    gX_F32v2_Floor(         gX_F32v2 v);
gX_F32v2    gX_F32v2_Ceil(          gX_F32v2 v);

//opencode: integer bitwise and shift functions for I32v2
gX_I32v2    gX_I32v2_Mod(           gX_I32v2 v1,   gX_I32v2 v2);
gX_F32v2    gX_F32v2_Mod(           gX_F32v2 v1,   gX_F32v2 v2);
gX_I32v2    gX_I32v2_And(           gX_I32v2 v1,   gX_I32v2 v2);
gX_I32v2    gX_I32v2_Or(            gX_I32v2 v1,   gX_I32v2 v2);
gX_I32v2    gX_I32v2_Xor(           gX_I32v2 v1,   gX_I32v2 v2);
gX_I32v2    gX_I32v2_LeftBitShift(  gX_I32v2 v,    gX_I32 s);
gX_I32v2    gX_I32v2_RightBitShift( gX_I32v2 v,    gX_I32 s);

#ifdef __cplusplus
}
#endif

#endif //GXMATH_VECTOR2_H
