/**********************************************************************************************
*
*   [gX: src/audio/synth.c]
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


//opencode: polyphonic synthesizer engine — real-time audio processor
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "synth.h"

#include "../common/log.h"
#include "../common/macros.h"
#include "../common/path.h"
#include "../gxmath/gxmath.h"
//ai: mixer buses — the synth output rides the synth bus gain.
#include "../audio.h"

//opencode: raylib audio processor API
#include "raylib.h"
#include <json-c/json.h>

//ai: gX frame time (honors GX_FIXED_DT for deterministic audits).
#include "../wrappers/wrappers.h"

gX_Synth g_synth;

//opencode: scope capture ring buffer — audio thread writes, main thread reads for display
gX_F32 g_synthScope[GX_SYNTH_SCOPE_SIZE];
gX_U32 g_synthScopeCount = 0;
gX_U32 g_synthScopeWrite = 0;

//ai: the voice filter rides the shared TPT/ZDF core (src/audio/filter.h):
//   the full 0..1 cutoff range is usable (the old Chamberlin clamp pinned
//   everything above ~0.17) and the topology is stable at any resonance.
//   Set is cache-preserving when the values have not moved, so the per-sample
//   cutoff modulation path stays cheap.
static gX_F32 gX_Synth_FilterProcess(gX_Synth_Voice *voice, gX_F32 input) {
    if (voice->filterMode < 0) return input; // NONE
    gX_Audio_Filter_Set(&voice->filter, voice->filterMode,
        voice->filterCutoff, voice->filterRes);
    gX_Audio_Filter_Prepare(&voice->filter, g_synth.sampleRate);
    return gX_Audio_Filter_Step(&voice->filter, 0, input);
}

//opencode: audio-thread callback — mixes synth voices into output buffer
//          buffer is stereo interleaved float; we ADD to existing mix
//ai: offline render core — the raylib mixed-processor adapter below shares
//   this entry point so unit tests can render without an audio device.
gX_I0 gX_Synth_Render(gX_F32 *samples, gX_U32 frames) {
    //ai: mixer — the synth rides its own bus; master is raylib's device gain.
    gX_F32 master = g_synth.masterVolume * gX_Audio_BusGain(GX_AUDIO_BUS_SYNTH);
    gX_I1 muted = g_synth.muted;
    gX_F32 invSR = 1.0f / (gX_F32)g_synth.sampleRate;
    gX_F32 twoPI = 2.0f * gX_PI;

    // Delay state
    gX_I1 delayOn = g_synth.delay.enabled;
    gX_F32 delayMix = g_synth.delay.mix;
    gX_F32 delayFB = g_synth.delay.feedback;
    gX_F32 delaySpread = g_synth.delay.stereoSpread;
    gX_U32 delaySize = g_synth.delayBufferSize;
    gX_U32 delayWr = g_synth.delayWritePos;
    gX_U32 delayRd = (delayWr + delaySize - (gX_U32)(g_synth.delay.delayTime * g_synth.sampleRate + 0.5f)) % delaySize;

    for (unsigned int i = 0; i < frames; i++) {
        gX_F32 mixL = 0.0f;
        gX_F32 mixR = 0.0f;

        for (gX_U32 v = 0; v < GX_SYNTH_MAX_VOICES; v++) {
            gX_Synth_Voice *voice = &g_synth.voices[v];
            if (!voice->active) continue;

            // ---- handle note-off request (capture releaseLevel before stage switch) ----
            if (voice->release && voice->envStage != GX_SYNTH_ENV_RELEASE
                               && voice->envStage != GX_SYNTH_ENV_OFF) {
                voice->releaseLevel = voice->envelope;
                voice->envStage = GX_SYNTH_ENV_RELEASE;
            }

            // ---- portamento frequency slide ----
            if (voice->portamentoStep != 0.0f) {
                voice->frequency += voice->portamentoStep;
                // Check if we've overshot the target
                if ((voice->portamentoStep > 0.0f && voice->frequency >= voice->portamentoFreq) ||
                    (voice->portamentoStep < 0.0f && voice->frequency <= voice->portamentoFreq)) {
                    voice->frequency = voice->portamentoFreq;
                    voice->portamentoStep = 0.0f;
                }
            }

            // ---- LFO modulation ----
            gX_F32 lfoValue = 0.0f;
            if (voice->lfoFreq > 0.0f && voice->lfoDepth > 0.0f) {
                switch (voice->lfoWaveform) {
                    case GX_SYNTH_WAVE_SINE:
                        lfoValue = sinf(voice->lfoPhase * twoPI);
                        break;
                    case GX_SYNTH_WAVE_SQUARE:
                        lfoValue = (voice->lfoPhase < 0.5f) ? 1.0f : -1.0f;
                        break;
                    case GX_SYNTH_WAVE_SAWTOOTH:
                        lfoValue = 2.0f * voice->lfoPhase - 1.0f;
                        break;
                    case GX_SYNTH_WAVE_TRIANGLE:
                        lfoValue = 4.0f * fabsf(voice->lfoPhase - 0.5f) - 1.0f;
                        break;
                    default:
                        lfoValue = sinf(voice->lfoPhase * twoPI);
                        break;
                }
                voice->lfoPhase += voice->lfoFreq * invSR;
                if (voice->lfoPhase >= 1.0f) voice->lfoPhase -= 1.0f;
            }

            // ---- modulated frequency (for pitch LFO and phase advance) ----
            gX_F32 modFreq = voice->frequency;
            if (voice->lfoFreq > 0.0f && voice->lfoDepth > 0.0f
                && voice->lfoTarget == GX_SYNTH_LFO_PITCH) {
                modFreq = voice->frequency * (1.0f + lfoValue * voice->lfoDepth * 0.1f);
                if (modFreq < 20.0f) modFreq = 20.0f;
            }

            // ---- FM modulation (frequency modulation synthesis) ----
            if (voice->fmAlgorithm != GX_SYNTH_FM_OFF) {
                gX_F32 fmMod = 0.0f;
                gX_I32 algo = voice->fmAlgorithm;

                // Compute operator 1 output
                gX_F32 op1PhaseInc = voice->frequency * voice->fmOp1Ratio * invSR;
                gX_F32 op1Sample = sinf(voice->fmPhase1 * twoPI);

                // Feedback: add previous output to phase
                if (voice->fmFeedback > 0.0f && algo == GX_SYNTH_FM_FEEDBACK) {
                    op1Sample = sinf(voice->fmPhase1 * twoPI + voice->fmPrevSample * voice->fmFeedback * 4.0f);
                }

                voice->fmPhase1 += op1PhaseInc;
                if (voice->fmPhase1 >= 1.0f) voice->fmPhase1 -= 1.0f;

                switch (algo) {
                    case GX_SYNTH_FM_SIMPLE:
                        // Op1 → Carrier: Op1 directly modulates carrier pitch
                        fmMod = op1Sample * voice->fmOp1Level;
                        break;

                    case GX_SYNTH_FM_SERIAL: {
                        // Op1 → Op2 → Carrier: Op1 modulates Op2, Op2 modulates carrier
                        gX_F32 op2PhaseInc = voice->frequency * voice->fmOp2Ratio * invSR;
                        gX_F32 op2Sample = sinf(voice->fmPhase2 * twoPI + op1Sample * voice->fmOp1Level * 2.0f);
                        voice->fmPhase2 += op2PhaseInc;
                        if (voice->fmPhase2 >= 1.0f) voice->fmPhase2 -= 1.0f;
                        fmMod = op2Sample * voice->fmOp2Level;
                        break;
                    }

                    case GX_SYNTH_FM_PARALLEL: {
                        // Op1 + Op2 → Carrier: both modulate carrier independently
                        gX_F32 op2PhaseInc = voice->frequency * voice->fmOp2Ratio * invSR;
                        gX_F32 op2Sample = sinf(voice->fmPhase2 * twoPI);
                        voice->fmPhase2 += op2PhaseInc;
                        if (voice->fmPhase2 >= 1.0f) voice->fmPhase2 -= 1.0f;
                        fmMod = op1Sample * voice->fmOp1Level
                              + op2Sample * voice->fmOp2Level;
                        break;
                    }

                    case GX_SYNTH_FM_FEEDBACK:
                        // Op1↻ → Carrier: Op1 with feedback into carrier
                        fmMod = op1Sample * voice->fmOp1Level;
                        break;
                }

                // Apply FM modulation to carrier frequency
                // Scale: at level 1.0, modulation swings frequency by ~50%
                modFreq += fmMod * voice->frequency * 0.5f;
                if (modFreq < 10.0f) modFreq = 10.0f;

                // Store sample for next frame's feedback
                voice->fmPrevSample = op1Sample * voice->fmOp1Level;
            }

            // ---- oscillator ----
            gX_F32 osc = 0.0f;
            switch (voice->waveform) {
                case GX_SYNTH_WAVE_SINE:
                    osc = sinf(voice->phase * twoPI);
                    break;
                case GX_SYNTH_WAVE_SQUARE:
                    osc = (voice->phase < voice->pulseWidth) ? 1.0f : -1.0f;
                    break;
                case GX_SYNTH_WAVE_SAWTOOTH:
                    osc = 2.0f * voice->phase - 1.0f;
                    break;
                case GX_SYNTH_WAVE_TRIANGLE:
                    osc = 4.0f * fabsf(voice->phase - 0.5f) - 1.0f;
                    break;
                case GX_SYNTH_WAVE_NOISE: {
                    static gX_U32 noiseSeed = 12345;
                    noiseSeed = noiseSeed * 1103515245u + 12345u;
                    osc = (gX_F32)(noiseSeed >> 16) / 32768.0f - 1.0f;
                    break;
                }
                case GX_SYNTH_WAVE_WAVETABLE: {
                    // Linear interpolation from wavetable data
                    gX_I32 wtIdx = voice->wavetableIndex;
                    if (wtIdx < 0) wtIdx = 0; // fallback to first loaded
                    if (wtIdx < GX_SYNTH_MAX_WT && g_synth.wavetables[wtIdx].loaded) {
                        gX_Synth_Wavetable *wt = &g_synth.wavetables[wtIdx];
                        gX_F32 pos = voice->phase * (gX_F32)(wt->size - 1);
                        gX_U32 idxA = (gX_U32)pos;
                        gX_U32 idxB = (idxA + 1 < wt->size) ? idxA + 1 : 0;
                        gX_F32 frac = pos - (gX_F32)idxA;
                        osc = wt->data[idxA] + (wt->data[idxB] - wt->data[idxA]) * frac;
                        // Scale from [0,1] or whatever range to [-1,1]
                        osc = osc * 2.0f - 1.0f;
                    } else {
                        osc = sinf(voice->phase * twoPI); // fallback to sine
                    }
                    break;
                }
                default:
                    osc = sinf(voice->phase * twoPI);
                    break;
            }

            // ---- apply filter to oscillator (with optional filter LFO) ----
            if (voice->lfoFreq > 0.0f && voice->lfoDepth > 0.0f
                && voice->lfoTarget == GX_SYNTH_LFO_FILTER) {
                gX_F32 modCutoff = voice->filterCutoff + lfoValue * voice->lfoDepth * 0.3f;
                if (modCutoff < 0.001f) modCutoff = 0.001f;
                if (modCutoff > 0.999f) modCutoff = 0.999f;
                gX_F32 savedCutoff = voice->filterCutoff;
                voice->filterCutoff = modCutoff;
                osc = gX_Synth_FilterProcess(voice, osc);
                voice->filterCutoff = savedCutoff;
            } else {
                osc = gX_Synth_FilterProcess(voice, osc);
            }

            // ---- envelope ----
            switch (voice->envStage) {
                case GX_SYNTH_ENV_ATTACK: {
                    gX_F32 step = 1.0f / (voice->attackTime * (gX_F32)g_synth.sampleRate + 1.0f);
                    voice->envelope += step;
                    if (voice->envelope >= 1.0f) {
                        voice->envelope = 1.0f;
                        voice->envStage = GX_SYNTH_ENV_DECAY;
                    }
                    break;
                }
                case GX_SYNTH_ENV_DECAY: {
                    gX_F32 step = (1.0f - voice->sustainLevel)
                                / (voice->decayTime * (gX_F32)g_synth.sampleRate + 1.0f);
                    voice->envelope -= step;
                    if (voice->envelope <= voice->sustainLevel) {
                        voice->envelope = voice->sustainLevel;
                        voice->envStage = GX_SYNTH_ENV_SUSTAIN;
                    }
                    break;
                }
                case GX_SYNTH_ENV_RELEASE: {
                    gX_F32 step = voice->releaseLevel
                                / (voice->releaseTime * (gX_F32)g_synth.sampleRate + 1.0f);
                    voice->envelope -= step;
                    if (voice->envelope <= 0.0f) {
                        voice->envelope = 0.0f;
                        voice->active = 0;
                        voice->envStage = GX_SYNTH_ENV_OFF;
                    }
                    break;
                }
                default:
                    break;
            }
            gX_F32 env = voice->envelope;

            // ---- amplitude LFO (tremolo) ----
            if (voice->lfoFreq > 0.0f && voice->lfoDepth > 0.0f
                && voice->lfoTarget == GX_SYNTH_LFO_AMP) {
                gX_F32 tremMod = (lfoValue * 0.5f + 0.5f);
                env *= (1.0f - tremMod * voice->lfoDepth * 0.5f);
                if (env < 0.0f) env = 0.0f;
            }

            // ---- mix ----
            gX_F32 s = osc * env * voice->velocity;
            gX_F32 panL = 1.0f - (voice->pan + 1.0f) * 0.5f;
            gX_F32 panR = (voice->pan + 1.0f) * 0.5f;
            mixL += s * panL;
            mixR += s * panR;

            // ---- advance phase (use modulated frequency for pitch LFO) ----
            voice->phase += modFreq * invSR;
            if (voice->phase >= 1.0f) voice->phase -= 1.0f;
        }

        // ---- apply delay effect ----
        if (delayOn && delaySize > 0) {
            gX_F32 wetL = g_synth.delayBuffer[0][delayRd];
            gX_F32 wetR = g_synth.delayBuffer[1][delayRd];

            //ai: feedback source blends self with the opposite channel —
            //   0 spread = mono self-feedback (repeats), 1 = full ping-pong.
            //   The previous form read only the opposite channel, so a mono
            //   spread had no repeats at all (the dry pulse echoed once).
            gX_F32 fbL = wetL * (1.0f - delaySpread) + wetR * delaySpread;
            gX_F32 fbR = wetR * (1.0f - delaySpread) + wetL * delaySpread;
            g_synth.delayBuffer[0][delayWr] = mixL + fbL * delayFB;
            g_synth.delayBuffer[1][delayWr] = mixR + fbR * delayFB;

            // Wet/dry mix
            gX_F32 dry = 1.0f - delayMix;
            mixL = mixL * dry + wetL * delayMix;
            mixR = mixR * dry + wetR * delayMix;

            // Advance delay pointers
            delayWr = (delayWr + 1) % delaySize;
            delayRd = (delayRd + 1) % delaySize;
        }

        // Apply master volume and add to output buffer (skipped when muted —
        // voices, delay and the scope keep running)
        if (!muted) {
            samples[i * 2]     += mixL * master;
            samples[i * 2 + 1] += mixR * master;
        }

        // Capture mix in ring buffer for oscilloscope
        g_synthScope[g_synthScopeWrite] = mixL * master + mixR * master;
        g_synthScopeWrite = (g_synthScopeWrite + 1) % GX_SYNTH_SCOPE_SIZE;
        if (g_synthScopeCount < GX_SYNTH_SCOPE_SIZE) g_synthScopeCount++;
    }

    // Write back delay pointers (may have been modified by delay processing)
    g_synth.delayWritePos = delayWr;
}

//ai: raylib mixed-processor adapter — receives the device-rate stereo buffer.
static void gX_Synth_Process(void *buffer, unsigned int frames) {
    gX_Synth_Render((gX_F32 *)buffer, (gX_U32)frames);
}

//opencode: initialize synth state
gX_I0 gX_Synth_Init(gX_I0) {
    gX_TraceLog(gX_LOG_NOTICE, "SYNTH: initialising polyphonic engine (%u voices)", GX_SYNTH_MAX_VOICES);

    memset(&g_synth, 0, sizeof(g_synth));

    g_synth.sampleRate = 48000;
    g_synth.masterVolume = 0.5f;

    //ai: muted until asked — GX_SYNTH_MUTE=0 opens the output (the mixer still
    //   attaches while muted so the scope/visualizer stays live; only the
    //   device buffer stays untouched).
    g_synth.muted = 1;
    const gX_I8 *muteEnv = getenv("GX_SYNTH_MUTE");
    if (muteEnv) g_synth.muted = (atoi(muteEnv) != 0) ? 1 : 0;
    g_synth.nextNoteID = 1;

    // Global ADSR defaults
    g_synth.defaultAttackTime  = 0.01f;
    g_synth.defaultDecayTime   = 0.1f;
    g_synth.defaultSustainLevel = 0.7f;
    g_synth.defaultReleaseTime = 0.3f;

    // Default ADSR for all voices
    for (gX_U32 i = 0; i < GX_SYNTH_MAX_VOICES; i++) {
        g_synth.voices[i].attackTime    = 0.01f;
        g_synth.voices[i].decayTime     = 0.1f;
        g_synth.voices[i].sustainLevel  = 0.7f;
        g_synth.voices[i].releaseTime   = 0.3f;
        g_synth.voices[i].releaseLevel  = 0.0f;
        g_synth.voices[i].pan           = 0.0f;
        g_synth.voices[i].filterMode    = GX_SYNTH_FILTER_NONE;
        g_synth.voices[i].filterCutoff  = 0.5f;
        g_synth.voices[i].filterRes     = 0.0f;
        g_synth.voices[i].lfoFreq       = 0.0f;
        g_synth.voices[i].lfoDepth      = 0.0f;
        g_synth.voices[i].lfoWaveform   = GX_SYNTH_WAVE_SINE;
        g_synth.voices[i].lfoTarget     = GX_SYNTH_LFO_PITCH;
        g_synth.voices[i].lfoPhase      = 0.0f;
        g_synth.voices[i].pulseWidth    = 0.5f;
        g_synth.voices[i].portamentoFreq = 0.0f;
        g_synth.voices[i].portamentoStep = 0.0f;
        g_synth.voices[i].fmAlgorithm   = GX_SYNTH_FM_OFF;
        g_synth.voices[i].fmOp1Ratio    = 1.0f;
        g_synth.voices[i].fmOp1Level    = 0.5f;
        g_synth.voices[i].fmOp2Ratio    = 2.0f;
        g_synth.voices[i].fmOp2Level    = 0.3f;
        g_synth.voices[i].fmFeedback    = 0.0f;
        g_synth.voices[i].fmPhase1      = 0.0f;
        g_synth.voices[i].fmPhase2      = 0.0f;
        g_synth.voices[i].fmPrevSample  = 0.0f;
        g_synth.voices[i].wavetableIndex = 0;
    }
    g_synth.defaultFilterMode   = GX_SYNTH_FILTER_LOWPASS;
    g_synth.defaultFilterCutoff = 0.8f;
    g_synth.defaultFilterRes    = 0.0f;
    g_synth.defaultPortamentoTime = 0.0f;

    // Global oscillator defaults
    g_synth.defaultWaveform       = GX_SYNTH_WAVE_SQUARE;
    g_synth.defaultPulseWidth     = 0.5f;
    g_synth.defaultWavetableIndex = 0;

    // Global LFO defaults
    g_synth.defaultLfoFreq    = 0.0f;
    g_synth.defaultLfoDepth   = 0.0f;
    g_synth.defaultLfoWaveform = GX_SYNTH_WAVE_SINE;
    g_synth.defaultLfoTarget  = GX_SYNTH_LFO_PITCH;

    // Global FM defaults
    gX_Synth_FMConfig_Init(&g_synth.defaultFM);

    // Delay init
    g_synth.delay.enabled      = 0;
    g_synth.delay.delayTime    = 0.3f;
    g_synth.delay.feedback     = 0.4f;
    g_synth.delay.mix          = 0.3f;
    g_synth.delay.stereoSpread = 0.5f;
    g_synth.delayBufferSize    = (gX_U32)(GX_SYNTH_DELAY_MAX_SEC * (gX_F32)g_synth.sampleRate);
    g_synth.delayWritePos      = 0;
    memset(g_synth.delayBuffer, 0, sizeof(g_synth.delayBuffer));

    // Arp init
    g_synth.arp.mode       = GX_SYNTH_ARP_OFF;
    g_synth.arp.rate       = 0.125f;
    g_synth.arp.gate       = 0.5f;
    g_synth.arp.heldCount  = 0;
    g_synth.arp.currentStep = 0;
    g_synth.arp.timer      = 0.0f;
    g_synth.arp.direction  = 1;
    g_synth.arp.lastNoteOn = -1;
    g_synth.arp.noteActive = 0;

    // Wavetables init
    g_synth.wavetableCount = 0;
    for (gX_U32 w = 0; w < GX_SYNTH_MAX_WT; w++) {
        g_synth.wavetables[w].loaded = 0;
        g_synth.wavetables[w].size = 0;
    }

    // Patch bank init (multi-timbral)
    g_synth.patchBankCount = 0;
    for (gX_U32 p = 0; p < GX_SYNTH_MAX_VOICES; p++) {
        gX_Synth_Patch_Init(&g_synth.patchBank[p]);
    }

    // Sequencer init
    g_synth.seq.playing = 0;
    g_synth.seq.bpm = 120.0f;
    g_synth.seq.stepsPerBeat = 4;
    g_synth.seq.currentStep = 0;
    g_synth.seq.timer = 0.0f;
    g_synth.seq.currentPattern = 0;
    g_synth.seq.lastNoteOn = -1;
    g_synth.seq.noteActive = 0;
    for (gX_U32 pat = 0; pat < GX_SYNTH_SEQ_MAX_PATTERNS; pat++) {
        g_synth.seq.patternLength[pat] = 16;
        for (gX_U32 s = 0; s < GX_SYNTH_SEQ_STEPS; s++) {
            g_synth.seq.patterns[pat][s].freq = 0.0f;
            g_synth.seq.patterns[pat][s].velocity = 0.0f;
            g_synth.seq.patterns[pat][s].gate = 0.5f;
        }
    }

    //ai: the mixer attach is lazy (gX_Synth_Update on the first sounding
    //   voice); Init only prepares state so the engine stays silent and
    //   device-free until a note plays.

    return;
}

//opencode: uninit synth state
gX_I0 gX_Synth_Uninit(gX_I0) {
    gX_Synth_AllNotesOff();

    if (g_synth.attached) {
        DetachAudioMixedProcessor(gX_Synth_Process);
        g_synth.attached = 0;
    }

    gX_TraceLog(gX_LOG_NOTICE, "SYNTH: unloaded");
}

//ai: lazy mixer attach/detach — the processor joins the raylib mixed chain
//   when a voice first sounds and leaves after a short idle window, so a
//   silent engine costs no audio-thread work. Logs are TRACE: a note pattern
//   with gaps flaps these transitions, which is not NOTICE material.
#define GX_SYNTH_IDLE_DETACH_SECONDS 1.0f

static void gX_Synth_MixerAttach(gX_I0) {
    if (g_synth.attached) return;
    if (!IsAudioDeviceReady()) return;
    AttachAudioMixedProcessor(gX_Synth_Process);
    g_synth.attached = 1;
    gX_TraceLog(gX_LOG_TRACE, "SYNTH: mixer attached");
}

static void gX_Synth_MixerDetach(gX_I0) {
    if (!g_synth.attached) return;
    DetachAudioMixedProcessor(gX_Synth_Process);
    g_synth.attached = 0;
    gX_TraceLog(gX_LOG_TRACE, "SYNTH: mixer detached (idle)");
}

gX_I1 gX_Synth_IsAttached(gX_I0) {
    return g_synth.attached;
}

//ai: output mute — voices, arp/sequencer and the scope keep running; the mix
//   only stops reaching the device buffer. Defaults to muted (see Init).
gX_I0 gX_Synth_SetMuted(gX_I1 muted) {
    g_synth.muted = muted ? 1 : 0;
}

gX_I1 gX_Synth_IsMuted(gX_I0) {
    return g_synth.muted;
}

//ai: main-thread per-frame update — ticks the arpeggiator/sequencer and keeps
//   the mixer attachment in step with voice activity. The processor stays
//   attached while the delay/arp/sequencer are enabled so effect tails and
//   scheduled notes are never cut mid-flight; it leaves after a short idle
//   window once everything is silent and off.
gX_I0 gX_Synth_Update(gX_I0) {
    static gX_F32 idleSeconds = 0.0f;

    gX_F32 dt = (gX_F32)gX_FrameTime_Get();
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;

    gX_Synth_Arp_Update(dt);
    gX_Synth_Seq_Update(dt);

    gX_I1 active = g_synth.delay.enabled || g_synth.arp.enabled || g_synth.seq.playing;
    for (gX_U32 i = 0; i < GX_SYNTH_MAX_VOICES && !active; i++) {
        if (g_synth.voices[i].active) active = 1;
    }

    if (active) {
        idleSeconds = 0.0f;
        gX_Synth_MixerAttach();
    } else if (g_synth.attached) {
        idleSeconds += dt;
        if (idleSeconds >= GX_SYNTH_IDLE_DETACH_SECONDS) {
            gX_Synth_MixerDetach();
            idleSeconds = 0.0f;
        }
    }
}

//opencode: find a free voice, or steal the oldest — prefers released over active
static gX_I32 gX_Synth_FindVoice(gX_I0) {
    gX_I32 inactive   = -1;
    gX_I32 released   = -1;
    gX_U32 releasedID = 0xFFFFFFFF;
    gX_I32 oldest     = -1;
    gX_U32 oldestID   = 0xFFFFFFFF;

    for (gX_U32 i = 0; i < GX_SYNTH_MAX_VOICES; i++) {
        gX_Synth_Voice *v = &g_synth.voices[i];

        if (!v->active) {
            inactive = (gX_I32)i;
            break;
        }

        if (v->envStage == GX_SYNTH_ENV_RELEASE || v->envStage == GX_SYNTH_ENV_OFF) {
            if (v->noteID < releasedID) {
                releasedID = v->noteID;
                released   = (gX_I32)i;
            }
        }

        if (v->noteID < oldestID) {
            oldestID = v->noteID;
            oldest   = (gX_I32)i;
        }
    }

    if (inactive >= 0) return inactive;
    if (released >= 0) return released;
    return oldest;
}

//opencode: play a note
gX_I32 gX_Synth_NoteOn(gX_F32 freq, gX_F32 velocity, gX_I32 waveform) {
    gX_I32 slot = gX_Synth_FindVoice();
    if (slot < 0) return -1;
    gX_Synth_MixerAttach();

    gX_Synth_Voice *voice = &g_synth.voices[slot];

    // If stealing, force release old note first
    if (voice->active) {
        voice->release = 1;
    }

    // Portamento: if the voice was not just stolen and portamento is active, slide
    gX_F32 prevFreq = voice->frequency;
    //ai: per-voice glide time wins over the global default (0 = default).
    gX_F32 portTime = (voice->portamentoTime > 0.001f)
        ? voice->portamentoTime : g_synth.defaultPortamentoTime;

    voice->active       = 1;
    voice->release      = 0;
    voice->velocity     = (velocity < 0.0f) ? 0.0f : (velocity > 1.0f) ? 1.0f : velocity;
    voice->waveform     = (waveform < 0) ? g_synth.defaultWaveform : waveform;
    voice->phase        = 0.0f;
    voice->envelope     = 0.0f;
    voice->envStage     = GX_SYNTH_ENV_ATTACK;
    voice->releaseLevel = 0.0f;

    // Apply global ADSR defaults
    voice->attackTime   = g_synth.defaultAttackTime;
    voice->decayTime    = g_synth.defaultDecayTime;
    voice->sustainLevel = g_synth.defaultSustainLevel;
    voice->releaseTime  = g_synth.defaultReleaseTime;

    // Copy default filter settings
    voice->filterMode   = g_synth.defaultFilterMode;
    voice->filterCutoff = g_synth.defaultFilterCutoff;
    voice->filterRes    = g_synth.defaultFilterRes;
    gX_Audio_Filter_Reset(&voice->filter);

    // LFO defaults
    voice->lfoFreq     = g_synth.defaultLfoFreq;
    voice->lfoDepth    = g_synth.defaultLfoDepth;
    voice->lfoWaveform = g_synth.defaultLfoWaveform;
    voice->lfoTarget   = g_synth.defaultLfoTarget;
    voice->lfoPhase    = 0.0f;

    // Oscillator defaults
    voice->pulseWidth    = g_synth.defaultPulseWidth;
    voice->wavetableIndex = g_synth.defaultWavetableIndex;

    // FM defaults
    voice->fmAlgorithm  = g_synth.defaultFM.algorithm;
    voice->fmOp1Ratio   = g_synth.defaultFM.op1Ratio;
    voice->fmOp1Level   = g_synth.defaultFM.op1Level;
    voice->fmOp2Ratio   = g_synth.defaultFM.op2Ratio;
    voice->fmOp2Level   = g_synth.defaultFM.op2Level;
    voice->fmFeedback   = g_synth.defaultFM.feedback;
    voice->fmPhase1     = 0.0f;
    voice->fmPhase2     = 0.0f;
    voice->fmPrevSample = 0.0f;

    // Portamento: slide from previous frequency to new one
    if (portTime > 0.001f && prevFreq > 20.0f && prevFreq != freq) {
        voice->frequency     = prevFreq;
        voice->portamentoFreq = freq;
        voice->portamentoStep = (freq - prevFreq) / (portTime * (gX_F32)g_synth.sampleRate);
    } else {
        voice->frequency    = freq;
        voice->portamentoFreq = freq;
        voice->portamentoStep = 0.0f;
    }

    voice->noteID       = g_synth.nextNoteID++;

    gX_TraceLog(gX_LOG_DEBUG, "SYNTH: noteOn slot=%d freq=%.1f wave=%d id=%u port=%.2f",
        slot, (double)(voice->portamentoFreq > 0 ? voice->portamentoFreq : freq),
        waveform, voice->noteID, (double)portTime);

    return slot;
}

//opencode: stop a note by voice index
gX_I0 gX_Synth_NoteOff(gX_U32 voiceIndex) {
    if (voiceIndex >= GX_SYNTH_MAX_VOICES) return;
    gX_Synth_Voice *voice = &g_synth.voices[voiceIndex];
    if (!voice->active) return;
    voice->release = 1;
}

//opencode: stop a note by noteID
gX_I0 gX_Synth_NoteOff_ID(gX_U32 noteID) {
    for (gX_U32 i = 0; i < GX_SYNTH_MAX_VOICES; i++) {
        if (g_synth.voices[i].active && g_synth.voices[i].noteID == noteID) {
            g_synth.voices[i].release = 1;
            return;
        }
    }
}

//opencode: stop all notes
gX_I0 gX_Synth_AllNotesOff(gX_I0) {
    for (gX_U32 i = 0; i < GX_SYNTH_MAX_VOICES; i++) {
        g_synth.voices[i].release = 1;
    }
}

//opencode: set master volume
gX_I0 gX_Synth_SetMasterVolume(gX_F32 vol) {
    g_synth.masterVolume = (vol < 0.0f) ? 0.0f : (vol > 2.0f) ? 2.0f : vol;
}

//opencode: set per-voice ADSR
gX_I0 gX_Synth_SetADSR(gX_U32 voiceIndex, gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r) {
    if (voiceIndex >= GX_SYNTH_MAX_VOICES) return;
    gX_Synth_Voice *voice = &g_synth.voices[voiceIndex];
    voice->attackTime   = (a < 0.001f) ? 0.001f : a;
    voice->decayTime    = (d < 0.001f) ? 0.001f : d;
    voice->sustainLevel = (s < 0.0f) ? 0.0f : (s > 1.0f) ? 1.0f : s;
    voice->releaseTime  = (r < 0.001f) ? 0.001f : r;
}

//opencode: set per-voice pan
gX_I0 gX_Synth_SetPan(gX_U32 voiceIndex, gX_F32 pan) {
    if (voiceIndex >= GX_SYNTH_MAX_VOICES) return;
    g_synth.voices[voiceIndex].pan = (pan < -1.0f) ? -1.0f : (pan > 1.0f) ? 1.0f : pan;
}

//opencode: get voice pointer
gX_Synth_Voice *gX_Synth_GetVoice(gX_U32 index) {
    if (index >= GX_SYNTH_MAX_VOICES) return 0;
    return &g_synth.voices[index];
}

//opencode: set per-voice filter
gX_I0 gX_Synth_SetFilter(gX_U32 voiceIndex, gX_I32 mode, gX_F32 cutoff, gX_F32 res) {
    if (voiceIndex >= GX_SYNTH_MAX_VOICES) return;
    gX_Synth_Voice *voice = &g_synth.voices[voiceIndex];
    voice->filterMode   = (mode < -1) ? -1 : (mode > 2) ? 2 : mode;
    voice->filterCutoff = (cutoff < 0.001f) ? 0.001f : (cutoff > 0.999f) ? 0.999f : cutoff;
    voice->filterRes    = (res < 0.0f) ? 0.0f : (res > 1.0f) ? 1.0f : res;
    gX_Audio_Filter_Reset(&voice->filter);
}

//opencode: set default filter for new notes (applies to active voices)
gX_I0 gX_Synth_SetDefaultFilter(gX_I32 mode, gX_F32 cutoff, gX_F32 res) {
    g_synth.defaultFilterMode   = (mode < -1) ? -1 : (mode > 2) ? 2 : mode;
    g_synth.defaultFilterCutoff = (cutoff < 0.001f) ? 0.001f : (cutoff > 0.999f) ? 0.999f : cutoff;
    g_synth.defaultFilterRes    = (res < 0.0f) ? 0.0f : (res > 1.0f) ? 1.0f : res;
    gX_Synth_ApplyDefaultsToActive();
}

//opencode: set per-voice LFO
gX_I0 gX_Synth_SetLFO(gX_U32 voiceIndex, gX_I32 target, gX_F32 freq, gX_F32 depth, gX_I32 waveform) {
    if (voiceIndex >= GX_SYNTH_MAX_VOICES) return;
    gX_Synth_Voice *voice = &g_synth.voices[voiceIndex];
    voice->lfoTarget   = (target < 0) ? 0 : (target > 2) ? 2 : target;
    voice->lfoFreq     = (freq < 0.0f) ? 0.0f : (freq > 20.0f) ? 20.0f : freq;
    voice->lfoDepth    = (depth < 0.0f) ? 0.0f : (depth > 1.0f) ? 1.0f : depth;
    voice->lfoWaveform = (waveform < 0) ? 0 : (waveform > 4) ? 4 : waveform;
    voice->lfoPhase    = 0.0f;
}

//opencode: set per-voice pulse width
gX_I0 gX_Synth_SetPulseWidth(gX_U32 voiceIndex, gX_F32 pw) {
    if (voiceIndex >= GX_SYNTH_MAX_VOICES) return;
    g_synth.voices[voiceIndex].pulseWidth = (pw < 0.01f) ? 0.01f : (pw > 0.99f) ? 0.99f : pw;
}

//opencode: set global ADSR defaults (applies to active voices)
gX_I0 gX_Synth_SetDefaultADSR(gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r) {
    g_synth.defaultAttackTime  = (a < 0.001f) ? 0.001f : a;
    g_synth.defaultDecayTime   = (d < 0.001f) ? 0.001f : d;
    g_synth.defaultSustainLevel = (s < 0.0f) ? 0.0f : (s > 1.0f) ? 1.0f : s;
    g_synth.defaultReleaseTime  = (r < 0.001f) ? 0.001f : r;
    gX_Synth_ApplyDefaultsToActive();
}

// ===== Apply Defaults to Active Voices (real-time) =====

//opencode: copy all global defaults to currently active voices (real-time updates)
gX_I0 gX_Synth_ApplyDefaultsToActive(gX_I0) {
    for (gX_U32 i = 0; i < GX_SYNTH_MAX_VOICES; i++) {
        gX_Synth_Voice *v = &g_synth.voices[i];
        if (!v->active) continue;
        v->waveform     = g_synth.defaultWaveform;
        v->pulseWidth   = g_synth.defaultPulseWidth;
        v->attackTime   = g_synth.defaultAttackTime;
        v->decayTime    = g_synth.defaultDecayTime;
        v->sustainLevel = g_synth.defaultSustainLevel;
        v->releaseTime  = g_synth.defaultReleaseTime;
        v->filterMode   = g_synth.defaultFilterMode;
        v->filterCutoff = g_synth.defaultFilterCutoff;
        v->filterRes    = g_synth.defaultFilterRes;
        v->lfoFreq      = g_synth.defaultLfoFreq;
        v->lfoDepth     = g_synth.defaultLfoDepth;
        v->lfoWaveform  = g_synth.defaultLfoWaveform;
        v->lfoTarget    = g_synth.defaultLfoTarget;
        v->fmAlgorithm  = g_synth.defaultFM.algorithm;
        v->fmOp1Ratio   = g_synth.defaultFM.op1Ratio;
        v->fmOp1Level   = g_synth.defaultFM.op1Level;
        v->fmOp2Ratio   = g_synth.defaultFM.op2Ratio;
        v->fmOp2Level   = g_synth.defaultFM.op2Level;
        v->fmFeedback   = g_synth.defaultFM.feedback;
    }
}

// ===== Global Default Setters =====

//opencode: set default waveform for new notes (applies to active voices)
gX_I0 gX_Synth_SetDefaultWaveform(gX_I32 waveform) {
    g_synth.defaultWaveform = (waveform < 0) ? 0 : (waveform > 5) ? 5 : waveform;
    gX_Synth_ApplyDefaultsToActive();
}

//opencode: set default pulse width for new notes (applies to active voices)
gX_I0 gX_Synth_SetDefaultPulseWidth(gX_F32 pw) {
    g_synth.defaultPulseWidth = (pw < 0.01f) ? 0.01f : (pw > 0.99f) ? 0.99f : pw;
    gX_Synth_ApplyDefaultsToActive();
}

//opencode: set default LFO for new notes (applies to active voices)
gX_I0 gX_Synth_SetDefaultLFO(gX_F32 freq, gX_F32 depth, gX_I32 waveform, gX_I32 target) {
    g_synth.defaultLfoFreq    = (freq < 0.0f) ? 0.0f : (freq > 20.0f) ? 20.0f : freq;
    g_synth.defaultLfoDepth   = (depth < 0.0f) ? 0.0f : (depth > 1.0f) ? 1.0f : depth;
    g_synth.defaultLfoWaveform = (waveform < 0) ? 0 : (waveform > 4) ? 4 : waveform;
    g_synth.defaultLfoTarget  = (target < 0) ? 0 : (target > 2) ? 2 : target;
    gX_Synth_ApplyDefaultsToActive();
}

//opencode: set default FM for new notes (applies to active voices)
gX_I0 gX_Synth_SetDefaultFM(gX_I32 algorithm, gX_F32 op1Ratio, gX_F32 op1Level, gX_F32 op2Ratio, gX_F32 op2Level, gX_F32 feedback) {
    g_synth.defaultFM.algorithm = (algorithm < 0) ? 0 : (algorithm > 4) ? 4 : algorithm;
    g_synth.defaultFM.op1Ratio  = (op1Ratio < 0.0f) ? 0.0f : (op1Ratio > 8.0f) ? 8.0f : op1Ratio;
    g_synth.defaultFM.op1Level  = (op1Level < 0.0f) ? 0.0f : (op1Level > 1.0f) ? 1.0f : op1Level;
    g_synth.defaultFM.op2Ratio  = (op2Ratio < 0.0f) ? 0.0f : (op2Ratio > 8.0f) ? 8.0f : op2Ratio;
    g_synth.defaultFM.op2Level  = (op2Level < 0.0f) ? 0.0f : (op2Level > 1.0f) ? 1.0f : op2Level;
    g_synth.defaultFM.feedback  = (feedback < 0.0f) ? 0.0f : (feedback > 1.0f) ? 1.0f : feedback;
    gX_Synth_ApplyDefaultsToActive();
}

// ===== Portamento =====

//opencode: set per-voice portamento time
//ai: the old body was a no-op (it cleared portamentoStep and ignored time).
//   The voice keeps its glide time across notes; 0 falls back to the global
//   default set by gX_Synth_SetDefaultPortamento.
gX_I0 gX_Synth_SetPortamento(gX_U32 voiceIndex, gX_F32 time) {
    if (voiceIndex >= GX_SYNTH_MAX_VOICES) return;
    if (time < 0.0f) time = 0.0f;
    if (time > 5.0f) time = 5.0f;
    g_synth.voices[voiceIndex].portamentoTime = time;
}

//opencode: set global portamento time
gX_I0 gX_Synth_SetDefaultPortamento(gX_F32 time) {
    g_synth.defaultPortamentoTime = (time < 0.0f) ? 0.0f : (time > 5.0f) ? 5.0f : time;
}

// ===== Delay =====

//opencode: set delay parameters
gX_I0 gX_Synth_Delay_Set(gX_F32 delayTime, gX_F32 feedback, gX_F32 mix, gX_F32 stereoSpread) {
    g_synth.delay.delayTime    = (delayTime < 0.02f) ? 0.02f : (delayTime > GX_SYNTH_DELAY_MAX_SEC) ? GX_SYNTH_DELAY_MAX_SEC : delayTime;
    g_synth.delay.feedback     = (feedback < 0.0f) ? 0.0f : (feedback > 1.0f) ? 1.0f : feedback;
    g_synth.delay.mix          = (mix < 0.0f) ? 0.0f : (mix > 1.0f) ? 1.0f : mix;
    g_synth.delay.stereoSpread = (stereoSpread < 0.0f) ? 0.0f : (stereoSpread > 1.0f) ? 1.0f : stereoSpread;
}

//opencode: enable/disable delay
gX_I0 gX_Synth_Delay_Enable(gX_I1 enabled) {
    g_synth.delay.enabled = enabled;
    if (!enabled) {
        // Clear delay buffer to prevent stale echoes when re-enabled
        memset(g_synth.delayBuffer, 0, sizeof(g_synth.delayBuffer));
        g_synth.delayWritePos = 0;
    }
}

// ===== Arpeggiator =====

//opencode: add a note to the arpeggiator's held note list
gX_I0 gX_Synth_Arp_NoteOn(gX_F32 freq, gX_F32 velocity) {
    if (g_synth.arp.heldCount >= GX_SYNTH_ARP_MAX_NOTES) return;

    // Don't add duplicates
    for (gX_I32 i = 0; i < g_synth.arp.heldCount; i++) {
        if (fabsf(g_synth.arp.heldNotes[i].freq - freq) < 0.5f) return;
    }

    g_synth.arp.heldNotes[g_synth.arp.heldCount].freq     = freq;
    g_synth.arp.heldNotes[g_synth.arp.heldCount].velocity  = velocity;
    g_synth.arp.heldCount++;
}

//opencode: remove a note from the arpeggiator's held note list by frequency
gX_I0 gX_Synth_Arp_NoteOff(gX_F32 freq) {
    for (gX_I32 i = 0; i < g_synth.arp.heldCount; i++) {
        if (fabsf(g_synth.arp.heldNotes[i].freq - freq) < 0.5f) {
            // Shift remaining notes down
            for (gX_I32 j = i; j < g_synth.arp.heldCount - 1; j++) {
                g_synth.arp.heldNotes[j] = g_synth.arp.heldNotes[j + 1];
            }
            g_synth.arp.heldCount--;
            // Reset step if we emptied the list
            if (g_synth.arp.heldCount == 0) {
                g_synth.arp.currentStep = 0;
                g_synth.arp.timer = 0.0f;
            }
            return;
        }
    }
}

//opencode: clear all held arpeggiator notes
gX_I0 gX_Synth_Arp_AllNotesOff(gX_I0) {
    g_synth.arp.heldCount = 0;
    g_synth.arp.currentStep = 0;
    g_synth.arp.timer = 0.0f;
    if (g_synth.arp.lastNoteOn >= 0) {
        gX_Synth_NoteOff((gX_U32)g_synth.arp.lastNoteOn);
    }
    g_synth.arp.lastNoteOn = -1;
    g_synth.arp.noteActive = 0;
}

//opencode: set arpeggiator mode
gX_I0 gX_Synth_Arp_SetMode(gX_I32 mode) {
    g_synth.arp.mode = (mode < 0) ? 0 : (mode > 4) ? 4 : mode;
    g_synth.arp.enabled = (mode > 0) ? 1 : 0;
    g_synth.arp.currentStep = 0;
    g_synth.arp.timer = 0.0f;
    g_synth.arp.direction = 1;
}

//opencode: set arpeggiator rate (seconds between notes)
gX_I0 gX_Synth_Arp_SetRate(gX_F32 rate) {
    g_synth.arp.rate = (rate < 0.02f) ? 0.02f : (rate > 2.0f) ? 2.0f : rate;
}

//opencode: set arpeggiator gate (fraction of note time held)
gX_I0 gX_Synth_Arp_SetGate(gX_F32 gate) {
    g_synth.arp.gate = (gate < 0.01f) ? 0.01f : (gate > 1.0f) ? 1.0f : gate;
}

//opencode: update arpeggiator — call from main thread each frame with deltaTime
gX_I0 gX_Synth_Arp_Update(gX_F32 deltaTime) {
    if (!g_synth.arp.enabled || g_synth.arp.heldCount == 0) {
        if (g_synth.arp.lastNoteOn >= 0) {
            gX_Synth_NoteOff((gX_U32)g_synth.arp.lastNoteOn);
            g_synth.arp.lastNoteOn = -1;
            g_synth.arp.noteActive = 0;
        }
        return;
    }

    g_synth.arp.timer += deltaTime;

    // Note-off phase: gate has expired
    if (g_synth.arp.noteActive && g_synth.arp.timer >= g_synth.arp.rate * g_synth.arp.gate) {
        if (g_synth.arp.lastNoteOn >= 0) {
            gX_Synth_NoteOff((gX_U32)g_synth.arp.lastNoteOn);
        }
        g_synth.arp.noteActive = 0;
    }

    // Note-on phase: time for next note
    if (g_synth.arp.timer >= g_synth.arp.rate) {
        g_synth.arp.timer = 0.0f;
        g_synth.arp.noteActive = 0;

        // Select the next note based on mode
        gX_I32 idx = 0;
        switch (g_synth.arp.mode) {
            case GX_SYNTH_ARP_UP:
                idx = g_synth.arp.currentStep % g_synth.arp.heldCount;
                g_synth.arp.currentStep = (g_synth.arp.currentStep + 1) % g_synth.arp.heldCount;
                break;
            case GX_SYNTH_ARP_DOWN:
                idx = (g_synth.arp.heldCount - 1 - g_synth.arp.currentStep) % g_synth.arp.heldCount;
                g_synth.arp.currentStep = (g_synth.arp.currentStep + 1) % g_synth.arp.heldCount;
                break;
            case GX_SYNTH_ARP_UPDOWN: {
                gX_I32 maxIdx = g_synth.arp.heldCount - 1;
                if (g_synth.arp.direction > 0) {
                    idx = g_synth.arp.currentStep;
                } else {
                    idx = maxIdx - g_synth.arp.currentStep;
                }
                g_synth.arp.currentStep += g_synth.arp.direction;
                if (g_synth.arp.currentStep > maxIdx) {
                    g_synth.arp.currentStep = maxIdx - 1;
                    g_synth.arp.direction = -1;
                } else if (g_synth.arp.currentStep < 0) {
                    g_synth.arp.currentStep = 1;
                    g_synth.arp.direction = 1;
                }
                break;
            }
            case GX_SYNTH_ARP_RANDOM:
                //opencode: synth-local PCG stream (deterministic arp, no
                //   shared rand() state)
                {
                    static gX_Random32 synthRng;
                    static gX_I1 synthRngSeeded = 0;
                    if (!synthRngSeeded) {
                        gX_Random32_Seed(&synthRng, 0xA11D10u, 0u);
                        synthRngSeeded = 1;
                    }
                    idx = (gX_I32)gX_Random32_Bounded(&synthRng,
                        (gX_U32)(g_synth.arp.heldCount > 0 ? g_synth.arp.heldCount : 1));
                }
                break;
            default:
                idx = 0;
                break;
        }

        if (idx >= 0 && idx < g_synth.arp.heldCount) {
            g_synth.arp.lastNoteOn = gX_Synth_NoteOn(
                g_synth.arp.heldNotes[idx].freq,
                g_synth.arp.heldNotes[idx].velocity,
                g_synth.voices[0].waveform // use same waveform as first voice
            );
            g_synth.arp.noteActive = 1;
        }
    }
}

// ===== Patch/Preset System =====

//opencode: initialize a patch with defaults
gX_I0 gX_Synth_Patch_Init(gX_Synth_Patch *patch) {
    memset(patch, 0, sizeof(gX_Synth_Patch));
    strcpy(patch->name, "default");
    patch->waveform     = GX_SYNTH_WAVE_SQUARE;
    patch->attackTime   = 0.01f;
    patch->decayTime    = 0.1f;
    patch->sustainLevel = 0.7f;
    patch->releaseTime  = 0.3f;
    patch->filterMode   = GX_SYNTH_FILTER_LOWPASS;
    patch->filterCutoff = 0.8f;
    patch->filterRes    = 0.0f;
    patch->lfoTarget    = GX_SYNTH_LFO_PITCH;
    patch->lfoFreq      = 0.0f;
    patch->lfoDepth     = 0.0f;
    patch->lfoWaveform  = GX_SYNTH_WAVE_SINE;
    patch->pulseWidth   = 0.5f;
    patch->portamentoTime = 0.0f;
    gX_Synth_FMConfig_Init(&patch->fm);
}

//opencode: save a patch to JSON file
gX_I0 gX_Synth_Patch_Save(gX_Synth_Patch *patch, const gX_I8 *path) {
    struct json_object *root = json_object_new_object();
    if (!root) { gX_TraceLog(gX_LOG_ERROR, "SYNTH: failed to create JSON root for patch save"); return; }

    json_object_object_add(root, "name",         json_object_new_string(patch->name));
    json_object_object_add(root, "waveform",     json_object_new_int(patch->waveform));
    json_object_object_add(root, "attackTime",   json_object_new_double(patch->attackTime));
    json_object_object_add(root, "decayTime",    json_object_new_double(patch->decayTime));
    json_object_object_add(root, "sustainLevel", json_object_new_double(patch->sustainLevel));
    json_object_object_add(root, "releaseTime",  json_object_new_double(patch->releaseTime));
    json_object_object_add(root, "filterMode",   json_object_new_int(patch->filterMode));
    json_object_object_add(root, "filterCutoff", json_object_new_double(patch->filterCutoff));
    json_object_object_add(root, "filterRes",    json_object_new_double(patch->filterRes));
    json_object_object_add(root, "lfoTarget",    json_object_new_int(patch->lfoTarget));
    json_object_object_add(root, "lfoFreq",      json_object_new_double(patch->lfoFreq));
    json_object_object_add(root, "lfoDepth",     json_object_new_double(patch->lfoDepth));
    json_object_object_add(root, "lfoWaveform",  json_object_new_int(patch->lfoWaveform));
    json_object_object_add(root, "pulseWidth",   json_object_new_double(patch->pulseWidth));
    json_object_object_add(root, "portamentoTime", json_object_new_double(patch->portamentoTime));
    json_object_object_add(root, "fmAlgorithm", json_object_new_int(patch->fm.algorithm));
    json_object_object_add(root, "fmOp1Ratio",  json_object_new_double(patch->fm.op1Ratio));
    json_object_object_add(root, "fmOp1Level",  json_object_new_double(patch->fm.op1Level));
    json_object_object_add(root, "fmOp2Ratio",  json_object_new_double(patch->fm.op2Ratio));
    json_object_object_add(root, "fmOp2Level",  json_object_new_double(patch->fm.op2Level));
    json_object_object_add(root, "fmFeedback",  json_object_new_double(patch->fm.feedback));

    gX_I32 ret = json_object_to_file_ext(path, root, JSON_C_TO_STRING_PRETTY_TAB);
    json_object_put(root);

    if (ret == 0) {
        gX_TraceLog(gX_LOG_NOTICE, "SYNTH: patch '%s' saved to %s", patch->name, path);
    } else {
        gX_TraceLog(gX_LOG_ERROR, "SYNTH: failed to save patch to %s", path);
    }
}

//opencode: load a patch from JSON file
gX_I0 gX_Synth_Patch_Load(gX_Synth_Patch *patch, const gX_I8 *path) {
    struct json_object *root = json_object_from_file(path);
    if (!root) {
        gX_TraceLog(gX_LOG_ERROR, "SYNTH: failed to load patch from %s", path);
        gX_Synth_Patch_Init(patch);
        return;
    }

    gX_Synth_Patch_Init(patch);

    struct json_object *val;

    if (json_object_object_get_ex(root, "name", &val))
        strncpy(patch->name, json_object_get_string(val), GX_SYNTH_PATCH_NAME_MAX - 1);
    if (json_object_object_get_ex(root, "waveform", &val))
        patch->waveform = json_object_get_int(val);
    if (json_object_object_get_ex(root, "attackTime", &val))
        patch->attackTime = (gX_F32)json_object_get_double(val);
    if (json_object_object_get_ex(root, "decayTime", &val))
        patch->decayTime = (gX_F32)json_object_get_double(val);
    if (json_object_object_get_ex(root, "sustainLevel", &val))
        patch->sustainLevel = (gX_F32)json_object_get_double(val);
    if (json_object_object_get_ex(root, "releaseTime", &val))
        patch->releaseTime = (gX_F32)json_object_get_double(val);
    if (json_object_object_get_ex(root, "filterMode", &val))
        patch->filterMode = json_object_get_int(val);
    if (json_object_object_get_ex(root, "filterCutoff", &val))
        patch->filterCutoff = (gX_F32)json_object_get_double(val);
    if (json_object_object_get_ex(root, "filterRes", &val))
        patch->filterRes = (gX_F32)json_object_get_double(val);
    if (json_object_object_get_ex(root, "lfoTarget", &val))
        patch->lfoTarget = json_object_get_int(val);
    if (json_object_object_get_ex(root, "lfoFreq", &val))
        patch->lfoFreq = (gX_F32)json_object_get_double(val);
    if (json_object_object_get_ex(root, "lfoDepth", &val))
        patch->lfoDepth = (gX_F32)json_object_get_double(val);
    if (json_object_object_get_ex(root, "lfoWaveform", &val))
        patch->lfoWaveform = json_object_get_int(val);
    if (json_object_object_get_ex(root, "pulseWidth", &val))
        patch->pulseWidth = (gX_F32)json_object_get_double(val);
    if (json_object_object_get_ex(root, "portamentoTime", &val))
        patch->portamentoTime = (gX_F32)json_object_get_double(val);
    if (json_object_object_get_ex(root, "fmAlgorithm", &val))
        patch->fm.algorithm = json_object_get_int(val);
    if (json_object_object_get_ex(root, "fmOp1Ratio", &val))
        patch->fm.op1Ratio = (gX_F32)json_object_get_double(val);
    if (json_object_object_get_ex(root, "fmOp1Level", &val))
        patch->fm.op1Level = (gX_F32)json_object_get_double(val);
    if (json_object_object_get_ex(root, "fmOp2Ratio", &val))
        patch->fm.op2Ratio = (gX_F32)json_object_get_double(val);
    if (json_object_object_get_ex(root, "fmOp2Level", &val))
        patch->fm.op2Level = (gX_F32)json_object_get_double(val);
    if (json_object_object_get_ex(root, "fmFeedback", &val))
        patch->fm.feedback = (gX_F32)json_object_get_double(val);

    json_object_put(root);
    gX_TraceLog(gX_LOG_NOTICE, "SYNTH: patch '%s' loaded from %s", patch->name, path);
}

//opencode: apply a patch as global defaults for new notes
gX_I0 gX_Synth_Patch_Apply(gX_Synth_Patch *patch) {
    g_synth.defaultAttackTime   = patch->attackTime;
    g_synth.defaultDecayTime    = patch->decayTime;
    g_synth.defaultSustainLevel = patch->sustainLevel;
    g_synth.defaultReleaseTime  = patch->releaseTime;
    g_synth.defaultFilterMode   = patch->filterMode;
    g_synth.defaultFilterCutoff = patch->filterCutoff;
    g_synth.defaultFilterRes    = patch->filterRes;
    g_synth.defaultPortamentoTime = patch->portamentoTime;
    //ai: the patch carries the oscillator/LFO surface too — the old body
    //   dropped it, so a loaded patch kept the previous waveform/pulse
    //   width/LFO on new notes.
    g_synth.defaultWaveform    = patch->waveform;
    g_synth.defaultPulseWidth  = patch->pulseWidth;
    g_synth.defaultLfoTarget   = patch->lfoTarget;
    g_synth.defaultLfoFreq     = patch->lfoFreq;
    g_synth.defaultLfoDepth    = patch->lfoDepth;
    g_synth.defaultLfoWaveform = patch->lfoWaveform;

    // FM config (applied to all voices)
    for (gX_U32 i = 0; i < GX_SYNTH_MAX_VOICES; i++) {
        g_synth.voices[i].fmAlgorithm = patch->fm.algorithm;
        g_synth.voices[i].fmOp1Ratio  = patch->fm.op1Ratio;
        g_synth.voices[i].fmOp1Level  = patch->fm.op1Level;
        g_synth.voices[i].fmOp2Ratio  = patch->fm.op2Ratio;
        g_synth.voices[i].fmOp2Level  = patch->fm.op2Level;
        g_synth.voices[i].fmFeedback  = patch->fm.feedback;
    }

    gX_TraceLog(gX_LOG_NOTICE, "SYNTH: patch '%s' applied as global defaults", patch->name);
}

//opencode: save current global state as a named patch in the bank
gX_I0 gX_Synth_Patch_SaveToBank(gX_U32 bankIndex, const gX_I8 *name) {
    if (bankIndex >= GX_SYNTH_MAX_VOICES) return;
    gX_Synth_Patch *p = &g_synth.patchBank[bankIndex];
    gX_Synth_Patch_Init(p);
    strncpy(p->name, name, GX_SYNTH_PATCH_NAME_MAX - 1);
    p->waveform      = g_synth.defaultWaveform;
    p->attackTime    = g_synth.defaultAttackTime;
    p->decayTime     = g_synth.defaultDecayTime;
    p->sustainLevel  = g_synth.defaultSustainLevel;
    p->releaseTime   = g_synth.defaultReleaseTime;
    p->filterMode    = g_synth.defaultFilterMode;
    p->filterCutoff  = g_synth.defaultFilterCutoff;
    p->filterRes     = g_synth.defaultFilterRes;
    //ai: oscillator/LFO capture from the global defaults (the sound new notes
    //   get); the previous mix read these from voice 0, which is stale when
    //   nothing has played yet.
    p->lfoTarget     = g_synth.defaultLfoTarget;
    p->lfoFreq       = g_synth.defaultLfoFreq;
    p->lfoDepth      = g_synth.defaultLfoDepth;
    p->lfoWaveform   = g_synth.defaultLfoWaveform;
    p->pulseWidth    = g_synth.defaultPulseWidth;
    p->portamentoTime = g_synth.defaultPortamentoTime;
    p->fm.algorithm = g_synth.defaultFM.algorithm;
    p->fm.op1Ratio  = g_synth.defaultFM.op1Ratio;
    p->fm.op1Level  = g_synth.defaultFM.op1Level;
    p->fm.op2Ratio  = g_synth.defaultFM.op2Ratio;
    p->fm.op2Level  = g_synth.defaultFM.op2Level;
    p->fm.feedback  = g_synth.defaultFM.feedback;
    if (bankIndex >= g_synth.patchBankCount) g_synth.patchBankCount = bankIndex + 1;
    gX_TraceLog(gX_LOG_NOTICE, "SYNTH: saved patch '%s' to bank slot %u", name, bankIndex);
}

//opencode: load a patch from bank onto a voice (multi-timbral)
gX_I0 gX_Synth_Patch_LoadFromBank(gX_U32 voiceIndex, gX_U32 bankIndex) {
    if (voiceIndex >= GX_SYNTH_MAX_VOICES) return;
    if (bankIndex >= g_synth.patchBankCount) return;
    gX_Synth_Patch *p = &g_synth.patchBank[bankIndex];
    gX_Synth_Voice *v = &g_synth.voices[voiceIndex];
    // Apply patch to voice (it will retain these for the next note if not stolen)
    v->waveform     = p->waveform;
    v->attackTime   = p->attackTime;
    v->decayTime    = p->decayTime;
    v->sustainLevel = p->sustainLevel;
    v->releaseTime  = p->releaseTime;
    v->filterMode   = p->filterMode;
    v->filterCutoff = p->filterCutoff;
    v->filterRes    = p->filterRes;
    v->lfoTarget    = p->lfoTarget;
    v->lfoFreq      = p->lfoFreq;
    v->lfoDepth     = p->lfoDepth;
    v->lfoWaveform  = p->lfoWaveform;
    v->pulseWidth   = p->pulseWidth;
    v->fmAlgorithm  = p->fm.algorithm;
    v->fmOp1Ratio   = p->fm.op1Ratio;
    v->fmOp1Level   = p->fm.op1Level;
    v->fmOp2Ratio   = p->fm.op2Ratio;
    v->fmOp2Level   = p->fm.op2Level;
    v->fmFeedback   = p->fm.feedback;
    gX_TraceLog(gX_LOG_DEBUG, "SYNTH: loaded patch '%s' from bank %u onto voice %u", p->name, bankIndex, voiceIndex);
}

// ===== Multi-timbral NoteOn =====

//opencode: play a note with optional patch override
gX_I32 gX_Synth_NoteOnEx(gX_F32 freq, gX_F32 velocity, gX_I32 waveform, gX_I32 patchIndex) {
    gX_I32 slot = gX_Synth_NoteOn(freq, velocity, waveform);
    if (slot < 0) return -1;

    // Apply patch from bank if requested
    if (patchIndex >= 0 && (gX_U32)patchIndex < g_synth.patchBankCount) {
        gX_Synth_Patch *p = &g_synth.patchBank[patchIndex];
        gX_Synth_Voice *v = &g_synth.voices[slot];
        v->waveform     = p->waveform;
        v->attackTime   = p->attackTime;
        v->decayTime    = p->decayTime;
        v->sustainLevel = p->sustainLevel;
        v->releaseTime  = p->releaseTime;
        v->filterMode   = p->filterMode;
        v->filterCutoff = p->filterCutoff;
        v->filterRes    = p->filterRes;
        v->lfoTarget    = p->lfoTarget;
        v->lfoFreq      = p->lfoFreq;
        v->lfoDepth     = p->lfoDepth;
        v->lfoWaveform  = p->lfoWaveform;
        v->pulseWidth   = p->pulseWidth;
        v->fmAlgorithm  = p->fm.algorithm;
        v->fmOp1Ratio   = p->fm.op1Ratio;
        v->fmOp1Level   = p->fm.op1Level;
        v->fmOp2Ratio   = p->fm.op2Ratio;
        v->fmOp2Level   = p->fm.op2Level;
        v->fmFeedback   = p->fm.feedback;
    }

    return slot;
}

// ===== FM Modulation =====

//opencode: initialize FM config to defaults (off)
gX_I0 gX_Synth_FMConfig_Init(gX_Synth_FMConfig *fm) {
    fm->algorithm = GX_SYNTH_FM_OFF;
    fm->op1Ratio  = 1.0f;
    fm->op1Level  = 0.0f;
    fm->op2Ratio  = 2.0f;
    fm->op2Level  = 0.0f;
    fm->feedback  = 0.0f;
}

//opencode: set frequency modulation for a voice (algorithm + operators + feedback)
gX_I0 gX_Synth_SetFM(gX_U32 voiceIndex, gX_I32 algorithm, gX_F32 op1Ratio, gX_F32 op1Level, gX_F32 op2Ratio, gX_F32 op2Level, gX_F32 feedback) {
    if (voiceIndex >= GX_SYNTH_MAX_VOICES) return;
    gX_Synth_Voice *voice = &g_synth.voices[voiceIndex];
    voice->fmAlgorithm = (algorithm < 0) ? 0 : (algorithm > 4) ? 4 : algorithm;
    voice->fmOp1Ratio  = (op1Ratio < 0.0f) ? 0.0f : (op1Ratio > 8.0f) ? 8.0f : op1Ratio;
    voice->fmOp1Level  = (op1Level < 0.0f) ? 0.0f : (op1Level > 1.0f) ? 1.0f : op1Level;
    voice->fmOp2Ratio  = (op2Ratio < 0.0f) ? 0.0f : (op2Ratio > 8.0f) ? 8.0f : op2Ratio;
    voice->fmOp2Level  = (op2Level < 0.0f) ? 0.0f : (op2Level > 1.0f) ? 1.0f : op2Level;
    voice->fmFeedback  = (feedback < 0.0f) ? 0.0f : (feedback > 1.0f) ? 1.0f : feedback;
    voice->fmPhase1    = 0.0f;
    voice->fmPhase2    = 0.0f;
    voice->fmPrevSample = 0.0f;
}

// ===== Wavetable =====

//opencode: generate a standard waveform into a wavetable slot
gX_I0 gX_Synth_WT_Generate(gX_U32 wtIndex, gX_I32 waveform) {
    if (wtIndex >= GX_SYNTH_MAX_WT) return;
    gX_Synth_Wavetable *wt = &g_synth.wavetables[wtIndex];
    wt->size = GX_SYNTH_WT_SIZE;
    wt->loaded = 1;
    const char *wname = "sine";
    for (gX_U32 i = 0; i < GX_SYNTH_WT_SIZE; i++) {
        gX_F32 phase = (gX_F32)i / (gX_F32)GX_SYNTH_WT_SIZE;
        switch (waveform) {
            case GX_SYNTH_WAVE_SINE:
                wt->data[i] = sinf(phase * 2.0f * gX_PI) * 0.5f + 0.5f; break;
            case GX_SYNTH_WAVE_SQUARE:
                wt->data[i] = (phase < 0.5f) ? 1.0f : 0.0f; break;
            case GX_SYNTH_WAVE_SAWTOOTH:
                wt->data[i] = phase; break;
            case GX_SYNTH_WAVE_TRIANGLE:
                wt->data[i] = (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f; break;
            default:
                wt->data[i] = sinf(phase * 2.0f * gX_PI) * 0.5f + 0.5f; break;
        }
        wname = (waveform == GX_SYNTH_WAVE_SINE) ? "sine" :
                (waveform == GX_SYNTH_WAVE_SQUARE) ? "square" :
                (waveform == GX_SYNTH_WAVE_SAWTOOTH) ? "saw" :
                (waveform == GX_SYNTH_WAVE_TRIANGLE) ? "triangle" : "sine";
    }
    if (wtIndex >= g_synth.wavetableCount) g_synth.wavetableCount = wtIndex + 1;
    gX_TraceLog(gX_LOG_NOTICE, "SYNTH: generated wavetable %u (%s, %u samples)", wtIndex, wname, wt->size);
}

//opencode: load a .bin raw float32 file as a wavetable
//opencode: 0 when the table loaded, 1 otherwise — demos and probes report the
//   outcome instead of guessing from the log.
gX_I32 gX_Synth_WT_LoadFile(gX_U32 wtIndex, const gX_I8 *path) {
    if (wtIndex >= GX_SYNTH_MAX_WT) return 1;
    gX_Synth_Wavetable *wt = &g_synth.wavetables[wtIndex];

    FILE *f = fopen(path, "rb");
    if (!f) {
        gX_TraceLog(gX_LOG_ERROR, "SYNTH: failed to open wavetable file %s", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fileSize < (long)sizeof(gX_F32)) {
        gX_TraceLog(gX_LOG_ERROR, "SYNTH: wavetable file %s too small (%ld bytes)", path, fileSize);
        fclose(f);
        return 1;
    }

    gX_U32 numSamples = (gX_U32)fileSize / sizeof(gX_F32);
    if (numSamples > GX_SYNTH_WT_SIZE) numSamples = GX_SYNTH_WT_SIZE;

    for (gX_U32 i = 0; i < numSamples; i++) {
        gX_F32 sample;
        if (fread(&sample, sizeof(gX_F32), 1, f) != 1) break;
        wt->data[i] = sample;
    }
    fclose(f);
    wt->size = numSamples;
    wt->loaded = 1;

    if (wtIndex >= g_synth.wavetableCount) g_synth.wavetableCount = wtIndex + 1;
    gX_TraceLog(gX_LOG_NOTICE, "SYNTH: loaded wavetable %u from %s (%u samples)", wtIndex, path, wt->size);
    return 0;
}

//opencode: 0 when the table saved, 1 otherwise — raw little-endian float32,
//   the format gX_Synth_WT_LoadFile reads back. Shipped fixture paths are
//   refused under the harness guard (GX_NO_SETTINGS_SAVE), mirroring the GFX
//   preset guard, so a gate-launched editor can never rewrite them.
gX_I32 gX_Synth_WT_SaveFile(gX_U32 wtIndex, const gX_I8 *path) {
    if (wtIndex >= GX_SYNTH_MAX_WT) return 1;
    gX_Synth_Wavetable *wt = &g_synth.wavetables[wtIndex];
    if (!wt->loaded || wt->size == 0) {
        gX_TraceLog(gX_LOG_ERROR, "SYNTH: wavetable %u is not loaded", wtIndex);
        return 1;
    }
    if (getenv("GX_NO_SETTINGS_SAVE") && path
        && strstr(path, "resources/audio/wavetable/")) {
        gX_TraceLog(gX_LOG_ERROR,
            "SYNTH: refusing to overwrite a shipped wavetable under the harness guard: %s",
            path);
        return 1;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        gX_TraceLog(gX_LOG_ERROR, "SYNTH: failed to open wavetable file for write %s", path);
        return 1;
    }
    size_t written = fwrite(wt->data, sizeof(gX_F32), wt->size, f);
    fclose(f);
    if (written != wt->size) {
        gX_TraceLog(gX_LOG_ERROR, "SYNTH: short write for wavetable %s", path);
        return 1;
    }
    gX_TraceLog(gX_LOG_NOTICE, "SYNTH: saved wavetable %u to %s (%u samples)", wtIndex, path, wt->size);
    return 0;
}

// ===== Step Sequencer =====

//opencode: set sequencer BPM
gX_I0 gX_Synth_Seq_SetBPM(gX_F32 bpm) {
    g_synth.seq.bpm = (bpm < 20.0f) ? 20.0f : (bpm > 300.0f) ? 300.0f : bpm;
}

//opencode: set a step in a pattern
gX_I0 gX_Synth_Seq_SetStep(gX_U32 pattern, gX_U32 step, gX_F32 freq, gX_F32 velocity, gX_F32 gate) {
    if (pattern >= GX_SYNTH_SEQ_MAX_PATTERNS) return;
    if (step >= GX_SYNTH_SEQ_STEPS) return;
    g_synth.seq.patterns[pattern][step].freq     = freq;
    g_synth.seq.patterns[pattern][step].velocity  = (velocity < 0.0f) ? 0.0f : (velocity > 1.0f) ? 1.0f : velocity;
    g_synth.seq.patterns[pattern][step].gate      = (gate < 0.01f) ? 0.01f : (gate > 1.0f) ? 1.0f : gate;
}

//opencode: set pattern length (number of active steps)
gX_I0 gX_Synth_Seq_SetLength(gX_U32 pattern, gX_U32 steps) {
    if (pattern >= GX_SYNTH_SEQ_MAX_PATTERNS) return;
    g_synth.seq.patternLength[pattern] = (steps < 1) ? 1 : (steps > GX_SYNTH_SEQ_STEPS) ? GX_SYNTH_SEQ_STEPS : steps;
}

//opencode: start sequencer playback
gX_I0 gX_Synth_Seq_Play(gX_I0) {
    g_synth.seq.playing = 1;
    g_synth.seq.currentStep = 0;
    g_synth.seq.timer = 0.0f;
    g_synth.seq.noteActive = 0;
    //ai: arm the first step — the next update fires it immediately.
    g_synth.seq.started = 0;
    gX_TraceLog(gX_LOG_NOTICE, "SYNTH: sequencer started (BPM=%.0f, pattern %d, %d steps)",
        (double)g_synth.seq.bpm, g_synth.seq.currentPattern,
        g_synth.seq.patternLength[g_synth.seq.currentPattern]);
}

//opencode: stop sequencer playback
gX_I0 gX_Synth_Seq_Stop(gX_I0) {
    g_synth.seq.playing = 0;
    g_synth.seq.started = 0;
    if (g_synth.seq.lastNoteOn >= 0) {
        gX_Synth_NoteOff((gX_U32)g_synth.seq.lastNoteOn);
        g_synth.seq.lastNoteOn = -1;
    }
    g_synth.seq.noteActive = 0;
}

//ai: fire one sequencer step — a frequency starts a note, a rest (freq 0)
//   releases the sounding one immediately.
static gX_I0 gX_Synth_Seq_FireStep(gX_I32 pat, gX_I32 step) {
    gX_F32 freq = g_synth.seq.patterns[pat][step].freq;
    if (freq > 0.0f) {
        g_synth.seq.lastNoteOn = gX_Synth_NoteOn(
            freq, g_synth.seq.patterns[pat][step].velocity,
            g_synth.voices[0].waveform);
        g_synth.seq.noteActive = 1;
    } else {
        if (g_synth.seq.lastNoteOn >= 0) gX_Synth_NoteOff((gX_U32)g_synth.seq.lastNoteOn);
        g_synth.seq.lastNoteOn = -1;
        g_synth.seq.noteActive = 0;
    }
}

//opencode: update sequencer — call from main thread each frame with deltaTime
gX_I0 gX_Synth_Seq_Update(gX_F32 deltaTime) {
    if (!g_synth.seq.playing) return;

    gX_I32 pat = g_synth.seq.currentPattern;
    gX_I32 len = g_synth.seq.patternLength[pat];
    if (len <= 0) return;

    // Step time = 60.0 / BPM / stepsPerBeat
    gX_F32 stepTime = 60.0f / g_synth.seq.bpm / (gX_F32)g_synth.seq.stepsPerBeat;
    if (stepTime <= 0.0001f) return;

    //ai: the first update after Play fires the armed step — the old driver
    //   advanced before playing, so step 0 never sounded until the pattern
    //   wrapped around.
    if (!g_synth.seq.started) {
        g_synth.seq.started = 1;
        gX_Synth_Seq_FireStep(pat, g_synth.seq.currentStep);
        return;
    }

    if (deltaTime < 0.0f) deltaTime = 0.0f;
    g_synth.seq.timer += deltaTime;

    // Note-off phase: gate expired (the gate belongs to the sounding step)
    if (g_synth.seq.noteActive && g_synth.seq.timer >= stepTime * g_synth.seq.patterns[pat][g_synth.seq.currentStep].gate) {
        if (g_synth.seq.lastNoteOn >= 0) {
            gX_Synth_NoteOff((gX_U32)g_synth.seq.lastNoteOn);
        }
        g_synth.seq.noteActive = 0;
    }

    //ai: advance on a remainder-carrying timer (the old driver zeroed the
    //   timer every step, drifting off the beat unless the frame rate
    //   divided the step time). A long frame can cover several steps — the
    //   loop catches up, capped so a hitch cannot burst a whole pattern.
    gX_I32 catchUp = 0;
    while (g_synth.seq.timer >= stepTime && catchUp < 8) {
        catchUp++;
        g_synth.seq.timer -= stepTime;

        // Advance to next step
        g_synth.seq.currentStep = (g_synth.seq.currentStep + 1) % len;
        gX_Synth_Seq_FireStep(pat, g_synth.seq.currentStep);
    }
    if (g_synth.seq.timer > stepTime) g_synth.seq.timer = stepTime;
}
