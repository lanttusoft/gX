/**********************************************************************************************
*
*   gxmath/convert.c
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

//opencode: ported convert functions from gamemath to gxmath

#include <math.h>

#include "convert.h"
#include "constants.h"
#include "vector3.h"

//opencode: gX_F32_Convert_MeterPrefix — full SI prefix range
gX_F32 gX_F32_Convert_MeterPrefix(gX_F32 meters, gX_I32 prefix) {
    switch (prefix) {
        case  3: return meters / 1.0e3f;      // kilo
        case  2: return meters / 1.0e2f;      // hecto
        case  1: return meters / 1.0e1f;      // deca
        case -1: return meters / 1.0e-1f;     // deci
        case -2: return meters / 1.0e-2f;     // centi
        case -3: return meters / 1.0e-3f;     // milli
        case -6: return meters / 1.0e-6f;     // micro
        case -9: return meters / 1.0e-9f;     // nano
        default: return meters;
    }
}

//opencode: gX_F32_Convert_Foot2Meter
gX_F32 gX_F32_Convert_Foot2Meter(gX_F32 foots) {
    return 0.3048f * foots;
}

//opencode: gX_F32_Convert_Inch2Meter
gX_F32 gX_F32_Convert_Inch2Meter(gX_F32 inches) {
    return 0.0254f * inches;
}

//opencode: gX_F32_Convert_Meter2Foot
gX_F32 gX_F32_Convert_Meter2Foot(gX_F32 meters) {
    return meters / 0.3048f;
}

//opencode: gX_F32_Convert_Meter2Inch
gX_F32 gX_F32_Convert_Meter2Inch(gX_F32 meters) {
    return meters / 0.0254f;
}

//opencode: gX_F32_Convert_Kelvin2Celsius
gX_F32 gX_F32_Convert_Kelvin2Celsius(gX_F32 kelvin) {
    return kelvin - 273.15f;
}

//opencode: gX_F32_Convert_Celsius2Kelvin
gX_F32 gX_F32_Convert_Celsius2Kelvin(gX_F32 celsius) {
    return celsius + 273.15f;
}

//opencode: gX_F32v3_Convert_RGB2HSV
gX_F32v3 gX_F32v3_Convert_RGB2HSV(gX_F32v3 rgb) {
    gX_F32v3 hsv = { 0.0f, 0.0f, 0.0f };
    gX_F32 mx = fmaxf(rgb.x, fmaxf(rgb.y, rgb.z));
    gX_F32 mn = fminf(rgb.x, fminf(rgb.y, rgb.z));
    gX_F32 d = mx - mn;
    hsv.z = mx;
    if (mx > 0.0f) hsv.y = d / mx;
    if (d > 0.0f) {
        if (mx == rgb.x) hsv.x = 60.0f * fmodf((rgb.y - rgb.z) / d, 6.0f);
        else if (mx == rgb.y) hsv.x = 60.0f * ((rgb.z - rgb.x) / d + 2.0f);
        else hsv.x = 60.0f * ((rgb.x - rgb.y) / d + 4.0f);
        if (hsv.x < 0.0f) hsv.x += 360.0f;
    }
    return hsv;
}

//opencode: gX_F32v3_Convert_HSV2RGB
gX_F32v3 gX_F32v3_Convert_HSV2RGB(gX_F32v3 hsv) {
    gX_F32v3 rgb = { 0.0f, 0.0f, 0.0f };
    gX_F32 h = hsv.x / 60.0f;
    gX_I32 hi = (gX_I32)h % 6;
    if (hi < 0) hi += 6;
    gX_F32 f = h - (gX_F32)hi;
    gX_F32 p = hsv.z * (1.0f - hsv.y);
    gX_F32 q = hsv.z * (1.0f - hsv.y * f);
    gX_F32 t = hsv.z * (1.0f - hsv.y * (1.0f - f));
    switch (hi) {
        case 0: rgb = (gX_F32v3){hsv.z, t, p}; break;
        case 1: rgb = (gX_F32v3){q, hsv.z, p}; break;
        case 2: rgb = (gX_F32v3){p, hsv.z, t}; break;
        case 3: rgb = (gX_F32v3){p, q, hsv.z}; break;
        case 4: rgb = (gX_F32v3){t, p, hsv.z}; break;
        default: rgb = (gX_F32v3){hsv.z, p, q}; break;
    }
    return rgb;
}

//opencode: gX_F32_Convert_cartesian2cylinder
gX_F32v3 gX_F32_Convert_cartesian2cylinder(gX_F32v3 p) {
    gX_F32v3 result;
    result.x = sqrtf(p.x * p.x + p.y * p.y);
    result.y = atan2f(p.y, p.x);
    result.z = p.z;
    return result;
}

//opencode: gX_F32_Convert_cartesian2spherical
gX_F32v3 gX_F32_Convert_cartesian2spherical(gX_F32v3 p) {
    gX_F32v3 result;
    result.x = sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
    result.y = atan2f(p.y, p.x);
    result.z = acosf(p.z / result.x);
    return result;
}

//opencode: gX_F32_Convert_cylinder2cartesian
gX_F32v3 gX_F32_Convert_cylinder2cartesian(gX_F32v3 c) {
    gX_F32v3 result;
    result.x = c.x * cosf(c.y);
    result.y = c.x * sinf(c.y);
    result.z = c.z;
    return result;
}

//opencode: gX_F32_Convert_cylinder2spherical
gX_F32v3 gX_F32_Convert_cylinder2spherical(gX_F32v3 c) {
    gX_F32v3 result;
    result.x = sqrtf(c.x * c.x + c.z * c.z);
    result.y = c.y;
    result.z = acosf(c.z / result.x);
    return result;
}

//opencode: gX_F32_Convert_spherical2cartesian
gX_F32v3 gX_F32_Convert_spherical2cartesian(gX_F32v3 s) {
    gX_F32v3 result;
    result.x = s.x * sinf(s.z) * cosf(s.y);
    result.y = s.x * sinf(s.z) * sinf(s.y);
    result.z = s.x * cosf(s.z);
    return result;
}

//opencode: gX_F32_Convert_spherical2cylinder
gX_F32v3 gX_F32_Convert_spherical2cylinder(gX_F32v3 s) {
    gX_F32v3 result;
    result.x = s.x * sinf(s.z);
    result.y = s.y;
    result.z = s.x * cosf(s.z);
    return result;
}

//opencode: gX_F32v2_Convert_Vector2Horizontal — the local sky frame: the
//   bearing from +y (north) toward +x (east) and the altitude, both in
//   radians. The old form returned degrees against a radians inverse and
//   used acos (losing the bearing sign), so the pair never round-tripped.
gX_F32v2 gX_F32v2_Convert_Vector2Horizontal(gX_F32v3 p) {
    gX_F32v2 result = { 0 };
    result.y = asinf(p.z);
    result.x = atan2f(p.x, p.y);
    return result;
}

//opencode: gX_F32v3_Convert_Horizontal2Vector — the inverse of
//   Vector2Horizontal (bearing from +y toward +x, altitude; radians).
gX_F32v3 gX_F32v3_Convert_Horizontal2Vector(gX_F32v2 p) {
    gX_F32v3 result;
    result.x = sinf(p.x) * cosf(p.y);
    result.y = cosf(p.x) * cosf(p.y);
    result.z = sinf(p.y);
    return result;
}

//opencode: gX_F32v2_Convert_Vector2Equatorial — the azimuth from +x toward
//   +y in radians. atan2 (not acos) keeps the sign, so the pair round-trips
//   for negative y (the math pass pins it).
gX_F32v2 gX_F32v2_Convert_Vector2Equatorial(gX_F32v3 p) {
    gX_F32v2 result = { 0 };
    result.y = asinf(p.z);
    result.x = atan2f(p.y, p.x);
    return result;
}

//opencode: gX_F32v3_Convert_Equatorial2Vector
gX_F32v3 gX_F32v3_Convert_Equatorial2Vector(gX_F32v2 p) {
    gX_F32v3 result;
    result.x = cosf(p.y) * cosf(p.x);
    result.y = cosf(p.y) * sinf(p.x);
    result.z = sinf(p.y);
    return result;
}

//opencode: gX_F32v3_Convert_Equatorial2Ecliptic
gX_F32v3 gX_F32v3_Convert_Equatorial2Ecliptic(gX_F32v3 p) {
    gX_F32 e = 23.4f * gX_DEG2RAD;
    gX_F32 ce = cosf(e);
    gX_F32 se = sinf(e);
    gX_F32v3 result;
    result.x = p.x;
    result.y = p.y * ce + p.z * se;
    result.z = p.z * ce - p.y * se;
    return result;
}

//opencode: gX_F32v3_Convert_Ecliptic2Equatorial
gX_F32v3 gX_F32v3_Convert_Ecliptic2Equatorial(gX_F32v3 p) {
    gX_F32 e = 23.4f * gX_DEG2RAD;
    gX_F32 ce = cosf(e);
    gX_F32 se = sinf(e);
    gX_F32v3 result;
    result.x = p.x;
    result.y = p.y * ce - p.z * se;
    result.z = p.z * ce + p.y * se;
    return result;
}

//opencode: gX_F32v3_Convert_Equatorial2Horizontal — the observer position is
//   a parameter (latitude/longitude in radians); the old 45N/0E hardcoding
//   made the pair scene-specific. The sum/difference trig form collapses to
//   the direct rotation below (2*sin(lat)cos(lon) etc.), verified by the
//   math pass round-trip pin.
gX_F32v3 gX_F32v3_Convert_Equatorial2Horizontal(gX_F32v3 p, gX_F32 latitude, gX_F32 longitude) {
    gX_F32 s_lat = sinf(latitude);
    gX_F32 c_lat = cosf(latitude);
    gX_F32 s_lon = sinf(longitude);
    gX_F32 c_lon = cosf(longitude);
    gX_F32v3 result;
    result.x = p.x * s_lat * c_lon + p.y * s_lat * s_lon - p.z * c_lat;
    result.y = p.x * s_lon - p.y * c_lon;
    result.z = p.x * c_lat * c_lon + p.y * c_lat * s_lon + p.z * s_lat;
    return result;
}

//opencode: gX_F32v3_Convert_Horizontal2Equatorial — the inverse rotation of
//   Equatorial2Horizontal (round-trip pinned by the math pass).
gX_F32v3 gX_F32v3_Convert_Horizontal2Equatorial(gX_F32v3 p, gX_F32 latitude, gX_F32 longitude) {
    gX_F32 s_lat = sinf(latitude);
    gX_F32 c_lat = cosf(latitude);
    gX_F32 s_lon = sinf(longitude);
    gX_F32 c_lon = cosf(longitude);
    gX_F32v3 result;
    result.x = p.x * s_lat * c_lon + p.z * c_lat * c_lon + p.y * s_lon;
    result.y = p.x * s_lat * s_lon + p.z * c_lat * s_lon - p.y * c_lon;
    result.z = p.z * s_lat - p.x * c_lat;
    return result;
}

//opencode: gX_F32_Convert_space2ball
//opencode: gX_F32_Convert_Linear2SRGB / SRGB2Linear
gX_F32 gX_F32_Convert_Linear2SRGB(gX_F32 linear) {
    if (linear <= 0.0031308f) return 12.92f * linear;
    return 1.055f * powf(linear, 1.0f / 2.4f) - 0.055f;
}

gX_F32 gX_F32_Convert_SRGB2Linear(gX_F32 srgb) {
    if (srgb <= 0.04045f) return srgb / 12.92f;
    return powf((srgb + 0.055f) / 1.055f, 2.4f);
}

//opencode: gX_F32_Convert_Amplitude2Decibel / Decibel2Amplitude
gX_F32 gX_F32_Convert_Amplitude2Decibel(gX_F32 amplitude) {
    if (amplitude <= 0.0f) return -INFINITY;
    return 20.0f * log10f(amplitude);
}

gX_F32 gX_F32_Convert_Decibel2Amplitude(gX_F32 decibel) {
    return powf(10.0f, decibel / 20.0f);
}

//opencode: gX_F32_Convert_MidiNote2Frequency / Frequency2MidiNote
gX_F32 gX_F32_Convert_MidiNote2Frequency(gX_F32 note) {
    return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
}

gX_F32 gX_F32_Convert_Frequency2MidiNote(gX_F32 frequency) {
    if (frequency <= 0.0f) return NAN;
    return 69.0f + 12.0f * log2f(frequency / 440.0f);
}

//opencode: gX_F32_Convert_Celsius2Fahrenheit / Fahrenheit2Celsius
gX_F32 gX_F32_Convert_Celsius2Fahrenheit(gX_F32 celsius) {
    return celsius * 9.0f / 5.0f + 32.0f;
}

gX_F32 gX_F32_Convert_Fahrenheit2Celsius(gX_F32 fahrenheit) {
    return (fahrenheit - 32.0f) * 5.0f / 9.0f;
}

//opencode: gX_F32_Convert_space2ball — the unit dome projection: bearing
//   from +y (north) toward +x (east) and altitude, both in radians (the same
//   frame as Vector2Horizontal). The old form mixed degrees with a raw z
//   component, so ball2space never reproduced its input.
gX_F32v2 gX_F32_Convert_space2ball(gX_F32v3 s) {
    gX_F32v2 result = { 0 };
    result.x = atan2f(s.x, s.y);
    result.y = asinf(s.z);
    return result;
}

//opencode: gX_F32_Convert_ball2space — the inverse of space2ball.
gX_F32v3 gX_F32_Convert_ball2space(gX_F32v2 b) {
    gX_F32v3 result = { 0 };
    result.x = sinf(b.x) * cosf(b.y);
    result.y = cosf(b.x) * cosf(b.y);
    result.z = sinf(b.y);
    return result;
}
