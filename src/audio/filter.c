/**********************************************************************************************
*
*   [gX: src/audio/filter.c]
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


//ai: shared filter core + master filter — see filter.h. The core is Simper's
//   ZDF SVF in tan-pre-warped form (g = tan(pi/2 * cutoff) for a cutoff
//   normalized to Nyquist); the master filter wraps it in a raylib mixed
//   processor that is re-asserted to the chain TAIL whenever one of the
//   engines' lazy processors attaches, so "master" means the whole mix.
#include <math.h>
#include <string.h>

#include "filter.h"

#include "../common/log.h"
#include "../gxmath/gxmath.h"

//ai: mixer + engines — the attach guard tracks their lazy processors.
#include "../audio.h"
#include "opx.h"
#include "pad.h"
#include "synth.h"

//opencode: raylib audio processor API (same pattern as the engines)
#include "raylib.h"

static gX_F32 gX_Audio_Filter_Clamp(gX_F32 value, gX_F32 lo, gX_F32 hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

gX_I0 gX_Audio_Filter_Reset(gX_Audio_Filter *filter) {
    if (!filter) return;
    for (gX_I32 ch = 0; ch < GX_AUDIO_FILTER_CHANNELS; ch++) {
        filter->ic1[ch] = 0.0f;
        filter->ic2[ch] = 0.0f;
    }
    return;
}

//ai: parameter clamp shared by Set and the master wrapper's change check.
static gX_I32 gX_Audio_Filter_ClampMode(gX_I32 mode) {
    return (mode < 0) ? 0 : (mode > 2) ? 2 : mode;
}

gX_I0 gX_Audio_Filter_Set(gX_Audio_Filter *filter, gX_I32 mode, gX_F32 cutoff, gX_F32 resonance) {
    if (!filter) return;
    mode = gX_Audio_Filter_ClampMode(mode);
    cutoff = gX_Audio_Filter_Clamp(cutoff, 0.001f, 0.99f);
    resonance = gX_Audio_Filter_Clamp(resonance, 0.0f, 1.0f);
    //ai: unchanged parameters keep the coefficient cache — per-sample callers
    //   (the engine filters with their own modulation) call Set every sample.
    if (filter->mode == mode && filter->cutoff == cutoff
        && filter->resonance == resonance) {
        return;
    }
    filter->mode = mode;
    filter->cutoff = cutoff;
    filter->resonance = resonance;
    filter->coeffsValid = 0;
    return;
}

//ai: coefficient refresh — a cheap compare keeps per-sample callers (the
//   engine filters with their own modulation) from paying tan() twice.
gX_I0 gX_Audio_Filter_Prepare(gX_Audio_Filter *filter, gX_U32 sampleRate) {
    if (!filter || sampleRate == 0) return;
    if (filter->coeffsValid
        && filter->cachedMode == filter->mode
        && filter->cachedCutoff == filter->cutoff
        && filter->cachedRes == filter->resonance) {
        return;
    }
    //ai: cutoff is normalized to Nyquist: fc = cutoff * fs/2
    gX_F32 g = tanf(gX_PI * 0.5f * filter->cutoff);
    //ai: k = 1/Q (damping). The old Chamberlin used q = 1 - res, so keep
    //   that feel; the ZDF topology stays stable for any k > 0.
    gX_F32 k = 1.0f - filter->resonance;
    if (k < 0.02f) k = 0.02f;
    gX_F32 a1 = 1.0f / (1.0f + g * (g + k));
    filter->g = g;
    filter->k = k;
    filter->a1 = a1;
    filter->a2 = g * a1;
    filter->a3 = g * filter->a2;
    filter->cachedMode = filter->mode;
    filter->cachedCutoff = filter->cutoff;
    filter->cachedRes = filter->resonance;
    filter->coeffsValid = 1;
    return;
}

gX_F32 gX_Audio_Filter_Step(gX_Audio_Filter *filter, gX_I32 channel, gX_F32 input) {
    if (!filter || channel < 0 || channel >= GX_AUDIO_FILTER_CHANNELS) return input;
    gX_F32 v3 = input - filter->ic2[channel];
    gX_F32 v1 = filter->a1 * filter->ic1[channel] + filter->a2 * v3;
    gX_F32 v2 = filter->ic2[channel] + filter->a2 * filter->ic1[channel] + filter->a3 * v3;
    filter->ic1[channel] = 2.0f * v1 - filter->ic1[channel];
    filter->ic2[channel] = 2.0f * v2 - filter->ic2[channel];
    switch (filter->mode) {
        case GX_AUDIO_FILTER_LP: return v2;
        case GX_AUDIO_FILTER_HP: return input - filter->k * v1 - v2;
        case GX_AUDIO_FILTER_BP: return v1;
        default: return input;
    }
}

gX_I0 gX_Audio_Filter_Process(gX_Audio_Filter *filter, gX_F32 *samples,
    gX_U32 frames, gX_U32 sampleRate) {
    if (!filter || !samples) return;
    gX_Audio_Filter_Prepare(filter, sampleRate);
    for (gX_U32 i = 0; i < frames; i++) {
        samples[i * 2] = gX_Audio_Filter_Step(filter, 0, samples[i * 2]);
        samples[i * 2 + 1] = gX_Audio_Filter_Step(filter, 1, samples[i * 2 + 1]);
    }
    return;
}

// ===========================================================================
// master filter — the tail of the mixed-processor chain
// ===========================================================================

static struct {
    gX_I1 enabled;
    gX_Audio_Filter filter;
    gX_I1 processorAttached;
    //ai: combined attach state of the other gX processors (engines + reverb):
    //   when one attaches it lands AFTER our processor, so we re-attach to
    //   stay at the tail.
    gX_I32 seenGeneration;
} g_masterFilter = {0};

//ai: the other gX processors' combined attach state — cheap flag reads.
static gX_I32 gX_Audio_MasterFilter_OthersMask(gX_I0) {
    gX_I32 mask = 0;
    if (gX_Synth_IsAttached()) mask |= 1 << 0;
    if (gX_OPX_IsAttached()) mask |= 1 << 1;
    if (gX_Pad_IsAttached()) mask |= 1 << 2;
    if (gX_Audio_Reverb_IsAttached()) mask |= 1 << 3;
    return mask;
}

//opencode: raylib mixed-processor adapter — runs on the final mixed buffer.
static void gX_Audio_MasterFilter_Process(void *buffer, unsigned int frames) {
    if (!g_masterFilter.enabled) return;
    //ai: the engines assume a 48 kHz device rate (raylib exposes no
    //   device-rate getter); the master filter follows the same contract.
    gX_Audio_Filter_Process(&g_masterFilter.filter, (gX_F32 *)buffer,
        (gX_U32)frames, 48000);
}

static gX_I0 gX_Audio_MasterFilter_Detach(gX_I0) {
    if (!g_masterFilter.processorAttached) return;
#if defined(GAME_BACKEND_RAYLIB)
    DetachAudioMixedProcessor(gX_Audio_MasterFilter_Process);
#endif
    g_masterFilter.processorAttached = 0;
    gX_TraceLog(gX_LOG_TRACE, "FILTER: master detached");
    return;
}

static gX_I0 gX_Audio_MasterFilter_Attach(gX_I0) {
    if (g_masterFilter.processorAttached) return;
#if defined(GAME_BACKEND_RAYLIB)
    if (!IsAudioDeviceReady()) return;
    AttachAudioMixedProcessor(gX_Audio_MasterFilter_Process);
#endif
    g_masterFilter.processorAttached = 1;
    gX_TraceLog(gX_LOG_TRACE, "FILTER: master attached (tail)");
    return;
}

gX_I0 gX_Audio_MasterFilter_Init(gX_I0) {
    memset(&g_masterFilter, 0, sizeof(g_masterFilter));
    gX_Audio_Filter_Set(&g_masterFilter.filter, GX_AUDIO_FILTER_LP, 0.8f, 0.0f);
    gX_Audio_Filter_Reset(&g_masterFilter.filter);
    return;
}

gX_I0 gX_Audio_MasterFilter_Unload(gX_I0) {
    gX_Audio_MasterFilter_Detach();
    g_masterFilter.enabled = 0;
    return;
}

gX_I0 gX_Audio_MasterFilter_Set(gX_I1 enabled, gX_I32 mode, gX_F32 cutoff, gX_F32 resonance) {
    //ai: only a REAL parameter change clears the filter state (avoids a
    //   transient on a redundant set, e.g. the same preset re-applied).
    gX_I1 paramsChanged =
        g_masterFilter.filter.mode != gX_Audio_Filter_ClampMode(mode)
        || g_masterFilter.filter.cutoff != gX_Audio_Filter_Clamp(cutoff, 0.001f, 0.99f)
        || g_masterFilter.filter.resonance != gX_Audio_Filter_Clamp(resonance, 0.0f, 1.0f);
    g_masterFilter.enabled = enabled ? 1 : 0;
    gX_Audio_Filter_Set(&g_masterFilter.filter, mode, cutoff, resonance);
    if (paramsChanged) gX_Audio_Filter_Reset(&g_masterFilter.filter);
    if (!g_masterFilter.enabled) {
        gX_Audio_MasterFilter_Detach();
    }
    return;
}

gX_I1 gX_Audio_MasterFilter_IsEnabled(gX_I0) {
    return g_masterFilter.enabled;
}

gX_I32 gX_Audio_MasterFilter_GetMode(gX_I0) {
    return g_masterFilter.filter.mode;
}

gX_F32 gX_Audio_MasterFilter_GetCutoff(gX_I0) {
    return g_masterFilter.filter.cutoff;
}

gX_F32 gX_Audio_MasterFilter_GetResonance(gX_I0) {
    return g_masterFilter.filter.resonance;
}

//ai: per-frame attach guard — the master filter must be the LAST processor
//   in raylib's chain. Every gX engine (and the reverb) attaches lazily, so
//   whenever their combined attach mask changes, re-attach ourselves at the
//   tail. Disabled means detached (no audio-thread cost).
gX_I0 gX_Audio_MasterFilter_Update(gX_I0) {
    if (!g_masterFilter.enabled) {
        gX_Audio_MasterFilter_Detach();
        g_masterFilter.seenGeneration = gX_Audio_MasterFilter_OthersMask();
        return;
    }
    gX_I32 others = gX_Audio_MasterFilter_OthersMask();
    if (others != g_masterFilter.seenGeneration) {
        g_masterFilter.seenGeneration = others;
        gX_Audio_MasterFilter_Detach();
        gX_Audio_MasterFilter_Attach();
    }
    gX_Audio_MasterFilter_Attach();
    return;
}
