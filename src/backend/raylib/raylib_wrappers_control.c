/**********************************************************************************************
*
*   [gX: src/backend/raylib/raylib_wrappers_control.c]
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

//opencode: D14 — backend control surface. All raylib control/update API used
//   by the engine (window control, audio playback, texture updates, glyph
//   lookup, file save, frame begin/end) lives here so engine code never calls
//   raylib directly. RAYLIB branch keeps raylib calls; GLFW branch delegates
//   to the gX_glfw_* natives so the GLFW build needs no raylib.
#include "raylib_wrappers_model.h"
#include "raylib_wrappers.h"
#include "../../wrappers/convert.h"
#include "../../common/log.h"
#include "../../rhi/rhi_tex.h"
#include "../../rhi/rhi.h"

//opencode: D14 — GLFW branch delegates to gX_glfw_* natives


// ---------------------------------------------------------------- window control

gX_I1 raylib_Window_ShouldClose(gX_I0) {
    return (gX_I1)WindowShouldClose();
}
gX_I1 raylib_Window_IsReady(gX_I0) {
    return (gX_I1)IsWindowReady();
}
gX_I1 raylib_Window_IsFullscreen(gX_I0) {
    return (gX_I1)IsWindowFullscreen();
}
gX_I1 raylib_Window_IsHidden(gX_I0) {
    return (gX_I1)IsWindowHidden();
}
gX_I1 raylib_Window_IsMinimized(gX_I0) {
    return (gX_I1)IsWindowMinimized();
}
gX_I1 raylib_Window_IsMaximized(gX_I0) {
    return (gX_I1)IsWindowMaximized();
}
gX_I1 raylib_Window_IsFocused(gX_I0) {
    return (gX_I1)IsWindowFocused();
}
gX_I1 raylib_Window_IsState(gX_U32 flag) {
    return (gX_I1)IsWindowState((unsigned int)flag);
}
gX_I0 raylib_Window_SetState(gX_U32 flags) {
    SetWindowState((unsigned int)flags);
}
gX_I0 raylib_Window_ClearState(gX_U32 flags) {
    ClearWindowState((unsigned int)flags);
}
gX_I0 raylib_Window_ToggleFullscreen(gX_I0) {
    ToggleFullscreen();
}
gX_I0 raylib_Window_ToggleBorderless(gX_I0) {
    ToggleBorderlessWindowed();
}
gX_I0 raylib_Window_Minimize(gX_I0) {
    MinimizeWindow();
}
gX_I0 raylib_Window_Maximize(gX_I0) {
    MaximizeWindow();
}
gX_I0 raylib_Window_Restore(gX_I0) {
    RestoreWindow();
}
gX_I0 raylib_Window_SetTitle(const gX_I8 *title) {
    SetWindowTitle((const char *)title);
}
gX_I0 raylib_Window_SetPosition(gX_I32 x, gX_I32 y) {
    SetWindowPosition((int)x, (int)y);
}
gX_I0 raylib_Window_SetMonitor(gX_I32 monitor) {
    SetWindowMonitor((int)monitor);
}

// ---------------------------------------------------------------- frame begin/end

gX_I0 raylib_Drawing_Begin(gX_I0) {
    BeginDrawing();
}

gX_I0 raylib_Drawing_End(gX_I0) {
    EndDrawing();
}

//opencode: RHI draws bypass raylib's queue; flush native primitives at the
//   boundary so they execute against the current framebuffer and GL state.
gX_I0 raylib_Drawing_Flush(gX_I0) {
    rlDrawRenderBatchActive();
}

// ---------------------------------------------------------------- audio control

gX_I0 raylib_AudioStream_Play(gX_AudioStream stream) {
    AudioStream as = gX_AudioStream_ConvertToBackend(stream);
    PlayAudioStream(as);
}

gX_I0 raylib_AudioStream_Update(gX_AudioStream stream, const gX_I0 *data, gX_I32 frameCount) {
    AudioStream as = gX_AudioStream_ConvertToBackend(stream);
    UpdateAudioStream(as, data, frameCount);
}

gX_I0 raylib_Sound_Update(gX_Sound sound, gX_I0 *data, gX_I32 sampleCount) {
    Sound snd = gX_Sound_ConvertToBackend(sound);
    UpdateSound(snd, data, sampleCount);
}

gX_I0 raylib_Sound_Play(gX_Sound sound) {
    Sound snd = gX_Sound_ConvertToBackend(sound);
    PlaySound(snd);
}

gX_I0 raylib_Sound_Stop(gX_Sound sound) {
    Sound snd = gX_Sound_ConvertToBackend(sound);
    StopSound(snd);
}

gX_I1 raylib_Sound_IsPlaying(gX_Sound sound) {
    Sound snd = gX_Sound_ConvertToBackend(sound);
    return (gX_I1)IsSoundPlaying(snd);
}

gX_I0 raylib_Sound_SetPan(gX_Sound sound, gX_F32 pan) {
    Sound snd = gX_Sound_ConvertToBackend(sound);
    SetSoundPan(snd, pan);
}

gX_I0 raylib_Sound_SetVolume(gX_Sound sound, gX_F32 volume) {
    Sound snd = gX_Sound_ConvertToBackend(sound);
    SetSoundVolume(snd, volume);
}

gX_I0 raylib_Sound_SetPitch(gX_Sound sound, gX_F32 pitch) {
    Sound snd = gX_Sound_ConvertToBackend(sound);
    SetSoundPitch(snd, pitch);
}

gX_I0 raylib_Music_Update(gX_Music music) {
    Music mus = gX_Music_ConvertToBackend(music);
    UpdateMusicStream(mus);
}

gX_I0 raylib_Music_Play(gX_Music music) {
    Music mus = gX_Music_ConvertToBackend(music);
    PlayMusicStream(mus);
}

gX_I0 raylib_Music_Pause(gX_Music music) {
    Music mus = gX_Music_ConvertToBackend(music);
    PauseMusicStream(mus);
}

gX_I0 raylib_Music_Stop(gX_Music music) {
    Music mus = gX_Music_ConvertToBackend(music);
    StopMusicStream(mus);
}

gX_I0 raylib_Music_Resume(gX_Music music) {
    Music mus = gX_Music_ConvertToBackend(music);
    ResumeMusicStream(mus);
}

gX_I0 raylib_Music_Seek(gX_Music music, gX_F32 position) {
    Music mus = gX_Music_ConvertToBackend(music);
    SeekMusicStream(mus, position);
}

gX_I0 raylib_Music_SetVolume(gX_Music music, gX_F32 volume) {
    Music mus = gX_Music_ConvertToBackend(music);
    SetMusicVolume(mus, volume);
}

gX_I0 raylib_Music_SetPitch(gX_Music music, gX_F32 pitch) {
    Music mus = gX_Music_ConvertToBackend(music);
    SetMusicPitch(mus, pitch);
}

gX_I0 raylib_Music_SetPan(gX_Music music, gX_F32 pan) {
    Music mus = gX_Music_ConvertToBackend(music);
    SetMusicPan(mus, pan);
}

gX_I1 raylib_Music_IsPlaying(gX_Music music) {
    Music mus = gX_Music_ConvertToBackend(music);
    return (gX_I1)IsMusicStreamPlaying(mus);
}

gX_F32 raylib_Music_GetLength(gX_Music music) {
    Music mus = gX_Music_ConvertToBackend(music);
    return (gX_F32)GetMusicTimeLength(mus);
}

gX_F32 raylib_Music_GetTimePlayed(gX_Music music) {
    Music mus = gX_Music_ConvertToBackend(music);
    return (gX_F32)GetMusicTimePlayed(mus);
}

// ---------------------------------------------------------------- texture control

gX_I0 raylib_Texture_SetFilter(gX_Texture texture, gX_I32 filter) {
    Texture tex = gX_Texture_ConvertToBackend(texture);
    SetTextureFilter(tex, (int)filter);
}

gX_I0 raylib_Texture_SetWrap(gX_Texture texture, gX_I32 wrap) {
    Texture tex = gX_Texture_ConvertToBackend(texture);
    SetTextureWrap(tex, (int)wrap);
}

gX_I0 raylib_Texture_Update(gX_Texture texture, const gX_I0 *pixels) {
    Texture tex = gX_Texture_ConvertToBackend(texture);
    UpdateTexture(tex, pixels);
}

gX_I0 raylib_Texture_UpdateRec(gX_Texture texture, gX_Rectangle rec, const gX_I0 *pixels) {
    Texture tex = gX_Texture_ConvertToBackend(texture);
    Rectangle r = gX_Rectangle_ConvertToBackend(rec);
    UpdateTextureRec(tex, r, pixels);
}

// ---------------------------------------------------------------- text control

gX_I32 raylib_Font_GetGlyphIndex(gX_Font font, gX_I32 codepoint) {
    Font fnt = gX_Font_ConvertToBackend(font);
    return (gX_I32)GetGlyphIndex(fnt, codepoint);
}

// ---------------------------------------------------------------- color/misc

gX_I32 raylib_Color_GetPixelDataSize(gX_I32 width, gX_I32 height, gX_I32 format) {
    return (gX_I32)GetPixelDataSize((int)width, (int)height, (int)format);
}

// ---------------------------------------------------------------- file control

gX_I1 raylib_FileData_Save(const gX_I8 *fileName, gX_I0 *data, gX_I32 dataSize) {
    return (gX_I1)SaveFileData((const char *)fileName, data, dataSize);
}

const gX_I8 *raylib_Directory_Working_Get(gX_I0) {
    return (const gX_I8 *)GetWorkingDirectory();
}
