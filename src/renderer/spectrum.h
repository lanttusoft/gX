/**********************************************************************************************
*
*   [gX: src/renderer/spectrum.h]
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


//opencode: Spectrum renderer — FFT frequency bar chart from synth scope buffer
#ifndef RENDERER_SPECTRUM_H
#define RENDERER_SPECTRUM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/types/state.h"
#include "../wrappers/wrappers.h"

#define GX_RENDERER_SPECTRUM_MAX_BINS 128

typedef struct gX_RendererSpectrum {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_LoadingState loadingstate;

    gX_RenderTexture rt;
    gX_I32 numBins;               // number of frequency bins (16–128)
    gX_F32 decayRate;             // how fast bars fall (0–1 per frame)
    gX_I1  logScale;              // logarithmic frequency axis
    gX_F32 barHeights[GX_RENDERER_SPECTRUM_MAX_BINS]; // smoothed heights
    gX_Color colorLow;
    gX_Color colorHigh;
} gX_RendererSpectrum;

gX_I32 gX_RendererSpectrum_Load(gX_RendererSpectrum *spec);
gX_I32 gX_RendererSpectrum_Unload(gX_RendererSpectrum *spec);
gX_I0  gX_RendererSpectrum_Update(gX_RendererSpectrum *spec);
//opencode: DrawTo with explicit output RT (NULL = module's own persistent RT)
gX_I0  gX_RendererSpectrum_DrawTo(gX_RendererSpectrum *spec, gX_RenderTexture *outputRT);
#define gX_RendererSpectrum_Draw(spec) gX_RendererSpectrum_DrawTo((spec), NULL)

#ifdef __cplusplus
}
#endif

#endif //RENDERER_SPECTRUM_H
