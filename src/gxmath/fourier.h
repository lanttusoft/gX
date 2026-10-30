/**********************************************************************************************
*
*   gxmath/fourier.h
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

//opencode: ported FFT from gamemath to gxmath with workspace-based iterative implementation

#ifndef GXMATH_FOURIER_H
#define GXMATH_FOURIER_H

#include "../common/types.h"
#include <complex.h>

typedef gX_F32 _Complex gX_F32_complex;
typedef gX_F32_complex *gX_F32_complex_n;

typedef struct gX_Fourier_Workspace {
    gX_U32 log2_n;
    gX_U32 n;
    gX_F32_complex_n roots;
} gX_Fourier_Workspace;

//opencode: gX_Fourier_Workspace_Create
gX_Fourier_Workspace* gX_Fourier_Workspace_Create(gX_U32 log2_n);

//opencode: gX_Fourier_FFT
void gX_Fourier_FFT(gX_Fourier_Workspace* ws, gX_F32_complex_n data);

//opencode: gX_Fourier_Workspace_Destroy
void gX_Fourier_Workspace_Destroy(gX_Fourier_Workspace* ws);

#endif //GXMATH_FOURIER_H
