/**********************************************************************************************
*
*   [gX: src/scripting/tour/tour_pad.cpp]
*   gX — a game engine
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


//ai: tour PAD API — script surface over the layered ensemble engine
//   (src/audio/pad.c). A patch is three layers + a long envelope + the
//   filter/LFO + the chorus and width; SetDefault* shapes what new notes
//   copy, the voice-scoped setters edit a sounding note, and presets
//   round-trip the default patch.
//opencode: raylib headers FIRST with C++ linkage — wrapper headers reached
//   through other includes would give raymath's operator overloads C linkage
//   (see src/tools/gfx/gfx_editor.cpp).
#include "raylib.h"
#include "raymath.h"

#include "tour.h"

#include <angelscript.h>

#include <string>

#include "../../common/log.h"
#include "../../audio/pad.h"
#include "../../audio/pad_schema.h"

// ===========================================================================
// Notes
// ===========================================================================

static gX_I32 TourPad_NoteOn(gX_F32 freq, gX_F32 velocity) {
    return gX_Pad_NoteOn(freq, velocity);
}

static gX_I0 TourPad_NoteOff(gX_I32 voice) {
    if (voice >= 0) gX_Pad_NoteOff((gX_U32)voice);
}

static gX_I0 TourPad_AllNotesOff(gX_I0) {
    gX_Pad_AllNotesOff();
}

static gX_I1 TourPad_VoiceActive(gX_I32 voice) {
    if (voice < 0 || voice >= GX_PAD_MAX_VOICES) return 0;
    return g_pad.voices[voice].active;
}

static gX_I1 TourPad_IsAttached(gX_I0) {
    return gX_Pad_IsAttached();
}

// ===========================================================================
// Layers
// ===========================================================================

static gX_I0 TourPad_SetLayer(gX_I32 voice, gX_I32 layer, gX_I32 waveform,
    gX_F32 detuneCents, gX_F32 level, gX_F32 pan, gX_F32 driftRate, gX_F32 driftDepth) {
    if (voice < 0) return;
    gX_Pad_Layer params = {0};
    params.waveform = waveform;
    params.detuneCents = detuneCents;
    params.level = level;
    params.pan = pan;
    params.driftRate = driftRate;
    params.driftDepth = driftDepth;
    gX_Pad_SetVoiceLayer((gX_U32)voice, layer, &params);
}

static gX_I0 TourPad_SetDefaultLayer(gX_I32 layer, gX_I32 waveform,
    gX_F32 detuneCents, gX_F32 level, gX_F32 pan, gX_F32 driftRate, gX_F32 driftDepth) {
    gX_Pad_Layer params = {0};
    params.waveform = waveform;
    params.detuneCents = detuneCents;
    params.level = level;
    params.pan = pan;
    params.driftRate = driftRate;
    params.driftDepth = driftDepth;
    gX_Pad_SetDefaultLayer(layer, &params);
}

// ===========================================================================
// Envelope / filter / chorus
// ===========================================================================

static gX_I0 TourPad_SetADSR(gX_I32 voice, gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r) {
    if (voice >= 0) gX_Pad_SetVoiceADSR((gX_U32)voice, a, d, s, r);
}

static gX_I0 TourPad_SetDefaultADSR(gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r) {
    gX_Pad_SetDefaultADSR(a, d, s, r);
}

static gX_I0 TourPad_SetFilter(gX_I32 voice, gX_I32 mode, gX_F32 cutoff, gX_F32 res) {
    if (voice >= 0) gX_Pad_SetVoiceFilter((gX_U32)voice, mode, cutoff, res);
}

static gX_I0 TourPad_SetDefaultFilter(gX_I32 mode, gX_F32 cutoff, gX_F32 res) {
    gX_Pad_SetDefaultFilter(mode, cutoff, res);
}

static gX_I0 TourPad_SetFilterLFO(gX_I32 voice, gX_F32 rate, gX_F32 depth) {
    if (voice >= 0) gX_Pad_SetVoiceFilterLFO((gX_U32)voice, rate, depth);
}

static gX_I0 TourPad_SetDefaultFilterLFO(gX_F32 rate, gX_F32 depth) {
    gX_Pad_SetDefaultFilterLFO(rate, depth);
}

static gX_I0 TourPad_SetVoicePan(gX_I32 voice, gX_F32 pan) {
    if (voice >= 0) gX_Pad_SetVoicePan((gX_U32)voice, pan);
}

static gX_I0 TourPad_SetChorus(gX_I1 enabled, gX_F32 rate, gX_F32 depth,
    gX_F32 mix, gX_F32 stereoPhase) {
    gX_Pad_SetChorus(enabled ? 1 : 0, rate, depth, mix, stereoPhase);
}

static gX_I0 TourPad_SetWidth(gX_F32 width) {
    gX_Pad_SetWidth(width);
}

static gX_I0 TourPad_SetMasterVolume(gX_F32 volume) {
    gX_Pad_SetMasterVolume(volume);
}

static gX_I1 TourPad_IsMuted(gX_I0) {
    return gX_Pad_IsMuted();
}

static gX_I0 TourPad_SetMuted(gX_I1 muted) {
    gX_Pad_SetMuted(muted ? 1 : 0);
}

// ===========================================================================
// Readbacks (UI sync / probes)
// ===========================================================================

static gX_I32 TourPad_GetLayerWaveform(gX_I32 layer) {
    if (layer < 0 || layer >= GX_PAD_LAYERS) return 0;
    return g_pad.defaultLayers[layer].waveform;
}

static gX_F32 TourPad_GetLayerLevel(gX_I32 layer) {
    if (layer < 0 || layer >= GX_PAD_LAYERS) return 0.0f;
    return g_pad.defaultLayers[layer].level;
}

static gX_F32 TourPad_GetLayerPan(gX_I32 layer) {
    if (layer < 0 || layer >= GX_PAD_LAYERS) return 0.0f;
    return g_pad.defaultLayers[layer].pan;
}

static gX_I1 TourPad_GetChorusEnabled(gX_I0) {
    return g_pad.chorusEnabled;
}

static gX_F32 TourPad_GetWidth(gX_I0) {
    return g_pad.width;
}

static gX_F32 TourPad_GetMasterVolume(gX_I0) {
    return g_pad.masterVolume;
}

// ===========================================================================
// Presets
// ===========================================================================

static gX_I32 TourPad_PresetSave(const std::string &path) {
    return gX_Pad_Preset_Save((const gX_I8 *)path.c_str());
}

static gX_I32 TourPad_PresetLoad(const std::string &path) {
    return gX_Pad_Preset_Load((const gX_I8 *)path.c_str());
}

// ===========================================================================
// Registration
// ===========================================================================

#define AS_CHECK(r) \
    do { if ((r) < 0) { \
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "AS tour-pad registration failed (code %d)", r); \
    } } while(0)

gX_I32 gX_TourPad_Register(asIScriptEngine *engine) {
    int r = 0;

    // --- Notes ---
    r = engine->RegisterGlobalFunction("int Pad_NoteOn(float, float)",
        asFUNCTION(TourPad_NoteOn), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Pad_NoteOff(int)",
        asFUNCTION(TourPad_NoteOff), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Pad_AllNotesOff()",
        asFUNCTION(TourPad_AllNotesOff), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool Pad_VoiceActive(int)",
        asFUNCTION(TourPad_VoiceActive), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool Pad_IsAttached()",
        asFUNCTION(TourPad_IsAttached), asCALL_CDECL); AS_CHECK(r);

    // --- Layers (voice + defaults) ---
    r = engine->RegisterGlobalFunction("void Pad_SetLayer(int, int, int, float, float, float, float, float)",
        asFUNCTION(TourPad_SetLayer), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Pad_SetDefaultLayer(int, int, float, float, float, float, float)",
        asFUNCTION(TourPad_SetDefaultLayer), asCALL_CDECL); AS_CHECK(r);

    // --- Envelope / filter / chorus ---
    r = engine->RegisterGlobalFunction("void Pad_SetADSR(int, float, float, float, float)",
        asFUNCTION(TourPad_SetADSR), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Pad_SetDefaultADSR(float, float, float, float)",
        asFUNCTION(TourPad_SetDefaultADSR), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Pad_SetFilter(int, int, float, float)",
        asFUNCTION(TourPad_SetFilter), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Pad_SetDefaultFilter(int, float, float)",
        asFUNCTION(TourPad_SetDefaultFilter), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Pad_SetFilterLFO(int, float, float)",
        asFUNCTION(TourPad_SetFilterLFO), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Pad_SetDefaultFilterLFO(float, float)",
        asFUNCTION(TourPad_SetDefaultFilterLFO), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Pad_SetVoicePan(int, float)",
        asFUNCTION(TourPad_SetVoicePan), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Pad_SetChorus(bool, float, float, float, float)",
        asFUNCTION(TourPad_SetChorus), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Pad_SetWidth(float)",
        asFUNCTION(TourPad_SetWidth), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Pad_SetMasterVolume(float)",
        asFUNCTION(TourPad_SetMasterVolume), asCALL_CDECL); AS_CHECK(r);

    // --- State + presets ---
    r = engine->RegisterGlobalFunction("bool Pad_IsMuted()",
        asFUNCTION(TourPad_IsMuted), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Pad_SetMuted(bool)",
        asFUNCTION(TourPad_SetMuted), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Pad_GetLayerWaveform(int)",
        asFUNCTION(TourPad_GetLayerWaveform), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Pad_GetLayerLevel(int)",
        asFUNCTION(TourPad_GetLayerLevel), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Pad_GetLayerPan(int)",
        asFUNCTION(TourPad_GetLayerPan), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool Pad_GetChorusEnabled()",
        asFUNCTION(TourPad_GetChorusEnabled), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Pad_GetWidth()",
        asFUNCTION(TourPad_GetWidth), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Pad_GetMasterVolume()",
        asFUNCTION(TourPad_GetMasterVolume), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Pad_Preset_Save(const string &in)",
        asFUNCTION(TourPad_PresetSave), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Pad_Preset_Load(const string &in)",
        asFUNCTION(TourPad_PresetLoad), asCALL_CDECL); AS_CHECK(r);

    // --- Constants ---
    {
        static gX_I32 cLayers = GX_PAD_LAYERS;
        static gX_I32 cMaxVoices = GX_PAD_MAX_VOICES;
        static gX_I32 cWaveSaw = GX_PAD_WAVE_SAW;
        static gX_I32 cWaveTri = GX_PAD_WAVE_TRI;
        static gX_I32 cFilterLP = GX_PAD_FILTER_LP;
        static gX_I32 cFilterHP = GX_PAD_FILTER_HP;
        static gX_I32 cFilterBP = GX_PAD_FILTER_BP;
        r = engine->RegisterGlobalProperty("const int PAD_LAYERS", (void *)&cLayers); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int PAD_MAX_VOICES", (void *)&cMaxVoices); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int PAD_WAVE_SAW", (void *)&cWaveSaw); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int PAD_WAVE_TRI", (void *)&cWaveTri); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int PAD_FILTER_LP", (void *)&cFilterLP); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int PAD_FILTER_HP", (void *)&cFilterHP); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int PAD_FILTER_BP", (void *)&cFilterBP); AS_CHECK(r);
    }

    gX_TraceLog_SCRIPTING(gX_LOG_INFO, "AS tour-pad API registered");
    return 0;
}
