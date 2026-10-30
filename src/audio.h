/**********************************************************************************************
*
*   [gX: src/audio.h]
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


#ifndef AUDIO_H
#define AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/types.h"
#include "common/text.h"
#include "common/heap.h"

//opencode: dynamic sound registry — string-keyed, loaded on demand
#define GX_AUDIO_SOUND_REGISTRY_MAX 64

typedef struct gX_Audio_State {
    gX_I1 play_music;
    gX_I1 pause_music;
    gX_I1 stop_music;
    gX_I1 music_change_volume;
} gX_Audio_State;

typedef struct gX_Audio_Sound {
    gX_Sound sound;
    gX_Sound_Parameters params;
} gX_Audio_Sound;

typedef struct gX_Audio_Music {
    gX_I1 loaded;
    gX_I1 external;
    gX_TextLine path;
    gX_Music music;
    gX_Sound_Parameters params;
    //ai: user volume as set through the music API; the applied
    //   params.volume is userVolume * the mixer's music bus gain.
    gX_F32 userVolume;
} gX_Audio_Music;

//opencode: a single entry in the dynamic sound registry
typedef struct gX_Audio_SoundEntry {
    gX_I8 key[48];
    gX_I8 path[256];
    gX_Sound sound;     //opencode: deep copy (not pointer) — survives RM heap realloc
    gX_I1 loaded;
    gX_U8 used;
} gX_Audio_SoundEntry;

typedef struct gX_Audio_Gen {
    gX_U32 state;
    gX_Wave wave;
    gX_Sound result;
    
    gX_I1 flip;
    gX_Sound bfr0;
    gX_Sound bfr1;

    //opencode: runtime one-shot sound effects
    gX_Sound shots[8];
    gX_F32   shotFreq[8];
    gX_F32   volume;  //opencode: master volume for continuous tone (0.0..1.0)
} gX_Audio_Gen;

//opencode: 3D positional sound source
typedef struct gX_Audio_Source3D {
    gX_I1    active;
    gX_Sound sound;
    gX_F32v3 position;
    gX_F32   baseVolume;
    gX_F32   maxDist;
    gX_F32   refDist;
} gX_Audio_Source3D;

//opencode: waveform types for procedural generation
#define GX_AUDIO_WAVE_SINE     0
#define GX_AUDIO_WAVE_SQUARE   1
#define GX_AUDIO_WAVE_SAWTOOTH 2
#define GX_AUDIO_WAVE_TRIANGLE 3
#define GX_AUDIO_WAVE_NOISE    4

//opencode: play a procedural sound effect. freq=440=A4, durationMs=500,
//   waveform=GX_AUDIO_WAVE_SQUARE, volume=0.0..1.0. Returns 0 on success.
gX_I32 gX_Audio_Gen_Play(gX_F32 freq, gX_I32 durationMs, gX_I32 waveform, gX_F32 volume);

//opencode: play a sound at a 3D world position. Returns slot index or <0 on error.
gX_I32 gX_Audio_Play3D(gX_Sound sound, gX_F32v3 position, gX_F32 volume, gX_F32 maxDist);
//ai: the pitch-aware form — the multi-copy SFX pool needs per-play pitch
//   variation on one-shot impacts. gX_Audio_Play3D is the pitch-1.0 alias.
gX_I32 gX_Audio_Play3DEx(gX_Sound sound, gX_F32v3 position, gX_F32 volume, gX_F32 pitch, gX_F32 maxDist);

//opencode: reverb parameters
typedef struct gX_Audio_Reverb {
    gX_I1  enabled;
    gX_F32 roomSize;   // 0.0..1.0 (delay time)
    gX_F32 damping;    // 0.0..1.0 (high-frequency absorption)
    gX_F32 wet;        // 0.0..1.0 (mix level)
    gX_F32 dry;        // 0.0..1.0 (dry level)
} gX_Audio_Reverb;

extern gX_Audio_Reverb g_audioReverb;
gX_I0 gX_Audio_Reverb_Init(gX_I0);
gX_I0 gX_Audio_Reverb_Set(gX_F32 roomSize, gX_F32 damping, gX_F32 wet);
//ai: drop the current tail (zero the delay line/indices) — a scene switch
//   can silence the room without disabling the effect.
gX_I0 gX_Audio_Reverb_Clear(gX_I0);
//ai: reverb core — stereo interleaved float, in place. The raylib mixed
//   processor adapter calls this; unit tests render with it directly (no
//   audio device needed). Returns 0 when it processed, 1 when it is a no-op
//   (disabled/never enabled/invalid buffer).
gX_I32 gX_Audio_Reverb_Process(gX_F32 *samples, gX_U32 frames);
//ai: whether the reverb processor is in the mixed chain (the master filter's
//   attach guard tracks it like the engines).
gX_I1 gX_Audio_Reverb_IsAttached(gX_I0);

//ai: mixer buses — every gX playback path scales by its bus, so scripts,
//   scenes and the UI can balance the mix without touching individual
//   sounds. The persisted user values live in gX_Settings (the music/sfx/
//   synth volume fields); gX_Settings_Update pushes field changes here
//   (live), and gX_Audio_BusSetVolume lets scripts drive the live mix
//   directly. Master is the device master volume (raylib), 0.0..2.0; the
//   other buses are 0.0..1.0 gains.
#define GX_AUDIO_BUS_MASTER 0
#define GX_AUDIO_BUS_MUSIC  1
#define GX_AUDIO_BUS_SFX    2
#define GX_AUDIO_BUS_SYNTH  3
#define GX_AUDIO_BUS_COUNT  4

gX_I0  gX_Audio_BusSetVolume(gX_I32 bus, gX_F32 volume);
gX_F32 gX_Audio_BusGetVolume(gX_I32 bus);
//ai: the gain a playback path multiplies in (1.0 when a bus is unset).
gX_F32 gX_Audio_BusGain(gX_I32 bus);

//ai: play a registry-owned sound through the SFX bus (GUI feedback, engine
//   cues). The caller's params.volume is scaled by the bus gain; returns 1
//   when a play was issued.
gX_I1 gX_Audio_Sound_PlayBus(gX_Sound *sound, gX_Sound_Parameters params);

//ai: global playback mute (GX_AUDIO_MUTE=1 at launch gates every gX play
//   path — registry, one-shots, 3D, SFX, music — for audits). The synth has
//   its own mute (GX_SYNTH_MUTE); voices/visualizers keep running either way.
gX_I0 gX_Audio_SetMuted(gX_I1 muted);
gX_I1 gX_Audio_IsMuted(gX_I0);

typedef struct gX_Audio {
    gX_I8 name[128];
    gX_I8 description[128];
    
    gX_Audio_State state;
    gX_I1 loaded;
    gX_I1 muted;              //ai: global playback mute (see gX_Audio_SetMuted)

    //ai: mixer bus volumes — live gains (see gX_Audio_BusSetVolume)
    gX_F32 mix[GX_AUDIO_BUS_COUNT];
    //ai: 0 until a bus is set (settings push) or the audio module loads; the
    //   gain accessor returns 1.0 while uninitialized so code that renders
    //   before audio init (boot-time unit tests) is not silenced.
    gX_I1 mixInitialized;
    
    gX_Audio_Sound testSound;
    
    gX_Audio_Sound pool[64];
    
    gX_Audio_Music music;
    
    //opencode: dynamic sound registry replaces the old 8 hardcoded slots
    gX_Audio_SoundEntry soundRegistry[GX_AUDIO_SOUND_REGISTRY_MAX];
    gX_U32 soundRegistryCount;
    gX_Heap *soundHeap;          // shared RM heap for all sound entries
    gX_I32 soundRefreshState;
    
    gX_Audio_Gen gen;
    gX_Sound     silentLoop;          //opencode: silent looping buffer (PulseAudio idle noise fix)

    gX_Audio_Source3D sources3D[16];  //opencode: 3D positional sounds

} gX_Audio;

extern gX_Audio audio;

//opencode: dynamic sound registry API
gX_I32 gX_Audio_SoundRegister(const gX_I8 *key, const gX_I8 *path);
gX_Sound *gX_Audio_SoundGet(const gX_I8 *key);
gX_I1 gX_Audio_SoundPlay(const gX_I8 *key);
gX_I1 gX_Audio_SoundPlayParams(const gX_I8 *key, gX_Sound_Parameters *params);

gX_I32 gX_Audio_Load(gX_I0);
gX_I32 gX_Audio_Unload(gX_I0);

gX_I32 gX_Audio_Music_Load(const gX_I8 *name);
gX_I32 gX_Audio_Music_Unload(gX_I0);
gX_I32 gX_Audio_Music_LoadResourceIndex(gX_I32 index);
gX_I32 gX_Audio_Music_LoadResourceName(const gX_I8 *name);
gX_I32 gX_Audio_Music_Play(gX_I0);
gX_I32 gX_Audio_Music_Pause(gX_I0);
gX_I32 gX_Audio_Music_Stop(gX_I0);
gX_I32 gX_Audio_Music_SetVolume(gX_F32 volume);

gX_I0 gX_Audio_Update(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //AUDIO_H
