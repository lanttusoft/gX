/**********************************************************************************************
*
*   gxmath/sdf.c
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

//opencode: ported SDF primitives from gamemath to gxmath

#include <math.h>

#include "sdf.h"
#include "scalar.h"
#include "vector2.h"
#include "vector3.h"

//opencode: gX_F32v3_SDF_RepeatInfinite
gX_F32v3 gX_F32v3_SDF_RepeatInfinite(gX_F32v3 p, gX_F32v3 c) {
    gX_F32v3 halfc = { c.x * 0.5f, c.y * 0.5f, c.z * 0.5f };
    gX_F32v3 phalfc = { p.x + halfc.x, p.y + halfc.y, p.z + halfc.z };
    gX_F32v3 mod;
    mod.x = fmodf(phalfc.x, c.x);
    mod.y = fmodf(phalfc.y, c.y);
    mod.z = fmodf(phalfc.z, c.z);
    gX_F32v3 result;
    result.x = mod.x - halfc.x;
    result.y = mod.y - halfc.y;
    result.z = mod.z - halfc.z;
    return result;
}

//opencode: gX_F32v3_SDF_RepeatFinite
gX_F32v3 gX_F32v3_SDF_RepeatFinite(gX_F32v3 p, gX_F32v3 a, gX_F32v3 d) {
    gX_F32v3 p_minus_d = { p.x - d.x, p.y - d.y, p.z - d.z };
    gX_F32v3 p_over_d = { p.x / d.x, p.y / d.y, p.z / d.z };
    gX_F32v3 rounded;
    rounded.x = roundf(p_over_d.x);
    rounded.y = roundf(p_over_d.y);
    rounded.z = roundf(p_over_d.z);
    gX_F32v3 clamped;
    clamped.x = fminf(fmaxf(rounded.x, -a.x), a.x);
    clamped.y = fminf(fmaxf(rounded.y, -a.y), a.y);
    clamped.z = fminf(fmaxf(rounded.z, -a.z), a.z);
    gX_F32v3 result;
    result.x = p_minus_d.x * clamped.x;
    result.y = p_minus_d.y * clamped.y;
    result.z = p_minus_d.z * clamped.z;
    return result;
}

//opencode: gX_F32_SDF_Sphere
gX_F32 gX_F32_SDF_Sphere(gX_F32v3 p, gX_F32 r) {
    return sqrtf(p.x * p.x + p.y * p.y + p.z * p.z) - r;
}

//opencode: gX_F32_SDF_Plane
gX_F32 gX_F32_SDF_Plane(gX_F32v3 p, gX_F32v3 n) {
    gX_F32 nl = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    gX_F32 inv = (nl > 0.0f) ? 1.0f / nl : 1.0f;
    gX_F32 nnx = n.x * inv;
    gX_F32 nny = n.y * inv;
    gX_F32 nnz = n.z * inv;
    return p.x * nnx + p.y * nny + p.z * nnz;
}

//opencode: gX_F32_SDF_Cube
gX_F32 gX_F32_SDF_Cube(gX_F32v3 p, gX_F32v3 b) {
    gX_F32 qx = fabsf(p.x) - b.x;
    gX_F32 qy = fabsf(p.y) - b.y;
    gX_F32 qz = fabsf(p.z) - b.z;
    gX_F32 len = sqrtf(fmaxf(qx, 0.0f) * fmaxf(qx, 0.0f) +
                       fmaxf(qy, 0.0f) * fmaxf(qy, 0.0f) +
                       fmaxf(qz, 0.0f) * fmaxf(qz, 0.0f));
    return len + fminf(fmaxf(qx, fmaxf(qy, qz)), 0.0f);
}

//opencode: gX_F32_SDF_Torus
gX_F32 gX_F32_SDF_Torus(gX_F32v3 p, gX_F32v2 t) {
    gX_F32 pl = sqrtf(p.x * p.x + p.z * p.z);
    gX_F32 dx = pl - t.x;
    return sqrtf(dx * dx + p.y * p.y) - t.y;
}

//opencode: gX_F32_SDF_BoxFrame
gX_F32 gX_F32_SDF_BoxFrame(gX_F32v3 p, gX_F32v3 b, gX_F32 e) {
    gX_F32 px = fabsf(p.x) - b.x;
    gX_F32 py = fabsf(p.y) - b.y;
    gX_F32 pz = fabsf(p.z) - b.z;
    gX_F32 qx = fabsf(px + e) - e;
    gX_F32 qy = fabsf(py + e) - e;
    gX_F32 qz = fabsf(pz + e) - e;
    gX_F32 v1x = fmaxf(px, 0.0f);
    gX_F32 v1y = fmaxf(qy, 0.0f);
    gX_F32 v1z = fmaxf(qz, 0.0f);
    gX_F32 d1 = sqrtf(v1x * v1x + v1y * v1y + v1z * v1z) + fminf(fmaxf(px, fmaxf(qy, qz)), 0.0f);
    gX_F32 v2x = fmaxf(qx, 0.0f);
    gX_F32 v2y = fmaxf(py, 0.0f);
    gX_F32 v2z = fmaxf(qz, 0.0f);
    gX_F32 d2 = sqrtf(v2x * v2x + v2y * v2y + v2z * v2z) + fminf(fmaxf(qx, fmaxf(py, qz)), 0.0f);
    gX_F32 v3x = fmaxf(qx, 0.0f);
    gX_F32 v3y = fmaxf(qy, 0.0f);
    gX_F32 v3z = fmaxf(pz, 0.0f);
    gX_F32 d3 = sqrtf(v3x * v3x + v3y * v3y + v3z * v3z) + fminf(fmaxf(qx, fmaxf(qy, pz)), 0.0f);
    return fminf(d1, fminf(d2, d3));
}

//opencode: gX_F32_SDF_Cylinder
gX_F32 gX_F32_SDF_Cylinder(gX_F32v3 p, gX_F32v2 h) {
    gX_F32 vlen = sqrtf(p.x * p.x + p.z * p.z);
    gX_F32 dx = vlen - h.x;
    gX_F32 dy = p.y - h.y;
    gX_F32 d;
    d = fminf(fmaxf(dx, dy), 0.0f) + sqrtf(fmaxf(dx, 0.0f) * fmaxf(dx, 0.0f) + fmaxf(dy, 0.0f) * fmaxf(dy, 0.0f));
    return d;
}

//opencode: gX_F32_SDF_Cone
gX_F32 gX_F32_SDF_Cone(gX_F32v3 p, gX_F32v3 c) {
    gX_F32 vlen = sqrtf(p.x * p.x + p.z * p.z);
    gX_F32 qx = vlen;
    gX_F32 qy = p.y;
    gX_F32 d1 = -qy - c.z;
    gX_F32 d2x = qx * c.x + qy * c.y;
    gX_F32 d2y = qy;
    gX_F32 d2 = fmaxf(d2x, d2y);
    gX_F32 d = fminf(fmaxf(d1, d2), 0.0f) + sqrtf(fmaxf(d1, 0.0f) * fmaxf(d1, 0.0f) + fmaxf(d2, 0.0f) * fmaxf(d2, 0.0f));
    return d;
}

//opencode: gX_F32_SDF_Capsule — segment a..b, radius r (GLSL twin:
//   gX_SDF_Capsule in common/sdf_basic.glsl)
gX_F32 gX_F32_SDF_Capsule(gX_F32v3 p, gX_F32v3 a, gX_F32v3 b, gX_F32 r) {
    gX_F32 pax = p.x - a.x, pay = p.y - a.y, paz = p.z - a.z;
    gX_F32 bax = b.x - a.x, bay = b.y - a.y, baz = b.z - a.z;
    gX_F32 denom = bax * bax + bay * bay + baz * baz;
    gX_F32 h = (denom > 0.0f)
        ? (pax * bax + pay * bay + paz * baz) / denom : 0.0f;
    if (h < 0.0f) h = 0.0f;
    if (h > 1.0f) h = 1.0f;
    gX_F32 dx = pax - bax * h, dy = pay - bay * h, dz = paz - baz * h;
    return sqrtf(dx * dx + dy * dy + dz * dz) - r;
}

//opencode: gX_F32_SDF_Octahedron (GLSL twin: gX_SDF_Octahedron)
gX_F32 gX_F32_SDF_Octahedron(gX_F32v3 p, gX_F32 s) {
    gX_F32 x = fabsf(p.x), y = fabsf(p.y), z = fabsf(p.z);
    return (x + y + z - s) * 0.57735027f;
}

//opencode: gX_F32_SDF_RoundBox (GLSL twin: gX_SDF_RoundBox)
gX_F32 gX_F32_SDF_RoundBox(gX_F32v3 p, gX_F32v3 b, gX_F32 r) {
    gX_F32 qx = fabsf(p.x) - b.x + r;
    gX_F32 qy = fabsf(p.y) - b.y + r;
    gX_F32 qz = fabsf(p.z) - b.z + r;
    gX_F32 len = sqrtf(fmaxf(qx, 0.0f) * fmaxf(qx, 0.0f) +
                       fmaxf(qy, 0.0f) * fmaxf(qy, 0.0f) +
                       fmaxf(qz, 0.0f) * fmaxf(qz, 0.0f));
    return len + fminf(fmaxf(qx, fmaxf(qy, qz)), 0.0f) - r;
}

//opencode: gX_F32_SDF_HexPrism (GLSL twin: gX_SDF_HexPrism)
gX_F32 gX_F32_SDF_HexPrism(gX_F32v3 p, gX_F32v2 h) {
    const gX_F32 kx = -0.8660254f, ky = 0.5f, kz = 0.57735f;
    gX_F32 px = fabsf(p.x), py = fabsf(p.y);
    gX_F32 dotxy = kx * px + ky * py;
    if (dotxy < 0.0f) {
        px -= 2.0f * dotxy * kx;
        py -= 2.0f * dotxy * ky;
    }
    gX_F32 clx = px;
    if (clx < -kz * h.x) clx = -kz * h.x;
    if (clx > kz * h.x) clx = kz * h.x;
    gX_F32 ddx = px - clx, ddy = py - h.x;
    gX_F32 dx = sqrtf(ddx * ddx + ddy * ddy) * ((ddy < 0.0f) ? -1.0f : 1.0f);
    gX_F32 dy = fabsf(p.z) - h.y;
    return fminf(fmaxf(dx, dy), 0.0f) +
        sqrtf(fmaxf(dx, 0.0f) * fmaxf(dx, 0.0f) +
              fmaxf(dy, 0.0f) * fmaxf(dy, 0.0f));
}

//opencode: gX_F32_SDF_Mandelbulb
gX_F32 gX_F32_SDF_Mandelbulb(gX_F32v3 p, gX_F32v4 param) {
    gX_F32v3 z = p;
    gX_F32 r = 0.0f;
    gX_F32 dr = param.x;
    gX_F32 power = param.y;
    gX_F32 lim = param.z;
    for (int i = 0; i < (int)lim; i++) {
        r = sqrtf(z.x * z.x + z.y * z.y + z.z * z.z);
        if (r > param.w) break;
        gX_F32 theta = acosf(z.z / r) * power;
        gX_F32 phi = 0.0f;
        if (!gX_F32_Equals(z.x, 0.0f)) phi = atan2f(z.y, z.x) * power;
        gX_F32 zr = powf(r, power);
        dr = powf(r, power - 1.0f) * power * dr + 1.0f;
        gX_F32 st = sinf(theta);
        z.x = st * cosf(phi) * zr;
        z.y = st * sinf(phi) * zr;
        z.z = cosf(theta) * zr;
        z.x += p.x;
        z.y += p.y;
        z.z += p.z;
    }
    return 0.5f * logf(r) * r / dr;
}

//opencode: gX_F32_SDF_Menger
static gX_F32 menger_cube(gX_F32v3 p, gX_F32v3 b) {
    gX_F32 qx = fabsf(p.x) - b.x;
    gX_F32 qy = fabsf(p.y) - b.y;
    gX_F32 qz = fabsf(p.z) - b.z;
    gX_F32 len = sqrtf(fmaxf(qx, 0.0f) * fmaxf(qx, 0.0f) +
                       fmaxf(qy, 0.0f) * fmaxf(qy, 0.0f) +
                       fmaxf(qz, 0.0f) * fmaxf(qz, 0.0f));
    return len + fminf(fmaxf(qx, fmaxf(qy, qz)), 0.0f);
}

gX_F32 gX_F32_SDF_Menger(gX_F32v3 p, gX_F32v3 b) {
    gX_F32 d = menger_cube(p, b);
    gX_F32 s = 1.0f / b.x;
    for (int m = 0; m < 8; m++) {
        //opencode: floor-mod (not fmodf) so the fold matches GLSL mod for
        //   negative inputs — the shader twin is gX_SDF_Menger.
        gX_F32v3 a;
        a.x = p.x * s - 2.0f * floorf(p.x * s / 2.0f) - 1.0f;
        a.y = p.y * s - 2.0f * floorf(p.y * s / 2.0f) - 1.0f;
        a.z = p.z * s - 2.0f * floorf(p.z * s / 2.0f) - 1.0f;
        s *= 3.0f;
        gX_F32v3 r;
        r.x = fabsf(1.0f - fabsf(a.x) * 3.0f);
        r.y = fabsf(1.0f - fabsf(a.y) * 3.0f);
        r.z = fabsf(1.0f - fabsf(a.z) * 3.0f);
        gX_F32 da = fmaxf(r.x, r.y);
        gX_F32 db = fmaxf(r.y, r.z);
        gX_F32 dc = fmaxf(r.z, r.x);
        gX_F32 c = (fminf(da, fminf(db, dc)) - 1.0f) / s;
        if (c > d) d = c;
    }
    return d;
}
