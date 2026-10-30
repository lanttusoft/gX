/**********************************************************************************************
*
*   [gX: src/scene/battle.c]
*   Native battle presentation — 3D arena and 2D HUD
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


//ai: native battle presentation: renderer-owned 3D blockout and wrapper-based HUD.
#include "battle.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../common/enum.h"
#include "../common/log.h"
#include "../common/macros.h"
#include "../main.h"
#include "../rhi/batch.h"
#include "../renderer/renderer.h"
#include "../wrappers/wrappers.h"

//ai: render the same nine-by-nine world-space board used by the headless runtime.
#define GX_BATTLE_GFX_GRID_COLUMNS GX_BATTLE_DEMO_GRID_COLUMNS
#define GX_BATTLE_GFX_GRID_ROWS GX_BATTLE_DEMO_GRID_ROWS
#define GX_BATTLE_GFX_GRID_HALF_WIDTH GX_BATTLE_DEMO_GRID_HALF_EXTENT
#define GX_BATTLE_GFX_GRID_HALF_DEPTH GX_BATTLE_DEMO_GRID_HALF_EXTENT
#define GX_BATTLE_GFX_ARENA_RADIUS 6.5f
#define GX_BATTLE_IMPACT_REACTION_SECONDS 0.48f

static gX_F32 gX_SceneBattle_Scale(gX_I0) {
    gX_F32 widthScale = (gX_F32)maindata.screenWidth / 1024.0f;
    gX_F32 heightScale = (gX_F32)maindata.screenHeight / 768.0f;
    return fminf(widthScale, heightScale);
}

//ai: convert a combat meter to a safe normalized HUD bar value.
static gX_F32 gX_SceneBattle_MeterRatio(gX_F64 value, gX_F64 maximum) {
    if (!isfinite(value) || !isfinite(maximum) || maximum <= 0.0) return 0.0f;
    gX_F64 ratio = value / maximum;
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    return (gX_F32)ratio;
}

static gX_I0 gX_SceneBattle_DrawText(const gX_I8 *text, gX_F32 x, gX_F32 y,
    gX_F32 size, gX_Color color) {
    gX_Batch_Flush();
    gX_RawString_Draw_Lesser(text, (gX_F32v2){x, y}, size, color);
}

static gX_I0 gX_SceneBattle_DrawCenteredText(const gX_I8 *text, gX_F32 x,
    gX_F32 y, gX_F32 size, gX_Color color) {
    gX_F32v2 measured = gX_RawString_Measure(text, gX_Font_GetDefault(), size, 0.0f);
    gX_SceneBattle_DrawText(text, x - measured.x * 0.5f, y, size, color);
}

static gX_I0 gX_SceneBattle_DrawPanel(gX_Rectangle rect, gX_Color fill,
    gX_Color border) {
    gX_Rectangle_Draw(rect, fill);
    gX_Rectangle_Draw_Border(rect, border);
}

//ai: map the demo enemy kind to a UI label without introducing content assets.
static const gX_I8 *gX_SceneBattle_EnemyName(gX_I32 kind) {
    switch (kind) {
        case GX_BATTLE_DEMO_ENEMY_WARDEN: return "RIFT WARDEN";
        case GX_BATTLE_DEMO_ENEMY_ADEPT: return "RIFT ADEPT";
        case GX_BATTLE_DEMO_ENEMY_SENTINEL: return "RIFT SENTINEL";
        default: return "RIFT HOSTILE";
    }
}

//ai: build a compact primitive arena in the GFX scene heap; all source models
//   are shipped gX primitives and the scene remains demo-scoped.
static gX_I32 gX_SceneBattle_GFX_AddModel(gX_RendererGFX *gfx,
    const gX_I8 *path, gX_F32v3 position, gX_F32v3 scale, gX_Color tint,
    gX_F32 metalness, gX_F32 roughness, gX_F32 emissiveFactor,
    gX_I32 actorIndex, gX_I32 partIndex, gX_SceneBattle *battle) {
    gX_RendererGFX_Model *model = gX_Heap_Add_Push_NoPtr(&gfx->models);
    if (!model) return -1;
    if (gX_RendererGFX_Model_Init(model, path) != 0) return -1;
    model->params.pos = position;
    model->params.scale = scale;
    model->params.usePBR = true;
    model->params.albedoTint = tint;
    model->params.metalness = metalness;
    model->params.roughness = roughness;
    model->params.emissiveColor = tint;
    model->params.emissiveFactor = emissiveFactor;
    gX_I32 modelIndex = (gX_I32)((gX_U8 *)model - (gX_U8 *)gfx->models.data)
        / (gX_I32)sizeof(gX_RendererGFX_Model);
    if (actorIndex >= 0 && partIndex >= 0)
        battle->state.gfxActorModels[actorIndex][partIndex] = modelIndex;
    return modelIndex;
}

static gX_Color gX_SceneBattle_EnemyTint(gX_I32 kind);

//ai: give each planned enemy action a stable palette shared by the HUD and arena cue.
static gX_Color gX_SceneBattle_IntentColor(gX_I32 action) {
    if (action == GX_BATTLE_AI_ACTION_MAGIC)
        return (gX_Color){104, 213, 255, 255};
    if (action == GX_BATTLE_AI_ACTION_GUARD)
        return (gX_Color){255, 211, 112, 255};
    return (gX_Color){255, 116, 132, 255};
}

//ai: resolve a bounded impact target to the renderer actor index used by this scene.
static gX_I1 gX_SceneBattle_ImpactTargetActor(
    const gX_SceneBattle_Runtime *runtime,
    const gX_SceneBattle_DemoImpact *impact, gX_U32 *outActor) {
    if (outActor) *outActor = UINT32_MAX;
    if (!runtime || !impact || !outActor
        || runtime->enemyCount > GX_BATTLE_DEMO_MAX_ENEMIES) return false;
    if (impact->targetKind == GX_BATTLE_DEMO_IMPACT_PLAYER) {
        if (impact->targetIndex >= GX_BATTLE_DEMO_PARTY_SIZE) return false;
        *outActor = impact->targetIndex;
        return true;
    }
    if (impact->targetIndex >= runtime->enemyCount
        || runtime->enemies[impact->targetIndex].kind != impact->targetKind)
        return false;
    *outActor = (gX_U32)impact->targetIndex + GX_BATTLE_DEMO_PARTY_SIZE;
    return true;
}

//ai: map a valid renderer actor to the floor position used by this formation.
static gX_I1 gX_SceneBattle_ActorBasePosition(
    const gX_SceneBattle_Runtime *runtime, gX_U32 actor, gX_F32v3 *outPosition) {
    if (outPosition) *outPosition = (gX_F32v3){0.0f, 0.0f, 0.0f};
    if (!runtime || !outPosition
        || runtime->enemyCount > GX_BATTLE_DEMO_MAX_ENEMIES) return false;
    if (actor < GX_BATTLE_DEMO_PARTY_SIZE)
        return gX_SceneBattle_Grid_CellToWorld(&runtime->grid,
            runtime->party[actor].gridCol, runtime->party[actor].gridRow,
            outPosition);
    gX_U32 enemy = actor - GX_BATTLE_DEMO_PARTY_SIZE;
    if (enemy >= runtime->enemyCount) return false;
    return gX_SceneBattle_Grid_CellToWorld(&runtime->grid,
        runtime->enemies[enemy].gridCol, runtime->enemies[enemy].gridRow,
        outPosition);
}

//ai: resolve a bounded actor position to the center of its presentational board cell.
static gX_I1 gX_SceneBattle_GFX_CellCenter(const gX_F32v3 *position,
    gX_F32v3 *outCenter) {
    if (outCenter) *outCenter = (gX_F32v3){0.0f, 0.0f, 0.0f};
    if (!position || !outCenter || !isfinite(position->x)
        || !isfinite(position->z)) return false;
    gX_F32 cellWidth = 2.0f * GX_BATTLE_GFX_GRID_HALF_WIDTH
        / (gX_F32)GX_BATTLE_GFX_GRID_COLUMNS;
    gX_F32 cellDepth = 2.0f * GX_BATTLE_GFX_GRID_HALF_DEPTH
        / (gX_F32)GX_BATTLE_GFX_GRID_ROWS;
    gX_F32 columnPosition = (position->x + GX_BATTLE_GFX_GRID_HALF_WIDTH)
        / cellWidth;
    gX_F32 rowPosition = (position->z + GX_BATTLE_GFX_GRID_HALF_DEPTH)
        / cellDepth;
    if (columnPosition < 0.0f || columnPosition >= GX_BATTLE_GFX_GRID_COLUMNS
        || rowPosition < 0.0f || rowPosition >= GX_BATTLE_GFX_GRID_ROWS)
        return false;
    gX_I32 column = (gX_I32)floorf(columnPosition);
    gX_I32 row = (gX_I32)floorf(rowPosition);
    outCenter->x = -GX_BATTLE_GFX_GRID_HALF_WIDTH
        + ((gX_F32)column + 0.5f) * cellWidth;
    outCenter->z = -GX_BATTLE_GFX_GRID_HALF_DEPTH
        + ((gX_F32)row + 0.5f) * cellDepth;
    return true;
}

//ai: find the source actor for enemy damage while retaining the compact impact record.
static gX_I1 gX_SceneBattle_ImpactSourceActor(
    const gX_SceneBattle_Runtime *runtime,
    const gX_SceneBattle_DemoImpact *impact, gX_U32 *outActor) {
    if (outActor) *outActor = UINT32_MAX;
    if (!runtime || !impact || !outActor
        || runtime->enemyCount > GX_BATTLE_DEMO_MAX_ENEMIES) return false;
    if (impact->sourceKind == GX_BATTLE_DEMO_IMPACT_PLAYER) {
        if (impact->sourceIndex >= GX_BATTLE_DEMO_PARTY_SIZE) return false;
        *outActor = impact->sourceIndex;
        return true;
    }
    for (gX_U32 enemy = 0; enemy < runtime->enemyCount; enemy++) {
        if (runtime->enemies[enemy].kind == impact->sourceKind) {
            *outActor = enemy + GX_BATTLE_DEMO_PARTY_SIZE;
            return true;
        }
    }
    return false;
}

//ai: select the latest still-active actual HP change without adding animation state.
static const gX_SceneBattle_DemoImpact *gX_SceneBattle_LatestImpactReaction(
    const gX_SceneBattle_Runtime *runtime) {
    if (!runtime || runtime->impactHead >= GX_BATTLE_DEMO_IMPACT_HISTORY
        || runtime->impactCount > GX_BATTLE_DEMO_IMPACT_HISTORY) return NULL;
    for (gX_U32 i = 0; i < runtime->impactCount; i++) {
        gX_U32 index = (runtime->impactHead + GX_BATTLE_DEMO_IMPACT_HISTORY
            - 1U - i) % GX_BATTLE_DEMO_IMPACT_HISTORY;
        const gX_SceneBattle_DemoImpact *impact = &runtime->impacts[index];
        if (isfinite(impact->age) && impact->age >= 0.0f
            && impact->age < GX_BATTLE_IMPACT_REACTION_SECONDS
            && isfinite(impact->amount) && impact->amount > 0.0f) return impact;
    }
    return NULL;
}

//ai: blend the hit tint into base PBR colors while the impact pulse is active.
static gX_Color gX_SceneBattle_ColorMix(gX_Color base, gX_Color flash,
    gX_F32 amount) {
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    return (gX_Color){
        (gX_U8)((gX_F32)base.r + ((gX_F32)flash.r - base.r) * amount),
        (gX_U8)((gX_F32)base.g + ((gX_F32)flash.g - base.g) * amount),
        (gX_U8)((gX_F32)base.b + ((gX_F32)flash.b - base.b) * amount),
        base.a};
}

//ai: map cardinal tactical facing to the renderer's horizontal plane.
static gX_I0 gX_SceneBattle_FacingDelta(gX_I32 facing,
    gX_F32 *outX, gX_F32 *outZ) {
    if (outX) *outX = 0.0f;
    if (outZ) *outZ = 0.0f;
    if (!outX || !outZ) return;
    if (facing == GX_BATTLE_DEMO_DIRECTION_NORTH) *outZ = -1.0f;
    else if (facing == GX_BATTLE_DEMO_DIRECTION_EAST) *outX = 1.0f;
    else if (facing == GX_BATTLE_DEMO_DIRECTION_SOUTH) *outZ = 1.0f;
    else if (facing == GX_BATTLE_DEMO_DIRECTION_WEST) *outX = -1.0f;
}

//ai: orient the facing tick so its long axis points along the selected slot direction.
static gX_F32 gX_SceneBattle_FacingAngle(gX_I32 facing) {
    if (facing == GX_BATTLE_DEMO_DIRECTION_NORTH) return 90.0f;
    if (facing == GX_BATTLE_DEMO_DIRECTION_SOUTH) return -90.0f;
    if (facing == GX_BATTLE_DEMO_DIRECTION_WEST) return 180.0f;
    return 0.0f;
}

//ai: map recorded magic elements and recovery to compact arena flash colors.
static gX_Color gX_SceneBattle_ImpactColor(
    const gX_SceneBattle_DemoImpact *impact) {
    if (impact->isHeal) return (gX_Color){108, 255, 178, 255};
    if (impact->element == GX_BATTLE_ELEMENT_FIRE)
        return (gX_Color){255, 111, 54, 255};
    if (impact->element == GX_BATTLE_ELEMENT_ICE)
        return (gX_Color){99, 211, 255, 255};
    if (impact->element == GX_BATTLE_ELEMENT_LIGHTNING)
        return (gX_Color){255, 232, 93, 255};
    return (gX_Color){255, 226, 211, 255};
}

//ai: add compact point lights to make the PBR blockout read against both
//   the procedural background and the GFX renderer's own sky/reflection passes.
static gX_I0 gX_SceneBattle_GFX_AddLight(gX_RendererGFX *gfx,
    gX_F32v3 position, gX_Color color) {
    gX_RendererGFX_Light *light = gX_Heap_Add_Push_NoPtr(&gfx->lights);
    if (!light) return;
    gX_RendererGFX_Light_Init(light);
    light->type = 1;
    light->pos = position;
    light->color = color;
    light->diffuseTint = color;
    light->specularTint = color;
    light->brightness = 3.2f;
    light->diffuseFactor = 1.0f;
    light->specularFactor = 1.0f;
    light->attenConst = 1.0f;
    light->attenLinear = 0.08f;
    light->attenQuad = 0.025f;
}

//ai: construct the octagonal nine-by-nine stage and actor blockouts once; GFX
//   owns rendering while this scene synchronizes formation and visual cues.
static gX_I1 gX_SceneBattle_GFX_Ensure(gX_SceneBattle *battle) {
    if (!maindata.backendLoaded || !renderer.gfx.data || renderer.gfx.amount == 0)
        return true;
    gX_RendererGFX *gfx = (gX_RendererGFX *)renderer.gfx.data;
    if (gfx->common.battleScene) return true;
    if (gfx->common.demoSceneAdded) return true;
    for (gX_U32 actor = 0; actor < GX_BATTLE_GFX_ACTOR_COUNT; actor++)
        for (gX_U32 part = 0; part < GX_BATTLE_GFX_PART_COUNT; part++)
            battle->state.gfxActorModels[actor][part] = -1;
    gX_I8 cubePath[256];
    gX_I8 spherePath[256];
    gX_I8 conePath[256];
    gX_I8 cylinderPath[256];
    gX_I8 torusPath[256];
    snprintf((char *)cubePath, sizeof(cubePath), GX_RESOURCE_DIR "/model/primitive/cube.glb");
    snprintf((char *)spherePath, sizeof(spherePath), GX_RESOURCE_DIR "/model/primitive/sphere.glb");
    snprintf((char *)conePath, sizeof(conePath), GX_RESOURCE_DIR "/model/primitive/cone.glb");
    snprintf((char *)cylinderPath, sizeof(cylinderPath), GX_RESOURCE_DIR "/model/primitive/cylinder.glb");
    snprintf((char *)torusPath, sizeof(torusPath), GX_RESOURCE_DIR "/model/primitive/torus.glb");
    gX_Mesh arenaDeckMesh = gX_Mesh_GenCylinder(
        GX_BATTLE_GFX_ARENA_RADIUS, 0.3f, 8);
    gX_I32 arenaDeckIndex = gX_RendererGFX_Model_AddGeneratedMesh(gfx,
        arenaDeckMesh);
    if (arenaDeckIndex >= 0) {
        gX_RendererGFX_Model *arenaDeck =
            &((gX_RendererGFX_Model *)gfx->models.data)[arenaDeckIndex];
        arenaDeck->params.pos = (gX_F32v3){0.0f, -0.315f, 0.0f};
        arenaDeck->params.usePBR = true;
        arenaDeck->params.albedoTint = (gX_Color){19, 35, 56, 255};
        arenaDeck->params.metalness = 0.48f;
        arenaDeck->params.roughness = 0.34f;
    } else {
        gX_Mesh_Unload(arenaDeckMesh);
        gX_TraceLog(gX_LOG_WARNING, "BATTLE_GFX: octagonal deck mesh unavailable");
    }
    gX_F32 edgeCenterRadius = GX_BATTLE_GFX_ARENA_RADIUS * cosf(0.39269908f);
    for (gX_U32 vertex = 0; vertex < 8; vertex++) {
        gX_F32 vertexAngle = ((gX_F32)vertex + 0.5f) * 0.78539816f;
        gX_Color edgeTint = {78, 153, 172, 255};
        gX_SceneBattle_GFX_AddModel(gfx, cubePath,
            (gX_F32v3){edgeCenterRadius * cosf(vertexAngle), 0.10f,
                edgeCenterRadius * sinf(vertexAngle)},
            (gX_F32v3){0.12f, 0.24f, 0.12f}, edgeTint,
            0.3f, 0.24f, 0.55f, -1, -1, battle);
    }
    gX_SceneBattle_GFX_AddModel(gfx, cylinderPath, (gX_F32v3){0.0f, 0.015f, 0.0f},
        (gX_F32v3){2.35f, 0.12f, 2.35f}, (gX_Color){22, 91, 122, 255},
        0.62f, 0.27f, 0.18f, -1, -1, battle);
    gX_SceneBattle_GFX_AddModel(gfx, torusPath, (gX_F32v3){0.0f, 0.105f, 0.0f},
        (gX_F32v3){2.05f, 0.12f, 2.05f}, (gX_Color){58, 194, 218, 255},
        0.45f, 0.2f, 0.48f, -1, -1, battle);
    battle->state.gfxImpactRippleModel = gX_SceneBattle_GFX_AddModel(gfx,
        torusPath, (gX_F32v3){0.0f, -30.0f, 0.0f},
        (gX_F32v3){0.34f, 0.025f, 0.34f}, (gX_Color){255, 255, 255, 255},
        0.08f, 0.22f, 0.0f, -1, -1, battle);
    for (gX_U32 column = 1; column < GX_BATTLE_GFX_GRID_COLUMNS; column++) {
        gX_I1 centerLine = column == GX_BATTLE_GFX_GRID_COLUMNS / 2U;
        gX_F32 x = -GX_BATTLE_GFX_GRID_HALF_WIDTH
            + 2.0f * GX_BATTLE_GFX_GRID_HALF_WIDTH * (gX_F32)column
            / (gX_F32)GX_BATTLE_GFX_GRID_COLUMNS;
        gX_Color tint = centerLine ? (gX_Color){92, 145, 158, 255}
            : (gX_Color){57, 85, 102, 255};
        gX_SceneBattle_GFX_AddModel(gfx, cubePath,
            (gX_F32v3){x, 0.127f, 0.0f},
            (gX_F32v3){0.014f, 0.01f, GX_BATTLE_GFX_GRID_HALF_DEPTH}, tint,
            0.32f, 0.32f, centerLine ? 0.08f : 0.025f, -1, -1, battle);
    }
    for (gX_U32 row = 1; row < GX_BATTLE_GFX_GRID_ROWS; row++) {
        gX_F32 z = -GX_BATTLE_GFX_GRID_HALF_DEPTH
            + 2.0f * GX_BATTLE_GFX_GRID_HALF_DEPTH * (gX_F32)row
            / (gX_F32)GX_BATTLE_GFX_GRID_ROWS;
        gX_Color tint = (gX_Color){57, 85, 102, 255};
        gX_SceneBattle_GFX_AddModel(gfx, cubePath,
            (gX_F32v3){0.0f, 0.127f, z},
            (gX_F32v3){GX_BATTLE_GFX_GRID_HALF_WIDTH, 0.01f, 0.014f}, tint,
            0.32f, 0.32f, 0.025f, -1, -1, battle);
    }
    gX_SceneBattle_GFX_AddModel(gfx, cubePath,
        (gX_F32v3){0.0f, 0.135f, 0.0f},
        (gX_F32v3){0.022f, 0.01f, GX_BATTLE_GFX_GRID_HALF_DEPTH},
        (gX_Color){101, 152, 162, 255}, 0.32f, 0.32f, 0.08f,
        -1, -1, battle);
    gX_SceneBattle_GFX_AddModel(gfx, cubePath,
        (gX_F32v3){0.0f, 0.135f, 0.0f},
        (gX_F32v3){GX_BATTLE_GFX_GRID_HALF_WIDTH, 0.01f, 0.022f},
        (gX_Color){101, 152, 162, 255}, 0.32f, 0.32f, 0.08f,
        -1, -1, battle);
    battle->state.gfxIntentCellModel = gX_SceneBattle_GFX_AddModel(gfx,
        torusPath, (gX_F32v3){0.0f, -30.0f, 0.0f},
        (gX_F32v3){0.36f, 0.025f, 0.36f}, (gX_Color){255, 255, 255, 255},
        0.08f, 0.22f, 0.0f, -1, -1, battle);
    static const gX_Color PartyTints[GX_BATTLE_DEMO_PARTY_SIZE] = {
        {43, 129, 178, 255}, {126, 91, 190, 255}, {59, 151, 119, 255}};
    for (gX_U32 partyIndex = 0; partyIndex < GX_BATTLE_DEMO_PARTY_SIZE;
            partyIndex++) {
        gX_F32v3 partyFloorPosition;
        if (!gX_SceneBattle_ActorBasePosition(&battle->state.runtime,
                partyIndex, &partyFloorPosition)) return false;
        gX_Color tint = PartyTints[partyIndex];
        const gX_I8 *bodyPath = partyIndex == GX_BATTLE_DEMO_ROLE_ARCANIST
            ? spherePath : cubePath;
        const gX_I8 *weaponPath = partyIndex == GX_BATTLE_DEMO_ROLE_ARCANIST
            ? cylinderPath : cubePath;
        gX_SceneBattle_GFX_AddModel(gfx, bodyPath,
            (gX_F32v3){partyFloorPosition.x, 0.98f, partyFloorPosition.z},
            (gX_F32v3){0.48f, 0.55f, 0.34f}, tint,
            0.24f, 0.34f, 0.1f, (gX_I32)partyIndex, 0, battle);
        gX_Color headTint = partyIndex == GX_BATTLE_DEMO_ROLE_VANGUARD
            ? (gX_Color){202, 151, 105, 255} : tint;
        gX_SceneBattle_GFX_AddModel(gfx, spherePath,
            (gX_F32v3){partyFloorPosition.x, 1.63f, partyFloorPosition.z},
            (gX_F32v3){0.34f, 0.34f, 0.34f}, headTint,
            0.05f, 0.62f, partyIndex == GX_BATTLE_DEMO_ROLE_MEDIC
                ? 0.12f : 0.0f, (gX_I32)partyIndex, 1, battle);
        gX_F32 weaponLength = partyIndex == GX_BATTLE_DEMO_ROLE_ARCANIST
            ? 0.85f : 0.68f;
        gX_I32 weaponIndex = gX_SceneBattle_GFX_AddModel(gfx, weaponPath,
            (gX_F32v3){partyFloorPosition.x + 0.55f, 1.0f,
                partyFloorPosition.z - 0.12f},
            (gX_F32v3){0.08f, weaponLength, 0.08f},
            partyIndex == GX_BATTLE_DEMO_ROLE_MEDIC
                ? (gX_Color){159, 246, 192, 255}
                : (gX_Color){181, 218, 231, 255},
            0.72f, 0.16f, 0.35f, (gX_I32)partyIndex, 2, battle);
        if (weaponIndex >= 0) {
            gX_RendererGFX_Model *models =
                (gX_RendererGFX_Model *)gfx->models.data;
            models[weaponIndex].params.rotation.z = -18.0f;
        }
        gX_SceneBattle_GFX_AddModel(gfx, cubePath,
            (gX_F32v3){partyFloorPosition.x, 0.36f, partyFloorPosition.z},
            (gX_F32v3){0.45f, 0.4f, 0.34f},
            (gX_Color){26, 48, 75, 255}, 0.18f, 0.48f, 0.0f,
            (gX_I32)partyIndex, 3, battle);
    }
    for (gX_U32 enemy = 0; enemy < GX_BATTLE_DEMO_MAX_ENEMIES; enemy++) {
        gX_U32 actor = enemy + GX_BATTLE_DEMO_PARTY_SIZE;
        gX_F32v3 enemyFloorPosition;
        if (!gX_SceneBattle_ActorBasePosition(&battle->state.runtime,
                actor, &enemyFloorPosition)) continue;
        gX_Color tint = enemy == 0
            ? (gX_Color){130, 43, 83, 255} : (gX_Color){42, 99, 144, 255};
        gX_SceneBattle_GFX_AddModel(gfx, spherePath,
            (gX_F32v3){enemyFloorPosition.x, 0.82f, enemyFloorPosition.z},
            (gX_F32v3){0.76f, 1.1f, 0.72f}, tint, 0.26f, 0.36f, 0.12f,
            (gX_I32)actor, 0, battle);
        gX_SceneBattle_GFX_AddModel(gfx, spherePath,
            (gX_F32v3){enemyFloorPosition.x, 1.62f, enemyFloorPosition.z},
            (gX_F32v3){0.48f, 0.48f, 0.48f}, tint, 0.18f, 0.4f, 0.16f,
            (gX_I32)actor, 1, battle);
        gX_SceneBattle_GFX_AddModel(gfx, conePath,
            (gX_F32v3){enemyFloorPosition.x, 2.08f, enemyFloorPosition.z},
            (gX_F32v3){0.35f, 0.48f, 0.35f}, tint, 0.24f, 0.3f, 0.36f,
            (gX_I32)actor, 2, battle);
        gX_SceneBattle_GFX_AddModel(gfx, torusPath,
            (gX_F32v3){enemyFloorPosition.x, 0.075f, enemyFloorPosition.z},
            (gX_F32v3){0.84f, 0.055f, 0.84f}, (gX_Color){255, 196, 94, 255},
            0.46f, 0.22f, 0.8f, (gX_I32)actor, 3, battle);
    }
    for (gX_U32 actor = 0; actor < GX_BATTLE_GFX_ACTOR_COUNT; actor++)
        battle->state.gfxFacingModels[actor] = gX_SceneBattle_GFX_AddModel(gfx,
            cubePath, (gX_F32v3){0.0f, -30.0f, 0.0f},
            (gX_F32v3){0.24f, 0.035f, 0.06f},
            actor < GX_BATTLE_DEMO_PARTY_SIZE
                ? (gX_Color){98, 211, 235, 255}
                : (gX_Color){246, 175, 120, 255},
            0.12f, 0.34f, 0.18f, -1, -1, battle);
    gX_SceneBattle_GFX_AddLight(gfx, (gX_F32v3){-3.6f, 4.2f, 2.8f},
        (gX_Color){84, 184, 255, 255});
    gX_SceneBattle_GFX_AddLight(gfx, (gX_F32v3){3.8f, 3.8f, -2.4f},
        (gX_Color){255, 111, 131, 255});
    gfx->common.battleScene = true;
    gfx->common.demoSceneAdded = true;
    gX_TraceLog(gX_LOG_NOTICE, "BATTLE_GFX: models=%u lights=%u",
        gfx->models.amount, gfx->lights.amount);
    return true;
}

//ai: keep the 3D actors in lockstep with the headless runtime and emphasize
//   the currently selected opponent without importing combat rules here.
static gX_I0 gX_SceneBattle_GFX_Sync(gX_SceneBattle *battle) {
    if (!renderer.gfx.data || renderer.gfx.amount == 0) return;
    gX_RendererGFX *gfx = (gX_RendererGFX *)renderer.gfx.data;
    if (!gfx->common.battleScene || !gfx->models.data) return;
    gX_RendererGFX_Model *models = (gX_RendererGFX_Model *)gfx->models.data;
    gX_U32 enemyCount = battle->state.runtime.enemyCount;
    if (enemyCount > GX_BATTLE_DEMO_MAX_ENEMIES)
        enemyCount = GX_BATTLE_DEMO_MAX_ENEMIES;
    gX_F32 bob = sinf((gX_F32)battle->state.elapsed * 2.5f) * 0.035f;
    gX_F32 pulse = sinf((gX_F32)battle->state.elapsed * 4.0f) * 0.5f + 0.5f;
    gX_U32 intentEnemyIndex = UINT32_MAX;
    gX_SceneBattle_EnemyAI_Decision enemyIntent;
    gX_I1 hasEnemyIntent = gX_SceneBattle_Runtime_GetEnemyIntent(
        &battle->state.runtime, &intentEnemyIndex, &enemyIntent);
    const gX_SceneBattle_DemoImpact *impact =
        gX_SceneBattle_LatestImpactReaction(&battle->state.runtime);
    gX_U32 impactSourceActor = UINT32_MAX;
    gX_U32 impactTargetActor = UINT32_MAX;
    gX_I1 hasImpactTarget = impact && gX_SceneBattle_ImpactTargetActor(
        &battle->state.runtime, impact, &impactTargetActor);
    gX_I1 hasImpactSource = impact && !impact->isHeal
        && gX_SceneBattle_ImpactSourceActor(&battle->state.runtime, impact,
            &impactSourceActor);
    gX_F32 impactPulse = 0.0f;
    gX_F32 impactProgress = 0.0f;
    if (hasImpactTarget) {
        impactProgress = impact->age / GX_BATTLE_IMPACT_REACTION_SECONDS;
        impactPulse = sinf(impactProgress * 3.14159265f);
    }
    gX_F32 impactDirection = impactSourceActor < GX_BATTLE_DEMO_PARTY_SIZE
        ? 1.0f : -1.0f;
    gX_Color impactColor = impact ? gX_SceneBattle_ImpactColor(impact)
        : (gX_Color){255, 255, 255, 255};
    gX_Color intentColor = hasEnemyIntent
        ? gX_SceneBattle_IntentColor(enemyIntent.action)
        : (gX_Color){255, 196, 94, 255};
    static const gX_Color PartyTints[GX_BATTLE_DEMO_PARTY_SIZE] = {
        {43, 129, 178, 255}, {126, 91, 190, 255}, {59, 151, 119, 255}};
    for (gX_U32 actor = 0; actor < GX_BATTLE_GFX_ACTOR_COUNT; actor++) {
        gX_I1 partyActor = actor < GX_BATTLE_DEMO_PARTY_SIZE;
        gX_U32 enemy = partyActor ? 0U : actor - GX_BATTLE_DEMO_PARTY_SIZE;
        gX_I1 alive = partyActor
            ? battle->state.runtime.party[actor].unit.hp > 0.0
            : enemy < enemyCount
                && battle->state.runtime.enemies[enemy].unit.hp > 0.0;
        gX_F32v3 basePosition = {0.0f, 0.0f, 0.0f};
        if (alive)
            gX_SceneBattle_ActorBasePosition(&battle->state.runtime,
                actor, &basePosition);
        gX_F32 ratio = partyActor
            ? gX_SceneBattle_MeterRatio(
                battle->state.runtime.party[actor].unit.hp,
                battle->state.runtime.party[actor].unit.hpMax) : 1.0f;
        gX_I1 selected = !partyActor
            && enemy == battle->state.runtime.targetIndex;
        gX_I1 intentActor = !partyActor && hasEnemyIntent
            && enemy == intentEnemyIndex;
        gX_F32 impactOffset = 0.0f;
        if (impactPulse > 0.0f && hasImpactSource && !impact->isHeal
            && actor == impactSourceActor && actor != impactTargetActor)
            impactOffset = impactDirection * 0.42f * impactPulse;
        else if (impactPulse > 0.0f && hasImpactTarget
            && !impact->isHeal && actor == impactTargetActor)
            impactOffset = impactDirection * 0.20f * impactPulse;
        gX_I32 facing = partyActor
            ? battle->state.runtime.party[actor].facing
            : (enemy < enemyCount ? battle->state.runtime.enemies[enemy].facing
                : GX_BATTLE_DEMO_DIRECTION_WEST);
        gX_F32 facingX = 0.0f;
        gX_F32 facingZ = 0.0f;
        gX_SceneBattle_FacingDelta(facing, &facingX, &facingZ);
        gX_F32 baseX = basePosition.x;
        gX_F32 baseZ = basePosition.z;
        gX_Color tint = partyActor ? PartyTints[actor]
            : (gX_Color){130, 43, 83, 255};
        if (!partyActor && enemy < enemyCount) {
            const gX_SceneBattle_DemoEnemy *runtimeEnemy =
                &battle->state.runtime.enemies[enemy];
            ratio = gX_SceneBattle_MeterRatio(runtimeEnemy->unit.hp,
                runtimeEnemy->unit.hpMax);
            tint = gX_SceneBattle_EnemyTint(runtimeEnemy->kind);
        }
        if (!partyActor && enemy >= enemyCount) alive = false;
        for (gX_U32 part = 0; part < GX_BATTLE_GFX_PART_COUNT; part++) {
            gX_I32 index = battle->state.gfxActorModels[actor][part];
            if (index < 0 || (gX_U32)index >= gfx->models.amount
                || !gfx->models.meta[index].USED) continue;
            gX_RendererGFX_Model *model = &models[index];
            gX_F32 bodyHeight = partyActor ? 1.1f : 0.72f + ratio * 0.38f;
            if (!alive) {
                model->params.pos.y = -30.0f;
                continue;
            }
            model->params.pos.x = baseX
                + (partyActor && part == 2 ? facingX * 0.55f : 0.0f)
                + impactOffset;
            model->params.pos.z = baseZ
                + (partyActor && part == 2 ? facingZ * 0.55f : 0.0f);
            if (part == 0) {
                model->params.pos.y = partyActor ? 0.98f + bob
                    : bodyHeight * 0.5f + bob;
                if (!partyActor) model->params.scale.y = bodyHeight;
            } else if (part == 1) {
                model->params.pos.y = (partyActor ? 1.63f : bodyHeight + 0.28f) + bob;
            } else {
                if (partyActor && part == 3) {
                    model->params.pos.y = 0.36f + bob;
                } else if (!partyActor && part == 3) {
                    model->params.pos.y = (selected || intentActor) ? 0.075f : -30.0f;
                } else {
                    model->params.pos.y = (partyActor ? 1.0f : bodyHeight + 0.72f) + bob;
                }
            }
            if (!partyActor) {
                if (part == 3) {
                    model->params.albedoTint = intentActor ? intentColor
                        : (gX_Color){255, 196, 94, 255};
                    model->params.emissiveColor = intentActor ? intentColor
                        : (gX_Color){255, 196, 94, 255};
                    model->params.emissiveFactor = intentActor
                        ? 1.2f + pulse * 0.6f : (selected ? 1.0f : 0.0f);
                } else {
                    model->params.albedoTint = part == 1
                        ? (gX_Color){(gX_U8)fminf(255.0f, (gX_F32)tint.r + 70.0f),
                            (gX_U8)fminf(255.0f, (gX_F32)tint.g + 70.0f),
                            (gX_U8)fminf(255.0f, (gX_F32)tint.b + 70.0f), 255}
                        : tint;
                    model->params.emissiveColor = intentActor ? intentColor : tint;
                    model->params.emissiveFactor = intentActor
                        ? 0.65f + pulse * 0.35f
                        : (selected ? (part == 2 ? 0.9f : 0.22f) : 0.06f);
                }
                if (part == 0) model->params.scale.y = bodyHeight;
            } else {
                gX_Color partTint = tint;
                if (part == 1 && actor == GX_BATTLE_DEMO_ROLE_VANGUARD)
                    partTint = (gX_Color){202, 151, 105, 255};
                else if (part == 2)
                    partTint = actor == GX_BATTLE_DEMO_ROLE_MEDIC
                        ? (gX_Color){159, 246, 192, 255}
                        : (gX_Color){181, 218, 231, 255};
                else if (part == 3) partTint = (gX_Color){26, 48, 75, 255};
                model->params.albedoTint = partTint;
                model->params.emissiveColor = partTint;
                model->params.emissiveFactor = part == 2 ? 0.35f
                    : (battle->state.runtime.party[actor].controllerId
                        == GX_BATTLE_DEMO_CONTROLLER_LOCAL ? 0.18f : 0.06f);
                if (part == 2) model->params.rotation.y =
                    gX_SceneBattle_FacingAngle(facing);
            }
            if (hasImpactTarget && actor == impactTargetActor && part < 3
                && impactPulse > 0.0f) {
                model->params.albedoTint = gX_SceneBattle_ColorMix(
                    model->params.albedoTint, impactColor, impactPulse * 0.72f);
                model->params.emissiveColor = impactColor;
                model->params.emissiveFactor += impactPulse * 1.8f;
            }
        }
    }
    for (gX_U32 actor = 0; actor < GX_BATTLE_GFX_ACTOR_COUNT; actor++) {
        gX_I32 modelIndex = battle->state.gfxFacingModels[actor];
        if (modelIndex < 0 || (gX_U32)modelIndex >= gfx->models.amount
            || !gfx->models.meta[modelIndex].USED) continue;
        gX_RendererGFX_Model *marker = &models[modelIndex];
        gX_I1 partyActor = actor < GX_BATTLE_DEMO_PARTY_SIZE;
        gX_U32 enemy = partyActor ? 0U : actor - GX_BATTLE_DEMO_PARTY_SIZE;
        gX_I1 alive = partyActor
            ? battle->state.runtime.party[actor].unit.hp > 0.0
            : enemy < enemyCount;
        if (!partyActor && alive)
            alive = battle->state.runtime.enemies[enemy].unit.hp > 0.0;
        gX_F32v3 actorPosition;
        if (!alive || !gX_SceneBattle_ActorBasePosition(
                &battle->state.runtime, actor, &actorPosition)) {
            marker->params.pos.y = -30.0f;
            continue;
        }
        gX_I32 facing = partyActor
            ? battle->state.runtime.party[actor].facing
            : battle->state.runtime.enemies[enemy].facing;
        gX_F32 facingX = 0.0f;
        gX_F32 facingZ = 0.0f;
        gX_SceneBattle_FacingDelta(facing, &facingX, &facingZ);
        marker->params.pos = (gX_F32v3){actorPosition.x + facingX * 0.36f,
            0.19f, actorPosition.z + facingZ * 0.36f};
        marker->params.rotation.y = gX_SceneBattle_FacingAngle(facing);
    }
    gX_I32 rippleIndex = battle->state.gfxImpactRippleModel;
    if (rippleIndex >= 0 && (gX_U32)rippleIndex < gfx->models.amount
        && gfx->models.meta[rippleIndex].USED) {
        gX_RendererGFX_Model *ripple = &models[rippleIndex];
        ripple->params.pos = (gX_F32v3){0.0f, -30.0f, 0.0f};
        ripple->params.scale = (gX_F32v3){0.34f, 0.025f, 0.34f};
        ripple->params.albedoTint = (gX_Color){255, 255, 255, 255};
        ripple->params.emissiveColor = (gX_Color){255, 255, 255, 255};
        ripple->params.emissiveFactor = 0.0f;
        gX_F32v3 impactPosition;
        if (hasImpactTarget && impactPulse > 0.0f
            && gX_SceneBattle_ActorBasePosition(&battle->state.runtime,
                impactTargetActor, &impactPosition)) {
            gX_F32 radiusSquared = impactPosition.x * impactPosition.x
                + impactPosition.z * impactPosition.z;
            impactPosition.y = radiusSquared < 2.35f * 2.35f ? 0.17f : 0.145f;
            gX_F32 rippleScale = 0.85f + impactProgress * 1.05f;
            ripple->params.pos = impactPosition;
            ripple->params.scale = (gX_F32v3){rippleScale, 0.07f, rippleScale};
            ripple->params.albedoTint = gX_SceneBattle_ColorMix(
                (gX_Color){35, 54, 72, 255}, impactColor, impactPulse);
            ripple->params.emissiveColor = impactColor;
            ripple->params.emissiveFactor = impactPulse * 2.0f;
        }
    }
    gX_I32 intentCellIndex = battle->state.gfxIntentCellModel;
    if (intentCellIndex >= 0 && (gX_U32)intentCellIndex < gfx->models.amount
        && gfx->models.meta[intentCellIndex].USED) {
        gX_RendererGFX_Model *intentCell = &models[intentCellIndex];
        intentCell->params.pos = (gX_F32v3){0.0f, -30.0f, 0.0f};
        intentCell->params.scale = (gX_F32v3){0.36f, 0.025f, 0.36f};
        intentCell->params.albedoTint = (gX_Color){255, 255, 255, 255};
        intentCell->params.emissiveColor = (gX_Color){255, 255, 255, 255};
        intentCell->params.emissiveFactor = 0.0f;
        if (hasEnemyIntent && intentEnemyIndex < enemyCount) {
            gX_U32 intentTargetActor = enemyIntent.action == GX_BATTLE_AI_ACTION_GUARD
                ? intentEnemyIndex + GX_BATTLE_DEMO_PARTY_SIZE
                : battle->state.runtime.enemies[intentEnemyIndex].targetPartyIndex;
            gX_I1 impactOwnsTargetCell = hasImpactTarget
                && impactTargetActor == intentTargetActor;
            gX_F32v3 actorPosition;
            gX_F32v3 cellCenter;
            if (!impactOwnsTargetCell
                && gX_SceneBattle_ActorBasePosition(&battle->state.runtime,
                    intentTargetActor, &actorPosition)
                && gX_SceneBattle_GFX_CellCenter(&actorPosition, &cellCenter)) {
                gX_F32 radiusSquared = cellCenter.x * cellCenter.x
                    + cellCenter.z * cellCenter.z;
                gX_F32 markerPulse = 0.5f + 0.5f * pulse;
                cellCenter.y = radiusSquared < 2.35f * 2.35f ? 0.17f : 0.145f;
                intentCell->params.pos = cellCenter;
                intentCell->params.scale = (gX_F32v3){
                    0.36f + markerPulse * 0.05f, 0.025f,
                    0.36f + markerPulse * 0.05f};
                intentCell->params.albedoTint = gX_SceneBattle_ColorMix(
                    (gX_Color){35, 54, 72, 255}, intentColor, 0.9f);
                intentCell->params.emissiveColor = intentColor;
                intentCell->params.emissiveFactor = 0.8f + markerPulse * 0.9f;
            }
        }
    }
}

//ai: translate the runtime's bounded enemy roster into renderer-neutral actor
//   state and let the GFX presentation consume only positions and colors.
static gX_Color gX_SceneBattle_EnemyTint(gX_I32 kind) {
    switch (kind) {
        case GX_BATTLE_DEMO_ENEMY_ADEPT: return (gX_Color){42, 112, 164, 255};
        case GX_BATTLE_DEMO_ENEMY_SENTINEL: return (gX_Color){160, 103, 35, 255};
        default: return (gX_Color){130, 43, 83, 255};
    }
}

//ai: use compact actor labels so the fixed-width recent-impact panel stays readable.
static const gX_I8 *gX_SceneBattle_ImpactName(gX_I8 kind, gX_U8 actorIndex) {
    if (kind == GX_BATTLE_DEMO_IMPACT_PLAYER) {
        static const gX_I8 *PartyNames[GX_BATTLE_DEMO_PARTY_SIZE] = {
            "VANGUARD", "ARCANIST", "MEDIC"};
        return actorIndex < GX_BATTLE_DEMO_PARTY_SIZE
            ? PartyNames[actorIndex] : "PARTY";
    }
    switch (kind) {
        case GX_BATTLE_DEMO_ENEMY_WARDEN: return "WARDEN";
        case GX_BATTLE_DEMO_ENEMY_ADEPT: return "ADEPT";
        case GX_BATTLE_DEMO_ENEMY_SENTINEL: return "SENTINEL";
        default: return "HOSTILE";
    }
}

//ai: draw the four most recent bounded HP changes independently of floater lifetime.
static gX_I0 gX_SceneBattle_DrawImpactHistory(
    const gX_SceneBattle_Runtime *runtime, gX_F32 width, gX_F32 height,
    gX_F32 scale) {
    gX_Rectangle panel = {width * 0.37f, height * 0.12f,
        width * 0.25f, height * 0.17f};
    gX_SceneBattle_DrawPanel(panel, (gX_Color){4, 10, 24, 232},
        (gX_Color){70, 134, 171, 210});
    gX_SceneBattle_DrawText("RECENT IMPACTS", panel.x + 10.0f * scale,
        panel.y + 7.0f * scale, 12.0f * scale, (gX_Color){130, 215, 232, 255});
    if (runtime->impactCount == 0) {
        gX_SceneBattle_DrawText("NO COMBAT EVENTS", panel.x + 10.0f * scale,
            panel.y + 31.0f * scale, 11.0f * scale, (gX_Color){148, 170, 185, 255});
        return;
    }
    gX_U32 visibleCount = runtime->impactCount < 4 ? runtime->impactCount : 4;
    for (gX_U32 row = 0; row < visibleCount; row++) {
        gX_U32 index = (runtime->impactHead + GX_BATTLE_DEMO_IMPACT_HISTORY
            - 1U - row) % GX_BATTLE_DEMO_IMPACT_HISTORY;
        const gX_SceneBattle_DemoImpact *impact = &runtime->impacts[index];
        gX_I8 entry[56];
        snprintf((char *)entry, sizeof(entry), "%s > %s %c%.0f",
            gX_SceneBattle_ImpactName(impact->sourceKind, impact->sourceIndex),
            gX_SceneBattle_ImpactName(impact->targetKind, impact->targetIndex),
            impact->isHeal ? '+' : '-', impact->amount);
        gX_Color color = impact->isHeal ? (gX_Color){111, 225, 174, 255}
            : (impact->targetKind == GX_BATTLE_DEMO_IMPACT_PLAYER
                ? (gX_Color){255, 132, 143, 255}
                : (gX_Color){255, 202, 124, 255});
        gX_SceneBattle_DrawText(entry, panel.x + 10.0f * scale,
            panel.y + (28.0f + row * 21.0f) * scale, 10.5f * scale, color);
    }
}

//ai: float fresh signed HP changes above their target while history entries remain in the ring.
static gX_I0 gX_SceneBattle_DrawImpactFloaters(
    const gX_SceneBattle_Runtime *runtime, gX_F32 width, gX_F32 height,
    gX_F32 scale) {
    gX_F32 ground = height * 0.585f;
    for (gX_U32 i = 0; i < runtime->impactCount; i++) {
        gX_U32 index = (runtime->impactHead + GX_BATTLE_DEMO_IMPACT_HISTORY
            - 1U - i) % GX_BATTLE_DEMO_IMPACT_HISTORY;
        const gX_SceneBattle_DemoImpact *impact = &runtime->impacts[index];
        if (impact->age >= GX_BATTLE_DEMO_IMPACT_SECONDS) continue;
        gX_F32 progress = impact->age / GX_BATTLE_DEMO_IMPACT_SECONDS;
        gX_F32 figureScale = 1.0f;
        gX_F32 x = width * (0.235f + 0.035f * (gX_F32)impact->targetIndex);
        if (impact->targetKind != GX_BATTLE_DEMO_IMPACT_PLAYER) {
            if (impact->targetIndex >= runtime->enemyCount
                || runtime->enemies[impact->targetIndex].kind != impact->targetKind)
                continue;
            figureScale = runtime->enemyCount == 1 ? 1.0f : 0.66f;
            x = runtime->enemyCount == 1 ? width * 0.705f
                : width * (0.62f + 0.19f * (gX_F32)impact->targetIndex);
        }
        gX_I8 amount[24];
        snprintf((char *)amount, sizeof(amount), "%c%.0f",
            impact->isHeal ? '+' : '-', impact->amount);
        gX_U8 alpha = (gX_U8)(255.0f * (1.0f - progress));
        gX_F32 y = ground - height * figureScale * (0.32f + progress * 0.10f);
        gX_Color color = impact->isHeal ? (gX_Color){111, 255, 185, alpha}
            : (gX_Color){255, 217, 131, alpha};
        gX_SceneBattle_DrawCenteredText(amount, x + 1.5f * scale,
            y + 1.5f * scale, 24.0f * scale * figureScale,
            (gX_Color){8, 10, 20, alpha});
        gX_SceneBattle_DrawCenteredText(amount, x, y,
            24.0f * scale * figureScale, color);
    }
}

//ai: expose the exact delayed enemy decision in the unused gap above the command panels.
static gX_I0 gX_SceneBattle_DrawEnemyIntent(
    const gX_SceneBattle_Runtime *runtime, gX_F32 width, gX_F32 height,
    gX_F32 scale) {
    gX_U32 enemyIndex = UINT32_MAX;
    gX_SceneBattle_EnemyAI_Decision decision;
    if (!gX_SceneBattle_Runtime_GetEnemyIntent(runtime, &enemyIndex,
            &decision)) return;
    const gX_SceneBattle_DemoEnemy *enemy = &runtime->enemies[enemyIndex];
    gX_I8 intentText[80];
    if (decision.action == GX_BATTLE_AI_ACTION_GUARD) {
        snprintf((char *)intentText, sizeof(intentText), "%s // GUARDS",
            gX_SceneBattle_EnemyName(enemy->kind));
    } else if (decision.action == GX_BATTLE_AI_ACTION_MAGIC
        && decision.spellIndex >= 0
        && decision.spellIndex < GX_BATTLE_DEMO_SPELL_COUNT) {
        const gX_SceneBattle_SpellCandidate *spell =
            &runtime->rules.spells[decision.spellIndex];
        const gX_I8 *elementName = "ELEMENTAL";
        if (spell->element == GX_BATTLE_ELEMENT_FIRE) elementName = "FIRE";
        else if (spell->element == GX_BATTLE_ELEMENT_ICE) elementName = "ICE";
        else if (spell->element == GX_BATTLE_ELEMENT_LIGHTNING)
            elementName = "LIGHTNING";
        snprintf((char *)intentText, sizeof(intentText), "%s // CASTS %s",
            gX_SceneBattle_EnemyName(enemy->kind), elementName);
    } else {
        snprintf((char *)intentText, sizeof(intentText), "%s // STRIKES",
            gX_SceneBattle_EnemyName(enemy->kind));
    }
    gX_Color intentColor = gX_SceneBattle_IntentColor(decision.action);
    gX_Rectangle panel = {width * 0.35f, height * 0.665f,
        width * 0.30f, height * 0.032f};
    gX_SceneBattle_DrawPanel(panel, (gX_Color){4, 10, 24, 236},
        (gX_Color){intentColor.r, intentColor.g, intentColor.b, 220});
    gX_SceneBattle_DrawCenteredText(intentText, width * 0.50f,
        height * 0.671f, 12.0f * scale, intentColor);
}

//ai: select the themed built-in backdrop once when the headless runtime enters a new wave.
static gX_I0 gX_SceneBattle_ApplyWaveBackdrop(gX_SceneBattle *battle) {
    if (!battle || battle->state.backdropWave == battle->state.runtime.waveIndex)
        return;
    if (maindata.masterIndexSwitch || maindata.masterIndexSwitched) return;
    if (!renderer.background.data || renderer.background.amount == 0) return;
    gX_RendererBackground *background =
        (gX_RendererBackground *)renderer.background.data;
    if (battle->state.runtime.waveIndex == 1)
        gX_RendererBackground_Preset_BuiltIn(background, gX_BG_PRESET_TITLE);
    battle->state.backdropWave = battle->state.runtime.waveIndex;
}

//ai: let the scene audit render wave two without importing input or renderer state into tests.
static gX_I1 gX_SceneBattle_ApplyTestWave(gX_SceneBattle *battle,
    gX_F64 elapsed) {
    const char *advanceWave = getenv("GX_BATTLE_TEST_ADVANCE_WAVE");
    gX_SceneBattle_Runtime *runtime = &battle->state.runtime;
    if (!advanceWave || advanceWave[0] != '1' || runtime->waveIndex != 0
        || !gX_SceneBattle_Runtime_IsPlayerTurn(runtime)) return true;
    gX_F32 attackDamage = runtime->rules.playerAttackDamage;
    runtime->rules.playerAttackDamage = (gX_F32)runtime->enemies[0].unit.hp + 1.0f;
    gX_I1 resolved = gX_SceneBattle_Runtime_ConfirmAction(runtime,
        GX_BATTLE_DEMO_ACTION_ATTACK, elapsed);
    runtime->rules.playerAttackDamage = attackDamage;
    if (resolved) battle->state.actionConfirmedUntil = elapsed + 1.0;
    return resolved;
}

//ai: generate deterministic physical or Fire impact probes only for visual capture.
static gX_I1 gX_SceneBattle_ApplyTestImpact(gX_SceneBattle *battle,
    gX_F64 elapsed) {
    const char *testImpact = getenv("GX_BATTLE_TEST_IMPACT");
    gX_SceneBattle_Runtime *runtime = &battle->state.runtime;
    if (!testImpact || (testImpact[0] != '1' && testImpact[0] != 'm')
        || runtime->waveIndex != 0) return true;
    gX_I1 firstStrike = runtime->impactCount == 0 && elapsed <= 0.1;
    gX_I1 followup = runtime->impactCount == 2 && elapsed >= 0.8;
    if (followup) {
        gX_U32 actorIndex = UINT32_MAX;
        if (!gX_SceneBattle_Runtime_GetControlledActor(runtime, &actorIndex))
            return false;
        gX_SceneBattle_Cooldown_Init(&runtime->party[actorIndex].cooldowns);
        runtime->party[actorIndex].energy = GX_BATTLE_DEMO_ATB_MAX;
    }
    if ((!firstStrike && !followup)
        || !gX_SceneBattle_Runtime_IsPlayerTurn(runtime)) return true;
    gX_I32 action = firstStrike && testImpact[0] == 'm'
        ? GX_BATTLE_DEMO_ACTION_MAGIC : GX_BATTLE_DEMO_ACTION_ATTACK;
    if (!gX_SceneBattle_Runtime_ConfirmAction(runtime,
            action, elapsed)) return false;
    battle->state.actionConfirmedUntil = elapsed + 1.0;
    return true;
}

//ai: let the visual gate capture one independent enemy telegraph before execution.
static gX_I1 gX_SceneBattle_ApplyTestIntent(gX_SceneBattle *battle,
    gX_F64 elapsed) {
    const char *testIntent = getenv("GX_BATTLE_TEST_INTENT");
    gX_SceneBattle_Runtime *runtime = &battle->state.runtime;
    if (!testIntent || testIntent[0] != '1' || runtime->waveIndex != 0
        || runtime->turnNumber != 0 || runtime->impactCount != 0
        || !runtime->enemies[0].intentValid) return true;
    runtime->enemies[0].intent.action = GX_BATTLE_AI_ACTION_MAGIC;
    runtime->enemies[0].intent.spellIndex = 1;
    runtime->enemies[0].targetPartyIndex = 0;
    runtime->enemies[0].intentReadyAt = elapsed + 1.0;
    return true;
}

//ai: bind the existing presentation to live demo meters, items, turn state, and feedback.
static gX_I0 gX_SceneBattle_DrawHud(gX_SceneBattle *battle, gX_F32 width,
    gX_F32 height, gX_F32 scale, gX_F32 pulse) {
    gX_SceneBattle_Runtime *runtime = &battle->state.runtime;
    gX_Rectangle playerPanel = {width * 0.045f, height * 0.70f, width * 0.27f, height * 0.215f};
    gX_Rectangle commandPanel = {width * 0.345f, height * 0.70f, width * 0.31f, height * 0.215f};
    gX_Rectangle enemyPanel = {width * 0.68f, height * 0.70f, width * 0.275f, height * 0.215f};
    gX_Color panelFill = {5, 12, 29, 242};
    gX_Color playerBorder = {68, 173, 217, 230};
    gX_Color enemyBorder = {207, 77, 105, 230};
    gX_F32 barH = height * 0.022f;
    gX_F32 barW = enemyPanel.width - 138.0f * scale;
    gX_I8 enemyHpText[48];
    gX_I8 footerText[64];
    gX_I8 waveText[32];
    gX_I8 targetText[48];
    gX_I8 playerSlotText[48];
    gX_I8 enemySlotText[48];
    gX_U32 livingEnemies = 0;
    gX_U32 controlledIndex = 0;
    gX_SceneBattle_Runtime_GetControlledActor(runtime, &controlledIndex);
    if (controlledIndex >= GX_BATTLE_DEMO_PARTY_SIZE) controlledIndex = 0;
    gX_SceneBattle_DemoPartyActor *controlled = &runtime->party[controlledIndex];
    gX_U32 targetIndex = runtime->targetIndex < runtime->enemyCount
        ? runtime->targetIndex : 0;
    gX_SceneBattle_DemoEnemy *target = &runtime->enemies[targetIndex];
    gX_I32 colDistance = (gX_I32)target->gridCol - controlled->gridCol;
    gX_I32 rowDistance = (gX_I32)target->gridRow - controlled->gridRow;
    if (colDistance < 0) colDistance = -colDistance;
    if (rowDistance < 0) rowDistance = -rowDistance;
    static const gX_I8 *directionNames[4] = {
        "NORTH", "EAST", "SOUTH", "WEST"};
    gX_U32 playerFacing = controlled->facing < 4
        ? controlled->facing : GX_BATTLE_DEMO_DIRECTION_EAST;
    const gX_I8 *turnText = "ATB CHARGING";
    const gX_I8 *enemyStatus = "STATUS  RIFTBOUND";
    gX_Color turnColor = {247, 150, 164, 255};
    const gX_I8 *commands[] = {"ATTACK", "MAGIC", "ITEM", "GUARD", "RUN"};
    snprintf((char *)enemyHpText, sizeof(enemyHpText), "%.0f / %.0f",
        target->unit.hp, target->unit.hpMax);
    for (gX_U32 i = 0; i < runtime->enemyCount; i++)
        if (runtime->enemies[i].unit.hp > 0.0) livingEnemies++;
    snprintf((char *)waveText, sizeof(waveText), "WAVE %02u / %02u",
        (gX_U32)runtime->waveIndex + 1,
        (gX_U32)GX_BATTLE_DEMO_WAVE_COUNT);
    snprintf((char *)targetText, sizeof(targetText),
        "TARGET %u/%u // RANGE %u", (gX_U32)targetIndex + 1,
        livingEnemies, (gX_U32)(colDistance + rowDistance));
    snprintf((char *)playerSlotText, sizeof(playerSlotText),
        "SLOT C%u R%u // %s", (gX_U32)controlled->gridCol + 1,
        (gX_U32)controlled->gridRow + 1, directionNames[playerFacing]);
    snprintf((char *)footerText, sizeof(footerText), "POTS %d // %s",
        runtime->rules.potionCount, playerSlotText);
    snprintf((char *)enemySlotText, sizeof(enemySlotText),
        "SLOT C%u R%u", (gX_U32)target->gridCol + 1,
        (gX_U32)target->gridRow + 1);
    if (target->unit.hp <= 0.0) enemyStatus = "STATUS  DEFEATED";
    else if (target->guarding) enemyStatus = "STATUS  GUARDING";
    if (runtime->outcome == GX_BATTLE_DEMO_OUTCOME_VICTORY) {
        turnText = "VICTORY";
        turnColor = (gX_Color){255, 220, 127, 255};
    } else if (runtime->outcome == GX_BATTLE_DEMO_OUTCOME_DEFEAT) {
        turnText = "DEFEAT";
    } else if (runtime->outcome == GX_BATTLE_DEMO_OUTCOME_ESCAPED) {
        turnText = "ESCAPED";
    } else if (gX_SceneBattle_Runtime_IsActorReady(runtime, controlledIndex)) {
        turnText = "ATB READY";
        turnColor = (gX_Color){255, 220, 127, 255};
    }

    gX_Rectangle_Draw((gX_Rectangle){width * 0.045f, height * 0.018f, width * 0.91f, height * 0.068f}, (gX_Color){4, 10, 26, 255});
    gX_Rectangle_Draw_Border((gX_Rectangle){width * 0.045f, height * 0.018f, width * 0.91f, height * 0.068f}, (gX_Color){89, 196, 226, 220});
    gX_SceneBattle_DrawPanel(playerPanel, panelFill, playerBorder);
    gX_SceneBattle_DrawPanel(commandPanel, panelFill, (gX_Color){219, 173, 67, 230});
    gX_SceneBattle_DrawPanel(enemyPanel, panelFill, enemyBorder);

    gX_SceneBattle_DrawText("PARTY // 3 ACTORS", playerPanel.x + 12.0f * scale,
        playerPanel.y + 9.0f * scale, 15.0f * scale, gX_WHITE);
    gX_SceneBattle_DrawText(gX_SceneBattle_EnemyName(target->kind), enemyPanel.x + 12.0f * scale, enemyPanel.y + 9.0f * scale, 19.0f * scale, gX_WHITE);
    gX_SceneBattle_DrawText("HP", enemyPanel.x + 12.0f * scale, enemyPanel.y + 38.0f * scale, 14.0f * scale, (gX_Color){245, 168, 177, 255});
    gX_Rectangle_Draw_Bar((gX_Rectangle){enemyPanel.x + 42.0f * scale, enemyPanel.y + 38.0f * scale, barW, barH}, gX_SceneBattle_MeterRatio(target->unit.hp, target->unit.hpMax), (gX_Color){58, 28, 44, 255}, (gX_Color){218, 74, 93, 255}, (gX_Color){137, 62, 84, 255});
    gX_Rectangle_Draw((gX_Rectangle){enemyPanel.x + enemyPanel.width - 88.0f * scale, enemyPanel.y + 34.0f * scale, 82.0f * scale, 22.0f * scale}, (gX_Color){28, 5, 15, 235});
    gX_SceneBattle_DrawText(enemyHpText, enemyPanel.x + enemyPanel.width - 83.0f * scale, enemyPanel.y + 36.0f * scale, 13.0f * scale, gX_WHITE);
    static const gX_I8 *PartyNames[GX_BATTLE_DEMO_PARTY_SIZE] = {
        "VANGUARD", "ARCANIST", "MEDIC"};
    static const gX_Color PartyColors[GX_BATTLE_DEMO_PARTY_SIZE] = {
        {76, 190, 227, 255}, {186, 145, 241, 255}, {102, 222, 166, 255}};
    for (gX_U32 actorIndex = 0; actorIndex < GX_BATTLE_DEMO_PARTY_SIZE;
            actorIndex++) {
        const gX_SceneBattle_DemoPartyActor *actor = &runtime->party[actorIndex];
        gX_F32 rowY = playerPanel.y + (26.0f + 42.0f * actorIndex) * scale;
        gX_F32 meterWidth = (playerPanel.width - 82.0f * scale);
        gX_I8 mpText[20];
        snprintf((char *)mpText, sizeof(mpText), "%.0f MP", actor->unit.mp);
        gX_Color nameColor = actor->unit.hp > 0.0
            ? PartyColors[actorIndex] : (gX_Color){110, 116, 129, 255};
        gX_SceneBattle_DrawText(PartyNames[actorIndex],
            playerPanel.x + 12.0f * scale, rowY,
            11.5f * scale, nameColor);
        gX_SceneBattle_DrawText(mpText,
            playerPanel.x + 103.0f * scale, rowY,
            10.0f * scale, (gX_Color){154, 183, 203, 255});
        gX_SceneBattle_DrawText(actor->controllerId
                == GX_BATTLE_DEMO_CONTROLLER_LOCAL ? "YOU" : "AI",
            playerPanel.x + playerPanel.width - 37.0f * scale, rowY,
            10.0f * scale, actor->controllerId
                == GX_BATTLE_DEMO_CONTROLLER_LOCAL
                    ? (gX_Color){255, 220, 127, 255}
                    : (gX_Color){150, 177, 195, 255});
        gX_SceneBattle_DrawText("HP", playerPanel.x + 12.0f * scale,
            rowY + 13.0f * scale, 8.0f * scale,
            (gX_Color){155, 190, 207, 255});
        gX_Rectangle_Draw_Bar((gX_Rectangle){playerPanel.x + 35.0f * scale,
            rowY + 14.0f * scale, meterWidth, 5.0f * scale},
            gX_SceneBattle_MeterRatio(actor->unit.hp, actor->unit.hpMax),
            (gX_Color){26, 38, 57, 255}, (gX_Color){52, 192, 151, 255},
            (gX_Color){74, 116, 139, 255});
        gX_SceneBattle_DrawText("ATB", playerPanel.x + 12.0f * scale,
            rowY + 23.0f * scale, 8.0f * scale,
            (gX_Color){155, 190, 207, 255});
        gX_Rectangle_Draw_Bar((gX_Rectangle){playerPanel.x + 35.0f * scale,
            rowY + 24.0f * scale, meterWidth, 5.0f * scale},
            gX_SceneBattle_MeterRatio(actor->energy, GX_BATTLE_DEMO_ATB_MAX),
            (gX_Color){26, 38, 57, 255},
            actor->energy >= GX_BATTLE_DEMO_ATB_MAX
                ? (gX_Color){249, 205, 101, 255}
                : (gX_Color){87, 151, 220, 255},
            (gX_Color){74, 116, 139, 255});
    }
    gX_SceneBattle_DrawText(footerText, playerPanel.x + 12.0f * scale,
        playerPanel.y + 151.0f * scale, 10.0f * scale,
        (gX_Color){96, 224, 184, 255});
    gX_SceneBattle_DrawText(enemyStatus, enemyPanel.x + 12.0f * scale, enemyPanel.y + 100.0f * scale, 13.0f * scale, target->kind == GX_BATTLE_DEMO_ENEMY_ADEPT ? (gX_Color){112, 201, 236, 255} : (gX_Color){247, 119, 126, 255});
    gX_SceneBattle_DrawText(enemySlotText, enemyPanel.x + 12.0f * scale,
        enemyPanel.y + 122.0f * scale, 11.0f * scale,
        (gX_Color){182, 185, 194, 255});

    gX_SceneBattle_DrawText("COMMAND", commandPanel.x + 12.0f * scale, commandPanel.y + 9.0f * scale, 17.0f * scale, (gX_Color){255, 220, 127, 255});
    gX_F32 commandY = commandPanel.y + 43.0f * scale;
    gX_F32 commandW = (commandPanel.width - 36.0f * scale) / 5.0f;
    for (gX_I32 i = 0; i < 5; i++) {
        gX_F32 commandX = commandPanel.x + 8.0f * scale + (commandW + 4.0f * scale) * (gX_F32)i;
        gX_I1 selected = battle->state.selectedAction == i;
        gX_Color commandFill = selected ? (gX_Color){138, 100, 36, (gX_U8)(185.0f + pulse * 55.0f)} : (gX_Color){16, 29, 49, 245};
        gX_Color commandBorder = selected ? (gX_Color){255, 222, 124, 255} : (gX_Color){61, 105, 135, 220};
        gX_Rectangle_Draw((gX_Rectangle){commandX, commandY, commandW, height * 0.067f}, commandFill);
        gX_Rectangle_Draw_Border((gX_Rectangle){commandX, commandY, commandW, height * 0.067f}, commandBorder);
        gX_SceneBattle_DrawCenteredText(commands[i], commandX + commandW * 0.5f, commandY + height * 0.022f, 13.0f * scale, selected ? gX_WHITE : (gX_Color){190, 207, 219, 255});
    }
    gX_Rectangle_Draw((gX_Rectangle){width * 0.37f, height * 0.612f, width * 0.26f, height * 0.047f}, (gX_Color){4, 10, 24, 235});
    gX_Rectangle_Draw_Border((gX_Rectangle){width * 0.37f, height * 0.612f, width * 0.26f, height * 0.047f}, (gX_Color){219, 173, 67, 180});
    gX_SceneBattle_DrawCenteredText(turnText, width * 0.50f, height * 0.625f, 19.0f * scale, turnColor);
    gX_SceneBattle_DrawText("ARROWS: COMMAND / TARGET // ENTER: ACT",
        commandPanel.x + 12.0f * scale, commandPanel.y + 103.0f * scale,
        9.0f * scale, (gX_Color){155, 185, 203, 255});
    gX_SceneBattle_DrawText("TAB: CONTROL // WASD: MOVE // Q/E: TURN",
        commandPanel.x + 12.0f * scale, commandPanel.y + 121.0f * scale,
        8.0f * scale, (gX_Color){155, 185, 203, 255});
    gX_SceneBattle_DrawEnemyIntent(runtime, width, height, scale);

    gX_SceneBattle_DrawText("BATTLE // ASTRAL RIFT", width * 0.06f, height * 0.035f, 23.0f * scale, (gX_Color){133, 224, 238, 255});
    gX_SceneBattle_DrawText(waveText, width * 0.79f, height * 0.039f, 16.0f * scale, (gX_Color){228, 236, 245, 255});
    gX_SceneBattle_DrawText("ENCOUNTER 01", width * 0.06f, height * 0.062f, 13.0f * scale, (gX_Color){155, 185, 203, 255});
    gX_SceneBattle_DrawText(targetText, width * 0.71f, height * 0.062f, 12.0f * scale, (gX_Color){241, 151, 161, 255});
    gX_Rectangle_Draw((gX_Rectangle){width * 0.045f, height * 0.928f, width * 0.91f, height * 0.046f}, (gX_Color){4, 10, 24, 242});
    gX_Rectangle_Draw_Border((gX_Rectangle){width * 0.045f, height * 0.928f, width * 0.91f, height * 0.046f}, (gX_Color){50, 106, 151, 165});
    gX_Color messageColor = battle->state.actionConfirmedUntil > battle->state.elapsed
        ? (gX_Color){255, 226, 139, 255} : (gX_Color){154, 181, 198, 255};
    gX_SceneBattle_DrawCenteredText(runtime->message, width * 0.50f,
        height * 0.934f, 16.0f * scale, messageColor);
}

//ai: initialize the live combat demo before exposing the scene to updates.
gX_I32 gX_SceneBattle_Load(gX_SceneBattle *battle) {
    if (!battle) return -1;
    strcpy(battle->name, "Battle");
    strcpy(battle->description, "realtime tactical party battle with independent ATB");
    if (!gX_SceneBattle_Runtime_Init(&battle->state.runtime)) return -1;
    if (gX_SceneBattle_Runtime_RegisterPolisher())
        gX_SceneBattle_Runtime_ApplyTuning(&battle->state.runtime);
    else
        gX_TraceLog(gX_LOG_WARNING, "BATTLE: polisher is full; using demo defaults");
    battle->loaded = true;
    battle->state.elapsed = maindata.timeElapsed;
    battle->state.previousElapsed = maindata.timeElapsed;
    battle->state.actionConfirmedUntil = 0.0;
    battle->state.selectedAction = 0;
    battle->state.backdropWave = battle->state.runtime.waveIndex;
    battle->state.gfxImpactRippleModel = -1;
    battle->state.gfxIntentCellModel = -1;
    for (gX_U32 actor = 0; actor < GX_BATTLE_GFX_ACTOR_COUNT; actor++)
    {
        battle->state.gfxFacingModels[actor] = -1;
        for (gX_U32 part = 0; part < GX_BATTLE_GFX_PART_COUNT; part++)
            battle->state.gfxActorModels[actor][part] = -1;
    }
    return 0;
}

gX_I32 gX_SceneBattle_Unload(gX_SceneBattle *battle) {
    if (!battle) return -1;
    battle->loaded = false;
    return 0;
}

gX_I32 gX_SceneBattle_Update(gX_SceneBattle *battle) {
    if (!battle || !battle->loaded) return -1;
    gX_F64 elapsed = maindata.timeElapsed;
    gX_F64 delta = elapsed - battle->state.previousElapsed;
    if (!isfinite(delta) || delta < 0.0) delta = 0.0;
    battle->state.elapsed = elapsed;
    battle->state.previousElapsed = elapsed;
    if (!gX_SceneBattle_Runtime_ApplyTuning(&battle->state.runtime)) return -1;
    if (!gX_SceneBattle_Runtime_Update(&battle->state.runtime, elapsed, (gX_F32)delta))
        return -1;
    if (!gX_SceneBattle_ApplyTestWave(battle, elapsed)) return -1;
    if (!gX_SceneBattle_ApplyTestImpact(battle, elapsed)) return -1;
    if (!gX_SceneBattle_ApplyTestIntent(battle, elapsed)) return -1;
    gX_U32 controlledIndex = UINT32_MAX;
    if (gX_SceneBattle_Runtime_GetControlledActor(&battle->state.runtime,
            &controlledIndex)) {
        if (gX_Key_IsPressed(gX_KEY_TAB)) {
            for (gX_U32 step = 1; step <= GX_BATTLE_DEMO_PARTY_SIZE; step++) {
                gX_U32 candidate = (controlledIndex + step)
                    % GX_BATTLE_DEMO_PARTY_SIZE;
                if (battle->state.runtime.party[candidate].unit.hp > 0.0
                    && gX_SceneBattle_Runtime_SetControlledActor(
                        &battle->state.runtime, candidate)) break;
            }
        }
        if (gX_Key_IsPressed(gX_KEY_W))
            gX_SceneBattle_Runtime_MovePlayer(&battle->state.runtime,
                GX_BATTLE_DEMO_DIRECTION_NORTH, elapsed);
        else if (gX_Key_IsPressed(gX_KEY_D))
            gX_SceneBattle_Runtime_MovePlayer(&battle->state.runtime,
                GX_BATTLE_DEMO_DIRECTION_EAST, elapsed);
        else if (gX_Key_IsPressed(gX_KEY_S))
            gX_SceneBattle_Runtime_MovePlayer(&battle->state.runtime,
                GX_BATTLE_DEMO_DIRECTION_SOUTH, elapsed);
        else if (gX_Key_IsPressed(gX_KEY_A))
            gX_SceneBattle_Runtime_MovePlayer(&battle->state.runtime,
                GX_BATTLE_DEMO_DIRECTION_WEST, elapsed);
        else if (gX_Key_IsPressed(gX_KEY_Q))
            gX_SceneBattle_Runtime_TurnPlayer(&battle->state.runtime, -1, elapsed);
        else if (gX_Key_IsPressed(gX_KEY_E))
            gX_SceneBattle_Runtime_TurnPlayer(&battle->state.runtime, 1, elapsed);
        else if (gX_Key_IsPressed(gX_KEY_LEFT))
            battle->state.selectedAction = (battle->state.selectedAction + 4) % 5;
        else if (gX_Key_IsPressed(gX_KEY_RIGHT))
            battle->state.selectedAction = (battle->state.selectedAction + 1) % 5;
        else if (gX_Key_IsPressed(gX_KEY_UP) || gX_Key_IsPressed(gX_KEY_DOWN)) {
            gX_I32 direction = gX_Key_IsPressed(gX_KEY_UP) ? 1 : -1;
            for (gX_U32 step = 0; step < battle->state.runtime.enemyCount; step++) {
                gX_U32 count = battle->state.runtime.enemyCount;
                gX_U32 current = battle->state.runtime.targetIndex;
                gX_U32 candidate = direction > 0
                    ? (current + step + 1) % count
                    : (current + count - (step + 1) % count) % count;
                if (gX_SceneBattle_Runtime_SelectTarget(
                        &battle->state.runtime, candidate)) break;
            }
        }
        else if (gX_Key_IsPressed(gX_KEY_ENTER) || gX_Key_IsPressed(gX_KEY_SPACE)) {
            if (gX_SceneBattle_Runtime_ConfirmAction(&battle->state.runtime,
                    battle->state.selectedAction, elapsed))
                battle->state.actionConfirmedUntil = elapsed + 1.0;
        }
    }
    gX_SceneBattle_ApplyWaveBackdrop(battle);
    gX_SceneBattle_GFX_Ensure(battle);
    gX_SceneBattle_GFX_Sync(battle);
    return 0;
}

gX_I32 gX_SceneBattle_Draw(gX_SceneBattle *battle) {
    if (!battle || !battle->loaded) return -1;
    gX_F32 width = (gX_F32)maindata.screenWidth;
    gX_F32 height = (gX_F32)maindata.screenHeight;
    gX_F32 scale = gX_SceneBattle_Scale();
    gX_F32 pulse = (gX_F32)(sinf((gX_F32)battle->state.elapsed * 4.0f) * 0.5f + 0.5f);
    gX_SceneBattle_DrawImpactFloaters(&battle->state.runtime, width, height, scale);
    gX_SceneBattle_DrawImpactHistory(&battle->state.runtime, width, height, scale);
    gX_SceneBattle_DrawHud(battle, width, height, scale, pulse);
    gX_Batch_Flush();
    return 0;
}
