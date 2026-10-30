/**********************************************************************************************
*
*   [gX: src/scripting/tour/tour_gfx.cpp]
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


//ai: tour gfx API (gX-native remake of gameX's GfxSet* / model / light
//   surface). Modules live in renderer heaps — the GFX instance is
//   renderer.gfx.data — and feature flags go through one readable name table
//   over gfx->state; structured settings are typed setters; models and lights
//   use the renderer's dynamic scene heaps.
//opencode: raylib headers FIRST with C++ linkage — renderer headers reach
//   raylib through wrappers' extern "C" blocks, which would give raymath's
//   operator overloads C linkage (see src/tools/gfx/gfx_editor.cpp).
#include "raylib.h"
#include "raymath.h"

#include "tour.h"

#include <angelscript.h>

#include <string>
#include <string.h>

#include "../../common/log.h"
#include "../../main.h"
#include "../../renderer/renderer.h"
#include "../../gxmath/vector3.h"

static gX_RendererGFX *TourGfx_Instance(void) {
    if (!renderer.gfx.data || renderer.gfx.amount == 0) return NULL;
    return (gX_RendererGFX*)renderer.gfx.data;
}

// ===========================================================================
// Feature flags — readable names over gfx->state
// ===========================================================================

static gX_I1 *TourGfx_FlagPtr(gX_RendererGFX *gfx, const char *name) {
    if (!strcmp(name, "fog")) return &gfx->state.ENABLE_FOG;
    if (!strcmp(name, "shadows")) return &gfx->state.ENABLE_SHADOWS;
    if (!strcmp(name, "point_shadows")) return &gfx->state.ENABLE_POINT_SHADOWS;
    if (!strcmp(name, "reflections")) return &gfx->state.ENABLE_REFLECTIONS;
    if (!strcmp(name, "planar_reflections")) return &gfx->state.ENABLE_PLANAR_REFLECTIONS;
    if (!strcmp(name, "skybox")) return &gfx->state.ENABLE_SKYBOX;
    if (!strcmp(name, "procedural_sky")) return &gfx->state.ENABLE_PROCEDURAL_SKY;
    if (!strcmp(name, "bloom")) return &gfx->state.ENABLE_BLOOM;
    if (!strcmp(name, "ssr")) return &gfx->state.ENABLE_SSR;
    if (!strcmp(name, "ssr_auxview")) return &gfx->state.ENABLE_SSR_AUXVIEW;
    if (!strcmp(name, "ssao")) return &gfx->state.ENABLE_SSAO;
    if (!strcmp(name, "motion_blur")) return &gfx->state.ENABLE_MOTION_BLUR;
    if (!strcmp(name, "dof")) return &gfx->state.ENABLE_DOF;
    if (!strcmp(name, "taa")) return &gfx->state.ENABLE_TAA;
    if (!strcmp(name, "fxaa")) return &gfx->state.ENABLE_FXAA;
    if (!strcmp(name, "rim")) return &gfx->state.ENABLE_RIM;
    if (!strcmp(name, "cookies")) return &gfx->state.ENABLE_COOKIES;
    if (!strcmp(name, "flat")) return &gfx->state.ENABLE_FLAT;
    if (!strcmp(name, "gouraud")) return &gfx->state.ENABLE_GOURAUD;
    if (!strcmp(name, "phong")) return &gfx->state.ENABLE_PHONG;
    if (!strcmp(name, "lighting_ambient")) return &gfx->state.ENABLE_LIGHTING_AMBIENT;
    if (!strcmp(name, "lighting_diffuse")) return &gfx->state.ENABLE_LIGHTING_DIFFUSE;
    if (!strcmp(name, "lighting_specular")) return &gfx->state.ENABLE_LIGHTING_SPECULAR;
    if (!strcmp(name, "map_albedo")) return &gfx->state.ENABLE_MAP_ALBEDO;
    if (!strcmp(name, "map_normal")) return &gfx->state.ENABLE_MAP_NORMAL;
    if (!strcmp(name, "map_metalness")) return &gfx->state.ENABLE_MAP_METALNESS;
    if (!strcmp(name, "map_parallax")) return &gfx->state.ENABLE_MAP_PARALLAX;
    if (!strcmp(name, "detail")) return &gfx->state.ENABLE_DETAIL;
    if (!strcmp(name, "depth_vis")) return &gfx->state.SHOW_DEPTH_VISUALIZER;
    if (!strcmp(name, "ssr_vis")) return &gfx->state.SHOW_SSR_VISUALIZER;
    if (!strcmp(name, "fogsprites")) return &gfx->state.ENABLE_FOGSPRITE;
    if (!strcmp(name, "volume_fog")) return &gfx->state.ENABLE_VOLUME_FOG;
    if (!strcmp(name, "godrays")) return &gfx->state.ENABLE_GODRAYS;
    if (!strcmp(name, "lens_flare")) return &gfx->state.ENABLE_LENS_FLARE;
    if (!strcmp(name, "particles")) return &gfx->state.ENABLE_PARTICLES;
    if (!strcmp(name, "decals")) return &gfx->state.ENABLE_DECALS;
    if (!strcmp(name, "water")) return &gfx->state.ENABLE_WATER;
    if (!strcmp(name, "rain")) return &gfx->state.ENABLE_RAIN;
    if (!strcmp(name, "snowfall")) return &gfx->state.ENABLE_SNOWFALL;
    if (!strcmp(name, "splat")) return &gfx->state.ENABLE_SPLAT;
    if (!strcmp(name, "wireframe")) return &gfx->state.ENABLE_WIREFRAME;
    if (!strcmp(name, "clustered")) return &gfx->state.ENABLE_CLUSTERED;
    if (!strcmp(name, "bindless")) return &gfx->state.ENABLE_BINDLESS;
    if (!strcmp(name, "instancing")) return &gfx->state.ENABLE_INSTANCING;
    if (!strcmp(name, "frustum_culling")) return &gfx->state.ENABLE_FRUSTUM_CULLING;
    if (!strcmp(name, "occlusion_culling")) return &gfx->state.ENABLE_OCCLUSION_CULLING;
    if (!strcmp(name, "stencil")) return &gfx->state.ENABLE_STENCIL;
    if (!strcmp(name, "stencil_outline")) return &gfx->state.ENABLE_STENCIL_OUTLINE;
    if (!strcmp(name, "tonemap_aces")) return &gfx->state.ENABLE_ACES_TONEMAP;
    if (!strcmp(name, "tonemap_agx")) return &gfx->state.ENABLE_AGX_TONEMAP;
    if (!strcmp(name, "gamma_correction")) return &gfx->state.ENABLE_GAMMA_CORRECTION;
    if (!strcmp(name, "env_ambient")) return &gfx->state.ENABLE_ENV_AMBIENT;
    return NULL;
}

static gX_I1 TourGfx_FlagGet(const std::string &name) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (!gfx) return 0;
    gX_I1 *flag = TourGfx_FlagPtr(gfx, name.c_str());
    return flag ? *flag : 0;
}

static void TourGfx_FlagSet(const std::string &name, gX_I1 value) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (!gfx) return;
    gX_I1 *flag = TourGfx_FlagPtr(gfx, name.c_str());
    if (!flag) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "Gfx_Flag_Set: unknown flag '%s'", name.c_str());
        return;
    }
    *flag = value ? 1 : 0;
}

// ===========================================================================
// Structured settings
// ===========================================================================

static void TourGfx_SetFogNear(gX_F32 near) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gfx->fog.near = near;
}

static void TourGfx_SetFogFar(gX_F32 far) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gfx->fog.far = far;
}

static void TourGfx_SetFogColor(gX_I32 r, gX_I32 g, gX_I32 b, gX_I32 a) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gfx->fog.color = (gX_Color){ (gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a };
}

static void TourGfx_SetSkyStrength(gX_F32 strength) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gfx->sky.strength = strength;
}

static void TourGfx_SetSkyColor(gX_I32 r, gX_I32 g, gX_I32 b, gX_I32 a) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gfx->sky.color = (gX_Color){ (gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a };
}

static void TourGfx_SetSkyAtmosphere(gX_F32 altitude, gX_F32 turbidity,
                                     gX_F32 density, gX_F32 horizon,
                                     gX_F32 sunIntensity, gX_F32 contrast) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (!gfx) return;
    gfx->sky.altitude = altitude;
    gfx->sky.turbidity = turbidity;
    gfx->sky.density = density;
    gfx->sky.horizon = horizon;
    gfx->sky.sunIntensity = sunIntensity;
    gfx->sky.contrast = contrast;
}

static void TourGfx_SetSunDir(gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gfx->sun.dir = gX_F32v3_Make(x, y, z);
}

static void TourGfx_SetSunColor(gX_I32 r, gX_I32 g, gX_I32 b, gX_I32 a) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gfx->sun.diffuseTint = (gX_Color){ (gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a };
}

static void TourGfx_SetSunBrightness(gX_F32 brightness) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gfx->sun.brightness = brightness;
}

static void TourGfx_SetBloomThreshold(gX_F32 v) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gfx->bloom.threshold = v;
}

static void TourGfx_SetBloomIntensity(gX_F32 v) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gfx->bloom.intensity = v;
}

static void TourGfx_SetBloomBlur(gX_F32 v) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gfx->bloom.blurAmount = v;
}

static void TourGfx_SetBloomExposure(gX_F32 v) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gfx->bloom.exposure = v;
}

static void TourGfx_SetSSAORadius(gX_F32 v) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gfx->ssao.radius = v;
}

static void TourGfx_SetSSAOStrength(gX_F32 v) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gfx->ssao.strength = v;
}

static void TourGfx_SetCascadeCount(gX_I32 count) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (!gfx) return;
    if (count < 1) count = 1;
    if (count > 4) count = 4;
    gfx->csm.cascadeCount = count;
}

// ===========================================================================
// Models + lights (dynamic scene heaps)
// ===========================================================================

static gX_RendererGFX_Model *TourGfx_Model(gX_I32 index) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (!gfx) return NULL;
    if (index < 0 || (gX_U32)index >= gfx->models.amount) return NULL;
    if (!gfx->models.meta[index].USED) return NULL;
    return (gX_RendererGFX_Model*)gX_Heap_SeekPtr_Index(&gfx->models, (gX_U32)index);
}

static gX_RendererGFX_Light *TourGfx_Light(gX_I32 index) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (!gfx) return NULL;
    if (index < 0 || (gX_U32)index >= gfx->lights.amount) return NULL;
    if (!gfx->lights.meta[index].USED) return NULL;
    return (gX_RendererGFX_Light*)gX_Heap_SeekPtr_Index(&gfx->lights, (gX_U32)index);
}

static gX_I32 TourGfx_ModelAdd(const std::string &path) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (!gfx) return -1;
    gX_I32 index = gX_RendererGFX_Model_Add(gfx, path.c_str());
    if (index < 0) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "Model_Add: failed for '%s'", path.c_str());
    }
    return index;
}

static void TourGfx_ModelSetPos(gX_I32 index, const gX_F32v3 &pos) {
    gX_RendererGFX_Model *model = TourGfx_Model(index);
    if (model) model->params.pos = pos;
}

static void TourGfx_ModelSetRotate(gX_I32 index, const gX_F32v3 &rotation) {
    gX_RendererGFX_Model *model = TourGfx_Model(index);
    if (model) model->params.rotation = rotation;
}

static void TourGfx_ModelSetScale(gX_I32 index, const gX_F32v3 &scale) {
    gX_RendererGFX_Model *model = TourGfx_Model(index);
    if (model) model->params.scale = scale;
}

static void TourGfx_ModelRemove(gX_I32 index) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gX_RendererGFX_Model_Remove(gfx, (gX_U32)index);
}

static void TourGfx_ModelClear(void) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gX_RendererGFX_Model_RemoveAll(gfx);
}

static gX_I32 TourGfx_LightAdd(void) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (!gfx) return -1;
    return gX_RendererGFX_Light_Add(gfx);
}

static void TourGfx_LightSetPos(gX_I32 index, const gX_F32v3 &pos) {
    gX_RendererGFX_Light *light = TourGfx_Light(index);
    if (light) light->pos = pos;
}

static void TourGfx_LightSetColor(gX_I32 index, gX_I32 r, gX_I32 g, gX_I32 b, gX_I32 a) {
    gX_RendererGFX_Light *light = TourGfx_Light(index);
    if (light) light->diffuseTint = (gX_Color){ (gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a };
}

static void TourGfx_LightSetBrightness(gX_I32 index, gX_F32 brightness) {
    gX_RendererGFX_Light *light = TourGfx_Light(index);
    if (light) light->brightness = brightness;
}

static void TourGfx_LightRemove(gX_I32 index) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gX_RendererGFX_Light_Remove(gfx, (gX_U32)index);
}

static void TourGfx_LightClear(void) {
    gX_RendererGFX *gfx = TourGfx_Instance();
    if (gfx) gX_RendererGFX_Light_RemoveAll(gfx);
}

// ===========================================================================
// Registration
// ===========================================================================

#define AS_CHECK(r) do { if (r < 0) \
    gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "AS tour-gfx registration failed (code %d)", r); \
} while(0)

gX_I32 gX_TourGfx_Register(asIScriptEngine *engine) {
    int r = 0;

    r = engine->RegisterGlobalFunction("bool Gfx_Flag_Get(const string &in)",
        asFUNCTION(TourGfx_FlagGet), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_Flag_Set(const string &in, bool)",
        asFUNCTION(TourGfx_FlagSet), asCALL_CDECL); AS_CHECK(r);

    r = engine->RegisterGlobalFunction("void Gfx_SetFogNear(float)",
        asFUNCTION(TourGfx_SetFogNear), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_SetFogFar(float)",
        asFUNCTION(TourGfx_SetFogFar), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_SetFogColor(int, int, int, int)",
        asFUNCTION(TourGfx_SetFogColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_SetSkyStrength(float)",
        asFUNCTION(TourGfx_SetSkyStrength), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_SetSkyColor(int, int, int, int)",
        asFUNCTION(TourGfx_SetSkyColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_SetSkyAtmosphere(float, float, float, float, float, float)",
        asFUNCTION(TourGfx_SetSkyAtmosphere), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_SetSunDir(float, float, float)",
        asFUNCTION(TourGfx_SetSunDir), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_SetSunColor(int, int, int, int)",
        asFUNCTION(TourGfx_SetSunColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_SetSunBrightness(float)",
        asFUNCTION(TourGfx_SetSunBrightness), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_SetBloomThreshold(float)",
        asFUNCTION(TourGfx_SetBloomThreshold), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_SetBloomIntensity(float)",
        asFUNCTION(TourGfx_SetBloomIntensity), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_SetBloomBlur(float)",
        asFUNCTION(TourGfx_SetBloomBlur), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_SetBloomExposure(float)",
        asFUNCTION(TourGfx_SetBloomExposure), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_SetSSAORadius(float)",
        asFUNCTION(TourGfx_SetSSAORadius), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_SetSSAOStrength(float)",
        asFUNCTION(TourGfx_SetSSAOStrength), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Gfx_SetCascadeCount(int)",
        asFUNCTION(TourGfx_SetCascadeCount), asCALL_CDECL); AS_CHECK(r);

    r = engine->RegisterGlobalFunction("int Model_Add(const string &in)",
        asFUNCTION(TourGfx_ModelAdd), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Model_SetPos(int, const Vec3 &in)",
        asFUNCTION(TourGfx_ModelSetPos), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Model_SetRotate(int, const Vec3 &in)",
        asFUNCTION(TourGfx_ModelSetRotate), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Model_SetScale(int, const Vec3 &in)",
        asFUNCTION(TourGfx_ModelSetScale), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Model_Remove(int)",
        asFUNCTION(TourGfx_ModelRemove), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Model_Clear()",
        asFUNCTION(TourGfx_ModelClear), asCALL_CDECL); AS_CHECK(r);

    r = engine->RegisterGlobalFunction("int Light_Add()",
        asFUNCTION(TourGfx_LightAdd), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Light_SetPos(int, const Vec3 &in)",
        asFUNCTION(TourGfx_LightSetPos), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Light_SetColor(int, int, int, int, int)",
        asFUNCTION(TourGfx_LightSetColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Light_SetBrightness(int, float)",
        asFUNCTION(TourGfx_LightSetBrightness), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Light_Remove(int)",
        asFUNCTION(TourGfx_LightRemove), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Light_Clear()",
        asFUNCTION(TourGfx_LightClear), asCALL_CDECL); AS_CHECK(r);

    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "registered tour gfx API (flags, fog/sky/sun/bloom/ssao, models, lights)");
    return 0;
}
