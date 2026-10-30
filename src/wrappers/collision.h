/**********************************************************************************************
* 
*   [wrappers/collision.h]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025-2026 lanttu, opencode
* 
*   [raylib]
*   LICENSE: zlib/libpng
*
*   raylib is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software:
*
*   Copyright (c) 2013-2023 Ramon Santamaria (@raysan5)
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
//opencode: wrapper for collision functions
#ifndef WRAPPERS_COLLISION_H
#define WRAPPERS_COLLISION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/config.h"
#include "../common/types.h"

gX_I1 gX_Collision_Recs(        gX_Rectangle rec1, gX_Rectangle rec2);
gX_I1 gX_Collision_Circles(     gX_F32v2 center1, gX_F32 radius1, gX_F32v2 center2, gX_F32 radius2);
gX_I1 gX_Collision_CircleRec(   gX_F32v2 center, gX_F32 radius, gX_Rectangle rec);
gX_I1 gX_Collision_PointRec(    gX_F32v2 point, gX_Rectangle rec);
gX_I1 gX_Collision_PointCircle( gX_F32v2 point, gX_F32v2 center, gX_F32 radius);
gX_I1 gX_Collision_PointTriangle(gX_F32v2 point, gX_F32v2 v1, gX_F32v2 v2, gX_F32v2 v3);
gX_I1 gX_Collision_PointPoly(   gX_F32v2 point, gX_F32v2 *points, gX_I32 pointCount);
gX_I1 gX_Collision_Lines(       gX_F32v2 start1, gX_F32v2 end1, gX_F32v2 start2, gX_F32v2 end2, gX_F32v2 *contact);

gX_I1 gX_Collision_Spheres(     gX_F32v3 center1, gX_F32 radius1, gX_F32v3 center2, gX_F32 radius2);
gX_I1 gX_Collision_Boxes(       gX_BoundingBox box1, gX_BoundingBox box2);
gX_I1 gX_Collision_BoxSphere(   gX_BoundingBox box, gX_F32v3 center, gX_F32 radius);

#ifdef __cplusplus
}
#endif

#endif //WRAPPERS_COLLISION_H
