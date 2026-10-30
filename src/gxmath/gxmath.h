/**********************************************************************************************
*
*   gxmath/gxmath.h
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

//opencode: Ported fast trig and math utilities from gamemath; umbrella header for all gxmath modules

#ifndef GXMATH_H
#define GXMATH_H

#include "calculus.h"
#include "color.h"
#include "config.h"
#include "constants.h"
#include "convert.h"
#include "easing.h"
#include "fourier.h"
#include "macros.h"
#include "matrix.h"
#include "physics.h"
#include "quaternion.h"
#include "random.h"
#include "scalar.h"
#include "sdf.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"

//opencode: Ported fast trig and math constants from gamemath to gxmath

//opencode: gX_F32_FastCos_Taylor3
//opencode: gX_F32_FastCos_Taylor4
//opencode: gX_F32_FastCos_Taylor5
//opencode: gX_F32_FastSin_Taylor3
//opencode: gX_F32_FastSin_Taylor4
//opencode: gX_F32_FastSin_Taylor5
gX_F32 gX_F32_FastCos_Taylor3(gX_F32 angle);
gX_F32 gX_F32_FastCos_Taylor4(gX_F32 angle);
gX_F32 gX_F32_FastCos_Taylor5(gX_F32 angle);
gX_F32 gX_F32_FastSin_Taylor3(gX_F32 angle);
gX_F32 gX_F32_FastSin_Taylor4(gX_F32 angle);
gX_F32 gX_F32_FastSin_Taylor5(gX_F32 angle);

//opencode: gX_F32_FastRSqrt_Taylor
//opencode: gX_F32_FastRSqrt_Quake
//opencode: gX_F32_FastRSqrt_QuakeUB
gX_F32 gX_F32_FastRSqrt_Taylor(gX_F32 x);
gX_F32 gX_F32_FastRSqrt_Quake(gX_F32 x);
gX_F32 gX_F32_FastRSqrt_QuakeUB(gX_F32 x);

//opencode: gX_F32v3_RayDirection
gX_F32v3 gX_F32v3_RayDirection(gX_F32 fov, gX_F32v2 res, gX_F32v2 coord);

#endif //GXMATH_H
