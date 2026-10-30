/**********************************************************************************************
*
*   gxmath/convert.h
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

//opencode: Ported coordinate conversion functions from gamemath to gxmath

#ifndef GXMATH_CONVERT_H
#define GXMATH_CONVERT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

gX_F32 gX_F32_Convert_MeterPrefix(gX_F32 meters, gX_I32 prefix);
gX_F32 gX_F32_Convert_Foot2Meter(gX_F32 foots);
gX_F32 gX_F32_Convert_Inch2Meter(gX_F32 inches);
gX_F32 gX_F32_Convert_Meter2Foot(gX_F32 meters);
gX_F32 gX_F32_Convert_Meter2Inch(gX_F32 meters);
gX_F32 gX_F32_Convert_Kelvin2Celsius(gX_F32 kelvin);
gX_F32 gX_F32_Convert_Celsius2Kelvin(gX_F32 celsius);

gX_F32v3 gX_F32v3_Convert_RGB2HSV(gX_F32v3 rgb);
gX_F32v3 gX_F32v3_Convert_HSV2RGB(gX_F32v3 hsv);

gX_F32v3 gX_F32_Convert_cartesian2cylinder(gX_F32v3 p);
gX_F32v3 gX_F32_Convert_cartesian2spherical(gX_F32v3 p);
gX_F32v3 gX_F32_Convert_cylinder2cartesian(gX_F32v3 c);
gX_F32v3 gX_F32_Convert_cylinder2spherical(gX_F32v3 c);
gX_F32v3 gX_F32_Convert_spherical2cartesian(gX_F32v3 s);
gX_F32v3 gX_F32_Convert_spherical2cylinder(gX_F32v3 s);

gX_F32v2 gX_F32v2_Convert_Vector2Horizontal(gX_F32v3 p);
gX_F32v3 gX_F32v3_Convert_Horizontal2Vector(gX_F32v2 p);
gX_F32v2 gX_F32v2_Convert_Vector2Equatorial(gX_F32v3 p);
gX_F32v3 gX_F32v3_Convert_Equatorial2Vector(gX_F32v2 p);
gX_F32v3 gX_F32v3_Convert_Equatorial2Ecliptic(gX_F32v3 p);
gX_F32v3 gX_F32v3_Convert_Ecliptic2Equatorial(gX_F32v3 p);
//opencode: equatorial <-> horizontal for an observer at (latitude,
//   longitude) in RADIANS; the pair is a rotation and its own inverse.
gX_F32v3 gX_F32v3_Convert_Equatorial2Horizontal(gX_F32v3 p, gX_F32 latitude, gX_F32 longitude);
gX_F32v3 gX_F32v3_Convert_Horizontal2Equatorial(gX_F32v3 p, gX_F32 latitude, gX_F32 longitude);

//opencode: the unit dome projection: bearing from +y (north) toward +x
//   (east) and altitude, in RADIANS (same frame as Vector2Horizontal).
gX_F32v2 gX_F32_Convert_space2ball(gX_F32v3 s);
gX_F32v3 gX_F32_Convert_ball2space(gX_F32v2 b);

//opencode: exact IEC 61966-2-1 sRGB transfer — for CPU image I/O and tooling
//   (the renderer's shaders keep their pow(1/2.2) approximation).
gX_F32 gX_F32_Convert_Linear2SRGB(gX_F32 linear);
gX_F32 gX_F32_Convert_SRGB2Linear(gX_F32 srgb);

//opencode: amplitude (voltage) decibels — 20*log10, the audio convention.
//   Amplitude 0 maps to -INFINITY (the log limit, not an error).
gX_F32 gX_F32_Convert_Amplitude2Decibel(gX_F32 amplitude);
gX_F32 gX_F32_Convert_Decibel2Amplitude(gX_F32 decibel);

//opencode: MIDI note <-> frequency, A4 (note 69) = 440 Hz. Non-positive
//   frequencies map to NaN.
gX_F32 gX_F32_Convert_MidiNote2Frequency(gX_F32 note);
gX_F32 gX_F32_Convert_Frequency2MidiNote(gX_F32 frequency);

//opencode: Celsius <-> Fahrenheit
gX_F32 gX_F32_Convert_Celsius2Fahrenheit(gX_F32 celsius);
gX_F32 gX_F32_Convert_Fahrenheit2Celsius(gX_F32 fahrenheit);

#ifdef __cplusplus
}
#endif

#endif //GXMATH_CONVERT_H
