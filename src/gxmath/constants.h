/**********************************************************************************************
*
*   gxmath/constants.h
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

//lanttu: original gxmath math constants

#ifndef GX_MATH_CONSTANTS_H
#define GX_MATH_CONSTANTS_H

#ifndef gX_PI
    #define gX_PI 3.14159265358979323846f
#endif //gX_PI

#ifndef gX_DEG2RAD
    #define gX_DEG2RAD (gX_PI/180.0f)
#endif //gX_DEG2RAD

#ifndef gX_RAD2DEG
    #define gX_RAD2DEG (180.0f/gX_PI)
#endif //gX_RAD2DEG

#ifndef gX_TAU
    #define gX_TAU 6.28318530717958647693f
#endif

#ifndef gX_PHI
    #define gX_PHI 1.61803398874989484820f
#endif

#ifndef gX_E
    #define gX_E 2.71828182845904523536f
#endif

#ifndef gX_SQRT2
    #define gX_SQRT2 1.41421356237309504880f
#endif

#ifndef gX_SQRT3
    #define gX_SQRT3 1.73205080756887729353f
#endif

#ifndef gX_SQRT5
    #define gX_SQRT5 2.23606797749978969641f
#endif

#ifndef gX_SQRT6
    #define gX_SQRT6 2.44948974278317809820f
#endif

#ifndef gX_SQRT7
    #define gX_SQRT7 2.64575131106459059050f
#endif

#ifndef gX_SQRT10
    #define gX_SQRT10 3.16227766016837933199f
#endif

#ifndef gX_LN2
    #define gX_LN2 0.69314718055994530942f
#endif

#ifndef gX_LN3
    #define gX_LN3 1.09861228866810969140f
#endif

#ifndef gX_LN10
    #define gX_LN10 2.30258509299404568402f
#endif

#endif //GX_MATH_CONSTANTS_H
