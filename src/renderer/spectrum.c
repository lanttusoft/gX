/**********************************************************************************************
*
*   [gX: src/renderer/spectrum.c]
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
#include "spectrum.h"

#include <math.h>
#include <string.h>

#include "../common/log.h"
#include "../common/macros.h"
#include "../main.h"
#include "../audio/synth.h"
#include "../gxmath/fourier.h"

gX_I32 gX_RendererSpectrum_Load(gX_RendererSpectrum *spec) {
    gX_TRACELOG_PROLOGUE("RendererSpectrum_Load", "SPECTRUM: Loading")
    strcpy(spec->name, "Spectrum");
    strcpy(spec->description, "frequency spectrum");
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "name: %s", spec->name);

    spec->rt = gX_RenderTexture_Load(512, 256);
    spec->numBins = 32;
    spec->decayRate = 0.85f;
    spec->logScale = 1;
    spec->colorLow = (gX_Color){0, 100, 200, 220};
    spec->colorHigh = (gX_Color){0, 220, 100, 220};
    memset(spec->barHeights, 0, sizeof(spec->barHeights));
    spec->loadingstate.loaded = true;

    gX_TRACELOG_EPILOGUE("SPECTRUM: Loaded")
    return 0;
}

gX_I32 gX_RendererSpectrum_Unload(gX_RendererSpectrum *spec) {
    if (!spec->loadingstate.loaded) return 0;
    gX_RenderTexture_Unload(spec->rt);
    spec->loadingstate.loaded = false;
    return 0;
}

gX_I0 gX_RendererSpectrum_Update(gX_RendererSpectrum *spec) {
    if (!spec->loadingstate.loaded) return;

    gX_U32 count = g_synthScopeCount;
    if (count < 8) return;

    // Compute FFT size (power of 2)
    gX_U32 fftN = 1;
    gX_U32 log2n = 0;
    while (fftN < count && fftN < 512) { fftN <<= 1; log2n++; }
    if (fftN < 8) return;

    gX_F32_complex fftData[512];
    gX_U32 start = (g_synthScopeWrite + GX_SYNTH_SCOPE_SIZE - count) % GX_SYNTH_SCOPE_SIZE;
    for (gX_U32 i = 0; i < fftN; i++) {
        gX_U32 idx = (start + i) % GX_SYNTH_SCOPE_SIZE;
        fftData[i] = g_synthScope[idx] + 0.0f * I;
    }

    gX_Fourier_Workspace *ws = gX_Fourier_Workspace_Create(log2n);
    if (!ws) return;
    gX_Fourier_FFT(ws, fftData);
    gX_Fourier_Workspace_Destroy(ws);

    gX_U32 numBins = spec->numBins;
    if (numBins > GX_RENDERER_SPECTRUM_MAX_BINS) numBins = GX_RENDERER_SPECTRUM_MAX_BINS;
    gX_U32 halfN = fftN / 2;
    if (numBins > halfN) numBins = halfN;

    // Compute magnitudes and apply decay
    gX_F32 maxMag = 0.001f;
    gX_F32 newHeights[GX_RENDERER_SPECTRUM_MAX_BINS];
    for (gX_U32 i = 0; i < numBins; i++) {
        gX_U32 binIdx;
        if (spec->logScale && numBins > 1) {
            // Logarithmic bin spacing
            gX_F32 f = (gX_F32)i / (gX_F32)(numBins - 1);
            binIdx = (gX_U32)(f * f * (gX_F32)(halfN - 1));
            if (binIdx >= halfN) binIdx = halfN - 1;
        } else {
            binIdx = i * halfN / numBins;
        }

        gX_F32 mag = cabsf(fftData[binIdx]);
        if (mag > maxMag) maxMag = mag;
        newHeights[i] = mag;
    }

    // Normalize and smooth with decay
    for (gX_U32 i = 0; i < numBins; i++) {
        gX_F32 norm = newHeights[i] / maxMag;
        if (norm > 1.0f) norm = 1.0f;
        if (norm > spec->barHeights[i]) {
            spec->barHeights[i] = norm; // Peak: jump up instantly
        } else {
            spec->barHeights[i] *= spec->decayRate; // Fall: decay
        }
    }
}

gX_I0 gX_RendererSpectrum_DrawTo(gX_RendererSpectrum *spec, gX_RenderTexture *outputRT) {
    if (!spec->loadingstate.loaded) return;

    gX_RenderTexture target = outputRT ? *outputRT : spec->rt;
    gX_F32 texW = (gX_F32)target.texture.width;
    gX_F32 texH = (gX_F32)target.texture.height;
    gX_I32 numBins = spec->numBins;
    if (numBins > GX_RENDERER_SPECTRUM_MAX_BINS) numBins = GX_RENDERER_SPECTRUM_MAX_BINS;
    if (numBins < 1) return;

    gX_F32 binW = texW / (gX_F32)numBins;

    gX_Drawing_BeginTexture(target);
        gX_Drawing_ClearBG((gX_Color){0, 0, 0, 255});

        for (gX_I32 i = 0; i < numBins; i++) {
            gX_F32 h = spec->barHeights[i] * texH;
            if (h < 1.0f) h = 1.0f;

            // Interpolate color from low to high
            gX_F32 t = (gX_F32)i / (gX_F32)numBins;
            gX_Color col;
            col.r = (gX_U8)(spec->colorLow.r + (gX_F32)(spec->colorHigh.r - spec->colorLow.r) * t);
            col.g = (gX_U8)(spec->colorLow.g + (gX_F32)(spec->colorHigh.g - spec->colorLow.g) * t);
            col.b = (gX_U8)(spec->colorLow.b + (gX_F32)(spec->colorHigh.b - spec->colorLow.b) * t);
            col.a = 220;

            gX_Rectangle bar = {
                (gX_F32)i * binW + 1.0f, texH - h,
                binW - 2.0f, h
            };
            gX_Rectangle_Draw(bar, col);
        }
    gX_Drawing_EndTexture();
}
