/**********************************************************************************************
*
*   [gX: src/audio/pad.c]
*   gX — PAD: layered ensemble pad synthesizer
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


//ai: PAD engine — see pad.h for the model. Per voice the three layers sum
//   into a per-voice resonant filter (with its own slow LFO), then all
//   voices feed the engine-level stereo chorus before the master/bus gain.
//   Oscillators are naive saw/triangle (the subtractive classic; the filter
//   handles the top end) and the drift modulation uses a small-angle
//   approximation of 2^(cents/1200) so no powf runs per sample.
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "pad.h"

#include "../common/alloc.h"
#include "../common/log.h"
#include "../gxmath/gxmath.h"
//ai: mixer — PAD output rides the synth bus and the global playback gates.
#include "../audio.h"

#include "../wrappers/wrappers.h"

//opencode: raylib audio processor API (same pattern as synth.c)
#include "raylib.h"

gX_Pad g_pad;

//ai: chorus delay line — heap-lazy (allocated in Init, freed in Unload), so
//   the engine adds no bss.
static gX_F32 *s_chorusBuf;   // 2 * GX_PAD_CHORUS_SIZE, interleaved L/R

//ai: 2^(cents/1200) for small |cents| — the drift range is <= 30 cents, where
//   the linear term is accurate to well under a cent.
static gX_F32 gX_Pad_DetuneMul(gX_F32 cents) {
    return 1.0f + cents * 0.00057762f;
}

static gX_F32 gX_Pad_Clamp(gX_F32 value, gX_F32 lo, gX_F32 hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static gX_F32 gX_Pad_Saw(gX_F32 phase) {
    return 2.0f * phase - 1.0f;
}

static gX_F32 gX_Pad_Triangle(gX_F32 phase) {
    return 4.0f * fabsf(phase - 0.5f) - 1.0f;
}

//ai: per-sample amplitude envelope (linear, same scheme as the other engines;
//   the long times are what make it a pad).
static gX_I0 gX_Pad_Voice_StepEnv(gX_Pad_Voice *voice) {
    gX_F32 sr = (gX_F32)g_pad.sampleRate;
    switch (voice->envStage) {
        case GX_PAD_ENV_ATTACK:
            voice->env += 1.0f / (voice->attackTime * sr + 1.0f);
            if (voice->env >= 1.0f) {
                voice->env = 1.0f;
                voice->envStage = GX_PAD_ENV_DECAY;
            }
            break;
        case GX_PAD_ENV_DECAY: {
            gX_F32 step = (1.0f - voice->sustainLevel)
                        / (voice->decayTime * sr + 1.0f);
            voice->env -= step;
            if (voice->env <= voice->sustainLevel) {
                voice->env = voice->sustainLevel;
                voice->envStage = GX_PAD_ENV_SUSTAIN;
            }
            break;
        }
        case GX_PAD_ENV_RELEASE:
            voice->env -= voice->releaseLevel / (voice->releaseTime * sr + 1.0f);
            if (voice->env <= 0.0f) {
                voice->env = 0.0f;
                voice->envStage = GX_PAD_ENV_OFF;
            }
            break;
        default:
            break;
    }
}

//ai: the voice filter rides the shared TPT/ZDF core (src/audio/filter.h):
//   the full 0..1 cutoff range is usable (the old Chamberlin clamp pinned
//   everything above ~0.17), the stereo state lives inside the core and the
//   slow filter LFO can sweep the whole range. Set is cache-preserving when
//   the values have not moved, so the per-sample path stays cheap.
static gX_F32 gX_Pad_FilterStep(gX_Pad_Voice *voice, gX_I32 channel, gX_F32 input) {
    if (voice->filterMode < 0) return input;
    gX_Audio_Filter_Set(&voice->filter, voice->filterMode,
        voice->filterCutoff, voice->filterRes);
    gX_Audio_Filter_Prepare(&voice->filter, g_pad.sampleRate);
    return gX_Audio_Filter_Step(&voice->filter, channel, input);
}

//ai: offline render core — stereo interleaved float, additive into the
//   buffer; the raylib mixed-processor adapter and the tests share it.
gX_I0 gX_Pad_Render(gX_F32 *samples, gX_U32 frames) {
    //ai: mixer — PAD rides the instrument (synth) bus; master is raylib's
    //   device gain.
    gX_F32 master = g_pad.masterVolume * gX_Audio_BusGain(GX_AUDIO_BUS_SYNTH);
    gX_I1 muted = g_pad.muted;
    gX_F32 invSR = 1.0f / (gX_F32)g_pad.sampleRate;
    gX_F32 twoPi = 2.0f * gX_PI;
    gX_I1 chorusOn = (g_pad.chorusEnabled && s_chorusBuf) ? 1 : 0;
    //ai: chorus geometry — base 12 ms, modulation up to 6 ms; the tap stays
    //   inside the 1024-frame line.
    gX_F32 chorusBase = 0.012f * (gX_F32)g_pad.sampleRate;
    gX_F32 chorusDepth = g_pad.chorusDepth * 0.006f * (gX_F32)g_pad.sampleRate;
    gX_F32 chorusPhaseInc = g_pad.chorusRate * invSR;

    for (gX_U32 i = 0; i < frames; i++) {
        gX_F32 mixL = 0.0f;
        gX_F32 mixR = 0.0f;

        for (gX_U32 v = 0; v < GX_PAD_MAX_VOICES; v++) {
            gX_Pad_Voice *voice = &g_pad.voices[v];
            if (!voice->active) continue;

            if (voice->release && voice->envStage != GX_PAD_ENV_RELEASE
                               && voice->envStage != GX_PAD_ENV_OFF) {
                voice->releaseLevel = voice->env;
                voice->envStage = GX_PAD_ENV_RELEASE;
            }
            gX_Pad_Voice_StepEnv(voice);

            //ai: filter LFO — a slow sweep over the voice cutoff
            gX_F32 cutoff = voice->filterCutoff;
            if (voice->filterLfoRate > 0.0f && voice->filterLfoDepth > 0.0f) {
                gX_F32 lfo = sinf(twoPi * voice->filterLfoPhase);
                cutoff += lfo * voice->filterLfoDepth * 0.3f;
                cutoff = gX_Pad_Clamp(cutoff, 0.01f, 0.99f);
                voice->filterLfoPhase += voice->filterLfoRate * invSR;
                if (voice->filterLfoPhase >= 1.0f) voice->filterLfoPhase -= 1.0f;
            }
            gX_F32 savedCutoff = voice->filterCutoff;
            voice->filterCutoff = cutoff;

            gX_F32 sumL = 0.0f;
            gX_F32 sumR = 0.0f;
            for (gX_I32 l = 0; l < GX_PAD_LAYERS; l++) {
                gX_Pad_Layer *layer = &voice->layers[l];
                if (layer->level <= 0.0f) continue;

                //ai: independent per-layer drift keeps the ensemble moving;
                //   the drift LFO advances at its own rate.
                gX_F32 driftCents = 0.0f;
                if (layer->driftRate > 0.0f && layer->driftDepth > 0.0f) {
                    driftCents = sinf(twoPi * layer->driftPhase) * layer->driftDepth;
                    layer->driftPhase += layer->driftRate * invSR;
                    if (layer->driftPhase >= 1.0f) layer->driftPhase -= 1.0f;
                    if (layer->driftPhase < 0.0f) layer->driftPhase += 1.0f;
                }
                gX_F32 mul = gX_Pad_DetuneMul(layer->detuneCents + driftCents);

                gX_F32 wave = (layer->waveform == GX_PAD_WAVE_TRI)
                    ? gX_Pad_Triangle(layer->phase)
                    : gX_Pad_Saw(layer->phase);
                gX_F32 value = wave * layer->level;

                gX_F32 pan = gX_Pad_Clamp(layer->pan * g_pad.width + voice->pan,
                    -1.0f, 1.0f);
                sumL += value * (1.0f - (pan + 1.0f) * 0.5f);
                sumR += value * ((pan + 1.0f) * 0.5f);

                gX_F32 inc = voice->frequency * mul * invSR;
                layer->phase += inc;
                if (layer->phase >= 1.0f) layer->phase -= floorf(layer->phase);
                if (layer->phase < 0.0f) layer->phase -= floorf(layer->phase);
            }

            gX_F32 filteredL = gX_Pad_FilterStep(voice, 0, sumL);
            gX_F32 filteredR = gX_Pad_FilterStep(voice, 1, sumR);
            voice->filterCutoff = savedCutoff;

            gX_F32 amp = voice->env * voice->velocity;
            mixL += filteredL * amp;
            mixR += filteredR * amp;

            if (voice->envStage == GX_PAD_ENV_OFF) voice->active = 0;
        }

        //ai: stereo ensemble chorus — two modulated taps at a phase offset;
        //   the write happens after the read so the tap never reads itself.
        if (chorusOn) {
            gX_F32 lfoL = sinf(twoPi * g_pad.chorusPhase);
            gX_F32 lfoR = sinf(twoPi * (g_pad.chorusPhase + g_pad.chorusStereoPhase));
            gX_F32 delayL = chorusBase + chorusDepth * lfoL;
            gX_F32 delayR = chorusBase + chorusDepth * lfoR;
            delayL = gX_Pad_Clamp(delayL, 16.0f, (gX_F32)(GX_PAD_CHORUS_SIZE - 2));
            delayR = gX_Pad_Clamp(delayR, 16.0f, (gX_F32)(GX_PAD_CHORUS_SIZE - 2));

            gX_U32 size = GX_PAD_CHORUS_SIZE;
            gX_U32 write = g_pad.chorusWrite % size;

            gX_F32 rl = (gX_F32)write - delayL;
            while (rl < 0.0f) rl += (gX_F32)size;
            gX_U32 i0 = (gX_U32)rl;
            gX_F32 frac = rl - (gX_F32)i0;
            gX_U32 i1 = (i0 + 1) % size;
            gX_F32 wetL = s_chorusBuf[i0 * 2] + (s_chorusBuf[i1 * 2] - s_chorusBuf[i0 * 2]) * frac;

            gX_F32 rr = (gX_F32)write - delayR;
            while (rr < 0.0f) rr += (gX_F32)size;
            gX_U32 j0 = (gX_U32)rr;
            gX_F32 fracR = rr - (gX_F32)j0;
            gX_U32 j1 = (j0 + 1) % size;
            gX_F32 wetR = s_chorusBuf[j0 * 2 + 1] + (s_chorusBuf[j1 * 2 + 1] - s_chorusBuf[j0 * 2 + 1]) * fracR;

            s_chorusBuf[write * 2] = mixL;
            s_chorusBuf[write * 2 + 1] = mixR;
            g_pad.chorusWrite = (write + 1) % size;

            gX_F32 dry = 1.0f - g_pad.chorusMix;
            mixL = mixL * dry + wetL * g_pad.chorusMix;
            mixR = mixR * dry + wetR * g_pad.chorusMix;

            g_pad.chorusPhase += chorusPhaseInc;
            if (g_pad.chorusPhase >= 1.0f) g_pad.chorusPhase -= 1.0f;
        }

        if (!muted) {
            samples[i * 2]     += mixL * master;
            samples[i * 2 + 1] += mixR * master;
        }
    }
}

//ai: raylib mixed-processor adapter — receives the device-rate stereo buffer.
static void gX_Pad_Process(void *buffer, unsigned int frames) {
    gX_Pad_Render((gX_F32 *)buffer, (gX_U32)frames);
}

gX_I0 gX_Pad_Init(gX_I0) {
    memset(&g_pad, 0, sizeof(g_pad));
    g_pad.sampleRate = 48000;
    g_pad.masterVolume = 0.5f;
    g_pad.width = 1.0f;
    g_pad.nextNoteID = 1;

    if (!s_chorusBuf) {
        s_chorusBuf = (gX_F32 *)gX_Calloc(2 * GX_PAD_CHORUS_SIZE * (gX_U32)sizeof(gX_F32));
    } else {
        //ai: a re-init starts from a clean line (the tests re-init the engine
        //   and stale chorus audio would leak between runs).
        memset(s_chorusBuf, 0, 2 * GX_PAD_CHORUS_SIZE * sizeof(gX_F32));
    }

    //ai: default patch — a warm three-layer ensemble: saw/tri/saw spread
    //   across the stereo field, each drifting at its own slow rate.
    gX_Pad_Layer *l0 = &g_pad.defaultLayers[0];
    l0->waveform = GX_PAD_WAVE_SAW;
    l0->detuneCents = -9.0f;
    l0->level = 0.5f;
    l0->pan = -0.55f;
    l0->driftRate = 0.11f;
    l0->driftDepth = 6.0f;
    gX_Pad_Layer *l1 = &g_pad.defaultLayers[1];
    l1->waveform = GX_PAD_WAVE_TRI;
    l1->detuneCents = 7.0f;
    l1->level = 0.55f;
    l1->pan = 0.05f;
    l1->driftRate = 0.17f;
    l1->driftDepth = 5.0f;
    gX_Pad_Layer *l2 = &g_pad.defaultLayers[2];
    l2->waveform = GX_PAD_WAVE_SAW;
    l2->detuneCents = 14.0f;
    l2->level = 0.45f;
    l2->pan = 0.6f;
    l2->driftRate = 0.07f;
    l2->driftDepth = 7.0f;

    //ai: long envelope / gentle moving filter — the pad's core movement.
    g_pad.defaultAttack = 1.2f;
    g_pad.defaultDecay = 1.5f;
    g_pad.defaultSustain = 0.85f;
    g_pad.defaultRelease = 3.0f;
    g_pad.defaultFilterMode = GX_PAD_FILTER_LP;
    g_pad.defaultFilterCutoff = 0.55f;
    g_pad.defaultFilterRes = 0.15f;
    g_pad.defaultFilterLfoRate = 0.09f;
    g_pad.defaultFilterLfoDepth = 0.25f;

    //ai: ensemble chorus on by default (the pad's width engine).
    g_pad.chorusEnabled = 1;
    g_pad.chorusRate = 0.6f;
    g_pad.chorusDepth = 0.6f;
    g_pad.chorusMix = 0.5f;
    g_pad.chorusStereoPhase = 0.25f;

    //ai: muted until asked — GX_PAD_MUTE=0 opens the output (the synth's
    //   GX_SYNTH_MUTE precedent); voices and the offline core keep running.
    g_pad.muted = 1;
    const gX_I8 *muteEnv = getenv("GX_PAD_MUTE");
    if (muteEnv) g_pad.muted = (atoi(muteEnv) != 0) ? 1 : 0;

    gX_TraceLog(gX_LOG_NOTICE,
        "PAD: initialised layered ensemble engine (%d voices x %d layers)",
        GX_PAD_MAX_VOICES, GX_PAD_LAYERS);
    return;
}

gX_I0 gX_Pad_Unload(gX_I0) {
    gX_Pad_AllNotesOff();
    if (g_pad.attached) {
        DetachAudioMixedProcessor(gX_Pad_Process);
        g_pad.attached = 0;
    }
    if (s_chorusBuf) {
        gX_Free(s_chorusBuf);
        s_chorusBuf = NULL;
    }
    gX_TraceLog(gX_LOG_NOTICE, "PAD: unloaded");
    return;
}

//ai: lazy mixer attach/detach — same contract as the other engines: the
//   processor joins on the first sounding note and leaves after the tails
//   go quiet (TRACE logs).
#define GX_PAD_IDLE_DETACH_SECONDS 1.0f

static gX_I0 gX_Pad_MixerAttach(gX_I0) {
    if (g_pad.attached) return;
    if (!IsAudioDeviceReady()) return;
    AttachAudioMixedProcessor(gX_Pad_Process);
    g_pad.attached = 1;
    gX_TraceLog(gX_LOG_TRACE, "PAD: mixer attached");
    return;
}

static gX_I0 gX_Pad_MixerDetach(gX_I0) {
    if (!g_pad.attached) return;
    DetachAudioMixedProcessor(gX_Pad_Process);
    g_pad.attached = 0;
    gX_TraceLog(gX_LOG_TRACE, "PAD: mixer detached (idle)");
    return;
}

gX_I1 gX_Pad_IsAttached(gX_I0) {
    return g_pad.attached;
}

gX_I0 gX_Pad_SetSampleRate(gX_U32 sampleRate) {
    if (sampleRate < 8000 || sampleRate > 192000) return;
    g_pad.sampleRate = sampleRate;
    return;
}

gX_I0 gX_Pad_Update(gX_I0) {
    static gX_F32 idleSeconds = 0.0f;

    gX_F32 dt = (gX_F32)gX_FrameTime_Get();
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;

    gX_I1 active = 0;
    for (gX_U32 i = 0; i < GX_PAD_MAX_VOICES && !active; i++) {
        if (g_pad.voices[i].active) active = 1;
    }

    if (active) {
        idleSeconds = 0.0f;
        gX_Pad_MixerAttach();
    } else if (g_pad.attached) {
        idleSeconds += dt;
        if (idleSeconds >= GX_PAD_IDLE_DETACH_SECONDS) {
            gX_Pad_MixerDetach();
            idleSeconds = 0.0f;
        }
    }
    return;
}

//ai: voice pick — idle first, then the oldest released voice, then the
//   oldest voice overall (the family's steal order).
static gX_I32 gX_Pad_FindVoice(gX_I0) {
    gX_I32 released = -1;
    gX_U32 releasedID = 0xFFFFFFFFu;
    gX_I32 oldest = -1;
    gX_U32 oldestID = 0xFFFFFFFFu;

    for (gX_U32 i = 0; i < GX_PAD_MAX_VOICES; i++) {
        gX_Pad_Voice *v = &g_pad.voices[i];
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

//ai: copy only the parameter fields; a sounding voice keeps its phase and
//   drift position when a layer is edited.
static gX_I0 gX_Pad_Layer_CopyParams(gX_Pad_Layer *dst, const gX_Pad_Layer *src) {
    dst->waveform = (src->waveform == GX_PAD_WAVE_TRI) ? GX_PAD_WAVE_TRI : GX_PAD_WAVE_SAW;
    dst->detuneCents = gX_Pad_Clamp(src->detuneCents, -50.0f, 50.0f);
    dst->level = gX_Pad_Clamp(src->level, 0.0f, 1.0f);
    dst->pan = gX_Pad_Clamp(src->pan, -1.0f, 1.0f);
    dst->driftRate = gX_Pad_Clamp(src->driftRate, 0.0f, 1.0f);
    dst->driftDepth = gX_Pad_Clamp(src->driftDepth, 0.0f, 30.0f);
    return;
}

gX_I32 gX_Pad_NoteOn(gX_F32 freq, gX_F32 velocity) {
    gX_I32 slot = gX_Pad_FindVoice();
    if (slot < 0) return -1;
    gX_Pad_MixerAttach();

    gX_Pad_Voice *voice = &g_pad.voices[slot];
    voice->active = 1;
    voice->release = 0;
    voice->frequency = (freq > 0.0f) ? freq : 440.0f;
    voice->velocity = gX_Pad_Clamp(velocity, 0.0f, 1.0f);
    voice->noteID = g_pad.nextNoteID++;

    voice->env = 0.0f;
    voice->releaseLevel = 0.0f;
    voice->envStage = GX_PAD_ENV_ATTACK;
    voice->attackTime = g_pad.defaultAttack;
    voice->decayTime = g_pad.defaultDecay;
    voice->sustainLevel = g_pad.defaultSustain;
    voice->releaseTime = g_pad.defaultRelease;

    //ai: deterministic-but-varied drift start per note (golden-ratio wrap)
    gX_F32 driftStart = (gX_F32)(voice->noteID % 997u) * 0.618034f;
    driftStart -= floorf(driftStart);

    for (gX_I32 l = 0; l < GX_PAD_LAYERS; l++) {
        gX_Pad_Layer *layer = &voice->layers[l];
        gX_Pad_Layer_CopyParams(layer, &g_pad.defaultLayers[l]);
        layer->phase = 0.0f;
        layer->driftPhase = driftStart;
        driftStart += 0.236068f;   //ai: de-correlate the layers' drift
        driftStart -= floorf(driftStart);
    }

    voice->filterMode = g_pad.defaultFilterMode;
    voice->filterCutoff = g_pad.defaultFilterCutoff;
    voice->filterRes = g_pad.defaultFilterRes;
    voice->filterLfoRate = g_pad.defaultFilterLfoRate;
    voice->filterLfoDepth = g_pad.defaultFilterLfoDepth;
    voice->filterLfoPhase = 0.0f;
    gX_Audio_Filter_Reset(&voice->filter);
    voice->pan = 0.0f;

    gX_TraceLog(gX_LOG_DEBUG,
        "PAD: noteOn slot=%d freq=%.1f vel=%.2f id=%u",
        slot, (double)voice->frequency, (double)voice->velocity, voice->noteID);
    return slot;
}

gX_I0 gX_Pad_NoteOff(gX_U32 voiceIndex) {
    if (voiceIndex >= GX_PAD_MAX_VOICES) return;
    g_pad.voices[voiceIndex].release = 1;
    return;
}

gX_I0 gX_Pad_NoteOff_ID(gX_U32 noteID) {
    for (gX_U32 i = 0; i < GX_PAD_MAX_VOICES; i++) {
        if (g_pad.voices[i].active && g_pad.voices[i].noteID == noteID) {
            g_pad.voices[i].release = 1;
            return;
        }
    }
    return;
}

gX_I0 gX_Pad_AllNotesOff(gX_I0) {
    for (gX_U32 i = 0; i < GX_PAD_MAX_VOICES; i++) {
        g_pad.voices[i].release = 1;
    }
    return;
}

// ------------------------------------------------------------------
// defaults / voice parameters
// ------------------------------------------------------------------

gX_I0 gX_Pad_SetDefaultLayer(gX_I32 layer, const gX_Pad_Layer *params) {
    if (layer < 0 || layer >= GX_PAD_LAYERS || !params) return;
    gX_Pad_Layer_CopyParams(&g_pad.defaultLayers[layer], params);
    return;
}

gX_I0 gX_Pad_GetDefaultLayer(gX_I32 layer, gX_Pad_Layer *out) {
    if (layer < 0 || layer >= GX_PAD_LAYERS || !out) return;
    *out = g_pad.defaultLayers[layer];
    return;
}

gX_I0 gX_Pad_SetDefaultADSR(gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r) {
    g_pad.defaultAttack = (a < 0.001f) ? 0.001f : a;
    g_pad.defaultDecay = (d < 0.001f) ? 0.001f : d;
    g_pad.defaultSustain = gX_Pad_Clamp(s, 0.0f, 1.0f);
    g_pad.defaultRelease = (r < 0.001f) ? 0.001f : r;
    return;
}

gX_I0 gX_Pad_SetDefaultFilter(gX_I32 mode, gX_F32 cutoff, gX_F32 res) {
    g_pad.defaultFilterMode = (mode < 0) ? 0 : (mode > 2) ? 2 : mode;
    g_pad.defaultFilterCutoff = gX_Pad_Clamp(cutoff, 0.01f, 0.99f);
    g_pad.defaultFilterRes = gX_Pad_Clamp(res, 0.0f, 1.0f);
    return;
}

gX_I0 gX_Pad_SetDefaultFilterLFO(gX_F32 rate, gX_F32 depth) {
    g_pad.defaultFilterLfoRate = gX_Pad_Clamp(rate, 0.0f, 2.0f);
    g_pad.defaultFilterLfoDepth = gX_Pad_Clamp(depth, 0.0f, 1.0f);
    return;
}

static gX_Pad_Voice *gX_Pad_VoicePtr(gX_U32 voiceIndex) {
    if (voiceIndex >= GX_PAD_MAX_VOICES) return NULL;
    return &g_pad.voices[voiceIndex];
}

gX_I0 gX_Pad_SetVoiceLayer(gX_U32 voiceIndex, gX_I32 layer, const gX_Pad_Layer *params) {
    gX_Pad_Voice *voice = gX_Pad_VoicePtr(voiceIndex);
    if (!voice || !params || layer < 0 || layer >= GX_PAD_LAYERS) return;
    gX_Pad_Layer_CopyParams(&voice->layers[layer], params);
    return;
}

gX_I0 gX_Pad_SetVoiceADSR(gX_U32 voiceIndex, gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r) {
    gX_Pad_Voice *voice = gX_Pad_VoicePtr(voiceIndex);
    if (!voice) return;
    voice->attackTime = (a < 0.001f) ? 0.001f : a;
    voice->decayTime = (d < 0.001f) ? 0.001f : d;
    voice->sustainLevel = gX_Pad_Clamp(s, 0.0f, 1.0f);
    voice->releaseTime = (r < 0.001f) ? 0.001f : r;
    return;
}

gX_I0 gX_Pad_SetVoiceFilter(gX_U32 voiceIndex, gX_I32 mode, gX_F32 cutoff, gX_F32 res) {
    gX_Pad_Voice *voice = gX_Pad_VoicePtr(voiceIndex);
    if (!voice) return;
    voice->filterMode = (mode < 0) ? 0 : (mode > 2) ? 2 : mode;
    voice->filterCutoff = gX_Pad_Clamp(cutoff, 0.01f, 0.99f);
    voice->filterRes = gX_Pad_Clamp(res, 0.0f, 1.0f);
    return;
}

gX_I0 gX_Pad_SetVoiceFilterLFO(gX_U32 voiceIndex, gX_F32 rate, gX_F32 depth) {
    gX_Pad_Voice *voice = gX_Pad_VoicePtr(voiceIndex);
    if (!voice) return;
    voice->filterLfoRate = gX_Pad_Clamp(rate, 0.0f, 2.0f);
    voice->filterLfoDepth = gX_Pad_Clamp(depth, 0.0f, 1.0f);
    return;
}

gX_I0 gX_Pad_SetVoicePan(gX_U32 voiceIndex, gX_F32 pan) {
    gX_Pad_Voice *voice = gX_Pad_VoicePtr(voiceIndex);
    if (!voice) return;
    voice->pan = gX_Pad_Clamp(pan, -1.0f, 1.0f);
    return;
}

gX_I0 gX_Pad_SetChorus(gX_I1 enabled, gX_F32 rate, gX_F32 depth, gX_F32 mix, gX_F32 stereoPhase) {
    g_pad.chorusEnabled = enabled ? 1 : 0;
    g_pad.chorusRate = gX_Pad_Clamp(rate, 0.05f, 3.0f);
    g_pad.chorusDepth = gX_Pad_Clamp(depth, 0.0f, 1.0f);
    g_pad.chorusMix = gX_Pad_Clamp(mix, 0.0f, 1.0f);
    g_pad.chorusStereoPhase = gX_Pad_Clamp(stereoPhase, 0.0f, 1.0f);
    return;
}

gX_I0 gX_Pad_SetWidth(gX_F32 width) {
    g_pad.width = gX_Pad_Clamp(width, 0.0f, 1.0f);
    return;
}

gX_I0 gX_Pad_SetMasterVolume(gX_F32 volume) {
    g_pad.masterVolume = gX_Pad_Clamp(volume, 0.0f, 2.0f);
    return;
}

gX_I0 gX_Pad_SetMuted(gX_I1 muted) {
    g_pad.muted = muted ? 1 : 0;
    return;
}

gX_I1 gX_Pad_IsMuted(gX_I0) {
    return g_pad.muted;
}

gX_Pad_Voice *gX_Pad_GetVoice(gX_U32 index) {
    if (index >= GX_PAD_MAX_VOICES) return NULL;
    return &g_pad.voices[index];
}
