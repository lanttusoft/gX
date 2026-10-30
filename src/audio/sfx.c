/**********************************************************************************************
*
*   [gX: src/audio/sfx.c]
*   gX — procedural one-shot sound effects
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


//ai: procedural SFX pool — see sfx.h for the contract. Variant waves are
//   synthesized once into mono float32 at the synth's 48 kHz (raylib converts
//   to the device rate inside LoadSoundFromWave), then loaded GX_AUDIO_SFX_
//   COPIES times per variant into audio.pool (the long-unused pool array).
//   The oscillator state below is a local LCG seeded per variant, so the
//   waves are deterministic run to run.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sfx.h"

#include "../audio.h"
#include "../camera.h"
#include "../main.h"

#include "../common/log.h"

#include "../gxmath/gxmath.h"

#include "../wrappers/wrappers.h"

//opencode: raylib audio device query (same pattern as audio.c)
#if defined(GAME_BACKEND_RAYLIB)
#include "raylib.h"
#endif

//ai: per-variant length in seconds — impact tails stay short so the pool is
//   cheap (GX_AUDIO_SFX_POOL_SIZE buffers) and dense events stay crisp.
static const gX_F32 GX_AUDIO_SFX_LENGTH[GX_AUDIO_SFX_COUNT] = {
    0.22f,  // THUD
    0.11f,  // CLACK
    0.30f,  // METAL
    0.20f,  // ARC
    0.06f   // CLICK
};

typedef struct gX_AudioSFX_State {
    gX_I1   ready;
    gX_F64  minInterval;
    gX_F64  lastPlay;
    gX_U32  nextCopy[GX_AUDIO_SFX_COUNT];
} gX_AudioSFX_State;

static gX_AudioSFX_State g_sfx;

//ai: deterministic white noise for the transient layers.
static gX_F32 gX_AudioSFX_Noise(gX_U32 *state) {
    *state = *state * 1103515245u + 12345u;
    return (gX_F32)(*state >> 16) / 32768.0f - 1.0f;
}

static gX_F32 gX_AudioSFX_Decay(gX_F32 t, gX_F32 tau) {
    if (tau <= 0.0f) return 0.0f;
    gX_F32 e = expf(-t / tau);
    return (e > 1.0f) ? 1.0f : e;
}

//ai: pure wave builder — switch on the variant, mix simple synthesis layers,
//   then normalize to a fixed peak so levels are predictable across variants.
gX_I0 gX_AudioSFX_BuildWave(gX_I32 variant, gX_F32 *out, gX_U32 frames, gX_U32 sampleRate) {
    if (!out || frames == 0 || sampleRate == 0) return;
    if (variant < 0 || variant >= GX_AUDIO_SFX_COUNT) variant = GX_AUDIO_SFX_THUD;

    gX_F32 invRate = 1.0f / (gX_F32)sampleRate;
    gX_F32 twoPi = 2.0f * gX_PI;
    gX_U32 seed = 0x9E3779B9u ^ ((gX_U32)(variant + 1) * 2654435761u);
    gX_F32 lp = 0.0f;
    gX_F32 peak = 0.0f;

    for (gX_U32 i = 0; i < frames; i++) {
        gX_F32 t = (gX_F32)i * invRate;
        gX_F32 n = gX_AudioSFX_Noise(&seed);
        gX_F32 s = 0.0f;

        switch (variant) {
            case GX_AUDIO_SFX_THUD: {
                //ai: falling sine (150 → 40 Hz) + heavily low-passed noise body
                gX_F32 f0 = 150.0f;
                gX_F32 slope = 1400.0f;
                gX_F32 ft = (t < 0.08f) ? t : 0.08f;
                s = sinf(twoPi * (f0 * ft - 0.5f * slope * ft * ft))
                    * gX_AudioSFX_Decay(t, 0.060f);
                lp += 0.25f * (n - lp);
                s += lp * gX_AudioSFX_Decay(t, 0.030f) * 0.45f;
                break;
            }
            case GX_AUDIO_SFX_CLACK: {
                //ai: noise transient + two fast resonances
                s = n * gX_AudioSFX_Decay(t, 0.012f) * 0.9f;
                s += sinf(twoPi * 1450.0f * t) * gX_AudioSFX_Decay(t, 0.020f) * 0.50f;
                s += sinf(twoPi * 2870.0f * t) * gX_AudioSFX_Decay(t, 0.014f) * 0.30f;
                break;
            }
            case GX_AUDIO_SFX_METAL: {
                //ai: inharmonic partials with per-partial decay + strike noise
                static const gX_F32 partial[5] = {418.0f, 963.0f, 1547.0f, 2318.0f, 3096.0f};
                for (gX_I32 k = 0; k < 5; k++) {
                    gX_F32 tau = 0.115f - (gX_F32)k * 0.016f;
                    gX_F32 amp = 0.8f / (1.0f + (gX_F32)k);
                    s += sinf(twoPi * partial[k] * t) * gX_AudioSFX_Decay(t, tau) * amp;
                }
                s += n * gX_AudioSFX_Decay(t, 0.006f) * 0.5f;
                break;
            }
            case GX_AUDIO_SFX_ARC: {
                //ai: square buzz + flutter gate + rising whine
                gX_F32 buzz = (sinf(twoPi * 110.0f * t) >= 0.0f) ? 1.0f : -1.0f;
                gX_F32 gate = 0.55f + 0.45f * sinf(twoPi * 47.0f * t);
                s = buzz * gate * gX_AudioSFX_Decay(t, 0.070f) * 0.40f;
                s += n * gX_AudioSFX_Decay(t, 0.050f) * 0.20f;
                s += sinf(twoPi * (600.0f + 1200.0f * t) * t)
                    * gX_AudioSFX_Decay(t, 0.080f) * 0.25f;
                break;
            }
            case GX_AUDIO_SFX_CLICK: {
                //ai: tool feedback — a 1.8 kHz tick + 3 ms noise edge
                s = sinf(twoPi * 1850.0f * t) * gX_AudioSFX_Decay(t, 0.006f);
                s += n * gX_AudioSFX_Decay(t, 0.0025f) * 0.6f;
                break;
            }
            default:
                break;
        }
        out[i] = s;
        gX_F32 a = fabsf(s);
        if (a > peak) peak = a;
    }

    //ai: normalize to 0.85 peak (silent variants stay silent)
    if (peak > 1e-4f) {
        gX_F32 gain = 0.85f / peak;
        for (gX_U32 i = 0; i < frames; i++) out[i] *= gain;
    }
}

static gX_U32 gX_AudioSFX_Frames(gX_I32 variant, gX_U32 sampleRate) {
    gX_F32 seconds = (variant >= 0 && variant < GX_AUDIO_SFX_COUNT)
        ? GX_AUDIO_SFX_LENGTH[variant] : GX_AUDIO_SFX_LENGTH[0];
    gX_U32 frames = (gX_U32)(seconds * (gX_F32)sampleRate);
    if (frames < 16) frames = 16;
    return frames;
}

gX_U32 gX_AudioSFX_WaveFrames(gX_I32 variant, gX_U32 sampleRate) {
    if (sampleRate == 0) sampleRate = 48000;
    return gX_AudioSFX_Frames(variant, sampleRate);
}

gX_I32 gX_AudioSFX_Init(gX_I0) {
    if (g_sfx.ready) return 0;
#if defined(GAME_BACKEND_RAYLIB)
    if (!IsAudioDeviceReady()) {
        gX_TraceLog(gX_LOG_WARNING, "AUDIO SFX: no audio device — pool disabled");
        return 1;
    }
#else
    return 1;
#endif

    //ai: longest variant at 48 kHz — reused as the synthesis scratch buffer
    gX_U32 maxFrames = 0;
    for (gX_I32 v = 0; v < GX_AUDIO_SFX_COUNT; v++) {
        gX_U32 frames = gX_AudioSFX_Frames(v, 48000);
        if (frames > maxFrames) maxFrames = frames;
    }
    gX_F32 *wave = (gX_F32 *)malloc((gX_U32)sizeof(gX_F32) * maxFrames);
    if (!wave) {
        gX_TraceLog(gX_LOG_ERROR, "AUDIO SFX: wave scratch alloc failed (%u frames)", maxFrames);
        return 1;
    }

    gX_U32 loaded = 0;
    for (gX_I32 v = 0; v < GX_AUDIO_SFX_COUNT; v++) {
        gX_U32 frames = gX_AudioSFX_Frames(v, 48000);
        gX_AudioSFX_BuildWave(v, wave, frames, 48000);
        gX_Wave w = {frames, 48000, 32, 1, wave};
        for (gX_I32 copy = 0; copy < GX_AUDIO_SFX_COPIES; copy++) {
            gX_U32 slot = (gX_U32)(v * GX_AUDIO_SFX_COPIES + copy);
            audio.pool[slot].sound = gX_Sound_LoadWave(w);
            if (audio.pool[slot].sound.frameCount == 0) {
                gX_TraceLog(gX_LOG_ERROR, "AUDIO SFX: variant %d copy %d failed to load", v, copy);
            } else {
                loaded++;
            }
        }
    }
    free(wave);

    g_sfx.minInterval = 0.02;
    g_sfx.lastPlay = 0.0;
    for (gX_I32 v = 0; v < GX_AUDIO_SFX_COUNT; v++) g_sfx.nextCopy[v] = 0;
    g_sfx.ready = (loaded > 0) ? 1 : 0;
    gX_TraceLog(gX_LOG_NOTICE, "AUDIO SFX: pool ready (%u/%d voices, %d variants)",
        loaded, GX_AUDIO_SFX_POOL_SIZE, GX_AUDIO_SFX_COUNT);
    return g_sfx.ready ? 0 : 1;
}

gX_I32 gX_AudioSFX_Unload(gX_I0) {
    for (gX_U32 slot = 0; slot < GX_AUDIO_SFX_POOL_SIZE; slot++) {
        if (audio.pool[slot].sound.frameCount > 0) {
            gX_Sound_Unload(audio.pool[slot].sound);
        }
        audio.pool[slot].sound = (gX_Sound){0};
    }
    g_sfx.ready = 0;
    return 0;
}

gX_I1 gX_AudioSFX_IsReady(gX_I0) {
    return g_sfx.ready;
}

gX_I0 gX_AudioSFX_SetMinInterval(gX_F32 seconds) {
    if (seconds < 0.0f) seconds = 0.0f;
    if (seconds > 1.0f) seconds = 1.0f;
    g_sfx.minInterval = (gX_F64)seconds;
}

gX_F32 gX_AudioSFX_GetMinInterval(gX_I0) {
    return (gX_F32)g_sfx.minInterval;
}

gX_I32 gX_AudioSFX_Play(gX_I32 variant, gX_F32v3 position, gX_F32 volume, gX_F32 pitch, gX_F32 maxDist) {
    if (!g_sfx.ready || variant < 0 || variant >= GX_AUDIO_SFX_COUNT) return -1;
    if (!maindata.backendAudioLoaded || audio.muted) return -1;
    if (volume <= 0.001f) return -1;

    //ai: distance cull — the 3D update would zero the attenuation anyway;
    //   dropping early keeps the voice slots for audible events.
    if (maxDist < 1.0f) maxDist = 1.0f;
    {
        gX_F32v3 d = gX_F32v3_Sub(position, camera.camera3D.position);
        gX_F32 dist = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        if (dist >= maxDist) return -1;
    }

    //ai: rate limit — dense contact showers share one spacing window.
    gX_F64 now = gX_Time_Get();
    if ((now - g_sfx.lastPlay) < g_sfx.minInterval) return -1;

    if (pitch < 0.25f) pitch = 0.25f;
    if (pitch > 4.0f) pitch = 4.0f;

    gX_U32 copy = g_sfx.nextCopy[variant]++ % GX_AUDIO_SFX_COPIES;
    gX_U32 slot = (gX_U32)(variant * GX_AUDIO_SFX_COPIES + (gX_I32)copy);
    gX_Sound snd = audio.pool[slot].sound;
    if (snd.frameCount == 0) return -1;

    g_sfx.lastPlay = now;
    return gX_Audio_Play3DEx(snd, position, volume, pitch, maxDist);
}
