/**********************************************************************************************
*
*   [gX: src/scripting/tour/tour_opx.cpp]
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


//ai: tour OPX API — script surface over the 8-operator FM engine
//   (src/audio/opx.c). Notes are frequencies and an optional algorithm;
//   NoteOn returns the voice slot for sounding-note operator edits, while
//   the SetDefault* twins shape the patch that new notes copy. Presets cover
//   the engine's default patch (operators + algorithm + master volume).
//opencode: raylib headers FIRST with C++ linkage — wrapper headers reached
//   through other includes would give raymath's operator overloads C linkage
//   (see src/tools/gfx/gfx_editor.cpp).
#include "raylib.h"
#include "raymath.h"

#include "tour.h"

#include <angelscript.h>

#include <string>

#include "../../common/log.h"
#include "../../audio/opx.h"
#include "../../audio/opx_schema.h"

// ===========================================================================
// Notes
// ===========================================================================

static gX_I32 TourOPX_NoteOn(gX_F32 freq, gX_F32 velocity, gX_I32 algorithm) {
    return gX_OPX_NoteOn(freq, velocity, algorithm);
}

static gX_I0 TourOPX_NoteOff(gX_I32 voice) {
    if (voice >= 0) gX_OPX_NoteOff((gX_U32)voice);
}

static gX_I0 TourOPX_AllNotesOff(gX_I0) {
    gX_OPX_AllNotesOff();
}

static gX_I1 TourOPX_VoiceActive(gX_I32 voice) {
    if (voice < 0 || voice >= GX_OPX_MAX_VOICES) return 0;
    return g_opx.voices[voice].active;
}

static gX_I1 TourOPX_IsAttached(gX_I0) {
    return gX_OPX_IsAttached();
}

// ===========================================================================
// Algorithm + per-voice operator parameters
// ===========================================================================

static gX_I32 TourOPX_AlgorithmCount(gX_I0) {
    return gX_OPX_AlgorithmCount();
}

static const std::string TourOPX_AlgorithmName(gX_I32 algorithm) {
    return std::string((const char *)gX_OPX_AlgorithmName(algorithm));
}

static gX_I0 TourOPX_SetOperator(gX_I32 voice, gX_I32 op, gX_F32 ratio, gX_F32 level) {
    if (voice >= 0) gX_OPX_SetOperator((gX_U32)voice, op, ratio, level);
}

static gX_I0 TourOPX_SetOperatorEnabled(gX_I32 voice, gX_I32 op, gX_I1 enabled) {
    if (voice >= 0) gX_OPX_SetOperatorEnabled((gX_U32)voice, op, enabled ? 1 : 0);
}

static gX_I0 TourOPX_SetOperatorADSR(gX_I32 voice, gX_I32 op,
    gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r) {
    if (voice >= 0) gX_OPX_SetOperatorADSR((gX_U32)voice, op, a, d, s, r);
}

static gX_I0 TourOPX_SetOperatorDetune(gX_I32 voice, gX_I32 op, gX_F32 cents) {
    if (voice >= 0) gX_OPX_SetOperatorDetune((gX_U32)voice, op, cents);
}

static gX_I0 TourOPX_SetOperatorFeedback(gX_I32 voice, gX_I32 op, gX_F32 amount) {
    if (voice >= 0) gX_OPX_SetOperatorFeedback((gX_U32)voice, op, amount);
}

static gX_I0 TourOPX_SetPan(gX_I32 voice, gX_F32 pan) {
    if (voice >= 0) gX_OPX_SetPan((gX_U32)voice, pan);
}

// ===========================================================================
// Defaults (the patch new notes copy)
// ===========================================================================

static gX_I0 TourOPX_SetDefaultAlgorithm(gX_I32 algorithm) {
    gX_OPX_SetDefaultAlgorithm(algorithm);
}

static gX_I0 TourOPX_SetDefaultOperator(gX_I32 op, gX_F32 ratio, gX_F32 level) {
    gX_OPX_SetDefaultOperator(op, ratio, level);
}

static gX_I0 TourOPX_SetDefaultOperatorEnabled(gX_I32 op, gX_I1 enabled) {
    gX_OPX_SetDefaultOperatorEnabled(op, enabled ? 1 : 0);
}

static gX_I0 TourOPX_SetDefaultOperatorADSR(gX_I32 op,
    gX_F32 a, gX_F32 d, gX_F32 s, gX_F32 r) {
    gX_OPX_SetDefaultOperatorADSR(op, a, d, s, r);
}

static gX_I0 TourOPX_SetDefaultOperatorDetune(gX_I32 op, gX_F32 cents) {
    gX_OPX_SetDefaultOperatorDetune(op, cents);
}

static gX_I0 TourOPX_SetDefaultOperatorFeedback(gX_I32 op, gX_F32 amount) {
    gX_OPX_SetDefaultOperatorFeedback(op, amount);
}

static gX_I0 TourOPX_SetMasterVolume(gX_F32 volume) {
    gX_OPX_SetMasterVolume(volume);
}

static gX_I1 TourOPX_IsMuted(gX_I0) {
    return gX_OPX_IsMuted();
}

static gX_I0 TourOPX_SetMuted(gX_I1 muted) {
    gX_OPX_SetMuted(muted ? 1 : 0);
}

// ===========================================================================
// Readbacks (UI sync / probes)
// ===========================================================================

static gX_I32 TourOPX_GetDefaultAlgorithm(gX_I0) {
    return gX_OPX_GetDefaultAlgorithm();
}

static gX_F32 TourOPX_GetOperatorRatio(gX_I32 op) {
    if (op < 0 || op >= GX_OPX_OPERATORS) return 0.0f;
    return g_opx.defaults[op].ratio;
}

static gX_F32 TourOPX_GetOperatorLevel(gX_I32 op) {
    if (op < 0 || op >= GX_OPX_OPERATORS) return 0.0f;
    return g_opx.defaults[op].level;
}

static gX_I1 TourOPX_GetOperatorEnabled(gX_I32 op) {
    if (op < 0 || op >= GX_OPX_OPERATORS) return 0;
    return g_opx.defaults[op].enabled;
}

static gX_F32 TourOPX_GetMasterVolume(gX_I0) {
    return g_opx.masterVolume;
}

// ===========================================================================
// Presets
// ===========================================================================

static gX_I32 TourOPX_PresetSave(const std::string &path) {
    return gX_OPX_Preset_Save((const gX_I8 *)path.c_str());
}

static gX_I32 TourOPX_PresetLoad(const std::string &path) {
    return gX_OPX_Preset_Load((const gX_I8 *)path.c_str());
}

// ===========================================================================
// Registration
// ===========================================================================

#define AS_CHECK(r) \
    do { if ((r) < 0) { \
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "AS tour-opx registration failed (code %d)", r); \
    } } while(0)

gX_I32 gX_TourOPX_Register(asIScriptEngine *engine) {
    int r = 0;

    // --- Notes ---
    r = engine->RegisterGlobalFunction("int Opx_NoteOn(float, float, int)",
        asFUNCTION(TourOPX_NoteOn), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Opx_NoteOff(int)",
        asFUNCTION(TourOPX_NoteOff), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Opx_AllNotesOff()",
        asFUNCTION(TourOPX_AllNotesOff), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool Opx_VoiceActive(int)",
        asFUNCTION(TourOPX_VoiceActive), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool Opx_IsAttached()",
        asFUNCTION(TourOPX_IsAttached), asCALL_CDECL); AS_CHECK(r);

    // --- Algorithms ---
    r = engine->RegisterGlobalFunction("int Opx_AlgorithmCount()",
        asFUNCTION(TourOPX_AlgorithmCount), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("string Opx_AlgorithmName(int)",
        asFUNCTION(TourOPX_AlgorithmName), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Opx_SetDefaultAlgorithm(int)",
        asFUNCTION(TourOPX_SetDefaultAlgorithm), asCALL_CDECL); AS_CHECK(r);

    // --- Per-voice operators ---
    r = engine->RegisterGlobalFunction("void Opx_SetOperator(int, int, float, float)",
        asFUNCTION(TourOPX_SetOperator), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Opx_SetOperatorEnabled(int, int, bool)",
        asFUNCTION(TourOPX_SetOperatorEnabled), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Opx_SetOperatorADSR(int, int, float, float, float, float)",
        asFUNCTION(TourOPX_SetOperatorADSR), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Opx_SetOperatorDetune(int, int, float)",
        asFUNCTION(TourOPX_SetOperatorDetune), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Opx_SetOperatorFeedback(int, int, float)",
        asFUNCTION(TourOPX_SetOperatorFeedback), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Opx_SetPan(int, float)",
        asFUNCTION(TourOPX_SetPan), asCALL_CDECL); AS_CHECK(r);

    // --- Defaults (new notes) ---
    r = engine->RegisterGlobalFunction("void Opx_SetDefaultOperator(int, float, float)",
        asFUNCTION(TourOPX_SetDefaultOperator), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Opx_SetDefaultOperatorEnabled(int, bool)",
        asFUNCTION(TourOPX_SetDefaultOperatorEnabled), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Opx_SetDefaultOperatorADSR(int, float, float, float, float)",
        asFUNCTION(TourOPX_SetDefaultOperatorADSR), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Opx_SetDefaultOperatorDetune(int, float)",
        asFUNCTION(TourOPX_SetDefaultOperatorDetune), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Opx_SetDefaultOperatorFeedback(int, float)",
        asFUNCTION(TourOPX_SetDefaultOperatorFeedback), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Opx_SetMasterVolume(float)",
        asFUNCTION(TourOPX_SetMasterVolume), asCALL_CDECL); AS_CHECK(r);

    // --- State + presets ---
    r = engine->RegisterGlobalFunction("bool Opx_IsMuted()",
        asFUNCTION(TourOPX_IsMuted), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Opx_SetMuted(bool)",
        asFUNCTION(TourOPX_SetMuted), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Opx_GetDefaultAlgorithm()",
        asFUNCTION(TourOPX_GetDefaultAlgorithm), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Opx_GetOperatorRatio(int)",
        asFUNCTION(TourOPX_GetOperatorRatio), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Opx_GetOperatorLevel(int)",
        asFUNCTION(TourOPX_GetOperatorLevel), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool Opx_GetOperatorEnabled(int)",
        asFUNCTION(TourOPX_GetOperatorEnabled), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Opx_GetMasterVolume()",
        asFUNCTION(TourOPX_GetMasterVolume), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Opx_Preset_Save(const string &in)",
        asFUNCTION(TourOPX_PresetSave), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Opx_Preset_Load(const string &in)",
        asFUNCTION(TourOPX_PresetLoad), asCALL_CDECL); AS_CHECK(r);

    // --- Constants ---
    {
        static gX_I32 cOperators = GX_OPX_OPERATORS;
        static gX_I32 cMaxVoices = GX_OPX_MAX_VOICES;
        static gX_I32 cAllCarrier = 0;
        static gX_I32 cPairs = 1;
        static gX_I32 cStacks = 2;
        static gX_I32 cCascade = 3;
        static gX_I32 cEP = 4;
        static gX_I32 cBass = 5;
        static gX_I32 cBell = 6;
        static gX_I32 cComplex = 7;
        r = engine->RegisterGlobalProperty("const int OPX_OPERATORS", (void *)&cOperators); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int OPX_MAX_VOICES", (void *)&cMaxVoices); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int OPX_ALG_ALL_CARRIER", (void *)&cAllCarrier); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int OPX_ALG_PAIRS", (void *)&cPairs); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int OPX_ALG_STACKS", (void *)&cStacks); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int OPX_ALG_CASCADE", (void *)&cCascade); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int OPX_ALG_EP", (void *)&cEP); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int OPX_ALG_BASS", (void *)&cBass); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int OPX_ALG_BELL", (void *)&cBell); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int OPX_ALG_COMPLEX", (void *)&cComplex); AS_CHECK(r);
    }

    gX_TraceLog_SCRIPTING(gX_LOG_INFO, "AS tour-opx API registered");
    return 0;
}
