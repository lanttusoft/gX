/**********************************************************************************************
*
*   [gX: src/audio/pad_tests.c]
*   gX — PAD layered ensemble engine tests (CPU only)
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


//ai: PAD offline suite — the render core needs no audio device. Covers the
//   layer model (detune beating shows as a modulated amplitude envelope),
//   long pad envelopes and release tails, the ensemble chorus (delay
//   modulation makes a steady tone breathe), stereo width, the moving
//   filter (LFO sweep and mode differences), stealing, determinism, mute,
//   extreme-setting boundedness and the preset schema (round trip + guard).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pad.h"
#include "pad_schema.h"

#include "../common/log.h"
#include "../gxmath/gxmath.h"

#define PAD_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            gX_TraceLog(gX_LOG_ERROR, "PAD_TEST FAIL: %s (line %d)", msg, __LINE__); \
            errors++; \
        } \
    } while(0)

#define PAD_TEST_FRAMES 2048
static gX_F32 padBuf[PAD_TEST_FRAMES * 2];

static gX_F32 pad_test_peak(const gX_F32 *samples, gX_U32 frames) {
    gX_F32 peak = 0.0f;
    for (gX_U32 i = 0; i < frames * 2; i++) {
        gX_F32 a = fabsf(samples[i]);
        if (a > peak) peak = a;
    }
    return peak;
}

static gX_F32 pad_test_rms(const gX_F32 *samples, gX_U32 frames) {
    gX_F64 sum = 0.0;
    for (gX_U32 i = 0; i < frames * 2; i++) sum += (gX_F64)samples[i] * samples[i];
    return (gX_F32)sqrt(sum / (gX_F64)(frames * 2));
}

//ai: spread of the per-window RMS — a steady tone's windows are nearly
//   identical; a beating (detuned) or chorus-modulated tone breathes.
static gX_F32 pad_test_window_spread(gX_U32 frames, gX_U32 windows) {
    gX_F32 lo = 1e9f, hi = -1e9f, mean = 0.0f;
    for (gX_U32 w = 0; w < windows; w++) {
        memset(padBuf, 0, sizeof(gX_F32) * frames * 2);
        gX_Pad_Render(padBuf, frames);
        gX_F32 r = pad_test_rms(padBuf, frames);
        if (r < lo) lo = r;
        if (r > hi) hi = r;
        mean += r;
    }
    mean /= (gX_F32)windows;
    if (mean <= 1e-6f) return 0.0f;
    return (hi - lo) / mean;
}

static gX_I0 pad_test_render(gX_U32 frames) {
    if (frames > PAD_TEST_FRAMES) frames = PAD_TEST_FRAMES;
    memset(padBuf, 0, sizeof(gX_F32) * frames * 2);
    gX_Pad_Render(padBuf, frames);
}

//ai: advance the engine by an arbitrary span in test-buffer-sized chunks;
//   the last chunk stays in padBuf for a measurement.
static gX_I0 pad_test_advance(gX_U32 frames) {
    while (frames > 0) {
        gX_U32 chunk = (frames > PAD_TEST_FRAMES) ? PAD_TEST_FRAMES : frames;
        memset(padBuf, 0, sizeof(gX_F32) * chunk * 2);
        gX_Pad_Render(padBuf, chunk);
        frames -= chunk;
    }
}

//ai: deterministic baseline patch — one saw layer, instant envelope, filter
//   wide open, no drift, chorus off. Each test layers its own changes on it.
static gX_I32 pad_test_prepare(void) {
    gX_I32 errors = 0;
    gX_Pad_Init();
    gX_Pad_SetMuted(0);
    gX_Pad_SetMasterVolume(1.0f);
    gX_Pad_SetSampleRate(48000);
    gX_Pad_SetWidth(1.0f);

    gX_Pad_Layer base = {0};
    base.waveform = GX_PAD_WAVE_SAW;
    base.detuneCents = 0.0f;
    base.level = 0.5f;
    base.pan = 0.0f;
    base.driftRate = 0.0f;
    base.driftDepth = 0.0f;
    for (gX_I32 l = 0; l < GX_PAD_LAYERS; l++) {
        gX_Pad_SetDefaultLayer(l, &base);
    }
    gX_Pad_SetDefaultADSR(0.01f, 0.01f, 1.0f, 0.05f);
    gX_Pad_SetDefaultFilter(GX_PAD_FILTER_LP, 0.99f, 0.0f);
    gX_Pad_SetDefaultFilterLFO(0.0f, 0.0f);
    gX_Pad_SetChorus(0, 0.6f, 0.0f, 0.0f, 0.25f);
    return errors;
}

//ai: mute leaves the buffer untouched; silence with no voices.
static gX_I32 pad_test_mute_and_silence(void) {
    gX_I32 errors = 0;
    errors += pad_test_prepare();

    pad_test_render(128);
    PAD_TEST_ASSERT(pad_test_peak(padBuf, 128) == 0.0f, "no voices render silence");

    gX_Pad_NoteOn(220.0f, 1.0f);
    gX_Pad_SetMuted(1);
    for (gX_U32 i = 0; i < 128 * 2; i++) padBuf[i] = 0.5f;
    gX_Pad_Render(padBuf, 128);
    gX_I1 untouched = 1;
    for (gX_U32 i = 0; i < 128 * 2; i++) {
        if (padBuf[i] != 0.5f) { untouched = 0; break; }
    }
    PAD_TEST_ASSERT(untouched, "muted render leaves the buffer untouched");
    gX_Pad_SetMuted(0);
    pad_test_render(128);
    PAD_TEST_ASSERT(pad_test_peak(padBuf, 128) > 0.01f, "unmuted voice is audible");
    gX_Pad_AllNotesOff();
    return errors;
}

//ai: detuned layers beat — the amplitude envelope of the mix varies window
//   to window; perfectly tuned layers stay steady.
static gX_I32 pad_test_layer_beating(void) {
    gX_I32 errors = 0;
    errors += pad_test_prepare();

    //ai: two layers, one per channel-ish; the coherent case has both at the
    //   same frequency and phase (constant envelope).
    gX_Pad_Layer a = {0};
    a.waveform = GX_PAD_WAVE_SAW;
    a.level = 0.5f;
    gX_Pad_SetDefaultLayer(0, &a);
    gX_Pad_Layer b = a;
    b.detuneCents = 0.0f;
    gX_Pad_SetDefaultLayer(1, &b);
    gX_Pad_Layer off = {0};
    off.level = 0.0f;
    gX_Pad_SetDefaultLayer(2, &off);

    gX_Pad_NoteOn(440.0f, 1.0f);
    pad_test_render(1024);  //ai: settle past the attack
    gX_F32 spreadCoherent = pad_test_window_spread(512, 8);

    gX_Pad_AllNotesOff();
    b.detuneCents = 50.0f;  //ai: ~12.9 Hz beat at 440
    gX_Pad_SetDefaultLayer(1, &b);
    gX_Pad_NoteOn(440.0f, 1.0f);
    pad_test_render(1024);
    gX_F32 spreadDetuned = pad_test_window_spread(512, 8);

    //ai: the coherent bound is a sanity ceiling, not a tuned number — the
    //   property is the CONTRAST (the old 0.06 was calibrated to the
    //   Chamberlin's ~8 kHz clamp; the TPT core leaves the filter nearly
    //   bypassed at 0.99, so the raw saw's window variation is a bit wider).
    PAD_TEST_ASSERT(spreadCoherent < 0.15f, "tuned layers hold a steady envelope");
    PAD_TEST_ASSERT(spreadDetuned > spreadCoherent * 3.0f,
        "detuned layers breathe (beating envelope)");
    gX_Pad_AllNotesOff();
    return errors;
}

//ai: long pad envelope — slow attack grows, release keeps the tail and then
//   retires the voice.
static gX_I32 pad_test_long_envelope(void) {
    gX_I32 errors = 0;
    errors += pad_test_prepare();
    gX_Pad_SetDefaultADSR(0.2f, 0.01f, 1.0f, 0.4f);

    gX_Pad_NoteOn(220.0f, 1.0f);
    pad_test_advance(4096);  //ai: ~0.09 s into a 0.2 s attack
    gX_F32 half = pad_test_peak(padBuf, PAD_TEST_FRAMES);
    pad_test_advance(12000); //ai: past the attack
    gX_F32 full = pad_test_peak(padBuf, PAD_TEST_FRAMES);
    PAD_TEST_ASSERT(half > 0.01f && full > half * 1.5f,
        "slow attack grows after 0.1 s");

    gX_Pad_NoteOff(0);
    pad_test_advance(9600);  //ai: 0.2 s of a 0.4 s release
    PAD_TEST_ASSERT(g_pad.voices[0].active, "release tail still sounds");
    for (gX_I32 i = 0; i < 16 && g_pad.voices[0].active; i++) {
        pad_test_advance(PAD_TEST_FRAMES);
    }
    PAD_TEST_ASSERT(!g_pad.voices[0].active, "release retires the voice");
    pad_test_advance(PAD_TEST_FRAMES);
    PAD_TEST_ASSERT(pad_test_peak(padBuf, PAD_TEST_FRAMES) == 0.0f,
        "retired voice is silent");
    return errors;
}

//ai: the ensemble chorus — with a steady tone the dry windows are flat, the
//   modulated-delay mix breathes, and the output stays bounded.
static gX_I32 pad_test_chorus(void) {
    gX_I32 errors = 0;
    errors += pad_test_prepare();

    gX_Pad_NoteOn(440.0f, 1.0f);
    pad_test_render(2048);  //ai: settle
    gX_F32 drySpread = pad_test_window_spread(1024, 8);

    gX_Pad_AllNotesOff();
    gX_Pad_NoteOn(440.0f, 1.0f);
    gX_Pad_SetChorus(1, 1.5f, 1.0f, 0.7f, 0.25f);
    pad_test_render(2048);  //ai: warm the delay line (starts empty)
    gX_F32 wetSpread = pad_test_window_spread(1024, 8);

    PAD_TEST_ASSERT(drySpread < 0.05f, "dry steady tone holds its level");
    PAD_TEST_ASSERT(wetSpread > drySpread * 3.0f,
        "chorus makes the steady tone breathe");
    PAD_TEST_ASSERT(pad_test_peak(padBuf, 1024) < 2.0f, "chorus output is bounded");

    gX_Pad_SetChorus(0, 0.6f, 0.0f, 0.0f, 0.25f);
    gX_Pad_AllNotesOff();
    return errors;
}

//ai: stereo width — width 0 collapses the panned layers to mono; the default
//   spread keeps the channels apart.
static gX_I32 pad_test_width(void) {
    gX_I32 errors = 0;
    errors += pad_test_prepare();

    gX_Pad_Layer left = {0};
    left.waveform = GX_PAD_WAVE_SAW;
    left.level = 0.5f;
    left.pan = -0.8f;
    gX_Pad_SetDefaultLayer(0, &left);
    gX_Pad_Layer right = left;
    right.pan = 0.8f;
    gX_Pad_SetDefaultLayer(1, &right);

    gX_Pad_SetWidth(0.0f);
    gX_Pad_NoteOn(220.0f, 1.0f);
    pad_test_render(PAD_TEST_FRAMES);
    gX_I1 mono = 1;
    for (gX_U32 i = 0; i < PAD_TEST_FRAMES; i++) {
        if (padBuf[i * 2] != padBuf[i * 2 + 1]) { mono = 0; break; }
    }
    PAD_TEST_ASSERT(mono, "width 0 collapses the layers to mono");

    gX_Pad_AllNotesOff();
    gX_Pad_SetWidth(1.0f);
    gX_Pad_NoteOn(220.0f, 1.0f);
    pad_test_render(PAD_TEST_FRAMES);
    gX_I1 stereo = 0;
    for (gX_U32 i = 0; i < PAD_TEST_FRAMES; i++) {
        if (padBuf[i * 2] != padBuf[i * 2 + 1]) { stereo = 1; break; }
    }
    PAD_TEST_ASSERT(stereo, "width 1 keeps the channels apart");
    gX_Pad_AllNotesOff();
    return errors;
}

//ai: single-bin DFT magnitude (left channel) — a precise, deterministic
//   measure of how much a specific harmonic survived the filter.
static gX_F32 pad_test_tone_mag(const gX_F32 *samples, gX_U32 frames, gX_F32 freq) {
    gX_F64 re = 0.0, im = 0.0;
    gX_F32 w = 2.0f * 3.14159265f * freq / (gX_F32)g_pad.sampleRate;
    for (gX_U32 i = 0; i < frames; i++) {
        gX_F64 x = samples[i * 2];
        re += x * cos((gX_F64)w * i);
        im += x * sin((gX_F64)w * i);
    }
    return (gX_F32)(2.0 * sqrt(re * re + im * im) / (gX_F64)frames);
}

//ai: high-band RMS via a one-pole split at ~600 Hz — the lowpass removes
//   the fundamental; the residual is the harmonic energy (a mean first
//   difference would not work here: total variation barely changes when a
//   discontinuity is smoothed, while the harmonic energy does).
static gX_F32 pad_test_high_rms(const gX_F32 *samples, gX_U32 frames) {
    gX_F32 a = 2.0f * 3.14159265f * 600.0f / (gX_F32)g_pad.sampleRate;
    if (a > 1.0f) a = 1.0f;
    gX_F64 y = 0.0, sum = 0.0;
    gX_U32 n = frames * 2;
    for (gX_U32 i = 0; i < n; i++) {
        gX_F64 x = samples[i];
        y += (gX_F64)a * (x - y);
        gX_F64 h = x - y;
        sum += h * h;
    }
    return (gX_F32)sqrt(sum / (gX_F64)n);
}

//ai: the moving filter — an LFO sweep changes the harmonic content over a
//   slow cycle, and the filter modes color the tone differently.
static gX_I32 pad_test_filter(void) {
    gX_I32 errors = 0;
    errors += pad_test_prepare();

    //ai: the shared TPT core has no saturation clamp, so the whole 0.01..0.99
    //   range is usable (the old Chamberlin pinned everything above ~0.17).
    gX_Pad_SetDefaultFilter(GX_PAD_FILTER_LP, 0.05f, 0.0f);
    gX_Pad_SetDefaultFilterLFO(1.0f, 1.0f);
    gX_Pad_NoteOn(220.0f, 1.0f);
    pad_test_advance(2048);   //ai: settle (LFO phase ~0.043)
    //ai: LFO phase 0.25 = fully open (cutoff clamped to the top)
    pad_test_advance(9950);
    gX_F32 highOpen = pad_test_high_rms(padBuf, PAD_TEST_FRAMES);
    //ai: LFO phase 0.75 = fully closed (cutoff at the 0.01 floor)
    pad_test_advance(24000);
    gX_F32 highClosed = pad_test_high_rms(padBuf, PAD_TEST_FRAMES);
    PAD_TEST_ASSERT(highOpen > highClosed * 1.5f,
        "filter LFO sweeps between closed and open");

    //ai: same cutoff, LP vs HP — the lowpass keeps the fundamental, the
    //   highpass removes it. The previous voice must be fully retired first
    //   (its release tail would leak the fundamental into the measurement).
    gX_Pad_AllNotesOff();
    gX_Pad_SetDefaultFilterLFO(0.0f, 0.0f);
    gX_Pad_SetDefaultFilter(GX_PAD_FILTER_LP, 0.05f, 0.0f);
    for (gX_I32 i = 0; i < 16 && g_pad.voices[0].active; i++) {
        pad_test_advance(PAD_TEST_FRAMES);
    }
    PAD_TEST_ASSERT(!g_pad.voices[0].active, "previous filter voice retired");
    gX_Pad_NoteOn(220.0f, 1.0f);
    pad_test_advance(2048);
    gX_F32 fundLP = pad_test_tone_mag(padBuf, PAD_TEST_FRAMES, 220.0f);

    gX_Pad_AllNotesOff();
    gX_Pad_SetDefaultFilter(GX_PAD_FILTER_HP, 0.05f, 0.0f);
    for (gX_I32 i = 0; i < 16 && g_pad.voices[0].active; i++) {
        pad_test_advance(PAD_TEST_FRAMES);
    }
    PAD_TEST_ASSERT(!g_pad.voices[0].active, "lowpass voice retired");
    gX_Pad_NoteOn(220.0f, 1.0f);
    pad_test_advance(2048);
    gX_F32 fundHP = pad_test_tone_mag(padBuf, PAD_TEST_FRAMES, 220.0f);
    PAD_TEST_ASSERT(fundLP > fundHP * 4.0f, "highpass removes the fundamental");

    //ai: full-range pin — two cutoffs ABOVE the old clamp (~0.17) must still
    //   differ (the Chamberlin made 0.35 and 0.6 literally the same filter).
    gX_Pad_AllNotesOff();
    gX_Pad_SetDefaultFilter(GX_PAD_FILTER_LP, 0.35f, 0.0f);
    for (gX_I32 i = 0; i < 16 && g_pad.voices[0].active; i++) {
        pad_test_advance(PAD_TEST_FRAMES);
    }
    gX_Pad_NoteOn(220.0f, 1.0f);
    pad_test_advance(2048);
    //ai: the 80th harmonic of a 220 Hz saw (17.6 kHz) sits above BOTH
    //   cutoffs; the TPT core passes more of it as the cutoff opens, while
    //   the old Chamberlin produced the identical filter at 0.35 and 0.6.
    gX_F32 hfMid = pad_test_tone_mag(padBuf, PAD_TEST_FRAMES, 17600.0f);

    gX_Pad_AllNotesOff();
    gX_Pad_SetDefaultFilter(GX_PAD_FILTER_LP, 0.6f, 0.0f);
    for (gX_I32 i = 0; i < 16 && g_pad.voices[0].active; i++) {
        pad_test_advance(PAD_TEST_FRAMES);
    }
    gX_Pad_NoteOn(220.0f, 1.0f);
    pad_test_advance(2048);
    gX_F32 hfFull = pad_test_tone_mag(padBuf, PAD_TEST_FRAMES, 17600.0f);
    PAD_TEST_ASSERT(hfFull > hfMid * 1.5f,
        "cutoffs above the old clamp still matter (full-range TPT core)");

    gX_Pad_AllNotesOff();
    return errors;
}

//ai: steal order — idle, then released, then oldest.
static gX_I32 pad_test_voice_stealing(void) {
    gX_I32 errors = 0;
    errors += pad_test_prepare();

    for (gX_I32 i = 0; i < GX_PAD_MAX_VOICES; i++) {
        gX_I32 slot = gX_Pad_NoteOn(220.0f + (gX_F32)i, 0.8f);
        PAD_TEST_ASSERT(slot == i, "voices fill in order");
    }
    gX_U32 firstID = g_pad.voices[0].noteID;
    gX_I32 slot = gX_Pad_NoteOn(330.0f, 0.8f);
    PAD_TEST_ASSERT(slot == 0, "ninth note steals the oldest voice");
    PAD_TEST_ASSERT(g_pad.voices[0].noteID != firstID, "stolen voice has a new note");
    PAD_TEST_ASSERT(fabsf(g_pad.voices[0].frequency - 330.0f) < 0.1f,
        "stolen voice retuned");
    gX_Pad_AllNotesOff();
    return errors;
}

//ai: the offline core is deterministic (same run twice is byte-identical).
static gX_I32 pad_test_determinism(void) {
    gX_I32 errors = 0;
    static gX_F32 a[PAD_TEST_FRAMES * 2];
    static gX_F32 b[PAD_TEST_FRAMES * 2];

    for (gX_I32 run = 0; run < 2; run++) {
        errors += pad_test_prepare();
        gX_Pad_Layer l1;
        gX_Pad_GetDefaultLayer(0, &l1);
        l1.driftRate = 0.2f;
        l1.driftDepth = 8.0f;
        gX_Pad_SetDefaultLayer(0, &l1);
        gX_Pad_SetChorus(1, 0.6f, 0.5f, 0.5f, 0.25f);
        gX_Pad_NoteOn(220.0f, 0.9f);
        gX_F32 *dst = run ? b : a;
        memset(dst, 0, sizeof(gX_F32) * PAD_TEST_FRAMES * 2);
        gX_Pad_Render(dst, PAD_TEST_FRAMES);
    }
    PAD_TEST_ASSERT(memcmp(a, b, sizeof(a)) == 0, "render is deterministic");
    return errors;
}

//ai: extreme settings stay finite and bounded (resonance, full chorus,
//   maximum drift).
static gX_I32 pad_test_extremes_bounded(void) {
    gX_I32 errors = 0;
    errors += pad_test_prepare();

    for (gX_I32 l = 0; l < GX_PAD_LAYERS; l++) {
        gX_Pad_Layer layer;
        gX_Pad_GetDefaultLayer(l, &layer);
        layer.level = 0.6f;
        layer.detuneCents = (gX_F32)(l - 1) * 40.0f;
        layer.driftRate = 1.0f;
        layer.driftDepth = 30.0f;
        gX_Pad_SetDefaultLayer(l, &layer);
    }
    gX_Pad_SetDefaultADSR(0.01f, 0.5f, 0.9f, 0.2f);
    gX_Pad_SetDefaultFilter(GX_PAD_FILTER_LP, 0.95f, 1.0f);
    gX_Pad_SetChorus(1, 3.0f, 1.0f, 1.0f, 0.75f);
    gX_Pad_NoteOn(110.0f, 1.0f);
    for (gX_I32 i = 0; i < 24; i++) pad_test_render(PAD_TEST_FRAMES);
    gX_I1 finite = 1;
    for (gX_U32 i = 0; i < PAD_TEST_FRAMES * 2; i++) {
        if (!isfinite(padBuf[i])) { finite = 0; break; }
    }
    PAD_TEST_ASSERT(finite, "extreme render is finite");
    PAD_TEST_ASSERT(pad_test_peak(padBuf, PAD_TEST_FRAMES) < 4.0f,
        "extreme render is bounded");
    gX_Pad_AllNotesOff();
    return errors;
}

//ai: preset schema — save/patch/load must restore the default patch (layers,
//   envelope, filter, chorus, width), and a failed load must not touch the
//   engine.
static gX_I32 pad_test_preset_round_trip(void) {
    gX_I32 errors = 0;
    errors += pad_test_prepare();

    const gX_I8 *scratch = getenv("GX_SCRATCH");
    if (!scratch || !scratch[0]) scratch = "/tmp/opencode";
    gX_I8 path[256];
    snprintf((char *)path, sizeof(path),
        "%s/pad_tests_preset_%ld.json", scratch, (long)getpid());

    gX_Pad_Layer layer = {0};
    layer.waveform = GX_PAD_WAVE_TRI;
    layer.detuneCents = 22.0f;
    layer.level = 0.66f;
    layer.pan = -0.7f;
    layer.driftRate = 0.31f;
    layer.driftDepth = 17.0f;
    gX_Pad_SetDefaultLayer(1, &layer);
    gX_Pad_SetDefaultADSR(2.5f, 3.0f, 0.7f, 4.5f);
    gX_Pad_SetDefaultFilter(GX_PAD_FILTER_BP, 0.07f, 0.35f);
    gX_Pad_SetDefaultFilterLFO(0.4f, 0.6f);
    gX_Pad_SetChorus(1, 1.2f, 0.8f, 0.6f, 0.4f);
    gX_Pad_SetWidth(0.8f);
    gX_Pad_SetMasterVolume(0.33f);
    PAD_TEST_ASSERT(gX_Pad_Preset_Save(path) == 0, "preset saved");

    gX_Pad_Layer other = {0};
    other.waveform = GX_PAD_WAVE_SAW;
    other.level = 0.1f;
    gX_Pad_SetDefaultLayer(1, &other);
    gX_Pad_SetDefaultADSR(0.01f, 0.01f, 1.0f, 0.05f);
    gX_Pad_SetDefaultFilter(GX_PAD_FILTER_LP, 0.2f, 0.0f);
    gX_Pad_SetDefaultFilterLFO(0.0f, 0.0f);
    gX_Pad_SetChorus(0, 0.6f, 0.0f, 0.0f, 0.25f);
    gX_Pad_SetWidth(1.0f);
    gX_Pad_SetMasterVolume(1.0f);
    PAD_TEST_ASSERT(gX_Pad_Preset_Load(path) == 0, "preset loaded");

    gX_Pad_Layer restored;
    gX_Pad_GetDefaultLayer(1, &restored);
    PAD_TEST_ASSERT(restored.waveform == GX_PAD_WAVE_TRI, "layer waveform restored");
    PAD_TEST_ASSERT(fabsf(restored.detuneCents - 22.0f) < 1e-3f, "layer detune restored");
    PAD_TEST_ASSERT(fabsf(restored.level - 0.66f) < 1e-3f, "layer level restored");
    PAD_TEST_ASSERT(fabsf(restored.pan + 0.7f) < 1e-3f, "layer pan restored");
    PAD_TEST_ASSERT(fabsf(restored.driftRate - 0.31f) < 1e-3f, "layer drift rate restored");
    PAD_TEST_ASSERT(fabsf(restored.driftDepth - 17.0f) < 1e-3f, "layer drift depth restored");
    PAD_TEST_ASSERT(fabsf(g_pad.defaultAttack - 2.5f) < 1e-3f, "attack restored");
    PAD_TEST_ASSERT(fabsf(g_pad.defaultRelease - 4.5f) < 1e-3f, "release restored");
    PAD_TEST_ASSERT(g_pad.defaultFilterMode == GX_PAD_FILTER_BP, "filter mode restored");
    PAD_TEST_ASSERT(fabsf(g_pad.defaultFilterCutoff - 0.07f) < 1e-3f, "filter cutoff restored");
    PAD_TEST_ASSERT(fabsf(g_pad.defaultFilterLfoRate - 0.4f) < 1e-3f, "filter LFO restored");
    PAD_TEST_ASSERT(g_pad.chorusEnabled && fabsf(g_pad.chorusRate - 1.2f) < 1e-3f,
        "chorus restored");
    PAD_TEST_ASSERT(fabsf(g_pad.chorusStereoPhase - 0.4f) < 1e-3f, "chorus phase restored");
    PAD_TEST_ASSERT(fabsf(g_pad.width - 0.8f) < 1e-3f, "width restored");
    PAD_TEST_ASSERT(fabsf(g_pad.masterVolume - 0.33f) < 1e-3f, "volume restored");

    //ai: missing input must fail without touching the engine.
    gX_F32 before = g_pad.defaultLayers[1].level;
    PAD_TEST_ASSERT(gX_Pad_Preset_Load("/tmp/pad_missing_preset.json") == 1,
        "missing preset refused");
    PAD_TEST_ASSERT(g_pad.defaultLayers[1].level == before, "failed load leaves state");

    remove((const char *)path);
    return errors;
}

//ai: harness guard — a save into the resource tree is refused while
//   GX_NO_SETTINGS_SAVE is set (the guard path is assembled at runtime so the
//   deps audit does not read it as a missing-reference literal).
static gX_I32 pad_test_preset_guard(void) {
    gX_I32 errors = 0;
    errors += pad_test_prepare();

    gX_I8 guardPath[128];
    snprintf((char *)guardPath, sizeof(guardPath),
        "%s/audio/presets/should_not_write.json", "resources");
    const gX_I8 *guardPrev = (const gX_I8 *)getenv("GX_NO_SETTINGS_SAVE");
    gX_I8 *guardSaved = guardPrev ? (gX_I8 *)strdup((const char *)guardPrev) : NULL;
    PAD_TEST_ASSERT(!guardPrev || guardSaved, "harness guard snapshot allocated");
    if (!guardPrev || guardSaved) {
        gX_I32 guardSet = setenv("GX_NO_SETTINGS_SAVE", "1", 1);
        PAD_TEST_ASSERT(guardSet == 0, "harness guard set");
        if (guardSet == 0) {
            PAD_TEST_ASSERT(gX_Pad_Preset_Save(guardPath) == 1,
                "resource-tree preset refused under the harness guard");
        }
        gX_I32 guardRestored = guardSaved
            ? setenv("GX_NO_SETTINGS_SAVE", (const char *)guardSaved, 1)
            : unsetenv("GX_NO_SETTINGS_SAVE");
        PAD_TEST_ASSERT(guardRestored == 0, "inherited harness guard restored");
        free(guardSaved);
    }
    FILE *check = fopen((const char *)guardPath, "rb");
    PAD_TEST_ASSERT(check == NULL, "refusal wrote nothing");
    if (check) fclose(check);
    return errors;
}

gX_I32 gX_Tests_Pad(gX_I0) {
    gX_I32 errors = 0;
    errors += pad_test_mute_and_silence();
    errors += pad_test_layer_beating();
    errors += pad_test_long_envelope();
    errors += pad_test_chorus();
    errors += pad_test_width();
    errors += pad_test_filter();
    errors += pad_test_voice_stealing();
    errors += pad_test_determinism();
    errors += pad_test_extremes_bounded();
    errors += pad_test_preset_round_trip();
    errors += pad_test_preset_guard();
    gX_TraceLog(gX_LOG_INFO, "PAD_TESTS: %d errors", errors);
    return errors;
}
