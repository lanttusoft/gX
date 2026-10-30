/**********************************************************************************************
*
*   gxmath/calculus.h
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

#ifndef GXMATH_CALCULUS_H
#define GXMATH_CALCULUS_H

#include "../common/types.h"

//opencode: Ported calculus functions from gamemath to gxmath

//opencode: gX_F32_Calculus_Differentiate
//opencode: gX_F32v2_Calculus_Nabla
//opencode: gX_F32v3_Calculus_Nabla
//opencode: gX_F32v4_Calculus_Nabla
//opencode: gX_F32_Calculus_Diverge
//opencode: gX_F32v3_Calculus_Curl
//opencode: gX_F32_Calculus_Simpson
//opencode: gX_F32_Newton
gX_F32 gX_F32_Calculus_Differentiate(gX_F32 (*func)(gX_F32), gX_F32 x, gX_F32 d);
gX_F32v2 gX_F32v2_Calculus_Nabla(gX_F32 (*func)(gX_F32, gX_F32), gX_F32v2 x, gX_F32 d);
gX_F32v3 gX_F32v3_Calculus_Nabla(gX_F32 (*func)(gX_F32, gX_F32, gX_F32), gX_F32v3 x, gX_F32 d);
gX_F32v4 gX_F32v4_Calculus_Nabla(gX_F32 (*func)(gX_F32, gX_F32, gX_F32, gX_F32), gX_F32v4 x, gX_F32 d);
gX_F32 gX_F32_Calculus_Diverge(gX_F32v3 (*vfield)(gX_F32, gX_F32, gX_F32), gX_F32v3 x, gX_F32 d);
gX_F32v3 gX_F32v3_Calculus_Curl(gX_F32v3 (*vfield)(gX_F32, gX_F32, gX_F32), gX_F32v3 x, gX_F32 d);
gX_F32 gX_F32_Calculus_Simpson(gX_F32 (*func)(gX_F32), gX_U32 steps, gX_F32 a, gX_F32 b);
gX_F32 gX_F32_Newton(gX_F32 (*func)(gX_F32), gX_F32 x0, gX_F32 d, gX_U32 steps);

#endif //GXMATH_CALCULUS_H
