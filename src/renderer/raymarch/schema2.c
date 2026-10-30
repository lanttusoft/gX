/**********************************************************************************************
*
*   [gX: src/renderer/raymarch/schema2.c]
*   [renderer/raymarch/schema2.c] — Raymarch offset-based schema (native gX port)
*
*   Copyright (c) 2025 lanttu
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

//opencode: native gX implementation of the raymarch preset schema. Field
//   tables (state/parameters/fog/sun/terrain/water/flags/misc/debug) run
//   through gX_Schema_JSON_SaveToObj/LoadFromObj; the fixed-size objects[8]
//   and lights[8] arrays are bridged through json-c like the background
//   renderer's schema does for its param sections.
#include "schema2.h"

#include <stdio.h>
#include <time.h>

#include "../../common/schema.h"
#include "../../common/marshal/schema_json.h"
#include "../../common/marshal/json_impl.h"
#include "../../common/log.h"

#include "../raymarch.h"
//ai: roughness imports are finite and bounded.
#include <math.h>

// ===========================================================================
// Field arrays — flat sections
// ===========================================================================

static gX_SchemaField rm_state_fields[] = {
    GX_SCHEMA_BOOL("useInternalTime",   gX_Raymarch_State, useInternalTime),
    GX_SCHEMA_BOOL("useInternalCamera", gX_Raymarch_State, useInternalCamera),
    GX_SCHEMA_BOOL("colorIsRGB",        gX_Raymarch_State, colorIsRGB),
    GX_SCHEMA_F32("time",               gX_Raymarch_State, time),
};

static gX_SchemaField rm_parameters_fields[] = {
    GX_SCHEMA_I32("bounces",        gX_Raymarch_Parameters, bounces),
    GX_SCHEMA_I32("steps",          gX_Raymarch_Parameters, steps),
    GX_SCHEMA_I32("stepsPerBounce", gX_Raymarch_Parameters, stepsPerBounce),
    GX_SCHEMA_F32("near",           gX_Raymarch_Parameters, near),
    GX_SCHEMA_F32("far",            gX_Raymarch_Parameters, far),
    GX_SCHEMA_F32("stepMax",        gX_Raymarch_Parameters, stepMax),
    GX_SCHEMA_F32("distTotal",      gX_Raymarch_Parameters, distTotal),
    GX_SCHEMA_F32("epsilon",        gX_Raymarch_Parameters, epsilon),
    GX_SCHEMA_F32("renderScale",    gX_Raymarch_Parameters, renderScale),
    GX_SCHEMA_ARRAY("correctionPolynomial", gX_Raymarch_Parameters,
                    correctionPolynomial, F32, GX_RM_POLYNOMIAL_SIZE),
};

static gX_SchemaField rm_fog_fields[] = {
    GX_SCHEMA_F32("near",  gX_Raymarch_Fog, near),
    GX_SCHEMA_F32("far",   gX_Raymarch_Fog, far),
    GX_SCHEMA_COLOR("color", gX_Raymarch_Fog, color),
};

static gX_SchemaField rm_sun_fields[] = {
    GX_SCHEMA_F32("brightness", gX_Raymarch_Sun, brightness),
    GX_SCHEMA_F32("size",       gX_Raymarch_Sun, size),
    GX_SCHEMA_V3("dir",         gX_Raymarch_Sun, dir),
    GX_SCHEMA_COLOR("color",    gX_Raymarch_Sun, color),
};

static gX_SchemaField rm_terrain_fields[] = {
    GX_SCHEMA_V3("offset",        gX_Raymarch_Terrain, offset),
    GX_SCHEMA_V3("scale",         gX_Raymarch_Terrain, scale),
    GX_SCHEMA_V4("rotate",        gX_Raymarch_Terrain, rotate),
    GX_SCHEMA_I32("seed",         gX_Raymarch_Terrain, seed),
    GX_SCHEMA_COLOR("color",      gX_Raymarch_Terrain, color),
    GX_SCHEMA_COLOR("colorSteep", gX_Raymarch_Terrain, colorSteep),
    GX_SCHEMA_COLOR("colorBeach", gX_Raymarch_Terrain, colorBeach),
    GX_SCHEMA_V4("parameters",    gX_Raymarch_Terrain, parameters),
};

static gX_SchemaField rm_water_fields[] = {
    GX_SCHEMA_V3("offset",     gX_Raymarch_Water, offset),
    GX_SCHEMA_V3("scale",      gX_Raymarch_Water, scale),
    GX_SCHEMA_V4("rotate",     gX_Raymarch_Water, rotate),
    GX_SCHEMA_V3("speed",      gX_Raymarch_Water, speed),
    GX_SCHEMA_I32("seed",      gX_Raymarch_Water, seed),
    GX_SCHEMA_COLOR("color",   gX_Raymarch_Water, color),
    GX_SCHEMA_V4("texturing",  gX_Raymarch_Water, texturing),
    GX_SCHEMA_V4("ripple",     gX_Raymarch_Water, ripple),
    GX_SCHEMA_V4("whirlpool",  gX_Raymarch_Water, whirlpool),
    GX_SCHEMA_F32("gradClamp", gX_Raymarch_Water, gradClamp),
};

static gX_SchemaField rm_flags_fields[] = {
    GX_SCHEMA_BOOL("enableSunShadows",        gX_Raymarch_Flags, enableSunShadows),
    GX_SCHEMA_BOOL("enableObjects",           gX_Raymarch_Flags, enableObjects),
    GX_SCHEMA_BOOL("enableObjectGlow",        gX_Raymarch_Flags, enableObjectGlow),
    GX_SCHEMA_BOOL("enableLights",            gX_Raymarch_Flags, enableLights),
    GX_SCHEMA_BOOL("enableLightShadows",      gX_Raymarch_Flags, enableLightShadows),
    GX_SCHEMA_BOOL("enableLightGlow",         gX_Raymarch_Flags, enableLightGlow),
    GX_SCHEMA_BOOL("enableLightIllumination", gX_Raymarch_Flags, enableLightIllumination),
    GX_SCHEMA_BOOL("enableFrag",              gX_Raymarch_Flags, enableFrag),
    GX_SCHEMA_BOOL("enableFog",               gX_Raymarch_Flags, enableFog),
    GX_SCHEMA_BOOL("enableTerrain",           gX_Raymarch_Flags, enableTerrain),
    GX_SCHEMA_BOOL("enableTerrainGrid",       gX_Raymarch_Flags, enableTerrainGrid),
    GX_SCHEMA_BOOL("enableWater",             gX_Raymarch_Flags, enableWater),
    GX_SCHEMA_BOOL("enableSampleSign",        gX_Raymarch_Flags, enableSampleSign),
    GX_SCHEMA_BOOL("enableSampleLastStep",    gX_Raymarch_Flags, enableSampleLastStep),
    GX_SCHEMA_BOOL("enableSky",               gX_Raymarch_Flags, enableSky),
    GX_SCHEMA_BOOL("enableAO",                gX_Raymarch_Flags, enableAO),
    GX_SCHEMA_BOOL("enableProceduralSky",     gX_Raymarch_Flags, enableProceduralSky),
    GX_SCHEMA_BOOL("enableClouds",            gX_Raymarch_Flags, enableClouds),
    GX_SCHEMA_BOOL("enableProxy",             gX_Raymarch_Flags, enableProxy),
    GX_SCHEMA_BOOL("enableVolumetricFog",     gX_Raymarch_Flags, enableVolumetricFog),
};

//ai: volumetric height fog + sun shafts (opt-in; off by default).
static gX_SchemaField rm_volfog_fields[] = {
    GX_SCHEMA_F32("density",       gX_Raymarch_VolFog, density),
    GX_SCHEMA_F32("heightFalloff", gX_Raymarch_VolFog, heightFalloff),
    GX_SCHEMA_F32("scattering",    gX_Raymarch_VolFog, scattering),
    GX_SCHEMA_F32("hgAsymmetry",   gX_Raymarch_VolFog, hgAsymmetry),
    GX_SCHEMA_COLOR("color",       gX_Raymarch_VolFog, color),
};

//ai: volumetric cloud layer (opt-in; off by default).
static gX_SchemaField rm_clouds_fields[] = {
    GX_SCHEMA_F32("bottom",   gX_Raymarch_Clouds, bottom),
    GX_SCHEMA_F32("top",      gX_Raymarch_Clouds, top),
    GX_SCHEMA_F32("coverage", gX_Raymarch_Clouds, coverage),
    GX_SCHEMA_F32("density",  gX_Raymarch_Clouds, density),
    GX_SCHEMA_F32("scale",    gX_Raymarch_Clouds, scale),
    GX_SCHEMA_F32("speed",    gX_Raymarch_Clouds, speed),
    GX_SCHEMA_F32("absorb",   gX_Raymarch_Clouds, absorb),
    GX_SCHEMA_F32("powder",   gX_Raymarch_Clouds, powder),
    GX_SCHEMA_COLOR("color",  gX_Raymarch_Clouds, color),
};

static gX_SchemaField rm_misc_fields[] = {
    GX_SCHEMA_COLOR("lightAmbient", gX_Raymarch_Misc, lightAmbient),
    GX_SCHEMA_V3("attenuation",     gX_Raymarch_Misc, attenuation),
    GX_SCHEMA_COLOR("firstColor",   gX_Raymarch_Misc, firstColor),
    GX_SCHEMA_COLOR("nullColor",    gX_Raymarch_Misc, nullColor),
};

static gX_SchemaField rm_debug_fields[] = {
    GX_SCHEMA_BOOL("showPos",         gX_Raymarch_Debug, showPos),
    GX_SCHEMA_BOOL("showDir",         gX_Raymarch_Debug, showDir),
    GX_SCHEMA_BOOL("showRay",         gX_Raymarch_Debug, showRay),
    GX_SCHEMA_BOOL("showGradient",    gX_Raymarch_Debug, showGradient),
    GX_SCHEMA_BOOL("showNormal",      gX_Raymarch_Debug, showNormal),
    GX_SCHEMA_BOOL("showBounceCount", gX_Raymarch_Debug, showBounceCount),
    GX_SCHEMA_BOOL("showPathSteps",   gX_Raymarch_Debug, showPathSteps),
    GX_SCHEMA_BOOL("showPathLength",  gX_Raymarch_Debug, showPathLength),
    GX_SCHEMA_BOOL("showDist",        gX_Raymarch_Debug, showDist),
};

#define RM_FIELD_COUNT(fields) \
    (gX_U32)(sizeof(fields) / sizeof(fields[0]))

//ai: flat numeric arrays reuse the scalar schema bridge; each node/domain occupies four entries.
static gX_SchemaField rm_graph_fields[] = {
    GX_SCHEMA_I32("root", gX_Raymarch_Graph, root),
    GX_SCHEMA_ARRAY("nodes", gX_Raymarch_Graph, nodes, F32, GX_RM_GRAPH_SLOTS*4),
    GX_SCHEMA_ARRAY("domains", gX_Raymarch_Graph, domains, F32, GX_RM_GRAPH_SLOTS*4),
};

static gX_SchemaField rm_root_fields[] = {
    GX_SCHEMA_OBJECT("graph", gX_RendererRaymarch, graph, rm_graph_fields, RM_FIELD_COUNT(rm_graph_fields)),
    GX_SCHEMA_OBJECT("state",      gX_RendererRaymarch, state,
                     rm_state_fields, RM_FIELD_COUNT(rm_state_fields)),
    GX_SCHEMA_OBJECT("parameters", gX_RendererRaymarch, parameters,
                     rm_parameters_fields, RM_FIELD_COUNT(rm_parameters_fields)),
    GX_SCHEMA_OBJECT("fog",        gX_RendererRaymarch, fog,
                     rm_fog_fields, RM_FIELD_COUNT(rm_fog_fields)),
    GX_SCHEMA_OBJECT("clouds",     gX_RendererRaymarch, clouds,
                     rm_clouds_fields, RM_FIELD_COUNT(rm_clouds_fields)),
    GX_SCHEMA_OBJECT("volfog",     gX_RendererRaymarch, volFog,
                     rm_volfog_fields, RM_FIELD_COUNT(rm_volfog_fields)),
    GX_SCHEMA_OBJECT("sun",        gX_RendererRaymarch, sun,
                     rm_sun_fields, RM_FIELD_COUNT(rm_sun_fields)),
    GX_SCHEMA_OBJECT("terrain",    gX_RendererRaymarch, terrain,
                     rm_terrain_fields, RM_FIELD_COUNT(rm_terrain_fields)),
    GX_SCHEMA_OBJECT("water",      gX_RendererRaymarch, water,
                     rm_water_fields, RM_FIELD_COUNT(rm_water_fields)),
    GX_SCHEMA_OBJECT("flags",      gX_RendererRaymarch, flags,
                     rm_flags_fields, RM_FIELD_COUNT(rm_flags_fields)),
    GX_SCHEMA_OBJECT("misc",       gX_RendererRaymarch, misc,
                     rm_misc_fields, RM_FIELD_COUNT(rm_misc_fields)),
    GX_SCHEMA_OBJECT("debug",      gX_RendererRaymarch, debug,
                     rm_debug_fields, RM_FIELD_COUNT(rm_debug_fields)),
};

// ===========================================================================
// json-c scalar/vector helpers for the inline-array bridges
// ===========================================================================

static gX_I0 rm_json_add_v3(struct json_object *obj, const gX_I8 *key,
    gX_F32v3 value)
{
    struct json_object *arr = json_object_new_array();
    json_object_array_add(arr, json_object_new_double(value.x));
    json_object_array_add(arr, json_object_new_double(value.y));
    json_object_array_add(arr, json_object_new_double(value.z));
    json_object_object_add(obj, key, arr);
}

static gX_I0 rm_json_add_v4(struct json_object *obj, const gX_I8 *key,
    gX_F32v4 value)
{
    struct json_object *arr = json_object_new_array();
    json_object_array_add(arr, json_object_new_double(value.x));
    json_object_array_add(arr, json_object_new_double(value.y));
    json_object_array_add(arr, json_object_new_double(value.z));
    json_object_array_add(arr, json_object_new_double(value.w));
    json_object_object_add(obj, key, arr);
}

static gX_I0 rm_json_read_v3(struct json_object *obj, const gX_I8 *key,
    gX_F32v3 *value)
{
    struct json_object *arr = NULL;
    if (!json_object_object_get_ex(obj, key, &arr)
        || json_object_array_length(arr) < 3) return;
    value->x = (gX_F32)json_object_get_double(
        json_object_array_get_idx(arr, 0));
    value->y = (gX_F32)json_object_get_double(
        json_object_array_get_idx(arr, 1));
    value->z = (gX_F32)json_object_get_double(
        json_object_array_get_idx(arr, 2));
}

static gX_I0 rm_json_read_v4(struct json_object *obj, const gX_I8 *key,
    gX_F32v4 *value)
{
    struct json_object *arr = NULL;
    if (!json_object_object_get_ex(obj, key, &arr)
        || json_object_array_length(arr) < 4) return;
    value->x = (gX_F32)json_object_get_double(
        json_object_array_get_idx(arr, 0));
    value->y = (gX_F32)json_object_get_double(
        json_object_array_get_idx(arr, 1));
    value->z = (gX_F32)json_object_get_double(
        json_object_array_get_idx(arr, 2));
    value->w = (gX_F32)json_object_get_double(
        json_object_array_get_idx(arr, 3));
}

static gX_I0 rm_json_read_f32(struct json_object *obj, const gX_I8 *key,
    gX_F32 *value)
{
    struct json_object *item = NULL;
    if (json_object_object_get_ex(obj, key, &item))
        *value = (gX_F32)json_object_get_double(item);
}

static gX_I0 rm_json_read_i32(struct json_object *obj, const gX_I8 *key,
    gX_I32 *value)
{
    struct json_object *item = NULL;
    if (json_object_object_get_ex(obj, key, &item))
        *value = (gX_I32)json_object_get_int(item);
}

// ===========================================================================
// Bridge — objects[GX_RM_MAX_OBJECTS] inline array
// ===========================================================================

static gX_I0 rm_objects_to_json(gX_RendererRaymarch *rm,
    struct json_object *root)
{
    struct json_object *section = json_object_new_object();
    json_object_object_add(root, "objects", section);
    json_object_object_add(section, "amount",
        json_object_new_int(rm->objects.amount));
    for (gX_I32 i = 0; i < rm->objects.amount && i < GX_RM_MAX_OBJECTS; i++) {
        gX_I8 key[32];
        snprintf((char *)key, sizeof(key), "obj[%d]", i);
        struct json_object *obj = json_object_new_object();
        json_object_object_add(obj, "type",
            json_object_new_int(rm->objects.type[i]));
        rm_json_add_v3(obj, "pos", rm->objects.pos[i]);
        rm_json_add_v3(obj, "matX", rm->objects.matX[i]);
        rm_json_add_v3(obj, "matY", rm->objects.matY[i]);
        rm_json_add_v3(obj, "matZ", rm->objects.matZ[i]);
        rm_json_add_v4(obj, "color", rm->objects.color[i]);
        rm_json_add_v4(obj, "parameters", rm->objects.parameters[i]);
        rm_json_add_v4(obj, "material", rm->objects.material[i]);
        json_object_object_add(obj,"roughness",json_object_new_double(rm->objects.roughness[i]));
        rm_json_add_v4(obj, "texturing", rm->objects.texturing[i]);
        rm_json_add_v4(obj, "lighting", rm->objects.lighting[i]);
        json_object_object_add(section, (const char *)key, obj);
    }
}

static gX_I0 rm_objects_from_json(gX_RendererRaymarch *rm,
    struct json_object *root)
{
    struct json_object *section = NULL;
    if (!json_object_object_get_ex(root, "objects", &section)) return;
    rm_json_read_i32(section, "amount", &rm->objects.amount);
    if (rm->objects.amount < 0) rm->objects.amount = 0;
    if (rm->objects.amount > GX_RM_MAX_OBJECTS)
        rm->objects.amount = GX_RM_MAX_OBJECTS;
    for (gX_I32 i = 0; i < GX_RM_MAX_OBJECTS; i++) {
        gX_I8 key[32];
        snprintf((char *)key, sizeof(key), "obj[%d]", i);
        struct json_object *obj = NULL;
        if (!json_object_object_get_ex(section, (const char *)key, &obj))
            continue;
        rm_json_read_i32(obj, "type", &rm->objects.type[i]);
        rm_json_read_v3(obj, "pos", &rm->objects.pos[i]);
        rm_json_read_v3(obj, "matX", &rm->objects.matX[i]);
        rm_json_read_v3(obj, "matY", &rm->objects.matY[i]);
        rm_json_read_v3(obj, "matZ", &rm->objects.matZ[i]);
        rm_json_read_v4(obj, "color", &rm->objects.color[i]);
        rm_json_read_v4(obj, "parameters", &rm->objects.parameters[i]);
        rm_json_read_v4(obj, "material", &rm->objects.material[i]);
        rm_json_read_f32(obj,"roughness",&rm->objects.roughness[i]);
        if (!isfinite(rm->objects.roughness[i])) rm->objects.roughness[i] = 1.0f;
        rm->objects.roughness[i] = fminf(1.0f,fmaxf(0.0f,rm->objects.roughness[i]));
        rm_json_read_v4(obj, "texturing", &rm->objects.texturing[i]);
        rm_json_read_v4(obj, "lighting", &rm->objects.lighting[i]);
    }
}

// ===========================================================================
// Bridge — lights[GX_RM_MAX_LIGHTS] inline array
// ===========================================================================

static gX_I0 rm_lights_to_json(gX_RendererRaymarch *rm,
    struct json_object *root)
{
    struct json_object *section = json_object_new_object();
    json_object_object_add(root, "lights", section);
    json_object_object_add(section, "amount",
        json_object_new_int(rm->lights.amount));
    for (gX_I32 i = 0; i < rm->lights.amount && i < GX_RM_MAX_LIGHTS; i++) {
        gX_I8 key[32];
        snprintf((char *)key, sizeof(key), "light[%d]", i);
        struct json_object *obj = json_object_new_object();
        json_object_object_add(obj, "brightness",
            json_object_new_double(rm->lights.brightness[i]));
        rm_json_add_v3(obj, "pos", rm->lights.pos[i]);
        rm_json_add_v4(obj, "color", rm->lights.color[i]);
        json_object_object_add(section, (const char *)key, obj);
    }
}

static gX_I0 rm_lights_from_json(gX_RendererRaymarch *rm,
    struct json_object *root)
{
    struct json_object *section = NULL;
    if (!json_object_object_get_ex(root, "lights", &section)) return;
    rm_json_read_i32(section, "amount", &rm->lights.amount);
    if (rm->lights.amount < 0) rm->lights.amount = 0;
    if (rm->lights.amount > GX_RM_MAX_LIGHTS)
        rm->lights.amount = GX_RM_MAX_LIGHTS;
    for (gX_I32 i = 0; i < GX_RM_MAX_LIGHTS; i++) {
        gX_I8 key[32];
        snprintf((char *)key, sizeof(key), "light[%d]", i);
        struct json_object *obj = NULL;
        if (!json_object_object_get_ex(section, (const char *)key, &obj))
            continue;
        rm_json_read_f32(obj, "brightness", &rm->lights.brightness[i]);
        rm_json_read_v3(obj, "pos", &rm->lights.pos[i]);
        rm_json_read_v4(obj, "color", &rm->lights.color[i]);
    }
}

// ===========================================================================
// Bridge — texture wrappers (skybox + tex0..2 paths/filters)
// ===========================================================================

//ai: raymarch texturing in presets — the skybox and the three object texture
//   slots round-trip by path/filter (plus the skybox layout). A changed path
//   unloads the live wrapper and requests a reload so the shared texture
//   loader re-reads the file on the next update.
static gX_I0 rm_textures_to_json(gX_RendererRaymarch *rm,
    struct json_object *root)
{
    struct json_object *section = json_object_new_object();
    json_object_object_add(root, "textures", section);
    gX_Texture_Wrapper *slots[4] = {
        &rm->texture.skybox,
        &rm->texture.textures[0],
        &rm->texture.textures[1],
        &rm->texture.textures[2],
    };
    static const gX_I8 *names[4] = { "skybox", "tex0", "tex1", "tex2" };
    for (gX_I32 i = 0; i < 4; i++) {
        struct json_object *obj = json_object_new_object();
        json_object_object_add(section, names[i], obj);
        json_object_object_add(obj, "path",
            json_object_new_string((const char *)slots[i]->path));
        json_object_object_add(obj, "filter",
            json_object_new_int((gX_I32)slots[i]->filter));
        if (i == 0) {
            json_object_object_add(obj, "isSkybox",
                json_object_new_boolean(slots[i]->isSkybox));
            json_object_object_add(obj, "layout",
                json_object_new_int((gX_I32)slots[i]->skyboxLayout));
        }
    }
}

static gX_I0 rm_texture_slot_from_json(gX_RendererRaymarch *rm,
    struct json_object *section, const gX_I8 *key, gX_Texture_Wrapper *wrapper,
    gX_I1 skybox)
{
    struct json_object *obj = NULL;
    if (!json_object_object_get_ex(section, key, &obj)) return;
    struct json_object *item = NULL;
    if (json_object_object_get_ex(obj, "path", &item)) {
        const char *value = json_object_get_string(item);
        if (value) {
            //packed-long: module helper applies the path + reload request.
            gX_RendererRaymarch_Texture_SetPath(rm, skybox ? -1
                : (gX_I32)(wrapper - rm->texture.textures), value);
        }
    }
    if (json_object_object_get_ex(obj, "filter", &item)) {
        gX_I32 filter = (gX_I32)json_object_get_int(item);
        if (filter != wrapper->filter) {
            wrapper->filter = filter;
            wrapper->filterChanged = true;
        }
    }
    if (skybox) {
        if (json_object_object_get_ex(obj, "isSkybox", &item))
            wrapper->isSkybox = json_object_get_boolean(item) ? 1 : 0;
        if (json_object_object_get_ex(obj, "layout", &item))
            wrapper->skyboxLayout = (gX_I32)json_object_get_int(item);
    }
}

static gX_I0 rm_textures_from_json(gX_RendererRaymarch *rm,
    struct json_object *root)
{
    struct json_object *section = NULL;
    if (!json_object_object_get_ex(root, "textures", &section)) return;
    rm_texture_slot_from_json(rm, section, "skybox", &rm->texture.skybox, 1);
    rm_texture_slot_from_json(rm, section, "tex0", &rm->texture.textures[0], 0);
    rm_texture_slot_from_json(rm, section, "tex1", &rm->texture.textures[1], 0);
    rm_texture_slot_from_json(rm, section, "tex2", &rm->texture.textures[2], 0);
}

// ===========================================================================
// Preset save / load
// ===========================================================================

gX_I32 gX_RendererRaymarch_Preset_Save_New(gX_RendererRaymarch *rm,
    const gX_I8 *path)
{
    //ai: malformed graphs are an explicit save failure, not silently serialized programs.
    if (!gX_RendererRaymarch_Graph_Compile(rm))
    {
        gX_TraceLog_RENDERER(gX_LOG_ERROR,"RENDERER_RM: Graph save refused: %s",rm->graph.error);
        return 1;
    }
    gX_TraceLog_RENDERER(gX_LOG_INFO,
        "RENDERER_RM: Saving preset (schema) to %s", (const char *)path);
    struct json_object *root = json_object_new_object();
    json_object_object_add(root, "name",
        json_object_new_string((const char *)rm->name));
    json_object_object_add(root, "description",
        json_object_new_string((const char *)rm->description));
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    gX_I8 date[64], timeStr[64];
    strftime((char *)date, sizeof(date), "%Y-%m-%d", tm);
    strftime((char *)timeStr, sizeof(timeStr), "%H:%M:%S", tm);
    json_object_object_add(root, "created_date",
        json_object_new_string((const char *)date));
    json_object_object_add(root, "created_time",
        json_object_new_string((const char *)timeStr));
    gX_Schema_JSON_SaveToObj(rm_root_fields, RM_FIELD_COUNT(rm_root_fields),
        (gX_I0 *)rm, root);
    rm_objects_to_json(rm, root);
    rm_lights_to_json(rm, root);
    rm_textures_to_json(rm, root);
    gX_I32 flags = JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY
        | JSON_C_TO_STRING_PRETTY_TAB;
    gX_I32 ret = json_object_to_file_ext((const char *)path, root, flags);
    json_object_put(root);
    gX_TraceLog_RENDERER(ret == 0 ? gX_LOG_INFO : gX_LOG_ERROR,
        "RENDERER_RM: Save %s", ret == 0 ? "done" : "failed");
    return ret == 0 ? 0 : -1;
}

gX_I32 gX_RendererRaymarch_Preset_Load_New(gX_RendererRaymarch *rm,
    const gX_I8 *path)
{
    gX_TraceLog_RENDERER(gX_LOG_INFO,
        "RENDERER_RM: Loading preset (schema) from %s", (const char *)path);
    struct json_object *root = json_object_from_file((const char *)path);
    if (!root) {
        gX_TraceLog_RENDERER(gX_LOG_ERROR,
            "RENDERER_RM: Failed to load preset from %s", (const char *)path);
        return 1;
    }
    //ai: graph-less historical presets always clear any previously edited graph.
    rm->graph = (gX_Raymarch_Graph){.root=-1};
    //ai: absence of the new property always resets stale edited values to matte.
    for (gX_I32 i = 0; i < GX_RM_MAX_OBJECTS; i++) rm->objects.roughness[i] = 1.0f;
    gX_Schema_JSON_LoadFromObj(rm_root_fields, RM_FIELD_COUNT(rm_root_fields),
        (gX_I0 *)rm, root);
    rm_objects_from_json(rm, root);
    rm_lights_from_json(rm, root);
    rm_textures_from_json(rm, root);
    json_object_put(root);
    if (!gX_RendererRaymarch_Graph_Compile(rm))
    {
        gX_TraceLog_RENDERER(gX_LOG_ERROR,"RENDERER_RM: Invalid preset graph: %s",rm->graph.error);
        return 1;
    }
    gX_TraceLog_RENDERER(gX_LOG_INFO, "RENDERER_RM: Load done");
    return 0;
}
