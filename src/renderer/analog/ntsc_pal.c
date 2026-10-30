/**********************************************************************************************
*
*   [gX: src/renderer/analog/ntsc_pal.c]
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


//opencode: CPU-based NTSC/PAL simulator — threaded modulate/demodulate via EMMIR library
//   Uses crt_* library from src/external/ (CRT_SYSTEM_NTSC only; PAL requires external pal.h/pal.c)
#include "ntsc_pal.h"

#include <stdlib.h>
#include <string.h>

#include "../../external/crt_core.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../wrappers/shader.h"
#include "../../wrappers/texture.h"
#include "../../wrappers/image.h"
#include "../../wrappers/convert.h"

//opencode: allocate CRT state and settings, create output buffer and GPU texture
gX_I32 gX_Analog_NTSCPAL_Init(gX_Analog_NTSCPAL *np, gX_U32 width, gX_U32 height) {
    gX_TRACELOG_PROLOGUE("Analog_NTSCPAL_Init", "ANALOG_NTSCPAL: Initializing")

    np->outputWidth = width;
    np->outputHeight = height;
    gX_U32 outputSize = width * height;

    //opencode: allocate CPU output buffer (RGBA 32-bit)
    np->outputImg = (gX_U32*)calloc(outputSize, sizeof(gX_U32));
    if (!np->outputImg) {
        gX_TraceLog(gX_LOG_ERROR, "ANALOG_NTSCPAL: failed to allocate output buffer (%ux%u)", width, height);
        return -1;
    }

    //opencode: allocate process double-buffer
    np->processBuffer = (gX_U32*)calloc(outputSize, sizeof(gX_U32));
    if (!np->processBuffer) {
        free(np->outputImg);
        np->outputImg = NULL;
        return -1;
    }

    //opencode: allocate CRT state (opaque)
    np->crtState = (gX_I0*)calloc(1, sizeof(struct CRT));
    if (!np->crtState) {
        free(np->outputImg); np->outputImg = NULL;
        free(np->processBuffer); np->processBuffer = NULL;
        return -1;
    }

    //opencode: allocate NTSC settings
    np->ntscSettings = (gX_I0*)calloc(1, sizeof(struct NTSC_SETTINGS));
    if (!np->ntscSettings) {
        free(np->outputImg); np->outputImg = NULL;
        free(np->processBuffer); np->processBuffer = NULL;
        free(np->crtState); np->crtState = NULL;
        return -1;
    }

    //opencode: initialize CRT library — output format RGBA
    struct CRT *crt = (struct CRT *)np->crtState;
    crt_init(crt, (int)width, (int)height, CRT_PIX_FORMAT_RGBA, (unsigned char*)np->outputImg);

    //opencode: create GPU texture for output
    {
        gX_Image blank = gX_Image_GenColor((gX_I32)width, (gX_I32)height, gX_BLACK);
        np->output = gX_LoadTextureFromImage(blank);
        gX_Image_Unload(blank);
    }

    gX_TraceLog(gX_LOG_TRACE, "ANALOG_NTSCPAL: output %ux%u, CRT buf=%zu settings=%zu",
        width, height, sizeof(struct CRT), sizeof(struct NTSC_SETTINGS));

    gX_TRACELOG_EPILOGUE("ANALOG_NTSCPAL: Init done")
    return 0;
}

gX_I32 gX_Analog_NTSCPAL_Uninit(gX_Analog_NTSCPAL *np) {
    gX_TRACELOG_PROLOGUE("Analog_NTSCPAL_Uninit", "ANALOG_NTSCPAL: Uninitializing")

    if (np->output.id > 0) gX_Texture_Unload(np->output);
    np->output.id = 0;

    free(np->outputImg);
    np->outputImg = NULL;

    free(np->processBuffer);
    np->processBuffer = NULL;

    free(np->crtState);
    np->crtState = NULL;

    free(np->ntscSettings);
    np->ntscSettings = NULL;

    free(np->palState);
    np->palState = NULL;

    free(np->palSettings);
    np->palSettings = NULL;

    np->threadRunning = 0;

    gX_TRACELOG_EPILOGUE("ANALOG_NTSCPAL: Uninit done")
    return 0;
}

//opencode: process one frame through the EMMIR NTSC modulate/demodulate pipeline
gX_I0 gX_Analog_NTSCPAL_Update(gX_Analog_NTSCPAL *np, const gX_U32 *inputPixels, gX_I32 inputW, gX_I32 inputH) {
    if (!np->crtState || !np->ntscSettings || !inputPixels) return;
    if (inputW <= 0 || inputH <= 0) return;

    struct CRT *crt = (struct CRT *)np->crtState;
    struct NTSC_SETTINGS *settings = (struct NTSC_SETTINGS *)np->ntscSettings;

    //opencode: configure NTSC settings for this frame
    settings->data      = (const unsigned char*)inputPixels;
    settings->format    = CRT_PIX_FORMAT_RGBA;
    settings->w         = inputW;
    settings->h         = inputH;
    settings->raw       = np->rawNTSC ? 1 : 0;
    settings->as_color  = np->colorNTSC ? 1 : 0;
    settings->field     = np->fieldNTSC;
    settings->frame     = np->fieldNTSC; // matching field for interlaced
    settings->hue       = (int)(np->hueNTSC + 0.5f) % 360;
    settings->xoffset   = 0;
    settings->yoffset   = 0;

    //opencode: set CRT monitor controls
    crt->brightness = 0;
    crt->contrast   = 180;
    crt->saturation = 10;
    crt->hue        = (int)(np->hueNTSC + 0.5f) % 360;
    crt->scanlines  = 0;
    crt->blend      = 1;

    //opencode: run modulate + demodulate synchronously. Accepted tradeoff for
    //   the analog sample scene: ~28ms at 640x480 (~35fps), ~70fps+ at the
    //   default render scale. A background thread would need double-buffered
    //   output + frame pacing wired into Init/Update/Uninit — not worth it
    //   for a demo renderer; revisit only if the effect ships as product.
    crt_modulate(crt, settings);
    crt_demodulate(crt, np->noiseNTSC);

    //opencode: flip field for next frame (interlace simulation)
    np->fieldNTSC ^= 1;
    np->frameCount++;
}

//opencode: upload current output buffer to GPU texture and draw fullscreen
gX_I0 gX_Analog_NTSCPAL_Draw(gX_Analog_NTSCPAL *np) {
    if (np->output.id == 0 || !np->outputImg) return;

    //opencode: update GPU texture from CPU output buffer
    gX_Texture_Update(np->output, np->outputImg);

    //opencode: draw the processed frame
    gX_Texture_Draw(np->output, (gX_Texture_Draw_Params){
        .scale = {1, 1},
        .tint = gX_WHITE
    });
}
