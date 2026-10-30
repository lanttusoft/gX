/**********************************************************************************************
*
*   [gX: src/scripting/tour/tour_synth.cpp]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu, opencode
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


//ai: tour synth API — script surface over gX's polyphonic synth (the engine
//   driver in src/audio/synth.c ticks the arpeggiator/sequencer and attaches
//   the mixer lazily while voices sound, so scripts only play notes and set
//   parameters). Notes are frequencies; NoteOn returns the voice slot for
//   per-voice parameter edits. The preset functions cover the full engine
//   state (global defaults + patch bank + sequencer patterns).
//opencode: raylib headers FIRST with C++ linkage — wrapper headers reached
//   through other includes would give raymath's operator overloads C linkage
//   (see src/tools/gfx/gfx_editor.cpp).
#include "raylib.h"
#include "raymath.h"

#include "tour.h"

#include <angelscript.h>

#include <string>

#include "../../common/log.h"
#include "../../audio/synth.h"
#include "../../audio/synth_schema.h"

// ===========================================================================
// Notes
// ===========================================================================

static gX_I32 TourSynth_NoteOn(gX_F32 freq, gX_F32 velocity) {
    return gX_Synth_NoteOn(freq, velocity, -1);
}

static gX_I32 TourSynth_NoteOnPatch(gX_F32 freq, gX_F32 velocity, gX_I32 patchIndex) {
    return gX_Synth_NoteOnEx(freq, velocity, -1, patchIndex);
}

static void TourSynth_NoteOff(gX_I32 voice) {
    if (voice >= 0) gX_Synth_NoteOff((gX_U32)voice);
}

static void TourSynth_AllNotesOff(void) {
    gX_Synth_AllNotesOff();
}

static gX_I1 TourSynth_VoiceActive(gX_I32 voice) {
    if (voice < 0 || voice >= GX_SYNTH_MAX_VOICES) return 0;
    return g_synth.voices[voice].active;
}

static gX_I32 TourSynth_GetVoiceCount(void) {
    return GX_SYNTH_MAX_VOICES;
}

// ===========================================================================
// Per-voice parameters
// ===========================================================================

static void TourSynth_SetADSR(gX_I32 voice, gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r) {
    if (voice >= 0) gX_Synth_SetADSR((gX_U32)voice, a, d, s, r);
}

static void TourSynth_SetPan(gX_I32 voice, gX_F32 pan) {
    if (voice >= 0) gX_Synth_SetPan((gX_U32)voice, pan);
}

static void TourSynth_SetFilter(gX_I32 voice, gX_I32 mode, gX_F32 cutoff, gX_F32 res) {
    if (voice >= 0) gX_Synth_SetFilter((gX_U32)voice, mode, cutoff, res);
}

static void TourSynth_SetLFO(gX_I32 voice, gX_I32 target, gX_F32 freq, gX_F32 depth, gX_I32 waveform) {
    if (voice >= 0) gX_Synth_SetLFO((gX_U32)voice, target, freq, depth, waveform);
}

static void TourSynth_SetFM(gX_I32 voice, gX_I32 algorithm, gX_F32 op1Ratio, gX_F32 op1Level,
    gX_F32 op2Ratio, gX_F32 op2Level, gX_F32 feedback)
{
    if (voice >= 0) gX_Synth_SetFM((gX_U32)voice, algorithm, op1Ratio, op1Level,
        op2Ratio, op2Level, feedback);
}

static void TourSynth_SetPulseWidth(gX_I32 voice, gX_F32 pw) {
    if (voice >= 0) gX_Synth_SetPulseWidth((gX_U32)voice, pw);
}

static void TourSynth_SetPortamento(gX_I32 voice, gX_F32 time) {
    if (voice >= 0) gX_Synth_SetPortamento((gX_U32)voice, time);
}

// ===========================================================================
// Global defaults (applied to new notes)
// ===========================================================================

static void TourSynth_SetDefaultWaveform(gX_I32 waveform) {
    gX_Synth_SetDefaultWaveform(waveform);
}

static void TourSynth_SetDefaultADSR(gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r) {
    gX_Synth_SetDefaultADSR(a, d, s, r);
}

static void TourSynth_SetDefaultFilter(gX_I32 mode, gX_F32 cutoff, gX_F32 res) {
    gX_Synth_SetDefaultFilter(mode, cutoff, res);
}

static void TourSynth_SetDefaultLFO(gX_F32 freq, gX_F32 depth, gX_I32 waveform, gX_I32 target) {
    gX_Synth_SetDefaultLFO(freq, depth, waveform, target);
}

static void TourSynth_SetDefaultFM(gX_I32 algorithm, gX_F32 op1Ratio, gX_F32 op1Level,
    gX_F32 op2Ratio, gX_F32 op2Level, gX_F32 feedback)
{
    gX_Synth_SetDefaultFM(algorithm, op1Ratio, op1Level, op2Ratio, op2Level, feedback);
}

static void TourSynth_SetDefaultPulseWidth(gX_F32 pw) {
    gX_Synth_SetDefaultPulseWidth(pw);
}

static void TourSynth_SetDefaultPortamento(gX_F32 time) {
    gX_Synth_SetDefaultPortamento(time);
}

static void TourSynth_SetMasterVolume(gX_F32 vol) {
    gX_Synth_SetMasterVolume(vol);
}

static void TourSynth_ApplyDefaultsToActive(void) {
    gX_Synth_ApplyDefaultsToActive();
}

// ===========================================================================
// Delay
// ===========================================================================

static void TourSynth_SetDelay(gX_F32 time, gX_F32 feedback, gX_F32 mix, gX_F32 spread) {
    gX_Synth_Delay_Set(time, feedback, mix, spread);
}

static void TourSynth_SetDelayEnable(gX_I1 enable) {
    gX_Synth_Delay_Enable(enable ? 1 : 0);
}

// ===========================================================================
// Arpeggiator + sequencer (the engine ticks both per frame)
// ===========================================================================

static void TourSynth_ArpNoteOn(gX_F32 freq, gX_F32 velocity) {
    gX_Synth_Arp_NoteOn(freq, velocity);
}

static void TourSynth_ArpNoteOff(gX_F32 freq) {
    gX_Synth_Arp_NoteOff(freq);
}

static void TourSynth_ArpAllNotesOff(void) {
    gX_Synth_Arp_AllNotesOff();
}

static void TourSynth_ArpSetMode(gX_I32 mode) {
    gX_Synth_Arp_SetMode(mode);
}

static void TourSynth_ArpSetRate(gX_F32 rate) {
    gX_Synth_Arp_SetRate(rate);
}

static void TourSynth_ArpSetGate(gX_F32 gate) {
    gX_Synth_Arp_SetGate(gate);
}

static void TourSynth_SeqSetBPM(gX_F32 bpm) {
    gX_Synth_Seq_SetBPM(bpm);
}

static void TourSynth_SeqSetStep(gX_I32 pattern, gX_I32 step, gX_F32 freq, gX_F32 velocity, gX_F32 gate) {
    if (pattern >= 0 && step >= 0)
        gX_Synth_Seq_SetStep((gX_U32)pattern, (gX_U32)step, freq, velocity, gate);
}

static void TourSynth_SeqSetLength(gX_I32 pattern, gX_I32 steps) {
    if (pattern >= 0) gX_Synth_Seq_SetLength((gX_U32)pattern, (gX_U32)steps);
}

static void TourSynth_SeqPlay(void) {
    gX_Synth_Seq_Play();
}

static void TourSynth_SeqStop(void) {
    gX_Synth_Seq_Stop();
}

// ===========================================================================
// Patches, presets, wavetables
// ===========================================================================

static gX_I32 TourSynth_PatchSaveToBank(gX_I32 bank, const std::string &name) {
    if (bank < 0 || bank >= GX_SYNTH_MAX_VOICES) return 1;
    gX_Synth_Patch_SaveToBank((gX_U32)bank, (const gX_I8 *)name.c_str());
    return 0;
}

static void TourSynth_PatchLoadFromBank(gX_I32 voice, gX_I32 bank) {
    if (voice >= 0 && bank >= 0) gX_Synth_Patch_LoadFromBank((gX_U32)voice, (gX_U32)bank);
}

//opencode: 0 on success, 1 on failure (the schema returns are a real signal
//   for demo status lines).
static gX_I32 TourSynth_PresetSave(const std::string &path) {
    return gX_Synth_Preset_Save((const gX_I8 *)path.c_str());
}

static gX_I32 TourSynth_PresetLoad(const std::string &path) {
    return gX_Synth_Preset_Load((const gX_I8 *)path.c_str());
}

static void TourSynth_WavetableGenerate(gX_I32 index, gX_I32 waveform) {
    if (index >= 0) gX_Synth_WT_Generate((gX_U32)index, waveform);
}

static gX_I32 TourSynth_WavetableLoad(gX_I32 index, const std::string &path) {
    if (index < 0) return 1;
    return gX_Synth_WT_LoadFile((gX_U32)index, (const gX_I8 *)path.c_str());
}

// ===========================================================================
// Defaults readback + patch bank state
// ===========================================================================

//opencode: readback for UI sync (a preset load pushes engine state back into
//   a demo's own controls) and for selftest assertions. Each getter mirrors
//   the g_synth default it belongs to; no engine-side setters needed.
static gX_F32 TourSynth_GetDefaultAttack(void)       { return g_synth.defaultAttackTime; }
static gX_F32 TourSynth_GetDefaultDecay(void)        { return g_synth.defaultDecayTime; }
static gX_F32 TourSynth_GetDefaultSustain(void)      { return g_synth.defaultSustainLevel; }
static gX_F32 TourSynth_GetDefaultRelease(void)      { return g_synth.defaultReleaseTime; }
static gX_I32 TourSynth_GetDefaultFilterMode(void)   { return g_synth.defaultFilterMode; }
static gX_F32 TourSynth_GetDefaultFilterCutoff(void) { return g_synth.defaultFilterCutoff; }
static gX_F32 TourSynth_GetDefaultFilterRes(void)    { return g_synth.defaultFilterRes; }
static gX_F32 TourSynth_GetDefaultLfoFreq(void)      { return g_synth.defaultLfoFreq; }
static gX_F32 TourSynth_GetDefaultLfoDepth(void)     { return g_synth.defaultLfoDepth; }
static gX_I32 TourSynth_GetDefaultLfoTarget(void)    { return g_synth.defaultLfoTarget; }
static gX_I32 TourSynth_GetDefaultFmAlgorithm(void)  { return g_synth.defaultFM.algorithm; }
static gX_F32 TourSynth_GetDefaultFmLevel(void)      { return g_synth.defaultFM.op1Level; }
static gX_F32 TourSynth_GetDefaultDelayTime(void)    { return g_synth.delay.delayTime; }
static gX_F32 TourSynth_GetDefaultDelayFeedback(void){ return g_synth.delay.feedback; }
static gX_F32 TourSynth_GetDefaultDelayMix(void)     { return g_synth.delay.mix; }
static gX_F32 TourSynth_GetMasterVolume(void)        { return g_synth.masterVolume; }
static gX_I32 TourSynth_GetDefaultWaveform(void)     { return g_synth.defaultWaveform; }
static gX_I32 TourSynth_GetDefaultArpMode(void)      { return g_synth.arp.mode; }
static gX_F32 TourSynth_GetDefaultArpRate(void)      { return g_synth.arp.rate; }
static gX_F32 TourSynth_GetDefaultArpGate(void)      { return g_synth.arp.gate; }

static gX_I32 TourSynth_PatchCount(void) {
    return (gX_I32)g_synth.patchBankCount;
}

static gX_F32 TourSynth_VoiceFilterCutoff(gX_I32 voice) {
    if (voice < 0 || voice >= GX_SYNTH_MAX_VOICES) return -1.0f;
    return g_synth.voices[voice].filterCutoff;
}

// ===========================================================================
// Wavetable access (preview + index selection)
// ===========================================================================

static void TourSynth_SetWavetableIndex(gX_I32 index) {
    g_synth.defaultWavetableIndex = index;
}

static gX_I32 TourSynth_WavetableCount(void) {
    return (gX_I32)g_synth.wavetableCount;
}

static gX_I32 TourSynth_WavetableSize(gX_I32 index) {
    if (index < 0 || index >= GX_SYNTH_MAX_WT) return 0;
    return (gX_I32)g_synth.wavetables[index].size;
}

static gX_F32 TourSynth_WavetableSample(gX_I32 index, gX_I32 sample) {
    if (index < 0 || index >= GX_SYNTH_MAX_WT) return 0.0f;
    gX_Synth_Wavetable *wt = &g_synth.wavetables[index];
    if (!wt->loaded || sample < 0 || sample >= (gX_I32)wt->size) return 0.0f;
    return wt->data[sample];
}

// ===========================================================================
// State + scope
// ===========================================================================

static gX_I1 TourSynth_IsAttached(void) {
    return gX_Synth_IsAttached();
}

static void TourSynth_SetMuted(gX_I1 muted) {
    gX_Synth_SetMuted(muted ? 1 : 0);
}

static gX_I1 TourSynth_IsMuted(void) {
    return gX_Synth_IsMuted();
}

static gX_I32 TourSynth_ScopeCount(void) {
    return (gX_I32)g_synthScopeCount;
}

static gX_F32 TourSynth_ScopeAt(gX_I32 index) {
    if (index < 0 || index >= (gX_I32)GX_SYNTH_SCOPE_SIZE) return 0.0f;
    return g_synthScope[index];
}

// ===========================================================================
// Registration
// ===========================================================================

#define AS_CHECK(r) do { if (r < 0) \
    gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "AS tour-synth registration failed (code %d)", r); \
} while(0)

gX_I32 gX_TourSynth_Register(asIScriptEngine *engine) {
    int r = 0;

    // --- Notes ---
    r = engine->RegisterGlobalFunction("int Synth_NoteOn(float, float)",
        asFUNCTION(TourSynth_NoteOn), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Synth_NoteOnPatch(float, float, int)",
        asFUNCTION(TourSynth_NoteOnPatch), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_NoteOff(int)",
        asFUNCTION(TourSynth_NoteOff), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_AllNotesOff()",
        asFUNCTION(TourSynth_AllNotesOff), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool Synth_VoiceActive(int)",
        asFUNCTION(TourSynth_VoiceActive), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Synth_GetVoiceCount()",
        asFUNCTION(TourSynth_GetVoiceCount), asCALL_CDECL); AS_CHECK(r);

    // --- Per-voice parameters ---
    r = engine->RegisterGlobalFunction("void Synth_SetADSR(int, float, float, float, float)",
        asFUNCTION(TourSynth_SetADSR), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetPan(int, float)",
        asFUNCTION(TourSynth_SetPan), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetFilter(int, int, float, float)",
        asFUNCTION(TourSynth_SetFilter), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetLFO(int, int, float, float, int)",
        asFUNCTION(TourSynth_SetLFO), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetFM(int, int, float, float, float, float, float)",
        asFUNCTION(TourSynth_SetFM), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetPulseWidth(int, float)",
        asFUNCTION(TourSynth_SetPulseWidth), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetPortamento(int, float)",
        asFUNCTION(TourSynth_SetPortamento), asCALL_CDECL); AS_CHECK(r);

    // --- Global defaults ---
    r = engine->RegisterGlobalFunction("void Synth_SetDefaultWaveform(int)",
        asFUNCTION(TourSynth_SetDefaultWaveform), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetDefaultADSR(float, float, float, float)",
        asFUNCTION(TourSynth_SetDefaultADSR), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetDefaultFilter(int, float, float)",
        asFUNCTION(TourSynth_SetDefaultFilter), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetDefaultLFO(float, float, int, int)",
        asFUNCTION(TourSynth_SetDefaultLFO), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetDefaultFM(int, float, float, float, float, float)",
        asFUNCTION(TourSynth_SetDefaultFM), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetDefaultPulseWidth(float)",
        asFUNCTION(TourSynth_SetDefaultPulseWidth), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetDefaultPortamento(float)",
        asFUNCTION(TourSynth_SetDefaultPortamento), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetMasterVolume(float)",
        asFUNCTION(TourSynth_SetMasterVolume), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_ApplyDefaultsToActive()",
        asFUNCTION(TourSynth_ApplyDefaultsToActive), asCALL_CDECL); AS_CHECK(r);

    // --- Delay ---
    r = engine->RegisterGlobalFunction("void Synth_SetDelay(float, float, float, float)",
        asFUNCTION(TourSynth_SetDelay), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetDelayEnable(bool)",
        asFUNCTION(TourSynth_SetDelayEnable), asCALL_CDECL); AS_CHECK(r);

    // --- Arpeggiator ---
    r = engine->RegisterGlobalFunction("void Synth_Arp_NoteOn(float, float)",
        asFUNCTION(TourSynth_ArpNoteOn), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_Arp_NoteOff(float)",
        asFUNCTION(TourSynth_ArpNoteOff), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_Arp_AllNotesOff()",
        asFUNCTION(TourSynth_ArpAllNotesOff), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_Arp_SetMode(int)",
        asFUNCTION(TourSynth_ArpSetMode), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_Arp_SetRate(float)",
        asFUNCTION(TourSynth_ArpSetRate), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_Arp_SetGate(float)",
        asFUNCTION(TourSynth_ArpSetGate), asCALL_CDECL); AS_CHECK(r);

    // --- Sequencer ---
    r = engine->RegisterGlobalFunction("void Synth_Seq_SetBPM(float)",
        asFUNCTION(TourSynth_SeqSetBPM), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_Seq_SetStep(int, int, float, float, float)",
        asFUNCTION(TourSynth_SeqSetStep), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_Seq_SetLength(int, int)",
        asFUNCTION(TourSynth_SeqSetLength), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_Seq_Play()",
        asFUNCTION(TourSynth_SeqPlay), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_Seq_Stop()",
        asFUNCTION(TourSynth_SeqStop), asCALL_CDECL); AS_CHECK(r);

    // --- Patches, presets, wavetables ---
    r = engine->RegisterGlobalFunction("int Synth_Patch_SaveToBank(int, const string &in)",
        asFUNCTION(TourSynth_PatchSaveToBank), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_Patch_LoadFromBank(int, int)",
        asFUNCTION(TourSynth_PatchLoadFromBank), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Synth_Preset_Save(const string &in)",
        asFUNCTION(TourSynth_PresetSave), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Synth_Preset_Load(const string &in)",
        asFUNCTION(TourSynth_PresetLoad), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_WavetableGenerate(int, int)",
        asFUNCTION(TourSynth_WavetableGenerate), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Synth_WavetableLoad(int, const string &in)",
        asFUNCTION(TourSynth_WavetableLoad), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetWavetableIndex(int)",
        asFUNCTION(TourSynth_SetWavetableIndex), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Synth_WavetableCount()",
        asFUNCTION(TourSynth_WavetableCount), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Synth_WavetableSize(int)",
        asFUNCTION(TourSynth_WavetableSize), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_WavetableSample(int, int)",
        asFUNCTION(TourSynth_WavetableSample), asCALL_CDECL); AS_CHECK(r);

    // --- Defaults readback (UI sync after a preset load) ---
    r = engine->RegisterGlobalFunction("float Synth_GetDefaultAttack()",
        asFUNCTION(TourSynth_GetDefaultAttack), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_GetDefaultDecay()",
        asFUNCTION(TourSynth_GetDefaultDecay), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_GetDefaultSustain()",
        asFUNCTION(TourSynth_GetDefaultSustain), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_GetDefaultRelease()",
        asFUNCTION(TourSynth_GetDefaultRelease), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Synth_GetDefaultFilterMode()",
        asFUNCTION(TourSynth_GetDefaultFilterMode), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_GetDefaultFilterCutoff()",
        asFUNCTION(TourSynth_GetDefaultFilterCutoff), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_GetDefaultFilterRes()",
        asFUNCTION(TourSynth_GetDefaultFilterRes), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_GetDefaultLfoFreq()",
        asFUNCTION(TourSynth_GetDefaultLfoFreq), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_GetDefaultLfoDepth()",
        asFUNCTION(TourSynth_GetDefaultLfoDepth), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Synth_GetDefaultLfoTarget()",
        asFUNCTION(TourSynth_GetDefaultLfoTarget), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Synth_GetDefaultFmAlgorithm()",
        asFUNCTION(TourSynth_GetDefaultFmAlgorithm), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_GetDefaultFmLevel()",
        asFUNCTION(TourSynth_GetDefaultFmLevel), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_GetDefaultDelayTime()",
        asFUNCTION(TourSynth_GetDefaultDelayTime), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_GetDefaultDelayFeedback()",
        asFUNCTION(TourSynth_GetDefaultDelayFeedback), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_GetDefaultDelayMix()",
        asFUNCTION(TourSynth_GetDefaultDelayMix), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_GetMasterVolume()",
        asFUNCTION(TourSynth_GetMasterVolume), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Synth_GetDefaultWaveform()",
        asFUNCTION(TourSynth_GetDefaultWaveform), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Synth_GetDefaultArpMode()",
        asFUNCTION(TourSynth_GetDefaultArpMode), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_GetDefaultArpRate()",
        asFUNCTION(TourSynth_GetDefaultArpRate), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_GetDefaultArpGate()",
        asFUNCTION(TourSynth_GetDefaultArpGate), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Synth_PatchCount()",
        asFUNCTION(TourSynth_PatchCount), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_VoiceFilterCutoff(int)",
        asFUNCTION(TourSynth_VoiceFilterCutoff), asCALL_CDECL); AS_CHECK(r);

    // --- State + scope ---
    r = engine->RegisterGlobalFunction("bool Synth_IsAttached()",
        asFUNCTION(TourSynth_IsAttached), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Synth_SetMuted(bool)",
        asFUNCTION(TourSynth_SetMuted), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool Synth_IsMuted()",
        asFUNCTION(TourSynth_IsMuted), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Synth_ScopeCount()",
        asFUNCTION(TourSynth_ScopeCount), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Synth_ScopeAt(int)",
        asFUNCTION(TourSynth_ScopeAt), asCALL_CDECL); AS_CHECK(r);

    // --- Constants ---
    {
        static gX_I32 cWaveSine = GX_SYNTH_WAVE_SINE;
        static gX_I32 cWaveSquare = GX_SYNTH_WAVE_SQUARE;
        static gX_I32 cWaveSawtooth = GX_SYNTH_WAVE_SAWTOOTH;
        static gX_I32 cWaveTriangle = GX_SYNTH_WAVE_TRIANGLE;
        static gX_I32 cWaveNoise = GX_SYNTH_WAVE_NOISE;
        static gX_I32 cWaveWavetable = GX_SYNTH_WAVE_WAVETABLE;
        r = engine->RegisterGlobalProperty("const int SYNTH_WAVE_SINE", (void *)&cWaveSine); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_WAVE_SQUARE", (void *)&cWaveSquare); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_WAVE_SAWTOOTH", (void *)&cWaveSawtooth); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_WAVE_TRIANGLE", (void *)&cWaveTriangle); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_WAVE_NOISE", (void *)&cWaveNoise); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_WAVE_WAVETABLE", (void *)&cWaveWavetable); AS_CHECK(r);

        static gX_I32 cFilterNone = GX_SYNTH_FILTER_NONE;
        static gX_I32 cFilterLowpass = GX_SYNTH_FILTER_LOWPASS;
        static gX_I32 cFilterHighpass = GX_SYNTH_FILTER_HIGHPASS;
        static gX_I32 cFilterBandpass = GX_SYNTH_FILTER_BANDPASS;
        r = engine->RegisterGlobalProperty("const int SYNTH_FILTER_NONE", (void *)&cFilterNone); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_FILTER_LOWPASS", (void *)&cFilterLowpass); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_FILTER_HIGHPASS", (void *)&cFilterHighpass); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_FILTER_BANDPASS", (void *)&cFilterBandpass); AS_CHECK(r);

        static gX_I32 cLfoPitch = GX_SYNTH_LFO_PITCH;
        static gX_I32 cLfoAmp = GX_SYNTH_LFO_AMP;
        static gX_I32 cLfoFilter = GX_SYNTH_LFO_FILTER;
        r = engine->RegisterGlobalProperty("const int SYNTH_LFO_PITCH", (void *)&cLfoPitch); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_LFO_AMP", (void *)&cLfoAmp); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_LFO_FILTER", (void *)&cLfoFilter); AS_CHECK(r);

        static gX_I32 cFmOff = GX_SYNTH_FM_OFF;
        static gX_I32 cFmSimple = GX_SYNTH_FM_SIMPLE;
        static gX_I32 cFmSerial = GX_SYNTH_FM_SERIAL;
        static gX_I32 cFmParallel = GX_SYNTH_FM_PARALLEL;
        static gX_I32 cFmFeedback = GX_SYNTH_FM_FEEDBACK;
        r = engine->RegisterGlobalProperty("const int SYNTH_FM_OFF", (void *)&cFmOff); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_FM_SIMPLE", (void *)&cFmSimple); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_FM_SERIAL", (void *)&cFmSerial); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_FM_PARALLEL", (void *)&cFmParallel); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_FM_FEEDBACK", (void *)&cFmFeedback); AS_CHECK(r);

        static gX_I32 cArpOff = GX_SYNTH_ARP_OFF;
        static gX_I32 cArpUp = GX_SYNTH_ARP_UP;
        static gX_I32 cArpDown = GX_SYNTH_ARP_DOWN;
        static gX_I32 cArpUpdown = GX_SYNTH_ARP_UPDOWN;
        static gX_I32 cArpRandom = GX_SYNTH_ARP_RANDOM;
        r = engine->RegisterGlobalProperty("const int SYNTH_ARP_OFF", (void *)&cArpOff); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_ARP_UP", (void *)&cArpUp); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_ARP_DOWN", (void *)&cArpDown); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_ARP_UPDOWN", (void *)&cArpUpdown); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int SYNTH_ARP_RANDOM", (void *)&cArpRandom); AS_CHECK(r);
    }

    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "registered tour synth API (notes, patches, arp, sequencer)");
    return 0;
}
