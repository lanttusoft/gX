/**********************************************************************************************
*
*   [gX: src/renderer/analog/ntsc_pal.h]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu
*
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*   gX is free software: you can redistribute it and/or modify it under the
*   terms of the GNU General Public License as published by the Free Software
*   Foundation, either version 3 of the License, or (at your option) any later
*   version.
*   gX is distributed in the hope that it will be useful, but WITHOUT ANY
*   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
*   FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
*   details.
*   You should have received a copy of the GNU General Public License along
*   with gX. If not, see <https://www.gnu.org/licenses/>.
*
*
**********************************************************************************************/


#ifndef ANALOG_NTSC_PAL_H
#define ANALOG_NTSC_PAL_H

#ifdef __cplusplus
extern "C" {
#endif

//opencode: CPU-based NTSC/PAL simulator using EMMIR's crt_* libraries
//   NTSC: uses crt_modulate + crt_demodulate (crt_ntsc.h)
//   PAL: uses pal_modulate + pal_demodulate (pal_core.h — requires external pal.c)
#include "../../common/types.h"
#include "../../common/threading.h"
#include "../../wrappers/image.h"
#include "../../wrappers/texture.h"

//opencode: max output dimension for NTSC/PAL processing
#define GX_ANALOG_NTSCPAL_MAX_W 1024
#define GX_ANALOG_NTSCPAL_MAX_H 768

typedef struct gX_Analog_NTSCPAL {
    gX_I1 enableNTSC;   // true=NTSC, false=PAL
    gX_I1 colorNTSC;
    gX_I32 noiseNTSC;
    gX_I32 fieldNTSC;
    gX_I1 rawNTSC;
    gX_F32 hueNTSC;

    gX_U32 outputWidth;
    gX_U32 outputHeight;
    gX_U32 *outputImg;     // CPU output buffer (RGBA pixels)
    gX_Texture output;      // GPU texture uploaded from outputImg

    //opencode: EMMIR library state (opaque pointers, allocated at runtime)
    gX_I0 *crtState;       // struct CRT*
    gX_I0 *ntscSettings;   // struct NTSC_SETTINGS*
    gX_I0 *palState;       // struct PAL_CRT*
    gX_I0 *palSettings;    // struct PAL_SETTINGS*

    //opencode: threading for background modulate/demodulate
    gX_Thread processThread;
    gX_Mutex  bufferMutex;
    gX_I1     threadRunning;
    gX_I1     frameReady;
    gX_U32   *processBuffer; // double-buffer processed by thread
    gX_U32    frameCount;     // frame counter for interlacing
} gX_Analog_NTSCPAL;

#define GX_ANALOG_NTSCPAL_DEFAULTS \
    (gX_Analog_NTSCPAL){ \
        .enableNTSC = 1, \
        .colorNTSC = 1, \
        .noiseNTSC = 0, \
        .fieldNTSC = 0, \
        .rawNTSC = 1, \
        .hueNTSC = 0.0f, \
        .outputWidth = 640, \
        .outputHeight = 480, \
        .outputImg = NULL, \
        .output = {0}, \
        .crtState = NULL, \
        .ntscSettings = NULL, \
        .palState = NULL, \
        .palSettings = NULL, \
        .processThread = {0}, \
        .bufferMutex = {0}, \
        .threadRunning = 0, \
        .frameReady = 0, \
        .processBuffer = NULL, \
        .frameCount = 0 \
    }

gX_I32 gX_Analog_NTSCPAL_Init(gX_Analog_NTSCPAL *np, gX_U32 width, gX_U32 height);
gX_I32 gX_Analog_NTSCPAL_Uninit(gX_Analog_NTSCPAL *np);
gX_I0  gX_Analog_NTSCPAL_Update(gX_Analog_NTSCPAL *np, const gX_U32 *inputPixels, gX_I32 inputW, gX_I32 inputH);
gX_I0  gX_Analog_NTSCPAL_Draw(gX_Analog_NTSCPAL *np);

#ifdef __cplusplus
}
#endif

#endif //ANALOG_NTSC_PAL_H
