/**********************************************************************************************
*
*   [gX: src/renderer/gfx/weather.c]
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


//opencode: CPU falling snow — deterministic LCG sim, GL33 point sprites.
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../gfx.h"
#include "weather.h"
#include "boilerplate.h"
#include "../renderer.h"

#include "../../main.h"
#include "../../camera.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_state.h"
#include "../../rhi/rhi_buf.h"
#include "../../rhi/gl_state.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/profiler.h"
#include "../../common/path.h"

#include "../../gxmath/matrix.h"
#include "../../gxmath/gxmath.h"

//opencode: deterministic LCG — sim randomness without rand() pollution.
static gX_F32 Weather_NextRandom(gX_RendererGFX_Weather *weather)
{
    return gX_Random32_F32(&weather->rng);
}

//opencode: read shader source from file into heap-allocated buffer.
static gX_I8 *Weather_ReadFile(const gX_I8 *path, gX_I64 *outLen)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { gX_TraceLog_GFX(gX_LOG_ERROR, "Shader not found: %s", path); return NULL; }
    fseek(fp, 0, SEEK_END);
    gX_I64 len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    gX_I8 *src = (gX_I8*)malloc((size_t)(len + 1));
    if (!src) { fclose(fp); return NULL; }
    size_t nread = fread(src, 1, (size_t)len, fp);
    src[nread] = '\0';
    fclose(fp);
    if (outLen) *outLen = len;
    return src;
}

//ai: retain the authored world floor while adapting the visible spawning band to eye height.
static gX_I0 Weather_VerticalBand(gX_RendererGFX_Weather *weather, gX_F32 *Bottom, gX_F32 *Top)
{
    gX_F32 Height = weather->spawnTop - weather->floorY;
    if (Height <= 0.0f) Height = 1.0f;
    *Bottom = fmaxf(weather->floorY, camera.camera3D.position.y - Height * 0.5f);
    *Top = fmaxf(weather->floorY + Height, camera.camera3D.position.y + Height * 0.5f);
}

//ai: populate the current spawning band; collision respawns start above its ceiling.
static gX_I0 Weather_RespawnFlake(gX_RendererGFX_Weather *weather, gX_U32 index, gX_I1 initial, gX_F32 centerX, gX_F32 centerZ)
{
    gX_F32 pickOne = Weather_NextRandom(weather);
    gX_F32 pickTwo = Weather_NextRandom(weather);
    gX_F32 pickThree = Weather_NextRandom(weather);
    gX_F32 pickFour = Weather_NextRandom(weather);
    gX_F32 pickFive = Weather_NextRandom(weather);
    gX_F32 pickSix = Weather_NextRandom(weather);
    weather->posX[index] = centerX + (pickOne * 2.0f - 1.0f) * weather->halfExtent;
    weather->posZ[index] = centerZ + (pickTwo * 2.0f - 1.0f) * weather->halfExtent;
    gX_F32 Bottom, Top;
    Weather_VerticalBand(weather, &Bottom, &Top);
    if (initial) weather->posY[index] = Bottom + pickThree * (Top - Bottom);
    else weather->posY[index] = Top + pickThree * 1.0f;
    weather->flakeSize[index] = 2.5f + pickFour * 3.5f;
    weather->flakeAlpha[index] = 0.55f + pickFive * 0.40f;
    weather->flakePhase[index] = pickSix * 6.2831853f;
    weather->flakeSpeed[index] = 0.70f + pickFour * 0.60f;
    return;
}

//ai: rain uses the same eye-adaptive spawning band without moving its world floor.
static gX_I0 Weather_RespawnRain(gX_RendererGFX_Weather *weather, gX_U32 index, gX_I1 initial, gX_F32 centerX, gX_F32 centerZ)
{
    weather->rainRng = weather->rainRng * 1664525u + 1013904223u;
    gX_F32 pickOne = (gX_F32)(weather->rainRng >> 8) * (1.0f / 16777216.0f);
    weather->rainRng = weather->rainRng * 1664525u + 1013904223u;
    gX_F32 pickTwo = (gX_F32)(weather->rainRng >> 8) * (1.0f / 16777216.0f);
    weather->rainRng = weather->rainRng * 1664525u + 1013904223u;
    gX_F32 pickThree = (gX_F32)(weather->rainRng >> 8) * (1.0f / 16777216.0f);
    weather->rainRng = weather->rainRng * 1664525u + 1013904223u;
    gX_F32 pickFour = (gX_F32)(weather->rainRng >> 8) * (1.0f / 16777216.0f);
    weather->rainPosX[index] = centerX + (pickOne * 2.0f - 1.0f) * weather->halfExtent;
    weather->rainPosZ[index] = centerZ + (pickTwo * 2.0f - 1.0f) * weather->halfExtent;
    gX_F32 Bottom, Top;
    Weather_VerticalBand(weather, &Bottom, &Top);
    if (initial) weather->rainPosY[index] = Bottom + pickThree * (Top - Bottom);
    else weather->rainPosY[index] = Top + pickThree * 1.0f;
    weather->rainAlpha[index] = 0.25f + pickFour * 0.20f;
    return;
}

//opencode: read the deterministic rain fixture hooks.
static gX_I0 Weather_ApplyRainEnv(gX_RendererGFX_Weather *weather)
{
    const gX_I8 *seedHook = getenv("GX_RAIN_SEED");
    const gX_I8 *rateHook = getenv("GX_RAIN_RATE");
    const gX_I8 *windHook = getenv("GX_RAIN_WIND");
    const gX_I8 *windZHook = getenv("GX_RAIN_WINDZ");
    const gX_I8 *speedHook = getenv("GX_RAIN_SPEED");
    gX_U32 seed = seedHook ? (gX_U32)atoi((const char *)seedHook) : 7331u;
    gX_I32 rate = rateHook ? (gX_I32)atoi((const char *)rateHook) : 384;
    gX_F32 windX = windHook ? (gX_F32)atof((const char *)windHook) : 1.20f;
    gX_F32 windZ = windZHook ? (gX_F32)atof((const char *)windZHook) : 0.40f;
    gX_F32 speed = speedHook ? (gX_F32)atof((const char *)speedHook) : 1.0f;
    if (rate < 0) rate = 0;
    if (rate > (gX_I32)gX_WEATHER_RAIN_MAX) rate = (gX_I32)gX_WEATHER_RAIN_MAX;
    if (speed < 0.0f) speed = 0.0f;
    if (speed > 4.0f) speed = 4.0f;
    if (seed != weather->rainSeed)
    {
        weather->rainSeed = seed;
        weather->rainRng = seed;
        for (gX_U32 i = 0; i < gX_WEATHER_RAIN_MAX; i++) Weather_RespawnRain(weather, i, 1, 0.0f, 0.0f);
    }
    weather->rainTarget = rate;
    weather->rainWindX = windX;
    weather->rainWindZ = windZ;
    weather->rainFall = 7.0f * speed;
    return;
}

//opencode: place one fog sprite — slow bank, low over the floor.
//ai: fog sprites retain their height spread around the eye, independent of precipitation ground.
static gX_I0 Weather_RespawnFog(gX_RendererGFX_Weather *weather, gX_U32 index, gX_I1 initial, gX_F32 centerX, gX_F32 centerZ)
{
    weather->fogRng = weather->fogRng * 1664525u + 1013904223u;
    gX_F32 pickOne = (gX_F32)(weather->fogRng >> 8) * (1.0f / 16777216.0f);
    weather->fogRng = weather->fogRng * 1664525u + 1013904223u;
    gX_F32 pickTwo = (gX_F32)(weather->fogRng >> 8) * (1.0f / 16777216.0f);
    weather->fogRng = weather->fogRng * 1664525u + 1013904223u;
    gX_F32 pickThree = (gX_F32)(weather->fogRng >> 8) * (1.0f / 16777216.0f);
    weather->fogRng = weather->fogRng * 1664525u + 1013904223u;
    gX_F32 pickFour = (gX_F32)(weather->fogRng >> 8) * (1.0f / 16777216.0f);
    weather->fogPosX[index] = centerX + (pickOne * 2.0f - 1.0f) * weather->halfExtent * 2.0f;
    weather->fogPosZ[index] = centerZ + (pickTwo * 2.0f - 1.0f) * weather->halfExtent * 2.0f;
    weather->fogPosY[index] = camera.camera3D.position.y - 1.80f + pickThree * 2.80f;
    if (!initial) weather->fogPosY[index] = camera.camera3D.position.y - 1.80f + pickThree * 1.50f;
    weather->fogAlpha[index] = 0.50f + pickFour * 0.50f;
    weather->fogSize[index] = 0.70f + pickThree * 0.60f;
    return;
}

//opencode: read the deterministic fog-sprite fixture hooks.
static gX_I0 Weather_ApplyFogEnv(gX_RendererGFX_Weather *weather)
{
    const gX_I8 *seedHook = getenv("GX_FOGSPRITE_SEED");
    const gX_I8 *countHook = getenv("GX_FOGSPRITE_COUNT");
    const gX_I8 *windHook = getenv("GX_FOGSPRITE_WIND");
    const gX_I8 *opacityHook = getenv("GX_FOGSPRITE_OPACITY");
    gX_U32 seed = seedHook ? (gX_U32)atoi((const char *)seedHook) : 4242u;
    gX_I32 count = countHook ? (gX_I32)atoi((const char *)countHook) : 28;
    gX_F32 wind = windHook ? (gX_F32)atof((const char *)windHook) : 0.25f;
    gX_F32 opacity = opacityHook ? (gX_F32)atof((const char *)opacityHook) : 0.10f;
    if (count < 0) count = 0;
    if (count > (gX_I32)gX_WEATHER_FOGSPRITE_MAX) count = (gX_I32)gX_WEATHER_FOGSPRITE_MAX;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 0.5f) opacity = 0.5f;
    if (seed != weather->fogSeed)
    {
        weather->fogSeed = seed;
        weather->fogRng = seed;
        for (gX_U32 i = 0; i < gX_WEATHER_FOGSPRITE_MAX; i++) Weather_RespawnFog(weather, i, 1, 0.0f, 0.0f);
    }
    weather->fogTarget = count;
    weather->fogWindX = wind;
    weather->fogWindZ = wind * 0.60f;
    weather->fogOpacity = opacity;
    return;
}

//ai: bounded snow-profile hook reader; invalid/non-finite input falls back to
//    the documented default so the sim never receives NaN/Inf.
static gX_F32 Weather_ProfileValue(const gX_I8 *hook, gX_F32 fallback,
    gX_F32 minimum, gX_F32 maximum)
{
    gX_F32 value = fallback;
    if (hook != NULL && hook[0] != '\0') {
        char *end = NULL;
        value = strtof((const char *)hook, &end);
        if (end == (char *)hook || !isfinite(value)) value = fallback;
    }
    if (value < minimum) value = minimum;
    if (value > maximum) value = maximum;
    return value;
}

//opencode: read the deterministic snowfall fixture hooks.
static gX_I0 Weather_ApplyEnv(gX_RendererGFX_Weather *weather)
{
    const gX_I8 *seedHook = getenv("GX_SNOWFALL_SEED");
    const gX_I8 *rateHook = getenv("GX_SNOWFALL_RATE");
    const gX_I8 *windHook = getenv("GX_SNOWFALL_WIND");
    const gX_I8 *windZHook = getenv("GX_SNOWFALL_WINDZ");
    const gX_I8 *speedHook = getenv("GX_SNOWFALL_SPEED");
    const gX_I8 *gustHook = getenv("GX_SNOWFALL_GUST");
    const gX_I8 *turbulenceHook = getenv("GX_SNOWFALL_TURBULENCE");
    const gX_I8 *sizeHook = getenv("GX_SNOWFALL_SIZE");
    const gX_I8 *alphaHook = getenv("GX_SNOWFALL_ALPHA");
    gX_U32 seed = seedHook ? (gX_U32)atoi((const char *)seedHook) : 1337u;
    gX_I32 rate = rateHook ? (gX_I32)atoi((const char *)rateHook) : 448;
    gX_F32 windX = windHook ? (gX_F32)atof((const char *)windHook) : 0.35f;
    gX_F32 windZ = windZHook ? (gX_F32)atof((const char *)windZHook) : 0.15f;
    gX_F32 speed = speedHook ? (gX_F32)atof((const char *)speedHook) : 1.0f;
    gX_F32 gust = Weather_ProfileValue(gustHook, 0.0f, 0.0f, 1.5f);
    gX_F32 turbulence = Weather_ProfileValue(turbulenceHook, 0.0f, 0.0f, 2.0f);
    gX_F32 size = Weather_ProfileValue(sizeHook, 1.0f, 0.25f, 2.0f);
    gX_F32 alpha = Weather_ProfileValue(alphaHook, 1.0f, 0.0f, 1.0f);
    if (rate < 0) rate = 0;
    if (rate > (gX_I32)gX_WEATHER_SNOW_MAX) rate = (gX_I32)gX_WEATHER_SNOW_MAX;
    if (speed < 0.0f) speed = 0.0f;
    if (speed > 4.0f) speed = 4.0f;
    if (seed != weather->seed)
    {
        weather->seed = seed;
        gX_Random32_Seed(&weather->rng, (gX_U64)seed, 0u);
        weather->timeAccum = 0.0f;
        for (gX_U32 i = 0; i < gX_WEATHER_SNOW_MAX; i++) Weather_RespawnFlake(weather, i, 1, 0.0f, 0.0f);
    }
    weather->targetCount = rate;
    weather->windX = windX;
    weather->windZ = windZ;
    weather->fallSpeed = 0.55f * speed;
    weather->gustStrength = gust;
    weather->turbulenceStrength = turbulence;
    weather->flakeSizeScale = size;
    weather->flakeAlphaScale = alpha;
    return;
}

gX_I0 gX_RendererGFX_Weather_Init(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Weather *weather = &gfx->weather;
    weather->enabled = 0;
    weather->seed = 1337u;
    gX_Random32_Seed(&weather->rng, 1337u, 0u);
    weather->targetCount = 448;
    weather->activeCount = 0;
    weather->windX = 0.35f;
    weather->windZ = 0.15f;
    weather->fallSpeed = 0.55f;
    weather->gustStrength = 0.0f;
    weather->turbulenceStrength = 0.0f;
    weather->flakeSizeScale = 1.0f;
    weather->flakeAlphaScale = 1.0f;
    weather->floorY = 0.09f;
    weather->halfExtent = 5.5f;
    weather->spawnTop = 6.5f;
    weather->timeAccum = 0.0f;
    weather->vbo = weather->vao = weather->renderProg = 0;
    weather->loaded = 0;
    weather->locsCached = 0;
    weather->uVP = weather->uPointScale = -1;
    weather->uLightTint = weather->rainUlightTint = -1;
    weather->rainSeed = 7331u;
    weather->rainRng = 7331u;
    weather->rainTarget = 384;
    weather->rainActive = 0;
    weather->rainWindX = 1.20f;
    weather->rainWindZ = 0.40f;
    weather->rainFall = 7.0f;
    weather->rainLength = 0.35f;
    weather->rainVbo = weather->rainVao = weather->rainProg = 0;
    weather->rainLoaded = 0;
    weather->rainLocsCached = 0;
    weather->rainUvp = -1;
    weather->posX = (gX_F32*)malloc(gX_WEATHER_SNOW_MAX * sizeof(gX_F32));
    weather->posY = (gX_F32*)malloc(gX_WEATHER_SNOW_MAX * sizeof(gX_F32));
    weather->posZ = (gX_F32*)malloc(gX_WEATHER_SNOW_MAX * sizeof(gX_F32));
    weather->flakeSize = (gX_F32*)malloc(gX_WEATHER_SNOW_MAX * sizeof(gX_F32));
    weather->flakeAlpha = (gX_F32*)malloc(gX_WEATHER_SNOW_MAX * sizeof(gX_F32));
    weather->flakePhase = (gX_F32*)malloc(gX_WEATHER_SNOW_MAX * sizeof(gX_F32));
    weather->flakeSpeed = (gX_F32*)malloc(gX_WEATHER_SNOW_MAX * sizeof(gX_F32));
    weather->snowUpload = (gX_F32*)malloc(gX_WEATHER_SNOW_MAX * 5 * sizeof(gX_F32));
    weather->rainPosX = (gX_F32*)malloc(gX_WEATHER_RAIN_MAX * sizeof(gX_F32));
    weather->rainPosY = (gX_F32*)malloc(gX_WEATHER_RAIN_MAX * sizeof(gX_F32));
    weather->rainPosZ = (gX_F32*)malloc(gX_WEATHER_RAIN_MAX * sizeof(gX_F32));
    weather->rainAlpha = (gX_F32*)malloc(gX_WEATHER_RAIN_MAX * sizeof(gX_F32));
    weather->rainUpload = (gX_F32*)malloc(gX_WEATHER_RAIN_MAX * 2 * 4 * sizeof(gX_F32));
    weather->fogSeed = 4242u;
    weather->fogRng = 4242u;
    weather->fogTarget = 28;
    weather->fogActive = 0;
    weather->fogWindX = 0.25f;
    weather->fogWindZ = 0.15f;
    weather->fogOpacity = 0.10f;
    weather->fogScale = 1.4f;
    weather->fogPosX = (gX_F32*)malloc(gX_WEATHER_FOGSPRITE_MAX * sizeof(gX_F32));
    weather->fogPosY = (gX_F32*)malloc(gX_WEATHER_FOGSPRITE_MAX * sizeof(gX_F32));
    weather->fogPosZ = (gX_F32*)malloc(gX_WEATHER_FOGSPRITE_MAX * sizeof(gX_F32));
    weather->fogAlpha = (gX_F32*)malloc(gX_WEATHER_FOGSPRITE_MAX * sizeof(gX_F32));
    weather->fogSize = (gX_F32*)malloc(gX_WEATHER_FOGSPRITE_MAX * sizeof(gX_F32));
    weather->fogUpload = (gX_F32*)malloc(gX_WEATHER_FOGSPRITE_MAX * 6 * 6 * sizeof(gX_F32));
    weather->fogVbo = weather->fogVao = weather->fogProg = 0;
    weather->fogLoaded = 0;
    weather->fogLocsCached = 0;
    weather->fogUvp = weather->fogUtint = -1;
    if (!weather->posX || !weather->posY || !weather->posZ
        || !weather->flakeSize || !weather->flakeAlpha || !weather->flakePhase
        || !weather->flakeSpeed || !weather->snowUpload
        || !weather->rainPosX || !weather->rainPosY || !weather->rainPosZ
        || !weather->rainAlpha || !weather->rainUpload
        || !weather->fogPosX || !weather->fogPosY || !weather->fogPosZ
        || !weather->fogAlpha || !weather->fogSize || !weather->fogUpload)
    {
        gX_TraceLog_GFX(gX_LOG_ERROR, "Weather: buffer allocation failed");
        return;
    }
    for (gX_U32 i = 0; i < gX_WEATHER_SNOW_MAX; i++) Weather_RespawnFlake(weather, i, 1, 0.0f, 0.0f);
    for (gX_U32 i = 0; i < gX_WEATHER_RAIN_MAX; i++) Weather_RespawnRain(weather, i, 1, 0.0f, 0.0f);
    for (gX_U32 i = 0; i < gX_WEATHER_FOGSPRITE_MAX; i++) Weather_RespawnFog(weather, i, 1, 0.0f, 0.0f);
    return;
}

gX_I0 gX_RendererGFX_Weather_Unload(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Weather *weather = &gfx->weather;
    if (weather->vbo > 0) { gX_RHI_Buffer_Delete(weather->vbo); weather->vbo = 0; }
    if (weather->vao > 0) { gX_RHI_VAO_Delete(weather->vao); weather->vao = 0; }
    if (weather->renderProg > 0) { gX_RHI_Shader_Delete(weather->renderProg); weather->renderProg = 0; }
    if (weather->rainVbo > 0) { gX_RHI_Buffer_Delete(weather->rainVbo); weather->rainVbo = 0; }
    if (weather->rainVao > 0) { gX_RHI_VAO_Delete(weather->rainVao); weather->rainVao = 0; }
    if (weather->rainProg > 0) { gX_RHI_Shader_Delete(weather->rainProg); weather->rainProg = 0; }
    free(weather->posX); weather->posX = NULL;
    free(weather->posY); weather->posY = NULL;
    free(weather->posZ); weather->posZ = NULL;
    free(weather->flakeSize); weather->flakeSize = NULL;
    free(weather->flakeAlpha); weather->flakeAlpha = NULL;
    free(weather->flakePhase); weather->flakePhase = NULL;
    free(weather->flakeSpeed); weather->flakeSpeed = NULL;
    free(weather->snowUpload); weather->snowUpload = NULL;
    free(weather->rainPosX); weather->rainPosX = NULL;
    free(weather->rainPosY); weather->rainPosY = NULL;
    free(weather->rainPosZ); weather->rainPosZ = NULL;
    free(weather->rainAlpha); weather->rainAlpha = NULL;
    free(weather->rainUpload); weather->rainUpload = NULL;
    free(weather->fogPosX); weather->fogPosX = NULL;
    free(weather->fogPosY); weather->fogPosY = NULL;
    free(weather->fogPosZ); weather->fogPosZ = NULL;
    free(weather->fogAlpha); weather->fogAlpha = NULL;
    free(weather->fogSize); weather->fogSize = NULL;
    free(weather->fogUpload); weather->fogUpload = NULL;
    if (weather->fogVbo > 0) { gX_RHI_Buffer_Delete(weather->fogVbo); weather->fogVbo = 0; }
    if (weather->fogVao > 0) { gX_RHI_VAO_Delete(weather->fogVao); weather->fogVao = 0; }
    if (weather->fogProg > 0) { gX_RHI_Shader_Delete(weather->fogProg); weather->fogProg = 0; }
    weather->fogLoaded = 0;
    weather->fogLocsCached = 0;
    weather->loaded = 0;
    weather->locsCached = 0;
    weather->rainLoaded = 0;
    weather->rainLocsCached = 0;
    return;
}

//ai: preserve in-bounds world positions and wrap arbitrarily many volume widths in one update.
//ai: share the Release wrap body rather than duplicating its modulo paths for each weather axis.
#if defined(NDEBUG) && (defined(__GNUC__) || defined(__clang__))
__attribute__((noinline))
#endif
static gX_F32 Weather_Wrap(gX_F32 Position, gX_F32 Center, gX_F32 HalfExtent)
{
    if (HalfExtent <= 0.0f) return Center;
    gX_F32 Offset = Position - Center;
    if (fabsf(Offset) <= HalfExtent) return Position;
    gX_F32 Width = HalfExtent * 2.0f;
    Offset = fmodf(Offset + HalfExtent, Width);
    if (Offset < 0.0f) Offset += Width;
    return Center + Offset - HalfExtent;
}

//ai: horizontal weather bounds follow the eye rather than the orbit/look target.
gX_I0 gX_RendererGFX_Weather_Update(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Weather *weather = &gfx->weather;
    if (!gfx->state.ENABLE_SNOWFALL && !gfx->state.ENABLE_RAIN && !gfx->state.ENABLE_FOGSPRITE) { weather->activeCount = 0; weather->rainActive = 0; weather->fogActive = 0; return; }
    if (gfx->state.ENABLE_SNOWFALL && !weather->loaded && maindata.backendLoaded)
    {
        weather->vao = gX_RHI_VAO_Create("weather_vao");
        weather->vbo = gX_RHI_Buffer_Create("weather_vbo");
        gX_RHI_VAO_Bind(weather->vao);
        gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, weather->vbo);
        gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, (gX_U32)(gX_WEATHER_SNOW_MAX * 5 * sizeof(gX_F32)), NULL, gX_RHI_DYNAMIC_DRAW);
        gX_RHI_Attrib_Pointer(0, 3, gX_RHI_FLOAT, 0, (gX_I32)(5 * sizeof(gX_F32)), (void*)0);
        gX_RHI_Attrib_Enable(0);
        gX_RHI_Attrib_Pointer(1, 1, gX_RHI_FLOAT, 0, (gX_I32)(5 * sizeof(gX_F32)), (void*)(3 * sizeof(gX_F32)));
        gX_RHI_Attrib_Enable(1);
        gX_RHI_Attrib_Pointer(2, 1, gX_RHI_FLOAT, 0, (gX_I32)(5 * sizeof(gX_F32)), (void*)(4 * sizeof(gX_F32)));
        gX_RHI_Attrib_Enable(2);
        gX_RHI_VAO_Bind(0);
        gX_I8 vertPath[gX_GFX_PATH_BUFFER];
        gX_I8 fragPath[gX_GFX_PATH_BUFFER];
        snprintf(vertPath, sizeof(vertPath), "%s", GX_PATH("shader", "gfx_weather.vert"));
        snprintf(fragPath, sizeof(fragPath), "%s", GX_PATH("shader", "gfx_weather.frag"));
        gX_I8 *vertSrc = Weather_ReadFile(vertPath, NULL);
        gX_I8 *fragSrc = Weather_ReadFile(fragPath, NULL);
        if (vertSrc && fragSrc)
        {
            gX_I8 errorLog[1024] = {0};
            weather->renderProg = gX_RHI_Shader_Compile(vertSrc, NULL, fragSrc, "weather_render", errorLog, sizeof(errorLog));
            if (weather->renderProg == 0) gX_TraceLog_GFX(gX_LOG_ERROR, "Weather render shader: %s", errorLog);
        }
        if (vertSrc) free(vertSrc);
        if (fragSrc) free(fragSrc);
        if (weather->renderProg > 0 && weather->vao > 0 && weather->vbo > 0) weather->loaded = 1;
        gX_RHI_Enable(gX_RHI_PROGRAM_POINT_SIZE);
        gX_TraceLog_GFX(gX_LOG_NOTICE, "Weather: render=%u vao=%u vbo=%u",
            weather->renderProg, weather->vao, weather->vbo);
    }
    Weather_ApplyEnv(weather);
    gX_F32 step = 0.0166667f;
    const gX_I8 *fixedHook = getenv("GX_FIXED_DT");
    if (fixedHook && fixedHook[0]) step = (gX_F32)atof((const char *)fixedHook);
    else
    {
        step = (gX_F32)gX_FrameTime_Get();
        if (step > 0.05f) step = 0.05f;
        if (step <= 0.0f) step = 0.0166667f;
    }
    weather->timeAccum += step;
    gX_F32 dx = camera.camera3D.position.x - camera.camera3D.target.x;
    gX_F32 dy = camera.camera3D.position.y - camera.camera3D.target.y;
    gX_F32 dz = camera.camera3D.position.z - camera.camera3D.target.z;
    gX_F32 dist = sqrtf(dx * dx + dy * dy + dz * dz);
    gX_F32 centerX = camera.camera3D.position.x;
    gX_F32 centerZ = camera.camera3D.position.z;
    gX_F32 spawnBottom, spawnTop;
    Weather_VerticalBand(weather, &spawnBottom, &spawnTop);
    if (gfx->state.ENABLE_SNOWFALL && weather->targetCount > 0
        && weather->posX && weather->posY && weather->posZ && weather->snowUpload)
    {
        gX_I32 active = weather->targetCount;
        if (dist > 22.0f) active = active / 4;
        else if (dist > 12.0f) active = active / 2;
        if (active < 0) active = 0;
        weather->activeCount = active;
        for (gX_I32 i = 0; i < active; i++)
        {
            gX_U32 index = (gX_U32)i;
            gX_F32 sway = sinf(weather->timeAccum * 2.0f + weather->flakePhase[index]) * 0.30f;
            gX_F32 driftX = weather->windX + sway;
            gX_F32 driftZ = weather->windZ;
            if (weather->gustStrength > 0.0f)
            {
                gX_F32 gustPhase = weather->flakePhase[index] * 0.37f;
                driftX += sinf(weather->timeAccum * 1.7f + gustPhase)
                    * weather->gustStrength;
                driftZ += cosf(weather->timeAccum * 1.3f + gustPhase * 0.71f)
                    * weather->gustStrength * 0.35f;
            }
            if (weather->turbulenceStrength > 0.0f)
            {
                gX_F32 turbulencePhase = weather->flakePhase[index] * 2.1f;
                driftX += sinf(weather->timeAccum * 7.3f + turbulencePhase)
                    * weather->turbulenceStrength * 0.55f;
                driftZ += cosf(weather->timeAccum * 5.9f
                    + turbulencePhase * 0.83f)
                    * weather->turbulenceStrength * 0.45f;
            }
            weather->posX[index] += driftX * step;
            weather->posZ[index] += driftZ * step;
            weather->posY[index] -= weather->fallSpeed * weather->flakeSpeed[index] * step;
            weather->posX[index] = Weather_Wrap(weather->posX[index], centerX, weather->halfExtent);
            weather->posZ[index] = Weather_Wrap(weather->posZ[index], centerZ, weather->halfExtent);
            if (weather->posY[index] < weather->floorY) Weather_RespawnFlake(weather, index, 0, centerX, centerZ);
            else if (weather->posY[index] < spawnBottom || weather->posY[index] > spawnTop + 1.0f)
                Weather_RespawnFlake(weather, index, 1, centerX, centerZ);
        }
    }
    else weather->activeCount = 0;
    if (gfx->state.ENABLE_RAIN && !weather->rainLoaded && maindata.backendLoaded)
    {
        weather->rainVao = gX_RHI_VAO_Create("rain_vao");
        weather->rainVbo = gX_RHI_Buffer_Create("rain_vbo");
        gX_RHI_VAO_Bind(weather->rainVao);
        gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, weather->rainVbo);
        gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, (gX_U32)(gX_WEATHER_RAIN_MAX * 2 * 4 * sizeof(gX_F32)), NULL, gX_RHI_DYNAMIC_DRAW);
        gX_RHI_Attrib_Pointer(0, 3, gX_RHI_FLOAT, 0, (gX_I32)(4 * sizeof(gX_F32)), (void*)0);
        gX_RHI_Attrib_Enable(0);
        gX_RHI_Attrib_Pointer(1, 1, gX_RHI_FLOAT, 0, (gX_I32)(4 * sizeof(gX_F32)), (void*)(3 * sizeof(gX_F32)));
        gX_RHI_Attrib_Enable(1);
        gX_RHI_VAO_Bind(0);
        gX_I8 rainVertPath[gX_GFX_PATH_BUFFER];
        gX_I8 rainFragPath[gX_GFX_PATH_BUFFER];
        snprintf(rainVertPath, sizeof(rainVertPath), "%s", GX_PATH("shader", "gfx_rain.vert"));
        snprintf(rainFragPath, sizeof(rainFragPath), "%s", GX_PATH("shader", "gfx_rain.frag"));
        gX_I8 *rainVertSrc = Weather_ReadFile(rainVertPath, NULL);
        gX_I8 *rainFragSrc = Weather_ReadFile(rainFragPath, NULL);
        if (rainVertSrc && rainFragSrc)
        {
            gX_I8 errorLog[1024] = {0};
            weather->rainProg = gX_RHI_Shader_Compile(rainVertSrc, NULL, rainFragSrc, "rain_render", errorLog, sizeof(errorLog));
            if (weather->rainProg == 0) gX_TraceLog_GFX(gX_LOG_ERROR, "Rain render shader: %s", errorLog);
        }
        if (rainVertSrc) free(rainVertSrc);
        if (rainFragSrc) free(rainFragSrc);
        if (weather->rainProg > 0 && weather->rainVao > 0 && weather->rainVbo > 0) weather->rainLoaded = 1;
        gX_TraceLog_GFX(gX_LOG_NOTICE, "WeatherRain: render=%u vao=%u vbo=%u",
            weather->rainProg, weather->rainVao, weather->rainVbo);
    }
    Weather_ApplyRainEnv(weather);
    if (gfx->state.ENABLE_RAIN && weather->rainTarget > 0
        && weather->rainPosX && weather->rainPosY && weather->rainPosZ && weather->rainUpload)
    {
        gX_I32 rainActive = weather->rainTarget;
        if (dist > 22.0f) rainActive = rainActive / 4;
        else if (dist > 12.0f) rainActive = rainActive / 2;
        if (rainActive < 0) rainActive = 0;
        weather->rainActive = rainActive;
        for (gX_I32 i = 0; i < rainActive; i++)
        {
            gX_U32 index = (gX_U32)i;
            weather->rainPosX[index] += weather->rainWindX * step;
            weather->rainPosZ[index] += weather->rainWindZ * step;
            weather->rainPosY[index] -= weather->rainFall * step;
            weather->rainPosX[index] = Weather_Wrap(weather->rainPosX[index], centerX, weather->halfExtent);
            weather->rainPosZ[index] = Weather_Wrap(weather->rainPosZ[index], centerZ, weather->halfExtent);
            if (weather->rainPosY[index] < weather->floorY) Weather_RespawnRain(weather, index, 0, centerX, centerZ);
            else if (weather->rainPosY[index] < spawnBottom || weather->rainPosY[index] > spawnTop + 1.0f)
                Weather_RespawnRain(weather, index, 1, centerX, centerZ);
        }
    }
    else weather->rainActive = 0;
    if (gfx->state.ENABLE_FOGSPRITE && !weather->fogLoaded && maindata.backendLoaded)
    {
        weather->fogVao = gX_RHI_VAO_Create("fogsprite_vao");
        weather->fogVbo = gX_RHI_Buffer_Create("fogsprite_vbo");
        gX_RHI_VAO_Bind(weather->fogVao);
        gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, weather->fogVbo);
        gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, (gX_U32)(gX_WEATHER_FOGSPRITE_MAX * 6 * 6 * sizeof(gX_F32)), NULL, gX_RHI_DYNAMIC_DRAW);
        gX_RHI_Attrib_Pointer(0, 3, gX_RHI_FLOAT, 0, (gX_I32)(6 * sizeof(gX_F32)), (void*)0);
        gX_RHI_Attrib_Enable(0);
        gX_RHI_Attrib_Pointer(1, 2, gX_RHI_FLOAT, 0, (gX_I32)(6 * sizeof(gX_F32)), (void*)(3 * sizeof(gX_F32)));
        gX_RHI_Attrib_Enable(1);
        gX_RHI_Attrib_Pointer(2, 1, gX_RHI_FLOAT, 0, (gX_I32)(6 * sizeof(gX_F32)), (void*)(5 * sizeof(gX_F32)));
        gX_RHI_Attrib_Enable(2);
        gX_RHI_VAO_Bind(0);
        gX_I8 fogVertPath[gX_GFX_PATH_BUFFER];
        gX_I8 fogFragPath[gX_GFX_PATH_BUFFER];
        snprintf(fogVertPath, sizeof(fogVertPath), "%s", GX_PATH("shader", "gfx_fogsprite.vert"));
        snprintf(fogFragPath, sizeof(fogFragPath), "%s", GX_PATH("shader", "gfx_fogsprite.frag"));
        gX_I8 *fogVertSrc = Weather_ReadFile(fogVertPath, NULL);
        gX_I8 *fogFragSrc = Weather_ReadFile(fogFragPath, NULL);
        if (fogVertSrc && fogFragSrc)
        {
            gX_I8 errorLog[1024] = {0};
            weather->fogProg = gX_RHI_Shader_Compile(fogVertSrc, NULL, fogFragSrc, "fogsprite_render", errorLog, sizeof(errorLog));
            if (weather->fogProg == 0) gX_TraceLog_GFX(gX_LOG_ERROR, "FogSprite render shader: %s", errorLog);
        }
        if (fogVertSrc) free(fogVertSrc);
        if (fogFragSrc) free(fogFragSrc);
        if (weather->fogProg > 0 && weather->fogVao > 0 && weather->fogVbo > 0) weather->fogLoaded = 1;
        gX_TraceLog_GFX(gX_LOG_NOTICE, "WeatherFog: render=%u vao=%u vbo=%u",
            weather->fogProg, weather->fogVao, weather->fogVbo);
    }
    Weather_ApplyFogEnv(weather);
    if (gfx->state.ENABLE_FOGSPRITE && weather->fogTarget > 0
        && weather->fogPosX && weather->fogPosY && weather->fogPosZ && weather->fogUpload)
    {
        gX_I32 fogActive = weather->fogTarget;
        if (dist > 22.0f) fogActive = fogActive / 2;
        if (fogActive < 0) fogActive = 0;
        weather->fogActive = fogActive;
        for (gX_I32 i = 0; i < fogActive; i++)
        {
            gX_U32 index = (gX_U32)i;
            weather->fogPosX[index] += weather->fogWindX * step;
            weather->fogPosZ[index] += weather->fogWindZ * step;
            weather->fogPosX[index] = Weather_Wrap(weather->fogPosX[index], centerX, weather->halfExtent * 2.0f);
            weather->fogPosZ[index] = Weather_Wrap(weather->fogPosZ[index], centerZ, weather->halfExtent * 2.0f);
            weather->fogPosY[index] = Weather_Wrap(weather->fogPosY[index], camera.camera3D.position.y - 0.40f, 1.40f);
        }
    }
    else weather->fogActive = 0;
    return;
}

//opencode: weather sprites obey the scene lighting — ambient plus the diffuse
//   sun contribution, so turning the sun off darkens flakes and drops (user
//   finding: weather ignored lighting). Shaders fall back to white when unset.
static gX_I0 Weather_LightTint(gX_RendererGFX *gfx, gX_F32 *out)
{
    gX_F32 ambient = 0.25f;
    if (gfx->state.ENABLE_LIGHTING_AMBIENT) ambient += 0.35f;
    gX_F32 sun = 0.0f;
    if (gfx->state.ENABLE_LIGHTING_DIFFUSE)
    {
        sun = gfx->sun.brightness;
        if (sun < 0.0f) sun = 0.0f;
        if (sun > 2.0f) sun = 2.0f;
        sun *= 0.5f;
    }
    out[0] = ambient + (gX_F32)gfx->sun.color.r / 255.0f * sun;
    out[1] = ambient + (gX_F32)gfx->sun.color.g / 255.0f * sun;
    out[2] = ambient + (gX_F32)gfx->sun.color.b / 255.0f * sun;
    if (out[0] > 1.0f) out[0] = 1.0f;
    if (out[1] > 1.0f) out[1] = 1.0f;
    if (out[2] > 1.0f) out[2] = 1.0f;
}

//ai: fog height fading follows its eye-relative volume instead of suppressing elevated sprites.
gX_I0 gX_RendererGFX_Weather_Draw(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Weather *weather = &gfx->weather;
    if (!maindata.backendLoaded) return;
    gX_Matrix view = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
    gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix proj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect, camera.frustum.near, camera.frustum.far);
    gX_Matrix vp = gX_Matrix_Multiply(view, proj);
    if (gfx->state.ENABLE_SNOWFALL && weather->loaded && weather->renderProg > 0
        && weather->vao > 0 && weather->vbo > 0 && weather->activeCount > 0
        && weather->snowUpload && weather->posX)
    {
    gX_F32 *upload = weather->snowUpload;
    gX_I32 active = weather->activeCount;
    if (active > (gX_I32)gX_WEATHER_SNOW_MAX) active = (gX_I32)gX_WEATHER_SNOW_MAX;
    for (gX_I32 i = 0; i < active; i++)
    {
        gX_U32 index = (gX_U32)i;
        gX_F32 size = weather->flakeSize[index];
        gX_F32 alpha = weather->flakeAlpha[index];
        if (weather->flakeSizeScale != 1.0f) size *= weather->flakeSizeScale;
        if (weather->flakeAlphaScale != 1.0f) alpha *= weather->flakeAlphaScale;
        upload[i * 5 + 0] = weather->posX[index];
        upload[i * 5 + 1] = weather->posY[index];
        upload[i * 5 + 2] = weather->posZ[index];
        upload[i * 5 + 3] = size;
        upload[i * 5 + 4] = alpha;
    }
    if (!weather->locsCached)
    {
        weather->uVP = gX_RHI_GetUniformLocation(weather->renderProg, "vp");
        weather->uPointScale = gX_RHI_GetUniformLocation(weather->renderProg, "pointScale");
        weather->uLightTint = gX_RHI_GetUniformLocation(weather->renderProg, "lightTint");
        weather->locsCached = 1;
    }
    GX_PROFILE_ZONE_BEGIN("gfx:weather");
    gX_GL_State_Push();
    gX_RHI_Enable(gX_RHI_BLEND);
    gX_RHI_Blend_Func_Separate(gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA, gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA);
    gX_RHI_Depth_Write(0);
    gX_RHI_Enable(gX_RHI_DEPTH_TEST);
    gX_RHI_Depth_Func(gX_RHI_LEQUAL);
    gX_RHI_VAO_Bind(weather->vao);
    gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, weather->vbo);
    gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, (gX_U32)(active * 5 * sizeof(gX_F32)), upload, gX_RHI_DYNAMIC_DRAW);
    gX_RHI_Shader_Use(weather->renderProg);
    gX_RHI_UniformMatrix(weather->uVP, 1, 1, &vp.m0);
    gX_RHI_Uniform_Float(weather->uPointScale, (gX_F32)maindata.renderHeight * 0.008f);
    {
        gX_F32 tint[3];
        Weather_LightTint(gfx, tint);
        gX_RHI_Uniform_Vec3(weather->uLightTint, tint[0], tint[1], tint[2]);
    }
    gX_RHI_DrawArrays(gX_RHI_POINTS, 0, (gX_U32)active);
    gX_RHI_VAO_Bind(0);
    gX_RHI_Shader_Use(0);
    gX_GL_State_Pop();
    GX_PROFILE_ZONE_END("gfx:weather");
    }
    if (gfx->state.ENABLE_RAIN && weather->rainLoaded && weather->rainProg > 0
        && weather->rainVao > 0 && weather->rainVbo > 0 && weather->rainActive > 0
        && weather->rainUpload && weather->rainPosX)
    {
        gX_I32 drops = weather->rainActive;
        if (drops > (gX_I32)gX_WEATHER_RAIN_MAX) drops = (gX_I32)gX_WEATHER_RAIN_MAX;
        gX_F32 *rainUpload = weather->rainUpload;
        gX_F32 fallLen = sqrtf(weather->rainWindX * weather->rainWindX
            + weather->rainFall * weather->rainFall
            + weather->rainWindZ * weather->rainWindZ);
        gX_F32 tailX = 0.0f, tailY = -1.0f, tailZ = 0.0f;
        if (fallLen > 0.0001f)
        {
            tailX = -weather->rainWindX / fallLen * weather->rainLength;
            tailY = -weather->rainFall / fallLen * weather->rainLength;
            tailZ = -weather->rainWindZ / fallLen * weather->rainLength;
        }
        for (gX_I32 i = 0; i < drops; i++)
        {
            gX_U32 index = (gX_U32)i;
            rainUpload[i * 8 + 0] = weather->rainPosX[index];
            rainUpload[i * 8 + 1] = weather->rainPosY[index];
            rainUpload[i * 8 + 2] = weather->rainPosZ[index];
            rainUpload[i * 8 + 3] = weather->rainAlpha[index];
            rainUpload[i * 8 + 4] = weather->rainPosX[index] + tailX;
            rainUpload[i * 8 + 5] = weather->rainPosY[index] + tailY;
            rainUpload[i * 8 + 6] = weather->rainPosZ[index] + tailZ;
            rainUpload[i * 8 + 7] = 0.0f;
        }
        if (!weather->rainLocsCached)
        {
            weather->rainUvp = gX_RHI_GetUniformLocation(weather->rainProg, "vp");
            weather->rainUlightTint = gX_RHI_GetUniformLocation(weather->rainProg, "lightTint");
            weather->rainLocsCached = 1;
        }
        GX_PROFILE_ZONE_BEGIN("gfx:rain");
        gX_GL_State_Push();
        gX_RHI_Enable(gX_RHI_BLEND);
        gX_RHI_Blend_Func_Separate(gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA, gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA);
        gX_RHI_Depth_Write(0);
        gX_RHI_Enable(gX_RHI_DEPTH_TEST);
        gX_RHI_Depth_Func(gX_RHI_LEQUAL);
        gX_RHI_VAO_Bind(weather->rainVao);
        gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, weather->rainVbo);
        gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, (gX_U32)(drops * 2 * 4 * sizeof(gX_F32)), rainUpload, gX_RHI_DYNAMIC_DRAW);
        gX_RHI_Shader_Use(weather->rainProg);
        gX_RHI_UniformMatrix(weather->rainUvp, 1, 1, &vp.m0);
        {
            gX_F32 tint[3];
            Weather_LightTint(gfx, tint);
            gX_RHI_Uniform_Vec3(weather->rainUlightTint, tint[0], tint[1], tint[2]);
        }
        gX_RHI_DrawArrays(gX_RHI_LINES, 0, (gX_U32)(drops * 2));
        gX_RHI_VAO_Bind(0);
        gX_RHI_Shader_Use(0);
        gX_GL_State_Pop();
        GX_PROFILE_ZONE_END("gfx:rain");
    }
    if (gfx->state.ENABLE_FOGSPRITE && weather->fogLoaded && weather->fogProg > 0
        && weather->fogVao > 0 && weather->fogVbo > 0 && weather->fogActive > 0
        && weather->fogUpload && weather->fogPosX)
    {
        gX_I32 sprites = weather->fogActive;
        if (sprites > (gX_I32)gX_WEATHER_FOGSPRITE_MAX) sprites = (gX_I32)gX_WEATHER_FOGSPRITE_MAX;
        static gX_U32 fogOrder[gX_WEATHER_FOGSPRITE_MAX];
        for (gX_I32 i = 0; i < sprites; i++) fogOrder[i] = (gX_U32)i;
        for (gX_I32 i = 1; i < sprites; i++)
        {
            gX_U32 key = fogOrder[i];
            gX_F32 keyDx = weather->fogPosX[key] - camera.camera3D.position.x;
            gX_F32 keyDy = weather->fogPosY[key] - camera.camera3D.position.y;
            gX_F32 keyDz = weather->fogPosZ[key] - camera.camera3D.position.z;
            gX_F32 keyDist = keyDx * keyDx + keyDy * keyDy + keyDz * keyDz;
            gX_I32 j = i - 1;
            while (j >= 0)
            {
                gX_U32 cur = fogOrder[j];
                gX_F32 curDx = weather->fogPosX[cur] - camera.camera3D.position.x;
                gX_F32 curDy = weather->fogPosY[cur] - camera.camera3D.position.y;
                gX_F32 curDz = weather->fogPosZ[cur] - camera.camera3D.position.z;
                gX_F32 curDist = curDx * curDx + curDy * curDy + curDz * curDz;
                if (curDist >= keyDist) break;
                fogOrder[j + 1] = fogOrder[j];
                j--;
            }
            fogOrder[j + 1] = key;
        }
        gX_F32v3 forward = gX_F32v3_Sub(camera.camera3D.target, camera.camera3D.position);
        forward = gX_F32v3_Normalize(forward);
        gX_F32v3 right = gX_F32v3_Normalize(gX_F32v3_Cross(forward, camera.camera3D.up));
        gX_F32v3 upv = gX_F32v3_Cross(right, forward);
        gX_F32 *fogVertices = weather->fogUpload;
        for (gX_I32 i = 0; i < sprites; i++)
        {
            gX_U32 index = fogOrder[i];
            gX_F32 half = weather->fogScale * weather->fogSize[index];
            gX_F32 cx = weather->fogPosX[index];
            gX_F32 cy = weather->fogPosY[index];
            gX_F32 cz = weather->fogPosZ[index];
            gX_F32 distDx = cx - camera.camera3D.position.x;
            gX_F32 distDy = cy - camera.camera3D.position.y;
            gX_F32 distDz = cz - camera.camera3D.position.z;
            gX_F32 dist = sqrtf(distDx * distDx + distDy * distDy + distDz * distDz);
            gX_F32 distFade = 1.20f - dist / 25.0f;
            if (distFade > 1.0f) distFade = 1.0f;
            if (distFade < 0.15f) distFade = 0.15f;
            gX_F32 heightFade = 1.50f - (cy - camera.camera3D.position.y + 2.0f) / 3.0f;
            if (heightFade > 1.0f) heightFade = 1.0f;
            if (heightFade < 0.30f) heightFade = 0.30f;
            gX_F32 alpha = weather->fogOpacity * weather->fogAlpha[index] * distFade * heightFade;
            gX_F32 corners[4][3] = {
                {cx - right.x * half - upv.x * half * 0.6f, cy - right.y * half - upv.y * half * 0.6f, cz - right.z * half - upv.z * half * 0.6f},
                {cx + right.x * half - upv.x * half * 0.6f, cy + right.y * half - upv.y * half * 0.6f, cz + right.z * half - upv.z * half * 0.6f},
                {cx + right.x * half + upv.x * half * 0.6f, cy + right.y * half + upv.y * half * 0.6f, cz + right.z * half + upv.z * half * 0.6f},
                {cx - right.x * half + upv.x * half * 0.6f, cy - right.y * half + upv.y * half * 0.6f, cz - right.z * half + upv.z * half * 0.6f},
            };
            gX_F32 uvs[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
            gX_U32 quad[6] = {0, 1, 2, 0, 2, 3};
            for (gX_I32 v = 0; v < 6; v++)
            {
                gX_U32 corner = quad[v];
                fogVertices[(i * 6 + v) * 6 + 0] = corners[corner][0];
                fogVertices[(i * 6 + v) * 6 + 1] = corners[corner][1];
                fogVertices[(i * 6 + v) * 6 + 2] = corners[corner][2];
                fogVertices[(i * 6 + v) * 6 + 3] = uvs[corner][0];
                fogVertices[(i * 6 + v) * 6 + 4] = uvs[corner][1];
                fogVertices[(i * 6 + v) * 6 + 5] = alpha;
            }
        }
        if (!weather->fogLocsCached)
        {
            weather->fogUvp = gX_RHI_GetUniformLocation(weather->fogProg, "vp");
            weather->fogUtint = gX_RHI_GetUniformLocation(weather->fogProg, "tint");
            weather->fogLocsCached = 1;
        }
        GX_PROFILE_ZONE_BEGIN("gfx:fogsprite");
        gX_GL_State_Push();
        gX_RHI_Enable(gX_RHI_BLEND);
        gX_RHI_Blend_Func_Separate(gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA, gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA);
        gX_RHI_Depth_Write(0);
        gX_RHI_Enable(gX_RHI_DEPTH_TEST);
        gX_RHI_Depth_Func(gX_RHI_LEQUAL);
        gX_RHI_VAO_Bind(weather->fogVao);
        gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, weather->fogVbo);
        gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, (gX_U32)(sprites * 6 * 6 * sizeof(gX_F32)), fogVertices, gX_RHI_DYNAMIC_DRAW);
        gX_RHI_Shader_Use(weather->fogProg);
        gX_RHI_UniformMatrix(weather->fogUvp, 1, 1, &vp.m0);
        gX_RHI_Uniform_Vec3(weather->fogUtint,
            (gX_F32)gfx->fog.color.r / 255.0f,
            (gX_F32)gfx->fog.color.g / 255.0f,
            (gX_F32)gfx->fog.color.b / 255.0f);
        gX_RHI_DrawArrays(gX_RHI_TRIANGLES, 0, (gX_U32)(sprites * 6));
        gX_RHI_VAO_Bind(0);
        gX_RHI_Shader_Use(0);
        gX_GL_State_Pop();
        GX_PROFILE_ZONE_END("gfx:fogsprite");
    }
    return;
}
