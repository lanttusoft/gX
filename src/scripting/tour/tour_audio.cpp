/**********************************************************************************************
*
*   [gX: src/scripting/tour/tour_audio.cpp]
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


//ai: tour audio API — the reel-script audio surface: music transport, the
//   procedural beep (gX_Audio_Gen_Play) and the master filter (the shared
//   TPT core on the final mix; off by default). Sound_PlayFile is not bound
//   yet (wrappers' sound pool lives behind the raylib-typed header the shell
//   avoids); tours use Music_* + Sound_Beep + the master filter.
#include "tour.h"

#include <angelscript.h>

#include <string>

#include "../../common/log.h"
#include "../../audio.h"
#include "../../audio/filter.h"

static void TourAudio_MusicLoad(const std::string &path) {
    if (gX_Audio_Music_Load(path.c_str()) != 0) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "Music_Load: failed for '%s'", path.c_str());
    }
}

static void TourAudio_MusicPlay(void) { gX_Audio_Music_Play(); }
static void TourAudio_MusicStop(void) { gX_Audio_Music_Stop(); }

static void TourAudio_MusicSetVolume(gX_F32 volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    gX_Audio_Music_SetVolume(volume);
}

static gX_F32 TourAudio_MusicGetPosition(void) { return audio.music.params.played; }
static gX_F32 TourAudio_MusicGetLength(void) { return audio.music.params.length; }
static gX_I1 TourAudio_MusicIsPlaying(void) { return audio.music.params.playing; }

static gX_I32 TourAudio_SoundBeep(gX_F32 frequency, gX_I32 durationMs, gX_F32 volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    return gX_Audio_Gen_Play(frequency, durationMs, 0, volume);
}

//ai: master filter — one call sets the whole state (the engine detaches when
//   disabled and keeps itself at the mixed-chain tail otherwise).
static void TourAudio_SetMasterFilter(gX_I1 enabled, gX_I32 mode,
    gX_F32 cutoff, gX_F32 resonance) {
    gX_Audio_MasterFilter_Set(enabled ? 1 : 0, mode, cutoff, resonance);
}

static gX_I1 TourAudio_GetMasterFilterEnabled(void) {
    return gX_Audio_MasterFilter_IsEnabled();
}

static gX_I32 TourAudio_GetMasterFilterMode(void) {
    return gX_Audio_MasterFilter_GetMode();
}

static gX_F32 TourAudio_GetMasterFilterCutoff(void) {
    return gX_Audio_MasterFilter_GetCutoff();
}

static gX_F32 TourAudio_GetMasterFilterResonance(void) {
    return gX_Audio_MasterFilter_GetResonance();
}

#define AS_CHECK(r) do { if (r < 0) \
    gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "AS tour-audio registration failed (code %d)", r); \
} while(0)

gX_I32 gX_TourAudio_Register(asIScriptEngine *engine) {
    int r = 0;

    r = engine->RegisterGlobalFunction("void Music_Load(const string &in)",
        asFUNCTION(TourAudio_MusicLoad), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Music_Play()",
        asFUNCTION(TourAudio_MusicPlay), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Music_Stop()",
        asFUNCTION(TourAudio_MusicStop), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Music_SetVolume(float)",
        asFUNCTION(TourAudio_MusicSetVolume), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Music_GetPosition()",
        asFUNCTION(TourAudio_MusicGetPosition), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Music_GetLength()",
        asFUNCTION(TourAudio_MusicGetLength), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool Music_IsPlaying()",
        asFUNCTION(TourAudio_MusicIsPlaying), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Sound_Beep(float, int, float)",
        asFUNCTION(TourAudio_SoundBeep), asCALL_CDECL); AS_CHECK(r);

    // --- Master filter ---
    r = engine->RegisterGlobalFunction("void Audio_SetMasterFilter(bool, int, float, float)",
        asFUNCTION(TourAudio_SetMasterFilter), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool Audio_GetMasterFilterEnabled()",
        asFUNCTION(TourAudio_GetMasterFilterEnabled), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Audio_GetMasterFilterMode()",
        asFUNCTION(TourAudio_GetMasterFilterMode), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Audio_GetMasterFilterCutoff()",
        asFUNCTION(TourAudio_GetMasterFilterCutoff), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Audio_GetMasterFilterResonance()",
        asFUNCTION(TourAudio_GetMasterFilterResonance), asCALL_CDECL); AS_CHECK(r);
    {
        static gX_I32 cFilterLP = GX_AUDIO_FILTER_LP;
        static gX_I32 cFilterHP = GX_AUDIO_FILTER_HP;
        static gX_I32 cFilterBP = GX_AUDIO_FILTER_BP;
        r = engine->RegisterGlobalProperty("const int AUDIO_FILTER_LP", (void *)&cFilterLP); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int AUDIO_FILTER_HP", (void *)&cFilterHP); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int AUDIO_FILTER_BP", (void *)&cFilterBP); AS_CHECK(r);
    }

    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG,
        "registered tour audio API (music transport, beep, master filter)");
    return 0;
}
