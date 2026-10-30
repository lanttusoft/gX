/**********************************************************************************************
*
*   [gX: src/audio/filter_tests.c]
*   gX — shared filter core + master filter tests (CPU only)
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


//ai: filter core suite — no audio device needed. The core's promise over the
//   old Chamberlin SVF is the FULL cutoff range (no f=2*sin clamp above
//   ~0.17), so the cases pin the response across the whole range with
//   single-bin tone magnitudes: LP attenuation vs cutoff, HP fundamental
//   removal, BP band emphasis, the resonance peak, high-cutoff transparency,
//   stability at the extremes and determinism. The master-filter setter
//   clamps and the per-frame attach guard's no-device path are covered too.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "filter.h"

#include "../common/log.h"
#include "../gxmath/gxmath.h"
#include "../settings.h"

#define FILT_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            gX_TraceLog(gX_LOG_ERROR, "FILTER_TEST FAIL: %s (line %d)", msg, __LINE__); \
            errors++; \
        } \
    } while(0)

#define FILT_TEST_RATE   48000
#define FILT_TEST_FRAMES 8192
static gX_F32 filtBuf[FILT_TEST_FRAMES * 2];

//ai: single-bin DFT magnitude of the left channel — how much of `freq`
//   survived the filter.
static gX_F32 filt_test_tone_mag(const gX_F32 *samples, gX_U32 frames, gX_F32 freq) {
    gX_F64 re = 0.0, im = 0.0;
    gX_F32 w = 2.0f * 3.14159265f * freq / (gX_F32)FILT_TEST_RATE;
    for (gX_U32 i = 0; i < frames; i++) {
        gX_F64 x = samples[i * 2];
        re += x * cos((gX_F64)w * i);
        im += x * sin((gX_F64)w * i);
    }
    return (gX_F32)(2.0 * sqrt(re * re + im * im) / (gX_F64)frames);
}

//ai: run a sine at `freq` through a fresh filter state and return the
//   surviving magnitude (the state resets so the result is deterministic).
static gX_F32 filt_test_response(gX_I32 mode, gX_F32 cutoff, gX_F32 res, gX_F32 freq) {
    gX_Audio_Filter filter;
    memset(&filter, 0, sizeof(filter));
    gX_Audio_Filter_Set(&filter, mode, cutoff, res);
    gX_Audio_Filter_Reset(&filter);
    gX_Audio_Filter_Prepare(&filter, FILT_TEST_RATE);
    for (gX_U32 i = 0; i < FILT_TEST_FRAMES; i++) {
        gX_F32 s = sinf(2.0f * 3.14159265f * freq * (gX_F32)i / (gX_F32)FILT_TEST_RATE);
        filtBuf[i * 2] = s;
        filtBuf[i * 2 + 1] = s;
    }
    gX_Audio_Filter_Process(&filter, filtBuf, FILT_TEST_FRAMES, FILT_TEST_RATE);
    return filt_test_tone_mag(filtBuf, FILT_TEST_FRAMES, freq);
}

//ai: the full-range promise — a 6 kHz tone through a lowpass at cutoff 0.1
//   (2.4 kHz) is crushed; at cutoff 0.6 (14.4 kHz) it passes. The old core
//   could not distinguish any cutoff above ~0.17.
static gX_I32 filt_test_full_range_lp(void) {
    gX_I32 errors = 0;
    gX_F32 dark = filt_test_response(GX_AUDIO_FILTER_LP, 0.1f, 0.0f, 6000.0f);
    gX_F32 open = filt_test_response(GX_AUDIO_FILTER_LP, 0.6f, 0.0f, 6000.0f);
    FILT_TEST_ASSERT(dark < open * 0.2f, "lowpass follows the cutoff across the range");
    FILT_TEST_ASSERT(open > 0.5f, "high cutoff passes the tone");

    //ai: two high cutoffs must still be distinguishable (the old clamp
    //   collapsed everything above ~0.17 onto one filter). The probe tone
    //   sits ABOVE both cutoffs: an SVF lowpass passes ~unity at its own
    //   cutoff (Q = 1), so comparing tones near fc proves nothing.
    gX_F32 mid = filt_test_response(GX_AUDIO_FILTER_LP, 0.25f, 0.0f, 12000.0f);
    gX_F32 high = filt_test_response(GX_AUDIO_FILTER_LP, 0.75f, 0.0f, 12000.0f);
    FILT_TEST_ASSERT(mid < 0.6f, "mid cutoff attenuates above it");
    FILT_TEST_ASSERT(high > mid * 2.0f, "cutoffs above the old clamp still matter");
    return errors;
}

//ai: highpass removes the fundamental the lowpass keeps.
static gX_I32 filt_test_highpass(void) {
    gX_I32 errors = 0;
    gX_F32 lp = filt_test_response(GX_AUDIO_FILTER_LP, 0.1f, 0.0f, 220.0f);
    gX_F32 hp = filt_test_response(GX_AUDIO_FILTER_HP, 0.1f, 0.0f, 220.0f);
    FILT_TEST_ASSERT(lp > 0.5f, "lowpass keeps the fundamental");
    FILT_TEST_ASSERT(hp < lp * 0.05f, "highpass removes the fundamental");
    FILT_TEST_ASSERT(filt_test_response(GX_AUDIO_FILTER_HP, 0.1f, 0.0f, 12000.0f) > 0.5f,
        "highpass keeps the top end");
    return errors;
}

//ai: bandpass emphasizes its center over the sides.
static gX_I32 filt_test_bandpass(void) {
    gX_I32 errors = 0;
    gX_F32 low = filt_test_response(GX_AUDIO_FILTER_BP, 0.1f, 0.5f, 200.0f);
    gX_F32 centre = filt_test_response(GX_AUDIO_FILTER_BP, 0.1f, 0.5f, 2400.0f);
    gX_F32 high = filt_test_response(GX_AUDIO_FILTER_BP, 0.1f, 0.5f, 15000.0f);
    FILT_TEST_ASSERT(centre > low * 3.0f && centre > high * 3.0f,
        "bandpass emphasizes its center");
    return errors;
}

//ai: resonance lifts the region around the cutoff.
static gX_I32 filt_test_resonance_peak(void) {
    gX_I32 errors = 0;
    gX_F32 flat = filt_test_response(GX_AUDIO_FILTER_LP, 0.1f, 0.0f, 2400.0f);
    gX_F32 peak = filt_test_response(GX_AUDIO_FILTER_LP, 0.1f, 0.9f, 2400.0f);
    FILT_TEST_ASSERT(peak > flat * 2.0f, "resonance lifts the cutoff region");
    return errors;
}

//ai: extremes stay finite and bounded (cutoff at the top, resonance at 1).
static gX_I32 filt_test_extremes_bounded(void) {
    gX_I32 errors = 0;
    gX_Audio_Filter filter;
    memset(&filter, 0, sizeof(filter));
    gX_Audio_Filter_Set(&filter, GX_AUDIO_FILTER_LP, 0.99f, 1.0f);
    gX_Audio_Filter_Reset(&filter);
    gX_Audio_Filter_Prepare(&filter, FILT_TEST_RATE);
    for (gX_U32 block = 0; block < 8; block++) {
        for (gX_U32 i = 0; i < 1024; i++) {
            //ai: a fast alternating square drives the worst case
            gX_F32 s = ((i + block) & 1) ? 0.9f : -0.9f;
            filtBuf[i * 2] = s;
            filtBuf[i * 2 + 1] = s;
        }
        gX_Audio_Filter_Process(&filter, filtBuf, 1024, FILT_TEST_RATE);
        for (gX_U32 i = 0; i < 1024 * 2; i++) {
            if (!isfinite(filtBuf[i]) || fabsf(filtBuf[i]) > 20.0f) {
                FILT_TEST_ASSERT(0, "extreme filter state stays finite and bounded");
                return errors;
            }
        }
    }

    gX_Audio_Filter_Set(&filter, GX_AUDIO_FILTER_HP, 0.001f, 1.0f);
    gX_Audio_Filter_Reset(&filter);
    gX_Audio_Filter_Prepare(&filter, FILT_TEST_RATE);
    for (gX_U32 i = 0; i < 1024; i++) {
        filtBuf[i * 2] = sinf((gX_F32)i * 0.7f) * 0.9f;
        filtBuf[i * 2 + 1] = filtBuf[i * 2];
    }
    gX_Audio_Filter_Process(&filter, filtBuf, 1024, FILT_TEST_RATE);
    for (gX_U32 i = 0; i < 1024 * 2; i++) {
        if (!isfinite(filtBuf[i])) {
            FILT_TEST_ASSERT(0, "low-cutoff highpass stays finite");
            return errors;
        }
    }
    return errors;
}

//ai: setter clamps + determinism.
static gX_I32 filt_test_setters_and_determinism(void) {
    gX_I32 errors = 0;
    gX_Audio_Filter filter;
    memset(&filter, 0, sizeof(filter));
    gX_Audio_Filter_Set(&filter, 9, 2.0f, -1.0f);
    FILT_TEST_ASSERT(filter.mode == GX_AUDIO_FILTER_BP, "mode clamps");
    FILT_TEST_ASSERT(filter.cutoff <= 0.99f && filter.cutoff >= 0.001f, "cutoff clamps");
    FILT_TEST_ASSERT(filter.resonance == 0.0f, "resonance clamps");

    static gX_F32 a[FILT_TEST_FRAMES * 2];
    static gX_F32 b[FILT_TEST_FRAMES * 2];
    for (gX_I32 run = 0; run < 2; run++) {
        gX_Audio_Filter f;
        memset(&f, 0, sizeof(f));
        gX_Audio_Filter_Set(&f, GX_AUDIO_FILTER_LP, 0.3f, 0.6f);
        gX_Audio_Filter_Reset(&f);
        gX_F32 *dst = run ? b : a;
        gX_F32 phase = 0.0f;
        for (gX_U32 i = 0; i < FILT_TEST_FRAMES; i++) {
            phase += 0.03f;
            if (phase >= 1.0f) phase -= 1.0f;
            gX_F32 s = phase * 2.0f - 1.0f;
            dst[i * 2] = s;
            dst[i * 2 + 1] = s;
        }
        gX_Audio_Filter_Process(&f, dst, FILT_TEST_FRAMES, FILT_TEST_RATE);
    }
    FILT_TEST_ASSERT(memcmp(a, b, sizeof(a)) == 0, "filter render is deterministic");
    return errors;
}

//ai: master-filter surface — clamps, getters and the guard's no-device path
//   (Update must not attach or crash without an audio device).
static gX_I32 filt_test_master_surface(void) {
    gX_I32 errors = 0;
    gX_Audio_MasterFilter_Init();
    FILT_TEST_ASSERT(!gX_Audio_MasterFilter_IsEnabled(), "master filter off by default");
    gX_Audio_MasterFilter_Set(1, GX_AUDIO_FILTER_HP, 0.25f, 0.5f);
    FILT_TEST_ASSERT(gX_Audio_MasterFilter_IsEnabled(), "master filter enables");
    FILT_TEST_ASSERT(gX_Audio_MasterFilter_GetMode() == GX_AUDIO_FILTER_HP, "master mode");
    FILT_TEST_ASSERT(fabsf(gX_Audio_MasterFilter_GetCutoff() - 0.25f) < 1e-6f, "master cutoff");
    FILT_TEST_ASSERT(fabsf(gX_Audio_MasterFilter_GetResonance() - 0.5f) < 1e-6f, "master resonance");
    gX_Audio_MasterFilter_Update();
    gX_Audio_MasterFilter_Set(0, GX_AUDIO_FILTER_LP, 0.5f, 0.0f);
    FILT_TEST_ASSERT(!gX_Audio_MasterFilter_IsEnabled(), "master filter disables");
    gX_Audio_MasterFilter_Update();
    gX_Audio_MasterFilter_Unload();
    return errors;
}

//ai: settings → master filter live sync — the persisted fields must reach
//   the filter on the per-frame apply, and a failed apply leaves the engine
//   state alone (the setters clamp).
static gX_I32 filt_test_settings_sync(void) {
    gX_I32 errors = 0;
    gX_Settings_Init();
    gX_Settings_Audio_Apply();
    FILT_TEST_ASSERT(!gX_Audio_MasterFilter_IsEnabled(),
        "master filter off after the settings sync");

    settings.masterFilterEnabled = 1;
    settings.masterFilterMode = GX_AUDIO_FILTER_HP;
    settings.masterFilterCutoff = 0.4f;
    settings.masterFilterResonance = 0.3f;
    gX_Settings_Audio_Apply();
    FILT_TEST_ASSERT(gX_Audio_MasterFilter_IsEnabled(), "settings enable the master filter");
    FILT_TEST_ASSERT(gX_Audio_MasterFilter_GetMode() == GX_AUDIO_FILTER_HP,
        "settings push the filter mode");
    FILT_TEST_ASSERT(fabsf(gX_Audio_MasterFilter_GetCutoff() - 0.4f) < 1e-5f,
        "settings push the filter cutoff");
    FILT_TEST_ASSERT(fabsf(gX_Audio_MasterFilter_GetResonance() - 0.3f) < 1e-5f,
        "settings push the filter resonance");

    //ai: restore the defaults for the suites that follow.
    settings.masterFilterEnabled = 0;
    settings.masterFilterMode = GX_AUDIO_FILTER_LP;
    settings.masterFilterCutoff = 0.8f;
    settings.masterFilterResonance = 0.0f;
    gX_Settings_Audio_Apply();
    return errors;
}

gX_I32 gX_Tests_AudioFilter(gX_I0) {
    gX_I32 errors = 0;
    errors += filt_test_full_range_lp();
    errors += filt_test_highpass();
    errors += filt_test_bandpass();
    errors += filt_test_resonance_peak();
    errors += filt_test_extremes_bounded();
    errors += filt_test_setters_and_determinism();
    errors += filt_test_master_surface();
    errors += filt_test_settings_sync();
    errors += filt_test_full_range_lp();
    errors += filt_test_highpass();
    errors += filt_test_bandpass();
    errors += filt_test_resonance_peak();
    errors += filt_test_extremes_bounded();
    errors += filt_test_setters_and_determinism();
    errors += filt_test_master_surface();
    gX_TraceLog(gX_LOG_INFO, "FILTER_TESTS: %d errors", errors);
    return errors;
}
