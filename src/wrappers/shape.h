/**********************************************************************************************
* 
*   [wrappers/shape.h]
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
//lanttu: original wrapper for raylib 2D/3D primitive drawing functions
#ifndef WRAPPERS_SHAPE_H
#define WRAPPERS_SHAPE_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "../common/config.h"
#include "../common/types.h"

gX_I0 gX_Rectangle_Draw(            gX_Rectangle rect,  gX_Color color);
gX_I0 gX_Rectangle_Draw_Parameters( gX_Rectangle rect,  gX_Rectangle_Parameters params);
gX_I0 gX_Rectangle_Draw_Border(     gX_Rectangle rect,  gX_Color color); //opencode: 1px outline
gX_I0 gX_Rectangle_Draw_Bar(        gX_Rectangle rect, gX_F32 fillRatio, gX_Color bgColor, gX_Color fillColor, gX_Color borderColor); //opencode: bg+fill+border
gX_I1 gX_Rectangle_Point_CheckCollision(gX_Rectangle rect, gX_F32v2 point);
    
//2d
gX_I0 gX_Point_Draw(                gX_F32v2 coord,     gX_Color color);
gX_I0 gX_Line_Draw(                 gX_Line line,       gX_Color color);
gX_I0 gX_Bezier_Draw(               gX_Bezier bezier,   gX_Color color);
gX_I0 gX_Circle_Draw(               gX_Circle circle,   gX_Color color);
gX_I0 gX_Ellipse_Draw(              gX_Ellipse ellipse, gX_Color color);
gX_I0 gX_Ring_Draw(                 gX_Ring ring,       gX_Color color);
gX_I0 gX_Triangle_Draw(             gX_Triangle tri,    gX_Color color);
gX_I0 gX_Polygon_Draw(              gX_Polygon poly,    gX_Color color);

//3d
gX_I0 gX_Point_Draw3D(              gX_F32v3 coord,         gX_Color color);
gX_I0 gX_Line3D_Draw(               gX_Line3D line,         gX_Color color);
gX_I0 gX_Circle3D_Draw(             gX_Circle3D circle,     gX_Color color);
gX_I0 gX_Circle3D_DrawMatrix(       gX_Circle3D circle,     gX_Color color, gX_Matrix matrix);
gX_I0 gX_Triangle3D_Draw(           gX_Triangle3D tri,      gX_Color color);
gX_I0 gX_Cube_Draw(                 gX_Cube cube,           gX_Color color);
gX_I0 gX_Sphere_Draw(               gX_Sphere sphere,       gX_Color color);
gX_I0 gX_Cylinder_Draw(             gX_Cylinder cylinder,   gX_Color color);
gX_I0 gX_Capsule_Draw(              gX_Capsule capsule,     gX_Color color);
gX_I0 gX_Plane_Draw(                gX_Plane plane,         gX_Color color);
gX_I0 gX_Ray_Draw(                  gX_Ray ray,             gX_Color color);
gX_I0 gX_Grid_Draw( gX_I32 slices, gX_F32 spacing);
    
#ifdef __cplusplus
}
#endif

#endif //WRAPPERS_SHAPE_H
