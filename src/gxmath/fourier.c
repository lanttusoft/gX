/**********************************************************************************************
*
*   gxmath/fourier.c
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

#include "fourier.h"
#include "constants.h"
#include <math.h>

//opencode: CMPLXF fallback for MinGW (not defined in older complex.h)
#ifndef CMPLXF
#define CMPLXF(x, y) ((gX_F32_complex){ (x), (y) })
#endif
#include <stdlib.h>

//opencode: gX_Fourier_Workspace_Create
gX_Fourier_Workspace* gX_Fourier_Workspace_Create(gX_U32 log2_n) {
    gX_Fourier_Workspace* ws = malloc(sizeof(gX_Fourier_Workspace));
    if (!ws) return NULL;

    ws->log2_n = log2_n;
    ws->n = 1 << log2_n;

    if (ws->n < 2) {
        ws->roots = NULL;
        return ws;
    }

    ws->roots = malloc((ws->n / 2) * sizeof(gX_F32_complex));
    if (!ws->roots) {
        free(ws);
        return NULL;
    }

    for (gX_U32 i = 0; i < ws->n / 2; i++) {
        gX_F32 arg = -2.0f * gX_PI * (gX_F32)i / (gX_F32)ws->n;
        ws->roots[i] = CMPLXF(cosf(arg), sinf(arg));
    }

    return ws;
}

//opencode: gX_Fourier_Workspace_Destroy
void gX_Fourier_Workspace_Destroy(gX_Fourier_Workspace* ws) {
    if (!ws) return;
    free(ws->roots);
    free(ws);
}

static gX_U32 bit_reverse(gX_U32 x, gX_U32 log2_n) {
    gX_U32 result = 0;
    for (gX_U32 i = 0; i < log2_n; i++) {
        if (x & ((gX_U32)1 << i))
            result |= (gX_U32)1 << (log2_n - 1 - i);
    }
    return result;
}

//opencode: gX_Fourier_FFT
void gX_Fourier_FFT(gX_Fourier_Workspace* ws, gX_F32_complex_n data) {
    if (!ws || !data) return;

    gX_U32 n = ws->n;
    if (n < 2) return;

    for (gX_U32 i = 0; i < n; i++) {
        gX_U32 j = bit_reverse(i, ws->log2_n);
        if (j > i) {
            gX_F32_complex temp = data[i];
            data[i] = data[j];
            data[j] = temp;
        }
    }

    for (gX_U32 len = 2; len <= n; len <<= 1) {
        gX_U32 half = len >> 1;
        gX_U32 step = n / len;
        for (gX_U32 i = 0; i < n; i += len) {
            for (gX_U32 j = 0; j < half; j++) {
                gX_F32_complex w = ws->roots[step * j];
                gX_F32_complex u = data[i + j];
                gX_F32_complex v = data[i + j + half] * w;
                data[i + j] = u + v;
                data[i + j + half] = u - v;
            }
        }
    }
}
