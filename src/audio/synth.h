/**********************************************************************************************
*
*   [gX: src/audio/synth.h]
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


//opencode: polyphonic synthesizer engine — old-console style
#ifndef SYNTH_H
#define SYNTH_H

#include "../common/types.h"
//ai: the voice filter is the shared TPT/ZDF core (src/audio/filter.h) —
//   the old Chamberlin SVF clamped its coefficient above cutoff ~0.17 and
//   collapsed the top of the range onto one filter.
#include "filter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_SYNTH_MAX_VOICES 8

//opencode: waveform types
#define GX_SYNTH_WAVE_SINE       0
#define GX_SYNTH_WAVE_SQUARE     1
#define GX_SYNTH_WAVE_SAWTOOTH   2
#define GX_SYNTH_WAVE_TRIANGLE   3
#define GX_SYNTH_WAVE_NOISE      4
#define GX_SYNTH_WAVE_WAVETABLE  5 // uses wavetable[index] for sample data

//opencode: envelope stages
typedef enum gX_Synth_EnvStage {
    GX_SYNTH_ENV_OFF,
    GX_SYNTH_ENV_ATTACK,
    GX_SYNTH_ENV_DECAY,
    GX_SYNTH_ENV_SUSTAIN,
    GX_SYNTH_ENV_RELEASE
} gX_Synth_EnvStage;

//opencode: filter modes
#define GX_SYNTH_FILTER_NONE      -1
#define GX_SYNTH_FILTER_LOWPASS    0
#define GX_SYNTH_FILTER_HIGHPASS   1
#define GX_SYNTH_FILTER_BANDPASS   2

//opencode: LFO target modes
#define GX_SYNTH_LFO_PITCH   0
#define GX_SYNTH_LFO_AMP     1
#define GX_SYNTH_LFO_FILTER  2

//opencode: FM algorithm modes
#define GX_SYNTH_FM_OFF       0  // no FM
#define GX_SYNTH_FM_SIMPLE    1  // Op1 → Carrier (standard 2-op)
#define GX_SYNTH_FM_SERIAL    2  // Op1 → Op2 → Carrier (cascade)
#define GX_SYNTH_FM_PARALLEL  3  // Op1 + Op2 → Carrier (sum)
#define GX_SYNTH_FM_FEEDBACK  4  // Op1↻ → Carrier (self-feedback)

//opencode: arpeggiator modes
#define GX_SYNTH_ARP_OFF      0
#define GX_SYNTH_ARP_UP       1
#define GX_SYNTH_ARP_DOWN     2
#define GX_SYNTH_ARP_UPDOWN   3
#define GX_SYNTH_ARP_RANDOM   4

#define GX_SYNTH_ARP_MAX_NOTES 12
#define GX_SYNTH_PATCH_NAME_MAX 64
#define GX_SYNTH_DELAY_MAX_SEC  2.0f
#define GX_SYNTH_DELAY_BUFFER_SAMPLES 96000 /* 2.0s @ 48kHz — integer ICE for the array dim */

//opencode: sequencer constants
#define GX_SYNTH_SEQ_STEPS         16
#define GX_SYNTH_SEQ_MAX_PATTERNS  8

//opencode: wavetable constants
#define GX_SYNTH_WT_SIZE  2048
#define GX_SYNTH_MAX_WT   4

//opencode: per-voice state (read by audio thread, written by main thread)
typedef struct gX_Synth_Voice {
    gX_I1  active;         // currently sounding
    gX_I1  release;        // note-off requested

    gX_F32 frequency;      // Hz
    gX_F32 velocity;       // 0.0–1.0
    gX_I32 waveform;       // GX_SYNTH_WAVE_*

    gX_F32 phase;          // oscillator phase [0, 1)
    gX_F32 envelope;       // current envelope level [0, 1]
    gX_I32 envStage;       // gX_Synth_EnvStage

    // ADSR times (seconds)
    gX_F32 attackTime;
    gX_F32 decayTime;
    gX_F32 sustainLevel;
    gX_F32 releaseTime;
    gX_F32 releaseLevel;   // envelope level when release was triggered

    // Filter (state-variable)
    gX_I32  filterMode;    // GX_SYNTH_FILTER_*
    gX_F32  filterCutoff;  // 0.0–1.0 (normalized 0 to Nyquist)
    gX_F32  filterRes;     // 0.0–1.0 resonance
    gX_Audio_Filter filter; //ai: shared TPT core (mode/cutoff/res live above)

    // LFO
    gX_F32 lfoFreq;       // 0.0 = off, 0.1–20.0 Hz
    gX_F32 lfoDepth;      // 0.0–1.0 modulation depth
    gX_I32 lfoWaveform;   // GX_SYNTH_WAVE_* (used as LFO waveform)
    gX_I32 lfoTarget;     // GX_SYNTH_LFO_*
    gX_F32 lfoPhase;      // audio-thread only [0, 1)

    // Pulse width (square wave only)
    gX_F32 pulseWidth;    // 0.0–1.0 duty cycle, default 0.5

    // Portamento (pitch glide)
    gX_F32 portamentoFreq; // sliding frequency (approaches target)
    gX_F32 portamentoStep; // per-sample frequency increment
    //ai: per-voice glide time; 0 = use the global default (set via
    //   gX_Synth_SetPortamento, persists across notes on this voice).
    gX_F32 portamentoTime;

    // FM configuration (algorithm + operators)
    gX_I32 fmAlgorithm;   // GX_SYNTH_FM_*
    gX_F32 fmOp1Ratio;    // operator 1 freq = carrier * ratio
    gX_F32 fmOp1Level;    // operator 1 output level (0–1)
    gX_F32 fmOp2Ratio;    // operator 2 freq = carrier * ratio
    gX_F32 fmOp2Level;    // operator 2 output level (0–1)
    gX_F32 fmFeedback;    // self-feedback amount (0–1)
    gX_F32 fmPhase1;      // operator 1 phase accumulator
    gX_F32 fmPhase2;      // operator 2 phase accumulator
    gX_F32 fmPrevSample;  // previous output sample (for feedback)

    // Wavetable index (used when waveform == GX_SYNTH_WAVE_WAVETABLE)
    gX_I32 wavetableIndex; // -1 = first loaded wavetable

    gX_F32 pan;            // -1 left, 0 center, 1 right

    gX_U32 noteID;         // unique ID for matching note-offs
} gX_Synth_Voice;

//opencode: wavetable — loaded sample data for wavetable oscillator
typedef struct gX_Synth_Wavetable {
    gX_F32 data[GX_SYNTH_WT_SIZE];
    gX_U32 size;
    gX_I1  loaded;
    gX_I8  name[64];
} gX_Synth_Wavetable;

//opencode: FM configuration struct (used in Patch and Voice)
typedef struct gX_Synth_FMConfig {
    gX_I32 algorithm;    // GX_SYNTH_FM_*
    gX_F32 op1Ratio;
    gX_F32 op1Level;
    gX_F32 op2Ratio;
    gX_F32 op2Level;
    gX_F32 feedback;
} gX_Synth_FMConfig;

//opencode: arpeggiator held note entry
typedef struct gX_Synth_ArpNote {
    gX_F32 freq;
    gX_F32 velocity;
} gX_Synth_ArpNote;

//opencode: stereo delay state
typedef struct gX_Synth_Delay {
    gX_I1  enabled;
    gX_F32 delayTime;     // seconds (0.0–2.0)
    gX_F32 feedback;      // 0.0–1.0
    gX_F32 mix;           // 0.0–1.0 wet/dry
    gX_F32 stereoSpread;  // 0.0–1.0 (0=mono, 1=full ping-pong)
} gX_Synth_Delay;

//opencode: arpeggiator state (main thread driven)
typedef struct gX_Synth_Arpeggiator {
    gX_I1  enabled;
    gX_I32 mode;           // GX_SYNTH_ARP_*
    gX_F32 rate;           // seconds between notes (0.02–2.0)
    gX_F32 gate;           // fraction of note time held (0.0–1.0)

    gX_Synth_ArpNote heldNotes[GX_SYNTH_ARP_MAX_NOTES];
    gX_I32 heldCount;

    gX_I32 currentStep;    // index into heldNotes (for UP/DOWN)
    gX_F32 timer;          // counts up to rate
    gX_I32 direction;      // 1 or -1 for up/down
    gX_I32 lastNoteOn;     // voice index of current arp note
    gX_I1  noteActive;     // whether the arp note is currently sounding
} gX_Synth_Arpeggiator;

//opencode: step sequencer step
typedef struct gX_Synth_SeqStep {
    gX_F32 freq;       // 0 = rest
    gX_F32 velocity;   // 0.0–1.0
    gX_F32 gate;       // 0.0–1.0 fraction of step time
} gX_Synth_SeqStep;

//opencode: step sequencer state
typedef struct gX_Synth_Sequencer {
    gX_I1  playing;
    gX_F32 bpm;
    gX_I32 stepsPerBeat;
    gX_I32 currentStep;
    gX_F32 timer;
    gX_I32 currentPattern;
    gX_I32 lastNoteOn;
    gX_I1  noteActive;
    //ai: gX_Synth_Seq_Play arms start; the next update fires the current
    //   step immediately (the previous driver advanced before playing, so
    //   step 0 never sounded until the first wrap).
    gX_I1  started;

    gX_Synth_SeqStep patterns[GX_SYNTH_SEQ_MAX_PATTERNS][GX_SYNTH_SEQ_STEPS];
    gX_I32 patternLength[GX_SYNTH_SEQ_MAX_PATTERNS];
} gX_Synth_Sequencer;

//opencode: preset/patch — snapshot of all synth parameters for a sound
typedef struct gX_Synth_Patch {
    gX_I8  name[GX_SYNTH_PATCH_NAME_MAX];
    gX_I32 waveform;
    gX_F32 attackTime;
    gX_F32 decayTime;
    gX_F32 sustainLevel;
    gX_F32 releaseTime;
    gX_I32 filterMode;
    gX_F32 filterCutoff;
    gX_F32 filterRes;
    gX_I32 lfoTarget;
    gX_F32 lfoFreq;
    gX_F32 lfoDepth;
    gX_I32 lfoWaveform;
    gX_F32 pulseWidth;
    gX_F32 portamentoTime;
    gX_Synth_FMConfig fm;
} gX_Synth_Patch;

//opencode: top-level synth state
typedef struct gX_Synth {
    gX_Synth_Voice voices[GX_SYNTH_MAX_VOICES];
    gX_F32 masterVolume;
    gX_I1  muted;          // output mute (default on; see gX_Synth_SetMuted)
    gX_U32 sampleRate;
    gX_U32 nextNoteID;
    gX_I1  attached;       // audio processor callback is registered

    // Global filter defaults (applied to new notes)
    gX_I32  defaultFilterMode;
    gX_F32  defaultFilterCutoff;
    gX_F32  defaultFilterRes;

    // Global ADSR defaults (applied to new notes)
    gX_F32 defaultAttackTime;
    gX_F32 defaultDecayTime;
    gX_F32 defaultSustainLevel;
    gX_F32 defaultReleaseTime;

    // Delay effect
    gX_Synth_Delay delay;
    gX_F32 delayBuffer[2][GX_SYNTH_DELAY_BUFFER_SAMPLES];
    gX_U32 delayWritePos;
    gX_U32 delayBufferSize;

    // Arpeggiator
    gX_Synth_Arpeggiator arp;

    // Portamento global time
    gX_F32 defaultPortamentoTime; // 0.0 = off

    // Global oscillator defaults (applied to new notes)
    gX_I32 defaultWaveform;       // GX_SYNTH_WAVE_*
    gX_F32 defaultPulseWidth;     // 0.01–0.99
    gX_I32 defaultWavetableIndex; // -1 = first loaded

    // Global LFO defaults (applied to new notes)
    gX_F32 defaultLfoFreq;
    gX_F32 defaultLfoDepth;
    gX_I32 defaultLfoWaveform;
    gX_I32 defaultLfoTarget;

    // Global FM defaults (applied to new notes)
    gX_Synth_FMConfig defaultFM;

    // Wavetables (for wavetable oscillator)
    gX_Synth_Wavetable wavetables[GX_SYNTH_MAX_WT];
    gX_U32 wavetableCount;

    // Patch bank (multi-timbral: each voice can use a different patch)
    gX_Synth_Patch patchBank[GX_SYNTH_MAX_VOICES];
    gX_U32 patchBankCount;

    // Step sequencer
    gX_Synth_Sequencer seq;
} gX_Synth;

extern gX_Synth g_synth;

gX_I0  gX_Synth_Init(gX_I0);
gX_I0  gX_Synth_Uninit(gX_I0);

//ai: main-thread per-frame update — ticks the arpeggiator/sequencer and keeps
//   the mixer attached while voices sound (lazy attach, idle detach).
gX_I0  gX_Synth_Update(gX_I0);
//ai: offline render core — stereo interleaved float at the synth sample rate.
//   The raylib mixed-processor adapter calls this; unit tests render with it
//   directly (no audio device needed).
gX_I0  gX_Synth_Render(gX_F32 *samples, gX_U32 frames);
//ai: mixer attachment state (see gX_Synth_Update).
gX_I1  gX_Synth_IsAttached(gX_I0);
//ai: output mute — defaults to on; GX_SYNTH_MUTE=0 at launch or
//   gX_Synth_SetMuted(0) opens the output. Voices, arp/sequencer and the
//   scope keep running while muted; only the device buffer stays untouched.
gX_I0  gX_Synth_SetMuted(gX_I1 muted);
gX_I1  gX_Synth_IsMuted(gX_I0);

// Returns voice index (0–7) or -1 if all voices busy
gX_I32 gX_Synth_NoteOn(gX_F32 freq, gX_F32 velocity, gX_I32 waveform);
gX_I32 gX_Synth_NoteOnEx(gX_F32 freq, gX_F32 velocity, gX_I32 waveform, gX_I32 patchIndex);
gX_I0  gX_Synth_NoteOff(gX_U32 voiceIndex);
gX_I0  gX_Synth_NoteOff_ID(gX_U32 noteID);
gX_I0  gX_Synth_AllNotesOff(gX_I0);

gX_I0  gX_Synth_SetMasterVolume(gX_F32 vol);
gX_I0  gX_Synth_SetADSR(gX_U32 voiceIndex, gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r);
gX_I0  gX_Synth_SetPan(gX_U32 voiceIndex, gX_F32 pan);

// Filter control
gX_I0  gX_Synth_SetFilter(gX_U32 voiceIndex, gX_I32 mode, gX_F32 cutoff, gX_F32 res);
gX_I0  gX_Synth_SetDefaultFilter(gX_I32 mode, gX_F32 cutoff, gX_F32 res);

// LFO control
gX_I0  gX_Synth_SetLFO(gX_U32 voiceIndex, gX_I32 target, gX_F32 freq, gX_F32 depth, gX_I32 waveform);

// FM modulation (algorithm with operators and feedback)
gX_I0  gX_Synth_SetFM(gX_U32 voiceIndex, gX_I32 algorithm, gX_F32 op1Ratio, gX_F32 op1Level, gX_F32 op2Ratio, gX_F32 op2Level, gX_F32 feedback);
gX_I0  gX_Synth_FMConfig_Init(gX_Synth_FMConfig *fm);

// Pulse width (square wave only)
gX_I0  gX_Synth_SetPulseWidth(gX_U32 voiceIndex, gX_F32 pw);

// Global ADSR defaults (applied to all new notes)
gX_I0  gX_Synth_SetDefaultADSR(gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r);

// Apply global defaults to all currently active voices (real-time updates)
gX_I0  gX_Synth_ApplyDefaultsToActive(gX_I0);

// Global oscillator defaults
gX_I0  gX_Synth_SetDefaultWaveform(gX_I32 waveform);
gX_I0  gX_Synth_SetDefaultPulseWidth(gX_F32 pw);

// Global LFO defaults
gX_I0  gX_Synth_SetDefaultLFO(gX_F32 freq, gX_F32 depth, gX_I32 waveform, gX_I32 target);

// Global FM defaults
gX_I0  gX_Synth_SetDefaultFM(gX_I32 algorithm, gX_F32 op1Ratio, gX_F32 op1Level, gX_F32 op2Ratio, gX_F32 op2Level, gX_F32 feedback);

// Portamento
gX_I0  gX_Synth_SetPortamento(gX_U32 voiceIndex, gX_F32 time);
gX_I0  gX_Synth_SetDefaultPortamento(gX_F32 time);

// Direct voice parameter access for scripting/bulk edits
gX_Synth_Voice *gX_Synth_GetVoice(gX_U32 index);

// Delay control
gX_I0  gX_Synth_Delay_Set(gX_F32 delayTime, gX_F32 feedback, gX_F32 mix, gX_F32 stereoSpread);
gX_I0  gX_Synth_Delay_Enable(gX_I1 enabled);

// Arpeggiator control
gX_I0  gX_Synth_Arp_NoteOn(gX_F32 freq, gX_F32 velocity);
gX_I0  gX_Synth_Arp_NoteOff(gX_F32 freq);
gX_I0  gX_Synth_Arp_AllNotesOff(gX_I0);
gX_I0  gX_Synth_Arp_SetMode(gX_I32 mode);
gX_I0  gX_Synth_Arp_SetRate(gX_F32 rate);
gX_I0  gX_Synth_Arp_SetGate(gX_F32 gate);
gX_I0  gX_Synth_Arp_Update(gX_F32 deltaTime); // called from main thread each frame

// Patch system
gX_I0  gX_Synth_Patch_Init(gX_Synth_Patch *patch);
gX_I0  gX_Synth_Patch_Save(gX_Synth_Patch *patch, const gX_I8 *path);
gX_I0  gX_Synth_Patch_Load(gX_Synth_Patch *patch, const gX_I8 *path);
gX_I0  gX_Synth_Patch_Apply(gX_Synth_Patch *patch);
gX_I0  gX_Synth_Patch_SaveToBank(gX_U32 bankIndex, const gX_I8 *name);
gX_I0  gX_Synth_Patch_LoadFromBank(gX_U32 voiceIndex, gX_U32 bankIndex);

// Wavetable
gX_I0  gX_Synth_WT_Generate(gX_U32 wtIndex, gX_I32 waveform);
gX_I32 gX_Synth_WT_LoadFile(gX_U32 wtIndex, const gX_I8 *path); // 0 loaded, 1 failed
gX_I32 gX_Synth_WT_SaveFile(gX_U32 wtIndex, const gX_I8 *path); // 0 saved, 1 failed

// Step sequencer
gX_I0  gX_Synth_Seq_SetBPM(gX_F32 bpm);
gX_I0  gX_Synth_Seq_SetStep(gX_U32 pattern, gX_U32 step, gX_F32 freq, gX_F32 velocity, gX_F32 gate);
gX_I0  gX_Synth_Seq_SetLength(gX_U32 pattern, gX_U32 steps);
gX_I0  gX_Synth_Seq_Play(gX_I0);
gX_I0  gX_Synth_Seq_Stop(gX_I0);
gX_I0  gX_Synth_Seq_Update(gX_F32 deltaTime);

// Scope capture ring buffer (audio thread writes, main thread reads for visualization)
#define GX_SYNTH_SCOPE_SIZE 2048
extern gX_F32  g_synthScope[GX_SYNTH_SCOPE_SIZE];
extern gX_U32  g_synthScopeCount;
extern gX_U32  g_synthScopeWrite;  // ring write position

#ifdef __cplusplus
}
#endif

#endif //SYNTH_H
