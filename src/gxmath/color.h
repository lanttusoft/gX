/**********************************************************************************************
*
*   gxmath/color.h
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

//lanttu: original gxmath module for color operations

#ifndef GXMATH_COLOR_H
#define GXMATH_COLOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

gX_Color gX_Color_Lerp(gX_Color c1, gX_Color c2, gX_F32 t);
gX_Color gX_Color_LerpRGB(gX_Color c1, gX_Color c2, gX_F32 t);

gX_Color gX_Color_Mul(gX_Color c1, gX_Color c2);

gX_Color gX_Color_FromV4(gX_F32v4 v4);
gX_F32v4 gX_Color_ToV4(gX_Color col);

gX_Color gX_Color_FromV4Raw(gX_F32v4 v4);
gX_F32v4 gX_Color_ToV4Raw(gX_Color col);

#ifdef __cplusplus
}
#endif

#endif //GXMATH_COLOR_H
