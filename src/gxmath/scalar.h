/**********************************************************************************************
*
*   gxmath/scalar.h
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

#ifndef GXMATH_SCALAR_H
#define GXMATH_SCALAR_H

#include "../common/types.h"

// Clamp value
// Clamp(value, min, max) -> result
// Original: float Clamp(float value, float min, float max) from raymath.h
// Original: Copyright (c) 2015-2023 Ramon Santamaria
// Relabeled as gX_F32 gX_F32_Clamp(gX_F32 value, gX_F32 min, gX_F32 max)
// All derivative functions are created from original
// Derivatives: Copyright (c) 2025 lanttu
gX_F32 gX_F32_Clamp(gX_F32 value, gX_F32 min, gX_F32 max);

// Calculate linear interpolation between two values
// Lerp(start, end, amount) -> result
// Original: float Lerp(float start, float end, float amount) from raymath.h
// Original: Copyright (c) 2015-2023 Ramon Santamaria
// Relabeled as gX_F32 gX_F32_Lerp(gX_F32 start, gX_F32 end, gX_F32 amount)
// All derivative functions are created from original
// Derivatives: Copyright (c) 2025 lanttu
gX_F32 gX_F32_Lerp(gX_F32 start, gX_F32 end, gX_F32 amount);

// Normalize input value within input range
// Normalize(value, start, end) -> result
// Original: float Normalize(float value, float start, float end) from raymath.h
// Original: Copyright (c) 2015-2023 Ramon Santamaria
// Relabeled as gX_F32 gX_F32_Normalize(gX_F32 value, gX_F32 start, gX_F32 end)
// All derivative functions are created from original
// Derivatives: Copyright (c) 2025 lanttu
gX_F32 gX_F32_Normalize(gX_F32 value, gX_F32 start, gX_F32 end);

// Remap input value within input range to output range
// Remap(value, inputStart, inputEnd, outputStart, outputEnd) -> result
// Original: float Remap(float value, float inputStart, float inputEnd, float outputStart, float outputEnd) from raymath.h
// Original: Copyright (c) 2015-2023 Ramon Santamaria
// Relabeled as gX_F32 gX_F32_Remap(gX_F32 value, gX_F32 inputStart, gX_F32 inputEnd, gX_F32 outputStart, gX_F32 outputEnd)
// All derivative functions are created from original
// Derivatives: Copyright (c) 2025 lanttu
gX_F32 gX_F32_Remap(gX_F32 value, gX_F32 inputStart, gX_F32 inputEnd, gX_F32 outputStart, gX_F32 outputEnd);

// Wrap input value from min to max
// Wrap(value, min, max) -> result
// Original: float Wrap(float value, float min, float max) from raymath.h
// Original: Copyright (c) 2015-2023 Ramon Santamaria
// Relabeled as gX_F32 gX_F32_Wrap(gX_F32 value, gX_F32 min, gX_F32 max)
// All derivative functions are created from original
// Derivatives: Copyright (c) 2025 lanttu
gX_F32 gX_F32_Wrap(gX_F32 value, gX_F32 min, gX_F32 max);

// Check whether two given scalars are almost equal
// Equals(x, y) -> result
// Equals_ErrorTol(x, y, tolerance) -> result
// Original: int FloatEquals(float x, float y) from raymath.h
// Original: Copyright (c) 2015-2023 Ramon Santamaria
// Relabeled as gX_I32 gX_F32_Equals(gX_F32 x, gX_F32 y)
// All derivative functions are created from original
// Derivatives: Copyright (c) 2025 lanttu
gX_I32 gX_F32_Equals(gX_F32 x, gX_F32 y);
gX_I32 gX_F32_Equals_ErrorToler(gX_F32 x, gX_F32 y, gX_F32 tolerance);
gX_I64 gX_F64_Equals(gX_F64 x, gX_F64 y);
gX_I64 gX_F64_Equals_ErrorToler(gX_F64 x, gX_F64 y, gX_F64 tolerance);

//opencode: Integer math functions
//opencode: gX_I32_Factorial
//opencode: gX_I64_Factorial
//opencode: gX_I32_GCD
//opencode: gX_I32_LCM
//opencode: gX_I32_Abs
//opencode: gX_I32_nCr
//opencode: gX_I32_nPr
gX_I32 gX_I32_Factorial(gX_I32 number);
gX_I64 gX_I64_Factorial(gX_I64 number);
gX_I32 gX_I32_GCD(gX_I32 number1, gX_I32 number2);
gX_I32 gX_I32_LCM(gX_I32 number1, gX_I32 number2);
gX_I32 gX_I32_Abs(gX_I32 number);
gX_I32 gX_I32_nCr(gX_I32 number1, gX_I32 number2);
gX_I32 gX_I32_nPr(gX_I32 number1, gX_I32 number2);

#endif //GXMATH_SCALAR_H
