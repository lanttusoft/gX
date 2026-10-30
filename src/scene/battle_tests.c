/**********************************************************************************************
*
*   [gX: src/scene/battle_tests.c]
*   Headless tests for realtime battle rules and actor-owned ATB
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


//ai: pin battle simulation in --test-only without opening a window or GL context.
#include "battle_runtime.h"

#include "../common/log.h"
#include "../common/polisher.h"

#include <math.h>
#include <stdint.h>

#define BATTLE_TEST_ASSERT(cond, message) \
    do { \
        if (!(cond)) { \
            gX_TraceLog(gX_LOG_ERROR, "BATTLE_TEST FAIL: %s (line %d)", message, __LINE__); \
            errors++; \
        } \
    } while (0)

#define BATTLE_TEST_CLOSE(actual, expected, epsilon, message) \
    do { \
        gX_F32 Actual = (actual); \
        gX_F32 Expected = (expected); \
        if (!isfinite(Actual) || fabsf(Actual - Expected) > (epsilon)) { \
            gX_TraceLog(gX_LOG_ERROR, "BATTLE_TEST FAIL: %s: %f vs %f (line %d)", \
                message, Actual, Expected, __LINE__); \
            errors++; \
        } \
    } while (0)

//ai: retain the bounded FIFO primitive as a separately tested pure utility.
static gX_I32 gX_Tests_Battle_Queue(gX_I0) {
    gX_I32 errors = 0;
    gX_SceneBattle_TurnQueue Queue;
    gX_I8 Type = -1;
    gX_U8 Index = 0;
    gX_SceneBattle_TurnQueue_Init(&Queue);
    BATTLE_TEST_ASSERT(gX_SceneBattle_TurnQueue_Enqueue(&Queue,
        GX_BATTLE_ACTOR_PLAYER, 2)
        && gX_SceneBattle_TurnQueue_Enqueue(&Queue,
            GX_BATTLE_ACTOR_ENEMY, 1)
        && gX_SceneBattle_TurnQueue_Peek(&Queue, &Type, &Index)
        && Type == GX_BATTLE_ACTOR_PLAYER && Index == 2,
        "turn queue preserves FIFO actor identity");
    BATTLE_TEST_ASSERT(gX_SceneBattle_TurnQueue_Dequeue(&Queue)
        && gX_SceneBattle_TurnQueue_Peek(&Queue, &Type, &Index)
        && Type == GX_BATTLE_ACTOR_ENEMY && Index == 1,
        "turn queue advances to its next entry");
    return errors;
}

//ai: pin grid coordinates and occupied-slot behavior used by runtime actors.
static gX_I32 gX_Tests_Battle_Grid(gX_I0) {
    gX_I32 errors = 0;
    gX_SceneBattle_Grid Grid;
    gX_F32v3 Position = {0};
    gX_I32 Col = -1;
    gX_I32 Row = -1;
    BATTLE_TEST_ASSERT(gX_SceneBattle_Grid_Init(&Grid, 9, 9, 1.0f,
        (gX_F32v3){-4.5f, 0.0f, -4.5f})
        && gX_SceneBattle_Grid_CellToWorld(&Grid, 2, 4, &Position)
        && gX_SceneBattle_Grid_WorldToCell(&Grid, Position, &Col, &Row)
        && Col == 2 && Row == 4,
        "nine-by-nine grid cell centers round-trip");
    BATTLE_TEST_ASSERT(gX_SceneBattle_Grid_CellSetOccupied(&Grid, 2, 4, true)
        && gX_SceneBattle_Grid_CellIsOccupied(&Grid, 2, 4)
        && gX_SceneBattle_Grid_CellSetOccupied(&Grid, 2, 4, false)
        && !gX_SceneBattle_Grid_CellIsOccupied(&Grid, 2, 4),
        "grid occupancy can be set and cleared");
    return errors;
}

//ai: verify the encounter starts with three actor-owned gauges and a controller-bound Vanguard.
static gX_I32 gX_Tests_Battle_InitAndATB(gX_I0) {
    gX_I32 errors = 0;
    gX_SceneBattle_Runtime Runtime;
    gX_U32 ControlledActor = UINT32_MAX;
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Init(&Runtime)
        && Runtime.enemyCount == 1
        && Runtime.party[0].unit.hp == 780.0
        && Runtime.party[1].role == GX_BATTLE_DEMO_ROLE_ARCANIST
        && Runtime.party[2].role == GX_BATTLE_DEMO_ROLE_MEDIC
        && Runtime.party[0].controllerId == GX_BATTLE_DEMO_CONTROLLER_LOCAL
        && Runtime.party[1].controllerId == GX_BATTLE_DEMO_CONTROLLER_AI
        && Runtime.party[2].controllerId == GX_BATTLE_DEMO_CONTROLLER_AI
        && gX_SceneBattle_Runtime_GetControlledActor(&Runtime, &ControlledActor)
        && ControlledActor == 0
        && gX_SceneBattle_Runtime_IsActorReady(&Runtime, 0)
        && !gX_SceneBattle_Runtime_IsActorReady(&Runtime, 1),
        "party initializes with independent ATB and one local controller");
    BATTLE_TEST_ASSERT(Runtime.grid.cells[4U * 9U + 2U].occupied
        && Runtime.grid.cells[3U * 9U + 1U].occupied
        && Runtime.grid.cells[5U * 9U + 1U].occupied
        && Runtime.grid.cells[4U * 9U + 5U].occupied,
        "every initial party and enemy actor owns a unique board cell");

    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Update(&Runtime, 1.0, 1.0f)
        && Runtime.party[0].energy == GX_BATTLE_DEMO_ATB_MAX
        && Runtime.party[1].energy == GX_BATTLE_DEMO_PARTY_ATB_RATE
        && Runtime.party[2].energy == GX_BATTLE_DEMO_PARTY_ATB_RATE,
        "continuous delta-time fills each party gauge independently");
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_SetControlledActor(&Runtime, 1)
        && gX_SceneBattle_Runtime_GetControlledActor(&Runtime, &ControlledActor)
        && ControlledActor == 1
        && Runtime.party[0].controllerId == GX_BATTLE_DEMO_CONTROLLER_AI
        && Runtime.party[1].controllerId == GX_BATTLE_DEMO_CONTROLLER_LOCAL,
        "control transfers by actor and the previous member returns to AI");
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_SetActorController(&Runtime, 2, 7)
        && Runtime.party[2].controllerId == 7
        && gX_SceneBattle_Runtime_GetControlledActor(&Runtime, &ControlledActor)
        && ControlledActor == 1
        && gX_SceneBattle_Runtime_SetActorController(&Runtime, 2,
            GX_BATTLE_DEMO_CONTROLLER_AI),
        "controller identity belongs to each actor for future multiplayer input");
    BATTLE_TEST_ASSERT(!gX_SceneBattle_Runtime_SetControlledActor(&Runtime, 9)
        && !gX_SceneBattle_Runtime_MoveActor(&Runtime, 0,
            GX_BATTLE_DEMO_DIRECTION_EAST),
        "invalid or non-owned party commands are refused");
    Runtime.party[0].energy = NAN;
    BATTLE_TEST_ASSERT(!gX_SceneBattle_Runtime_Update(&Runtime, 1.1, 0.1f),
        "non-finite actor ATB is rejected before simulation");
    return errors;
}

//ai: cover action costs, local cooldowns, enemy telegraphs, and actor-indexed impacts.
static gX_I32 gX_Tests_Battle_Actions(gX_I0) {
    gX_I32 errors = 0;
    gX_SceneBattle_Runtime Runtime;
    gX_U32 EnemyIndex = UINT32_MAX;
    gX_SceneBattle_EnemyAI_Decision Intent;
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Init(&Runtime)
        && gX_SceneBattle_Runtime_ConfirmAction(&Runtime,
            GX_BATTLE_DEMO_ACTION_ATTACK, 0.0)
        && Runtime.enemies[0].unit.hp == 465.0
        && Runtime.party[0].energy == 0.0f
        && Runtime.impacts[0].sourceIndex == 0
        && Runtime.impacts[0].targetIndex == 0
        && gX_SceneBattle_Cooldown_IsActive(&Runtime.party[0].cooldowns,
            GX_BATTLE_ACTION_ATTACK, 0),
        "a ready actor attacks, spends ATB, and records its own cooldown");
    BATTLE_TEST_ASSERT(!gX_SceneBattle_Runtime_ConfirmAction(&Runtime,
        GX_BATTLE_DEMO_ACTION_MAGIC, 0.0)
        && Runtime.party[0].unit.mp == 46.0,
        "commands are refused until that actor's ATB gauge refills");
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Update(&Runtime, 0.35, 0.35f)
        && gX_SceneBattle_Runtime_GetEnemyIntent(&Runtime,
            &EnemyIndex, &Intent)
        && EnemyIndex == 0 && Intent.action == GX_BATTLE_AI_ACTION_ATTACK
        && Runtime.enemies[0].targetPartyIndex == 0
        && Runtime.party[0].unit.hp == 780.0,
        "full enemy ATB caches a stable telegraph and target before its delay");
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Update(&Runtime, 0.70, 0.35f)
        && Runtime.party[0].unit.hp == 690.0
        && Runtime.impacts[1].sourceKind == GX_BATTLE_DEMO_ENEMY_WARDEN
        && Runtime.impacts[1].targetIndex == 0
        && !gX_SceneBattle_Runtime_GetEnemyIntent(&Runtime,
            &EnemyIndex, &Intent),
        "the enemy resolves its cached intent against the party actor");
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Update(&Runtime, 3.70, 3.0f)
        && Runtime.party[0].energy == GX_BATTLE_DEMO_ATB_MAX
        && Runtime.party[1].energy == 0.0f
        && Runtime.party[2].energy == 0.0f
        && Runtime.party[1].cooldowns.tracks[0].actionType
            == GX_BATTLE_ACTION_ATTACK
        && !gX_SceneBattle_Cooldown_IsActive(&Runtime.party[0].cooldowns,
            GX_BATTLE_ACTION_ATTACK, 0),
        "AI companions act on their own full gauges and retain actor-local cooldowns");
    BATTLE_TEST_ASSERT(Runtime.impactCount >= 4,
        "companion attacks and support actions enter impact history");
    gX_SceneBattle_Cooldown_Init(&Runtime.party[0].cooldowns);
    gX_F64 EnemyHpBeforeMagic = Runtime.enemies[0].unit.hp;
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_ConfirmAction(&Runtime,
        GX_BATTLE_DEMO_ACTION_MAGIC, 3.70)
        && Runtime.party[0].unit.mp == 33.0
        && fabs(Runtime.enemies[0].unit.hp - (EnemyHpBeforeMagic - 125.0)) < 0.001
        && Runtime.impacts[(Runtime.impactHead
            + GX_BATTLE_DEMO_IMPACT_HISTORY - 1U)
            % GX_BATTLE_DEMO_IMPACT_HISTORY].element == GX_BATTLE_ELEMENT_ICE,
        "manual magic spends the controlled actor's MP and resolves an element");

    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Init(&Runtime)
        && gX_SceneBattle_Runtime_Update(&Runtime, 3.0, 3.0f)
        && Runtime.party[1].unit.hp == Runtime.party[1].unit.hpMax
        && Runtime.party[2].unit.mp < 70.0
        && Runtime.impacts[1].isHeal
        && Runtime.impacts[1].sourceIndex == 2
        && Runtime.impacts[1].targetIndex == 1,
        "Medic AI heals the weakest ally using its own MP and cooldown");
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Update(&Runtime, 3.351, 0.351f),
        "hostile telegraph update resolves");
    BATTLE_TEST_ASSERT(Runtime.party[0].unit.hp < 780.0,
        "hostile telegraph damages its cached party target");
    BATTLE_TEST_ASSERT(Runtime.impacts[Runtime.impactHead == 0
        ? GX_BATTLE_DEMO_IMPACT_HISTORY - 1U
        : Runtime.impactHead - 1U].targetIndex == 0,
        "hostile impact records its party actor index");
    return errors;
}

//ai: preserve occupied slots, facing, controller switching, and position-dependent combat.
static gX_I32 gX_Tests_Battle_Tactics(gX_I0) {
    gX_I32 errors = 0;
    gX_SceneBattle_Runtime Runtime;
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Init(&Runtime)
        && gX_SceneBattle_Runtime_MovePlayer(&Runtime,
            GX_BATTLE_DEMO_DIRECTION_EAST, 0.0)
        && Runtime.party[0].gridCol == 3
        && Runtime.party[0].facing == GX_BATTLE_DEMO_DIRECTION_EAST
        && Runtime.party[0].energy == GX_BATTLE_DEMO_ATB_MAX,
        "movement updates occupied cells and facing without spending ATB");
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_MovePlayer(&Runtime,
        GX_BATTLE_DEMO_DIRECTION_EAST, 0.0)
        && !gX_SceneBattle_Runtime_MovePlayer(&Runtime,
            GX_BATTLE_DEMO_DIRECTION_EAST, 0.0)
        && Runtime.party[0].gridCol == 4,
        "an occupied enemy slot blocks realtime movement");
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Init(&Runtime)
        && gX_SceneBattle_Grid_CellSetOccupied(&Runtime.grid, 2, 4, false)
        && gX_SceneBattle_Grid_CellSetOccupied(&Runtime.grid, 4, 4, true),
        "adjacent strike fixture moves the party actor onto a free slot");
    Runtime.party[0].gridCol = 4;
    Runtime.party[0].facing = GX_BATTLE_DEMO_DIRECTION_EAST;
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_ConfirmAction(&Runtime,
        GX_BATTLE_DEMO_ACTION_ATTACK, 0.0), "front-facing close attack resolves");
    BATTLE_TEST_CLOSE(Runtime.impacts[0].amount, 217.5f, 0.0001f,
        "adjacent front attack earns its damage bonus");

    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Init(&Runtime)
        && gX_SceneBattle_Grid_CellSetOccupied(&Runtime.grid, 2, 4, false)
        && gX_SceneBattle_Grid_CellSetOccupied(&Runtime.grid, 4, 4, true),
        "rear strike fixture moves the party actor beside the enemy");
    Runtime.party[0].gridCol = 4;
    Runtime.party[0].facing = GX_BATTLE_DEMO_DIRECTION_WEST;
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_ConfirmAction(&Runtime,
        GX_BATTLE_DEMO_ACTION_ATTACK, 0.0), "rear-facing close attack resolves");
    BATTLE_TEST_CLOSE(Runtime.impacts[0].amount, 141.375f, 0.0001f,
        "turning away reduces physical attack damage");

    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Init(&Runtime)
        && gX_SceneBattle_Grid_CellSetOccupied(&Runtime.grid, 2, 4, false)
        && gX_SceneBattle_Grid_CellSetOccupied(&Runtime.grid, 4, 4, true),
        "incoming-risk fixture moves the controlled actor adjacent");
    Runtime.party[0].gridCol = 4;
    Runtime.party[0].facing = GX_BATTLE_DEMO_DIRECTION_EAST;
    Runtime.rules.guardDamageMultiplier = 1.0f;
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_ConfirmAction(&Runtime,
        GX_BATTLE_DEMO_ACTION_GUARD, 0.0)
        && gX_SceneBattle_Runtime_Update(&Runtime, 0.0, 0.0f)
        && gX_SceneBattle_Runtime_Update(&Runtime, 0.35, 0.35f),
        "guard and adjacent enemy intent resolve continuously");
    BATTLE_TEST_CLOSE(Runtime.impacts[0].amount, 112.5f, 0.0001f,
        "front-facing adjacent hit carries increased risk");

    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Init(&Runtime)
        && gX_SceneBattle_Runtime_TurnPlayer(&Runtime, -1, 0.0)
        && Runtime.party[0].facing == GX_BATTLE_DEMO_DIRECTION_NORTH
        && Runtime.party[0].energy == GX_BATTLE_DEMO_ATB_MAX,
        "quarter-turns change facing without consuming ATB");
    return errors;
}

//ai: pin transient tuning bounds and live application to the realtime encounter.
static gX_I32 gX_Tests_Battle_Tuning(gX_I0) {
    gX_I32 errors = 0;
    gX_SceneBattle_Runtime Runtime;
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Init(&Runtime)
        && gX_SceneBattle_Runtime_ApplyTuning(&Runtime)
        && Runtime.rules.playerAttackDamage == GX_BATTLE_DEMO_ATTACK_DAMAGE_DEFAULT
        && Runtime.rules.itemHealAmount == GX_BATTLE_DEMO_POTION_HEAL_DEFAULT
        && Runtime.rules.enemyTurnDelay == GX_BATTLE_DEMO_ENEMY_DELAY_DEFAULT,
        "runtime applies bounded transient combat tuning");
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_RegisterPolisher()
        && gX_Polisher_Find("GX_BATTLE_ATTACK_DAMAGE")
        && gX_Polisher_Find("GX_BATTLE_ENEMY_DELAY"),
        "battle attack and telegraph delay remain available through Polisher");
    BATTLE_TEST_ASSERT(!gX_SceneBattle_Runtime_ApplyTuning(NULL),
        "tuning rejects a null runtime");
    return errors;
}

//ai: verify actual capped healing/damage and the fixed-size impact history.
static gX_I32 gX_Tests_Battle_Impacts(gX_I0) {
    gX_I32 errors = 0;
    gX_SceneBattle_Runtime Runtime;
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Init(&Runtime),
        "impact runtime initializes");
    Runtime.party[0].unit.hp = 950.0;
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_ConfirmAction(&Runtime,
        GX_BATTLE_DEMO_ACTION_ITEM, 0.0)
        && Runtime.party[1].unit.hp == Runtime.party[1].unit.hpMax
        && Runtime.impacts[0].isHeal
        && Runtime.impacts[0].amount == 180.0f
        && Runtime.impacts[0].targetIndex == 1,
        "potion impact targets the weakest ally and records actual restored HP");
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Init(&Runtime),
        "lethal impact runtime initializes");
    Runtime.rules.playerAttackDamage = 1000.0f;
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_ConfirmAction(&Runtime,
        GX_BATTLE_DEMO_ACTION_ATTACK, 0.0)
        && Runtime.waveIndex == 1
        && Runtime.impacts[0].amount == 610.0f
        && !Runtime.grid.cells[4U * 9U + 5U].occupied,
        "lethal impact records remaining HP, releases its slot, and advances the wave");
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Update(&Runtime,
        2.0, 2.0f) && Runtime.impacts[0].age == GX_BATTLE_DEMO_IMPACT_SECONDS,
        "impact floaters age while their history entry remains stored");
    return errors;
}

//ai: cover both wave formations, actor-state carryover, and terminal outcomes.
static gX_I32 gX_Tests_Battle_Waves(gX_I0) {
    gX_I32 errors = 0;
    gX_SceneBattle_Runtime Runtime;
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Init(&Runtime),
        "wave runtime initializes");
    Runtime.rules.playerAttackDamage = 1000.0f;
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_ConfirmAction(&Runtime,
        GX_BATTLE_DEMO_ACTION_ATTACK, 0.0)
        && Runtime.waveIndex == 1 && Runtime.enemyCount == 2
        && Runtime.party[0].unit.hp == 780.0
        && Runtime.party[0].unit.mp == 46.0,
        "clearing wave one loads two enemies and preserves party meters");
    Runtime.party[0].energy = GX_BATTLE_DEMO_ATB_MAX;
    gX_SceneBattle_Cooldown_Init(&Runtime.party[0].cooldowns);
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_SelectTarget(&Runtime, 0)
        && gX_SceneBattle_Runtime_ConfirmAction(&Runtime,
            GX_BATTLE_DEMO_ACTION_ATTACK, 0.0)
        && Runtime.enemies[0].unit.hp == 0.0
        && Runtime.targetIndex == 1,
        "target selection tracks a defeated wave-two opponent");
    Runtime.party[0].energy = GX_BATTLE_DEMO_ATB_MAX;
    gX_SceneBattle_Cooldown_Init(&Runtime.party[0].cooldowns);
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_ConfirmAction(&Runtime,
        GX_BATTLE_DEMO_ACTION_ATTACK, 0.0)
        && Runtime.enemies[1].unit.hp == 0.0
        && Runtime.outcome == GX_BATTLE_DEMO_OUTCOME_VICTORY,
        "clearing both wave-two actors ends in victory");
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Init(&Runtime)
        && gX_SceneBattle_Runtime_ConfirmAction(&Runtime,
            GX_BATTLE_DEMO_ACTION_RUN, 0.0)
        && Runtime.outcome == GX_BATTLE_DEMO_OUTCOME_ESCAPED,
        "run remains a terminal command when the actor is ready");
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Init(&Runtime),
        "defeat runtime initializes");
    for (gX_U32 i = 1; i < GX_BATTLE_DEMO_PARTY_SIZE; i++) {
        Runtime.party[i].unit.hp = 0.0;
        gX_SceneBattle_Grid_CellSetOccupied(&Runtime.grid,
            Runtime.party[i].gridCol, Runtime.party[i].gridRow, false);
    }
    Runtime.party[0].unit.hp = 1.0;
    Runtime.enemies[0].attackDamage = 200.0f;
    BATTLE_TEST_ASSERT(gX_SceneBattle_Runtime_Update(&Runtime, 0.0, 0.0f)
        && gX_SceneBattle_Runtime_Update(&Runtime, 0.35, 0.35f)
        && Runtime.outcome == GX_BATTLE_DEMO_OUTCOME_DEFEAT,
        "lethal realtime enemy action ends the encounter in defeat");
    return errors;
}

//ai: run the CPU-only regression groups and emit the stable test marker.
gX_I32 gX_Tests_Battle(gX_I0) {
    gX_I32 errors = 0;
    errors += gX_Tests_Battle_Queue();
    errors += gX_Tests_Battle_Grid();
    errors += gX_Tests_Battle_InitAndATB();
    errors += gX_Tests_Battle_Actions();
    errors += gX_Tests_Battle_Tactics();
    errors += gX_Tests_Battle_Tuning();
    errors += gX_Tests_Battle_Impacts();
    errors += gX_Tests_Battle_Waves();
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "BATTLE_TESTS: all passed");
    return errors;
}
