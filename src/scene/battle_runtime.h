/**********************************************************************************************
*
*   [gX: src/scene/battle_runtime.h]
*   Headless battle demo runtime — actor-owned realtime and ATB simulation
*
*   Copyright (c) 2026 lanttu, gX contributors
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


#ifndef GX_SCENE_BATTLE_RUNTIME_H
#define GX_SCENE_BATTLE_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "battle_actions.h"
#include "battle_ai.h"
#include "battle_logic.h"

#define GX_BATTLE_DEMO_ACTION_ATTACK 0
#define GX_BATTLE_DEMO_ACTION_MAGIC 1
#define GX_BATTLE_DEMO_ACTION_ITEM 2
#define GX_BATTLE_DEMO_ACTION_GUARD 3
#define GX_BATTLE_DEMO_ACTION_RUN 4

#define GX_BATTLE_DEMO_OUTCOME_ACTIVE 0
#define GX_BATTLE_DEMO_OUTCOME_VICTORY 1
#define GX_BATTLE_DEMO_OUTCOME_DEFEAT 2
#define GX_BATTLE_DEMO_OUTCOME_ESCAPED 3

//ai: bound the realtime roster and give each actor its own independently charged gauge.
#define GX_BATTLE_DEMO_SPELL_COUNT 4
#define GX_BATTLE_DEMO_MAX_ENEMIES 2
#define GX_BATTLE_DEMO_PARTY_SIZE 3
#define GX_BATTLE_DEMO_WAVE_COUNT 2
#define GX_BATTLE_DEMO_IMPACT_HISTORY 8
#define GX_BATTLE_DEMO_ATB_MAX 100.0f
#define GX_BATTLE_DEMO_PARTY_ATB_RATE 34.0f
#define GX_BATTLE_DEMO_ENEMY_ATB_RATE 30.0f
#define GX_BATTLE_DEMO_CONTROLLER_AI 0
#define GX_BATTLE_DEMO_CONTROLLER_LOCAL 1
#define GX_BATTLE_DEMO_ROLE_VANGUARD 0
#define GX_BATTLE_DEMO_ROLE_ARCANIST 1
#define GX_BATTLE_DEMO_ROLE_MEDIC 2
//ai: the headless board matches the renderer's nine-by-nine arena in world units.
#define GX_BATTLE_DEMO_GRID_COLUMNS 9
#define GX_BATTLE_DEMO_GRID_ROWS 9
#define GX_BATTLE_DEMO_GRID_HALF_EXTENT 4.5f
#define GX_BATTLE_DEMO_GRID_CELL_SIZE 1.0f
#define GX_BATTLE_DEMO_DIRECTION_NORTH 0
#define GX_BATTLE_DEMO_DIRECTION_EAST 1
#define GX_BATTLE_DEMO_DIRECTION_SOUTH 2
#define GX_BATTLE_DEMO_DIRECTION_WEST 3
//ai: keep the transient polisher defaults and slider ranges explicit and testable.
#define GX_BATTLE_DEMO_ATTACK_DAMAGE_DEFAULT 145.0f
#define GX_BATTLE_DEMO_ATTACK_DAMAGE_MIN 50.0f
#define GX_BATTLE_DEMO_ATTACK_DAMAGE_MAX 300.0f
#define GX_BATTLE_DEMO_POTION_HEAL_DEFAULT 180.0f
#define GX_BATTLE_DEMO_POTION_HEAL_MIN 50.0f
#define GX_BATTLE_DEMO_POTION_HEAL_MAX 400.0f
#define GX_BATTLE_DEMO_ENEMY_DELAY_DEFAULT 0.35f
#define GX_BATTLE_DEMO_ENEMY_DELAY_MIN 0.05f
#define GX_BATTLE_DEMO_ENEMY_DELAY_MAX 1.5f
#define GX_BATTLE_DEMO_GUARD_MULTIPLIER_DEFAULT 0.45f
#define GX_BATTLE_DEMO_GUARD_MULTIPLIER_MIN 0.0f
#define GX_BATTLE_DEMO_GUARD_MULTIPLIER_MAX 1.0f
#define GX_BATTLE_DEMO_IMPACT_SECONDS 1.25f
#define GX_BATTLE_DEMO_IMPACT_PLAYER (-1)

#define GX_BATTLE_DEMO_ENEMY_WARDEN 0
#define GX_BATTLE_DEMO_ENEMY_ADEPT 1
#define GX_BATTLE_DEMO_ENEMY_SENTINEL 2

typedef struct gX_SceneBattle_DemoUnit {
    gX_F64 hp;
    gX_F64 hpMax;
    gX_F64 mp;
    gX_F64 mpMax;
} gX_SceneBattle_DemoUnit;

typedef struct gX_SceneBattle_DemoRules {
    gX_F32 playerAttackDamage;
    gX_F32 itemHealAmount;
    gX_F32 magicHealThreshold;
    gX_F32 enemyTurnDelay;
    gX_F32 guardDamageMultiplier;
    gX_I32 potionCount;
    gX_SceneBattle_SpellCandidate spells[GX_BATTLE_DEMO_SPELL_COUNT];
} gX_SceneBattle_DemoRules;

//ai: store combat resources, tactical placement, and control ownership per party actor.
typedef struct gX_SceneBattle_DemoPartyActor {
    gX_SceneBattle_DemoUnit unit;
    gX_SceneBattle_CooldownState cooldowns;
    gX_U32 turnOrdinal;
    gX_I32 role;
    gX_U8 gridCol;
    gX_U8 gridRow;
    gX_U8 facing;
    gX_U8 controllerId;
    gX_F32 energy;
    gX_F32 attackScale;
    gX_I1 guarding;
} gX_SceneBattle_DemoPartyActor;

//ai: track enemy ATB and one cached telegraph per hostile actor.
typedef struct gX_SceneBattle_DemoEnemy {
    gX_SceneBattle_DemoUnit unit;
    gX_SceneBattle_CooldownState cooldowns;
    gX_U32 turnOrdinal;
    gX_I32 kind;
    gX_U8 gridCol;
    gX_U8 gridRow;
    gX_U8 facing;
    gX_U8 targetPartyIndex;
    gX_F32 energy;
    gX_F64 intentReadyAt;
    gX_F32 attackDamage;
    gX_I1 intentValid;
    gX_I1 guarding;
    gX_SceneBattle_EnemyAI_Decision intent;
} gX_SceneBattle_DemoEnemy;

//ai: retain actor identity for each bounded combat impact.
typedef struct gX_SceneBattle_DemoImpact {
    gX_F32 amount;
    gX_F32 age;
    gX_U8 targetIndex;
    gX_U8 sourceIndex;
    gX_U8 element;
    gX_I8 sourceKind;
    gX_I8 targetKind;
    gX_I1 isHeal;
} gX_SceneBattle_DemoImpact;

typedef struct gX_SceneBattle_Runtime {
    gX_SceneBattle_CooldownTuning cooldownTuning;
    gX_SceneBattle_DemoRules rules;
    gX_SceneBattle_Grid grid;
    gX_SceneBattle_DemoPartyActor party[GX_BATTLE_DEMO_PARTY_SIZE];
    gX_SceneBattle_DemoEnemy enemies[GX_BATTLE_DEMO_MAX_ENEMIES];
    gX_U32 turnNumber;
    gX_I32 outcome;
    gX_U8 enemyCount;
    gX_U8 targetIndex;
    gX_U8 waveIndex;
    gX_I8 message[128];
    gX_SceneBattle_DemoImpact impacts[GX_BATTLE_DEMO_IMPACT_HISTORY];
    gX_U8 impactHead;
    gX_U8 impactCount;
} gX_SceneBattle_Runtime;

//ai: keep the two-wave battle loop and bounded impact history renderer-independent.
gX_I1 gX_SceneBattle_Runtime_Init(gX_SceneBattle_Runtime *runtime);
//ai: register transient Battle controls with the existing polisher surface.
gX_I1 gX_SceneBattle_Runtime_RegisterPolisher(gX_I0);
//ai: clamp live polisher values and copy them into this encounter's rules.
gX_I1 gX_SceneBattle_Runtime_ApplyTuning(gX_SceneBattle_Runtime *runtime);
gX_I1 gX_SceneBattle_Runtime_Update(gX_SceneBattle_Runtime *runtime,
    gX_F64 elapsed, gX_F32 deltaSeconds);
gX_I1 gX_SceneBattle_Runtime_ConfirmAction(gX_SceneBattle_Runtime *runtime,
    gX_I32 action, gX_F64 elapsed);
//ai: route commands through actor-owned gauges and controller ownership.
gX_I1 gX_SceneBattle_Runtime_ConfirmActorAction(
    gX_SceneBattle_Runtime *runtime, gX_U32 actorIndex, gX_I32 action,
    gX_F64 elapsed);
gX_I1 gX_SceneBattle_Runtime_SetControlledActor(
    gX_SceneBattle_Runtime *runtime, gX_U32 actorIndex);
gX_I1 gX_SceneBattle_Runtime_SetActorController(
    gX_SceneBattle_Runtime *runtime, gX_U32 actorIndex,
    gX_U8 controllerId);
gX_I1 gX_SceneBattle_Runtime_GetControlledActor(
    const gX_SceneBattle_Runtime *runtime, gX_U32 *outActorIndex);
gX_I1 gX_SceneBattle_Runtime_IsActorReady(
    const gX_SceneBattle_Runtime *runtime, gX_U32 actorIndex);
gX_I1 gX_SceneBattle_Runtime_SelectTarget(gX_SceneBattle_Runtime *runtime,
    gX_U32 targetIndex);
//ai: real-time movement and facing update occupied cells without spending ATB.
gX_I1 gX_SceneBattle_Runtime_MoveActor(gX_SceneBattle_Runtime *runtime,
    gX_U32 actorIndex, gX_I32 direction);
gX_I1 gX_SceneBattle_Runtime_TurnActor(gX_SceneBattle_Runtime *runtime,
    gX_U32 actorIndex, gX_I32 quarterTurns);
gX_I1 gX_SceneBattle_Runtime_MovePlayer(gX_SceneBattle_Runtime *runtime,
    gX_I32 direction, gX_F64 elapsed);
gX_I1 gX_SceneBattle_Runtime_TurnPlayer(gX_SceneBattle_Runtime *runtime,
    gX_I32 quarterTurns, gX_F64 elapsed);
//ai: return the earliest per-enemy telegraph; execution uses its cached decision.
gX_I1 gX_SceneBattle_Runtime_GetEnemyIntent(
    const gX_SceneBattle_Runtime *runtime, gX_U32 *outEnemyIndex,
    gX_SceneBattle_EnemyAI_Decision *outDecision);
gX_I1 gX_SceneBattle_Runtime_IsPlayerTurn(
    const gX_SceneBattle_Runtime *runtime);

#ifdef __cplusplus
}
#endif

#endif //GX_SCENE_BATTLE_RUNTIME_H
