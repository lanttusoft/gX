/**********************************************************************************************
*
*   [gX: src/scripting/tour/tour_raymarch.cpp]
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


//ai: tour raymarch API — the CPU raymarch renderer module
//   (renderer.raymarch.data; scene SCENE_RENDERER_RAYMARCH) driven through
//   gameX's RmSet* contract plus the gX-only surface: the extra parameters
//   (stepsPerBounce/distTotal/renderScale), the full flag set (glow/shadow/
//   proxy/volumetric, sample debug), object transform setters, and the
//   built-in presets. Byte-colour setters take 0..255 (gX_Color); object and
//   light colours are linear v4 floats (the module's HDR convention). The
//   SDF graph and the clouds/volFog parameter blocks are not bound — presets
//   carry those looks; the graph authoring surface stays with the editor.
//opencode: raylib headers FIRST with C++ linkage — wrapper headers reached
//   through other includes would give raymath's operator overloads C linkage
//   (see src/tools/gfx/gfx_editor.cpp).
#include "raylib.h"
#include "raymath.h"

#include "tour.h"

#include <angelscript.h>

#include <string>

#include "../../common/log.h"
#include "../../main.h"
#include "../../renderer/renderer.h"
#include "../../renderer/raymarch.h"

// ===========================================================================
// Instance
// ===========================================================================

static gX_RendererRaymarch *TourRaymarch_Instance(void) {
    if (!renderer.raymarch.data || renderer.raymarch.amount == 0) return NULL;
    return (gX_RendererRaymarch *)renderer.raymarch.data;
}

// ===========================================================================
// Terrain
// ===========================================================================

static void TourRaymarch_SetTerrainOffset(gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->terrain.offset = gX_F32v3_Make(x, y, z);
}

static void TourRaymarch_SetTerrainScale(gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->terrain.scale = gX_F32v3_Make(x, y, z);
}

static void TourRaymarch_SetTerrainSeed(gX_I32 seed) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->terrain.seed = seed;
}

static void TourRaymarch_SetTerrainColor(gX_I32 r, gX_I32 g, gX_I32 b, gX_I32 a) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->terrain.color = (gX_Color){(gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a};
}

static void TourRaymarch_SetTerrainColorSteep(gX_I32 r, gX_I32 g, gX_I32 b, gX_I32 a) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->terrain.colorSteep = (gX_Color){(gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a};
}

static void TourRaymarch_SetTerrainColorBeach(gX_I32 r, gX_I32 g, gX_I32 b, gX_I32 a) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->terrain.colorBeach = (gX_Color){(gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a};
}

static void TourRaymarch_SetTerrainParams(gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->terrain.parameters = gX_F32v4_Make(x, y, z, w);
}

// ===========================================================================
// Water
// ===========================================================================

static void TourRaymarch_SetWaterOffset(gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->water.offset = gX_F32v3_Make(x, y, z);
}

static void TourRaymarch_SetWaterScale(gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->water.scale = gX_F32v3_Make(x, y, z);
}

static void TourRaymarch_SetWaterSpeed(gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->water.speed = gX_F32v3_Make(x, y, z);
}

static void TourRaymarch_SetWaterColor(gX_I32 r, gX_I32 g, gX_I32 b, gX_I32 a) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->water.color = (gX_Color){(gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a};
}

static void TourRaymarch_SetWaterRipple(gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->water.ripple = gX_F32v4_Make(x, y, z, w);
}

static void TourRaymarch_SetWaterWhirlpool(gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->water.whirlpool = gX_F32v4_Make(x, y, z, w);
}

// ===========================================================================
// Fog + sun
// ===========================================================================

static void TourRaymarch_SetFogNear(gX_F32 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->fog.near = v;
}

static void TourRaymarch_SetFogFar(gX_F32 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->fog.far = v;
}

static void TourRaymarch_SetFogColor(gX_I32 r, gX_I32 g, gX_I32 b, gX_I32 a) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->fog.color = (gX_Color){(gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a};
}

static void TourRaymarch_SetSunBrightness(gX_F32 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->sun.brightness = v;
}

static void TourRaymarch_SetSunSize(gX_F32 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->sun.size = v;
}

static void TourRaymarch_SetSunDir(gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->sun.dir = gX_F32v3_Make(x, y, z);
}

static void TourRaymarch_SetSunColor(gX_I32 r, gX_I32 g, gX_I32 b, gX_I32 a) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->sun.color = (gX_Color){(gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a};
}

// ===========================================================================
// March parameters
// ===========================================================================

static void TourRaymarch_SetBounces(gX_I32 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->parameters.bounces = v;
}

static void TourRaymarch_SetSteps(gX_I32 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->parameters.steps = v;
}

static void TourRaymarch_SetStepsPerBounce(gX_I32 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->parameters.stepsPerBounce = v;
}

static void TourRaymarch_SetNear(gX_F32 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->parameters.near = v;
}

static void TourRaymarch_SetFar(gX_F32 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->parameters.far = v;
}

static void TourRaymarch_SetEpsilon(gX_F32 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->parameters.epsilon = v;
}

static void TourRaymarch_SetStepMax(gX_F32 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->parameters.stepMax = v;
}

static void TourRaymarch_SetDistTotal(gX_F32 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->parameters.distTotal = v;
}

static void TourRaymarch_SetRenderScale(gX_F32 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->parameters.renderScale = v;
}

// ===========================================================================
// Flags
// ===========================================================================

static void TourRaymarch_SetFlagSunShadows(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableSunShadows = v ? 1 : 0;
}

static void TourRaymarch_SetFlagObjects(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableObjects = v ? 1 : 0;
}

static void TourRaymarch_SetFlagObjectGlow(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableObjectGlow = v ? 1 : 0;
}

static void TourRaymarch_SetFlagLights(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableLights = v ? 1 : 0;
}

static void TourRaymarch_SetFlagLightShadows(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableLightShadows = v ? 1 : 0;
}

static void TourRaymarch_SetFlagLightGlow(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableLightGlow = v ? 1 : 0;
}

static void TourRaymarch_SetFlagLightIllumination(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableLightIllumination = v ? 1 : 0;
}

static void TourRaymarch_SetFlagFrag(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableFrag = v ? 1 : 0;
}

static void TourRaymarch_SetFlagFog(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableFog = v ? 1 : 0;
}

static void TourRaymarch_SetFlagTerrain(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableTerrain = v ? 1 : 0;
}

static void TourRaymarch_SetFlagTerrainGrid(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableTerrainGrid = v ? 1 : 0;
}

static void TourRaymarch_SetFlagWater(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableWater = v ? 1 : 0;
}

static void TourRaymarch_SetFlagSampleSign(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableSampleSign = v ? 1 : 0;
}

static void TourRaymarch_SetFlagSampleLastStep(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableSampleLastStep = v ? 1 : 0;
}

static void TourRaymarch_SetFlagSky(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableSky = v ? 1 : 0;
}

static void TourRaymarch_SetFlagAO(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableAO = v ? 1 : 0;
}

static void TourRaymarch_SetFlagProceduralSky(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableProceduralSky = v ? 1 : 0;
}

static void TourRaymarch_SetFlagClouds(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableClouds = v ? 1 : 0;
}

static void TourRaymarch_SetFlagProxy(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableProxy = v ? 1 : 0;
}

static void TourRaymarch_SetFlagVolumetricFog(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->flags.enableVolumetricFog = v ? 1 : 0;
}

// ===========================================================================
// Objects
// ===========================================================================

static void TourRaymarch_SetObjectPos(gX_I32 index, gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm && index >= 0 && index < GX_RM_MAX_OBJECTS)
        rm->objects.pos[index] = gX_F32v3_Make(x, y, z);
}

static void TourRaymarch_SetObjectColor(gX_I32 index, gX_F32 r, gX_F32 g, gX_F32 b, gX_F32 a) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm && index >= 0 && index < GX_RM_MAX_OBJECTS)
        rm->objects.color[index] = gX_F32v4_Make(r, g, b, a);
}

static void TourRaymarch_SetObjectType(gX_I32 index, gX_I32 type) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) gX_RendererRaymarch_Object_Type_Set(rm, index, type);
}

static void TourRaymarch_SetObjectParams(gX_I32 index, gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm && index >= 0 && index < GX_RM_MAX_OBJECTS)
        rm->objects.parameters[index] = gX_F32v4_Make(x, y, z, w);
}

static void TourRaymarch_SetObjectScale(gX_I32 index, gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) gX_RendererRaymarch_Object_Scale_Set(rm, index, gX_F32v3_Make(x, y, z));
}

static void TourRaymarch_SetObjectRotate(gX_I32 index, gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) gX_RendererRaymarch_Object_Rotate(rm, index, gX_F32v3_Make(x, y, z));
}

static void TourRaymarch_SetObjectAmount(gX_I32 amount) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (!rm) return;
    if (amount < 0) amount = 0;
    if (amount > GX_RM_MAX_OBJECTS) amount = GX_RM_MAX_OBJECTS;
    rm->objects.amount = amount;
}

static void TourRaymarch_RemoveObject(gX_I32 index) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) gX_RendererRaymarch_Object_Remove(rm, index);
}

// ===========================================================================
// Lights
// ===========================================================================

static void TourRaymarch_SetLightPos(gX_I32 index, gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm && index >= 0 && index < GX_RM_MAX_LIGHTS)
        rm->lights.pos[index] = gX_F32v3_Make(x, y, z);
}

static void TourRaymarch_SetLightColor(gX_I32 index, gX_F32 r, gX_F32 g, gX_F32 b, gX_F32 a) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm && index >= 0 && index < GX_RM_MAX_LIGHTS)
        rm->lights.color[index] = gX_F32v4_Make(r, g, b, a);
}

static void TourRaymarch_SetLightBrightness(gX_I32 index, gX_F32 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm && index >= 0 && index < GX_RM_MAX_LIGHTS)
        rm->lights.brightness[index] = v;
}

static void TourRaymarch_SetLightAmount(gX_I32 amount) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (!rm) return;
    if (amount < 0) amount = 0;
    if (amount > GX_RM_MAX_LIGHTS) amount = GX_RM_MAX_LIGHTS;
    rm->lights.amount = amount;
}

// ===========================================================================
// Misc + state
// ===========================================================================

static void TourRaymarch_SetAmbientColor(gX_I32 r, gX_I32 g, gX_I32 b, gX_I32 a) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->misc.lightAmbient = (gX_Color){(gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a};
}

static void TourRaymarch_SetNullColor(gX_I32 r, gX_I32 g, gX_I32 b, gX_I32 a) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->misc.nullColor = (gX_Color){(gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a};
}

static void TourRaymarch_SetTime(gX_F32 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->state.time = v;
}

static void TourRaymarch_SetUseInternalTime(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->state.useInternalTime = v ? 1 : 0;
}

static void TourRaymarch_SetUseInternalCamera(gX_I1 v) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) rm->state.useInternalCamera = v ? 1 : 0;
}

// ===========================================================================
// Presets
// ===========================================================================

static void TourRaymarch_LoadDefault(void) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) gX_RendererRaymarch_LoadDefault(rm);
}

static void TourRaymarch_LoadRandom(void) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) gX_RendererRaymarch_LoadRandom(rm);
}

static void TourRaymarch_LoadBuiltIn(gX_I32 index) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) gX_RendererRaymarch_Preset_BuiltIn(rm, index);
}

static void TourRaymarch_LoadPreset(const std::string &path) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) gX_RendererRaymarch_Preset_Load(rm, path.c_str());
}

static void TourRaymarch_SavePreset(const std::string &path) {
    gX_RendererRaymarch *rm = TourRaymarch_Instance();
    if (rm) gX_RendererRaymarch_Preset_Save(rm, path.c_str());
}

// ===========================================================================
// Registration
// ===========================================================================

#define AS_CHECK(r) do { if (r < 0) \
    gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "AS tour-raymarch registration failed (code %d)", r); \
} while(0)

gX_I32 gX_TourRaymarch_Register(asIScriptEngine *engine) {
    int r = 0;

    // --- Terrain ---
    r = engine->RegisterGlobalFunction("void Raymarch_SetTerrainOffset(float, float, float)",
        asFUNCTION(TourRaymarch_SetTerrainOffset), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetTerrainScale(float, float, float)",
        asFUNCTION(TourRaymarch_SetTerrainScale), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetTerrainSeed(int)",
        asFUNCTION(TourRaymarch_SetTerrainSeed), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetTerrainColor(int, int, int, int)",
        asFUNCTION(TourRaymarch_SetTerrainColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetTerrainColorSteep(int, int, int, int)",
        asFUNCTION(TourRaymarch_SetTerrainColorSteep), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetTerrainColorBeach(int, int, int, int)",
        asFUNCTION(TourRaymarch_SetTerrainColorBeach), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetTerrainParams(float, float, float, float)",
        asFUNCTION(TourRaymarch_SetTerrainParams), asCALL_CDECL); AS_CHECK(r);

    // --- Water ---
    r = engine->RegisterGlobalFunction("void Raymarch_SetWaterOffset(float, float, float)",
        asFUNCTION(TourRaymarch_SetWaterOffset), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetWaterScale(float, float, float)",
        asFUNCTION(TourRaymarch_SetWaterScale), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetWaterSpeed(float, float, float)",
        asFUNCTION(TourRaymarch_SetWaterSpeed), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetWaterColor(int, int, int, int)",
        asFUNCTION(TourRaymarch_SetWaterColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetWaterRipple(float, float, float, float)",
        asFUNCTION(TourRaymarch_SetWaterRipple), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetWaterWhirlpool(float, float, float, float)",
        asFUNCTION(TourRaymarch_SetWaterWhirlpool), asCALL_CDECL); AS_CHECK(r);

    // --- Fog + sun ---
    r = engine->RegisterGlobalFunction("void Raymarch_SetFogNear(float)",
        asFUNCTION(TourRaymarch_SetFogNear), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFogFar(float)",
        asFUNCTION(TourRaymarch_SetFogFar), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFogColor(int, int, int, int)",
        asFUNCTION(TourRaymarch_SetFogColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetSunBrightness(float)",
        asFUNCTION(TourRaymarch_SetSunBrightness), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetSunSize(float)",
        asFUNCTION(TourRaymarch_SetSunSize), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetSunDir(float, float, float)",
        asFUNCTION(TourRaymarch_SetSunDir), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetSunColor(int, int, int, int)",
        asFUNCTION(TourRaymarch_SetSunColor), asCALL_CDECL); AS_CHECK(r);

    // --- March parameters ---
    r = engine->RegisterGlobalFunction("void Raymarch_SetBounces(int)",
        asFUNCTION(TourRaymarch_SetBounces), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetSteps(int)",
        asFUNCTION(TourRaymarch_SetSteps), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetStepsPerBounce(int)",
        asFUNCTION(TourRaymarch_SetStepsPerBounce), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetNear(float)",
        asFUNCTION(TourRaymarch_SetNear), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFar(float)",
        asFUNCTION(TourRaymarch_SetFar), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetEpsilon(float)",
        asFUNCTION(TourRaymarch_SetEpsilon), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetStepMax(float)",
        asFUNCTION(TourRaymarch_SetStepMax), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetDistTotal(float)",
        asFUNCTION(TourRaymarch_SetDistTotal), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetRenderScale(float)",
        asFUNCTION(TourRaymarch_SetRenderScale), asCALL_CDECL); AS_CHECK(r);

    // --- Flags ---
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagSunShadows(bool)",
        asFUNCTION(TourRaymarch_SetFlagSunShadows), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagObjects(bool)",
        asFUNCTION(TourRaymarch_SetFlagObjects), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagObjectGlow(bool)",
        asFUNCTION(TourRaymarch_SetFlagObjectGlow), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagLights(bool)",
        asFUNCTION(TourRaymarch_SetFlagLights), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagLightShadows(bool)",
        asFUNCTION(TourRaymarch_SetFlagLightShadows), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagLightGlow(bool)",
        asFUNCTION(TourRaymarch_SetFlagLightGlow), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagLightIllumination(bool)",
        asFUNCTION(TourRaymarch_SetFlagLightIllumination), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagFrag(bool)",
        asFUNCTION(TourRaymarch_SetFlagFrag), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagFog(bool)",
        asFUNCTION(TourRaymarch_SetFlagFog), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagTerrain(bool)",
        asFUNCTION(TourRaymarch_SetFlagTerrain), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagTerrainGrid(bool)",
        asFUNCTION(TourRaymarch_SetFlagTerrainGrid), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagWater(bool)",
        asFUNCTION(TourRaymarch_SetFlagWater), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagSampleSign(bool)",
        asFUNCTION(TourRaymarch_SetFlagSampleSign), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagSampleLastStep(bool)",
        asFUNCTION(TourRaymarch_SetFlagSampleLastStep), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagSky(bool)",
        asFUNCTION(TourRaymarch_SetFlagSky), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagAO(bool)",
        asFUNCTION(TourRaymarch_SetFlagAO), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagProceduralSky(bool)",
        asFUNCTION(TourRaymarch_SetFlagProceduralSky), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagClouds(bool)",
        asFUNCTION(TourRaymarch_SetFlagClouds), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagProxy(bool)",
        asFUNCTION(TourRaymarch_SetFlagProxy), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetFlagVolumetricFog(bool)",
        asFUNCTION(TourRaymarch_SetFlagVolumetricFog), asCALL_CDECL); AS_CHECK(r);

    // --- Objects ---
    r = engine->RegisterGlobalFunction("void Raymarch_SetObjectPos(int, float, float, float)",
        asFUNCTION(TourRaymarch_SetObjectPos), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetObjectColor(int, float, float, float, float)",
        asFUNCTION(TourRaymarch_SetObjectColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetObjectType(int, int)",
        asFUNCTION(TourRaymarch_SetObjectType), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetObjectParams(int, float, float, float, float)",
        asFUNCTION(TourRaymarch_SetObjectParams), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetObjectScale(int, float, float, float)",
        asFUNCTION(TourRaymarch_SetObjectScale), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetObjectRotate(int, float, float, float)",
        asFUNCTION(TourRaymarch_SetObjectRotate), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetObjectAmount(int)",
        asFUNCTION(TourRaymarch_SetObjectAmount), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_RemoveObject(int)",
        asFUNCTION(TourRaymarch_RemoveObject), asCALL_CDECL); AS_CHECK(r);

    // --- Lights ---
    r = engine->RegisterGlobalFunction("void Raymarch_SetLightPos(int, float, float, float)",
        asFUNCTION(TourRaymarch_SetLightPos), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetLightColor(int, float, float, float, float)",
        asFUNCTION(TourRaymarch_SetLightColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetLightBrightness(int, float)",
        asFUNCTION(TourRaymarch_SetLightBrightness), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetLightAmount(int)",
        asFUNCTION(TourRaymarch_SetLightAmount), asCALL_CDECL); AS_CHECK(r);

    // --- Misc + state ---
    r = engine->RegisterGlobalFunction("void Raymarch_SetAmbientColor(int, int, int, int)",
        asFUNCTION(TourRaymarch_SetAmbientColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetNullColor(int, int, int, int)",
        asFUNCTION(TourRaymarch_SetNullColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetTime(float)",
        asFUNCTION(TourRaymarch_SetTime), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetUseInternalTime(bool)",
        asFUNCTION(TourRaymarch_SetUseInternalTime), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SetUseInternalCamera(bool)",
        asFUNCTION(TourRaymarch_SetUseInternalCamera), asCALL_CDECL); AS_CHECK(r);

    // --- Presets ---
    r = engine->RegisterGlobalFunction("void Raymarch_LoadDefault()",
        asFUNCTION(TourRaymarch_LoadDefault), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_LoadRandom()",
        asFUNCTION(TourRaymarch_LoadRandom), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_LoadBuiltIn(int)",
        asFUNCTION(TourRaymarch_LoadBuiltIn), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_LoadPreset(const string &in)",
        asFUNCTION(TourRaymarch_LoadPreset), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Raymarch_SavePreset(const string &in)",
        asFUNCTION(TourRaymarch_SavePreset), asCALL_CDECL); AS_CHECK(r);

    {
        static gX_I32 cDefault = 0;
        static gX_I32 cIsland = 1;
        static gX_I32 cWater = 2;
        static gX_I32 cFlatlands = 3;
        static gX_I32 cDesert = 4;
        static gX_I32 cAlien = 5;
        r = engine->RegisterGlobalProperty("const int RM_PRESET_DEFAULT", (void *)&cDefault); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int RM_PRESET_ISLAND", (void *)&cIsland); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int RM_PRESET_WATER", (void *)&cWater); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int RM_PRESET_FLATLANDS", (void *)&cFlatlands); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int RM_PRESET_DESERT", (void *)&cDesert); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int RM_PRESET_ALIEN", (void *)&cAlien); AS_CHECK(r);
    }

    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "registered tour raymarch API (terrain/water/sun/flags/objects/presets)");
    return 0;
}
