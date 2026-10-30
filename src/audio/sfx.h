/**********************************************************************************************
*
*   [gX: src/audio/sfx.h]
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


//ai: a small palette of procedurally generated impact/feedback sounds with a
//   multi-copy voice pool, so a burst of effects (physics contacts, tool
//   fire) overlaps instead of retriggering one buffer. No assets: every
//   variant is synthesized at audio-init (gX_AudioSFX_BuildWave is pure, so
//   the unit tests render it without an audio device). Playback is 3D
//   (position/attenuation/pan ride gX_Audio_Play3DEx), bus-scaled by
//   GX_AUDIO_BUS_SFX and rate-limited to keep dense contact showers from
//   turning into machine-gun fire.
#ifndef AUDIO_SFX_H
#define AUDIO_SFX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

//ai: variants (indices into the pool + BuildWave)
#define GX_AUDIO_SFX_THUD   0  // soft/low body hit (wood, flesh, water entry)
#define GX_AUDIO_SFX_CLACK  1  // hard short hit (box, stone, plastic)
#define GX_AUDIO_SFX_METAL  2  // resonant metal clang
#define GX_AUDIO_SFX_ARC    3  // charged contact (electric zap)
#define GX_AUDIO_SFX_CLICK  4  // tool/UI feedback tick (toolgun fire)
#define GX_AUDIO_SFX_COUNT  5

//ai: concurrent copies per variant — raylib sounds are single-instance, so
//   overlap needs distinct buffers per voice.
#define GX_AUDIO_SFX_COPIES 4
#define GX_AUDIO_SFX_POOL_SIZE (GX_AUDIO_SFX_COUNT * GX_AUDIO_SFX_COPIES)

//ai: build the stereo-ready mono float32 wave for a variant (deterministic;
//   0..1 s at the given rate). Pure — the unit tests call it directly.
gX_I0 gX_AudioSFX_BuildWave(gX_I32 variant, gX_F32 *out, gX_U32 frames, gX_U32 sampleRate);
//ai: the per-variant length (frames) the pool loads; the tests size their
//   scratch buffers from the same source.
gX_U32 gX_AudioSFX_WaveFrames(gX_I32 variant, gX_U32 sampleRate);

//ai: pool lifetime — gX_Audio_Load/Unload own it; a no-op without a device.
gX_I32 gX_AudioSFX_Init(gX_I0);
gX_I32 gX_AudioSFX_Unload(gX_I0);
gX_I1 gX_AudioSFX_IsReady(gX_I0);

//ai: play one variant at a world position. pitch 1.0 = natural, 0.5..2.0 is
//   the useful range. Returns a 3D source slot (>=0) or <0 when dropped
//   (muted, rate-limited, out of range, pool empty).
gX_I32 gX_AudioSFX_Play(gX_I32 variant, gX_F32v3 position, gX_F32 volume, gX_F32 pitch, gX_F32 maxDist);

//ai: global minimum spacing between two pool plays (default 0.02 s).
gX_I0  gX_AudioSFX_SetMinInterval(gX_F32 seconds);
gX_F32 gX_AudioSFX_GetMinInterval(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //AUDIO_SFX_H
