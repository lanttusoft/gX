/**********************************************************************************************
* 
*   [wrappers/shape.c]
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
//opencode: D10 — desktop shapes (RAYLIB+GLFW) now draw through gX_Batch (own shader,
//   RHI-only, no raylib/rlgl). The old raylib Draw* / GLFW-native shape paths and the
//   rlgl matrix-sync hacks in drawing.c are gone. PS1/PS2/LIBDRAGON branches unchanged.
#include "wrappers_header.h"
#include "shape.h"
#include "../gxmath/vector3.h"
#include "../gxmath/constants.h"
#include "../gxmath/matrix.h"
#include "../rhi/batch.h"
#include <math.h>

//opencode: PS1 and PS2 shared wrapper state

//2d

extern gX_DrawingInfo drawinfo;

gX_I0 gX_Rectangle_Draw(gX_Rectangle rect, gX_Color color) {
    gX_Rectangle_Parameters params = { 0 };
    params.colors[0] = color;
    raylib_Rectangle_Draw(rect,params);
}

//opencode: N64 multi-color rect — average all 4 corner colors into a single fill color
//   Full vertex-color gradient support would require triangle drawing and is TBD.
gX_I0 gX_Rectangle_Draw_Parameters(gX_Rectangle rect, gX_Rectangle_Parameters params) {
    //opencode: v0.3.8 — scale around the rect center (hover-reactive pulse);
    //   scale {1,1} or {0,0} (unset) is a no-op
    if (params.scale.x != 0.0f && (params.scale.x != 1.0f || params.scale.y != 1.0f)) {
        gX_F32 cx = rect.x + rect.width  * 0.5f;
        gX_F32 cy = rect.y + rect.height * 0.5f;
        gX_F32 nw = rect.width  * params.scale.x;
        gX_F32 nh = rect.height * params.scale.y;
        rect.x = cx - nw * 0.5f;
        rect.y = cy - nh * 0.5f;
        rect.width  = nw;
        rect.height = nh;
    }
    raylib_Rectangle_Draw(rect,params);
}

gX_I1 gX_Rectangle_Point_CheckCollision(gX_Rectangle rect, gX_F32v2 point) {
    //opencode: D14 — pure math, no backend call
    return (point.x >= rect.x && point.x <= rect.x + rect.width &&
            point.y >= rect.y && point.y <= rect.y + rect.height) ? 1 : 0;
}

//opencode: draw a 1px border outline around a rectangle (4 edge rects)
gX_I0 gX_Rectangle_Draw_Border(gX_Rectangle rect, gX_Color color) {
    gX_Rectangle_Draw((gX_Rectangle){rect.x,          rect.y,           rect.width, 1}, color);
    gX_Rectangle_Draw((gX_Rectangle){rect.x,          rect.y+rect.height-1, rect.width, 1}, color);
    gX_Rectangle_Draw((gX_Rectangle){rect.x,          rect.y,           1, rect.height}, color);
    gX_Rectangle_Draw((gX_Rectangle){rect.x+rect.width-1, rect.y,      1, rect.height}, color);
}

//opencode: draw a bar with background, fill (0..1 ratio), and 1px border
gX_I0 gX_Rectangle_Draw_Bar(gX_Rectangle rect, gX_F32 fillRatio, gX_Color bgColor, gX_Color fillColor, gX_Color borderColor) {
    gX_Rectangle_Draw(rect, bgColor);
    if (fillRatio > 0.001f) {
        gX_Rectangle_Draw((gX_Rectangle){rect.x+1, rect.y+1, (rect.width-2)*fillRatio, rect.height-2}, fillColor);
    }
    gX_Rectangle_Draw_Border(rect, borderColor);
}

//opencode: D10 — 1px quad via batch (replaces raylib DrawPixelV)
gX_I0 gX_Point_Draw(gX_F32v2 coord, gX_Color color) {
    gX_Batch_Begin(GX_BATCH_QUADS);
    gX_Batch_Color4ub(color.r, color.g, color.b, color.a);
    gX_Batch_Vertex2f(coord.x, coord.y);
    gX_Batch_Vertex2f(coord.x + 1, coord.y);
    gX_Batch_Vertex2f(coord.x + 1, coord.y + 1);
    gX_Batch_Vertex2f(coord.x, coord.y + 1);
    gX_Batch_End();
}

//opencode: D10 — line via batch (replaces raylib DrawLineV)
gX_I0 gX_Line_Draw(gX_Line line, gX_Color color) {
    gX_Batch_Begin(GX_BATCH_LINES);
    gX_Batch_Color4ub(color.r, color.g, color.b, color.a);
    gX_Batch_Vertex2f(line.start.x, line.start.y);
    gX_Batch_Vertex2f(line.end.x, line.end.y);
    gX_Batch_End();
}

//opencode: D10 — cubic bezier sampled as line strip via batch (replaces DrawSplineSegmentBezierCubic)
gX_I0 gX_Bezier_Draw(gX_Bezier bezier, gX_Color color) {
    gX_Batch_Begin(GX_BATCH_LINES);
    gX_Batch_Color4ub(color.r, color.g, color.b, color.a);
    const gX_I32 segs = 32;
    for (gX_I32 i = 0; i < segs; i++) {
        gX_F32 t0 = (gX_F32)i / (gX_F32)segs;
        gX_F32 t1 = (gX_F32)(i + 1) / (gX_F32)segs;
        gX_F32 inv0 = 1.0f - t0, inv1 = 1.0f - t1;
        gX_F32 x0 = inv0*inv0*inv0*bezier.start.x + 3*inv0*inv0*t0*bezier.control1.x + 3*inv0*t0*t0*bezier.control2.x + t0*t0*t0*bezier.end.x;
        gX_F32 y0 = inv0*inv0*inv0*bezier.start.y + 3*inv0*inv0*t0*bezier.control1.y + 3*inv0*t0*t0*bezier.control2.y + t0*t0*t0*bezier.end.y;
        gX_F32 x1 = inv1*inv1*inv1*bezier.start.x + 3*inv1*inv1*t1*bezier.control1.x + 3*inv1*t1*t1*bezier.control2.x + t1*t1*t1*bezier.end.x;
        gX_F32 y1 = inv1*inv1*inv1*bezier.start.y + 3*inv1*inv1*t1*bezier.control1.y + 3*inv1*t1*t1*bezier.control2.y + t1*t1*t1*bezier.end.y;
        gX_Batch_Vertex2f(x0, y0);
        gX_Batch_Vertex2f(x1, y1);
    }
    gX_Batch_End();
}

//opencode: D10 — filled circle via triangle fan through batch (replaces raylib DrawCircleV)
gX_I0 gX_Circle_Draw(gX_Circle circle, gX_Color color) {
    gX_Batch_Begin(GX_BATCH_TRIANGLES);
    gX_Batch_Color4ub(color.r, color.g, color.b, color.a);
    const gX_I32 segs = 36;
    for (gX_I32 i = 0; i < segs; i++) {
        gX_F32 a0 = (gX_F32)i * 2.0f * gX_PI / (gX_F32)segs;
        gX_F32 a1 = (gX_F32)(i + 1) * 2.0f * gX_PI / (gX_F32)segs;
        gX_Batch_Vertex2f(circle.center.x, circle.center.y);
        gX_Batch_Vertex2f(circle.center.x + cosf(a0) * circle.radius, circle.center.y + sinf(a0) * circle.radius);
        gX_Batch_Vertex2f(circle.center.x + cosf(a1) * circle.radius, circle.center.y + sinf(a1) * circle.radius);
    }
    gX_Batch_End();
}

//opencode: D10 — filled triangle via batch (replaces raylib DrawTriangle)
gX_I0 gX_Triangle_Draw(gX_Triangle tri, gX_Color color) {
    gX_Batch_Begin(GX_BATCH_TRIANGLES);
    gX_Batch_Color4ub(color.r, color.g, color.b, color.a);
    gX_Batch_Vertex2f(tri.v1.x, tri.v1.y);
    gX_Batch_Vertex2f(tri.v2.x, tri.v2.y);
    gX_Batch_Vertex2f(tri.v3.x, tri.v3.y);
    gX_Batch_End();
}

//3d

//opencode: D10 — 3D point via tiny quad through batch (replaces raylib DrawPoint3D)
gX_I0 gX_Point_Draw3D(gX_F32v3 coord, gX_Color color) {
    gX_Batch_Begin(GX_BATCH_QUADS);
    gX_Batch_Color4ub(color.r, color.g, color.b, color.a);
    gX_Batch_Vertex3f(coord.x - 0.05f, coord.y, coord.z);
    gX_Batch_Vertex3f(coord.x + 0.05f, coord.y, coord.z);
    gX_Batch_Vertex3f(coord.x + 0.05f, coord.y, coord.z + 0.05f);
    gX_Batch_Vertex3f(coord.x - 0.05f, coord.y, coord.z + 0.05f);
    gX_Batch_End();
}

//opencode: D10 — 3D line via batch (replaces raylib DrawLine3D)
gX_I0 gX_Line3D_Draw(gX_Line3D line, gX_Color color) {
    gX_Batch_Begin(GX_BATCH_LINES);
    gX_Batch_Color4ub(color.r, color.g, color.b, color.a);
    gX_Batch_Vertex3f(line.start.x, line.start.y, line.start.z);
    gX_Batch_Vertex3f(line.end.x, line.end.y, line.end.z);
    gX_Batch_End();
}


gX_I0 gX_Circle3D_Draw(gX_Circle3D circle, gX_Color color) {
    gX_F32v3 axis = gX_F32v3_Normalize(circle.axis);
    gX_F32v3 zUp = {0.0f, 0.0f, 1.0f};
    gX_F32 d = gX_F32v3_Dot(zUp, axis);
    gX_Matrix T = gX_Matrix_Translate(circle.center.x, circle.center.y, circle.center.z);
    gX_Matrix R = gX_Matrix_Identity();
    if (fabsf(d) > 0.9999f) {
        if (d < 0.0f) R = gX_Matrix_RotateX(gX_PI);
    } else {
        gX_F32v3 rotAxis = gX_F32v3_Normalize(gX_F32v3_Cross(zUp, axis));
        R = gX_Matrix_Rotate(rotAxis, acosf(d));
    }
    gX_Matrix M = gX_Matrix_Multiply(T, R);
    gX_Batch_Begin(GX_BATCH_LINES);
    gX_Batch_Color4ub(color.r, color.g, color.b, color.a);
    for (int i = 0; i < 360; i += 10) {
        gX_F32 k1 = (gX_F32)i * (gX_PI / 180.0f);
        gX_F32 k2 = ((gX_F32)i + 10.0f) * (gX_PI / 180.0f);
        gX_F32 vx1 = sinf(k1) * circle.radius;
        gX_F32 vy1 = cosf(k1) * circle.radius;
        gX_F32 tx1 = M.m0*vx1 + M.m4*vy1 + M.m12;
        gX_F32 ty1 = M.m1*vx1 + M.m5*vy1 + M.m13;
        gX_F32 tz1 = M.m2*vx1 + M.m6*vy1 + M.m14;
        gX_Batch_Vertex3f(tx1, ty1, tz1);
        gX_F32 vx2 = sinf(k2) * circle.radius;
        gX_F32 vy2 = cosf(k2) * circle.radius;
        gX_F32 tx2 = M.m0*vx2 + M.m4*vy2 + M.m12;
        gX_F32 ty2 = M.m1*vx2 + M.m5*vy2 + M.m13;
        gX_F32 tz2 = M.m2*vx2 + M.m6*vy2 + M.m14;
        gX_Batch_Vertex3f(tx2, ty2, tz2);
    }
    gX_Batch_End();
}

gX_I0 gX_Circle3D_DrawMatrix( gX_Circle3D circle, gX_Color color, gX_Matrix matrix) {
    Matrix mat = gX_Matrix_ConvertToBackend(matrix);
    raylib_Circle_Draw_Matrix(circle.center, circle.radius, mat, color);
}

//opencode: D10 — filled sphere via lat-long quads through batch (replaces raylib DrawSphereEx)
gX_I0 gX_Sphere_Draw(gX_Sphere sphere, gX_Color color) {
    gX_I32 rings = sphere.rings < 1 ? 1 : sphere.rings;
    gX_I32 slices = sphere.slices < 3 ? 3 : sphere.slices;
    gX_Batch_Begin(GX_BATCH_QUADS);
    gX_Batch_Color4ub(color.r, color.g, color.b, color.a);
    for (gX_I32 r = 0; r < rings; r++) {
        gX_F32 phi0 = (gX_F32)r * gX_PI / (gX_F32)rings;
        gX_F32 phi1 = (gX_F32)(r + 1) * gX_PI / (gX_F32)rings;
        for (gX_I32 s = 0; s < slices; s++) {
            gX_F32 th0 = (gX_F32)s * 2.0f * gX_PI / (gX_F32)slices;
            gX_F32 th1 = (gX_F32)(s + 1) * 2.0f * gX_PI / (gX_F32)slices;
            gX_F32 p00[3] = { sinf(phi0)*cosf(th0), cosf(phi0), sinf(phi0)*sinf(th0) };
            gX_F32 p01[3] = { sinf(phi0)*cosf(th1), cosf(phi0), sinf(phi0)*sinf(th1) };
            gX_F32 p11[3] = { sinf(phi1)*cosf(th1), cosf(phi1), sinf(phi1)*sinf(th1) };
            gX_F32 p10[3] = { sinf(phi1)*cosf(th0), cosf(phi1), sinf(phi1)*sinf(th0) };
            gX_Batch_Vertex3f(sphere.pos.x + p00[0]*sphere.radius, sphere.pos.y + p00[1]*sphere.radius, sphere.pos.z + p00[2]*sphere.radius);
            gX_Batch_Vertex3f(sphere.pos.x + p01[0]*sphere.radius, sphere.pos.y + p01[1]*sphere.radius, sphere.pos.z + p01[2]*sphere.radius);
            gX_Batch_Vertex3f(sphere.pos.x + p11[0]*sphere.radius, sphere.pos.y + p11[1]*sphere.radius, sphere.pos.z + p11[2]*sphere.radius);
            gX_Batch_Vertex3f(sphere.pos.x + p10[0]*sphere.radius, sphere.pos.y + p10[1]*sphere.radius, sphere.pos.z + p10[2]*sphere.radius);
        }
    }
    gX_Batch_End();
}

//opencode: D10 — cylinder via batch (body quads + cap fans, replaces raylib DrawCylinderEx)
gX_I0 gX_Cylinder_Draw(gX_Cylinder cylinder, gX_Color color) {
    gX_I32 sides = cylinder.sides < 3 ? 3 : cylinder.sides;
    gX_F32v3 dir = { cylinder.posEnd.x - cylinder.posStart.x,
                     cylinder.posEnd.y - cylinder.posStart.y,
                     cylinder.posEnd.z - cylinder.posStart.z };
    gX_F32 len = sqrtf(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
    if (len < 0.0001f) len = 1.0f;
    gX_F32v3 fwd = { dir.x/len, dir.y/len, dir.z/len };
    gX_F32v3 up = { 0.0f, 1.0f, 0.0f };
    gX_F32 fupDot = fwd.x*up.x + fwd.y*up.y + fwd.z*up.z;
    if (fupDot > 0.99f || fupDot < -0.99f) up = (gX_F32v3){ 1.0f, 0.0f, 0.0f };
    gX_F32v3 right = gX_F32v3_Normalize(gX_F32v3_Cross(fwd, up));
    gX_F32v3 realUp = gX_F32v3_Normalize(gX_F32v3_Cross(right, fwd));

    gX_Batch_Begin(GX_BATCH_QUADS);
    gX_Batch_Color4ub(color.r, color.g, color.b, color.a);
    for (gX_I32 i = 0; i < sides; i++) {
        gX_F32 a0 = (gX_F32)i * 2.0f * gX_PI / (gX_F32)sides;
        gX_F32 a1 = (gX_F32)(i + 1) * 2.0f * gX_PI / (gX_F32)sides;
        gX_F32 c0 = cosf(a0), s0 = sinf(a0);
        gX_F32 c1 = cosf(a1), s1 = sinf(a1);
        gX_F32 b0x = cylinder.posStart.x + right.x*c0*cylinder.radius.x + realUp.x*s0*cylinder.radius.x;
        gX_F32 b0y = cylinder.posStart.y + right.y*c0*cylinder.radius.x + realUp.y*s0*cylinder.radius.x;
        gX_F32 b0z = cylinder.posStart.z + right.z*c0*cylinder.radius.x + realUp.z*s0*cylinder.radius.x;
        gX_F32 b1x = cylinder.posStart.x + right.x*c1*cylinder.radius.x + realUp.x*s1*cylinder.radius.x;
        gX_F32 b1y = cylinder.posStart.y + right.y*c1*cylinder.radius.x + realUp.y*s1*cylinder.radius.x;
        gX_F32 b1z = cylinder.posStart.z + right.z*c1*cylinder.radius.x + realUp.z*s1*cylinder.radius.x;
        gX_F32 t0x = cylinder.posEnd.x + right.x*c0*cylinder.radius.y + realUp.x*s0*cylinder.radius.y;
        gX_F32 t0y = cylinder.posEnd.y + right.y*c0*cylinder.radius.y + realUp.y*s0*cylinder.radius.y;
        gX_F32 t0z = cylinder.posEnd.z + right.z*c0*cylinder.radius.y + realUp.z*s0*cylinder.radius.y;
        gX_F32 t1x = cylinder.posEnd.x + right.x*c1*cylinder.radius.y + realUp.x*s1*cylinder.radius.y;
        gX_F32 t1y = cylinder.posEnd.y + right.y*c1*cylinder.radius.y + realUp.y*s1*cylinder.radius.y;
        gX_F32 t1z = cylinder.posEnd.z + right.z*c1*cylinder.radius.y + realUp.z*s1*cylinder.radius.y;
        gX_Batch_Vertex3f(b0x, b0y, b0z);
        gX_Batch_Vertex3f(t0x, t0y, t0z);
        gX_Batch_Vertex3f(t1x, t1y, t1z);
        gX_Batch_Vertex3f(b1x, b1y, b1z);
    }
    gX_Batch_End();
    gX_Batch_Begin(GX_BATCH_TRIANGLES);
    gX_Batch_Color4ub(color.r, color.g, color.b, color.a);
    for (gX_I32 i = 0; i < sides; i++) {
        gX_F32 a0 = (gX_F32)i * 2.0f * gX_PI / (gX_F32)sides;
        gX_F32 a1 = (gX_F32)(i + 1) * 2.0f * gX_PI / (gX_F32)sides;
        gX_F32 c0 = cosf(a0), s0 = sinf(a0);
        gX_F32 c1 = cosf(a1), s1 = sinf(a1);
        gX_Batch_Vertex3f(cylinder.posEnd.x, cylinder.posEnd.y, cylinder.posEnd.z);
        gX_Batch_Vertex3f(cylinder.posEnd.x + right.x*c0*cylinder.radius.y + realUp.x*s0*cylinder.radius.y,
                          cylinder.posEnd.y + right.y*c0*cylinder.radius.y + realUp.y*s0*cylinder.radius.y,
                          cylinder.posEnd.z + right.z*c0*cylinder.radius.y + realUp.z*s0*cylinder.radius.y);
        gX_Batch_Vertex3f(cylinder.posEnd.x + right.x*c1*cylinder.radius.y + realUp.x*s1*cylinder.radius.y,
                          cylinder.posEnd.y + right.y*c1*cylinder.radius.y + realUp.y*s1*cylinder.radius.y,
                          cylinder.posEnd.z + right.z*c1*cylinder.radius.y + realUp.z*s1*cylinder.radius.y);
        gX_Batch_Vertex3f(cylinder.posStart.x, cylinder.posStart.y, cylinder.posStart.z);
        gX_Batch_Vertex3f(cylinder.posStart.x + right.x*c1*cylinder.radius.x + realUp.x*s1*cylinder.radius.x,
                          cylinder.posStart.y + right.y*c1*cylinder.radius.x + realUp.y*s1*cylinder.radius.x,
                          cylinder.posStart.z + right.z*c1*cylinder.radius.x + realUp.z*s1*cylinder.radius.x);
        gX_Batch_Vertex3f(cylinder.posStart.x + right.x*c0*cylinder.radius.x + realUp.x*s0*cylinder.radius.x,
                          cylinder.posStart.y + right.y*c0*cylinder.radius.x + realUp.y*s0*cylinder.radius.x,
                          cylinder.posStart.z + right.z*c0*cylinder.radius.x + realUp.z*s0*cylinder.radius.x);
    }
    gX_Batch_End();
}

//opencode: D10 — cube via 6 quads through batch (replaces raylib DrawCubeV)
gX_I0 gX_Cube_Draw(gX_Cube cube, gX_Color color) {
    gX_F32 hx = cube.size.x * 0.5f;
    gX_F32 hy = cube.size.y * 0.5f;
    gX_F32 hz = cube.size.z * 0.5f;
    gX_F32 cx = cube.pos.x, cy = cube.pos.y, cz = cube.pos.z;
    gX_Batch_Begin(GX_BATCH_QUADS);
    gX_Batch_Color4ub(color.r, color.g, color.b, color.a);
    // -Z face
    gX_Batch_Vertex3f(cx-hx, cy-hy, cz-hz); gX_Batch_Vertex3f(cx+hx, cy-hy, cz-hz);
    gX_Batch_Vertex3f(cx+hx, cy+hy, cz-hz); gX_Batch_Vertex3f(cx-hx, cy+hy, cz-hz);
    // +Z face
    gX_Batch_Vertex3f(cx-hx, cy-hy, cz+hz); gX_Batch_Vertex3f(cx+hx, cy-hy, cz+hz);
    gX_Batch_Vertex3f(cx+hx, cy+hy, cz+hz); gX_Batch_Vertex3f(cx-hx, cy+hy, cz+hz);
    // -X face
    gX_Batch_Vertex3f(cx-hx, cy-hy, cz-hz); gX_Batch_Vertex3f(cx-hx, cy+hy, cz-hz);
    gX_Batch_Vertex3f(cx-hx, cy+hy, cz+hz); gX_Batch_Vertex3f(cx-hx, cy-hy, cz+hz);
    // +X face
    gX_Batch_Vertex3f(cx+hx, cy-hy, cz-hz); gX_Batch_Vertex3f(cx+hx, cy+hy, cz-hz);
    gX_Batch_Vertex3f(cx+hx, cy+hy, cz+hz); gX_Batch_Vertex3f(cx+hx, cy-hy, cz+hz);
    // -Y face
    gX_Batch_Vertex3f(cx-hx, cy-hy, cz-hz); gX_Batch_Vertex3f(cx+hx, cy-hy, cz-hz);
    gX_Batch_Vertex3f(cx+hx, cy-hy, cz+hz); gX_Batch_Vertex3f(cx-hx, cy-hy, cz+hz);
    // +Y face
    gX_Batch_Vertex3f(cx-hx, cy+hy, cz-hz); gX_Batch_Vertex3f(cx+hx, cy+hy, cz-hz);
    gX_Batch_Vertex3f(cx+hx, cy+hy, cz+hz); gX_Batch_Vertex3f(cx-hx, cy+hy, cz+hz);
    gX_Batch_End();
}

//opencode: D10 — grid via batch lines (replaces raylib DrawGrid)
gX_I0 gX_Grid_Draw(gX_I32 slices, gX_F32 spacing) {
    gX_I32 halfSlices = slices / 2;
    gX_Batch_Begin(GX_BATCH_LINES);
    for (gX_I32 i = -halfSlices; i <= halfSlices; i++) {
        gX_Batch_Color4ub(200, 200, 200, 255);
        if (i == 0) gX_Batch_Color4ub(80, 80, 80, 255);
        gX_Batch_Vertex3f((gX_F32)i * spacing, 0.0f, (gX_F32)-halfSlices * spacing);
        gX_Batch_Vertex3f((gX_F32)i * spacing, 0.0f, (gX_F32)halfSlices * spacing);
        gX_Batch_Color4ub(200, 200, 200, 255);
        if (i == 0) gX_Batch_Color4ub(80, 80, 80, 255);
        gX_Batch_Vertex3f((gX_F32)-halfSlices * spacing, 0.0f, (gX_F32)i * spacing);
        gX_Batch_Vertex3f((gX_F32)halfSlices * spacing, 0.0f, (gX_F32)i * spacing);
    }
    gX_Batch_End();
}
