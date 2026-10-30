/**********************************************************************************************
*
*   gxmath/sdf.h
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

#ifndef GXMATH_SDF_H
#define GXMATH_SDF_H

#include "../common/types.h"

//opencode: Ported SDF primitives from gamemath to gxmath

//opencode: gX_F32v3_SDF_RepeatInfinite
//opencode: gX_F32v3_SDF_RepeatFinite
gX_F32v3 gX_F32v3_SDF_RepeatInfinite(gX_F32v3 p, gX_F32v3 c);
gX_F32v3 gX_F32v3_SDF_RepeatFinite(gX_F32v3 p, gX_F32v3 a, gX_F32v3 d);

//opencode: gX_F32_SDF_Sphere
//opencode: gX_F32_SDF_Plane
//opencode: gX_F32_SDF_Cube
//opencode: gX_F32_SDF_Torus
//opencode: gX_F32_SDF_BoxFrame
//opencode: gX_F32_SDF_Cylinder
//opencode: gX_F32_SDF_Cone
//opencode: gX_F32_SDF_Capsule
//opencode: gX_F32_SDF_Octahedron
//opencode: gX_F32_SDF_RoundBox
//opencode: gX_F32_SDF_HexPrism
//opencode: gX_F32_SDF_Mandelbulb
//opencode: gX_F32_SDF_Menger
gX_F32 gX_F32_SDF_Sphere(gX_F32v3 p, gX_F32 r);
gX_F32 gX_F32_SDF_Plane(gX_F32v3 p, gX_F32v3 n);
gX_F32 gX_F32_SDF_Cube(gX_F32v3 p, gX_F32v3 b);
gX_F32 gX_F32_SDF_Torus(gX_F32v3 p, gX_F32v2 t);
gX_F32 gX_F32_SDF_BoxFrame(gX_F32v3 p, gX_F32v3 b, gX_F32 e);
gX_F32 gX_F32_SDF_Cylinder(gX_F32v3 p, gX_F32v2 h);
gX_F32 gX_F32_SDF_Cone(gX_F32v3 p, gX_F32v3 c);
gX_F32 gX_F32_SDF_Capsule(gX_F32v3 p, gX_F32v3 a, gX_F32v3 b, gX_F32 r);
gX_F32 gX_F32_SDF_Octahedron(gX_F32v3 p, gX_F32 s);
gX_F32 gX_F32_SDF_RoundBox(gX_F32v3 p, gX_F32v3 b, gX_F32 r);
gX_F32 gX_F32_SDF_HexPrism(gX_F32v3 p, gX_F32v2 h);
gX_F32 gX_F32_SDF_Mandelbulb(gX_F32v3 p, gX_F32v4 param);
gX_F32 gX_F32_SDF_Menger(gX_F32v3 p, gX_F32v3 b);

#endif //GXMATH_SDF_H
