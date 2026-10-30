/**********************************************************************************************
*
*   [gX: src/renderer/gfx/weather.h]
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


#ifndef RENDERER_GFX_WEATHER_H
#define RENDERER_GFX_WEATHER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "../../gxmath/matrix.h"
#include "../../gxmath/random.h"

//opencode: CPU-simulated falling snow — deterministic LCG sim separated
//   from rendering. GL33-safe by design: plain VBO point sprites with a
//   GLSL 330 shader (no compute, no SSBO), unlike the GPU particle path
//   which needs GL 4.3+. Bounded counts, camera-relative bounds, floor
//   collision with respawn, distance LOD. Accumulation stays in the
//   snow-decal fixture; this layer is the falling precipitation.
//
//opencode: CPU rain streaks share the sim — fast drops drawn as GL line
//   segments along the wind-slanted fall vector (points cannot stretch).
//   Floor collision respawns; splashes are deferred (see backlog).
//
//opencode: CPU fog sprites share the sim — slow-drifting camera-facing
//   billboards, back-to-front sorted, depth-tested, tinted from the scene
//   fog color. True depth-texture soft particles are deferred; distance
//   and height fades bound their influence instead.
#define gX_WEATHER_SNOW_MAX 512
#define gX_WEATHER_RAIN_MAX 512
#define gX_WEATHER_FOGSPRITE_MAX 48

typedef struct gX_RendererGFX_Weather {
    gX_I1 enabled;
    gX_U32 seed;
    gX_Random32 rng;   //opencode: per-weather PCG stream (no shared rand())
    gX_I32 targetCount;
    gX_I32 activeCount;
    gX_F32 windX;
    gX_F32 windZ;
    gX_F32 fallSpeed;
    //ai: bounded env-only snowfall profile; all-zero/one defaults preserve
    //    the pre-profile simulation and point upload exactly.
    gX_F32 gustStrength;       // 0..1.5, default 0
    gX_F32 turbulenceStrength; // 0..2.0, default 0
    gX_F32 flakeSizeScale;     // 0.25..2.0, default 1
    gX_F32 flakeAlphaScale;    // 0..1.0, default 1
    gX_F32 floorY;
    gX_F32 halfExtent;
    gX_F32 spawnTop;
    gX_F32 timeAccum;
    gX_F32 *posX;
    gX_F32 *posY;
    gX_F32 *posZ;
    gX_F32 *flakeSize;
    gX_F32 *flakeAlpha;
    gX_F32 *flakePhase;
    gX_F32 *flakeSpeed;
    gX_F32 *snowUpload;
    gX_U32 vbo;
    gX_U32 vao;
    gX_U32 renderProg;
    gX_I1 loaded;
    gX_I1 locsCached;
    gX_I32 uVP;
    gX_I32 uPointScale;
    //opencode: weather lighting tint (ambient + sun) for the sprite shaders.
    gX_I32 uLightTint;
    gX_U32 rainSeed;
    gX_U32 rainRng;
    gX_I32 rainTarget;
    gX_I32 rainActive;
    gX_F32 rainWindX;
    gX_F32 rainWindZ;
    gX_F32 rainFall;
    gX_F32 rainLength;
    gX_F32 *rainPosX;
    gX_F32 *rainPosY;
    gX_F32 *rainPosZ;
    gX_F32 *rainAlpha;
    gX_F32 *rainUpload;
    gX_U32 rainVbo;
    gX_U32 rainVao;
    gX_U32 rainProg;
    gX_I1 rainLoaded;
    gX_I1 rainLocsCached;
    gX_I32 rainUvp;
    gX_I32 rainUlightTint;
    gX_U32 fogSeed;
    gX_U32 fogRng;
    gX_I32 fogTarget;
    gX_I32 fogActive;
    gX_F32 fogWindX;
    gX_F32 fogWindZ;
    gX_F32 fogOpacity;
    gX_F32 fogScale;
    gX_F32 *fogPosX;
    gX_F32 *fogPosY;
    gX_F32 *fogPosZ;
    gX_F32 *fogAlpha;
    gX_F32 *fogSize;
    gX_F32 *fogUpload;
    gX_U32 fogVbo;
    gX_U32 fogVao;
    gX_U32 fogProg;
    gX_I1 fogLoaded;
    gX_I1 fogLocsCached;
    gX_I32 fogUvp;
    gX_I32 fogUtint;
} gX_RendererGFX_Weather;

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: weather lifecycle — init defaults, per-frame CPU sim, point draw.
gX_I0 gX_RendererGFX_Weather_Init(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Weather_Unload(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Weather_Update(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Weather_Draw(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_WEATHER_H
