/**********************************************************************************************
*
*   [gX: src/audio/sfx_tests.c]
*   gX — procedural SFX pool / mixer / reverb tests (CPU only)
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


//ai: no audio device is needed — the SFX wave synthesis, the mixer bus math
//   and the reverb core are pure CPU paths. The play-side tests pin the
//   device guards (a headless process must not crash and must not pretend to
//   play).
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sfx.h"

#include "../audio.h"
#include "../settings.h"

#include "../common/log.h"
#include "../gxmath/gxmath.h"

#define SFX_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            gX_TraceLog(gX_LOG_ERROR, "SFX_TEST FAIL: %s (line %d)", msg, __LINE__); \
            errors++; \
        } \
    } while(0)

#define SFX_TEST_RATE 48000
#define SFX_TEST_BUILD_FRAMES 16384
static gX_F32 sfxWaveA[SFX_TEST_BUILD_FRAMES];
static gX_F32 sfxWaveB[SFX_TEST_BUILD_FRAMES];

static gX_F32 sfx_test_peak(const gX_F32 *samples, gX_U32 frames) {
    gX_F32 peak = 0.0f;
    for (gX_U32 i = 0; i < frames; i++) {
        gX_F32 a = fabsf(samples[i]);
        if (a > peak) peak = a;
    }
    return peak;
}

static gX_F64 sfx_test_energy(const gX_F32 *samples, gX_U32 from, gX_U32 to) {
    gX_F64 sum = 0.0;
    if (to > SFX_TEST_BUILD_FRAMES) to = SFX_TEST_BUILD_FRAMES;
    for (gX_U32 i = from; i < to; i++) sum += (gX_F64)samples[i] * (gX_F64)samples[i];
    return sum;
}

//ai: every variant must be audible, bounded, deterministic (a rebuilt wave is
//   byte-identical — captures replay the same mix) and decaying (the tail
//   window carries less energy than the head window).
static gX_I32 sfx_test_build_wave(void) {
    gX_I32 errors = 0;
    for (gX_I32 v = 0; v < GX_AUDIO_SFX_COUNT; v++) {
        gX_U32 n = gX_AudioSFX_WaveFrames(v, SFX_TEST_RATE);
        if (n > SFX_TEST_BUILD_FRAMES) n = SFX_TEST_BUILD_FRAMES;
        memset(sfxWaveA, 0, sizeof(gX_F32) * SFX_TEST_BUILD_FRAMES);
        gX_AudioSFX_BuildWave(v, sfxWaveA, n, SFX_TEST_RATE);
        gX_AudioSFX_BuildWave(v, sfxWaveB, n, SFX_TEST_RATE);

        gX_F32 peak = sfx_test_peak(sfxWaveA, n);
        SFX_TEST_ASSERT(peak > 0.5f, "variant is audible");
        SFX_TEST_ASSERT(peak <= 0.86f, "variant peak is normalized");
        SFX_TEST_ASSERT(memcmp(sfxWaveA, sfxWaveB, sizeof(gX_F32) * n) == 0,
            "variant build is deterministic");
        //ai: decay — the last fifth of the variant's own window must carry
        //   less energy than the first fifth.
        gX_U32 fifth = n / 5;
        if (fifth < 16) fifth = n / 2;
        gX_F64 head = sfx_test_energy(sfxWaveA, 0, fifth);
        gX_F64 tail = sfx_test_energy(sfxWaveA, n - fifth, n);
        SFX_TEST_ASSERT(tail < head, "variant decays toward the tail");
    }

    //ai: argument guards must not crash
    gX_AudioSFX_BuildWave(-1, sfxWaveA, 128, SFX_TEST_RATE);
    gX_AudioSFX_BuildWave(GX_AUDIO_SFX_COUNT, sfxWaveA, 128, SFX_TEST_RATE);
    gX_AudioSFX_BuildWave(0, NULL, 128, SFX_TEST_RATE);
    gX_AudioSFX_BuildWave(0, sfxWaveA, 0, SFX_TEST_RATE);
    gX_AudioSFX_BuildWave(0, sfxWaveA, 128, 0);
    return errors;
}

//ai: headless guards — without a device the pool reports not-ready and Play
//   must refuse (never touch raylib's audio buffers).
static gX_I32 sfx_test_play_guards(void) {
    gX_I32 errors = 0;
    if (gX_AudioSFX_IsReady()) {
        //ai: a device is present (an interactive run of --test-only) —
        //   exercise the real pool instead of the refusal path.
        gX_I32 slot = gX_AudioSFX_Play(GX_AUDIO_SFX_CLICK,
            (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 10.0f);
        SFX_TEST_ASSERT(slot >= 0, "device run plays a click");
        return errors;
    }
    SFX_TEST_ASSERT(gX_AudioSFX_Play(GX_AUDIO_SFX_THUD,
        (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 10.0f) < 0,
        "headless play is refused");
    SFX_TEST_ASSERT(gX_AudioSFX_Play(-1,
        (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 10.0f) < 0,
        "invalid variant is refused");
    SFX_TEST_ASSERT(gX_AudioSFX_Play(GX_AUDIO_SFX_COUNT,
        (gX_F32v3){0.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 10.0f) < 0,
        "out-of-range variant is refused");
    return errors;
}

//ai: mixer buses — clamps, gain defaults, and the bus-aware sound play
//   refusing a zeroed sound without a device.
static gX_I32 sfx_test_mixer_bus(void) {
    gX_I32 errors = 0;
    gX_F32 saved[GX_AUDIO_BUS_COUNT];
    for (gX_I32 bus = 0; bus < GX_AUDIO_BUS_COUNT; bus++) {
        saved[bus] = gX_Audio_BusGetVolume(bus);
    }

    gX_Audio_BusSetVolume(GX_AUDIO_BUS_SFX, 2.0f);
    SFX_TEST_ASSERT(gX_Audio_BusGetVolume(GX_AUDIO_BUS_SFX) == 1.0f,
        "sfx bus clamps high");
    SFX_TEST_ASSERT(gX_Audio_BusGain(GX_AUDIO_BUS_SFX) == 1.0f, "gain follows the bus");
    gX_Audio_BusSetVolume(GX_AUDIO_BUS_SFX, -3.0f);
    SFX_TEST_ASSERT(gX_Audio_BusGetVolume(GX_AUDIO_BUS_SFX) == 0.0f,
        "sfx bus clamps low");
    gX_Audio_BusSetVolume(GX_AUDIO_BUS_SFX, 0.4f);
    SFX_TEST_ASSERT(fabsf(gX_Audio_BusGetVolume(GX_AUDIO_BUS_SFX) - 0.4f) < 1e-6f,
        "sfx bus holds a set value");

    gX_Audio_BusSetVolume(GX_AUDIO_BUS_MASTER, 3.0f);
    SFX_TEST_ASSERT(gX_Audio_BusGetVolume(GX_AUDIO_BUS_MASTER) == 2.0f,
        "master bus clamps at 2.0");
    gX_Audio_BusSetVolume(GX_AUDIO_BUS_MASTER, -1.0f);
    SFX_TEST_ASSERT(gX_Audio_BusGetVolume(GX_AUDIO_BUS_MASTER) == 0.0f,
        "master bus clamps at 0");

    SFX_TEST_ASSERT(gX_Audio_BusGain(99) == 1.0f, "invalid bus gain is neutral");
    SFX_TEST_ASSERT(gX_Audio_BusGetVolume(-1) == 0.0f, "invalid bus volume is zero");

    gX_Sound none = (gX_Sound){0};
    gX_Audio_BusSetVolume(GX_AUDIO_BUS_SFX, 1.0f);
    SFX_TEST_ASSERT(gX_Audio_Sound_PlayBus(NULL, (gX_Sound_Parameters){0}) == 0,
        "NULL sound refused");
    SFX_TEST_ASSERT(gX_Audio_Sound_PlayBus(&none, (gX_Sound_Parameters){0}) == 0,
        "zero sound refused");

    for (gX_I32 bus = 0; bus < GX_AUDIO_BUS_COUNT; bus++) {
        gX_Audio_BusSetVolume(bus, saved[bus]);
    }
    return errors;
}

//ai: reverb core — dry passthrough when disabled, an audible tail when
//   enabled, and the damping parameter actually absorbing the tail (the
//   parameter was stored but never applied before this slice).
static gX_I32 sfx_test_reverb_damping(void) {
    gX_I32 errors = 0;
    enum { FRAMES = 16000, DELAY = 5300 };  // roomSize 0.6 -> 5300 samples
    static gX_F32 buf[FRAMES * 2];

    //ai: disabled reverb is a passthrough no-op (and must not touch the mix)
    gX_Audio_Reverb_Init();
    for (gX_U32 i = 0; i < FRAMES * 2; i++) buf[i] = 0.25f;
    gX_Audio_Reverb_Process(buf, FRAMES);
    gX_I1 unchanged = 1;
    for (gX_U32 i = 0; i < FRAMES * 2; i++) {
        if (buf[i] != 0.25f) { unchanged = 0; break; }
    }
    SFX_TEST_ASSERT(unchanged, "disabled reverb leaves the buffer untouched");

    //ai: impulse through the delay line, damping 0 (bright echo)
    static gX_F32 dryTail[FRAMES];
    memset(buf, 0, sizeof(buf));
    buf[0] = 1.0f;
    buf[1] = 1.0f;
    gX_Audio_Reverb_Set(0.6f, 0.0f, 0.8f);
    gX_Audio_Reverb_Clear();
    gX_Audio_Reverb_Process(buf, FRAMES);
    for (gX_U32 i = 0; i < FRAMES; i++) dryTail[i] = buf[i * 2];
    //ai: the first echo lands at roomSize*8000+500 samples; measure around it
    gX_F64 bright = sfx_test_energy(dryTail, DELAY - 64, DELAY + 700);

    //ai: same impulse with heavy damping — the recirculating tap is absorbed
    memset(buf, 0, sizeof(buf));
    buf[0] = 1.0f;
    buf[1] = 1.0f;
    gX_Audio_Reverb_Set(0.6f, 0.95f, 0.8f);
    gX_Audio_Reverb_Clear();
    gX_Audio_Reverb_Process(buf, FRAMES);
    for (gX_U32 i = 0; i < FRAMES; i++) dryTail[i] = buf[i * 2];
    gX_F64 damped = sfx_test_energy(dryTail, DELAY - 64, DELAY + 700);
    SFX_TEST_ASSERT(bright > 1e-3, "bright reverb produces an echo");
    SFX_TEST_ASSERT(damped < bright * 0.25, "damping absorbs the tail");
    SFX_TEST_ASSERT(damped > 0.0, "damped reverb still passes signal");

    //ai: leave the reverb disabled so later tests are unaffected
    gX_Audio_Reverb_Set(0.5f, 0.5f, 0.0f);
    return errors;
}

//ai: settings → mixer live sync — a volume field edit (pause/Settings
//   sliders) must reach the audio bus without an Apply press.
static gX_I32 sfx_test_settings_sync(void) {
    gX_I32 errors = 0;
    gX_Settings_Init();
    gX_Settings_Audio_Apply();
    SFX_TEST_ASSERT(fabsf(gX_Audio_BusGetVolume(GX_AUDIO_BUS_MUSIC) - 1.0f) < 1e-6f,
        "music bus starts open");

    settings.masterVolume = 1.4f;
    settings.musicVolume = 0.35f;
    settings.sfxVolume = 0.0f;
    settings.synthVolume = 0.6f;
    gX_Settings_Audio_Apply();
    SFX_TEST_ASSERT(fabsf(gX_Audio_BusGetVolume(GX_AUDIO_BUS_MASTER) - 1.4f) < 1e-6f,
        "master field syncs live");
    SFX_TEST_ASSERT(fabsf(gX_Audio_BusGetVolume(GX_AUDIO_BUS_MUSIC) - 0.35f) < 1e-6f,
        "music field syncs live");
    SFX_TEST_ASSERT(gX_Audio_BusGetVolume(GX_AUDIO_BUS_SFX) == 0.0f,
        "effects field syncs live");
    SFX_TEST_ASSERT(fabsf(gX_Audio_BusGetVolume(GX_AUDIO_BUS_SYNTH) - 0.6f) < 1e-6f,
        "synth field syncs live");

    //ai: restore the defaults for the suites that follow.
    settings.masterVolume = 1.0f;
    settings.musicVolume = 1.0f;
    settings.sfxVolume = 1.0f;
    settings.synthVolume = 1.0f;
    gX_Settings_Audio_Apply();
    return errors;
}

gX_I32 gX_Tests_AudioSFX(gX_I0) {
    gX_I32 errors = 0;
    errors += sfx_test_build_wave();
    errors += sfx_test_play_guards();
    errors += sfx_test_mixer_bus();
    errors += sfx_test_settings_sync();
    errors += sfx_test_reverb_damping();
    gX_TraceLog(gX_LOG_INFO, "SFX_TESTS: %d errors", errors);
    return errors;
}
