/**********************************************************************************************
*
*   gxmath/scalar.c
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

//lanttu: original gxmath module based on raymath v1.5

#include <math.h>       // Required for: floor(), floorf(), fmax(), fmaxf(), fabs(), fabsf()

#include "scalar.h"
#include "macros.h"

// Clamp value
// Clamp(value, min, max) -> result
// Original: float Clamp(float value, float min, float max) from raymath.h
// Original: Copyright (c) 2015-2023 Ramon Santamaria
// Relabeled as gX_F32 gX_F32_Clamp(gX_F32 value, gX_F32 min, gX_F32 max)
// All derivative functions are created from original
// Derivatives: Copyright (c) 2025 lanttu
gX_F32 gX_F32_Clamp(gX_F32 value, gX_F32 min, gX_F32 max) {
    gX_F32 result = (value < min)? min : value;
    if (result > max) result = max;
    return result;
}

// Calculate linear interpolation between two values
// Lerp(start, end, amount) -> result
// Original: float Lerp(float start, float end, float amount) from raymath.h
// Original: Copyright (c) 2015-2023 Ramon Santamaria
// Relabeled as gX_F32 gX_F32_Lerp(gX_F32 start, gX_F32 end, gX_F32 amount)
// All derivative functions are created from original
// Derivatives: Copyright (c) 2025 lanttu
gX_F32 gX_F32_Lerp(gX_F32 start, gX_F32 end, gX_F32 amount) {
    gX_F32 result = start + amount*(end - start);
    return result;
}

// Normalize input value within input range
// Normalize(value, start, end) -> result
// Original: float Normalize(float value, float start, float end) from raymath.h
// Original: Copyright (c) 2015-2023 Ramon Santamaria
// Relabeled as gX_F32 gX_F32_Normalize(gX_F32 value, gX_F32 start, gX_F32 end)
// All derivative functions are created from original
// Derivatives: Copyright (c) 2025 lanttu
gX_F32 gX_F32_Normalize(gX_F32 value, gX_F32 start, gX_F32 end) {
    gX_F32 result = (value - start)/(end - start);
    return result;
}

// Remap input value within input range to output range
// Remap(value, inputStart, inputEnd, outputStart, outputEnd) -> result
// Original: float Remap(float value, float inputStart, float inputEnd, float outputStart, float outputEnd) from raymath.h
// Original: Copyright (c) 2015-2023 Ramon Santamaria
// Relabeled as gX_F32 gX_F32_Remap(gX_F32 value, gX_F32 inputStart, gX_F32 inputEnd, gX_F32 outputStart, gX_F32 outputEnd)
// All derivative functions are created from original
// Derivatives: Copyright (c) 2025 lanttu
gX_F32 gX_F32_Remap(gX_F32 value, gX_F32 inputStart, gX_F32 inputEnd, gX_F32 outputStart, gX_F32 outputEnd) {
    gX_F32 result = (value - inputStart)/(inputEnd - inputStart)*(outputEnd - outputStart) + outputStart;
    return result;
}

// Wrap input value from min to max
// Wrap(value, min, max) -> result
// Original: float Wrap(float value, float min, float max) from raymath.h
// Original: Copyright (c) 2015-2023 Ramon Santamaria
// Relabeled as gX_F32 gX_F32_Wrap(gX_F32 value, gX_F32 min, gX_F32 max)
// All derivative functions are created from original
// Derivatives: Copyright (c) 2025 lanttu
gX_F32 gX_F32_Wrap(gX_F32 value, gX_F32 min, gX_F32 max) {
    gX_F32 result = value - (max - min)*floorf((value - min)/(max - min));
    return result;
}

// Check whether two given scalars are almost equal
// Equals(x, y) -> result
// Equals_ErrorTol(x, y, tolerance) -> result
// Original: int FloatEquals(float x, float y) from raymath.h
// Original: Copyright (c) 2015-2023 Ramon Santamaria
// Relabeled as gX_I32 gX_F32_Equals(gX_F32 x, gX_F32 y)
// All derivative functions are created from original
// Derivatives: Copyright (c) 2025 lanttu
gX_I32 gX_F32_Equals(gX_F32 x, gX_F32 y) {
    gX_I32 result = (fabsf(x - y)) <= (gX_F32_EPSILON*fmaxf(1.0f, fmaxf(fabsf(x), fabsf(y))));
    return result;
}
gX_I32 gX_F32_Equals_ErrorToler(gX_F32 x, gX_F32 y, gX_F32 tolerance) {
    gX_I32 result = (fabsf(x - y)) <= (tolerance*fmaxf(1.0f, fmaxf(fabsf(x), fabsf(y))));
    return result;
}
gX_I64 gX_F64_Equals(gX_F64 x, gX_F64 y) {
    gX_I64 result = (fabs(x - y)) <= (gX_F64_EPSILON*fmax(1.0, fmax(fabs(x), fabs(y))));
    return result;
}
gX_I64 gX_F64_Equals_ErrorToler(gX_F64 x, gX_F64 y, gX_F64 tolerance) {
    gX_I64 result = (fabs(x - y)) <= (tolerance*fmax(1.0, fmax(fabs(x), fabs(y))));
    return result;
}

//opencode: gX_I32_Factorial
gX_I32 gX_I32_Factorial(gX_I32 number) {
    if (number == 0 || number == 1) return 1;
    return number * gX_I32_Factorial(number - 1);
}

//opencode: gX_I64_Factorial
gX_I64 gX_I64_Factorial(gX_I64 number) {
    if (number == 0 || number == 1) return 1;
    return number * gX_I64_Factorial(number - 1);
}

//opencode: gX_I32_GCD
gX_I32 gX_I32_GCD(gX_I32 number1, gX_I32 number2) {
    if (!number2) return (number1 > number2) ? number1 : number2;
    return gX_I32_GCD(number2, number1 % number2);
}

//opencode: gX_I32_LCM
gX_I32 gX_I32_LCM(gX_I32 number1, gX_I32 number2) {
    gX_I32 gcd = gX_I32_GCD(number1, number2);
    if (number1 < 0) number1 = gX_I32_Abs(number1);
    if (number2 < 0) number2 = gX_I32_Abs(number2);
    gX_I32 product = number1 * number2;
    if (gcd != 0) return product / gcd;
    return 0;
}

//opencode: gX_I32_Abs
gX_I32 gX_I32_Abs(gX_I32 number) {
    return (number > 0) ? number : -number;
}

//opencode: gX_I32_nCr
gX_I32 gX_I32_nCr(gX_I32 number1, gX_I32 number2) {
    gX_I32 n = gX_I32_Factorial(number1);
    gX_I32 k = gX_I32_Factorial(number2);
    gX_I32 nk = gX_I32_Factorial(number1 - number2);
    //opencode: C(n,k) = n! / (k! * (n-k)!) — the old `n / k * nk` divided
    //   first and overflowed I32 (nCr(10,3) returned garbage). Caught by the
    //   math pass's int_math case.
    return n / (k * nk);
}

//opencode: gX_I32_nPr
gX_I32 gX_I32_nPr(gX_I32 number1, gX_I32 number2) {
    gX_I32 n = gX_I32_Factorial(number1);
    gX_I32 nk = gX_I32_Factorial(number1 - number2);
    return n / nk;
}
