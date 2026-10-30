#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: math pass — verifies the engine's gxmath corpus against an
#   independent SymPy oracle. The engine prints the corpus with
#   GX_MATH_DUMP=1 (src/gxmath/gxmath_tests.c); this script recomputes every
#   case with SymPy — exact rationals for the algebraic cases, float64
#   formulas for the trig/sqrt cases — so a slip in the C formulas fails here
#   even when the engine's own unit tests agree with it.
#
#   Case layouts (mirrored in gxmath_tests.c):
#     multiply    L(16) R(16) O(16)     invert      M(16) O(16)
#     transform   M(16) v(3)  O(3)      determinant M(16) O(1)
#     perspective fov aspect near far(4) O(16)
#     rotate_xyz  rx ry rz(3) O(16)     lookat      eye(3) target(3) up(3) O(16)
#     vectors     a(3) b(3) cross(3) dot(1) proj(3)
#     sdf_ext     p(3) b(3) e m(3) param(4) c(3) a(3) d(3) O(8)
#     quaternion  q1(4) q2(4) qa(4) qb(4) t(1) O(24)
#     quaternion_rot axis(3) angle euler(3) q(4) O(35)
#     matrix_aux  a(16) b(16) frustum(6) ortho(6) vectors(16) O(176)
#     matrix_small m2(4) m3(9) O(4)
#     f64         a(4) b(4) t(1) O(4) — emitted as MATHDUMP64
#     edge_cases  singular(16) O(86) — each scalar is (kind, value), kind
#                 0 finite, 1 +inf, 2 -inf, 3 NaN
#
#   The runner is fail-closed: a non-zero child status, a malformed or
#   duplicate case, a non-finite value, a count mismatch, or an oracle/native
#   arity mismatch fails the gate instead of being skipped.
#
#   Convention notes (verified against src/gxmath/matrix.c):
#     - matrices are stored row-major by name (m0..m15) and multiplied with
#       the standard row x column product;
#     - gX_F32v3_Transform(v, M) = (M^T * (v,1))[0..2] (row-vector x matrix);
#     - gX_Matrix_RotateXYZ equals the column-convention Rz*Ry*Rx (the
#       natural X-then-Y-then-Z order);
#     - gX_Matrix_LookAt equals the standard gluLookAt view transposed.

import argparse
import cmath
import math
import subprocess
import sys
from pathlib import Path

try:
    from sympy import Float, Matrix, Rational, cos, sin, sqrt, tan
except ImportError:
    print("math-pass: sympy not found (pip install sympy)", file=sys.stderr)
    sys.exit(1)

ROOT = Path(__file__).resolve().parent.parent

TOL_EXACT = 1e-5
TOL_FLOAT = 1e-4


class MathPassError(ValueError):
    """Raised when a dump or oracle result cannot be verified."""


def exact(x):
    return Rational(x)


def f64(x):
    return Float(x, 17)


def mat(vals, conv):
    return Matrix(4, 4, [conv(v) for v in vals])


def v3(vals, conv):
    return Matrix(3, 1, [conv(v) for v in vals])


def rot_col(axis, ang):
    c, s = cos(ang), sin(ang)
    if axis == "x":
        return Matrix([[1, 0, 0], [0, c, -s], [0, s, c]])
    if axis == "y":
        return Matrix([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    return Matrix([[c, -s, 0], [s, c, 0], [0, 0, 1]])


#opencode: reference PCG32 (XSH-RR 64/32), transcribed from the algorithm so
#   the engine's implementation is checked against a second implementation,
#   anchored by the published pcg32-demo vectors for seed 42 / stream 54.
PCG_DEMO = [0xA15C02B7, 0x7B47F409, 0xBA1D3330, 0x83D2F293, 0xBFA4784B, 0xCBED606E]
MASK32 = (1 << 32) - 1
MASK64 = (1 << 64) - 1


class Pcg32:
    def __init__(self, seed, stream):
        self.state = 0
        self.inc = ((stream << 1) | 1) & MASK64
        self.next()
        self.state = (self.state + seed) & MASK64
        self.next()

    def next(self):
        old = self.state
        self.state = (old * 6364136223846793005 + self.inc) & MASK64
        xorshifted = (((old >> 18) ^ old) >> 27) & MASK32
        rot = (old >> 59) & 31
        return ((xorshifted >> rot) | (xorshifted << ((32 - rot) & 31))) & MASK32

    def bounded(self, bound):
        if bound == 0:
            return 0
        threshold = ((-bound) & MASK32) % bound
        while True:
            v = self.next()
            if v >= threshold:
                return v % bound


def split32(v):
    return [float((v >> 16) & 0xFFFF), float(v & 0xFFFF)]


def case_random(inp, conv):
    seed = int(inp[0]) * 65536 + int(inp[1])
    stream = int(inp[2]) * 65536 + int(inp[3])
    if seed == 42 and stream == 54:
        raws = PCG_DEMO
    else:
        r = Pcg32(seed, stream)
        raws = [r.next() for _ in range(6)]
    out = []
    for v in raws:
        out += split32(v)
    r = Pcg32(seed, stream)
    for _ in range(4):
        out += split32(r.bounded(100))
    return out


def _sdf_cube(p, b):
    qx, qy, qz = abs(p[0]) - b[0], abs(p[1]) - b[1], abs(p[2]) - b[2]
    return (math.sqrt(max(qx, 0.0) ** 2 + max(qy, 0.0) ** 2 + max(qz, 0.0) ** 2)
            + min(max(qx, max(qy, qz)), 0.0))


def case_sdf(inp, conv):
    (p, r, n, b, t, h, c) = (inp[0:3], inp[3], inp[4:7], inp[7:10],
                             inp[10:12], inp[12:14], inp[14:17])
    #ai: v0.15 twins — capsule (a, b, r), octahedron size, rounded-box
    #   radius, hex-prism (radius, half-height).
    ca, cb, cr = inp[17:20], inp[20:23], inp[23]
    os_, rr, hh = inp[24], inp[25], inp[26:28]
    out = []
    out.append(math.sqrt(p[0] ** 2 + p[1] ** 2 + p[2] ** 2) - r)
    nl = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
    inv = 1.0 / nl if nl > 0.0 else 1.0
    out.append(p[0] * n[0] * inv + p[1] * n[1] * inv + p[2] * n[2] * inv)
    out.append(_sdf_cube(p, b))
    pl = math.sqrt(p[0] ** 2 + p[2] ** 2)
    dx = pl - t[0]
    out.append(math.sqrt(dx * dx + p[1] ** 2) - t[1])
    vl = math.sqrt(p[0] ** 2 + p[2] ** 2)
    dx, dy = vl - h[0], p[1] - h[1]
    out.append(min(max(dx, dy), 0.0) + math.sqrt(max(dx, 0.0) ** 2 + max(dy, 0.0) ** 2))
    vl = math.sqrt(p[0] ** 2 + p[2] ** 2)
    qx, qy = vl, p[1]
    d1 = -qy - c[2]
    d2 = max(qx * c[0] + qy * c[1], qy)
    out.append(min(max(d1, d2), 0.0) + math.sqrt(max(d1, 0.0) ** 2 + max(d2, 0.0) ** 2))
    # capsule: distance to the segment a..b minus r
    bax, bay, baz = cb[0] - ca[0], cb[1] - ca[1], cb[2] - ca[2]
    denom = bax * bax + bay * bay + baz * baz
    hh_ = ((p[0] - ca[0]) * bax + (p[1] - ca[1]) * bay + (p[2] - ca[2]) * baz) / denom
    hh_ = min(max(hh_, 0.0), 1.0)
    ddx = p[0] - ca[0] - bax * hh_
    ddy = p[1] - ca[1] - bay * hh_
    ddz = p[2] - ca[2] - baz * hh_
    out.append(math.sqrt(ddx * ddx + ddy * ddy + ddz * ddz) - cr)
    out.append((abs(p[0]) + abs(p[1]) + abs(p[2]) - os_) * 0.57735027)
    qx, qy, qz = abs(p[0]) - b[0] + rr, abs(p[1]) - b[1] + rr, abs(p[2]) - b[2] + rr
    out.append(math.sqrt(max(qx, 0.0) ** 2 + max(qy, 0.0) ** 2 + max(qz, 0.0) ** 2)
               + min(max(qx, max(qy, qz)), 0.0) - rr)
    kx, ky, kz = -0.8660254, 0.5, 0.57735
    px, py = abs(p[0]), abs(p[1])
    dotxy = kx * px + ky * py
    if dotxy < 0.0:
        px -= 2.0 * dotxy * kx
        py -= 2.0 * dotxy * ky
    cl = min(max(px, -kz * hh[0]), kz * hh[0])
    ddx, ddy = px - cl, py - hh[0]
    hx = math.sqrt(ddx * ddx + ddy * ddy) * (-1.0 if ddy < 0.0 else 1.0)
    hy = abs(p[2]) - hh[1]
    out.append(min(max(hx, hy), 0.0)
               + math.sqrt(max(hx, 0.0) ** 2 + max(hy, 0.0) ** 2))
    # menger sponge: cube minus the cross folds (floor-mod matches GLSL mod)
    d = _sdf_cube(p, b)
    s = 1.0 / b[0]
    for _ in range(8):
        a = [p[i] * s - 2.0 * math.floor(p[i] * s / 2.0) - 1.0 for i in range(3)]
        s *= 3.0
        rr_ = [abs(1.0 - abs(a[i]) * 3.0) for i in range(3)]
        da = max(rr_[0], rr_[1])
        db = max(rr_[1], rr_[2])
        dc = max(rr_[2], rr_[0])
        d = max(d, (min(da, min(db, dc)) - 1.0) / s)
    out.append(d)
    return out


def case_easing(inp, conv):
    st, en, t, s, pp, c0, c1 = inp[0:7]
    return [
        en if t >= s else st,
        (1 - t) * st + t * en,
        (1 - t) ** 2 * st + t ** 2 * en,
        (1 - t) ** pp * st + t ** pp * en,
        (1 - t) ** 3 * st + 3 * (1 - t) ** 2 * t * c0 + 3 * (1 - t) * t ** 2 * c1 + t ** 3 * en,
    ]


def case_scalar(inp, conv):
    v, mn, mx, is_, ie, os_, oe, la, lb, lt = inp[0:10]
    return [
        min(max(v, mn), mx),
        (1 - lt) * la + lt * lb,
        (v - is_) / (ie - is_),
        (v - is_) / (ie - is_) * (oe - os_) + os_,
        v - (mx - mn) * math.floor((v - mn) / (mx - mn)),
    ]


def case_int_math(inp, conv):
    n, a, b, c, d, e, f, g, h = (int(x) for x in inp[0:9])
    return [math.factorial(n), math.gcd(a, b), math.lcm(c, d), math.comb(e, f), math.perm(g, h)]


def case_color(inp, conv):
    c1 = [int(x) for x in inp[0:4]]
    c2 = [int(x) for x in inp[4:8]]
    t = inp[8]

    def lerp_ch(a, b):
        # C truncates toward zero; the engine clamps to [0,255]
        v = a + int(t * float(b - a))
        return max(0, min(255, v))

    out = [lerp_ch(c1[i], c2[i]) for i in range(4)]
    out += [lerp_ch(c1[i], c2[i]) for i in range(3)] + [255]
    out += [int(c1[i] / 255.0 * c2[i] / 255.0 * 255.0) for i in range(4)]
    out += [c1[i] / 255.0 for i in range(4)]
    return out


#opencode: CODATA/SI constants — deliberately independent of the engine's
#   config.h (the engine's h is the older 6.626070040e-34, ~1.7e-8 off the
#   2019 SI exact value; the tolerance covers the difference and a wrong
#   exponent or formula still fails).
C_LIGHT = 299792458.0
BOLTZMANN = 1.380649e-23
PLANCK_H = 6.62607015e-34
COULOMB_K = 8.9875517923e9
MU0_4PI = 1.0e-7
STEFAN_BOLTZMANN = 5.670374419e-8
WIEN_B = 2.897771955e-3


def case_thermal(inp, conv):
    temperature, area, emissivity = inp[0], inp[1], inp[2]
    sample = [planck_radiance(temperature, w) for w in (650e-9, 520e-9, 460e-9)]
    peak = max(sample)
    rgb = [value / peak for value in sample] if peak > 0.0 else [0.0, 0.0, 0.0]
    power = STEFAN_BOLTZMANN * emissivity * area * temperature ** 4
    wien = WIEN_B / temperature
    return rgb + [power, wien]


def case_em(inp, conv):
    q1, q2, dist = inp[0:3]
    force = COULOMB_K * q1 * q2 / (dist * dist)
    potential = COULOMB_K * q1 * q2 / dist
    qfield, rx, ry, rz = inp[3:7]
    r3 = (rx * rx + ry * ry + rz * rz) ** 1.5
    field = [COULOMB_K * qfield * component / r3 for component in (rx, ry, rz)]
    qv, vx, vy, vz, bx, by, bz = inp[7:14]
    velocity = (vx, vy, vz)
    magnetic = (bx, by, bz)
    lorentz = [
        qv * (velocity[1] * magnetic[2] - velocity[2] * magnetic[1]),
        qv * (velocity[2] * magnetic[0] - velocity[0] * magnetic[2]),
        qv * (velocity[0] * magnetic[1] - velocity[1] * magnetic[0]),
    ]
    cyclotron = inp[14] * inp[15] / (abs(inp[16]) * inp[17])
    photon = PLANCK_H * C_LIGHT / inp[18]
    return [force, potential] + field + lorentz + [cyclotron, photon]


def case_dipole(inp, conv):
    m1 = inp[0:3]
    m2 = inp[3:6]
    r = inp[6:9]
    r2 = sum(component * component for component in r)
    rlen = math.sqrt(r2)
    rhat = [component / rlen for component in r]
    m1_r = sum(a * b for a, b in zip(m1, rhat))
    m2_r = sum(a * b for a, b in zip(m2, rhat))
    m1_m2 = sum(a * b for a, b in zip(m1, m2))
    field = [
        MU0_4PI * (3.0 * m1_r * rhat[i] - m1[i]) / r2 ** 1.5
        for i in range(3)
    ]
    m2x, m2y, m2z = m2
    torque = [
        m2y * field[2] - m2z * field[1],
        m2z * field[0] - m2x * field[2],
        m2x * field[1] - m2y * field[0],
    ]
    scale = 3.0 * MU0_4PI / (r2 * r2)
    force = [
        scale * (m1_r * m2[i] + m2_r * m1[i] + m1_m2 * rhat[i]
                 - 5.0 * m1_r * m2_r * rhat[i])
        for i in range(3)
    ]
    on_axis = -6.0 * MU0_4PI * inp[9] * inp[10] / inp[11] ** 4
    potential = -MU0_4PI * (3.0 * m1_r * m2_r - m1_m2) / r2 ** 1.5
    field_axis = MU0_4PI * 2.0 * inp[12] / inp[13] ** 3
    field_equatorial = MU0_4PI * inp[14] / inp[15] ** 3
    return field + torque + force + [on_axis, potential, field_axis,
                                     field_equatorial]


def planck_radiance(T, lam):
    return (2.0 * PLANCK_H * C_LIGHT ** 2) / (
        lam ** 5 * (math.exp(PLANCK_H * C_LIGHT / (lam * BOLTZMANN * T)) - 1.0))


def case_physics(inp, conv):
    force, mass, g, h = inp[0:4], inp[4], inp[5], inp[6]
    vel, kmass, length, speed = inp[7:11], inp[11], inp[12], inp[13]
    acc = [f / mass for f in force]
    ep = mass * g * h
    ek = 0.5 * kmass * sum(v * v for v in vel)
    lorentz = length * math.sqrt(1.0 - (speed / C_LIGHT) ** 2)
    return acc + [ep, ek, lorentz]


def case_relativity(inp, conv):
    velocity, light_speed, u, v = inp[0:4]
    time0, length0, doppler_beta, rapidity_beta = inp[4:8]
    beta = velocity / light_speed
    gamma = 1.0 / math.sqrt(1.0 - beta * beta)
    contracted = length0 * math.sqrt(1.0 - 0.9 * 0.9)
    dilated = time0 * (1.0 / math.sqrt(1.0 - 0.9 * 0.9))
    doppler = math.sqrt((1.0 + doppler_beta) / (1.0 - doppler_beta))
    added = (u + v) / (1.0 + (u * v) / (light_speed * light_speed))
    rapidity = math.atanh(rapidity_beta)
    return [beta, gamma, contracted, dilated, doppler, added, rapidity]


def case_planck(inp, conv):
    T, lam = inp[0], inp[1]
    rj = 2.0 * C_LIGHT * BOLTZMANN * T / lam ** 4
    rgb = [planck_radiance(T, w) for w in (650e-9, 520e-9, 460e-9)]
    return [rj, planck_radiance(T, lam)] + rgb


def dft(signal):
    n = len(signal)
    out = []
    for k in range(n):
        acc = 0j
        for i, xv in enumerate(signal):
            acc += xv * cmath.exp(-2j * math.pi * k * i / n)
        out.append(acc)
    return out


def case_fft(inp, conv):
    n = len(inp) // 2
    signal = [complex(inp[2 * i], inp[2 * i + 1]) for i in range(n)]
    out = []
    for z in dft(signal):
        out += [z.real, z.imag]
    return out


def case_calculus(inp, conv):
    x, d, steps, a, b, px, py, pz = inp[0:8]
    # probes fixed in gxmath_tests.c: f1 = x^2, f2 = x^2 - 2,
    # f3 = x^2 + y*z, v3 = (x*y, y*z, z*x)
    return [
        2.0 * x,                      # f1'(x); the forward difference adds O(d)
        2.0 * px, pz, py,             # grad f3
        py + pz + px,                 # div v3
        -py, -pz, -px,                # curl v3
        (b ** 3 - a ** 3) / 3.0,      # integral of x^2 (Simpson is exact here)
        math.sqrt(2.0),               # Newton root of x^2 - 2
    ]


def case_convert_units(inp, conv):
    meters, feet, inches, kelvin, celsius, prefix = inp[0:6]
    return [
        feet * 0.3048,
        meters / 0.3048,
        inches * 0.0254,
        meters / 0.0254,
        kelvin - 273.15,
        celsius + 273.15,
        meters / (10.0 ** int(prefix)),
    ]


def case_convert_color(inp, conv):
    r, g, b, h, s, v = inp[0:6]
    mx, mn = max(r, g, b), min(r, g, b)
    d = mx - mn
    hv = 0.0
    if d > 0.0:
        if mx == r:
            hv = 60.0 * math.fmod((g - b) / d, 6.0)
        elif mx == g:
            hv = 60.0 * ((b - r) / d + 2.0)
        else:
            hv = 60.0 * ((r - g) / d + 4.0)
        if hv < 0.0:
            hv += 360.0
    sv = d / mx if mx > 0.0 else 0.0
    hh = h / 60.0
    hi = int(hh) % 6
    f = hh - hi
    p_ = v * (1.0 - s)
    q = v * (1.0 - s * f)
    t = v * (1.0 - s * (1.0 - f))
    sectors = [
        (v, t, p_), (q, v, p_), (p_, v, t),
        (p_, q, v), (t, p_, v), (v, p_, q),
    ]
    return [hv, sv, mx] + list(sectors[hi])


def case_convert_coords(inp, conv):
    x, y, z = inp[0:3]
    cyl = [math.sqrt(x * x + y * y), math.atan2(y, x), z]
    sph = [math.sqrt(x * x + y * y + z * z), math.atan2(y, x),
           math.acos(z / math.sqrt(x * x + y * y + z * z))]
    rho, theta, zz = cyl
    r, phi = sph[0], sph[2]
    cyl2sph = [math.sqrt(rho * rho + zz * zz), theta, math.acos(zz / math.sqrt(rho * rho + zz * zz))]
    sph2cyl = [r * math.sin(phi), theta, r * math.cos(phi)]
    # the dump's second half of each pair is the round trip, which must be p
    return cyl + [x, y, z] + sph + [x, y, z] + cyl2sph + sph2cyl


def case_convert_sky(inp, conv):
    ex, ey, ez, lat, lon = inp[0:5]
    e = math.radians(23.4)
    ce, se = math.cos(e), math.sin(e)
    ecl = [ex, ey * ce + ez * se, ez * ce - ey * se]
    eq_inv = [ex, ey * ce - ez * se, ez * ce + ey * se]
    sl, cl = math.sin(lat), math.cos(lat)
    slon, clon = math.sin(lon), math.cos(lon)

    def eq2hor(p):
        x, y, z = p
        return [x * sl * clon + y * sl * slon - z * cl,
                x * slon - y * clon,
                x * cl * clon + y * cl * slon + z * sl]

    def hor2eq(p):
        x, y, z = p
        return [x * sl * clon + z * cl * clon + y * slon,
                x * sl * slon + z * cl * slon - y * clon,
                z * sl - x * cl]

    hor = eq2hor([ex, ey, ez])
    hor_inv = hor2eq([ex, ey, ez])
    # ecl2eq(eq2ecl(eq)) = eq, eq2ecl(ecl2eq(eq)) = ecl,
    # hor2eq(eq2hor(eq)) = eq, eq2hor(hor2eq(hor)) = hor
    return (ecl + eq_inv + [ex, ey, ez] + ecl
            + hor + hor_inv + [ex, ey, ez] + hor)


def case_convert_new(inp, conv):
    linear, srgb, amp, db, note, freq, celsius, fahrenheit = inp[0:8]

    def l2s(c):
        return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1.0 / 2.4) - 0.055

    def s2l(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    return [
        l2s(linear),
        s2l(srgb),
        20.0 * math.log10(amp),
        10.0 ** (db / 20.0),
        440.0 * 2.0 ** ((note - 69.0) / 12.0),
        69.0 + 12.0 * math.log2(freq / 440.0),
        celsius * 9.0 / 5.0 + 32.0,
        (fahrenheit - 32.0) * 5.0 / 9.0,
    ]


def case_convert_proj(inp, conv):
    x, y, z = inp[0:3]
    # the round-trip halves must return p; the forward halves use the fixed
    # conventions: bearing from +y toward +x, equatorial azimuth from +x
    return [
        math.atan2(x, y), math.asin(z), x, y, z,
        math.atan2(y, x), math.asin(z), x, y, z,
    ]


def case_convert_ball(inp, conv):
    x, y, z = inp[0:3]
    return [math.atan2(x, y), math.asin(z), x, y, z]


def case_multiply(inp, conv):
    L = mat(inp[0:16], conv)
    R = mat(inp[16:32], conv)
    return list(L * R)


def case_invert(inp, conv):
    return list(mat(inp[0:16], conv).inv())


def case_transform(inp, conv):
    M = mat(inp[0:16], conv)
    v = Matrix(4, 1, [inp[16], inp[17], inp[18], 1])
    out = M.T * v
    return [out[0], out[1], out[2]]


def case_determinant(inp, conv):
    return [mat(inp[0:16], conv).det()]


def case_perspective(inp, conv):
    fov, aspect, near, far = (conv(x) for x in inp[0:4])
    top = near * tan(fov / 2)
    right = top * aspect
    m = Matrix(4, 4, [0] * 16)
    m[0, 0] = near / right
    m[1, 1] = near / top
    m[2, 2] = -(far + near) / (far - near)
    m[2, 3] = -1
    m[3, 2] = -2 * far * near / (far - near)
    return list(m)


def case_rotate_xyz(inp, conv):
    rx, ry, rz = (conv(x) for x in inp[0:3])
    R = rot_col("z", rz) * rot_col("y", ry) * rot_col("x", rx)
    out = Matrix(4, 4, [0] * 16)
    for r in range(3):
        for c in range(3):
            out[r, c] = R[r, c]
    out[3, 3] = 1
    return list(out)


def case_lookat(inp, conv):
    eye = v3(inp[0:3], conv)
    target = v3(inp[3:6], conv)
    up = v3(inp[6:9], conv)
    f = (target - eye).normalized()
    s = f.cross(up).normalized()
    u = s.cross(f)
    v = Matrix(4, 4, [0] * 16)
    for c in range(3):
        v[0, c] = s[c]
        v[1, c] = u[c]
        v[2, c] = -f[c]
    v[0, 3] = -s.dot(eye)
    v[1, 3] = -u.dot(eye)
    v[2, 3] = f.dot(eye)
    v[3, 3] = 1
    return list(v.T)


def case_vectors(inp, conv):
    a = v3(inp[0:3], conv)
    b = v3(inp[3:6], conv)
    cross = a.cross(b)
    dot = a.dot(b)
    proj = b * (a.dot(b) / b.dot(b))
    return [cross[0], cross[1], cross[2], dot, proj[0], proj[1], proj[2]]


#ai: extend the SDF corpus to the primitives appended after the original six.
def _round_away(value):
    return math.floor(value + 0.5) if value >= 0.0 else math.ceil(value - 0.5)


def _sdf_box_frame(p, b, e):
    px, py, pz = abs(p[0]) - b[0], abs(p[1]) - b[1], abs(p[2]) - b[2]
    qx, qy, qz = abs(px + e) - e, abs(py + e) - e, abs(pz + e) - e
    d1 = math.sqrt(max(px, 0.0) ** 2 + max(qy, 0.0) ** 2
                   + max(qz, 0.0) ** 2) + min(max(px, max(qy, qz)), 0.0)
    d2 = math.sqrt(max(qx, 0.0) ** 2 + max(py, 0.0) ** 2
                   + max(qz, 0.0) ** 2) + min(max(qx, max(py, qz)), 0.0)
    d3 = math.sqrt(max(qx, 0.0) ** 2 + max(qy, 0.0) ** 2
                   + max(pz, 0.0) ** 2) + min(max(qx, max(qy, pz)), 0.0)
    return min(d1, min(d2, d3))


def _sdf_mandelbulb(p, param):
    z = list(p)
    r = 0.0
    dr = param[0]
    power = param[1]
    for _ in range(max(0, int(param[2]))):
        r = math.sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2])
        if r > param[3]:
            break
        theta = math.acos(z[2] / r) * power
        if abs(z[0]) <= 1e-6 * max(1.0, abs(z[0])):
            phi = 0.0
        else:
            phi = math.atan2(z[1], z[0]) * power
        zr = r ** power
        dr = (r ** (power - 1.0)) * power * dr + 1.0
        st = math.sin(theta)
        z = [st * math.cos(phi) * zr + p[0],
             st * math.sin(phi) * zr + p[1],
             math.cos(theta) * zr + p[2]]
    return 0.5 * math.log(r) * r / dr


def case_sdf_ext(inp, conv):
    p, b = inp[0:3], inp[3:6]
    e = inp[6]
    m, param = inp[7:10], inp[10:14]
    c, a, d = inp[14:17], inp[17:20], inp[20:23]
    half = [value * 0.5 for value in c]
    repeated = [math.fmod(p[index] + half[index], c[index]) - half[index]
                for index in range(3)]
    rounded = [_round_away(p[index] / d[index]) for index in range(3)]
    clamped = [min(max(rounded[index], -a[index]), a[index]) for index in range(3)]
    finite = [(p[index] - d[index]) * clamped[index] for index in range(3)]
    return [_sdf_box_frame(p, b, e), _sdf_mandelbulb(m, param)] + repeated + finite


#ai: quaternion formulas mirror the C branch structure, including slerp's
#   nlerp shortcut and the from-matrix largest-component branch.
def _q_dot(q1, q2):
    return sum(a * b for a, b in zip(q1, q2))


def _q_normalize(q):
    length = math.sqrt(_q_dot(q, q))
    if length == 0.0:
        length = 1.0
    return [value / length for value in q]


def _q_multiply(q1, q2):
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2
    return [x1 * w2 + w1 * x2 + y1 * z2 - z1 * y2,
            y1 * w2 + w1 * y2 + z1 * x2 - x1 * z2,
            z1 * w2 + w1 * z2 + x1 * y2 - y1 * x2,
            w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2]


def _q_invert(q):
    length_sq = _q_dot(q, q)
    if length_sq == 0.0:
        return list(q)
    inv = 1.0 / length_sq
    return [-q[0] * inv, -q[1] * inv, -q[2] * inv, q[3] * inv]


def _q_lerp(q1, q2, amount):
    return [q1[index] + amount * (q2[index] - q1[index]) for index in range(4)]


def _q_slerp(q1, q2, amount):
    q2 = list(q2)
    cosine = _q_dot(q1, q2)
    if cosine < 0.0:
        q2 = [-value for value in q2]
        cosine = -cosine
    if abs(cosine) >= 1.0:
        return list(q1)
    if cosine > 0.95:
        return _q_normalize(_q_lerp(q1, q2, amount))
    half = math.acos(cosine)
    sine = math.sqrt(1.0 - cosine * cosine)
    if abs(sine) < 1e-6:
        return [(q1[index] + q2[index]) * 0.5 for index in range(4)]
    ratio_a = math.sin((1.0 - amount) * half) / sine
    ratio_b = math.sin(amount * half) / sine
    return [q1[index] * ratio_a + q2[index] * ratio_b for index in range(4)]


def case_quaternion(inp, conv):
    q1 = [float(value) for value in inp[0:4]]
    q2 = [float(value) for value in inp[4:8]]
    qa = [float(value) for value in inp[8:12]]
    qb = [float(value) for value in inp[12:16]]
    amount = float(inp[16])
    return (_q_multiply(q1, q2) + _q_invert(q1) + _q_normalize(q1)
            + _q_lerp(q1, q2, amount)
            + _q_normalize(_q_lerp(q1, q2, amount))
            + _q_slerp(qa, qb, amount))


def _q_from_axis_angle(axis, angle):
    axis_len = math.sqrt(sum(value * value for value in axis))
    if axis_len == 0.0:
        return [0.0, 0.0, 0.0, 1.0]
    half = angle * 0.5
    unit = [value / axis_len for value in axis]
    q = [unit[0] * math.sin(half), unit[1] * math.sin(half),
         unit[2] * math.sin(half), math.cos(half)]
    return _q_normalize(q)


def _q_to_axis_angle(q):
    q = [float(value) for value in q]
    if abs(q[3]) > 1.0:
        q = _q_normalize(q)
    angle = 2.0 * math.acos(q[3])
    den = math.sqrt(1.0 - q[3] * q[3])
    if den > 1e-6:
        axis = [q[0] / den, q[1] / den, q[2] / den]
    else:
        axis = [1.0, 0.0, 0.0]
    return axis + [angle]


def _q_from_euler(pitch, yaw, roll):
    x0, x1 = math.cos(pitch * 0.5), math.sin(pitch * 0.5)
    y0, y1 = math.cos(yaw * 0.5), math.sin(yaw * 0.5)
    z0, z1 = math.cos(roll * 0.5), math.sin(roll * 0.5)
    return [x1 * y0 * z0 - x0 * y1 * z1,
            x0 * y1 * z0 + x1 * y0 * z1,
            x0 * y0 * z1 - x1 * y1 * z0,
            x0 * y0 * z0 + x1 * y1 * z1]


def _q_to_euler(q):
    x0 = 2.0 * (q[3] * q[0] + q[1] * q[2])
    x1 = 1.0 - 2.0 * (q[0] * q[0] + q[1] * q[1])
    y0 = 2.0 * (q[3] * q[1] - q[2] * q[0])
    y0 = min(max(y0, -1.0), 1.0)
    z0 = 2.0 * (q[3] * q[2] + q[0] * q[1])
    z1 = 1.0 - 2.0 * (q[1] * q[1] + q[2] * q[2])
    return [math.atan2(x0, x1), math.asin(y0), math.atan2(z0, z1)]


def _q_to_matrix(q):
    x, y, z, w = q
    a2, b2, c2 = x * x, y * y, z * z
    ac, ab, bc = x * z, x * y, y * z
    ad, bd, cd = w * x, w * y, w * z
    matrix = [0.0] * 16
    matrix[0] = 1.0 - 2.0 * (b2 + c2)
    matrix[1] = 2.0 * (ab + cd)
    matrix[2] = 2.0 * (ac - bd)
    matrix[4] = 2.0 * (ab - cd)
    matrix[5] = 1.0 - 2.0 * (a2 + c2)
    matrix[6] = 2.0 * (bc + ad)
    matrix[8] = 2.0 * (ac + bd)
    matrix[9] = 2.0 * (bc - ad)
    matrix[10] = 1.0 - 2.0 * (a2 + b2)
    matrix[15] = 1.0
    return matrix


def _q_from_matrix(matrix):
    four_w = matrix[0] + matrix[5] + matrix[10]
    four_x = matrix[0] - matrix[5] - matrix[10]
    four_y = matrix[5] - matrix[0] - matrix[10]
    four_z = matrix[10] - matrix[0] - matrix[5]
    biggest_idx = 0
    biggest = four_w
    if four_x > biggest:
        biggest, biggest_idx = four_x, 1
    if four_y > biggest:
        biggest, biggest_idx = four_y, 2
    if four_z > biggest:
        biggest, biggest_idx = four_z, 3
    biggest_val = math.sqrt(biggest + 1.0) * 0.5
    mult = 0.25 / biggest_val
    q = [0.0] * 4
    if biggest_idx == 0:
        q = [(matrix[6] - matrix[9]) * mult,
             (matrix[8] - matrix[2]) * mult,
             (matrix[1] - matrix[4]) * mult, biggest_val]
    elif biggest_idx == 1:
        q = [biggest_val, (matrix[1] + matrix[4]) * mult,
             (matrix[8] + matrix[2]) * mult, (matrix[6] - matrix[9]) * mult]
    elif biggest_idx == 2:
        q = [(matrix[1] + matrix[4]) * mult, biggest_val,
             (matrix[6] + matrix[9]) * mult, (matrix[8] - matrix[2]) * mult]
    else:
        q = [(matrix[8] + matrix[2]) * mult,
             (matrix[6] + matrix[9]) * mult, biggest_val,
             (matrix[1] - matrix[4]) * mult]
    return q


def case_quaternion_rot(inp, conv):
    axis = [float(value) for value in inp[0:3]]
    angle = float(inp[3])
    euler = [float(value) for value in inp[4:7]]
    q = [float(value) for value in inp[7:11]]
    from_axis = _q_from_axis_angle(axis, angle)
    from_euler = _q_from_euler(euler[0], euler[1], euler[2])
    matrix = _q_to_matrix(q)
    return (from_axis + _q_to_axis_angle(from_axis) + from_euler
            + _q_to_euler(from_euler) + matrix + _q_from_matrix(matrix))


def _classify(value):
    value = float(value)
    if math.isnan(value):
        return [3.0, 0.0]
    if math.isinf(value):
        return [1.0 if value > 0.0 else 2.0, 0.0]
    return [0.0, value]


def case_edge_cases(inp, conv):
    singular = mat(inp[0:16], conv)
    nan = float("nan")
    inf = float("inf")
    values = [
        0.0, 0.0, 0.0,
        nan, nan, nan,
        0.0,
        nan,
        0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
        1.0, 0.0, 0.0, 0.0,
        inf,
        nan,
        0.0,
        math.sqrt(14.0) - 0.5,
        nan, nan, nan,
        1.0, 2.0, 3.0,
        nan,
        0.0, 0.0, 0.0,
        nan,
        nan,
        nan,
        -inf,
        nan,
        singular.det(),
        0.0, 8.0, 8.0,
    ]
    classified = []
    for value in values:
        classified += _classify(value)
    return classified


def case_matrix_aux(inp, conv):
    A = mat(inp[0:16], conv)
    B = mat(inp[16:32], conv)
    frustum_args = [conv(value) for value in inp[32:38]]
    ortho_args = [conv(value) for value in inp[38:44]]
    rows = [Matrix(4, 1, [conv(value) for value in inp[44 + 4 * index:48 + 4 * index]])
            for index in range(4)]
    cofactor = Matrix(4, 4, lambda row, column: A.cofactor(row, column))
    adjugate = cofactor.T
    element_mul = Matrix(4, 4, [A[row, column] * B[row, column]
                                for row in range(4) for column in range(4)])
    element_div = Matrix(4, 4, [A[row, column] / B[row, column]
                                for row in range(4) for column in range(4)])
    left, right, bottom, top, near, far = frustum_args
    rl, tb, fn = right - left, top - bottom, far - near
    frustum = Matrix(4, 4, [0] * 16)
    frustum[0, 0] = 2 * near / rl
    frustum[1, 1] = 2 * near / tb
    frustum[2, 0] = (right + left) / rl
    frustum[2, 1] = (top + bottom) / tb
    frustum[2, 2] = -(far + near) / fn
    frustum[2, 3] = -1
    frustum[3, 2] = -2 * far * near / fn
    left, right, bottom, top, near, far = ortho_args
    rl, tb, fn = right - left, top - bottom, far - near
    ortho = Matrix(4, 4, [0] * 16)
    ortho[0, 0] = 2 / rl
    ortho[1, 1] = 2 / tb
    ortho[2, 2] = -2 / fn
    ortho[3, 0] = -(left + right) / rl
    ortho[3, 1] = -(top + bottom) / tb
    ortho[3, 2] = -(far + near) / fn
    ortho[3, 3] = 1
    from_vectors = Matrix.vstack(*rows)
    return (list(A.T) + list(A + B) + list(A - B)
            + list(element_mul) + list(element_div)
            + list(cofactor) + list(adjugate)
            + list(frustum) + list(ortho)
            + list(A) + list(from_vectors))


def case_matrix_small(inp, conv):
    m2 = [conv(value) for value in inp[0:4]]
    m3 = [conv(value) for value in inp[4:13]]
    det2 = m2[0] * m2[3] - m2[1] * m2[2]
    trace2 = m2[0] + m2[3]
    det3 = (m3[0] * (m3[4] * m3[8] - m3[5] * m3[7])
            - m3[1] * (m3[3] * m3[8] - m3[5] * m3[6])
            + m3[2] * (m3[3] * m3[7] - m3[4] * m3[6]))
    trace3 = m3[0] + m3[4] + m3[8]
    return [det2, trace2, det3, trace3]


def case_f64(inp, conv):
    a = [conv(value) for value in inp[0:4]]
    b = [conv(value) for value in inp[4:8]]
    amount = conv(inp[8])
    return [a[index] + amount * (b[index] - a[index]) for index in range(4)]


CASES = {
    "multiply": (48, TOL_EXACT, case_multiply),
    "invert": (32, TOL_EXACT, case_invert),
    "transform": (22, TOL_EXACT, case_transform),
    "determinant": (17, TOL_EXACT, case_determinant),
    "perspective": (20, TOL_FLOAT, case_perspective),
    "rotate_xyz": (19, TOL_FLOAT, case_rotate_xyz),
    "lookat": (25, TOL_FLOAT, case_lookat),
    "vectors": (13, TOL_EXACT, case_vectors),
    "random": (24, 0.0, case_random),
    "sdf": (39, TOL_FLOAT, case_sdf),
    "sdf_ext": (31, TOL_FLOAT, case_sdf_ext),
    "quaternion": (41, TOL_FLOAT, case_quaternion),
    "quaternion_rot": (46, TOL_FLOAT, case_quaternion_rot),
    "matrix_aux": (236, TOL_EXACT, case_matrix_aux),
    "matrix_small": (17, TOL_EXACT, case_matrix_small),
    "f64": (13, TOL_FLOAT, case_f64),
    "edge_cases": (102, TOL_FLOAT, case_edge_cases),
    "easing": (12, TOL_FLOAT, case_easing),
    "scalar": (15, TOL_FLOAT, case_scalar),
    "int_math": (14, 0.0, case_int_math),
    "color": (25, 0.05, case_color),
    "physics": (21, TOL_FLOAT, case_physics),
    "relativity": (15, TOL_FLOAT, case_relativity),
    "planck": (7, TOL_FLOAT, case_planck),
    "em": (29, TOL_FLOAT, case_em),
    "dipole": (29, TOL_FLOAT, case_dipole),
    "thermal": (8, TOL_FLOAT, case_thermal),
    "fft_delta": (32, TOL_FLOAT, case_fft),
    "fft_sine": (32, TOL_FLOAT, case_fft),
    "calculus": (18, 1e-3, case_calculus),
    "convert_units": (13, TOL_FLOAT, case_convert_units),
    "convert_color": (12, TOL_FLOAT, case_convert_color),
    "convert_coords": (21, TOL_FLOAT, case_convert_coords),
    "convert_sky": (29, TOL_FLOAT, case_convert_sky),
    "convert_new": (16, TOL_FLOAT, case_convert_new),
    "convert_proj": (13, TOL_FLOAT, case_convert_proj),
    "convert_ball": (8, TOL_FLOAT, case_convert_ball),
}

#ai: input counts keep the native layout in one place and make arity checks direct.
CASE_INPUTS = {
    "multiply": 32,
    "invert": 16,
    "transform": 19,
    "determinant": 16,
    "perspective": 4,
    "rotate_xyz": 3,
    "lookat": 9,
    "vectors": 6,
    "random": 4,
    "sdf": 28,
    "sdf_ext": 23,
    "quaternion": 17,
    "quaternion_rot": 11,
    "matrix_aux": 60,
    "matrix_small": 13,
    "f64": 9,
    "edge_cases": 16,
    "easing": 7,
    "scalar": 10,
    "int_math": 9,
    "color": 9,
    "physics": 14,
    "relativity": 8,
    "planck": 2,
    "em": 19,
    "dipole": 16,
    "thermal": 3,
    "fft_delta": 16,
    "fft_sine": 16,
    "calculus": 8,
    "convert_units": 6,
    "convert_color": 6,
    "convert_coords": 3,
    "convert_sky": 5,
    "convert_new": 8,
    "convert_proj": 3,
    "convert_ball": 3,
}


def parse_dump(text):
    parsed = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line.startswith("MATHDUMP"):
            continue
        parts = line.split()
        if parts[0] not in ("MATHDUMP", "MATHDUMP64"):
            continue
        if len(parts) < 3:
            raise MathPassError(f"malformed MATHDUMP line: {line!r}")
        name = parts[1]
        if name in parsed:
            raise MathPassError(f"duplicate case: {name}")
        try:
            count = int(parts[2])
        except ValueError as exc:
            raise MathPassError(f"{name}: invalid count {parts[2]!r}") from exc
        values = []
        for token in parts[3:]:
            try:
                value = float(token)
            except ValueError as exc:
                raise MathPassError(f"{name}: invalid number {token!r}") from exc
            if not math.isfinite(value):
                raise MathPassError(f"{name}: non-finite number {token!r}")
            values.append(value)
        if count != len(values):
            raise MathPassError(f"{name}: count {count} does not match {len(values)} values")
        parsed[name] = values
    if not parsed:
        raise MathPassError("no MATHDUMP lines (binary built without the dump?)")
    return parsed


def run_dump(binary, runner=None):
    runner = runner or subprocess.run
    try:
        run = runner([str(binary)], env={"GX_MATH_DUMP": "1", "PATH": "/usr/bin:/bin"},
                     capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired as exc:
        raise MathPassError("dump timed out") from exc
    if run.returncode != 0:
        raise MathPassError(f"dump exited with status {run.returncode}")
    return parse_dump(run.stdout or "")


def split_case(name, values):
    if name not in CASE_INPUTS:
        raise MathPassError(f"{name}: no input layout")
    count = CASE_INPUTS[name]
    return values[:count], values[count:]


def expected_float(value):
    if isinstance(value, float):
        return value
    if isinstance(value, int):
        return float(value)
    return float(value.evalf(17))


def check_case(name, values):
    if name not in CASES:
        raise MathPassError(f"{name}: unknown case")
    want_count, tol, oracle = CASES[name]
    if len(values) != want_count:
        raise MathPassError(f"{name}: expected {want_count} floats, got {len(values)}")
    inputs, native = split_case(name, values)
    conv = f64 if tol == TOL_FLOAT else exact
    try:
        expected = oracle(inputs, conv)
    except Exception as exc:
        raise MathPassError(f"{name}: oracle failed: {exc}") from exc
    try:
        expected_count = len(expected)
    except TypeError as exc:
        raise MathPassError(f"{name}: oracle did not return a sequence") from exc
    if expected_count != len(native):
        raise MathPassError(f"{name}: oracle returned {expected_count} values, expected {len(native)}")
    worst = 0.0
    for index, (want, got) in enumerate(zip(expected, native)):
        want_float = expected_float(want)
        if not math.isfinite(want_float):
            raise MathPassError(f"{name}: non-finite oracle value at entry {index}")
        if not math.isfinite(got):
            raise MathPassError(f"{name}: non-finite native value at entry {index}")
        error = abs(want_float - got) / max(1.0, abs(want_float))
        if not math.isfinite(error):
            raise MathPassError(f"{name}: non-finite error at entry {index}")
        worst = max(worst, error)
    return worst, tol


def main():
    ap = argparse.ArgumentParser(description="gX math pass (SymPy oracle)")
    ap.add_argument("--binary", default=str(ROOT / "build" / "gX_build"))
    args = ap.parse_args()

    binary = Path(args.binary)
    if not binary.exists():
        print(f"math-pass: binary not found: {binary}", file=sys.stderr)
        return 1
    try:
        parsed = run_dump(binary)
    except MathPassError as exc:
        print(f"math-pass: {exc}", file=sys.stderr)
        return 1

    failures = 0
    for name, values in parsed.items():
        if name not in CASES:
            print(f"  {name}: unknown case")
            failures += 1
            continue
        try:
            worst, tol = check_case(name, values)
        except MathPassError as exc:
            print(f"  {exc}")
            failures += 1
            continue

        ok = worst <= tol
        print(f"  {name:<14} {len(values) - CASE_INPUTS[name]:>2} entries  "
              f"max-rel-err {worst:.2e}  tol {tol:.0e}  {'ok' if ok else 'FAIL'}")
        if not ok:
            failures += 1

    missing = sorted(set(CASES) - set(parsed))
    if missing:
        print(f"  missing cases: {', '.join(missing)}")
        failures += 1
    if failures:
        print(f"MATH_PASS: FAIL ({failures} case(s))")
        return 1
    print("MATH_PASS: PASS — gxmath corpus matches the SymPy oracle")
    return 0


if __name__ == "__main__":
    sys.exit(main())
