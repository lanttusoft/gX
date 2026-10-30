/**********************************************************************************************
*
*   [gX: src/audio/filter.h]
*   gX — TPT/ZDF state-variable filter + the engine-level master filter
*
*   Copyright (c) 2026 lanttu, opencode
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


//ai: the shared filter core. The engines' original Chamberlin SVF uses
//   f = 2*sin(pi*cutoff) and clamps f at 1.0, so every cutoff above ~0.17
//   collapses onto one fixed filter (wide open). This is a topology-
//   preserving-transform state-variable filter (Andrew Simper): stable at
//   any cutoff and resonance, with the full 0..1 range mapping to 0..Nyquist
//   (cutoff 1.0 -> Nyquist/edge), so a sweep keeps moving at the top.
//   Coefficient refresh is cached: call Prepare when cutoff/resonance may
//   have moved, then Step per channel.
//
//   On top of the core sits the **master filter**: an always-optional
//   processor on the mixed output, kept at the TAIL of raylib's mixed
//   processor chain (engines attach lazily and would otherwise land after
//   it, leaving their output unfiltered). It is enabled through the API /
//   settings and runs before the device gets the buffer.
#ifndef AUDIO_FILTER_H
#define AUDIO_FILTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

#define GX_AUDIO_FILTER_LP 0
#define GX_AUDIO_FILTER_HP 1
#define GX_AUDIO_FILTER_BP 2

//ai: stereo channels carried by one filter state.
#define GX_AUDIO_FILTER_CHANNELS 2

typedef struct gX_Audio_Filter {
    gX_I32 mode;        //ai: GX_AUDIO_FILTER_*
    gX_F32 cutoff;      //ai: 0..1 normalized to Nyquist
    gX_F32 resonance;   //ai: 0..1
    //ai: cached coefficients + state (one ic pair per channel)
    gX_F32 g, k, a1, a2, a3;
    gX_F32 cachedCutoff;
    gX_F32 cachedRes;
    gX_I32 cachedMode;
    gX_I1  coeffsValid;
    gX_F32 ic1[GX_AUDIO_FILTER_CHANNELS];
    gX_F32 ic2[GX_AUDIO_FILTER_CHANNELS];
} gX_Audio_Filter;

//ai: clear the state (keeps mode/cutoff/resonance).
gX_I0  gX_Audio_Filter_Reset(gX_Audio_Filter *filter);
//ai: set the parameters (clamped) and invalidate the coefficient cache.
gX_I0  gX_Audio_Filter_Set(gX_Audio_Filter *filter, gX_I32 mode, gX_F32 cutoff, gX_F32 resonance);
//ai: refresh the coefficients if cutoff/resonance/mode moved (cheap compare).
gX_I0  gX_Audio_Filter_Prepare(gX_Audio_Filter *filter, gX_U32 sampleRate);
//ai: one sample through the filter.
gX_F32 gX_Audio_Filter_Step(gX_Audio_Filter *filter, gX_I32 channel, gX_F32 input);
//ai: stereo interleaved block, in place (coefficients once per block).
gX_I0  gX_Audio_Filter_Process(gX_Audio_Filter *filter, gX_F32 *samples,
    gX_U32 frames, gX_U32 sampleRate);

//ai: engine-level master filter — enable/params, per-frame attach guard and
//   the settings-facing accessors.
gX_I0  gX_Audio_MasterFilter_Init(gX_I0);
gX_I0  gX_Audio_MasterFilter_Unload(gX_I0);
gX_I0  gX_Audio_MasterFilter_Update(gX_I0);
gX_I0  gX_Audio_MasterFilter_Set(gX_I1 enabled, gX_I32 mode, gX_F32 cutoff, gX_F32 resonance);
gX_I1  gX_Audio_MasterFilter_IsEnabled(gX_I0);
gX_I32 gX_Audio_MasterFilter_GetMode(gX_I0);
gX_F32 gX_Audio_MasterFilter_GetCutoff(gX_I0);
gX_F32 gX_Audio_MasterFilter_GetResonance(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //AUDIO_FILTER_H
