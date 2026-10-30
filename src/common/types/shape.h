/**********************************************************************************************
*
*   [gX: src/common/types/shape.h]
*
*   Copyright (c) 2025 lanttu
*
*
*   This software is provided "as-is", without any express or implied warranty.
*   In no event will the authors be held liable for any damages arising from
*   the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose,
*   including commercial applications, and to alter it and redistribute it
*   freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not
*     claim that you wrote the original software. If you use this software
*     in a product, an acknowledgment would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not
*     be misrepresented as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
*
*   Portions derived from raylib (zlib/libpng), Copyright (c) 2013-2023 Ramon Santamaria (@raysan5).
*
**********************************************************************************************/

#ifndef TYPES_SHAPE_H
#define TYPES_SHAPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "color.h"
#include "primary.h"
#include "vectors.h"
    
// [raylib] borrowed
// Rectangle, 4 components
typedef struct gX_Rectangle {
    gX_F32 x;
    gX_F32 y;
    gX_F32 width;
    gX_F32 height;
} gX_Rectangle;

typedef struct gX_Rectangle_Parameters {
    gX_Color colors[4];
    gX_I1    gradientVertical;
    gX_I1    gradientHorizontal;
    gX_I1    gradientFourPoint;
    gX_I1    gradientMulti;
    gX_F32v2 origin;
    gX_F32   rotation;
    gX_I1    isRound;
    gX_F32   roundness;
    gX_I1    lines;
    gX_F32   lineThickness;
    gX_F32v2 scale; //opencode: v0.3.8 — scale around the rect center (hover-reactive pulse)
} gX_Rectangle_Parameters;

typedef struct gX_Line {
    gX_F32v2 start;
    gX_F32v2 end;
    gX_F32   thickness;
} gX_Line;

typedef struct gX_Line3D {
    gX_F32v3 start;
    gX_F32v3 end;
    gX_F32   thickness;
} gX_Line3D;

typedef struct gX_Bezier {
    gX_F32v2 start;
    gX_F32v2 end;
    gX_F32v2 control1;
    gX_F32v2 control2;
    gX_F32   thickness;
} gX_Bezier;

typedef struct gX_Circle {
    gX_F32v2 center;
    gX_F32   radius;
} gX_Circle;

typedef struct gX_Circle3D {
    gX_F32v3 center;
    gX_F32   radius;
    gX_F32v3 axis;
    gX_F32   angle;
} gX_Circle3D;

typedef struct gX_Ellipse {
    gX_F32v2 center;
    gX_F32v2 radius;
} gX_Ellipse;

typedef struct gX_Ring {
    gX_F32v2 center;
    gX_F32   inner;
    gX_F32   outer;
    gX_F32   start;
    gX_F32   end;
    gX_I32   segments;
} gX_Ring;

typedef struct gX_Triangle {
    gX_F32v2 v1;
    gX_F32v2 v2;
    gX_F32v2 v3;
    gX_F32   thickness;
} gX_Triangle;

typedef struct gX_Triangle3D {
    gX_F32v3 v1;
    gX_F32v3 v2;
    gX_F32v3 v3;
    gX_F32   thickness;
} gX_Triangle3D;

typedef struct gX_Polygon {
    gX_F32v2 center;
    gX_I32   sides;
    gX_F32   radius;
    gX_F32   rotation;
} gX_Polygon;

typedef struct gX_Cube {
    gX_F32v3 pos;
    gX_F32v3 size;
} gX_Cube;

typedef struct gX_Sphere {
    gX_F32v3 pos;
    gX_F32  radius;
    gX_I32  rings;
    gX_I32  slices;
} gX_Sphere;

typedef struct gX_Cylinder {
    gX_F32v3 posStart;
    gX_F32v3 posEnd;
    gX_F32v2 radius;
    gX_I32   sides;
} gX_Cylinder;

typedef struct gX_Capsule {
    gX_F32v3 posStart;
    gX_F32v3 posEnd;
    gX_F32   radius;
    gX_I32   slices;
    gX_I32   rings;
} gX_Capsule;

typedef struct gX_Plane {
    gX_F32v3 pos;
    gX_F32v2 size;
} gX_Plane;

typedef struct gX_Shape_Parameters {
    gX_I1 lines;
    gX_I1 gradient;
} gX_Shape_Parameters;

#ifdef __cplusplus
}
#endif

#endif //TYPES_SHAPE_H
