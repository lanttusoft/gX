/**********************************************************************************************
*
*   [gX: src/audio.c]
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


#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"

#include "common/alloc.h"
#include "common/config.h"
#include "common/enum.h"
#include "common/log.h"

#include "gxmath/gxmath.h"

#include "resource/resource.h"

#include "camera.h"
#include "common/path.h"

//opencode: raylib audio processor API
#if defined(GAME_BACKEND_RAYLIB)
#include "raylib.h"
#endif

#include "main.h"
#include "audio/filter.h"
#include "audio/opx.h"
#include "audio/pad.h"
#include "audio/sfx.h"
#include "audio/synth.h"

#include "wrappers/wrappers.h"

gX_Audio audio;

//ai: mixer buses — live gains every playback path multiplies in. MASTER is
//   applied through raylib's device master volume (0.0..2.0, the settings
//   slider range); the rest are 0.0..1.0 gains.
static gX_F32 gX_Audio_Bus_Clamp(gX_I32 bus, gX_F32 volume) {
    gX_F32 max = (bus == GX_AUDIO_BUS_MASTER) ? 2.0f : 1.0f;
    if (volume < 0.0f) return 0.0f;
    if (volume > max) return max;
    return volume;
}

gX_I0 gX_Audio_BusSetVolume(gX_I32 bus, gX_F32 volume) {
    if (bus < 0 || bus >= GX_AUDIO_BUS_COUNT) return;
    volume = gX_Audio_Bus_Clamp(bus, volume);
    audio.mix[bus] = volume;
    audio.mixInitialized = 1;
    if (bus == GX_AUDIO_BUS_MASTER) {
        maindata.masterVolume = volume;
        gX_MasterVolume_Set(volume);
    }
}

gX_F32 gX_Audio_BusGetVolume(gX_I32 bus) {
    if (bus < 0 || bus >= GX_AUDIO_BUS_COUNT) return 0.0f;
    if (!audio.mixInitialized) return 1.0f;
    return audio.mix[bus];
}

gX_F32 gX_Audio_BusGain(gX_I32 bus) {
    if (bus < 0 || bus >= GX_AUDIO_BUS_COUNT) return 1.0f;
    return audio.mixInitialized ? audio.mix[bus] : 1.0f;
}

gX_I0 gX_Audio_SetMuted(gX_I1 muted) {
    audio.muted = muted ? 1 : 0;
}

gX_I1 gX_Audio_IsMuted(gX_I0) {
    return audio.muted;
}

static gX_I0 gX_Audio_Gen_Generate(gX_Wave *wave, gX_F32 freq, gX_I32 waveform, gX_F32 volume) {
    gX_F32 *ptr = (gX_F32*)wave->data;
    for (gX_U32 i = 0; i < wave->frameCount; i++) {
        gX_F32 t = (gX_F32)i;
        gX_F32 phase = t * freq * 2.0f * gX_PI / (gX_F32)wave->sampleRate;
        gX_F32 res = 0.0f;
        switch (waveform) {
            case GX_AUDIO_WAVE_SINE:
                res = sinf(phase);
                break;
            case GX_AUDIO_WAVE_SQUARE:
                for (gX_I32 k = 1; k <= 16; k += 2)
                    res += (0.5f / (gX_F32)k) * sinf((gX_F32)k * phase);
                break;
            case GX_AUDIO_WAVE_SAWTOOTH:
                for (gX_I32 k = 1; k <= 16; k++)
                    res += (0.3f / (gX_F32)k) * sinf((gX_F32)k * phase);
                break;
            case GX_AUDIO_WAVE_TRIANGLE:
                for (gX_I32 k = 1; k <= 16; k += 2) {
                    gX_F32 sign = ((k / 2) % 2) ? -1.0f : 1.0f;
                    res += sign * (0.3f / ((gX_F32)k * (gX_F32)k)) * sinf((gX_F32)k * phase);
                }
                break;
            case GX_AUDIO_WAVE_NOISE: {
                static gX_U32 noiseSeed = 42;
                noiseSeed = noiseSeed * 1103515245u + 12345u;
                res = (gX_F32)(noiseSeed >> 16) / 32768.0f - 1.0f;
                break;
            }
            default:
                res = sinf(phase);
        }
        *ptr = res * volume;
        ptr++;
    }
}

static gX_I0 gX_Audio_Gen_Load(gX_Audio_Gen *gen) {
    gX_U32 bytes = gen->wave.frameCount * (gen->wave.sampleSize / 8);
    gen->wave.data = gX_Calloc(bytes);
    gen->volume = 0.25f;
    //opencode: one-shot sounds only — continuous tone is disabled
    gen->bfr0 = (gX_Sound){0};
    gen->bfr1 = (gX_Sound){0};
    gen->flip = 0;
}

static gX_I0 gX_Audio_Gen_Unload(gX_Audio_Gen *gen) {
    //opencode: clean up any active one-shot sounds
    for (gX_I32 i = 0; i < 8; i++)
        if (gen->shots[i].stream.buffer) gX_Sound_Unload(gen->shots[i]);
    gX_Free(gen->wave.data);
    gen->wave.data = NULL;
}

static gX_I0 gX_Audio_Gen_Update(gX_Audio_Gen *gen) {
    if (!gen->wave.data) return;

    //opencode: clean up finished one-shot sounds
    for (gX_I32 i = 0; i < 8; i++) {
        if (gen->shots[i].stream.buffer && !gX_Sound_IsPlaying(gen->shots[i])) {
            gX_Sound_Unload(gen->shots[i]);
            gen->shots[i] = (gX_Sound){0};
            gen->shotFreq[i] = 0.0f;
        }
    }
}

gX_I32 gX_Audio_Gen_Play(gX_F32 freq, gX_I32 durationMs, gX_I32 waveform, gX_F32 volume) {
    gX_Audio_Gen *gen = &audio.gen;
    if (!gen->wave.data) return 1;
    //ai: mixer gate — global mute and a pulled-down SFX bus skip the
    //   synthesis work, not just the playback.
    if (audio.muted) return 1;
    volume *= gX_Audio_BusGain(GX_AUDIO_BUS_SFX);
    if (volume <= 0.001f) return 1;

    gX_I32 slot = -1;
    for (gX_I32 i = 0; i < 8; i++) {
        if (!gen->shots[i].stream.buffer) { slot = i; break; }
    }
    if (slot < 0) return 2;

    gX_U32 frameCount = (gX_U32)((gX_F32)durationMs / 1000.0f * (gX_F32)gen->wave.sampleRate);
    if (frameCount < 10) frameCount = 10;

    gX_Wave w = {0};
    w.frameCount = frameCount;
    w.sampleRate = gen->wave.sampleRate;
    w.sampleSize = gen->wave.sampleSize;
    w.channels = 1;
    w.data = gX_Calloc(frameCount * (w.sampleSize / 8));
    if (!w.data) {
        gX_TraceLog(gX_LOG_ERROR, "AUDIO: gen_play calloc failed (%u bytes)", frameCount * (w.sampleSize / 8));
        return 3;
    }

    gX_Audio_Gen_Generate(&w, freq, waveform, volume);
    //opencode: fade out last 10% to avoid click
    gX_F32 *ptr = (gX_F32*)w.data;
    gX_U32 total = frameCount;
    gX_U32 fadeStart = total - total / 10;
    for (gX_U32 i = fadeStart; i < total; i++)
        ptr[i] *= 1.0f - (gX_F32)(i - fadeStart) / (gX_F32)(total - fadeStart);

    gen->shots[slot] = gX_Sound_LoadWave(w);
    gen->shotFreq[slot] = freq;
    gX_Free(w.data);
    gX_Sound_Play(gen->shots[slot]);
    return 0;
}

// ========================================================================
// 3D positional sound
// ========================================================================

//ai: pitch-aware 3D play — the SFX pool needs a per-play pitch on the same
//   sound copies; gX_Audio_Play3D passes 1.0.
gX_I32 gX_Audio_Play3DEx(gX_Sound sound, gX_F32v3 position, gX_F32 volume, gX_F32 pitch, gX_F32 maxDist) {
    if (audio.muted) return 1;
    //opencode: find a free slot
    gX_I32 slot = -1;
    for (gX_I32 i = 0; i < 16; i++) {
        if (!audio.sources3D[i].active) { slot = i; break; }
    }
    if (slot < 0) return 1;

    audio.sources3D[slot].active = 1;
    audio.sources3D[slot].sound = sound;
    audio.sources3D[slot].position = position;
    audio.sources3D[slot].baseVolume = volume;
    audio.sources3D[slot].maxDist = maxDist;
    audio.sources3D[slot].refDist = 1.0f;

    //opencode: play initially at center, bus-scaled volume; the per-frame
    //   3D update takes over attenuation/pan.
    gX_Sound_Play_Parameters(sound, (gX_Sound_Parameters){
        .volume = volume * gX_Audio_BusGain(GX_AUDIO_BUS_SFX),
        .pitch  = (pitch > 0.0f) ? pitch : 1.0f,
        .pan    = 0.0f,
        .play   = true
    });
    return 0;
}

gX_I32 gX_Audio_Play3D(gX_Sound sound, gX_F32v3 position, gX_F32 volume, gX_F32 maxDist) {
    return gX_Audio_Play3DEx(sound, position, volume, 1.0f, maxDist);
}

static gX_I0 gX_Audio_Update3D(gX_I0) {
    //opencode: listener is the camera
    gX_F32v3 listener = camera.camera3D.position;
    gX_F32v3 forward = gX_F32v3_Sub(camera.camera3D.target, listener);
    gX_F32 fwdLen = sqrtf(forward.x * forward.x + forward.z * forward.z);
    if (fwdLen < 0.001f) fwdLen = 0.001f;

    for (gX_I32 i = 0; i < 16; i++) {
        gX_Audio_Source3D *src = &audio.sources3D[i];
        if (!src->active) continue;
        if (!gX_Sound_IsPlaying(src->sound)) {
            src->active = 0;
            continue;
        }

        //opencode: distance attenuation
        gX_F32v3 delta = gX_F32v3_Sub(src->position, listener);
        gX_F32 dist = sqrtf(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        gX_F32 atten = (dist <= src->refDist) ? 1.0f :
                       (dist >= src->maxDist) ? 0.0f :
                       1.0f - (dist - src->refDist) / (src->maxDist - src->refDist);
        //ai: mixer — the SFX bus scales in-flight positional sources too
        gX_F32 vol = src->baseVolume * atten * gX_Audio_BusGain(GX_AUDIO_BUS_SFX);

        //opencode: stereo pan — raylib SetSoundPan expects -1 (left) to 1 (right)
        gX_F32v3 right = { forward.z / fwdLen, 0.0f, -forward.x / fwdLen };
        gX_F32 dot = (delta.x * right.x + delta.z * right.z);
        gX_F32 pan = dot / (dist + 0.001f);
        if (pan < -1.0f) pan = -1.0f;
        if (pan > 1.0f) pan = 1.0f;

        gX_Sound_SetVolume(src->sound, vol);
        gX_Sound_SetPan(src->sound, pan);
    }
}

gX_I32 gX_Audio_SoundRegister(const gX_I8 *key, const gX_I8 *path) {
    if (audio.soundRegistryCount >= GX_AUDIO_SOUND_REGISTRY_MAX) {
        gX_TraceLog_AUDIO(gX_LOG_ERROR, "Sound registry full (%d max)", GX_AUDIO_SOUND_REGISTRY_MAX);
        return -1;
    }
    //opencode: check for duplicate key — replace silently
    for (gX_U32 i = 0; i < audio.soundRegistryCount; i++) {
        if (strcmp(audio.soundRegistry[i].key, key) == 0) {
            gX_TraceLog_AUDIO(gX_LOG_WARNING, "Sound registry: replacing existing key '%s'", key);
            strncpy(audio.soundRegistry[i].path, path, sizeof(audio.soundRegistry[i].path) - 1);
            audio.soundRegistry[i].loaded = false;
            memset(&audio.soundRegistry[i].sound, 0, sizeof(gX_Sound));
            return (gX_I32)i;
        }
    }
    gX_U32 idx = audio.soundRegistryCount;
    strncpy(audio.soundRegistry[idx].key, key, sizeof(audio.soundRegistry[idx].key) - 1);
    strncpy(audio.soundRegistry[idx].path, path, sizeof(audio.soundRegistry[idx].path) - 1);
    memset(&audio.soundRegistry[idx].sound, 0, sizeof(gX_Sound));
    audio.soundRegistry[idx].loaded = false;
    audio.soundRegistry[idx].used = 1;
    audio.soundRegistryCount++;
    return (gX_I32)idx;
}

//opencode: register a procedurally generated sound (no file, no third-party
//   copyright — replaces the legacy gameX-era UI wavs). Synth into PCM at
//   init, fade last 10% to avoid clicks, load into the registry directly.
static gX_I32 gX_Audio_GenRegisterWave(const gX_I8 *key, gX_F32 freq, gX_I32 waveform, gX_U32 durationMs, gX_F32 volume) {
    gX_U32 frames = (gX_U32)((gX_F64)audio.gen.wave.sampleRate * (gX_F64)durationMs / 1000.0);
    if (frames < 16) frames = 16;
    gX_I0 *data = gX_Calloc(frames * (audio.gen.wave.sampleSize / 8));
    if (!data) return -1;
    gX_Wave w = {frames, audio.gen.wave.sampleRate, audio.gen.wave.sampleSize, 1, data};
    gX_Audio_Gen_Generate(&w, freq, waveform, volume);
    gX_F32 *ptr = (gX_F32*)data;
    gX_U32 fadeStart = frames - frames / 10;
    for (gX_U32 i = fadeStart; i < frames; i++)
        ptr[i] *= 1.0f - (gX_F32)(i - fadeStart) / (gX_F32)(frames - fadeStart);
    gX_Sound snd = gX_Sound_LoadWave(w);
    gX_Free(data);
    if (snd.frameCount == 0) return -1;
    gX_I32 idx = gX_Audio_SoundRegister(key, "");
    if (idx < 0) return idx;
    audio.soundRegistry[idx].sound = snd;
    audio.soundRegistry[idx].loaded = true;
    return idx;
}

//opencode: forward declaration for lazy loading
static gX_I0 gX_Audio_LoadRegistryEntry(gX_U32 idx);
//ai: forward declaration — Music_Load applies the derived volume before the
//   function body below.
static gX_I0 gX_Audio_Music_ApplyVolume(gX_I0);

gX_Sound *gX_Audio_SoundGet(const gX_I8 *key) {
    if (!key) return NULL;
    for (gX_U32 i = 0; i < audio.soundRegistryCount; i++) {
        if (strcmp(audio.soundRegistry[i].key, key) == 0) {
            //opencode: lazy-load on first access if not loaded yet
            if (!audio.soundRegistry[i].loaded && audio.soundRegistry[i].path[0]) {
                gX_Audio_LoadRegistryEntry(i);
            }
            //opencode: only return valid sounds — prevents crash on zeroed structs
            if (audio.soundRegistry[i].loaded && audio.soundRegistry[i].sound.frameCount > 0) {
                return &audio.soundRegistry[i].sound;
            }
            return NULL;
        }
    }
    return NULL;
}

gX_I1 gX_Audio_SoundPlay(const gX_I8 *key) {
    gX_Sound *s = gX_Audio_SoundGet(key);
    if (s && s->frameCount > 0 && maindata.backendAudioLoaded && !audio.muted) {
        return gX_Audio_Sound_PlayBus(s, (gX_Sound_Parameters){
            .volume = 1.0f, .pitch = 1.0f, .pan = 0.0f, .play = true
        });
    }
    return 0;
}

gX_I1 gX_Audio_SoundPlayParams(const gX_I8 *key, gX_Sound_Parameters *params) {
    gX_Sound *s = gX_Audio_SoundGet(key);
    if (s && s->frameCount > 0 && maindata.backendAudioLoaded && !audio.muted) {
        return gX_Audio_Sound_PlayBus(s, *params);
    }
    return 0;
}

//ai: the bus-aware SFX play — GUI feedback and engine cues route here so the
//   mixer's SFX gain (and the global mute) applies to registry sounds too.
gX_I1 gX_Audio_Sound_PlayBus(gX_Sound *sound, gX_Sound_Parameters params) {
    if (!sound || sound->frameCount == 0) return 0;
    if (!maindata.backendAudioLoaded || audio.muted) return 0;
    params.volume *= gX_Audio_BusGain(GX_AUDIO_BUS_SFX);
    if (params.volume <= 0.001f) return 0;
    gX_Sound_Play_Parameters(*sound, params);
    return 1;
}

gX_I32 gX_Audio_Load(gX_I0) {
    gX_TRACELOG_PROLOGUE("Audio_Load","AUDIO: Loading audio")
    
    audio.loaded = false;
    audio.soundRegistryCount = 0;
    audio.soundRefreshState = 0;
    audio.soundHeap = NULL;
    
    //ai: --disableAudio skips the audio subsystem entirely (no device, no
    //   registry synthesis, no SFX pool); every play path already gates on
    //   maindata.backendAudioLoaded. The synth keeps its CPU state (voices
    //   and the offline render core stay valid) — only the device path is
    //   gone.
    if (!maindata.backendAudioUse) {
#if gX_ENABLE_AUDIO == 1
        gX_Synth_Init();
        gX_OPX_Init();
        gX_Pad_Init();
#endif
        gX_Audio_MasterFilter_Init();
        gX_TraceLog_AUDIO(gX_LOG_NOTICE, "audio disabled (--disableAudio)");
        gX_TRACELOG_EPILOGUE("AUDIO: Loading skipped")
        return 0;
    }

    //ai: mixer defaults — the settings module pushes the persisted user
    //   values before this point (settings load runs earlier); only a run
    //   without that push (or an unset bus) falls back to open buses.
    if (!audio.mixInitialized) {
        for (gX_I32 bus = 0; bus < GX_AUDIO_BUS_COUNT; bus++) audio.mix[bus] = 1.0f;
        audio.mix[GX_AUDIO_BUS_MASTER] = (maindata.masterVolume > 0.0f) ? maindata.masterVolume : 1.0f;
        audio.mixInitialized = 1;
    }
    gX_MasterVolume_Set(audio.mix[GX_AUDIO_BUS_MASTER]);

    //ai: global playback mute for audits (GX_AUDIO_MUTE=1) — everything gX
    //   plays stays silent; the synth's own GX_SYNTH_MUTE is separate.
    audio.muted = 0;
    {
        const gX_I8 *muteEnv = getenv("GX_AUDIO_MUTE");
        if (muteEnv && atoi(muteEnv) != 0) audio.muted = 1;
    }

    strcpy(audio.name,"Audio");
    strcpy(audio.description,"audio handler");
    gX_TraceLog_AUDIO(gX_LOG_GREEN,"name: %s",audio.name);
    gX_TraceLog_AUDIO(gX_LOG_GREEN,"desc: %s",audio.description);
    
    audio.gen.wave.frameCount = 48000; // framecount / samplerate = seconds ( 1 channel )
    audio.gen.wave.sampleRate = 48000;
    audio.gen.wave.sampleSize = 32;
    audio.gen.wave.channels = 1;

    //opencode: register all sound keys as procedurally generated PCM — the
    //   legacy gameX-era UI wavs were removed for copyright; synthesized tones
    //   keep the same keys so GUI/scene call sites are unchanged.
    //   (freq, waveform, ms, volume)
    gX_Audio_GenRegisterWave("buttonHover",   880.0f, GX_AUDIO_WAVE_SINE,      40, 0.15f);
    gX_Audio_GenRegisterWave("buttonConfirm", 1320.0f, GX_AUDIO_WAVE_SINE,     60, 0.20f);
    gX_Audio_GenRegisterWave("buttonCancel",  220.0f, GX_AUDIO_WAVE_SAWTOOTH,  80, 0.18f);
    gX_Audio_GenRegisterWave("warning",       660.0f, GX_AUDIO_WAVE_SQUARE,    200, 0.20f);
    gX_Audio_GenRegisterWave("successful",    990.0f, GX_AUDIO_WAVE_SINE,      120, 0.20f);
    gX_Audio_GenRegisterWave("message",       1760.0f, GX_AUDIO_WAVE_SINE,     80, 0.12f);
    gX_Audio_GenRegisterWave("notify",        440.0f, GX_AUDIO_WAVE_TRIANGLE,  50, 0.12f);
    gX_Audio_GenRegisterWave("mystery",       660.0f, GX_AUDIO_WAVE_TRIANGLE,  100, 0.15f);
    gX_Audio_GenRegisterWave("error",         165.0f, GX_AUDIO_WAVE_SAWTOOTH,  150, 0.25f);
    gX_Audio_GenRegisterWave("coin",          1318.0f, GX_AUDIO_WAVE_SINE,     50, 0.15f);
    gX_Audio_GenRegisterWave("levelup",       880.0f, GX_AUDIO_WAVE_SQUARE,    150, 0.18f);
    gX_Audio_GenRegisterWave("hurt",          262.0f, GX_AUDIO_WAVE_SAWTOOTH,  120, 0.22f);
    gX_Audio_GenRegisterWave("open",          523.0f, GX_AUDIO_WAVE_TRIANGLE,  80, 0.12f);
    gX_Audio_GenRegisterWave("close",         392.0f, GX_AUDIO_WAVE_TRIANGLE,  80, 0.12f);
    gX_Audio_GenRegisterWave("tv_jingle",     784.0f, GX_AUDIO_WAVE_SQUARE,    90, 0.15f);
    
    //opencode: no default music ships in gX (music/ dir removed at the
    //   copyright audit; music API stays, loads fail gracefully)
    audio.music.path[0] = '\0';
    audio.music.params.position = 0.0f;
    audio.music.params.volume = 0.5f;
    audio.music.params.pitch = 1.0f;
    audio.music.userVolume = 0.5f;
    
    gX_Audio_Reverb_Init();
#if gX_ENABLE_AUDIO == 1
    gX_Synth_Init();
    //ai: OPX (8-operator FM) — a separate instrument engine sharing the
    //   audio device, the synth bus and the tests' offline-core pattern.
    gX_OPX_Init();
    //ai: PAD (layered ensemble pad) — the sustained-texture instrument,
    //   same device/bus/offline-core conventions.
    gX_Pad_Init();
#endif
    //ai: the master filter (optional, off by default) — the shared TPT core
    //   on the final mixed buffer, kept at the chain tail per frame.
    gX_Audio_MasterFilter_Init();
    //ai: procedural one-shot SFX pool (sandbox impacts, tool feedback) —
    //   needs the audio device, so it initializes here.
    gX_AudioSFX_Init();
    
    //opencode: create silent buffer to keep PulseAudio stream active
    //   A silent 0.5s wave prevents PulseAudio from suspending the stream and producing clicks.
    {
        gX_U32 silentFrames = 24000;
        gX_I0 *silentData = calloc(silentFrames, 4);
        if (silentData) {
            gX_Wave sw = {silentFrames, 48000, 32, 1, silentData};
            audio.silentLoop = gX_Sound_LoadWave(sw);
            free(silentData);
        }
    }
    
    gX_TRACELOG_EPILOGUE("AUDIO: Loading done")
    return 0;
}

// Reverb — stereo delay line with feedback (attached as mixed processor)
// ========================================================================

gX_Audio_Reverb g_audioReverb = {0};

//opencode: heap-lazy buffer (compression P3) — 384 KB static → calloc on first
// enable via gX_Audio_Reverb_Set. Zero bss footprint when reverb unused.
#define REVERB_BUF_SIZE 48000  // 1 second at 48 kHz
static struct {
    gX_F32 *buf;              // stereo interleaved, allocated on first enable
    gX_U32  idxL, idxR;
    gX_F32  lpL, lpR;         //ai: damping one-pole states (per channel)
    gX_I1  inited;
    gX_I1  processorAttached;
} g_reverbState = {0};

gX_I32 gX_Audio_Unload(gX_I0) {
    gX_TRACELOG_PROLOGUE("Audio_Unload","AUDIO: Unloading audio")
    gX_Audio_Music_Unload();
    
    //opencode: free reverb delay line (compression P3 — heap-lazy)
    if (g_reverbState.buf) {
        free(g_reverbState.buf);
        g_reverbState.buf = NULL;
        g_reverbState.inited = 0;
        g_reverbState.processorAttached = 0;
    }
    
    
    if (audio.silentLoop.frameCount > 0) {
        gX_Sound_Stop(audio.silentLoop);
        gX_Sound_Unload(audio.silentLoop);
        audio.silentLoop = (gX_Sound){0};
    }
    
    gX_Audio_Gen_Unload(&audio.gen);
    //ai: the SFX pool owns audio-device buffers — release before the device
    gX_AudioSFX_Unload();
#if gX_ENABLE_AUDIO == 1
    gX_Synth_Uninit();
    gX_OPX_Unload();
    gX_Pad_Unload();
#endif
    gX_Audio_MasterFilter_Unload();
    
    gX_TRACELOG_EPILOGUE("AUDIO: Unloading done")
    return 0;
}

gX_I32 gX_Audio_Music_Load(const gX_I8 *name) {
    
    if (audio.music.loaded) {
        gX_Audio_Music_Unload();
    }
    
    //ai: bounded path copy + honest failure — the previous form strcpy'd an
    //   unbounded name and reported success even when nothing loaded.
    if (name && name[0]) {
        snprintf(audio.music.path, sizeof(audio.music.path), "%s", name);
    } else if (!audio.music.path[0]) {
        gX_TraceLog(gX_LOG_WARNING, "AUDIO: music load without a path");
        return 1;
    }
    
    audio.music.music = gX_Music_Load(audio.music.path);
    audio.music.loaded = (audio.music.music.frameCount > 0);
    audio.music.external = false;
    if (!audio.music.loaded) {
        gX_TraceLog(gX_LOG_WARNING, "AUDIO: music '%s' failed to load", audio.music.path);
        return 1;
    }
    gX_Audio_Music_ApplyVolume();
    return 0;
}

gX_I32 gX_Audio_Music_Unload(gX_I0) {
    if (audio.music.external) return 1;
    gX_Music_Unload(audio.music.music);
    audio.music.loaded = false;
    return 0;
}

gX_I32 gX_Audio_Music_LoadResourceIndex(gX_I32 index) {
    
    if (audio.music.loaded) {
        gX_Audio_Music_Unload();
    }
    
    gX_Heap *musicHeap = gX_ResourceManager_GetHeap_Type(gX_DATATYPE_STRUCT_MUSIC);
    gX_Music *musics = musicHeap->data;
    
    audio.music.music = musics[index];
    audio.music.loaded = true;
    audio.music.external = true;
    return 0;
}

gX_I32 gX_Audio_Music_Play(gX_I0) {
    gX_Music_Play(audio.music.music);
    audio.music.params.play = true;
    audio.music.params.pause = false;
    return 0;
}

gX_I32 gX_Audio_Music_Pause(gX_I0) {
    gX_Music_Pause(audio.music.music);
    audio.music.params.pause = true;
    return 0;
}

gX_I32 gX_Audio_Music_Stop(gX_I0) {
    gX_Music_Stop(audio.music.music);
    audio.music.params.play = false;
    return 0;
}

gX_I32 gX_Audio_Music_SetVolume(gX_F32 volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    audio.music.userVolume = volume;
    gX_Audio_Music_ApplyVolume();
    return 0;
}

//ai: apply user volume x the music bus gain — the applied volume is derived,
//   never stored back over the user value (the pause/Settings sliders edit
//   the settings field; gX_Settings_Update pushes the bus here live).
static gX_I0 gX_Audio_Music_ApplyVolume(gX_I0) {
    audio.music.params.volume = audio.music.userVolume * gX_Audio_BusGain(GX_AUDIO_BUS_MUSIC);
    if (audio.music.loaded) {
        gX_Music_Update_Parameters(audio.music.music, &audio.music.params);
    }
}

//opencode: load a single sound registry entry from the RM heap
static gX_I0 gX_Audio_LoadRegistryEntry(gX_U32 idx) {
    if (idx >= audio.soundRegistryCount) return;
    gX_Audio_SoundEntry *entry = &audio.soundRegistry[idx];
    if (!entry->used || !entry->path[0]) return;
    //opencode: lazy-init the sound heap if not ready yet
    if (!audio.soundHeap) {
        audio.soundHeap = gX_ResourceManager_GetHeap_Type(gX_DATATYPE_STRUCT_SOUND);
        if (!audio.soundHeap) return; // resource manager not ready yet
    }
    gX_Sound *heapSound = (gX_Sound*)gX_RMHeap_CheckElseLoadFileName(
        audio.soundHeap, GX_RESOURCE_DIR "/audio/", entry->path, 0);
    if (heapSound) {
        entry->sound = *heapSound; //opencode: deep copy — survives heap realloc
    } else {
        memset(&entry->sound, 0, sizeof(entry->sound));
    }
    entry->loaded = (entry->sound.frameCount > 0);
    gX_TraceLog_AUDIO(gX_LOG_INFO, "Sound '%s' loaded=%d frameCount=%.0f path=%s",
        entry->key, entry->loaded,
        (gX_F64)entry->sound.frameCount,
        entry->path);
}

//opencode: load all unloaded registry entries from disk
static gX_I0 gX_Audio_LoadRegistry(gX_I0) {
    if (!audio.soundHeap) {
        audio.soundHeap = gX_ResourceManager_GetHeap_Type(gX_DATATYPE_STRUCT_SOUND);
        if (!audio.soundHeap) return;
    }
    for (gX_U32 i = 0; i < audio.soundRegistryCount; i++) {
        if (!audio.soundRegistry[i].loaded) {
            gX_Audio_LoadRegistryEntry(i);
        }
    }
}

gX_I0 gX_Audio_Update(gX_I0) {
    
    if (maindata.backendLoaded && resources.defaultsLoaded) {
        if (!(audio.loaded)) {
            gX_Audio_LoadRegistry();
            
            
            gX_Audio_Gen_Load(&audio.gen);
            
            audio.loaded = true;
        }
        //opencode: refresh if sound heap was hot-reloaded
        if (audio.soundHeap && audio.soundHeap->refreshState > audio.soundRefreshState) {
            gX_Audio_LoadRegistry();
            audio.soundRefreshState = audio.soundHeap->refreshState;
        }
    }
    
    if (audio.music.loaded && audio.music.params.play) {
        gX_Music_Update_Parameters(audio.music.music,&audio.music.params);
    }
    
    //ai: music bus changes re-derive the applied music volume (the settings
    //   sliders drive the bus; the music keeps its own user volume).
    {
        static gX_F32 musicBusApplied = -1.0f;
        gX_F32 musicGain = gX_Audio_BusGain(GX_AUDIO_BUS_MUSIC);
        if (musicGain != musicBusApplied) {
            musicBusApplied = musicGain;
            gX_Audio_Music_ApplyVolume();
        }
    }

    //opencode: gen update and 3D positional update
    gX_Audio_Gen_Update(&audio.gen);
    if (maindata.backendAudioLoaded) gX_Audio_Update3D();

    //ai: synth driver — ticks the arpeggiator/sequencer and keeps the mixer
    //   attached while voices sound (lazy attach, idle detach).
#if gX_ENABLE_AUDIO == 1
    gX_Synth_Update();
    gX_OPX_Update();
    gX_Pad_Update();
#endif
    //ai: master filter attach guard — keeps it at the mixed-chain tail as
    //   the engines' lazy processors come and go (and detaches when off).
    gX_Audio_MasterFilter_Update();
    
    if (audio.state.play_music) {
        gX_Audio_Music_Play();
        audio.state.play_music = false;
    }
    
    //opencode: keep silent sound playing to prevent PulseAudio stream idle noise
    if (audio.loaded && audio.silentLoop.frameCount > 0 &&
        !gX_Sound_IsPlaying(audio.silentLoop)) {
        gX_Sound_Play_Parameters(audio.silentLoop, (gX_Sound_Parameters){
            .volume = 0.0f,
            .play = true
        });
    }
    if (audio.state.pause_music) {
        gX_Audio_Music_Pause();
        audio.state.pause_music = false;
    }
    if (audio.state.stop_music) {
        gX_Audio_Music_Stop();
        audio.state.stop_music = false;
    }
    
}

// ========================================================================
//opencode: reverb — stereo delay line with feedback
//ai: gX_Audio_Reverb_Process is the device-free core (stereo interleaved
//   float, in place); the raylib mixed-processor adapter below calls it and
//   the unit tests render with it directly.
gX_I32 gX_Audio_Reverb_Process(gX_F32 *samples, gX_U32 frames) {
    if (!samples || !g_audioReverb.enabled || !g_reverbState.inited) return 1;
    gX_F32 *buf = g_reverbState.buf;
    if (!buf) return 1;
    gX_U32 size = REVERB_BUF_SIZE;
    gX_F32 wet = g_audioReverb.wet;
    gX_F32 dry = g_audioReverb.dry;
    gX_F32 feedback = g_audioReverb.roomSize * 0.7f;  // 0..0.7
    //ai: damping — one-pole low-pass on the recirculating tap. The parameter
    //   existed but was never applied, so the tail stayed bright regardless.
    //   damping 0 = transparent, 1 = the tail dies immediately.
    gX_F32 damping = g_audioReverb.damping;
    if (damping < 0.0f) damping = 0.0f;
    if (damping > 1.0f) damping = 1.0f;
    gX_U32 delaySamples = (gX_U32)(g_audioReverb.roomSize * 8000.0f + 500.0f);
    if (delaySamples < 100) delaySamples = 100;
    if (delaySamples > size - 100) delaySamples = size - 100;

    for (gX_U32 i = 0; i < frames; i++) {
        // Left channel
        gX_U32 rIdxL = (g_reverbState.idxL + size - delaySamples) % size;
        gX_F32 delayedL = buf[rIdxL * 2];
        g_reverbState.lpL += (1.0f - damping) * (delayedL - g_reverbState.lpL);
        gX_F32 tapL = g_reverbState.lpL;
        gX_F32 inL = samples[i * 2];
        buf[(g_reverbState.idxL) * 2] = inL + tapL * feedback;
        samples[i * 2] = inL * dry + tapL * wet;
        g_reverbState.idxL = (g_reverbState.idxL + 1) % size;

        // Right channel (slightly different delay for stereo spread)
        gX_U32 rIdxR = (g_reverbState.idxR + size - delaySamples - 117) % size;
        gX_F32 delayedR = buf[rIdxR * 2 + 1];
        g_reverbState.lpR += (1.0f - damping) * (delayedR - g_reverbState.lpR);
        gX_F32 tapR = g_reverbState.lpR;
        gX_F32 inR = samples[i * 2 + 1];
        buf[(g_reverbState.idxR) * 2 + 1] = inR + tapR * feedback;
        samples[i * 2 + 1] = inR * dry + tapR * wet;
        g_reverbState.idxR = (g_reverbState.idxR + 1) % size;
    }
    return 0;
}

//opencode: called by raylib for every audio frame (stereo float interleaved)
static void gX_Audio_Reverb_ProcessAdapter(void *buffer, unsigned int frames) {
    gX_Audio_Reverb_Process((gX_F32*)buffer, (gX_U32)frames);
}

gX_I1 gX_Audio_Reverb_IsAttached(gX_I0) {
    return g_reverbState.processorAttached;
}

gX_I0 gX_Audio_Reverb_Clear(gX_I0) {
    if (g_reverbState.buf) {
        memset(g_reverbState.buf, 0,
            sizeof(gX_F32) * REVERB_BUF_SIZE * 2);
    }
    g_reverbState.idxL = 0;
    g_reverbState.idxR = 0;
    g_reverbState.lpL = 0.0f;
    g_reverbState.lpR = 0.0f;
}

gX_I0 gX_Audio_Reverb_Init(gX_I0) {
    g_audioReverb.enabled = 0;
    g_audioReverb.roomSize = 0.5f;
    g_audioReverb.damping = 0.5f;
    g_audioReverb.wet = 0.3f;
    g_audioReverb.dry = 0.8f;
    g_reverbState.idxL = 0;
    g_reverbState.idxR = 0;
    g_reverbState.lpL = 0.0f;
    g_reverbState.lpR = 0.0f;
    g_reverbState.inited = 0;
    g_reverbState.processorAttached = 0;

#if defined(GAME_BACKEND_RAYLIB)
    //opencode: don't attach until first enable — avoids PulseAudio idle noise
    //AttachAudioMixedProcessor(gX_Audio_Reverb_ProcessAdapter);
#endif
}

gX_I0 gX_Audio_Reverb_Set(gX_F32 roomSize, gX_F32 damping, gX_F32 wet) {
    g_audioReverb.roomSize = (roomSize < 0.0f) ? 0.0f : (roomSize > 1.0f) ? 1.0f : roomSize;
    g_audioReverb.damping = (damping < 0.0f) ? 0.0f : (damping > 1.0f) ? 1.0f : damping;
    g_audioReverb.wet = (wet < 0.0f) ? 0.0f : (wet > 1.0f) ? 1.0f : wet;
    g_audioReverb.dry = 1.0f - wet * 0.5f;
    g_audioReverb.enabled = (wet > 0.01f) ? 1 : 0;
    //opencode: allocate the delay line on first enable (compression P3)
    if (g_audioReverb.enabled && !g_reverbState.inited) {
        if (!g_reverbState.buf) {
            g_reverbState.buf = (gX_F32*)calloc(REVERB_BUF_SIZE * 2, sizeof(gX_F32));
        }
        if (!g_reverbState.buf) {
            gX_TraceLog(gX_LOG_ERROR, "AUDIO: reverb buffer alloc failed (%u bytes)",
                        (gX_U32)(REVERB_BUF_SIZE * 2 * sizeof(gX_F32)));
            g_audioReverb.enabled = 0;
            return;
        }
        //ai: a re-enable after Unload/Init reuses the buffer but starts from
        //   a clean line (the old code reallocated over the pointer).
        gX_Audio_Reverb_Clear();
        g_reverbState.inited = 1;
    }
    //opencode: attach processor on first enable (avoids PulseAudio idle noise when unused)
    //ai: device guard — a headless/test process has no audio device, and
    //   attaching to an uninitialized raylib audio system is undefined.
    if (g_audioReverb.enabled && !g_reverbState.processorAttached) {
#if defined(GAME_BACKEND_RAYLIB)
        if (IsAudioDeviceReady()) {
            AttachAudioMixedProcessor(gX_Audio_Reverb_ProcessAdapter);
            g_reverbState.processorAttached = 1;
        }
#endif
    }
    gX_TraceLog(gX_LOG_NOTICE, "AUDIO: reverb roomSize=%.2f damping=%.2f wet=%.2f",
                g_audioReverb.roomSize, g_audioReverb.damping, g_audioReverb.wet);
}
