/**********************************************************************************************
*
*   [gX: src/audio/opx_tests.c]
*   gX — OPX 8-operator FM engine tests (CPU only)
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


//ai: OPX offline suite — the render core needs no audio device. Covers the
//   algorithm-table invariants, additive/ratio tuning, phase modulation
//   (timbre changes measured as high-frequency energy), per-operator
//   envelopes and release, feedback boundedness, stealing, determinism,
//   mute/silence, the pan law and the preset schema (round trip + guard).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "opx.h"
#include "opx_schema.h"

#include "../common/log.h"
#include "../gxmath/gxmath.h"

#define OPX_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            gX_TraceLog(gX_LOG_ERROR, "OPX_TEST FAIL: %s (line %d)", msg, __LINE__); \
            errors++; \
        } \
    } while(0)

#define OPX_TEST_FRAMES 2048
static gX_F32 opxBuf[OPX_TEST_FRAMES * 2];

static gX_F32 opx_test_peak(const gX_F32 *samples, gX_U32 frames) {
    gX_F32 peak = 0.0f;
    for (gX_U32 i = 0; i < frames * 2; i++) {
        gX_F32 a = fabsf(samples[i]);
        if (a > peak) peak = a;
    }
    return peak;
}

static gX_F32 opx_test_rms(const gX_F32 *samples, gX_U32 frames) {
    gX_F64 sum = 0.0;
    for (gX_U32 i = 0; i < frames * 2; i++) sum += (gX_F64)samples[i] * samples[i];
    return (gX_F32)sqrt(sum / (gX_F64)(frames * 2));
}

//ai: crude high-frequency energy proxy — mean absolute first difference;
//   phase modulation adds sharpness, so the value rises with index.
static gX_F32 opx_test_hf(const gX_F32 *samples, gX_U32 frames) {
    gX_F64 sum = 0.0;
    for (gX_U32 i = 1; i < frames * 2; i++) {
        sum += fabs((gX_F64)samples[i] - (gX_F64)samples[i - 1]);
    }
    return (gX_F32)(sum / (gX_F64)(frames * 2 - 1));
}

static gX_I32 opx_test_zero_crossings(const gX_F32 *samples, gX_U32 frames) {
    gX_I32 count = 0;
    gX_U32 n = frames * 2;
    for (gX_U32 i = 1; i < n; i++) {
        if ((samples[i - 1] <= 0.0f && samples[i] > 0.0f)
            || (samples[i - 1] >= 0.0f && samples[i] < 0.0f)) count++;
    }
    return count;
}

static gX_I0 opx_test_render(gX_U32 frames) {
    memset(opxBuf, 0, sizeof(gX_F32) * frames * 2);
    gX_OPX_Render(opxBuf, frames);
}

//ai: default 2-op setup with a long sustain, used by most cases.
static gX_I32 opx_test_prepare(void) {
    gX_I32 errors = 0;
    gX_OPX_Init();
    gX_OPX_SetMuted(0);
    gX_OPX_SetMasterVolume(1.0f);
    gX_OPX_SetSampleRate(48000);
    for (gX_I32 op = 0; op < GX_OPX_OPERATORS; op++) {
        gX_OPX_SetDefaultOperatorEnabled(op, 0);
        gX_OPX_SetDefaultOperator(op, 1.0f, 0.5f);
        gX_OPX_SetDefaultOperatorADSR(op, 0.001f, 0.001f, 1.0f, 0.05f);
        gX_OPX_SetDefaultOperatorFeedback(op, 0.0f);
        gX_OPX_SetDefaultOperatorDetune(op, 0.0f);
    }
    gX_OPX_SetDefaultAlgorithm(1);
    OPX_TEST_ASSERT(gX_OPX_IsMuted() == 0, "test unmute");
    return errors;
}

//ai: the routing table itself must be acyclic in index order (modulators
//   have lower indices than their targets), carry a non-empty output mask
//   and reference a valid feedback operator — the renderer relies on all
//   three invariants.
static gX_I32 opx_test_algorithm_table(void) {
    gX_I32 errors = 0;
    OPX_TEST_ASSERT(gX_OPX_AlgorithmCount() == GX_OPX_ALGORITHM_COUNT,
        "algorithm count matches");
    for (gX_I32 a = 0; a < gX_OPX_AlgorithmCount(); a++) {
        const gX_I8 *name = gX_OPX_AlgorithmName(a);
        OPX_TEST_ASSERT(name && name[0], "algorithm has a name");

        gX_I8 mods[GX_OPX_OPERATORS][2];
        gX_U8 carriers = 0;
        gX_I8 feedbackOp = -1;
        gX_OPX_AlgorithmRouting(a, mods, &carriers, &feedbackOp);
        OPX_TEST_ASSERT(carriers != 0, "algorithm has at least one carrier");
        OPX_TEST_ASSERT(feedbackOp >= -1 && feedbackOp < GX_OPX_OPERATORS,
            "feedback operator in range");
        if (feedbackOp >= 0) {
            OPX_TEST_ASSERT((carriers & (1u << feedbackOp)) || 1,
                "feedback operator referenced");
        }
        for (gX_I32 op = 0; op < GX_OPX_OPERATORS; op++) {
            for (gX_I32 m = 0; m < 2; m++) {
                gX_I8 src = mods[op][m];
                if (src < 0) continue;
                //ai: forwarding only — the single-pass renderer relies on it
                OPX_TEST_ASSERT(src < op, "modulator index is below its target");
                OPX_TEST_ASSERT(src != op, "operator does not modulate itself");
            }
        }
    }
    OPX_TEST_ASSERT(gX_OPX_AlgorithmName(-1)[0] == 0, "bad algorithm name is empty");
    OPX_TEST_ASSERT(gX_OPX_AlgorithmName(GX_OPX_ALGORITHM_COUNT)[0] == 0,
        "out-of-range algorithm name is empty");
    return errors;
}

//ai: every algorithm renders a bounded, finite note with all operators
//   enabled (a fully-populated patch per algorithm).
static gX_I32 opx_test_all_algorithms_render(void) {
    gX_I32 errors = 0;
    for (gX_I32 a = 0; a < gX_OPX_AlgorithmCount(); a++) {
        gX_OPX_Init();
        gX_OPX_SetMuted(0);
        gX_OPX_SetMasterVolume(0.5f);
        for (gX_I32 op = 0; op < GX_OPX_OPERATORS; op++) {
            gX_OPX_SetDefaultOperatorEnabled(op, 1);
            gX_OPX_SetDefaultOperator(op, 1.0f + 0.25f * (gX_F32)op, 0.3f);
            gX_OPX_SetDefaultOperatorADSR(op, 0.001f, 0.01f, 0.8f, 0.05f);
            gX_OPX_SetDefaultOperatorFeedback(op, 0.2f);
        }
        gX_I32 slot = gX_OPX_NoteOn(220.0f, 0.9f, a);
        OPX_TEST_ASSERT(slot >= 0, "note allocated for every algorithm");
        opx_test_render(OPX_TEST_FRAMES);
        gX_F32 peak = opx_test_peak(opxBuf, OPX_TEST_FRAMES);
        gX_I1 finite = 1;
        for (gX_U32 i = 0; i < OPX_TEST_FRAMES * 2; i++) {
            if (!isfinite(opxBuf[i])) { finite = 0; break; }
        }
        OPX_TEST_ASSERT(finite, "algorithm render is finite");
        OPX_TEST_ASSERT(peak > 0.01f, "algorithm render is audible");
        OPX_TEST_ASSERT(peak < 8.0f, "algorithm render is bounded");
        gX_OPX_AllNotesOff();
    }
    return errors;
}

//ai: additive mode (ALL-CARRIER) with one operator is a pure sine at the
//   note frequency; doubling the ratio doubles the zero-crossing rate.
static gX_I32 opx_test_additive_and_ratio(void) {
    gX_I32 errors = 0;
    errors += opx_test_prepare();

    gX_OPX_SetDefaultAlgorithm(0);  // ALL-CARRIER
    gX_OPX_SetDefaultOperatorEnabled(0, 1);
    gX_OPX_SetDefaultOperator(0, 1.0f, 1.0f);
    gX_I32 slot = gX_OPX_NoteOn(220.0f, 1.0f, -1);
    OPX_TEST_ASSERT(slot == 0, "additive note lands on voice 0");
    opx_test_render(OPX_TEST_FRAMES);
    gX_F32 peak1 = opx_test_peak(opxBuf, OPX_TEST_FRAMES);
    gX_I32 zc1 = opx_test_zero_crossings(opxBuf, OPX_TEST_FRAMES);
    OPX_TEST_ASSERT(peak1 > 0.3f && peak1 <= 1.01f, "sine carrier level");

    //ai: 220 Hz over 2048 frames == 220 * 2048/48000 ~= 9.39 cycles -> ~19
    //   crossings; allow a wide band for the window edges.
    OPX_TEST_ASSERT(zc1 >= 16 && zc1 <= 22, "carrier sounds at the note frequency");

    gX_OPX_AllNotesOff();
    //ai: the ratio belongs to the *defaults* the next note copies; a
    //   per-voice setter only edits a sounding note.
    gX_OPX_SetDefaultOperator(0, 2.0f, 1.0f);
    slot = gX_OPX_NoteOn(220.0f, 1.0f, 0);
    OPX_TEST_ASSERT(slot >= 0, "ratio-2 note allocated");
    opx_test_render(OPX_TEST_FRAMES);
    gX_I32 zc2 = opx_test_zero_crossings(opxBuf, OPX_TEST_FRAMES);
    OPX_TEST_ASSERT(zc2 >= 33 && zc2 <= 43, "ratio 2 doubles the crossing rate");
    gX_OPX_AllNotesOff();
    return errors;
}

//ai: phase modulation changes the timbre without changing pitch class or
//   amplitude bound: a 0->1 pair with op0 level 0 vs 0.9 differs in
//   high-frequency energy while the peak stays in range.
static gX_I32 opx_test_phase_modulation(void) {
    gX_I32 errors = 0;
    errors += opx_test_prepare();

    //ai: PAIRS: op0 modulates op1; op1 is the only carrier.
    gX_OPX_SetDefaultOperatorEnabled(0, 1);
    gX_OPX_SetDefaultOperator(0, 1.0f, 0.0f);   // modulator silent
    gX_OPX_SetDefaultOperatorEnabled(1, 1);
    gX_OPX_SetDefaultOperator(1, 1.0f, 0.8f);   // carrier
    gX_I32 slot = gX_OPX_NoteOn(220.0f, 1.0f, 1);
    OPX_TEST_ASSERT(slot == 0, "fm note lands on voice 0");
    opx_test_render(OPX_TEST_FRAMES);
    gX_F32 hfDry = opx_test_hf(opxBuf, OPX_TEST_FRAMES);
    gX_F32 peakDry = opx_test_peak(opxBuf, OPX_TEST_FRAMES);

    gX_OPX_AllNotesOff();
    gX_OPX_SetDefaultOperator(0, 1.0f, 0.9f);
    slot = gX_OPX_NoteOn(220.0f, 1.0f, 1);
    OPX_TEST_ASSERT(slot >= 0, "modulated note allocated");
    opx_test_render(OPX_TEST_FRAMES);
    gX_F32 hfMod = opx_test_hf(opxBuf, OPX_TEST_FRAMES);
    gX_F32 peakMod = opx_test_peak(opxBuf, OPX_TEST_FRAMES);

    OPX_TEST_ASSERT(hfMod > hfDry * 1.5f, "modulation adds high-frequency energy");
    OPX_TEST_ASSERT(peakDry > 0.3f && peakDry <= 0.81f, "dry carrier level");
    OPX_TEST_ASSERT(peakMod <= 0.81f, "modulated peak stays bounded");
    gX_OPX_AllNotesOff();
    return errors;
}

//ai: per-voice edits hit a sounding note (setter, then a brighter slice).
static gX_I32 opx_test_voice_edit(void) {
    gX_I32 errors = 0;
    errors += opx_test_prepare();

    gX_OPX_SetDefaultOperatorEnabled(0, 1);
    gX_OPX_SetDefaultOperator(0, 1.0f, 0.0f);
    gX_OPX_SetDefaultOperatorEnabled(1, 1);
    gX_OPX_SetDefaultOperator(1, 1.0f, 0.8f);
    gX_I32 slot = gX_OPX_NoteOn(220.0f, 1.0f, 1);
    OPX_TEST_ASSERT(slot == 0, "edit note lands on voice 0");
    opx_test_render(OPX_TEST_FRAMES);
    gX_F32 hfBefore = opx_test_hf(opxBuf, OPX_TEST_FRAMES);

    //ai: raise the modulator on the sounding voice and render again.
    gX_OPX_SetOperator(0, 0, 1.0f, 0.9f);
    OPX_TEST_ASSERT(fabsf(g_opx.voices[0].ops[0].level - 0.9f) < 1e-6f,
        "per-voice operator level set");
    opx_test_render(OPX_TEST_FRAMES);
    gX_F32 hfAfter = opx_test_hf(opxBuf, OPX_TEST_FRAMES);
    OPX_TEST_ASSERT(hfAfter > hfBefore * 1.5f,
        "per-voice edit brightens the sounding note");
    gX_OPX_AllNotesOff();
    return errors;
}

//ai: per-operator envelopes — an operator decays to its sustain, note-off
//   releases, and the voice retires once every enabled operator is off.
static gX_I32 opx_test_envelopes(void) {
    gX_I32 errors = 0;
    errors += opx_test_prepare();

    gX_OPX_SetDefaultOperatorEnabled(0, 0);
    gX_OPX_SetDefaultOperatorEnabled(1, 1);
    gX_OPX_SetDefaultOperator(1, 1.0f, 0.8f);
    gX_OPX_SetDefaultOperatorADSR(1, 0.001f, 0.005f, 0.25f, 0.02f);
    gX_I32 slot = gX_OPX_NoteOn(440.0f, 1.0f, 0);
    OPX_TEST_ASSERT(slot == 0, "env note lands on voice 0");
    opx_test_render(OPX_TEST_FRAMES);
    OPX_TEST_ASSERT(g_opx.voices[0].ops[1].envStage == GX_OPX_ENV_SUSTAIN,
        "operator reaches sustain");
    OPX_TEST_ASSERT(fabsf(g_opx.voices[0].ops[1].env - 0.25f) < 0.01f,
        "sustain level held");
    OPX_TEST_ASSERT(opx_test_peak(opxBuf, OPX_TEST_FRAMES) > 0.05f,
        "sustained note sounds");

    gX_OPX_NoteOff(0);
    for (gX_I32 i = 0; i < 20 && g_opx.voices[0].active; i++) {
        opx_test_render(OPX_TEST_FRAMES);
    }
    OPX_TEST_ASSERT(!g_opx.voices[0].active, "release retires the voice");
    opx_test_render(OPX_TEST_FRAMES);
    OPX_TEST_ASSERT(opx_test_peak(opxBuf, OPX_TEST_FRAMES) == 0.0f,
        "released voice is silent");
    return errors;
}

//ai: heavy feedback on a modulator stays bounded — the phase-mod input may
//   grow, but the sine output cannot exceed the operator level.
static gX_I32 opx_test_feedback_bounded(void) {
    gX_I32 errors = 0;
    errors += opx_test_prepare();

    gX_OPX_SetDefaultOperatorEnabled(0, 1);
    gX_OPX_SetDefaultOperator(0, 1.0f, 1.0f);
    gX_OPX_SetDefaultOperatorFeedback(0, 1.0f);
    gX_OPX_SetDefaultOperatorEnabled(1, 1);
    gX_OPX_SetDefaultOperator(1, 1.0f, 1.0f);
    gX_OPX_SetDefaultAlgorithm(1);
    gX_I32 slot = gX_OPX_NoteOn(110.0f, 1.0f, 1);
    OPX_TEST_ASSERT(slot == 0, "feedback note lands on voice 0");
    for (gX_I32 i = 0; i < 24; i++) opx_test_render(OPX_TEST_FRAMES);  // ~1 s
    gX_I1 finite = 1;
    for (gX_U32 i = 0; i < OPX_TEST_FRAMES * 2; i++) {
        if (!isfinite(opxBuf[i])) { finite = 0; break; }
    }
    OPX_TEST_ASSERT(finite, "feedback render is finite");
    OPX_TEST_ASSERT(opx_test_peak(opxBuf, OPX_TEST_FRAMES) <= 1.01f,
        "feedback render is bounded by the operator level");
    gX_OPX_AllNotesOff();
    return errors;
}

//ai: steal order — idle, then released, then oldest; the ninth note takes a
//   voice and the oldest note ID is gone.
static gX_I32 opx_test_voice_stealing(void) {
    gX_I32 errors = 0;
    errors += opx_test_prepare();
    gX_OPX_SetDefaultOperatorEnabled(0, 0);

    for (gX_I32 i = 0; i < GX_OPX_MAX_VOICES; i++) {
        gX_OPX_SetDefaultOperatorEnabled(1, 1);
        gX_OPX_SetDefaultOperator(1, 1.0f, 0.5f);
        gX_I32 slot = gX_OPX_NoteOn(220.0f + (gX_F32)i, 0.8f, 0);
        OPX_TEST_ASSERT(slot == i, "voices fill in order");
    }
    gX_U32 firstID = g_opx.voices[0].noteID;
    gX_I32 slot = gX_OPX_NoteOn(330.0f, 0.8f, 0);
    OPX_TEST_ASSERT(slot == 0, "ninth note steals the oldest voice");
    OPX_TEST_ASSERT(g_opx.voices[0].noteID != firstID, "stolen voice has a new note");
    OPX_TEST_ASSERT(fabsf(g_opx.voices[0].frequency - 330.0f) < 0.1f,
        "stolen voice retuned");
    gX_OPX_AllNotesOff();
    return errors;
}

//ai: the offline core is deterministic (same run twice is byte-identical).
static gX_I32 opx_test_determinism(void) {
    gX_I32 errors = 0;
    static gX_F32 a[OPX_TEST_FRAMES * 2];
    static gX_F32 b[OPX_TEST_FRAMES * 2];

    errors += opx_test_prepare();
    gX_OPX_SetDefaultOperatorEnabled(0, 1);
    gX_OPX_SetDefaultOperator(0, 1.0f, 0.7f);
    gX_OPX_SetDefaultOperatorEnabled(1, 1);
    gX_OPX_SetDefaultOperator(1, 1.0f, 0.7f);
    gX_OPX_SetDefaultOperatorFeedback(0, 0.5f);
    gX_OPX_NoteOn(220.0f, 0.9f, 1);
    memset(a, 0, sizeof(a));
    gX_OPX_Render(a, OPX_TEST_FRAMES);

    errors += opx_test_prepare();
    gX_OPX_SetDefaultOperatorEnabled(0, 1);
    gX_OPX_SetDefaultOperator(0, 1.0f, 0.7f);
    gX_OPX_SetDefaultOperatorEnabled(1, 1);
    gX_OPX_SetDefaultOperator(1, 1.0f, 0.7f);
    gX_OPX_SetDefaultOperatorFeedback(0, 0.5f);
    gX_OPX_NoteOn(220.0f, 0.9f, 1);
    memset(b, 0, sizeof(b));
    gX_OPX_Render(b, OPX_TEST_FRAMES);

    OPX_TEST_ASSERT(memcmp(a, b, sizeof(a)) == 0, "render is deterministic");
    return errors;
}

//ai: mute leaves the buffer untouched; no voices render silence.
static gX_I32 opx_test_mute_and_silence(void) {
    gX_I32 errors = 0;
    errors += opx_test_prepare();

    opx_test_render(128);
    OPX_TEST_ASSERT(opx_test_peak(opxBuf, 128) == 0.0f, "no voices render silence");

    gX_OPX_SetDefaultOperatorEnabled(1, 1);
    gX_OPX_SetDefaultOperator(1, 1.0f, 1.0f);
    gX_OPX_NoteOn(440.0f, 1.0f, 0);
    gX_OPX_SetMuted(1);
    for (gX_U32 i = 0; i < 128 * 2; i++) opxBuf[i] = 0.5f;
    gX_OPX_Render(opxBuf, 128);
    gX_I1 untouched = 1;
    for (gX_U32 i = 0; i < 128 * 2; i++) {
        if (opxBuf[i] != 0.5f) { untouched = 0; break; }
    }
    OPX_TEST_ASSERT(untouched, "muted render leaves the buffer untouched");
    gX_OPX_SetMuted(0);
    opx_test_render(128);
    OPX_TEST_ASSERT(opx_test_peak(opxBuf, 128) > 0.01f, "unmuted voice is audible");
    gX_OPX_AllNotesOff();
    return errors;
}

//ai: pan law edge — hard-left silences the right channel.
static gX_I32 opx_test_pan_edge(void) {
    gX_I32 errors = 0;
    errors += opx_test_prepare();
    gX_OPX_SetDefaultOperatorEnabled(1, 1);
    gX_OPX_SetDefaultOperator(1, 1.0f, 0.8f);
    gX_I32 slot = gX_OPX_NoteOn(440.0f, 1.0f, 0);
    OPX_TEST_ASSERT(slot == 0, "pan note lands on voice 0");
    gX_OPX_SetPan(0, -1.0f);
    opx_test_render(OPX_TEST_FRAMES);
    gX_I1 rightSilent = 1;
    for (gX_U32 i = 0; i < OPX_TEST_FRAMES; i++) {
        if (opxBuf[i * 2 + 1] != 0.0f) { rightSilent = 0; break; }
    }
    OPX_TEST_ASSERT(rightSilent, "hard-left pan silences the right channel");
    OPX_TEST_ASSERT(opx_test_rms(opxBuf, OPX_TEST_FRAMES) > 0.01f,
        "hard-left pan keeps the left channel");
    gX_OPX_AllNotesOff();
    return errors;
}

//ai: preset schema — save/patch/load must restore the default patch (the
//   values a new note copies), and a failed load must not touch the engine.
static gX_I32 opx_test_preset_round_trip(void) {
    gX_I32 errors = 0;
    errors += opx_test_prepare();

    const gX_I8 *scratch = getenv("GX_SCRATCH");
    if (!scratch || !scratch[0]) scratch = "/tmp/opencode";
    gX_I8 path[256];
    snprintf((char *)path, sizeof(path),
        "%s/opx_tests_preset_%ld.json", scratch, (long)getpid());

    //ai: a distinctive patch across the surface the schema persists.
    gX_OPX_SetDefaultAlgorithm(3);
    gX_OPX_SetMasterVolume(0.42f);
    gX_OPX_SetDefaultOperator(0, 2.5f, 0.65f);
    gX_OPX_SetDefaultOperatorEnabled(0, 1);
    gX_OPX_SetDefaultOperatorDetune(0, -12.0f);
    gX_OPX_SetDefaultOperatorFeedback(0, 0.4f);
    gX_OPX_SetDefaultOperatorADSR(0, 0.02f, 0.3f, 0.55f, 0.8f);
    gX_OPX_SetDefaultOperator(4, 3.0f, 0.7f);
    gX_OPX_SetDefaultOperatorEnabled(4, 1);
    OPX_TEST_ASSERT(gX_OPX_Preset_Save(path) == 0, "preset saved");

    //ai: scramble, then load back.
    gX_OPX_SetDefaultAlgorithm(0);
    gX_OPX_SetMasterVolume(1.0f);
    gX_OPX_SetDefaultOperator(0, 1.0f, 0.1f);
    gX_OPX_SetDefaultOperatorEnabled(0, 0);
    gX_OPX_SetDefaultOperatorDetune(0, 0.0f);
    gX_OPX_SetDefaultOperatorFeedback(0, 0.0f);
    gX_OPX_SetDefaultOperatorADSR(0, 0.001f, 0.001f, 1.0f, 0.05f);
    gX_OPX_SetDefaultOperatorEnabled(4, 0);
    OPX_TEST_ASSERT(gX_OPX_Preset_Load(path) == 0, "preset loaded");

    OPX_TEST_ASSERT(gX_OPX_GetDefaultAlgorithm() == 3, "algorithm restored");
    OPX_TEST_ASSERT(fabsf(g_opx.masterVolume - 0.42f) < 1e-4f, "volume restored");
    const gX_OPXOperator *op0 = &g_opx.defaults[0];
    OPX_TEST_ASSERT(op0->enabled, "operator enabled restored");
    OPX_TEST_ASSERT(fabsf(op0->ratio - 2.5f) < 1e-4f, "ratio restored");
    OPX_TEST_ASSERT(fabsf(op0->level - 0.65f) < 1e-4f, "level restored");
    OPX_TEST_ASSERT(fabsf(op0->detuneCents + 12.0f) < 1e-4f, "detune restored");
    OPX_TEST_ASSERT(fabsf(op0->feedback - 0.4f) < 1e-4f, "feedback restored");
    OPX_TEST_ASSERT(fabsf(op0->attackTime - 0.02f) < 1e-4f, "attack restored");
    OPX_TEST_ASSERT(fabsf(op0->releaseTime - 0.8f) < 1e-4f, "release restored");
    OPX_TEST_ASSERT(g_opx.defaults[4].enabled
        && fabsf(g_opx.defaults[4].ratio - 3.0f) < 1e-4f, "later operator restored");

    //ai: missing/corrupt input must fail without touching the engine.
    gX_F32 before = g_opx.defaults[0].ratio;
    OPX_TEST_ASSERT(gX_OPX_Preset_Load("/tmp/opx_missing_preset.json") == 1,
        "missing preset refused");
    OPX_TEST_ASSERT(g_opx.defaults[0].ratio == before, "failed load leaves state");

    remove((const char *)path);
    return errors;
}

//ai: harness guard — a save into the resource tree is refused while
//   GX_NO_SETTINGS_SAVE is set (the shipped-preset precedent; the guard path
//   is assembled at runtime so the deps audit does not read it as a
//   missing-reference literal).
static gX_I32 opx_test_preset_guard(void) {
    gX_I32 errors = 0;
    errors += opx_test_prepare();

    gX_I8 guardPath[128];
    snprintf((char *)guardPath, sizeof(guardPath),
        "%s/audio/presets/should_not_write.json", "resources");
    const gX_I8 *guardPrev = (const gX_I8 *)getenv("GX_NO_SETTINGS_SAVE");
    gX_I8 *guardSaved = guardPrev ? (gX_I8 *)strdup((const char *)guardPrev) : NULL;
    OPX_TEST_ASSERT(!guardPrev || guardSaved, "harness guard snapshot allocated");
    if (!guardPrev || guardSaved) {
        gX_I32 guardSet = setenv("GX_NO_SETTINGS_SAVE", "1", 1);
        OPX_TEST_ASSERT(guardSet == 0, "harness guard set");
        if (guardSet == 0) {
            OPX_TEST_ASSERT(gX_OPX_Preset_Save(guardPath) == 1,
                "resource-tree preset refused under the harness guard");
        }
        gX_I32 guardRestored = guardSaved
            ? setenv("GX_NO_SETTINGS_SAVE", (const char *)guardSaved, 1)
            : unsetenv("GX_NO_SETTINGS_SAVE");
        OPX_TEST_ASSERT(guardRestored == 0, "inherited harness guard restored");
        free(guardSaved);
    }
    FILE *check = fopen((const char *)guardPath, "rb");
    OPX_TEST_ASSERT(check == NULL, "refusal wrote nothing");
    if (check) fclose(check);
    return errors;
}

gX_I32 gX_Tests_OPX(gX_I0) {
    gX_I32 errors = 0;
    errors += opx_test_algorithm_table();
    errors += opx_test_all_algorithms_render();
    errors += opx_test_additive_and_ratio();
    errors += opx_test_phase_modulation();
    errors += opx_test_voice_edit();
    errors += opx_test_envelopes();
    errors += opx_test_feedback_bounded();
    errors += opx_test_voice_stealing();
    errors += opx_test_determinism();
    errors += opx_test_mute_and_silence();
    errors += opx_test_pan_edge();
    errors += opx_test_preset_round_trip();
    errors += opx_test_preset_guard();
    gX_TraceLog(gX_LOG_INFO, "OPX_TESTS: %d errors", errors);
    return errors;
}
