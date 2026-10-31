/**********************************************************************************************
*
*   [gX: src/renderer/gfx/schema.c]
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


//opencode: offset-based schema for GFX renderer (new system, coexists with old schema.c)
#include <stdlib.h>
#include "schema.h"

#include <string.h>
#include <time.h>

#include "../../common/schema.h"
#include "../../common/marshal/schema_json.h"
#include "../../common/log.h"
#include "../../common/path.h"

#include "../gfx.h"
#include "../renderer.h"
#include "../../camera.h"

// ---------------------------------------------------------------------------
// Heap callbacks — models
// ---------------------------------------------------------------------------

static gX_U32 gfx_model_count(gX_I0 *base)
{
    gX_RendererGFX *gfx = (gX_RendererGFX*)base;
    return gfx->models.amount;
}

static gX_I0* gfx_model_at(gX_I0 *base, gX_U32 index)
{
    gX_RendererGFX *gfx = (gX_RendererGFX*)base;
    if (index >= gfx->models.amount) return NULL;
    if (!gfx->models.meta[index].USED) return NULL;
    return gX_Heap_SeekPtr_Index(&gfx->models, index);
}

//opencode: use GetIndex variant to avoid separate SeekIndex call after potential realloc
static gX_I32 gfx_model_add(gX_I0 *base)
{
    gX_RendererGFX *gfx = (gX_RendererGFX*)base;
    gX_I32 index = -1;
    gX_RendererGFX_Model *model = gX_Heap_Add_FirstEmpty_NoPtr_GetIndex(&gfx->models, &index);
    if (!model) return -1;
    gX_RendererGFX_Model_Init(model, "");
    return index;
}

// ---------------------------------------------------------------------------
// Heap callbacks — per-material override entries (inside model params)
// ---------------------------------------------------------------------------

static gX_U32 gfx_material_override_count(gX_I0 *base)
{
    gX_RendererGFX_Model_Parameters *params = (gX_RendererGFX_Model_Parameters*)base;
    return (gX_U32)params->materialOverrideCount;
}

static gX_I0* gfx_material_override_at(gX_I0 *base, gX_U32 index)
{
    gX_RendererGFX_Model_Parameters *params = (gX_RendererGFX_Model_Parameters*)base;
    if ((gX_I32)index >= params->materialOverrideCount) return NULL;
    if (index >= gX_GFX_MATERIAL_OVERRIDES) return NULL;
    return &params->materialOverrides[index];
}

//opencode: preset-loaded entries get the same defaults as sidecar-loaded
//   ones (map IDs stay unresolved until Model_Load resolves them).
static gX_I32 gfx_material_override_add(gX_I0 *base)
{
    gX_RendererGFX_Model_Parameters *params = (gX_RendererGFX_Model_Parameters*)base;
    if (params->materialOverrideCount >= gX_GFX_MATERIAL_OVERRIDES) return -1;
    gX_I32 index = params->materialOverrideCount;
    gX_RendererGFX_MaterialOverride *ov = &params->materialOverrides[index];
    memset(ov, 0, sizeof(*ov));
    ov->albedo = (gX_Color){255, 255, 255, 255};
    ov->roughness = -1.0f;
    ov->metalness = -1.0f;
    ov->albedoMapID = -1;
    ov->emissiveMapID = -1;
    //ai: source-compatible macro scale; terrain blending itself defaults off.
    ov->terrainDetailScale = 0.1f;
    params->materialOverrideCount++;
    return index;
}

//opencode: material names identify override targets; remaining override values are sparse.
static gX_SchemaField gfx_material_override_fields[] = {
    GX_SCHEMA_REQUIRED_STRING("material", gX_RendererGFX_MaterialOverride, material),
    GX_SCHEMA_SPARSE_COLOR("emissiveColor", gX_RendererGFX_MaterialOverride, emissiveColor),
    GX_SCHEMA_SPARSE_F32("emissive", gX_RendererGFX_MaterialOverride, emissive),
    GX_SCHEMA_SPARSE_COLOR("albedo", gX_RendererGFX_MaterialOverride, albedo),
    GX_SCHEMA_SPARSE_F32("roughness", gX_RendererGFX_MaterialOverride, roughness),
    GX_SCHEMA_SPARSE_F32("metalness", gX_RendererGFX_MaterialOverride, metalness),
    GX_SCHEMA_SPARSE_STRING("albedoMap", gX_RendererGFX_MaterialOverride, albedoMap),
    GX_SCHEMA_SPARSE_STRING("emissiveMap", gX_RendererGFX_MaterialOverride, emissiveMap),
    //ai: preset and sidecar surfaces share the explicit terrain contract.
    GX_SCHEMA_BOOL_DEF("terrainBlend", gX_RendererGFX_MaterialOverride, terrainBlend, 0),
    GX_SCHEMA_SPARSE_STRING("terrainAlbedoMap", gX_RendererGFX_MaterialOverride, terrainAlbedoMap),
    GX_SCHEMA_SPARSE_STRING("terrainDetailMap", gX_RendererGFX_MaterialOverride, terrainDetailMap),
    GX_SCHEMA_SPARSE_F32("terrainDetailScale", gX_RendererGFX_MaterialOverride, terrainDetailScale),
};

// ---------------------------------------------------------------------------
// Heap callbacks — lights
// ---------------------------------------------------------------------------

static gX_U32 gfx_light_count(gX_I0 *base)
{
    gX_RendererGFX *gfx = (gX_RendererGFX*)base;
    return gfx->lights.amount;
}

static gX_I0* gfx_light_at(gX_I0 *base, gX_U32 index)
{
    gX_RendererGFX *gfx = (gX_RendererGFX*)base;
    if (index >= gfx->lights.amount) return NULL;
    if (!gfx->lights.meta[index].USED) return NULL;
    return gX_Heap_SeekPtr_Index(&gfx->lights, index);
}

//opencode: use GetIndex variant to avoid separate SeekIndex call after potential realloc
static gX_I32 gfx_light_add(gX_I0 *base)
{
    gX_RendererGFX *gfx = (gX_RendererGFX*)base;
    gX_I32 index = -1;
    gX_RendererGFX_Light *light = gX_Heap_Add_FirstEmpty_NoPtr_GetIndex(&gfx->lights, &index);
    if (!light) return -1;
    gX_RendererGFX_Light_Init(light);
    return index;
}

// ---------------------------------------------------------------------------
// Heap callbacks — textures
// ---------------------------------------------------------------------------

static gX_U32 gfx_texture_count(gX_I0 *base)
{
    gX_RendererGFX *gfx = (gX_RendererGFX*)base;
    return gfx->textures.amount;
}

static gX_I0* gfx_texture_at(gX_I0 *base, gX_U32 index)
{
    gX_RendererGFX *gfx = (gX_RendererGFX*)base;
    if (index >= gfx->textures.amount) return NULL;
    if (!gfx->textures.meta[index].USED) return NULL;
    return gX_Heap_SeekPtr_Index(&gfx->textures, index);
}

//opencode: use GetIndex variant to avoid separate SeekIndex call after potential realloc
static gX_I32 gfx_texture_add(gX_I0 *base)
{
    gX_RendererGFX *gfx = (gX_RendererGFX*)base;
    gX_I32 index = -1;
    gX_RendererGFX_Texture *tex = gX_Heap_Add_FirstEmpty_NoPtr_GetIndex(&gfx->textures, &index);
    if (!tex) return -1;
    gX_RendererGFX_Texture_Init(tex);
    return index;
}

//opencode: editor-authored decals share the renderer's normal heap callbacks.
static gX_U32 gfx_decal_count(gX_I0 *base)
{
    gX_RendererGFX *gfx = (gX_RendererGFX*)base;
    return gfx->decals.amount;
}

static gX_I0* gfx_decal_at(gX_I0 *base, gX_U32 index)
{
    gX_RendererGFX *gfx = (gX_RendererGFX*)base;
    if (index >= gfx->decals.amount || !gfx->decals.meta[index].USED) return NULL;
    return gX_Heap_SeekPtr_Index(&gfx->decals, index);
}

static gX_I32 gfx_decal_add(gX_I0 *base)
{
    gX_RendererGFX *gfx = (gX_RendererGFX*)base;
    gX_I32 index = -1;
    gX_RendererGFX_Decal *decal = gX_Heap_Add_FirstEmpty_NoPtr_GetIndex(&gfx->decals, &index);
    if (!decal) return -1;
    memset(decal, 0, sizeof(*decal));
    decal->normal = (gX_F32v3){0.0f, 1.0f, 0.0f};
    decal->size = 1.0f;
    decal->textureIndex = -1;
    decal->alpha = 1.0f;
    decal->expireTime = -1.0;
    return index;
}

//opencode: emitter heap callbacks operate on the nested particle subsystem.
static gX_U32 gfx_emitter_count(gX_I0 *base)
{
    gX_RendererGFX_Particle *particle = (gX_RendererGFX_Particle*)base;
    return particle->emitters.amount;
}

static gX_I0* gfx_emitter_at(gX_I0 *base, gX_U32 index)
{
    gX_RendererGFX_Particle *particle = (gX_RendererGFX_Particle*)base;
    if (index >= particle->emitters.amount || !particle->emitters.meta[index].USED) return NULL;
    return gX_Heap_SeekPtr_Index(&particle->emitters, index);
}

static gX_I32 gfx_emitter_add(gX_I0 *base)
{
    gX_RendererGFX_Particle *particle = (gX_RendererGFX_Particle*)base;
    gX_RendererGFX_Emitter *emitter = gX_Heap_Add_FirstEmpty_NoPtr(&particle->emitters);
    if (!emitter) return -1;
    memset(emitter, 0, sizeof(*emitter));
    emitter->active = 1;
    emitter->emitRate = 10.0f;
    emitter->baseVel = (gX_F32v3){0.0f, 5.0f, 0.0f};
    emitter->spread = 30.0f;
    emitter->color = (gX_F32v4){1.0f, 1.0f, 1.0f, 1.0f};
    emitter->life = 2.0f;
    emitter->speed = 2.0f;
    return (gX_I32)((gX_U8*)emitter - (gX_U8*)particle->emitters.data)
        / (gX_I32)sizeof(gX_RendererGFX_Emitter);
}

static gX_SchemaField gfx_decal_fields[] = {
    GX_SCHEMA_V3("pos", gX_RendererGFX_Decal, pos),
    GX_SCHEMA_V3("normal", gX_RendererGFX_Decal, normal),
    GX_SCHEMA_F32("size", gX_RendererGFX_Decal, size),
    GX_SCHEMA_F32("rotation", gX_RendererGFX_Decal, rotation),
    GX_SCHEMA_I32("textureIndex", gX_RendererGFX_Decal, textureIndex),
    GX_SCHEMA_F32("alpha", gX_RendererGFX_Decal, alpha),
    GX_SCHEMA_BOOL("snowLayer", gX_RendererGFX_Decal, snowLayer),
    GX_SCHEMA_F64("expireTime", gX_RendererGFX_Decal, expireTime),
};

static gX_SchemaField gfx_emitter_fields[] = {
    GX_SCHEMA_BOOL("active", gX_RendererGFX_Emitter, active),
    GX_SCHEMA_F32("emitRate", gX_RendererGFX_Emitter, emitRate),
    GX_SCHEMA_V3("pos", gX_RendererGFX_Emitter, pos),
    GX_SCHEMA_V3("baseVel", gX_RendererGFX_Emitter, baseVel),
    GX_SCHEMA_F32("spread", gX_RendererGFX_Emitter, spread),
    GX_SCHEMA_V4("color", gX_RendererGFX_Emitter, color),
    GX_SCHEMA_F32("life", gX_RendererGFX_Emitter, life),
    GX_SCHEMA_F32("speed", gX_RendererGFX_Emitter, speed),
};

static gX_SchemaField gfx_particle_fields[] = {
    GX_SCHEMA_HEAP("emitters", gX_RendererGFX_Particle, emitters,
        gfx_emitter_count, gfx_emitter_at, gfx_emitter_add,
        gfx_emitter_fields,
        (gX_U32)(sizeof(gfx_emitter_fields) / sizeof(gfx_emitter_fields[0]))),
};

static gX_SchemaField gfx_camera_fields[] = {
    GX_SCHEMA_V3("position", gX_Camera3D, position),
    GX_SCHEMA_V3("target", gX_Camera3D, target),
    GX_SCHEMA_V3("up", gX_Camera3D, up),
    GX_SCHEMA_F32("fovy", gX_Camera3D, fovy),
    GX_SCHEMA_I32("projection", gX_Camera3D, projection),
};

typedef struct gX_GFX_Editor_Preset_State {
    gX_I1 usePP;
    gX_I1 useCompositor;
} gX_GFX_Editor_Preset_State;

static gX_SchemaField gfx_editor_fields[] = {
    GX_SCHEMA_BOOL("usePP", gX_GFX_Editor_Preset_State, usePP),
    GX_SCHEMA_BOOL("useCompositor", gX_GFX_Editor_Preset_State, useCompositor),
};

// ---------------------------------------------------------------------------
// Field arrays — model parameters (69 fields)
// ---------------------------------------------------------------------------

static gX_SchemaField gfx_model_params_fields[] = {
    GX_SCHEMA_V3("pos",       gX_RendererGFX_Model_Parameters, pos),
    GX_SCHEMA_V3("scale",    gX_RendererGFX_Model_Parameters, scale),
    GX_SCHEMA_V3("rotation",  gX_RendererGFX_Model_Parameters, rotation),
    //opencode: optional model motion keys preserve defaults in older presets.
    { .key = "orbitEnabled", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, orbitEnabled),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitPaused", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, orbitPaused),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitLoop", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, orbitLoop),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitPlane", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_I32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, orbitPlane),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitCenter", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_F32V3,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, orbitCenter),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitRadius", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, orbitRadius),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitSpeed", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, orbitSpeed),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitPhase", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, orbitPhase),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitElapsed", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_F64,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, orbitElapsed),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    //opencode: waypoint motion-path keys (flat xyz triples for the F32 array).
    { .key = "pathMode", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_I32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, pathMode),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "pathWaypointCount", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_I32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, pathWaypointCount),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "pathWaypoints", .fieldType = GX_SCHEMA_ARRAY,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, pathWaypoints),
      .arrayCount = gX_GFX_MODEL_WAYPOINTS * 3,
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "pathPingPong", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, pathPingPong),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "pathSmooth", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, pathSmooth),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    //opencode: per-waypoint Bezier handles (flat xyz triples, mirrored across
    //   each point); pathBezier selects the cubic evaluation.
    { .key = "pathBezier", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, pathBezier),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "pathTangents", .fieldType = GX_SCHEMA_ARRAY,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, pathTangents),
      .arrayCount = gX_GFX_MODEL_WAYPOINTS * 3,
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    //opencode: independent in-handles + the mirror switch.
    { .key = "pathTangentsIn", .fieldType = GX_SCHEMA_ARRAY,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, pathTangentsIn),
      .arrayCount = gX_GFX_MODEL_WAYPOINTS * 3,
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "pathHandlesIndependent", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Model_Parameters,
          pathHandlesIndependent),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    //opencode: GLB clip playback (first slice) — clip index, playhead, flags.
    { .key = "animClip", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_I32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, animClip),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "animTime", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_F64,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, animTime),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "animPlaying", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, animPlaying),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "animLoop", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, animLoop),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "animSpeed", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, animSpeed),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    //opencode: non-destructive trim range (seconds; end <= 0 = clip end).
    { .key = "animStart", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, animStart),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "animEnd", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, animEnd),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    //opencode: cross-fade secondary clip + blend weight.
    { .key = "animClip2", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_I32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, animClip2),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "animBlend", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, animBlend),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    //opencode: per-bone pose overrides (11 floats per entry).
    { .key = "poseOverrideCount", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_I32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, poseOverrideCount),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "poseOverrides", .fieldType = GX_SCHEMA_ARRAY,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Model_Parameters, poseOverrides),
      .arrayCount = gX_GFX_MODEL_POSE_OVERRIDES * 11,
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },

    //opencode: lighting model — 0=Phong (default), 1=PBR. Per-model opt-in
    //   for the Cook-Torrance + IBL path. Existing presets load with Phong.
    GX_SCHEMA_BOOL("usePBR",  gX_RendererGFX_Model_Parameters, usePBR),
    GX_SCHEMA_BOOL_DEF("doubleSided", gX_RendererGFX_Model_Parameters, doubleSided, 0),
    GX_SCHEMA_BOOL_DEF("flipNormals", gX_RendererGFX_Model_Parameters, flipNormals, 0),

    GX_SCHEMA_F32("shininess", gX_RendererGFX_Model_Parameters, shininess),

    // Albedo
    GX_SCHEMA_F32("albedo",          gX_RendererGFX_Model_Parameters, albedo),
    GX_SCHEMA_COLOR("albedoTint", gX_RendererGFX_Model_Parameters, albedoTint),
    GX_SCHEMA_COLOR("albedoTintTop", gX_RendererGFX_Model_Parameters, albedoTintTop),
    GX_SCHEMA_BOOL("useTopTint", gX_RendererGFX_Model_Parameters, useTopTint),
    GX_SCHEMA_BOOL("hasAlbedoMap",   gX_RendererGFX_Model_Parameters, hasAlbedoMap),
    GX_SCHEMA_F32("albedoMapFactor", gX_RendererGFX_Model_Parameters, albedoMapFactor),
    GX_SCHEMA_V2("albedoMapScale",   gX_RendererGFX_Model_Parameters, albedoMapScale),
    GX_SCHEMA_V2("albedoMapOffset",  gX_RendererGFX_Model_Parameters, albedoMapOffset),

    // Metalness
    GX_SCHEMA_F32("metalness",          gX_RendererGFX_Model_Parameters, metalness),
    GX_SCHEMA_BOOL("hasMetalnessMap",   gX_RendererGFX_Model_Parameters, hasMetalnessMap),
    GX_SCHEMA_F32("metalnessMapFactor", gX_RendererGFX_Model_Parameters, metalnessMapFactor),
    GX_SCHEMA_V2("metalnessMapScale",   gX_RendererGFX_Model_Parameters, metalnessMapScale),
    GX_SCHEMA_V2("metalnessMapOffset",  gX_RendererGFX_Model_Parameters, metalnessMapOffset),

    // Normal
    GX_SCHEMA_BOOL("hasNormalMap",    gX_RendererGFX_Model_Parameters, hasNormalMap),
    GX_SCHEMA_F32("normalMapFactor",  gX_RendererGFX_Model_Parameters, normalMapFactor),
    GX_SCHEMA_V2("normalMapScale",    gX_RendererGFX_Model_Parameters, normalMapScale),
    GX_SCHEMA_V2("normalMapOffset",   gX_RendererGFX_Model_Parameters, normalMapOffset),

    // Height
    GX_SCHEMA_BOOL("hasHeightMap",    gX_RendererGFX_Model_Parameters, hasHeightMap),
    GX_SCHEMA_F32("heightMapFactor",  gX_RendererGFX_Model_Parameters, heightMapFactor),
    GX_SCHEMA_V2("heightMapScale",    gX_RendererGFX_Model_Parameters, heightMapScale),
    GX_SCHEMA_V2("heightMapOffset",   gX_RendererGFX_Model_Parameters, heightMapOffset),
    //opencode: parallax quality (0=simple, 1=steep, 2=steep+self-shadow)
    GX_SCHEMA_I32("parallaxMode",     gX_RendererGFX_Model_Parameters, parallaxMode),

    // Reflections
    GX_SCHEMA_BOOL("enableReflections",     gX_RendererGFX_Model_Parameters, enableReflections),
    //ai: absent opt-in preserves legacy additive environment shading.
    GX_SCHEMA_BOOL_DEF("balancedReflection", gX_RendererGFX_Model_Parameters, balancedReflection, 0),
    //opencode: SSR receiver participation is separate from direct environment reflections.
    GX_SCHEMA_BOOL("receiveSSR",             gX_RendererGFX_Model_Parameters, receiveSSR),
    GX_SCHEMA_BOOL("planarReflection",      gX_RendererGFX_Model_Parameters, planarReflection),
    GX_SCHEMA_I32("mirrorPair",             gX_RendererGFX_Model_Parameters, mirrorPair),
    GX_SCHEMA_BOOL_DEF("excludeFromPlanarCapture", gX_RendererGFX_Model_Parameters,
        excludeFromPlanarCapture, false),
    GX_SCHEMA_F32("reflectionPlaneHeight",  gX_RendererGFX_Model_Parameters, reflectionPlaneHeight),
    GX_SCHEMA_V3("reflectionNormal",        gX_RendererGFX_Model_Parameters, reflectionNormal),
    GX_SCHEMA_BOOL_DEF("reflectionPlaneFollowsModel", gX_RendererGFX_Model_Parameters,
        reflectionPlaneFollowsModel, false),
    GX_SCHEMA_BOOL("nonPlanarReflection",   gX_RendererGFX_Model_Parameters, nonPlanarReflection),
    GX_SCHEMA_F32("waveAmplitude",          gX_RendererGFX_Model_Parameters, waveAmplitude),

    // Specular
    GX_SCHEMA_V3("specularColor",           gX_RendererGFX_Model_Parameters, specularColor),

    // Emissive
    GX_SCHEMA_BOOL("hasEmissiveMap",     gX_RendererGFX_Model_Parameters, hasEmissiveMap),
    GX_SCHEMA_F32("emissiveFactor",      gX_RendererGFX_Model_Parameters, emissiveFactor),
    GX_SCHEMA_COLOR("emissiveColor",     gX_RendererGFX_Model_Parameters, emissiveColor),
    GX_SCHEMA_STRING("emissiveMaterial", gX_RendererGFX_Model_Parameters, emissiveMaterial),
    GX_SCHEMA_V2("emissiveMapScale",     gX_RendererGFX_Model_Parameters, emissiveMapScale),
    GX_SCHEMA_V2("emissiveMapOffset",    gX_RendererGFX_Model_Parameters, emissiveMapOffset),
    GX_SCHEMA_F32("emissiveMapFactor",   gX_RendererGFX_Model_Parameters, emissiveMapFactor),

    // Alpha / blending
    GX_SCHEMA_BOOL("enableAlphaTest",    gX_RendererGFX_Model_Parameters, enableAlphaTest),
    GX_SCHEMA_F32("alphaCutoff",         gX_RendererGFX_Model_Parameters, alphaCutoff),
    GX_SCHEMA_BOOL("enableBlending",     gX_RendererGFX_Model_Parameters, enableBlending),
    GX_SCHEMA_F32("blendOpacity",        gX_RendererGFX_Model_Parameters, blendOpacity),

    // PBR roughness
    GX_SCHEMA_F32("roughness",            gX_RendererGFX_Model_Parameters, roughness),
    GX_SCHEMA_BOOL("hasRoughnessMap",     gX_RendererGFX_Model_Parameters, hasRoughnessMap),
    GX_SCHEMA_F32("roughnessMapFactor",   gX_RendererGFX_Model_Parameters, roughnessMapFactor),
    GX_SCHEMA_V2("roughnessMapScale",     gX_RendererGFX_Model_Parameters, roughnessMapScale),
    GX_SCHEMA_V2("roughnessMapOffset",    gX_RendererGFX_Model_Parameters, roughnessMapOffset),

    // PBR ambient occlusion
    GX_SCHEMA_F32("ao",                  gX_RendererGFX_Model_Parameters, ao),
    GX_SCHEMA_BOOL("hasAoMap",           gX_RendererGFX_Model_Parameters, hasAoMap),
    GX_SCHEMA_F32("aoMapFactor",         gX_RendererGFX_Model_Parameters, aoMapFactor),
    GX_SCHEMA_V2("aoMapScale",           gX_RendererGFX_Model_Parameters, aoMapScale),
    GX_SCHEMA_V2("aoMapOffset",          gX_RendererGFX_Model_Parameters, aoMapOffset),

    //opencode: opt-in static lightmap sampled from TEXCOORD_1.
    GX_SCHEMA_BOOL("hasLightmap",         gX_RendererGFX_Model_Parameters, hasLightmap),
    GX_SCHEMA_I32("lightmapID",           gX_RendererGFX_Model_Parameters, lightmapID),
    GX_SCHEMA_F32("lightmapStrength",     gX_RendererGFX_Model_Parameters, lightmapStrength),
    GX_SCHEMA_V2("lightmapScale",         gX_RendererGFX_Model_Parameters, lightmapScale),
    GX_SCHEMA_V2("lightmapOffset",        gX_RendererGFX_Model_Parameters, lightmapOffset),

    // GLB water
    GX_SCHEMA_BOOL("hasWater",           gX_RendererGFX_Model_Parameters, hasWater),
    GX_SCHEMA_F32("waterScrollU",        gX_RendererGFX_Model_Parameters, waterScrollU),
    GX_SCHEMA_F32("waterScrollV",        gX_RendererGFX_Model_Parameters, waterScrollV),
    GX_SCHEMA_U32("instanceCount",       gX_RendererGFX_Model_Parameters, instanceCount),

    // Misc
    GX_SCHEMA_BOOL("hasGlossMap",        gX_RendererGFX_Model_Parameters, hasGlossMap),
    GX_SCHEMA_F32("glossMapFactor",      gX_RendererGFX_Model_Parameters, glossMapFactor),
    GX_SCHEMA_I32("glossMapID",          gX_RendererGFX_Model_Parameters, glossMapID),
    GX_SCHEMA_I32("injectedNormalMapID", gX_RendererGFX_Model_Parameters, injectedNormalMapID),
    GX_SCHEMA_F32("refractiveIndex",     gX_RendererGFX_Model_Parameters, refractiveIndex),
    GX_SCHEMA_BOOL("enableRefraction",   gX_RendererGFX_Model_Parameters, enableRefraction),
    //opencode: 3D text label
    GX_SCHEMA_BOOL("textVisible",        gX_RendererGFX_Model_Parameters, textVisible),
    GX_SCHEMA_BOOL("textBillboard",      gX_RendererGFX_Model_Parameters, textBillboard),
    GX_SCHEMA_STRING("textString",       gX_RendererGFX_Model_Parameters, textString),
    GX_SCHEMA_STRING("textFontPath",     gX_RendererGFX_Model_Parameters, textFontPath),
    GX_SCHEMA_F32("textFontSize",        gX_RendererGFX_Model_Parameters, textFontSize),
    //ai: label placement is saved with model parameters and covered by parameter history copies.
    GX_SCHEMA_V3("textOffset",           gX_RendererGFX_Model_Parameters, textOffset),
    //ai: persisted label styling/top attachment.
    GX_SCHEMA_BOOL("textAnchorTop",     gX_RendererGFX_Model_Parameters, textAnchorTop),
    GX_SCHEMA_F32("textGap",            gX_RendererGFX_Model_Parameters, textGap),
    GX_SCHEMA_V2("textWorldSize",       gX_RendererGFX_Model_Parameters, textWorldSize),
    GX_SCHEMA_BOOL("textFit",           gX_RendererGFX_Model_Parameters, textFit),
    GX_SCHEMA_COLOR("textColor",        gX_RendererGFX_Model_Parameters, textColor),
    GX_SCHEMA_COLOR("textShadowColor",  gX_RendererGFX_Model_Parameters, textShadowColor),
    GX_SCHEMA_F32("textShadowPixels",   gX_RendererGFX_Model_Parameters, textShadowPixels),

    //opencode: per-material override entries (sidecar-format data carried in
    //   presets). Empty lists serialize as [] and reload as none; map paths
    //   resolve at Model_Load (runtime IDs are never serialized).
    GX_SCHEMA_HEAP("materialOverrides", gX_RendererGFX_Model_Parameters, materialOverrides,
        gfx_material_override_count, gfx_material_override_at, gfx_material_override_add,
        gfx_material_override_fields,
        (gX_U32)(sizeof(gfx_material_override_fields) / sizeof(gfx_material_override_fields[0]))),
};

// ---------------------------------------------------------------------------
// Field arrays — model entry (path + params)
// ---------------------------------------------------------------------------

//opencode: model assets are required identifiers inside otherwise partial entries.
static gX_SchemaField gfx_model_fields[] = {
    GX_SCHEMA_REQUIRED_STRING("path", gX_RendererGFX_Model, path),
    //opencode: diagnostic texture-disable choice is part of scene authorship.
    GX_SCHEMA_BOOL("useTextures", gX_RendererGFX_Model, useTextures),
    GX_SCHEMA_OBJECT("params", gX_RendererGFX_Model, params,
                     gfx_model_params_fields,
                     (gX_U32)(sizeof(gfx_model_params_fields) / sizeof(gfx_model_params_fields[0]))),
    //opencode: texture injection slots — 8 entries, each textureID[i] is an index
    //   into gfx->textures (-1 = none) and texturePurpose[i] is a gX_MaterialMapIndex.
    //   These were previously runtime-only and got lost on preset save.
    GX_SCHEMA_ARRAY("textureID",      gX_RendererGFX_Model, textureID,      I32, gX_GFX_MODEL_TEXTURE_SLOTS),
    GX_SCHEMA_ARRAY("texturePurpose", gX_RendererGFX_Model, texturePurpose, I32, gX_GFX_MODEL_TEXTURE_SLOTS),
};

// ---------------------------------------------------------------------------
// Field arrays — light (29 fields)
// ---------------------------------------------------------------------------

static gX_SchemaField gfx_light_fields[] = {
    GX_SCHEMA_V3("pos",         gX_RendererGFX_Light, pos),
    GX_SCHEMA_V3("dir",         gX_RendererGFX_Light, dir),
    //opencode: optional motion keys preserve defaults when loading older presets.
    { .key = "orbitEnabled", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Light, orbitEnabled),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitPaused", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Light, orbitPaused),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitLoop", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Light, orbitLoop),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitPlane", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_I32,
      .offset = offsetof(gX_RendererGFX_Light, orbitPlane),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitCenter", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_F32V3,
      .offset = offsetof(gX_RendererGFX_Light, orbitCenter),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitRadius", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Light, orbitRadius),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitSpeed", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Light, orbitSpeed),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitPhase", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Light, orbitPhase),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "orbitElapsed", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_F64,
      .offset = offsetof(gX_RendererGFX_Light, orbitElapsed),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    //opencode: waypoint motion-path keys (flat xyz triples for the F32 array).
    { .key = "pathMode", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_I32,
      .offset = offsetof(gX_RendererGFX_Light, pathMode),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "pathWaypointCount", .fieldType = GX_SCHEMA_SCALAR,
      .datatype = gX_DATATYPE_I32,
      .offset = offsetof(gX_RendererGFX_Light, pathWaypointCount),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "pathWaypoints", .fieldType = GX_SCHEMA_ARRAY,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Light, pathWaypoints),
      .arrayCount = gX_GFX_LIGHT_WAYPOINTS * 3,
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "pathPingPong", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Light, pathPingPong),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "pathSmooth", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Light, pathSmooth),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    //opencode: per-waypoint Bezier handles (flat xyz triples, mirrored across
    //   each point); pathBezier selects the cubic evaluation.
    { .key = "pathBezier", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Light, pathBezier),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "pathTangents", .fieldType = GX_SCHEMA_ARRAY,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Light, pathTangents),
      .arrayCount = gX_GFX_LIGHT_WAYPOINTS * 3,
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    //opencode: independent in-handles + the mirror switch.
    { .key = "pathTangentsIn", .fieldType = GX_SCHEMA_ARRAY,
      .datatype = gX_DATATYPE_F32,
      .offset = offsetof(gX_RendererGFX_Light, pathTangentsIn),
      .arrayCount = gX_GFX_LIGHT_WAYPOINTS * 3,
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    { .key = "pathHandlesIndependent", .fieldType = GX_SCHEMA_BOOL,
      .datatype = gX_DATATYPE_I1,
      .offset = offsetof(gX_RendererGFX_Light, pathHandlesIndependent),
      .missingSeverity = GX_SCHEMA_MISSING_IGNORE },
    GX_SCHEMA_COLOR("color",    gX_RendererGFX_Light, color),
    GX_SCHEMA_F32("brightness", gX_RendererGFX_Light, brightness),
    GX_SCHEMA_I32("type",       gX_RendererGFX_Light, type),
    GX_SCHEMA_F32("fov",        gX_RendererGFX_Light, fov),
    GX_SCHEMA_COLOR("diffuseTint",    gX_RendererGFX_Light, diffuseTint),
    GX_SCHEMA_COLOR("specularTint",   gX_RendererGFX_Light, specularTint),
    GX_SCHEMA_F32("diffuseFactor",    gX_RendererGFX_Light, diffuseFactor),
    GX_SCHEMA_F32("specularFactor",   gX_RendererGFX_Light, specularFactor),
    GX_SCHEMA_F32("attenConst",       gX_RendererGFX_Light, attenConst),
    GX_SCHEMA_F32("attenLinear",      gX_RendererGFX_Light, attenLinear),
    GX_SCHEMA_F32("attenQuad",        gX_RendererGFX_Light, attenQuad),
    GX_SCHEMA_BOOL("castShadows",     gX_RendererGFX_Light, castShadows),
    GX_SCHEMA_U32("shadowSize",       gX_RendererGFX_Light, shadowSize),
    GX_SCHEMA_F32("shadowFov",        gX_RendererGFX_Light, shadowFov),
    GX_SCHEMA_F32("shadowFar",        gX_RendererGFX_Light, shadowFar),
    GX_SCHEMA_F32("shadowRange",      gX_RendererGFX_Light, shadowRange),
    GX_SCHEMA_BOOL("cookieEnabled",   gX_RendererGFX_Light, cookieEnabled),
    GX_SCHEMA_STRING("cookiePath",    gX_RendererGFX_Light, cookiePath),
};

// ---------------------------------------------------------------------------
// Field arrays — texture (path only)
// ---------------------------------------------------------------------------

//opencode: texture records require an asset path to identify their resource.
static gX_SchemaField gfx_texture_fields[] = {
    GX_SCHEMA_REQUIRED_STRING("path", gX_RendererGFX_Texture, wrapper.path),
};

// ---------------------------------------------------------------------------
// Field arrays — state toggles (41 fields)
// ---------------------------------------------------------------------------

static gX_SchemaField gfx_state_fields[] = {
    GX_SCHEMA_BOOL("SHOW_DEPTH_VISUALIZER",   gX_RendererGFX_State, SHOW_DEPTH_VISUALIZER),
    GX_SCHEMA_I32("depthVisSource",            gX_RendererGFX_State, depthVisSource),
    GX_SCHEMA_BOOL("SHOW_SSR_VISUALIZER",      gX_RendererGFX_State, SHOW_SSR_VISUALIZER),
    GX_SCHEMA_I32("ssrDebugMode",              gX_RendererGFX_State, ssrDebugMode),
    GX_SCHEMA_BOOL("ENABLE_LIGHTING_AMBIENT",  gX_RendererGFX_State, ENABLE_LIGHTING_AMBIENT),
    GX_SCHEMA_BOOL("ENABLE_LIGHTING_DIFFUSE",  gX_RendererGFX_State, ENABLE_LIGHTING_DIFFUSE),
    GX_SCHEMA_BOOL("ENABLE_LIGHTING_SPECULAR", gX_RendererGFX_State, ENABLE_LIGHTING_SPECULAR),
    GX_SCHEMA_BOOL("ENABLE_MAP_ALBEDO",        gX_RendererGFX_State, ENABLE_MAP_ALBEDO),
    GX_SCHEMA_BOOL("ENABLE_MAP_METALNESS",     gX_RendererGFX_State, ENABLE_MAP_METALNESS),
    GX_SCHEMA_BOOL("ENABLE_MAP_NORMAL",        gX_RendererGFX_State, ENABLE_MAP_NORMAL),
    GX_SCHEMA_BOOL("ENABLE_MAP_PARALLAX",      gX_RendererGFX_State, ENABLE_MAP_PARALLAX),
    GX_SCHEMA_BOOL("ENABLE_DETAIL",            gX_RendererGFX_State, ENABLE_DETAIL),
    GX_SCHEMA_BOOL("ENABLE_FLAT",              gX_RendererGFX_State, ENABLE_FLAT),
    GX_SCHEMA_BOOL("ENABLE_GOURAUD",           gX_RendererGFX_State, ENABLE_GOURAUD),
    GX_SCHEMA_BOOL("ENABLE_PHONG",             gX_RendererGFX_State, ENABLE_PHONG),
    //opencode: authored shader tuning values, separate from diagnostic hooks.
    GX_SCHEMA_F32("shadowBiasMax",             gX_RendererGFX_State, shadowBiasMax),
    GX_SCHEMA_F32("shadowBiasMin",             gX_RendererGFX_State, shadowBiasMin),
    GX_SCHEMA_F32("parallaxMinLayers",          gX_RendererGFX_State, parallaxMinLayers),
    GX_SCHEMA_F32("parallaxMaxLayers",          gX_RendererGFX_State, parallaxMaxLayers),
    GX_SCHEMA_F32("parallaxOffset",             gX_RendererGFX_State, parallaxOffset),
    GX_SCHEMA_F32("parallaxShadowOffset",       gX_RendererGFX_State, parallaxShadowOffset),
    GX_SCHEMA_F32("parallaxShadowSteps",        gX_RendererGFX_State, parallaxShadowSteps),
    GX_SCHEMA_F32("pointShadowBias",            gX_RendererGFX_State, pointShadowBias),
    GX_SCHEMA_F32("pointShadowExtra",           gX_RendererGFX_State, pointShadowExtra),
    GX_SCHEMA_F32("pointShadowMinBias",         gX_RendererGFX_State, pointShadowMinBias),
    GX_SCHEMA_F32("tangentLengthThreshold",     gX_RendererGFX_State, tangentLengthThreshold),
    GX_SCHEMA_F32("normalYThreshold",            gX_RendererGFX_State, normalYThreshold),
    GX_SCHEMA_BOOL("ENABLE_FOG",               gX_RendererGFX_State, ENABLE_FOG),
    GX_SCHEMA_BOOL("ENABLE_SHADOWS",           gX_RendererGFX_State, ENABLE_SHADOWS),
    GX_SCHEMA_BOOL("ENABLE_REFLECTIONS",       gX_RendererGFX_State, ENABLE_REFLECTIONS),
    GX_SCHEMA_BOOL_DEF("ENABLE_PLANAR_REFLECTIONS", gX_RendererGFX_State,
        ENABLE_PLANAR_REFLECTIONS, true),
    GX_SCHEMA_BOOL("ENABLE_SKYBOX",            gX_RendererGFX_State, ENABLE_SKYBOX),
    GX_SCHEMA_BOOL("ENABLE_PROCEDURAL_SKY",    gX_RendererGFX_State, ENABLE_PROCEDURAL_SKY),
    GX_SCHEMA_BOOL("ENABLE_POINT_SHADOWS",     gX_RendererGFX_State, ENABLE_POINT_SHADOWS),
    GX_SCHEMA_BOOL("ENABLE_COOKIES",           gX_RendererGFX_State, ENABLE_COOKIES),
    GX_SCHEMA_BOOL("ENABLE_BLOOM",             gX_RendererGFX_State, ENABLE_BLOOM),
    GX_SCHEMA_BOOL("ENABLE_SSR",               gX_RendererGFX_State, ENABLE_SSR),
    GX_SCHEMA_BOOL("ENABLE_SSR_AUXVIEW",       gX_RendererGFX_State, ENABLE_SSR_AUXVIEW),
    GX_SCHEMA_BOOL("ENABLE_MOTION_BLUR",       gX_RendererGFX_State, ENABLE_MOTION_BLUR),
    GX_SCHEMA_BOOL("ENABLE_DOF",               gX_RendererGFX_State, ENABLE_DOF),
    GX_SCHEMA_BOOL("ENABLE_TAA",               gX_RendererGFX_State, ENABLE_TAA),
    //opencode: SSAO toggle (tuning lives in the ssao sub-object)
    GX_SCHEMA_BOOL("ENABLE_SSAO",              gX_RendererGFX_State, ENABLE_SSAO),
    //opencode: v0.3.3-audit — schema gaps closed (env-only flags were not
    //   JSON-tunable; see docs/gfx_audit_2026-08.md finding #12)
    GX_SCHEMA_BOOL("ENABLE_FXAA",              gX_RendererGFX_State, ENABLE_FXAA),
    GX_SCHEMA_BOOL("ENABLE_CLUSTERED",         gX_RendererGFX_State, ENABLE_CLUSTERED),
    GX_SCHEMA_BOOL("ENABLE_AGX_TONEMAP",       gX_RendererGFX_State, ENABLE_AGX_TONEMAP),
    // Rim lighting
    GX_SCHEMA_BOOL("ENABLE_RIM",               gX_RendererGFX_State, ENABLE_RIM),
    GX_SCHEMA_F32("rimIntensity",               gX_RendererGFX_State, rimIntensity),
    GX_SCHEMA_F32("rimPower",                   gX_RendererGFX_State, rimPower),
    GX_SCHEMA_COLOR("rimColor",                 gX_RendererGFX_State, rimColor),
    // Environment-aware ambient
    GX_SCHEMA_BOOL("ENABLE_ENV_AMBIENT",       gX_RendererGFX_State, ENABLE_ENV_AMBIENT),
    GX_SCHEMA_F32("envAmbientFactor",           gX_RendererGFX_State, envAmbientFactor),
    //opencode: global texture filter quality — 0=POINT, 1=BILINEAR, 2=TRILINEAR,
    //   3=ANISOTROPIC_4X, 4=ANISOTROPIC_8X, 5=ANISOTROPIC_16X.
    GX_SCHEMA_I32("textureFilter",            gX_RendererGFX_State, textureFilter),
    //opencode: shader quality level — 0=LOW, 1=MEDIUM, 2=HIGH.
    GX_SCHEMA_I32("shaderQuality",            gX_RendererGFX_State, shaderQuality),
    //opencode: mipmap LOD bias — negative=sharper, positive=blurrier.
    GX_SCHEMA_F32("mipmapBias",               gX_RendererGFX_State, mipmapBias),
    //opencode: MSAA level — 0=off, 2, 4, 8.
    GX_SCHEMA_I32("msaaLevel",                gX_RendererGFX_State, msaaLevel),
    //opencode: quality preset — 0=Low, 1=Medium, 2=High, 3=Ultra.
    GX_SCHEMA_I32("qualityPreset",            gX_RendererGFX_State, qualityPreset),
    //opencode: GPU compute particles
    GX_SCHEMA_BOOL("ENABLE_PARTICLES",        gX_RendererGFX_State, ENABLE_PARTICLES),
    //opencode: CPU falling snow (GL33 weather layer)
    GX_SCHEMA_BOOL("ENABLE_SNOWFALL",         gX_RendererGFX_State, ENABLE_SNOWFALL),
    //opencode: CPU rain streaks (GL33 weather layer)
    GX_SCHEMA_BOOL("ENABLE_RAIN",             gX_RendererGFX_State, ENABLE_RAIN),
    //opencode: CPU fog sprites (GL33 weather layer)
    GX_SCHEMA_BOOL("ENABLE_FOGSPRITE",        gX_RendererGFX_State, ENABLE_FOGSPRITE),
    //opencode: gaussian splat research fixture
    GX_SCHEMA_BOOL("ENABLE_SPLAT",            gX_RendererGFX_State, ENABLE_SPLAT),
    //opencode: god rays toggle (tuning lives in the godray sub-object)
    GX_SCHEMA_BOOL("ENABLE_GODRAYS",          gX_RendererGFX_State, ENABLE_GODRAYS),
    //opencode: volumetric fog toggle (tuning lives in the fogVolume sub-object)
    GX_SCHEMA_BOOL("ENABLE_VOLUME_FOG",       gX_RendererGFX_State, ENABLE_VOLUME_FOG),
    //opencode: lens flares toggle (tuning lives in the lensflare sub-object)
    GX_SCHEMA_BOOL("ENABLE_LENS_FLARE",       gX_RendererGFX_State, ENABLE_LENS_FLARE),
    //opencode: occlusion culling
    GX_SCHEMA_BOOL("ENABLE_OCCLUSION_CULLING", gX_RendererGFX_State, ENABLE_OCCLUSION_CULLING),
    GX_SCHEMA_BOOL("ENABLE_INSTANCING",        gX_RendererGFX_State, ENABLE_INSTANCING),
    //opencode: bindless textures (GL_ARB_bindless_texture)
    GX_SCHEMA_BOOL("ENABLE_BINDLESS",          gX_RendererGFX_State, ENABLE_BINDLESS),
    //opencode: ACES filmic tone mapping
    GX_SCHEMA_BOOL("ENABLE_ACES_TONEMAP",      gX_RendererGFX_State, ENABLE_ACES_TONEMAP),
    GX_SCHEMA_BOOL("ENABLE_GAMMA_CORRECTION",  gX_RendererGFX_State, ENABLE_GAMMA_CORRECTION),
    GX_SCHEMA_BOOL("ENABLE_STENCIL",            gX_RendererGFX_State, ENABLE_STENCIL),
    GX_SCHEMA_BOOL("ENABLE_STENCIL_OUTLINE",    gX_RendererGFX_State, ENABLE_STENCIL_OUTLINE),
    GX_SCHEMA_BOOL("ENABLE_FRUSTUM_CULLING",    gX_RendererGFX_State, ENABLE_FRUSTUM_CULLING),
    GX_SCHEMA_BOOL("SHOW_POSITION",            gX_RendererGFX_State, SHOW_POSITION),
    GX_SCHEMA_BOOL("SHOW_TEXCOORD",            gX_RendererGFX_State, SHOW_TEXCOORD),
    GX_SCHEMA_BOOL("SHOW_TEXCOORD2",           gX_RendererGFX_State, SHOW_TEXCOORD2),
    GX_SCHEMA_BOOL("SHOW_NORMAL",              gX_RendererGFX_State, SHOW_NORMAL),
    GX_SCHEMA_BOOL("SHOW_COLOR",               gX_RendererGFX_State, SHOW_COLOR),
    GX_SCHEMA_BOOL("SHOW_TANGENT",             gX_RendererGFX_State, SHOW_TANGENT),
    GX_SCHEMA_BOOL("ENABLE_DECALS",            gX_RendererGFX_State, ENABLE_DECALS),
    GX_SCHEMA_BOOL("SHOW_GRID",                gX_RendererGFX_State, SHOW_GRID),
    GX_SCHEMA_BOOL("SHOW_GIZMO",               gX_RendererGFX_State, SHOW_GIZMO),
    GX_SCHEMA_BOOL("SHOW_BOUNDING_BOX",        gX_RendererGFX_State, SHOW_BOUNDING_BOX),
    GX_SCHEMA_BOOL("SHOW_SKELETON",            gX_RendererGFX_State, SHOW_SKELETON),
    GX_SCHEMA_BOOL("SHOW_LIGHT_MARKERS",       gX_RendererGFX_State, SHOW_LIGHT_MARKERS),
    GX_SCHEMA_BOOL("ENABLE_WIREFRAME",         gX_RendererGFX_State, ENABLE_WIREFRAME),
    GX_SCHEMA_BOOL("ENABLE_WATER",             gX_RendererGFX_State, ENABLE_WATER),
    GX_SCHEMA_F32("exposure",                  gX_RendererGFX_State, exposure),
};

// ---------------------------------------------------------------------------
// Field arrays — sub-object tunings (each applied at the real sub-struct base)
// ---------------------------------------------------------------------------

static gX_SchemaField gfx_ssao_fields[] = {
    GX_SCHEMA_F32("radius",      gX_RendererGFX_SSAO, radius),
    GX_SCHEMA_F32("bias",        gX_RendererGFX_SSAO, bias),
    GX_SCHEMA_F32("strength",    gX_RendererGFX_SSAO, strength),
    GX_SCHEMA_I32("kernelSize",  gX_RendererGFX_SSAO, kernelSize),
    //opencode: v0.3.3-audit — algorithm switches, previously env-only
    GX_SCHEMA_BOOL("useGTAO",    gX_RendererGFX_SSAO, useGTAO),
    GX_SCHEMA_BOOL("useSSGI",    gX_RendererGFX_SSAO, useSSGI),
    GX_SCHEMA_F32("thickness",   gX_RendererGFX_SSAO, thickness),
    GX_SCHEMA_F32("ssgiIntensity", gX_RendererGFX_SSAO, ssgiIntensity),
    GX_SCHEMA_F32("ssgiBounceGain", gX_RendererGFX_SSAO, ssgiBounceGain),
};

static gX_SchemaField gfx_csm_fields[] = {
    GX_SCHEMA_I32("cascadeCount",   gX_RendererGFX_CSM, cascadeCount),
    GX_SCHEMA_F32("distance0",      gX_RendererGFX_CSM, cascadeDistances[0]),
    GX_SCHEMA_F32("distance1",      gX_RendererGFX_CSM, cascadeDistances[1]),
    GX_SCHEMA_F32("distance2",      gX_RendererGFX_CSM, cascadeDistances[2]),
    GX_SCHEMA_F32("distance3",      gX_RendererGFX_CSM, cascadeDistances[3]),
    GX_SCHEMA_F32("blendScale",     gX_RendererGFX_CSM, blendScale),
};

static gX_SchemaField gfx_ssr_fields[] = {
    //opencode: the raster-normal path is opt-in and survives editor presets.
    GX_SCHEMA_BOOL("normalBufferEnabled", gX_RendererGFX_SSR, normalBufferEnabled),
    GX_SCHEMA_I32("maxSteps",    gX_RendererGFX_SSR, maxSteps),
    GX_SCHEMA_F32("stepSize",    gX_RendererGFX_SSR, stepSize),
    GX_SCHEMA_F32("maxDistance", gX_RendererGFX_SSR, maxDistance),
    GX_SCHEMA_F32("depthBias",   gX_RendererGFX_SSR, depthBias),
    GX_SCHEMA_F32("opacity",     gX_RendererGFX_SSR, opacity),
    GX_SCHEMA_F32("roughness",   gX_RendererGFX_SSR, roughness),
    GX_SCHEMA_F32("maxLod",      gX_RendererGFX_SSR, maxLod),
    GX_SCHEMA_F32("edgeFade",    gX_RendererGFX_SSR, edgeFade),
    GX_SCHEMA_I32("maxBounces",  gX_RendererGFX_SSR, maxBounces),
    GX_SCHEMA_F32("bounceAtten", gX_RendererGFX_SSR, bounceAtten),
    GX_SCHEMA_F32("maxDepthDelta", gX_RendererGFX_SSR, maxDepthDelta),
    GX_SCHEMA_F32("worldGapThreshold", gX_RendererGFX_SSR, worldGapThreshold),
    GX_SCHEMA_F32("finalDepthThreshold", gX_RendererGFX_SSR, finalDepthThreshold),
    GX_SCHEMA_F32("hitFacingThreshold", gX_RendererGFX_SSR, hitFacingThreshold),
};

static gX_SchemaField gfx_reflection_fields[] = {
    GX_SCHEMA_F32("bias", gX_RendererGFX_Reflection, bias),
};

static gX_SchemaField gfx_godray_fields[] = {
    GX_SCHEMA_I32("density",  gX_RendererGFX_Godray, density),
    GX_SCHEMA_F32("decay",    gX_RendererGFX_Godray, decay),
    GX_SCHEMA_F32("weight",   gX_RendererGFX_Godray, weight),
    GX_SCHEMA_F32("exposure", gX_RendererGFX_Godray, exposure),
};

static gX_SchemaField gfx_fogvolume_fields[] = {
    GX_SCHEMA_F32("density",      gX_RendererGFX_FogVolume, density),
    GX_SCHEMA_F32("heightFalloff", gX_RendererGFX_FogVolume, heightFalloff),
    GX_SCHEMA_F32("scattering",   gX_RendererGFX_FogVolume, scattering),
    GX_SCHEMA_F32("hgAsymmetry",  gX_RendererGFX_FogVolume, hgAsymmetry),
};

static gX_SchemaField gfx_lensflare_fields[] = {
    GX_SCHEMA_F32("intensity", gX_RendererGFX_LensFlare, intensity),
    GX_SCHEMA_F32("dispersal", gX_RendererGFX_LensFlare, dispersal),
};

// ---------------------------------------------------------------------------
// Field arrays — bloom (4 fields)
// ---------------------------------------------------------------------------

static gX_SchemaField gfx_bloom_fields[] = {
    GX_SCHEMA_F32("threshold",  gX_RendererGFX_Bloom, threshold),
    GX_SCHEMA_F32("intensity",  gX_RendererGFX_Bloom, intensity),
    GX_SCHEMA_F32("blurAmount", gX_RendererGFX_Bloom, blurAmount),
    GX_SCHEMA_F32("exposure",   gX_RendererGFX_Bloom, exposure),
    GX_SCHEMA_F32("vignette",   gX_RendererGFX_Bloom, vignette),
    GX_SCHEMA_F32("saturation", gX_RendererGFX_Bloom, saturation),
    GX_SCHEMA_F32("contrast",   gX_RendererGFX_Bloom, contrast),
    //opencode: v0.3.3-audit — auto-exposure toggle, previously env-only
    GX_SCHEMA_BOOL("autoExposure", gX_RendererGFX_Bloom, autoExposure),
    GX_SCHEMA_F32("knee", gX_RendererGFX_Bloom, knee),
    GX_SCHEMA_F32("exposureCompensation", gX_RendererGFX_Bloom, exposureCompensation),
    GX_SCHEMA_F32("exposureMin", gX_RendererGFX_Bloom, exposureMin),
    GX_SCHEMA_F32("exposureMax", gX_RendererGFX_Bloom, exposureMax),
};

// ---------------------------------------------------------------------------
// Field arrays — fog (3 fields)
// ---------------------------------------------------------------------------

static gX_SchemaField gfx_fog_fields[] = {
    GX_SCHEMA_F32("near",           gX_RendererGFX_Fog, near),
    GX_SCHEMA_F32("far",            gX_RendererGFX_Fog, far),
    GX_SCHEMA_COLOR("color",         gX_RendererGFX_Fog, color),
    GX_SCHEMA_F32("heightDensity",   gX_RendererGFX_Fog, heightDensity),
    GX_SCHEMA_F32("heightBase",      gX_RendererGFX_Fog, heightBase),
    GX_SCHEMA_BOOL("matchVoidColor", gX_RendererGFX_Fog, matchVoidColor),
};

// ---------------------------------------------------------------------------
// Field arrays — sky (3 fields)
// ---------------------------------------------------------------------------

static gX_SchemaField gfx_sky_fields[] = {
    GX_SCHEMA_F32("strength",         gX_RendererGFX_Sky, strength),
    GX_SCHEMA_COLOR("color",          gX_RendererGFX_Sky, color),
    GX_SCHEMA_STRING("skyboxModelPath", gX_RendererGFX_Sky, skyboxModelPath),
    GX_SCHEMA_STRING("skyboxPath",     gX_RendererGFX_Sky, skyboxPath),
    //ai: legacy presets retain procedural/clear backgrounds.
    GX_SCHEMA_BOOL_DEF("visibleImageSky", gX_RendererGFX_Sky, visibleImageSky, 0),
    //opencode: Sky Lab — procedural-sky art direction.
    GX_SCHEMA_F32("altitude",       gX_RendererGFX_Sky, altitude),
    GX_SCHEMA_F32("turbidity",      gX_RendererGFX_Sky, turbidity),
    GX_SCHEMA_F32("density",        gX_RendererGFX_Sky, density),
    GX_SCHEMA_F32("hue",            gX_RendererGFX_Sky, hue),
    GX_SCHEMA_F32("horizon",        gX_RendererGFX_Sky, horizon),
    GX_SCHEMA_F32("sunIntensity",   gX_RendererGFX_Sky, sunIntensity),
    GX_SCHEMA_F32("contrast",       gX_RendererGFX_Sky, contrast),
    GX_SCHEMA_F32("starDensity",    gX_RendererGFX_Sky, starDensity),
    GX_SCHEMA_F32("starBrightness", gX_RendererGFX_Sky, starBrightness),
    GX_SCHEMA_F32("starTwinkle",    gX_RendererGFX_Sky, starTwinkle),
};

// ---------------------------------------------------------------------------
// Field arrays — sun (subset of light fields: dir, color, brightness)
// ---------------------------------------------------------------------------

static gX_SchemaField gfx_sun_fields[] = {
    GX_SCHEMA_V3("dir",         gX_RendererGFX_Light, dir),
    GX_SCHEMA_COLOR("color",    gX_RendererGFX_Light, color),
    GX_SCHEMA_F32("brightness", gX_RendererGFX_Light, brightness),
    GX_SCHEMA_BOOL("castShadows", gX_RendererGFX_Light, castShadows),
    GX_SCHEMA_COLOR("diffuseTint", gX_RendererGFX_Light, diffuseTint),
    GX_SCHEMA_COLOR("specularTint", gX_RendererGFX_Light, specularTint),
    GX_SCHEMA_F32("diffuseFactor", gX_RendererGFX_Light, diffuseFactor),
    GX_SCHEMA_F32("specularFactor", gX_RendererGFX_Light, specularFactor),
};

//ai: water appearance, procedural/texture normal detail, and ripple fields are preset-authored.
static gX_SchemaField gfx_water_fields[] = {
    GX_SCHEMA_F32("planeHeight", gX_RendererWater, planeHeight),
    GX_SCHEMA_COLOR("tint", gX_RendererWater, tint),
    GX_SCHEMA_F32("normalMapStrength", gX_RendererWater, normalMapStrength),
    GX_SCHEMA_V2("normalMapScale", gX_RendererWater, normalMapScale),
    GX_SCHEMA_F32("proceduralNormalStrength", gX_RendererWater, proceduralNormalStrength),
    GX_SCHEMA_F32("proceduralNormalScale", gX_RendererWater, proceduralNormalScale),
    GX_SCHEMA_V2("normalMapScroll", gX_RendererWater, normalMapScroll),
    GX_SCHEMA_STRING("normalMapPath", gX_RendererWater, normalMapPath),
    GX_SCHEMA_I32("waveCount", gX_RendererWater, waveCount),
    GX_SCHEMA_BOOL("automaticRipples", gX_RendererWater, automaticRipples),
#ifdef GX_WATER_REFRACTION_EXPERIMENT
    GX_SCHEMA_BOOL("sceneRefractionEnabled", gX_RendererWater, sceneRefractionEnabled),
#endif
    GX_SCHEMA_ARRAY("waveDirections", gX_RendererWater, waveDirections, F32, GX_WATER_MAX_WAVES),
    GX_SCHEMA_ARRAY("waveAmplitudes", gX_RendererWater, waveAmplitudes, F32, GX_WATER_MAX_WAVES),
    GX_SCHEMA_ARRAY("waveFrequencies", gX_RendererWater, waveFrequencies, F32, GX_WATER_MAX_WAVES),
    GX_SCHEMA_ARRAY("waveSpeeds", gX_RendererWater, waveSpeeds, F32, GX_WATER_MAX_WAVES),
    GX_SCHEMA_ARRAY("waveSteepness", gX_RendererWater, waveSteepness, F32, GX_WATER_MAX_WAVES),
};

static gX_SchemaField gfx_weather_fields[] = {
    GX_SCHEMA_I32("targetCount", gX_RendererGFX_Weather, targetCount),
    GX_SCHEMA_F32("windX", gX_RendererGFX_Weather, windX),
    GX_SCHEMA_F32("windZ", gX_RendererGFX_Weather, windZ),
    GX_SCHEMA_F32("fallSpeed", gX_RendererGFX_Weather, fallSpeed),
    GX_SCHEMA_F32("floorY", gX_RendererGFX_Weather, floorY),
    GX_SCHEMA_F32("halfExtent", gX_RendererGFX_Weather, halfExtent),
    GX_SCHEMA_F32("spawnTop", gX_RendererGFX_Weather, spawnTop),
    GX_SCHEMA_I32("rainTarget", gX_RendererGFX_Weather, rainTarget),
    GX_SCHEMA_F32("rainWindX", gX_RendererGFX_Weather, rainWindX),
    GX_SCHEMA_F32("rainWindZ", gX_RendererGFX_Weather, rainWindZ),
    GX_SCHEMA_F32("rainFall", gX_RendererGFX_Weather, rainFall),
    GX_SCHEMA_F32("rainLength", gX_RendererGFX_Weather, rainLength),
    GX_SCHEMA_I32("fogTarget", gX_RendererGFX_Weather, fogTarget),
    GX_SCHEMA_F32("fogWindX", gX_RendererGFX_Weather, fogWindX),
    GX_SCHEMA_F32("fogWindZ", gX_RendererGFX_Weather, fogWindZ),
    GX_SCHEMA_F32("fogOpacity", gX_RendererGFX_Weather, fogOpacity),
    GX_SCHEMA_F32("fogScale", gX_RendererGFX_Weather, fogScale),
};

// ---------------------------------------------------------------------------
// Field arrays — motion blur (tuning params)
// ---------------------------------------------------------------------------

static gX_SchemaField gfx_motionblur_fields[] = {
    GX_SCHEMA_F32("strength",    gX_RendererGFX_MotionBlur, strength),
    GX_SCHEMA_I32("maxSamples",  gX_RendererGFX_MotionBlur, maxSamples),
};

// ---------------------------------------------------------------------------
// Field arrays — DOF (tuning params)
// ---------------------------------------------------------------------------

static gX_SchemaField gfx_dof_fields[] = {
    GX_SCHEMA_F32("focalDistance", gX_RendererGFX_DOF, focalDistance),
    GX_SCHEMA_F32("focalRange",    gX_RendererGFX_DOF, focalRange),
    GX_SCHEMA_F32("maxRadius",     gX_RendererGFX_DOF, maxRadius),
};

// ---------------------------------------------------------------------------
// Field arrays — TAA (tuning params)
// ---------------------------------------------------------------------------

static gX_SchemaField gfx_taa_fields[] = {
    GX_SCHEMA_F32("blendFactor", gX_RendererGFX_TAA, blendFactor),
};

// ---------------------------------------------------------------------------
// Field arrays — detail texture (3 fields)
// ---------------------------------------------------------------------------

static gX_SchemaField gfx_detail_fields[] = {
    GX_SCHEMA_F32("uvScale",  gX_RendererGFX_Detail, uvScale),
    GX_SCHEMA_F32("strength", gX_RendererGFX_Detail, strength),
    GX_SCHEMA_F32("distFade", gX_RendererGFX_Detail, distFade),
    GX_SCHEMA_STRING("mapPath", gX_RendererGFX_Detail, mapPath),
};

// ---------------------------------------------------------------------------
// Root GFX schema — ties all sub-objects together
// ---------------------------------------------------------------------------

static gX_SchemaField gfx_root_fields[] = {
    GX_SCHEMA_OBJECT("state", gX_RendererGFX, state,
                     gfx_state_fields,
                     (gX_U32)(sizeof(gfx_state_fields) / sizeof(gfx_state_fields[0]))),
    GX_SCHEMA_OBJECT("ssao", gX_RendererGFX, ssao,
                     gfx_ssao_fields,
                     (gX_U32)(sizeof(gfx_ssao_fields) / sizeof(gfx_ssao_fields[0]))),
    GX_SCHEMA_OBJECT("csm", gX_RendererGFX, csm,
                     gfx_csm_fields,
                     (gX_U32)(sizeof(gfx_csm_fields) / sizeof(gfx_csm_fields[0]))),
    GX_SCHEMA_OBJECT("ssr", gX_RendererGFX, ssr,
                     gfx_ssr_fields,
                     (gX_U32)(sizeof(gfx_ssr_fields) / sizeof(gfx_ssr_fields[0]))),
    GX_SCHEMA_OBJECT("reflection", gX_RendererGFX, reflection,
                     gfx_reflection_fields,
                     (gX_U32)(sizeof(gfx_reflection_fields) / sizeof(gfx_reflection_fields[0]))),
    GX_SCHEMA_OBJECT("godray", gX_RendererGFX, godray,
                     gfx_godray_fields,
                     (gX_U32)(sizeof(gfx_godray_fields) / sizeof(gfx_godray_fields[0]))),
    GX_SCHEMA_OBJECT("fogVolume", gX_RendererGFX, fogVolume,
                     gfx_fogvolume_fields,
                     (gX_U32)(sizeof(gfx_fogvolume_fields) / sizeof(gfx_fogvolume_fields[0]))),
    GX_SCHEMA_OBJECT("lensflare", gX_RendererGFX, lensflare,
                     gfx_lensflare_fields,
                     (gX_U32)(sizeof(gfx_lensflare_fields) / sizeof(gfx_lensflare_fields[0]))),
    GX_SCHEMA_OBJECT("bloom", gX_RendererGFX, bloom,
                     gfx_bloom_fields,
                     (gX_U32)(sizeof(gfx_bloom_fields) / sizeof(gfx_bloom_fields[0]))),
    GX_SCHEMA_OBJECT("fog",   gX_RendererGFX, fog,
                     gfx_fog_fields,
                     (gX_U32)(sizeof(gfx_fog_fields) / sizeof(gfx_fog_fields[0]))),
    GX_SCHEMA_OBJECT("detail", gX_RendererGFX, detail,
                     gfx_detail_fields,
                     (gX_U32)(sizeof(gfx_detail_fields) / sizeof(gfx_detail_fields[0]))),
    GX_SCHEMA_OBJECT("motionBlur", gX_RendererGFX, motionBlur,
                     gfx_motionblur_fields,
                     (gX_U32)(sizeof(gfx_motionblur_fields) / sizeof(gfx_motionblur_fields[0]))),
    GX_SCHEMA_OBJECT("dof",   gX_RendererGFX, dof,
                     gfx_dof_fields,
                     (gX_U32)(sizeof(gfx_dof_fields) / sizeof(gfx_dof_fields[0]))),
    GX_SCHEMA_OBJECT("taa",   gX_RendererGFX, taa,
                     gfx_taa_fields,
                     (gX_U32)(sizeof(gfx_taa_fields) / sizeof(gfx_taa_fields[0]))),
    GX_SCHEMA_OBJECT("sky",   gX_RendererGFX, sky,
                     gfx_sky_fields,
                     (gX_U32)(sizeof(gfx_sky_fields) / sizeof(gfx_sky_fields[0]))),
    GX_SCHEMA_OBJECT("sun",   gX_RendererGFX, sun,
                     gfx_sun_fields,
                     (gX_U32)(sizeof(gfx_sun_fields) / sizeof(gfx_sun_fields[0]))),
    GX_SCHEMA_OBJECT("water", gX_RendererGFX, water,
                     gfx_water_fields,
                     (gX_U32)(sizeof(gfx_water_fields) / sizeof(gfx_water_fields[0]))),
    GX_SCHEMA_OBJECT("particles", gX_RendererGFX, particle,
                     gfx_particle_fields,
                     (gX_U32)(sizeof(gfx_particle_fields) / sizeof(gfx_particle_fields[0]))),
    GX_SCHEMA_OBJECT("weather", gX_RendererGFX, weather,
                     gfx_weather_fields,
                     (gX_U32)(sizeof(gfx_weather_fields) / sizeof(gfx_weather_fields[0]))),

    GX_SCHEMA_HEAP("models",   gX_RendererGFX, models,
                   gfx_model_count, gfx_model_at, gfx_model_add,
                    gfx_model_fields,
                    (gX_U32)(sizeof(gfx_model_fields) / sizeof(gfx_model_fields[0]))),
    GX_SCHEMA_HEAP("lights",   gX_RendererGFX, lights,
                   gfx_light_count, gfx_light_at, gfx_light_add,
                    gfx_light_fields,
                    (gX_U32)(sizeof(gfx_light_fields) / sizeof(gfx_light_fields[0]))),
    GX_SCHEMA_HEAP("textures", gX_RendererGFX, textures,
                   gfx_texture_count, gfx_texture_at, gfx_texture_add,
                    gfx_texture_fields,
                    (gX_U32)(sizeof(gfx_texture_fields) / sizeof(gfx_texture_fields[0]))),
    GX_SCHEMA_HEAP("decals", gX_RendererGFX, decals,
                   gfx_decal_count, gfx_decal_at, gfx_decal_add,
                   gfx_decal_fields,
                   (gX_U32)(sizeof(gfx_decal_fields) / sizeof(gfx_decal_fields[0]))),
};

// ---------------------------------------------------------------------------
// Preset Save/Load — public API
// ---------------------------------------------------------------------------

//opencode: json-c types needed for manual root construction (meta fields)
#include "../../common/marshal/json_impl.h"

//ai: one camera schema encoder serves all three consumers; caller owns the object.
struct json_object *gX_RendererGFX_Preset_CameraToObj(const gX_Camera3D *Camera)
{
    struct json_object *Object = json_object_new_object();
    if (Object) gX_Schema_JSON_SaveToObj(gfx_camera_fields,
        sizeof(gfx_camera_fields)/sizeof(gfx_camera_fields[0]), (gX_I0*)Camera, Object);
    return Object;
}

//ai: share serialization with consent-based reports, without intermediate files.
struct json_object *gX_RendererGFX_Preset_ToObj(gX_RendererGFX *gfx, gX_I1 Diagnostic)
{
    if (!gfx) return NULL;
    //opencode: create root object manually so we can inject meta fields
    // (name, description, timestamps) that live in gfx->name/gfx->description
    // but are NOT part of the offset-based schema itself
    struct json_object *root = json_object_new_object();
    if (!root) return NULL;

    // Add meta fields
    json_object_object_add(root, "name", json_object_new_string(gfx->name));
    json_object_object_add(root, "description", json_object_new_string(gfx->description));

    //opencode: timestamp — ISO‑ish date + time
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    gX_I8 date[64], timeStr[64];
    strftime(date, sizeof(date), "%Y-%m-%d", tm);
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", tm);
    json_object_object_add(root, "created_date", json_object_new_string(date));
    json_object_object_add(root, "created_time", json_object_new_string(timeStr));

    // Add schema-driven data
    gX_U32 fc = (gX_U32)(sizeof(gfx_root_fields) / sizeof(gfx_root_fields[0]));
    if (Diagnostic)
    {
        fc = 0;
        while (fc < sizeof(gfx_root_fields)/sizeof(gfx_root_fields[0])
            && strcmp(gfx_root_fields[fc].key, "detail") != 0) fc++;
    }
    gX_Schema_JSON_SaveToObj(gfx_root_fields, fc, (gX_I0*)gfx, root);
    struct json_object *cameraObj = gX_RendererGFX_Preset_CameraToObj(&camera.camera3D);
    json_object_object_add(root, "camera", cameraObj);
    struct json_object *editorObj = json_object_new_object();
    gX_GFX_Editor_Preset_State editorState = {
        renderer.state.usePP,
        renderer.state.useCompositor,
    };
    gX_U32 editorFieldCount = (gX_U32)(sizeof(gfx_editor_fields) / sizeof(gfx_editor_fields[0]));
    gX_Schema_JSON_SaveToObj(gfx_editor_fields, editorFieldCount,
        (gX_I0*)&editorState, editorObj);
    json_object_object_add(root, "editor", editorObj);
    return root;
}

//ai: preserve shipped-preset guard and disk save behavior after serializer extraction.
gX_I32 gX_RendererGFX_Preset_Save_New(gX_RendererGFX *gfx, const gX_I8 *path)
{
    if (getenv("GX_NO_SETTINGS_SAVE") && path
        && strstr((const char *)path, "resources/tools/")) {
        gX_TraceLog_GFX(gX_LOG_ERROR,
            "Refusing to save a shipped preset under the harness guard: %s",
            path);
        return 1;
    }
    gX_TraceLog_GFX(gX_LOG_INFO, "Saving preset (new schema) to %s", path);
    struct json_object *root = gX_RendererGFX_Preset_ToObj(gfx, 0);

    // Write
    gX_I32 flags = JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_PRETTY_TAB;
    gX_I32 ret = json_object_to_file_ext(path, root, flags);
    json_object_put(root);

    if (ret == 0)
        gX_TraceLog_GFX(gX_LOG_INFO, "Save done");
    else
        gX_TraceLog_GFX(gX_LOG_ERROR, "Save failed");
    return ret == 0 ? 0 : -1;
}

//opencode: retain JSON context and reload authored water-map ownership safely.
//ai: preset replacement invalidates both SSAO temporal histories before the new scene renders.
//ai: legacy presets restore enabled periodic ripple sources even after loading a disabled preset.
//ai: older water presets default procedural normal strength and scale without retaining stale values.
gX_I32 gX_RendererGFX_Preset_Load_New(gX_RendererGFX *gfx, const gX_I8 *path)
{
    gX_TraceLog_GFX(gX_LOG_INFO, "Loading preset (new schema) from %s", path);
    struct json_object *root = json_object_from_file(path);
    gX_RendererGFX_ClearScene(gfx);
    gX_RendererGFX_Emitter_RemoveAll(gfx);
    snprintf(gfx->sky.skyboxPath, sizeof(gfx->sky.skyboxPath), "%s",
        GX_PATH("texture", "skybox/skybox1.png"));
    if (gfx->detail.map.id > 0) gX_Texture_Unload(gfx->detail.map);
    gfx->detail.map = (gX_Texture){0};
    gfx->detail.mapPath[0] = 0;
    gX_U32 fc = (gX_U32)(sizeof(gfx_root_fields) / sizeof(gfx_root_fields[0]));
    gX_I32 ret = gX_Schema_JSON_Load(gfx_root_fields, fc, (gX_I0*)gfx, path);
    if (ret == 0) {
        struct json_object *waterObj = NULL;
        struct json_object *automaticObj = NULL;
        if (!root || !json_object_object_get_ex(root, "water", &waterObj)
            || !json_object_object_get_ex(waterObj, "automaticRipples", &automaticObj))
            gfx->water.automaticRipples = true;
        struct json_object *scrollObj = NULL;
        if (!waterObj || !json_object_object_get_ex(waterObj, "normalMapScroll", &scrollObj))
            gfx->water.normalMapScroll = (gX_F32v2){0.01f, 0.01f};
        struct json_object *proceduralStrengthObj = NULL;
        if (!waterObj || !json_object_object_get_ex(waterObj,
                "proceduralNormalStrength", &proceduralStrengthObj))
            gfx->water.proceduralNormalStrength = 1.0f;
        struct json_object *proceduralScaleObj = NULL;
        if (!waterObj || !json_object_object_get_ex(waterObj,
                "proceduralNormalScale", &proceduralScaleObj))
            gfx->water.proceduralNormalScale = 1.0f;
#ifdef GX_WATER_REFRACTION_EXPERIMENT
        struct json_object *refractionObj = NULL;
        if (!waterObj || !json_object_object_get_ex(waterObj, "sceneRefractionEnabled", &refractionObj))
            gfx->water.sceneRefractionEnabled = false;
#endif
        struct json_object *cameraObj = NULL;
        struct json_object *editorObj = NULL;
        gX_U32 cameraFieldCount = (gX_U32)(sizeof(gfx_camera_fields) / sizeof(gfx_camera_fields[0]));
        gX_U32 editorFieldCount = (gX_U32)(sizeof(gfx_editor_fields) / sizeof(gfx_editor_fields[0]));
        gX_GFX_Editor_Preset_State editorState = {
            renderer.state.usePP,
            renderer.state.useCompositor,
        };
        if (root) json_object_object_get_ex(root, "camera", &cameraObj);
        gX_Schema_JSON_LoadFromObjPath(gfx_camera_fields, cameraFieldCount,
            (gX_I0*)&camera.camera3D, cameraObj, "camera");
        if (root) json_object_object_get_ex(root, "editor", &editorObj);
        {
            gX_Schema_JSON_LoadFromObjPath(gfx_editor_fields,
                editorFieldCount, (gX_I0*)&editorState, editorObj, "editor");
            renderer.state.usePP = editorState.usePP;
            renderer.state.useCompositor = editorState.useCompositor;
        }
        if (gfx->detail.mapPath[0]) gfx->detail.map = gX_Texture_Load(gfx->detail.mapPath);
        if (gfx->water.loadingstate.loaded) {
            if (gfx->water.normalMapTex.id > 0)
                gX_Texture_Unload(gfx->water.normalMapTex);
            gfx->water.normalMapTex = gfx->water.normalMapPath[0]
                ? gX_Texture_Load(gfx->water.normalMapPath) : (gX_Texture){0};
        }
        gfx->ssr.normalPass = 0;
        if (gfx->ssr.normalBufferEnabled) {
            gfx->ssr.normalMap.wrapper.loadingState.loadAsap = 1;
        } else {
            gfx->ssr.normalMap.wrapper.loadingState.loadAsap = 0;
            if (gfx->ssr.normalMap.wrapper.loadingState.loaded)
                gX_RendererGFX_RenderTexture_Unload(&gfx->ssr.normalMap);
        }
        //ai: the opt-in image renderer requires a real cubemap, not the legacy dormant 2D load.
        gX_I1 ImageSkyReload = gfx->sky.visibleImageSky && !gfx->sky.skybox.wrapper.isSkybox;
        if (strcmp(gfx->sky.skybox.wrapper.path, gfx->sky.skyboxPath) != 0 || ImageSkyReload) {
            if (gfx->sky.skybox.wrapper.loadingState.loaded)
                gX_RendererGFX_Texture_Unload(&gfx->sky.skybox);
            snprintf(gfx->sky.skybox.wrapper.path, sizeof(gfx->sky.skybox.wrapper.path),
                "%s", gfx->sky.skyboxPath);
            if (gfx->sky.visibleImageSky)
            {
                gfx->sky.skybox.wrapper.isSkybox = 1;
                gfx->sky.skybox.wrapper.skyboxLayout = 0;
            }
            gfx->sky.skybox.wrapper.loadingState.loadAsap = gfx->sky.skyboxPath[0] != 0;
        }
        gfx->common.cookieAtlasDirty = 1;
        gX_RendererGFX_TemporalHistory_Reset(&gfx->ssao.gtaoTemporal);
        gX_RendererGFX_TemporalHistory_Reset(&gfx->ssao.ssgiTemporal);
        gfx->ssao.gtaoFinalTex.id = 0;
        gfx->ssao.ssgiFinalTex.id = 0;
        gfx->ssao.temporalModesInitialized = false;
        gX_TraceLog_GFX(gX_LOG_INFO, "Load done");
    }
    if (root) json_object_put(root);
    return ret;
}

// ---------------------------------------------------------------------------
// Schema self-test — verifies every OBJECT/HEAP child offset stays within its
// declared struct member. Catches structType/offsetof mismatches that would
// corrupt memory on preset save/load.
// ---------------------------------------------------------------------------

typedef struct gX_GFX_Schema_Member {
    const gX_I8 *key;
    gX_U32 size;
} gX_GFX_Schema_Member;

static gX_U32 gfx_schema_member_size(const gX_I8 *key)
{
    static const gX_GFX_Schema_Member members[] = {
        { "state",      sizeof(gX_RendererGFX_State) },
        { "ssao",       sizeof(gX_RendererGFX_SSAO) },
        { "csm",        sizeof(gX_RendererGFX_CSM) },
        { "ssr",        sizeof(gX_RendererGFX_SSR) },
        { "reflection", sizeof(gX_RendererGFX_Reflection) },
        { "godray",     sizeof(gX_RendererGFX_Godray) },
        { "fogVolume",  sizeof(gX_RendererGFX_FogVolume) },
        { "lensflare",  sizeof(gX_RendererGFX_LensFlare) },
        { "bloom",      sizeof(gX_RendererGFX_Bloom) },
        { "fog",        sizeof(gX_RendererGFX_Fog) },
        { "detail",     sizeof(gX_RendererGFX_Detail) },
        { "motionBlur", sizeof(gX_RendererGFX_MotionBlur) },
        { "dof",        sizeof(gX_RendererGFX_DOF) },
        { "taa",        sizeof(gX_RendererGFX_TAA) },
        { "sky",        sizeof(gX_RendererGFX_Sky) },
        { "sun",        sizeof(gX_RendererGFX_Light) },
        { "models",     sizeof(gX_RendererGFX_Model) },
        { "lights",     sizeof(gX_RendererGFX_Light) },
        { "textures",   sizeof(gX_RendererGFX_Texture) },
        { "decals",     sizeof(gX_RendererGFX_Decal) },
        { "water",      sizeof(gX_RendererWater) },
        { "particles",  sizeof(gX_RendererGFX_Particle) },
        { "weather",    sizeof(gX_RendererGFX_Weather) },
        { "params",     sizeof(gX_RendererGFX_Model_Parameters) },
    };
    for (gX_U32 i = 0; i < (gX_U32)(sizeof(members)/sizeof(members[0])); i++)
        if (strcmp(members[i].key, key) == 0) return members[i].size;
    return 0;
}

gX_I32 gX_RendererGFX_Schema_SelfTest(gX_I0)
{
    gX_I32 errors = 0;
    gX_U32 fc = (gX_U32)(sizeof(gfx_root_fields) / sizeof(gfx_root_fields[0]));
    for (gX_U32 i = 0; i < fc; i++) {
        const gX_SchemaField *rootField = &gfx_root_fields[i];
        gX_U32 memberSize = gfx_schema_member_size(rootField->key);
        if (memberSize == 0) {
            gX_TraceLog_GFX(gX_LOG_WARNING, "Schema self-test: no member size table entry for '%s'", rootField->key);
            continue;
        }
        const gX_SchemaField *children = (rootField->fieldType == GX_SCHEMA_HEAP) ?
            rootField->heapChildren : rootField->children;
        gX_U32 childCount = (rootField->fieldType == GX_SCHEMA_HEAP) ?
            rootField->heapChildCount : rootField->childCount;
        for (gX_U32 ci = 0; ci < childCount; ci++) {
            const gX_SchemaField *child = &children[ci];
            if (child->fieldType == GX_SCHEMA_OBJECT) {
                gX_U32 nestedSize = gfx_schema_member_size(child->key);
                if (nestedSize > 0) {
                    for (gX_U32 ni = 0; ni < child->childCount; ni++) {
                        gX_U32 ds = gX_Datatype_Size(child->children[ni].datatype);
                        if (ds > 0 && child->children[ni].offset + ds > nestedSize) {
                            gX_TraceLog_GFX(gX_LOG_ERROR,
                                "Schema self-test: %s.%s.%s offset %u + %u exceeds %s (%u bytes)",
                                rootField->key, child->key, child->children[ni].key,
                                child->children[ni].offset, ds, child->key, nestedSize);
                            errors++;
                        }
                    }
                    if (child->offset + nestedSize > memberSize) {
                        gX_TraceLog_GFX(gX_LOG_ERROR,
                            "Schema self-test: %s.%s object offset %u + %u exceeds %s (%u bytes)",
                            rootField->key, child->key, child->offset, nestedSize, rootField->key, memberSize);
                        errors++;
                    }
                }
            } else {
                gX_U32 ds = (child->datatype == gX_DATATYPE_I1) ? 1 : gX_Datatype_Size(child->datatype);
                if (ds > 0 && child->offset + ds > memberSize) {
                    gX_TraceLog_GFX(gX_LOG_ERROR,
                        "Schema self-test: %s.%s offset %u + %u exceeds %s (%u bytes)",
                        rootField->key, child->key, child->offset, ds, rootField->key, memberSize);
                    errors++;
                }
            }
        }
    }
    return errors;
}
