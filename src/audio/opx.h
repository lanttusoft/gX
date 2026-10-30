/**********************************************************************************************
*
*   [gX: src/audio/opx.h]
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


//ai: OPX — an 8-operator phase-modulation (FM) instrument in the DX7
//   tradition, separate from the demo synth (gX_Synth_*) so the synthvis
//   scene, the shell demo and existing patches stay untouched. Per voice:
//   8 sine operators, each with its own ratio, output level, detune,
//   self-feedback and ADSR; routing comes from a fixed data-driven
//   algorithm table (two modulation inputs per operator, DX7-style, every
//   modulator has a lower index than its target so a single forward pass
//   evaluates the graph). Modulation is phase modulation
//   (sin(2pi(phase + index))), not the demo synth's frequency sweep, so
//   high indices add harmonics instead of shifting pitch.
//
//   Rendering is offline-testable (gX_OPX_Render, no audio device); the
//   raylib mixed-processor adapter attaches lazily on the first note and
//   detaches after the voices go quiet, exactly like the synth. Output
//   rides the mixer's synth bus (GX_AUDIO_BUS_SYNTH) and starts MUTED
//   (GX_OPX_MUTE=0 or gX_OPX_SetMuted(0) opens it) — voices and the offline
//   core run either way.
#ifndef AUDIO_OPX_H
#define AUDIO_OPX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

#define GX_OPX_OPERATORS        8
#define GX_OPX_MAX_VOICES       8
#define GX_OPX_ALGORITHM_COUNT  8

//ai: level -> phase-index scale. A modulator at level 1.0 swings the
//   target's phase by +/- 2 cycles; the classic bright-but-controllable
//   range sits around 0.3..0.8.
#define GX_OPX_MOD_SCALE 2.0f

//ai: envelope stages (shared shape with the synth's; OPX keeps its own so
//   the module stays self-contained).
#define GX_OPX_ENV_OFF      0
#define GX_OPX_ENV_ATTACK   1
#define GX_OPX_ENV_DECAY    2
#define GX_OPX_ENV_SUSTAIN  3
#define GX_OPX_ENV_RELEASE  4

//ai: one operator instance (voice) or template (defaults).
typedef struct gX_OPXOperator {
    gX_I1 enabled;
    gX_F32 ratio;        //ai: frequency multiplier (0.25..16)
    gX_F32 level;        //ai: 0..1 — carrier gain / modulator index
    gX_F32 detuneCents;  //ai: -100..100
    gX_F32 feedback;     //ai: self-feedback amount 0..1 (one-sample delay)
    gX_F32 attackTime;   //ai: seconds
    gX_F32 decayTime;
    gX_F32 sustainLevel; //ai: 0..1
    gX_F32 releaseTime;

    //ai: runtime (phase/envelope/feedback history)
    gX_F32 phase;
    gX_F32 env;
    gX_F32 releaseLevel;
    gX_F32 prevOut;
    gX_F32 detuneMul;    //ai: cached 2^(cents/1200)
    gX_I32 envStage;
} gX_OPXOperator;

typedef struct gX_OPXVoice {
    gX_I1  active;
    gX_I1  release;      //ai: note-off requested
    gX_F32 frequency;    //ai: Hz (the note's fundamental)
    gX_F32 velocity;     //ai: 0..1 (output gain)
    gX_F32 pan;          //ai: -1 left .. 1 right
    gX_U32 noteID;
    gX_I32 algorithm;
    gX_OPXOperator ops[GX_OPX_OPERATORS];
} gX_OPXVoice;

typedef struct gX_OPX {
    gX_OPXVoice voices[GX_OPX_MAX_VOICES];
    //ai: operator templates copied into a voice on NoteOn (the synth's
    //   default-parameter pattern, so callers set up a patch then play).
    gX_OPXOperator defaults[GX_OPX_OPERATORS];
    gX_I32 defaultAlgorithm;
    gX_F32 masterVolume;
    gX_I1  muted;         //ai: output mute (default on, see header note)
    gX_I1  attached;      //ai: raylib mixed-processor callback registered
    gX_U32 sampleRate;
    gX_U32 nextNoteID;
} gX_OPX;

extern gX_OPX g_opx;

//ai: lifecycle + per-frame driver. gX_Audio_Load/Unload own Init/Unload;
//   gX_OPX_Update ticks the lazy mixer attach (kept while voices sound,
//   detached ~1 s after silence) from gX_Audio_Update.
gX_I0 gX_OPX_Init(gX_I0);
gX_I0 gX_OPX_Unload(gX_I0);
gX_I0 gX_OPX_Update(gX_I0);

//ai: offline render core — stereo interleaved float, additive into the
//   buffer (the mixed-processor adapter and the unit tests share it).
gX_I0 gX_OPX_Render(gX_F32 *samples, gX_U32 frames);
gX_I1 gX_OPX_IsAttached(gX_I0);
gX_I0 gX_OPX_SetSampleRate(gX_U32 sampleRate);

//ai: notes — returns the voice slot (0..GX_OPX_MAX_VOICES-1) or -1.
//   waveform-style algorithm selection is per-note (the default algorithm
//   when -1); voice stealing prefers idle > released > oldest.
gX_I32 gX_OPX_NoteOn(gX_F32 freq, gX_F32 velocity, gX_I32 algorithm);
gX_I0  gX_OPX_NoteOff(gX_U32 voiceIndex);
gX_I0  gX_OPX_NoteOff_ID(gX_U32 noteID);
gX_I0  gX_OPX_AllNotesOff(gX_I0);

//ai: global/default surface (new notes copy the current defaults).
gX_I0 gX_OPX_SetDefaultAlgorithm(gX_I32 algorithm);
gX_I32 gX_OPX_GetDefaultAlgorithm(gX_I0);
gX_I0 gX_OPX_SetDefaultOperator(gX_I32 op, gX_F32 ratio, gX_F32 level);
gX_I0 gX_OPX_SetDefaultOperatorEnabled(gX_I32 op, gX_I1 enabled);
gX_I0 gX_OPX_SetDefaultOperatorADSR(gX_I32 op, gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r);
gX_I0 gX_OPX_SetDefaultOperatorDetune(gX_I32 op, gX_F32 cents);
gX_I0 gX_OPX_SetDefaultOperatorFeedback(gX_I32 op, gX_F32 amount);

//ai: per-voice edits (a sounding note).
gX_I0 gX_OPX_SetOperator(gX_U32 voiceIndex, gX_I32 op, gX_F32 ratio, gX_F32 level);
gX_I0 gX_OPX_SetOperatorEnabled(gX_U32 voiceIndex, gX_I32 op, gX_I1 enabled);
gX_I0 gX_OPX_SetOperatorADSR(gX_U32 voiceIndex, gX_I32 op, gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r);
gX_I0 gX_OPX_SetOperatorDetune(gX_U32 voiceIndex, gX_I32 op, gX_F32 cents);
gX_I0 gX_OPX_SetOperatorFeedback(gX_U32 voiceIndex, gX_I32 op, gX_F32 amount);
gX_I0 gX_OPX_SetPan(gX_U32 voiceIndex, gX_F32 pan);

gX_OPXVoice *gX_OPX_GetVoice(gX_U32 index);

gX_I0 gX_OPX_SetMasterVolume(gX_F32 volume);
gX_I0 gX_OPX_SetMuted(gX_I1 muted);
gX_I1 gX_OPX_IsMuted(gX_I0);

//ai: algorithm table readout (names for UIs/demos; the table itself is the
//   routing data — see opx.c).
gX_I32 gX_OPX_AlgorithmCount(gX_I0);
const gX_I8 *gX_OPX_AlgorithmName(gX_I32 algorithm);
//ai: routing readout for UIs and the headless table-invariant test:
//   mods[op][m] = modulator operator index (-1 none), carriers = output
//   bitmask (bit op set = summed to the voice output), feedbackOp = the
//   self-feedback operator (-1 none). A bad algorithm reads algorithm 0.
gX_I0 gX_OPX_AlgorithmRouting(gX_I32 algorithm, gX_I8 mods[GX_OPX_OPERATORS][2],
    gX_U8 *carriers, gX_I8 *feedbackOp);

#ifdef __cplusplus
}
#endif

#endif //AUDIO_OPX_H
