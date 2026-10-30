/**********************************************************************************************
* 
*   [wrappers/collision.c]
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
#include "wrappers_header.h"
#include "collision.h"

//opencode: gX_Collision_Recs — AABB overlap test
gX_I1 gX_Collision_Recs(gX_Rectangle rec1, gX_Rectangle rec2) {
    Rectangle r1 = gX_Rectangle_ConvertToBackend(rec1);
    Rectangle r2 = gX_Rectangle_ConvertToBackend(rec2);
    return (gX_I1)CheckCollisionRecs(r1, r2);
}

//opencode: gX_Collision_Circles — distance check
gX_I1 gX_Collision_Circles(gX_F32v2 center1, gX_F32 radius1, gX_F32v2 center2, gX_F32 radius2) {
    Vector2 c1 = gX_F32v2_ConvertToBackend(center1);
    Vector2 c2 = gX_F32v2_ConvertToBackend(center2);
    return (gX_I1)CheckCollisionCircles(c1, radius1, c2, radius2);
}

//opencode: gX_Collision_CircleRec — circle vs AABB
gX_I1 gX_Collision_CircleRec(gX_F32v2 center, gX_F32 radius, gX_Rectangle rec) {
    Vector2 c = gX_F32v2_ConvertToBackend(center);
    Rectangle r = gX_Rectangle_ConvertToBackend(rec);
    return (gX_I1)CheckCollisionCircleRec(c, radius, r);
}

//opencode: gX_Collision_PointRec — point in AABB
gX_I1 gX_Collision_PointRec(gX_F32v2 point, gX_Rectangle rec) {
    Vector2 p = gX_F32v2_ConvertToBackend(point);
    Rectangle r = gX_Rectangle_ConvertToBackend(rec);
    return (gX_I1)CheckCollisionPointRec(p, r);
}

//opencode: gX_Collision_PointCircle — point in circle
gX_I1 gX_Collision_PointCircle(gX_F32v2 point, gX_F32v2 center, gX_F32 radius) {
    Vector2 p = gX_F32v2_ConvertToBackend(point);
    Vector2 c = gX_F32v2_ConvertToBackend(center);
    return (gX_I1)CheckCollisionPointCircle(p, c, radius);
}

//opencode: gX_Collision_PointTriangle — barycentric test
gX_I1 gX_Collision_PointTriangle(gX_F32v2 point, gX_F32v2 v1, gX_F32v2 v2, gX_F32v2 v3) {
    Vector2 p = gX_F32v2_ConvertToBackend(point);
    Vector2 a = gX_F32v2_ConvertToBackend(v1);
    Vector2 b = gX_F32v2_ConvertToBackend(v2);
    Vector2 c = gX_F32v2_ConvertToBackend(v3);
    return (gX_I1)CheckCollisionPointTriangle(p, a, b, c);
}

//opencode: gX_Collision_PointPoly — ray casting algorithm
gX_I1 gX_Collision_PointPoly(gX_F32v2 point, gX_F32v2 *points, gX_I32 pointCount) {
    Vector2 p = gX_F32v2_ConvertToBackend(point);
    return (gX_I1)CheckCollisionPointPoly(p, (Vector2 *)points, pointCount);
}

//opencode: gX_Collision_Lines — line segment intersection
gX_I1 gX_Collision_Lines(gX_F32v2 start1, gX_F32v2 end1, gX_F32v2 start2, gX_F32v2 end2, gX_F32v2 *contact) {
    Vector2 s1 = gX_F32v2_ConvertToBackend(start1);
    Vector2 e1 = gX_F32v2_ConvertToBackend(end1);
    Vector2 s2 = gX_F32v2_ConvertToBackend(start2);
    Vector2 e2 = gX_F32v2_ConvertToBackend(end2);
    Vector2 cp;
    gX_I1 hit = (gX_I1)CheckCollisionLines(s1, e1, s2, e2, &cp);
    if (contact) {
        contact->x = cp.x;
        contact->y = cp.y;
    }
    return hit;
}

//opencode: gX_Collision_Spheres — 3D distance check
gX_I1 gX_Collision_Spheres(gX_F32v3 center1, gX_F32 radius1, gX_F32v3 center2, gX_F32 radius2) {
    Vector3 c1 = gX_F32v3_ConvertToBackend(center1);
    Vector3 c2 = gX_F32v3_ConvertToBackend(center2);
    return (gX_I1)CheckCollisionSpheres(c1, radius1, c2, radius2);
}

//opencode: gX_Collision_Boxes — AABB overlap (3D)
gX_I1 gX_Collision_Boxes(gX_BoundingBox box1, gX_BoundingBox box2) {
    BoundingBox b1 = (BoundingBox){gX_F32v3_ConvertToBackend(box1.min), gX_F32v3_ConvertToBackend(box1.max)};
    BoundingBox b2 = (BoundingBox){gX_F32v3_ConvertToBackend(box2.min), gX_F32v3_ConvertToBackend(box2.max)};
    return (gX_I1)CheckCollisionBoxes(b1, b2);
}

//opencode: gX_Collision_BoxSphere — AABB vs sphere
gX_I1 gX_Collision_BoxSphere(gX_BoundingBox box, gX_F32v3 center, gX_F32 radius) {
    BoundingBox b = (BoundingBox){gX_F32v3_ConvertToBackend(box.min), gX_F32v3_ConvertToBackend(box.max)};
    Vector3 c = gX_F32v3_ConvertToBackend(center);
    return (gX_I1)CheckCollisionBoxSphere(b, c, radius);
}
