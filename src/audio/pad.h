/**********************************************************************************************
*
*   [gX: src/audio/pad.h]
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


//ai: PAD — the ambient pad instrument. Where the demo synth is a general
//   chip-flavored synth and OPX is FM, PAD is built for sustained evolving
//   textures: every voice holds three detuned saw/triangle layers, each with
//   its own slow drift LFO (independent rates give the ensemble shimmer),
//   a long ADSR, and a per-voice resonant lowpass with its own slow filter
//   LFO. The engine adds a stereo ensemble chorus (modulated short delays
//   at incommensurate L/R phases) and a width control over the layer pans —
//   the two pieces that actually make a pad read as a pad.
//
//   Rendering is offline-testable (gX_Pad_Render, no audio device); the
//   raylib mixed-processor adapter attaches lazily on the first note and
//   detaches after the tails go quiet. Output rides the mixer's synth bus
//   (the instrument bus shared with the synth and OPX) and starts MUTED
//   (GX_PAD_MUTE=0 or gX_Pad_SetMuted(0) opens it) — voices and the offline
//   core run either way. The chorus delay line is heap-lazy (allocated in
//   Init, zero bss).
#ifndef AUDIO_PAD_H
#define AUDIO_PAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
//ai: the voice filter is the shared TPT/ZDF core (src/audio/filter.h).
#include "filter.h"

#define GX_PAD_MAX_VOICES 8
#define GX_PAD_LAYERS     3

//ai: layer waveforms
#define GX_PAD_WAVE_SAW 0
#define GX_PAD_WAVE_TRI 1

//ai: voice filter modes (state-variable)
#define GX_PAD_FILTER_LP 0
#define GX_PAD_FILTER_HP 1
#define GX_PAD_FILTER_BP 2

//ai: envelope stages (own copy so the module stays self-contained).
#define GX_PAD_ENV_OFF     0
#define GX_PAD_ENV_ATTACK  1
#define GX_PAD_ENV_DECAY   2
#define GX_PAD_ENV_SUSTAIN 3
#define GX_PAD_ENV_RELEASE 4

//ai: chorus delay line — 1024 frames is ~21 ms at 48 kHz; the base delay
//   (12 ms) plus the modulation depth (up to 6 ms) stays inside it.
#define GX_PAD_CHORUS_SIZE 1024

//ai: one detuned oscillator layer (template or voice instance).
typedef struct gX_Pad_Layer {
    gX_I32 waveform;      //ai: GX_PAD_WAVE_*
    gX_F32 detuneCents;   //ai: -50..50
    gX_F32 level;         //ai: 0..1
    gX_F32 pan;           //ai: -1..1 (scaled by the engine width)
    gX_F32 driftRate;     //ai: Hz, 0..1 (slow pitch drift)
    gX_F32 driftDepth;    //ai: cents, 0..30

    //ai: runtime
    gX_F32 phase;
    gX_F32 driftPhase;
} gX_Pad_Layer;

typedef struct gX_Pad_Voice {
    gX_I1  active;
    gX_I1  release;
    gX_F32 frequency;
    gX_F32 velocity;
    gX_F32 pan;           //ai: voice trim on top of the layer pans
    gX_U32 noteID;

    gX_F32 env;
    gX_F32 releaseLevel;
    gX_I32 envStage;
    gX_F32 attackTime;
    gX_F32 decayTime;
    gX_F32 sustainLevel;
    gX_F32 releaseTime;

    gX_Pad_Layer layers[GX_PAD_LAYERS];

    //ai: per-voice resonant filter + its slow LFO
    gX_I32 filterMode;
    gX_F32 filterCutoff;  //ai: 0..1 normalized
    gX_F32 filterRes;     //ai: 0..1
    gX_F32 filterLfoRate; //ai: Hz, 0..2
    gX_F32 filterLfoDepth;//ai: 0..1
    gX_F32 filterLfoPhase;
    gX_Audio_Filter filter; //ai: shared TPT core (stereo state inside)
} gX_Pad_Voice;

typedef struct gX_Pad {
    gX_Pad_Voice voices[GX_PAD_MAX_VOICES];
    gX_Pad_Layer defaultLayers[GX_PAD_LAYERS];

    gX_F32 defaultAttack, defaultDecay, defaultSustain, defaultRelease;
    gX_I32 defaultFilterMode;
    gX_F32 defaultFilterCutoff, defaultFilterRes;
    gX_F32 defaultFilterLfoRate, defaultFilterLfoDepth;

    gX_F32 masterVolume;
    gX_F32 width;         //ai: 0 = mono layer pans, 1 = full spread
    gX_I1  muted;         //ai: output mute (default on, see header note)
    gX_I1  attached;
    gX_U32 sampleRate;
    gX_U32 nextNoteID;

    //ai: stereo ensemble chorus
    gX_I1  chorusEnabled;
    gX_F32 chorusRate;    //ai: Hz (0.05..3)
    gX_F32 chorusDepth;   //ai: 0..1 -> up to 6 ms of delay modulation
    gX_F32 chorusMix;     //ai: 0..1 wet
    gX_F32 chorusStereoPhase; //ai: 0..1 L/R LFO offset (0.25 = quadrature)
    gX_F32 chorusPhase;
    gX_U32 chorusWrite;
} gX_Pad;

extern gX_Pad g_pad;

//ai: lifecycle + per-frame driver. gX_Audio_Load/Unload own Init/Unload;
//   gX_Pad_Update ticks the lazy mixer attach (kept while voices sound,
//   detached ~1 s after silence) from gX_Audio_Update.
gX_I0 gX_Pad_Init(gX_I0);
gX_I0 gX_Pad_Unload(gX_I0);
gX_I0 gX_Pad_Update(gX_I0);

//ai: offline render core — stereo interleaved float, additive into the
//   buffer (the mixed-processor adapter and the unit tests share it).
gX_I0 gX_Pad_Render(gX_F32 *samples, gX_U32 frames);
gX_I1 gX_Pad_IsAttached(gX_I0);
gX_I0 gX_Pad_SetSampleRate(gX_U32 sampleRate);

//ai: notes — returns the voice slot (0..GX_PAD_MAX_VOICES-1) or -1.
gX_I32 gX_Pad_NoteOn(gX_F32 freq, gX_F32 velocity);
gX_I0  gX_Pad_NoteOff(gX_U32 voiceIndex);
gX_I0  gX_Pad_NoteOff_ID(gX_U32 noteID);
gX_I0  gX_Pad_AllNotesOff(gX_I0);

//ai: defaults for new notes (a pad is a patch: layers, envelope, filter).
gX_I0 gX_Pad_SetDefaultLayer(gX_I32 layer, const gX_Pad_Layer *params);
gX_I0 gX_Pad_GetDefaultLayer(gX_I32 layer, gX_Pad_Layer *out);
gX_I0 gX_Pad_SetDefaultADSR(gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r);
gX_I0 gX_Pad_SetDefaultFilter(gX_I32 mode, gX_F32 cutoff, gX_F32 res);
gX_I0 gX_Pad_SetDefaultFilterLFO(gX_F32 rate, gX_F32 depth);

//ai: edits on a sounding voice.
gX_I0 gX_Pad_SetVoiceLayer(gX_U32 voiceIndex, gX_I32 layer, const gX_Pad_Layer *params);
gX_I0 gX_Pad_SetVoiceADSR(gX_U32 voiceIndex, gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r);
gX_I0 gX_Pad_SetVoiceFilter(gX_U32 voiceIndex, gX_I32 mode, gX_F32 cutoff, gX_F32 res);
gX_I0 gX_Pad_SetVoiceFilterLFO(gX_U32 voiceIndex, gX_F32 rate, gX_F32 depth);
gX_I0 gX_Pad_SetVoicePan(gX_U32 voiceIndex, gX_F32 pan);

//ai: ensemble chorus + stereo width (engine level, applied after the voice
//   mix).
gX_I0 gX_Pad_SetChorus(gX_I1 enabled, gX_F32 rate, gX_F32 depth, gX_F32 mix, gX_F32 stereoPhase);
gX_I0 gX_Pad_SetWidth(gX_F32 width);

gX_I0 gX_Pad_SetMasterVolume(gX_F32 volume);
gX_I0 gX_Pad_SetMuted(gX_I1 muted);
gX_I1 gX_Pad_IsMuted(gX_I0);

gX_Pad_Voice *gX_Pad_GetVoice(gX_U32 index);

#ifdef __cplusplus
}
#endif

#endif //AUDIO_PAD_H
