/**********************************************************************************************
*
*   [gX: src/audio/opx.c]
*   gX — OPX: 8-operator phase-modulation FM engine
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


//ai: OPX engine — see opx.h for the model. The render core is a single
//   forward pass per voice: operators are evaluated in index order and the
//   algorithm table only ever routes a lower index into a higher one (the
//   table test pins that invariant), so each operator's modulation inputs
//   are already computed in the same sample. Self-feedback is the one
//   intentional one-sample delay.
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "opx.h"

#include "../common/log.h"
#include "../gxmath/gxmath.h"
//ai: mixer — OPX output rides the synth bus and the global playback gates.
#include "../audio.h"

#include "../wrappers/wrappers.h"

//opencode: raylib audio processor API (same pattern as synth.c)
#include "raylib.h"

gX_OPX g_opx;

//ai: fixed algorithm table — DX7-style routing data. mods[op][m] is the
//   m-th modulator feeding operator op (-1 = none); carriers is the output
//   bitmask; feedbackOp is the single self-feedback operator (-1 = none).
typedef struct gX_OPXAlgorithm {
    const gX_I8 *name;
    gX_I8 mods[GX_OPX_OPERATORS][2];
    gX_U8 carriers;
    gX_I8 feedbackOp;
} gX_OPXAlgorithm;

#define OPX_NO_MOD (-1)
static const gX_OPXAlgorithm g_opxAlgorithms[GX_OPX_ALGORITHM_COUNT] = {
    //ai: 0 — additive sine bank (all eight operators sound)
    { "ALL-CARRIER",
      {{OPX_NO_MOD, OPX_NO_MOD}, {OPX_NO_MOD, OPX_NO_MOD},
       {OPX_NO_MOD, OPX_NO_MOD}, {OPX_NO_MOD, OPX_NO_MOD},
       {OPX_NO_MOD, OPX_NO_MOD}, {OPX_NO_MOD, OPX_NO_MOD},
       {OPX_NO_MOD, OPX_NO_MOD}, {OPX_NO_MOD, OPX_NO_MOD}},
      0xFF, -1 },
    //ai: 1 — four 2-op pairs (0->1, 2->3, 4->5, 6->7)
    { "PAIRS",
      {{OPX_NO_MOD, OPX_NO_MOD}, {0, OPX_NO_MOD},
       {OPX_NO_MOD, OPX_NO_MOD}, {2, OPX_NO_MOD},
       {OPX_NO_MOD, OPX_NO_MOD}, {4, OPX_NO_MOD},
       {OPX_NO_MOD, OPX_NO_MOD}, {6, OPX_NO_MOD}},
      0xAA, -1 },
    //ai: 2 — two 4-op stacks (0->1->2->3, 4->5->6->7)
    { "STACKS",
      {{OPX_NO_MOD, OPX_NO_MOD}, {0, OPX_NO_MOD},
       {1, OPX_NO_MOD}, {2, OPX_NO_MOD},
       {OPX_NO_MOD, OPX_NO_MOD}, {4, OPX_NO_MOD},
       {5, OPX_NO_MOD}, {6, OPX_NO_MOD}},
      0x88, -1 },
    //ai: 3 — one 8-op cascade (0->...->7)
    { "CASCADE",
      {{OPX_NO_MOD, OPX_NO_MOD}, {0, OPX_NO_MOD},
       {1, OPX_NO_MOD}, {2, OPX_NO_MOD},
       {3, OPX_NO_MOD}, {4, OPX_NO_MOD},
       {5, OPX_NO_MOD}, {6, OPX_NO_MOD}},
      0x80, -1 },
    //ai: 4 — e-piano shape: 0->1, 2->3->4, 5->6, 7; feedback on 2
    { "EP",
      {{OPX_NO_MOD, OPX_NO_MOD}, {0, OPX_NO_MOD},
       {OPX_NO_MOD, OPX_NO_MOD}, {2, OPX_NO_MOD},
       {3, OPX_NO_MOD}, {OPX_NO_MOD, OPX_NO_MOD},
       {5, OPX_NO_MOD}, {OPX_NO_MOD, OPX_NO_MOD}},
      0xD2, 2 },
    //ai: 5 — bass: three modulated carriers plus 6/7 free; feedback on 0
    { "BASS",
      {{OPX_NO_MOD, OPX_NO_MOD}, {0, OPX_NO_MOD},
       {OPX_NO_MOD, OPX_NO_MOD}, {2, OPX_NO_MOD},
       {OPX_NO_MOD, OPX_NO_MOD}, {4, OPX_NO_MOD},
       {OPX_NO_MOD, OPX_NO_MOD}, {OPX_NO_MOD, OPX_NO_MOD}},
      0xEA, 0 },
    //ai: 6 — bell: four pairs, self-feedback on the last modulator
    { "BELL",
      {{OPX_NO_MOD, OPX_NO_MOD}, {0, OPX_NO_MOD},
       {OPX_NO_MOD, OPX_NO_MOD}, {2, OPX_NO_MOD},
       {OPX_NO_MOD, OPX_NO_MOD}, {4, OPX_NO_MOD},
       {OPX_NO_MOD, OPX_NO_MOD}, {6, OPX_NO_MOD}},
      0xAA, 6 },
    //ai: 7 — complex: two modulators into 2, 2->3; two into 6, 6->7
    { "COMPLEX",
      {{OPX_NO_MOD, OPX_NO_MOD}, {OPX_NO_MOD, OPX_NO_MOD},
       {0, 1}, {2, OPX_NO_MOD},
       {OPX_NO_MOD, OPX_NO_MOD}, {OPX_NO_MOD, OPX_NO_MOD},
       {4, 5}, {6, OPX_NO_MOD}},
      0x88, 2 },
};

//ai: the algorithm routing for a voice (out-of-range reads algorithm 0 so a
//   corrupt preset cannot walk off the table).
static const gX_OPXAlgorithm *gX_OPX_Algorithm(gX_I32 algorithm) {
    if (algorithm < 0 || algorithm >= GX_OPX_ALGORITHM_COUNT) return &g_opxAlgorithms[0];
    return &g_opxAlgorithms[algorithm];
}

gX_I32 gX_OPX_AlgorithmCount(gX_I0) {
    return GX_OPX_ALGORITHM_COUNT;
}

const gX_I8 *gX_OPX_AlgorithmName(gX_I32 algorithm) {
    if (algorithm < 0 || algorithm >= GX_OPX_ALGORITHM_COUNT) return "";
    return g_opxAlgorithms[algorithm].name;
}

gX_I0 gX_OPX_AlgorithmRouting(gX_I32 algorithm, gX_I8 mods[GX_OPX_OPERATORS][2],
    gX_U8 *carriers, gX_I8 *feedbackOp) {
    const gX_OPXAlgorithm *alg = gX_OPX_Algorithm(algorithm);
    if (mods) {
        for (gX_I32 op = 0; op < GX_OPX_OPERATORS; op++) {
            mods[op][0] = alg->mods[op][0];
            mods[op][1] = alg->mods[op][1];
        }
    }
    if (carriers) *carriers = alg->carriers;
    if (feedbackOp) *feedbackOp = alg->feedbackOp;
}

static gX_F32 gX_OPX_Clamp(gX_F32 value, gX_F32 lo, gX_F32 hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

//ai: detune multiplier cached on the operator (powf per sample would be
//   wasteful; detune changes only through the setters).
static gX_F32 gX_OPX_DetuneMul(gX_F32 cents) {
    return powf(2.0f, cents / 1200.0f);
}

//ai: per-sample envelope step (same linear scheme as the demo synth).
static gX_I0 gX_OPX_Operator_StepEnv(gX_OPXOperator *op) {
    gX_F32 sr = (gX_F32)g_opx.sampleRate;
    switch (op->envStage) {
        case GX_OPX_ENV_ATTACK:
            op->env += 1.0f / (op->attackTime * sr + 1.0f);
            if (op->env >= 1.0f) {
                op->env = 1.0f;
                op->envStage = GX_OPX_ENV_DECAY;
            }
            break;
        case GX_OPX_ENV_DECAY: {
            gX_F32 step = (1.0f - op->sustainLevel)
                        / (op->decayTime * sr + 1.0f);
            op->env -= step;
            if (op->env <= op->sustainLevel) {
                op->env = op->sustainLevel;
                op->envStage = GX_OPX_ENV_SUSTAIN;
            }
            break;
        }
        case GX_OPX_ENV_RELEASE:
            op->env -= op->releaseLevel / (op->releaseTime * sr + 1.0f);
            if (op->env <= 0.0f) {
                op->env = 0.0f;
                op->envStage = GX_OPX_ENV_OFF;
            }
            break;
        default:
            break;
    }
}

//ai: offline render core — stereo interleaved float, additive into the
//   buffer; the raylib mixed-processor adapter and the tests share it.
gX_I0 gX_OPX_Render(gX_F32 *samples, gX_U32 frames) {
    //ai: mixer — OPX rides the synth bus; master is raylib's device gain.
    gX_F32 master = g_opx.masterVolume * gX_Audio_BusGain(GX_AUDIO_BUS_SYNTH);
    gX_I1 muted = g_opx.muted;
    gX_F32 invSR = 1.0f / (gX_F32)g_opx.sampleRate;
    gX_F32 twoPi = 2.0f * gX_PI;

    for (gX_U32 i = 0; i < frames; i++) {
        gX_F32 mixL = 0.0f;
        gX_F32 mixR = 0.0f;

        for (gX_U32 v = 0; v < GX_OPX_MAX_VOICES; v++) {
            gX_OPXVoice *voice = &g_opx.voices[v];
            if (!voice->active) continue;

            const gX_OPXAlgorithm *alg = gX_OPX_Algorithm(voice->algorithm);

            //ai: latch note-off into every sounding operator's release
            if (voice->release) {
                for (gX_I32 op = 0; op < GX_OPX_OPERATORS; op++) {
                    gX_OPXOperator *o = &voice->ops[op];
                    if (!o->enabled) continue;
                    if (o->envStage != GX_OPX_ENV_RELEASE
                        && o->envStage != GX_OPX_ENV_OFF) {
                        o->releaseLevel = o->env;
                        o->envStage = GX_OPX_ENV_RELEASE;
                    }
                }
            }

            gX_F32 sound = 0.0f;
            gX_F32 opOut[GX_OPX_OPERATORS];
            for (gX_I32 op = 0; op < GX_OPX_OPERATORS; op++) opOut[op] = 0.0f;

            for (gX_I32 op = 0; op < GX_OPX_OPERATORS; op++) {
                gX_OPXOperator *o = &voice->ops[op];
                if (!o->enabled) {
                    o->prevOut = 0.0f;
                    continue;
                }

                gX_OPX_Operator_StepEnv(o);

                //ai: phase modulation — the algorithm's modulators are
                //   already evaluated this sample (lower indices), plus this
                //   operator's own previous output for self-feedback.
                gX_F32 modIn = 0.0f;
                gX_I8 src0 = alg->mods[op][0];
                gX_I8 src1 = alg->mods[op][1];
                if (src0 >= 0) modIn += opOut[src0];
                if (src1 >= 0) modIn += opOut[src1];
                if (alg->feedbackOp == op && o->feedback > 0.0f) {
                    modIn += o->prevOut * o->feedback;
                }

                gX_F32 out = sinf(twoPi * (o->phase + modIn * GX_OPX_MOD_SCALE));
                out *= o->env * o->level;
                opOut[op] = out;
                o->prevOut = out;

                if (alg->carriers & (1u << op)) sound += out;

                gX_F32 inc = voice->frequency * o->ratio * o->detuneMul * invSR;
                o->phase += inc;
                if (o->phase >= 1.0f) o->phase -= floorf(o->phase);
                if (o->phase < 0.0f) o->phase -= floorf(o->phase);
            }

            sound *= voice->velocity;
            gX_F32 panL = 1.0f - (voice->pan + 1.0f) * 0.5f;
            gX_F32 panR = (voice->pan + 1.0f) * 0.5f;
            mixL += sound * panL;
            mixR += sound * panR;

            //ai: the voice retires once every enabled operator is silent
            gX_I1 allOff = 1;
            for (gX_I32 op = 0; op < GX_OPX_OPERATORS && allOff; op++) {
                if (voice->ops[op].enabled
                    && voice->ops[op].envStage != GX_OPX_ENV_OFF) {
                    allOff = 0;
                }
            }
            if (allOff) voice->active = 0;
        }

        if (!muted) {
            samples[i * 2]     += mixL * master;
            samples[i * 2 + 1] += mixR * master;
        }
    }
}

//ai: raylib mixed-processor adapter — receives the device-rate stereo buffer.
static void gX_OPX_Process(void *buffer, unsigned int frames) {
    gX_OPX_Render((gX_F32 *)buffer, (gX_U32)frames);
}

gX_I0 gX_OPX_Init(gX_I0) {
    memset(&g_opx, 0, sizeof(g_opx));
    g_opx.sampleRate = 48000;
    g_opx.masterVolume = 0.5f;
    g_opx.nextNoteID = 1;
    g_opx.defaultAlgorithm = 1;  //ai: PAIRS, the classic 2-op default

    //ai: default patch — 0 modulates 1 (PAIRS), operations 2..7 off; a
    //   fresh note is a clean 2-op FM tone until the caller shapes it.
    for (gX_I32 op = 0; op < GX_OPX_OPERATORS; op++) {
        gX_OPXOperator *o = &g_opx.defaults[op];
        o->enabled = (op <= 1) ? 1 : 0;
        o->ratio = 1.0f;
        o->level = (op == 0) ? 0.5f : 0.8f;
        o->detuneCents = 0.0f;
        o->detuneMul = 1.0f;
        o->feedback = 0.0f;
        o->attackTime = 0.01f;
        o->decayTime = 0.1f;
        o->sustainLevel = 0.7f;
        o->releaseTime = 0.3f;
    }

    //ai: muted until asked — GX_OPX_MUTE=0 opens the output (the synth's
    //   GX_SYNTH_MUTE precedent); voices and the offline core keep running.
    g_opx.muted = 1;
    const gX_I8 *muteEnv = getenv("GX_OPX_MUTE");
    if (muteEnv) g_opx.muted = (atoi(muteEnv) != 0) ? 1 : 0;

    gX_TraceLog(gX_LOG_NOTICE,
        "OPX: initialised 8-operator FM engine (%d voices, %d algorithms)",
        GX_OPX_MAX_VOICES, GX_OPX_ALGORITHM_COUNT);
    return;
}

gX_I0 gX_OPX_Unload(gX_I0) {
    gX_OPX_AllNotesOff();
    if (g_opx.attached) {
        DetachAudioMixedProcessor(gX_OPX_Process);
        g_opx.attached = 0;
    }
    gX_TraceLog(gX_LOG_NOTICE, "OPX: unloaded");
    return;
}

//ai: lazy mixer attach/detach — same contract as the synth's: the processor
//   joins on the first sounding note and leaves after the voices go quiet
//   (TRACE logs; note-gap flapping is not NOTICE material).
#define GX_OPX_IDLE_DETACH_SECONDS 1.0f

static gX_I0 gX_OPX_MixerAttach(gX_I0) {
    if (g_opx.attached) return;
    if (!IsAudioDeviceReady()) return;
    AttachAudioMixedProcessor(gX_OPX_Process);
    g_opx.attached = 1;
    gX_TraceLog(gX_LOG_TRACE, "OPX: mixer attached");
    return;
}

static gX_I0 gX_OPX_MixerDetach(gX_I0) {
    if (!g_opx.attached) return;
    DetachAudioMixedProcessor(gX_OPX_Process);
    g_opx.attached = 0;
    gX_TraceLog(gX_LOG_TRACE, "OPX: mixer detached (idle)");
    return;
}

gX_I1 gX_OPX_IsAttached(gX_I0) {
    return g_opx.attached;
}

gX_I0 gX_OPX_SetSampleRate(gX_U32 sampleRate) {
    if (sampleRate < 8000 || sampleRate > 192000) return;
    g_opx.sampleRate = sampleRate;
    return;
}

gX_I0 gX_OPX_Update(gX_I0) {
    static gX_F32 idleSeconds = 0.0f;

    gX_F32 dt = (gX_F32)gX_FrameTime_Get();
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;

    gX_I1 active = 0;
    for (gX_U32 i = 0; i < GX_OPX_MAX_VOICES && !active; i++) {
        if (g_opx.voices[i].active) active = 1;
    }

    if (active) {
        idleSeconds = 0.0f;
        gX_OPX_MixerAttach();
    } else if (g_opx.attached) {
        idleSeconds += dt;
        if (idleSeconds >= GX_OPX_IDLE_DETACH_SECONDS) {
            gX_OPX_MixerDetach();
            idleSeconds = 0.0f;
        }
    }
    return;
}

//ai: voice pick — idle first, then the oldest released voice, then the
//   oldest voice overall (the synth's steal order).
static gX_I32 gX_OPX_FindVoice(gX_I0) {
    gX_I32 idle = -1;
    gX_I32 released = -1;
    gX_U32 releasedID = 0xFFFFFFFFu;
    gX_I32 oldest = -1;
    gX_U32 oldestID = 0xFFFFFFFFu;

    for (gX_U32 i = 0; i < GX_OPX_MAX_VOICES; i++) {
        gX_OPXVoice *v = &g_opx.voices[i];
        if (!v->active) return (gX_I32)i;
        if (v->release && v->noteID < releasedID) {
            releasedID = v->noteID;
            released = (gX_I32)i;
        }
        if (v->noteID < oldestID) {
            oldestID = v->noteID;
            oldest = (gX_I32)i;
        }
    }
    if (released >= 0) return released;
    return oldest;
}

gX_I32 gX_OPX_NoteOn(gX_F32 freq, gX_F32 velocity, gX_I32 algorithm) {
    gX_I32 slot = gX_OPX_FindVoice();
    if (slot < 0) return -1;
    gX_OPX_MixerAttach();

    if (algorithm < 0) algorithm = g_opx.defaultAlgorithm;
    if (algorithm >= GX_OPX_ALGORITHM_COUNT) algorithm = GX_OPX_ALGORITHM_COUNT - 1;

    gX_OPXVoice *voice = &g_opx.voices[slot];
    voice->active = 1;
    voice->release = 0;
    voice->frequency = (freq > 0.0f) ? freq : 440.0f;
    voice->velocity = gX_OPX_Clamp(velocity, 0.0f, 1.0f);
    voice->algorithm = algorithm;
    voice->noteID = g_opx.nextNoteID++;

    gX_I32 enabledCount = 0;
    for (gX_I32 op = 0; op < GX_OPX_OPERATORS; op++) {
        const gX_OPXOperator *src = &g_opx.defaults[op];
        gX_OPXOperator *o = &voice->ops[op];
        *o = *src;
        o->phase = 0.0f;
        o->env = 0.0f;
        o->releaseLevel = 0.0f;
        o->prevOut = 0.0f;
        o->detuneMul = gX_OPX_DetuneMul(o->detuneCents);
        o->envStage = o->enabled ? GX_OPX_ENV_ATTACK : GX_OPX_ENV_OFF;
        if (o->enabled) enabledCount++;
    }
    if (enabledCount == 0) voice->active = 0;

    gX_TraceLog(gX_LOG_DEBUG,
        "OPX: noteOn slot=%d freq=%.1f vel=%.2f alg=%d id=%u",
        slot, (double)voice->frequency, (double)voice->velocity,
        algorithm, voice->noteID);
    return slot;
}

gX_I0 gX_OPX_NoteOff(gX_U32 voiceIndex) {
    if (voiceIndex >= GX_OPX_MAX_VOICES) return;
    g_opx.voices[voiceIndex].release = 1;
    return;
}

gX_I0 gX_OPX_NoteOff_ID(gX_U32 noteID) {
    for (gX_U32 i = 0; i < GX_OPX_MAX_VOICES; i++) {
        if (g_opx.voices[i].active && g_opx.voices[i].noteID == noteID) {
            g_opx.voices[i].release = 1;
            return;
        }
    }
    return;
}

gX_I0 gX_OPX_AllNotesOff(gX_I0) {
    for (gX_U32 i = 0; i < GX_OPX_MAX_VOICES; i++) {
        g_opx.voices[i].release = 1;
    }
    return;
}

// ------------------------------------------------------------------
// defaults / per-voice parameters
// ------------------------------------------------------------------

static gX_I0 gX_OPX_Operator_Set(gX_OPXOperator *o, gX_F32 ratio, gX_F32 level) {
    o->ratio = gX_OPX_Clamp(ratio, 0.25f, 16.0f);
    o->level = gX_OPX_Clamp(level, 0.0f, 1.0f);
    return;
}

gX_I0 gX_OPX_SetDefaultOperator(gX_I32 op, gX_F32 ratio, gX_F32 level) {
    if (op < 0 || op >= GX_OPX_OPERATORS) return;
    gX_OPX_Operator_Set(&g_opx.defaults[op], ratio, level);
    return;
}

gX_I0 gX_OPX_SetDefaultOperatorEnabled(gX_I32 op, gX_I1 enabled) {
    if (op < 0 || op >= GX_OPX_OPERATORS) return;
    g_opx.defaults[op].enabled = enabled ? 1 : 0;
    return;
}

//ai: shared ADSR clamp (0.001 s floor keeps the step math finite).
static gX_I0 gX_OPX_Operator_SetADSR(gX_OPXOperator *o,
    gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r) {
    o->attackTime = (a < 0.001f) ? 0.001f : a;
    o->decayTime = (d < 0.001f) ? 0.001f : d;
    o->sustainLevel = gX_OPX_Clamp(s, 0.0f, 1.0f);
    o->releaseTime = (r < 0.001f) ? 0.001f : r;
    return;
}

gX_I0 gX_OPX_SetDefaultOperatorADSR(gX_I32 op, gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r) {
    if (op < 0 || op >= GX_OPX_OPERATORS) return;
    gX_OPX_Operator_SetADSR(&g_opx.defaults[op], a, d, s, r);
    return;
}

gX_I0 gX_OPX_SetDefaultOperatorDetune(gX_I32 op, gX_F32 cents) {
    if (op < 0 || op >= GX_OPX_OPERATORS) return;
    g_opx.defaults[op].detuneCents = gX_OPX_Clamp(cents, -100.0f, 100.0f);
    g_opx.defaults[op].detuneMul = gX_OPX_DetuneMul(g_opx.defaults[op].detuneCents);
    return;
}

gX_I0 gX_OPX_SetDefaultOperatorFeedback(gX_I32 op, gX_F32 amount) {
    if (op < 0 || op >= GX_OPX_OPERATORS) return;
    g_opx.defaults[op].feedback = gX_OPX_Clamp(amount, 0.0f, 1.0f);
    return;
}

gX_I0 gX_OPX_SetDefaultAlgorithm(gX_I32 algorithm) {
    if (algorithm < 0 || algorithm >= GX_OPX_ALGORITHM_COUNT) return;
    g_opx.defaultAlgorithm = algorithm;
    return;
}

gX_I32 gX_OPX_GetDefaultAlgorithm(gX_I0) {
    return g_opx.defaultAlgorithm;
}

static gX_OPXOperator *gX_OPX_VoiceOp(gX_U32 voiceIndex, gX_I32 op) {
    if (voiceIndex >= GX_OPX_MAX_VOICES) return NULL;
    if (op < 0 || op >= GX_OPX_OPERATORS) return NULL;
    return &g_opx.voices[voiceIndex].ops[op];
}

gX_I0 gX_OPX_SetOperator(gX_U32 voiceIndex, gX_I32 op, gX_F32 ratio, gX_F32 level) {
    gX_OPXOperator *o = gX_OPX_VoiceOp(voiceIndex, op);
    if (!o) return;
    gX_OPX_Operator_Set(o, ratio, level);
    return;
}

gX_I0 gX_OPX_SetOperatorEnabled(gX_U32 voiceIndex, gX_I32 op, gX_I1 enabled) {
    gX_OPXOperator *o = gX_OPX_VoiceOp(voiceIndex, op);
    if (!o) return;
    o->enabled = enabled ? 1 : 0;
    if (o->enabled && o->envStage == GX_OPX_ENV_OFF) {
        o->env = 0.0f;
        o->envStage = GX_OPX_ENV_ATTACK;
    }
    return;
}

gX_I0 gX_OPX_SetOperatorADSR(gX_U32 voiceIndex, gX_I32 op,
    gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r) {
    gX_OPXOperator *o = gX_OPX_VoiceOp(voiceIndex, op);
    if (!o) return;
    gX_OPX_Operator_SetADSR(o, a, d, s, r);
    return;
}

gX_I0 gX_OPX_SetOperatorDetune(gX_U32 voiceIndex, gX_I32 op, gX_F32 cents) {
    gX_OPXOperator *o = gX_OPX_VoiceOp(voiceIndex, op);
    if (!o) return;
    o->detuneCents = gX_OPX_Clamp(cents, -100.0f, 100.0f);
    o->detuneMul = gX_OPX_DetuneMul(o->detuneCents);
    return;
}

gX_I0 gX_OPX_SetOperatorFeedback(gX_U32 voiceIndex, gX_I32 op, gX_F32 amount) {
    gX_OPXOperator *o = gX_OPX_VoiceOp(voiceIndex, op);
    if (!o) return;
    o->feedback = gX_OPX_Clamp(amount, 0.0f, 1.0f);
    return;
}

gX_I0 gX_OPX_SetPan(gX_U32 voiceIndex, gX_F32 pan) {
    if (voiceIndex >= GX_OPX_MAX_VOICES) return;
    g_opx.voices[voiceIndex].pan = gX_OPX_Clamp(pan, -1.0f, 1.0f);
    return;
}

gX_OPXVoice *gX_OPX_GetVoice(gX_U32 index) {
    if (index >= GX_OPX_MAX_VOICES) return NULL;
    return &g_opx.voices[index];
}

gX_I0 gX_OPX_SetMasterVolume(gX_F32 volume) {
    g_opx.masterVolume = gX_OPX_Clamp(volume, 0.0f, 2.0f);
    return;
}

gX_I0 gX_OPX_SetMuted(gX_I1 muted) {
    g_opx.muted = muted ? 1 : 0;
    return;
}

gX_I1 gX_OPX_IsMuted(gX_I0) {
    return g_opx.muted;
}
