/**********************************************************************************************
* 
*   [wrappers/audio.h]
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
#ifndef WRAPPERS_AUDIO_H
#define WRAPPERS_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "../common/config.h"
#include "../common/types.h"

//opencode: Sound_Wrapper (no Load/Unload/Update — handled by rmheap)
typedef struct gX_Sound_Wrapper {
    gX_LoadingState loadingState;
    gX_TextLine path;
    gX_Sound    sound;
    gX_Sound_Parameters params;
} gX_Sound_Wrapper;
    
gX_Wave  gX_Wave_Load(      const gX_I8 *path);
gX_I0    gX_Wave_Unload(    gX_Wave wave);
gX_I1    gX_Wave_Ready(     gX_Wave wave);
gX_Wave  gX_Wave_Copy(      gX_Wave wave);
gX_I0    gX_Wave_Crop(      gX_Wave *wave, gX_I32 initSample, gX_I32 finalSample);
    
gX_Sound gX_Sound_Load(     const gX_I8 *path);
gX_Sound gX_Sound_LoadWave( gX_Wave wave);
gX_I0    gX_Sound_Unload(               gX_Sound  sound);
gX_I1    gX_Sound_Ready(                gX_Sound  sound);
gX_I0    gX_Sound_Update(               gX_Sound  sound, gX_I0 *data, gX_I32 sampleCount);
gX_I0    gX_Sound_Play(                 gX_Sound  sound);
gX_I0    gX_Sound_PlayPtr(              gX_Sound *sound);
gX_I0    gX_Sound_Play_Parameters(      gX_Sound  sound, gX_Sound_Parameters params);
gX_I0    gX_Sound_PlayPtr_Parameters(   gX_Sound *sound, gX_Sound_Parameters params);
gX_I0    gX_Sound_Play_Quiet(           gX_Sound  sound);
gX_I0    gX_Sound_Stop(                 gX_Sound  sound);
gX_I1    gX_Sound_IsPlaying(            gX_Sound  sound);
gX_I0    gX_Sound_SetPan(               gX_Sound  sound, gX_F32 pan);
gX_I0    gX_Sound_SetVolume(            gX_Sound  sound, gX_F32 volume);

gX_Music gX_Music_Load(     const gX_I8 *path);
gX_I0    gX_Music_Unload(           gX_Music music);
gX_I0    gX_Music_Update(           gX_Music music);
gX_I0    gX_Music_Update_Parameters(gX_Music music, gX_Sound_Parameters *params);
gX_I1    gX_Music_IsPlaying(        gX_Music music);
gX_I0    gX_Music_Play(             gX_Music music);
gX_I0    gX_Music_Pause(            gX_Music music);
gX_I0    gX_Music_Stop(             gX_Music music);
gX_I0    gX_Music_Resume(           gX_Music music);
gX_I0    gX_Music_Seek(             gX_Music music, gX_F32 position);
gX_I0    gX_Music_SetVolume(        gX_Music music, gX_F32 volume);
gX_I0    gX_Music_SetPitch(         gX_Music music, gX_F32 pitch);
gX_I0    gX_Music_SetPan(           gX_Music music, gX_F32 pan);
gX_F32   gX_Music_GetLength(        gX_Music music);
gX_F32   gX_Music_GetTimePlayed(    gX_Music music);

#ifdef __cplusplus
}
#endif

#endif //WRAPPERS_H
