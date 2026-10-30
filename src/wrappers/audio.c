/**********************************************************************************************
* 
*   [wrappers/audio.c]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025-2026 lanttu, opencode
* 
*   [raylib]
*   LICENSE: zlib/libpng
*
*   raylib is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software:
*
*   Copyright (c) 2013-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/
//lanttu: original wrapper for raylib audio (Wave/Sound/Music) functions
#include "wrappers_header.h"
#include "audio.h"

//opencode: N64 audio stub — mixer integration TBD

gX_Wave gX_Wave_Load(const gX_I8 *path) {
    Wave wave = LoadWave(path);
    return gX_Wave_ConvertFromBackend(wave);
}

gX_I0 gX_Wave_Unload(gX_Wave wave) {
    Wave wav = gX_Wave_ConvertToBackend(wave);
    UnloadWave(wav);
}

gX_Sound gX_Sound_Load(const gX_I8 *path) {
    Sound sound = LoadSound(path);
    return gX_Sound_ConvertFromBackend(sound);
}

gX_Sound gX_Sound_LoadWave(gX_Wave wave) {
    Wave wav = gX_Wave_ConvertToBackend(wave);
    Sound sound = LoadSoundFromWave(wav);
    return gX_Sound_ConvertFromBackend(sound);
}

gX_I0 gX_Sound_Unload(gX_Sound sound) {
    //opencode: skip if never properly loaded (no frames decoded)
    if (sound.frameCount == 0) return;
    Sound snd = gX_Sound_ConvertToBackend(sound);
    UnloadSound(snd);
}

gX_I0 gX_Sound_Update(gX_Sound sound, gX_I0 *data, gX_I32 sampleCount) {
    Sound snd = gX_Sound_ConvertToBackend(sound);
    UpdateSound(snd,data,sampleCount);
}

gX_I0 gX_Sound_Play(gX_Sound sound) {
    Sound snd = gX_Sound_ConvertToBackend(sound);
    PlaySound(snd);
}

gX_I0 gX_Sound_Play_Parameters(gX_Sound sound, gX_Sound_Parameters params) {
    Sound snd = gX_Sound_ConvertToBackend(sound);
    SetSoundVolume(snd,params.volume);
    SetSoundPitch(snd,params.pitch);
    SetSoundPan(snd,params.pan);
    PlaySound(snd);
}

gX_I0 gX_Sound_Play_Quiet(gX_Sound sound) {
    gX_Sound_Parameters params = { 0 };
    params.volume = 0.25f;
    params.pan = 0.0f;  //opencode: center (-1=left, 0=center, 1=right)
    params.pitch = 1.0f;
    gX_Sound_Play_Parameters(sound, params);
}

gX_I0 gX_Sound_Stop(gX_Sound sound) {
    Sound snd = gX_Sound_ConvertToBackend(sound);
    StopSound(snd);
}

gX_I1 gX_Sound_IsPlaying(gX_Sound sound) {
    Sound snd = gX_Sound_ConvertToBackend(sound);
    return IsSoundPlaying(snd) ? 1 : 0;
}

gX_I0 gX_Sound_SetPan(gX_Sound sound, gX_F32 pan) {
    Sound snd = gX_Sound_ConvertToBackend(sound);
    SetSoundPan(snd, pan);
}

gX_I0 gX_Sound_SetVolume(gX_Sound sound, gX_F32 volume) {
    Sound snd = gX_Sound_ConvertToBackend(sound);
    SetSoundVolume(snd, volume);
}

gX_Music gX_Music_Load(const gX_I8 *path) {
    Music music = LoadMusicStream(path);
    return gX_Music_ConvertFromBackend(music);
}

gX_I0 gX_Music_Unload(gX_Music music) {
    //opencode: skip if never loaded
    if (!music.ctxData && music.stream.buffer == 0) return;
    Music mus = gX_Music_ConvertToBackend(music);
    UnloadMusicStream(mus);
}

gX_I0 gX_Music_Update_Parameters(gX_Music music, gX_Sound_Parameters *params) {
    Music mus = gX_Music_ConvertToBackend(music);
    SetMusicVolume(mus,params->volume);
    SetMusicPitch(mus,params->pitch);
    UpdateMusicStream(mus);
    params->playing = IsMusicStreamPlaying(mus);
    params->length = GetMusicTimeLength(mus);
    params->played = GetMusicTimePlayed(mus);
}

gX_I0 gX_Music_Play(gX_Music music) {
    Music mus = gX_Music_ConvertToBackend(music);
    PlayMusicStream(mus);
}

gX_I0 gX_Music_Pause(gX_Music music) {
    Music mus = gX_Music_ConvertToBackend(music);
    PauseMusicStream(mus);
}

gX_I0 gX_Music_Stop(gX_Music music) {
    Music mus = gX_Music_ConvertToBackend(music);
    StopMusicStream(mus);
}

gX_I0 gX_Music_Resume(gX_Music music) {
    Music mus = gX_Music_ConvertToBackend(music);
    ResumeMusicStream(mus);
}

gX_I0 gX_Music_Seek(gX_Music music, gX_F32 position) {
    Music mus = gX_Music_ConvertToBackend(music);
    SeekMusicStream(mus,position);
}

gX_I0 gX_Music_SetVolume(gX_Music music, gX_F32 volume) {
    Music mus = gX_Music_ConvertToBackend(music);
    SetMusicVolume(mus,volume);
}

gX_I0 gX_Music_SetPitch(gX_Music music, gX_F32 pitch) {
    Music mus = gX_Music_ConvertToBackend(music);
    SetMusicPitch(mus,pitch);
}

gX_I0 gX_Music_SetPan(gX_Music music, gX_F32 pan) {
    Music mus = gX_Music_ConvertToBackend(music);
    SetMusicPan(mus,pan);
}

gX_F32 gX_Music_GetLength(gX_Music music) {
    Music mus = gX_Music_ConvertToBackend(music);
    return GetMusicTimeLength(mus);
}

gX_F32 gX_Music_GetTimePlayed(gX_Music music) {
    Music mus = gX_Music_ConvertToBackend(music);
    return GetMusicTimePlayed(mus);
}
