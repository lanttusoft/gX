/**********************************************************************************************
*
*   [gX: src/scene/battle_runtime.c]
*   Headless realtime battle runtime — actor-owned ATB and tactical party combat
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


//ai: keep combat simulation deterministic and independent of the renderer.
#include "battle_runtime.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../common/polisher.h"

//ai: keep the release-only code-size annotation optional on non-GNU compilers.
#if defined(__GNUC__) || defined(__clang__)
#define GX_BATTLE_NOINLINE __attribute__((noinline))
#else
#define GX_BATTLE_NOINLINE
#endif

//ai: keep battle tuning process-local and out of settings or save data.
static gX_F32 gX_BattleDemoAttackDamage = GX_BATTLE_DEMO_ATTACK_DAMAGE_DEFAULT;
static gX_F32 gX_BattleDemoPotionHeal = GX_BATTLE_DEMO_POTION_HEAL_DEFAULT;
static gX_F32 gX_BattleDemoEnemyDelay = GX_BATTLE_DEMO_ENEMY_DELAY_DEFAULT;
static gX_F32 gX_BattleDemoGuardMultiplier = GX_BATTLE_DEMO_GUARD_MULTIPLIER_DEFAULT;
static gX_I1 gX_BattleDemoPolisherRegistered = false;

//ai: keep environment and editor values inside the same bounds as their sliders.
static gX_F32 gX_SceneBattle_Runtime_ClampTuning(gX_F32 value,
    gX_F32 minimum, gX_F32 maximum, gX_F32 defaultValue) {
    if (!isfinite(value)) value = defaultValue;
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

//ai: keep the report's modified bit aligned when invalid script values are clamped.
static gX_I0 gX_SceneBattle_Runtime_UpdatePolisherEntry(
    const gX_I8 *name, gX_F32 value) {
    gX_Polisher_Entry *entry = gX_Polisher_Find(name);
    if (entry && entry->f32Value)
        entry->modified = value != entry->f32Original;
}

//ai: copy feedback into a bounded scene-owned buffer.
static gX_I0 gX_SceneBattle_Runtime_Message(gX_SceneBattle_Runtime *runtime,
    const gX_I8 *message) {
    gX_U32 CopyIndex = 0;
    if (!runtime || !message) return;
    while (CopyIndex + 1 < sizeof(runtime->message) && message[CopyIndex]) {
        runtime->message[CopyIndex] = message[CopyIndex];
        CopyIndex++;
    }
    runtime->message[CopyIndex] = '\0';
}

//ai: reject damaged ring metadata before it can address outside the bounded history.
static gX_I1 gX_SceneBattle_Runtime_ImpactStateValid(
    const gX_SceneBattle_Runtime *runtime) {
    return runtime && runtime->impactHead < GX_BATTLE_DEMO_IMPACT_HISTORY
        && runtime->impactCount <= GX_BATTLE_DEMO_IMPACT_HISTORY;
}

//ai: append an actual HP change and retain both actor indices for the renderer.
static gX_I0 gX_SceneBattle_Runtime_RecordImpact(
    gX_SceneBattle_Runtime *runtime, gX_F64 amount, gX_I8 sourceKind,
    gX_U8 sourceIndex, gX_I8 targetKind, gX_U8 targetIndex,
    gX_I1 isHeal, gX_U8 element) {
    if (!runtime || !gX_SceneBattle_Runtime_ImpactStateValid(runtime)
        || !isfinite(amount) || amount <= 0.0) return;
    gX_SceneBattle_DemoImpact *Impact = &runtime->impacts[runtime->impactHead];
    Impact->amount = (gX_F32)amount;
    Impact->age = 0.0f;
    Impact->sourceKind = sourceKind;
    Impact->sourceIndex = sourceIndex;
    Impact->targetKind = targetKind;
    Impact->targetIndex = targetIndex;
    Impact->element = element;
    Impact->isHeal = isHeal;
    runtime->impactHead = (gX_U8)((runtime->impactHead + 1U)
        % GX_BATTLE_DEMO_IMPACT_HISTORY);
    if (runtime->impactCount < GX_BATTLE_DEMO_IMPACT_HISTORY)
        runtime->impactCount++;
}

//ai: age visible floaters without deleting their entries from the recent-impact ring.
static gX_I1 gX_SceneBattle_Runtime_TickImpacts(
    gX_SceneBattle_Runtime *runtime, gX_F32 deltaSeconds) {
    if (!gX_SceneBattle_Runtime_ImpactStateValid(runtime)) return false;
    for (gX_U32 i = 0; i < runtime->impactCount; i++) {
        gX_U32 Index = (runtime->impactHead + GX_BATTLE_DEMO_IMPACT_HISTORY
            - 1U - i) % GX_BATTLE_DEMO_IMPACT_HISTORY;
        gX_SceneBattle_DemoImpact *Impact = &runtime->impacts[Index];
        if (!isfinite(Impact->age) || Impact->age < 0.0f) return false;
        Impact->age = fminf(GX_BATTLE_DEMO_IMPACT_SECONDS,
            Impact->age + deltaSeconds);
    }
    return true;
}

//ai: centralize actor liveness so defeated actors never own occupied slots.
static gX_I1 gX_SceneBattle_Runtime_PartyAlive(
    const gX_SceneBattle_Runtime *runtime, gX_U32 actorIndex) {
    return runtime && actorIndex < GX_BATTLE_DEMO_PARTY_SIZE
        && gX_SceneBattle_Target_IsAlive(&(gX_SceneBattle_TargetVitals){
            runtime->party[actorIndex].unit.hp, 0.0});
}

//ai: centralize enemy liveness so defeated opponents cannot act or be selected.
static gX_I1 gX_SceneBattle_Runtime_EnemyAlive(
    const gX_SceneBattle_Runtime *runtime, gX_U32 enemyIndex) {
    return runtime && runtime->enemyCount <= GX_BATTLE_DEMO_MAX_ENEMIES
        && enemyIndex < runtime->enemyCount
        && gX_SceneBattle_Target_IsAlive(&(gX_SceneBattle_TargetVitals){
            runtime->enemies[enemyIndex].unit.hp, 0.0});
}

//ai: count living party actors without allocating a roster copy.
static gX_U32 gX_SceneBattle_Runtime_LivingPartyCount(
    const gX_SceneBattle_Runtime *runtime) {
    gX_U32 Count = 0;
    for (gX_U32 i = 0; runtime && i < GX_BATTLE_DEMO_PARTY_SIZE; i++)
        if (gX_SceneBattle_Runtime_PartyAlive(runtime, i)) Count++;
    return Count;
}

//ai: require every living actor to own one distinct occupied tactical slot.
static gX_I1 gX_SceneBattle_Runtime_BoardValid(
    const gX_SceneBattle_Runtime *runtime) {
    if (!runtime || runtime->enemyCount > GX_BATTLE_DEMO_MAX_ENEMIES
        || runtime->grid.cols != GX_BATTLE_DEMO_GRID_COLUMNS
        || runtime->grid.rows != GX_BATTLE_DEMO_GRID_ROWS) return false;
    gX_U32 LiveActors = 0;
    for (gX_U32 i = 0; i < GX_BATTLE_DEMO_PARTY_SIZE; i++) {
        if (!gX_SceneBattle_Runtime_PartyAlive(runtime, i)) continue;
        const gX_SceneBattle_DemoPartyActor *Actor = &runtime->party[i];
        if (Actor->gridCol >= runtime->grid.cols
            || Actor->gridRow >= runtime->grid.rows
            || !gX_SceneBattle_Grid_CellIsOccupied(&runtime->grid,
                Actor->gridCol, Actor->gridRow)) return false;
        for (gX_U32 other = 0; other < i; other++) {
            if (gX_SceneBattle_Runtime_PartyAlive(runtime, other)
                && Actor->gridCol == runtime->party[other].gridCol
                && Actor->gridRow == runtime->party[other].gridRow) return false;
        }
        LiveActors++;
    }
    for (gX_U32 i = 0; i < runtime->enemyCount; i++) {
        if (!gX_SceneBattle_Runtime_EnemyAlive(runtime, i)) continue;
        const gX_SceneBattle_DemoEnemy *Enemy = &runtime->enemies[i];
        if (Enemy->gridCol >= runtime->grid.cols
            || Enemy->gridRow >= runtime->grid.rows
            || !gX_SceneBattle_Grid_CellIsOccupied(&runtime->grid,
                Enemy->gridCol, Enemy->gridRow)) return false;
        for (gX_U32 other = 0; other < GX_BATTLE_DEMO_PARTY_SIZE; other++) {
            if (gX_SceneBattle_Runtime_PartyAlive(runtime, other)
                && Enemy->gridCol == runtime->party[other].gridCol
                && Enemy->gridRow == runtime->party[other].gridRow) return false;
        }
        for (gX_U32 other = 0; other < i; other++) {
            if (gX_SceneBattle_Runtime_EnemyAlive(runtime, other)
                && Enemy->gridCol == runtime->enemies[other].gridCol
                && Enemy->gridRow == runtime->enemies[other].gridRow) return false;
        }
        LiveActors++;
    }
    gX_U32 OccupiedCells = 0;
    for (gX_I32 row = 0; row < runtime->grid.rows; row++)
        for (gX_I32 col = 0; col < runtime->grid.cols; col++)
            if (gX_SceneBattle_Grid_CellIsOccupied(&runtime->grid, col, row))
                OccupiedCells++;
    return OccupiedCells == LiveActors;
}

//ai: report whether a wave still has at least one living opponent.
static gX_I1 gX_SceneBattle_Runtime_HasEnemies(
    const gX_SceneBattle_Runtime *runtime) {
    if (!runtime || runtime->enemyCount > GX_BATTLE_DEMO_MAX_ENEMIES) return false;
    for (gX_U32 i = 0; i < runtime->enemyCount; i++)
        if (gX_SceneBattle_Runtime_EnemyAlive(runtime, i)) return true;
    return false;
}

//ai: select the first living opponent after the current target is defeated.
static gX_I1 gX_SceneBattle_Runtime_FirstEnemy(
    const gX_SceneBattle_Runtime *runtime, gX_U32 *outIndex) {
    if (outIndex) *outIndex = UINT32_MAX;
    if (!runtime || !outIndex
        || runtime->enemyCount > GX_BATTLE_DEMO_MAX_ENEMIES) return false;
    for (gX_U32 i = 0; i < runtime->enemyCount; i++) {
        if (gX_SceneBattle_Runtime_EnemyAlive(runtime, i)) {
            *outIndex = i;
            return true;
        }
    }
    return false;
}

//ai: restore local control to a survivor if its current actor is defeated.
static gX_I0 gX_SceneBattle_Runtime_EnsureLocalController(
    gX_SceneBattle_Runtime *runtime) {
    for (gX_U32 i = 0; i < GX_BATTLE_DEMO_PARTY_SIZE; i++) {
        if (runtime->party[i].controllerId == GX_BATTLE_DEMO_CONTROLLER_LOCAL
            && gX_SceneBattle_Runtime_PartyAlive(runtime, i)) return;
    }
    for (gX_U32 i = 0; i < GX_BATTLE_DEMO_PARTY_SIZE; i++)
        if (runtime->party[i].controllerId == GX_BATTLE_DEMO_CONTROLLER_LOCAL)
            runtime->party[i].controllerId = GX_BATTLE_DEMO_CONTROLLER_AI;
    for (gX_U32 i = 0; i < GX_BATTLE_DEMO_PARTY_SIZE; i++) {
        if (gX_SceneBattle_Runtime_PartyAlive(runtime, i)) {
            runtime->party[i].controllerId = GX_BATTLE_DEMO_CONTROLLER_LOCAL;
            return;
        }
    }
}

//ai: clear pending enemy actions whenever combat reaches a terminal outcome.
static gX_I0 gX_SceneBattle_Runtime_Finish(gX_SceneBattle_Runtime *runtime,
    gX_I32 outcome, const gX_I8 *message) {
    runtime->outcome = outcome;
    for (gX_U32 i = 0; i < runtime->enemyCount; i++)
        runtime->enemies[i].intentValid = false;
    gX_SceneBattle_Runtime_Message(runtime, message);
}

//ai: initialize one of the two fixed formations without disturbing party state.
static gX_I1 gX_SceneBattle_Runtime_LoadWave(
    gX_SceneBattle_Runtime *runtime, gX_U32 waveIndex) {
    if (!runtime || waveIndex >= GX_BATTLE_DEMO_WAVE_COUNT) return false;
    memset(runtime->enemies, 0, sizeof(runtime->enemies));
    runtime->waveIndex = (gX_U8)waveIndex;
    if (waveIndex == 0) {
        runtime->enemyCount = 1;
        runtime->enemies[0].unit = (gX_SceneBattle_DemoUnit){
            610.0, 1000.0, 46.0, 100.0};
        runtime->enemies[0].kind = GX_BATTLE_DEMO_ENEMY_WARDEN;
        runtime->enemies[0].attackDamage = 90.0f;
        runtime->enemies[0].gridCol = 5;
        runtime->enemies[0].gridRow = 4;
    } else {
        runtime->enemyCount = 2;
        runtime->enemies[0].unit = (gX_SceneBattle_DemoUnit){
            350.0, 350.0, 40.0, 100.0};
        runtime->enemies[0].kind = GX_BATTLE_DEMO_ENEMY_ADEPT;
        runtime->enemies[0].attackDamage = 60.0f;
        runtime->enemies[0].gridCol = 4;
        runtime->enemies[0].gridRow = 3;
        runtime->enemies[1].unit = (gX_SceneBattle_DemoUnit){
            420.0, 420.0, 30.0, 100.0};
        runtime->enemies[1].kind = GX_BATTLE_DEMO_ENEMY_SENTINEL;
        runtime->enemies[1].attackDamage = 80.0f;
        runtime->enemies[1].gridCol = 4;
        runtime->enemies[1].gridRow = 5;
    }
    for (gX_U32 i = 0; i < runtime->enemyCount; i++) {
        gX_SceneBattle_DemoEnemy *Enemy = &runtime->enemies[i];
        Enemy->facing = GX_BATTLE_DEMO_DIRECTION_WEST;
        Enemy->energy = GX_BATTLE_DEMO_ATB_MAX;
        Enemy->intent.action = GX_BATTLE_AI_ACTION_ATTACK;
        Enemy->intent.spellIndex = -1;
        if (gX_SceneBattle_Grid_CellIsOccupied(&runtime->grid,
                Enemy->gridCol, Enemy->gridRow)
            || !gX_SceneBattle_Grid_CellSetOccupied(&runtime->grid,
                Enemy->gridCol, Enemy->gridRow, true)) return false;
        gX_SceneBattle_Cooldown_Init(&Enemy->cooldowns);
    }
    runtime->targetIndex = 0;
    return gX_SceneBattle_Runtime_BoardValid(runtime);
}

//ai: share the terminal and wave-transition path between party and enemy actions.
static GX_BATTLE_NOINLINE gX_I1 gX_SceneBattle_Runtime_AdvanceEncounter(
    gX_SceneBattle_Runtime *runtime) {
    if (runtime->targetIndex >= runtime->enemyCount
        || !gX_SceneBattle_Runtime_EnemyAlive(runtime, runtime->targetIndex)) {
        gX_U32 FirstEnemy = 0;
        if (gX_SceneBattle_Runtime_FirstEnemy(runtime, &FirstEnemy))
            runtime->targetIndex = (gX_U8)FirstEnemy;
    }
    if (gX_SceneBattle_Runtime_LivingPartyCount(runtime) == 0) {
        gX_SceneBattle_Runtime_Finish(runtime, GX_BATTLE_DEMO_OUTCOME_DEFEAT,
            "DEFEAT // PARTY DOWN");
        return true;
    }
    if (gX_SceneBattle_Runtime_HasEnemies(runtime)) return true;
    if ((gX_U32)runtime->waveIndex + 1 >= GX_BATTLE_DEMO_WAVE_COUNT) {
        gX_SceneBattle_Runtime_Finish(runtime, GX_BATTLE_DEMO_OUTCOME_VICTORY,
            "VICTORY // ALL RIFT WAVES CLEARED");
        return true;
    }
    if (!gX_SceneBattle_Runtime_LoadWave(runtime,
            (gX_U32)runtime->waveIndex + 1)) return false;
    gX_SceneBattle_Runtime_Message(runtime, "WAVE 02 // RIFT HOSTILES ENTER");
    return true;
}

//ai: select a deterministic hostile decision using the actor's own cooldowns.
static gX_I1 gX_SceneBattle_Runtime_ChooseEnemyIntent(
    const gX_SceneBattle_Runtime *runtime, gX_U32 enemyIndex,
    gX_SceneBattle_EnemyAI_Decision *outDecision) {
    if (!runtime || !outDecision || runtime->enemyCount > GX_BATTLE_DEMO_MAX_ENEMIES
        || !gX_SceneBattle_Runtime_EnemyAlive(runtime, enemyIndex)) return false;
    const gX_SceneBattle_DemoEnemy *Enemy = &runtime->enemies[enemyIndex];
    gX_SceneBattle_EnemyAI_Context Context = {
        Enemy->unit.hp,
        Enemy->unit.hpMax,
        Enemy->unit.mp,
        Enemy->turnOrdinal,
        runtime->rules.spells,
        GX_BATTLE_DEMO_SPELL_COUNT,
        &Enemy->cooldowns};
    gX_I32 Profile = GX_BATTLE_AI_PROFILE_BALANCED;
    if (Enemy->kind == GX_BATTLE_DEMO_ENEMY_ADEPT)
        Profile = GX_BATTLE_AI_PROFILE_AGGRESSIVE;
    else if (Enemy->kind == GX_BATTLE_DEMO_ENEMY_SENTINEL)
        Profile = GX_BATTLE_AI_PROFILE_DEFENSIVE;
    return gX_SceneBattle_EnemyAI_ChooseForProfile(&Context, Profile,
        outDecision);
}

//ai: initialize the three actor-owned party records, occupied board, and first wave.
gX_I1 gX_SceneBattle_Runtime_Init(gX_SceneBattle_Runtime *runtime) {
    if (!runtime) return false;
    memset(runtime, 0, sizeof(*runtime));
    gX_SceneBattle_CooldownTuning_Default(&runtime->cooldownTuning);
    runtime->rules.playerAttackDamage = GX_BATTLE_DEMO_ATTACK_DAMAGE_DEFAULT;
    runtime->rules.itemHealAmount = GX_BATTLE_DEMO_POTION_HEAL_DEFAULT;
    runtime->rules.magicHealThreshold = 0.55f;
    runtime->rules.enemyTurnDelay = GX_BATTLE_DEMO_ENEMY_DELAY_DEFAULT;
    runtime->rules.guardDamageMultiplier = GX_BATTLE_DEMO_GUARD_MULTIPLIER_DEFAULT;
    runtime->rules.potionCount = 2;
    runtime->rules.spells[0] = (gX_SceneBattle_SpellCandidate){
        GX_BATTLE_ELEMENT_FIRE, -145.0, -18.0, false, false};
    runtime->rules.spells[1] = (gX_SceneBattle_SpellCandidate){
        GX_BATTLE_ELEMENT_ICE, -125.0, -13.0, false, false};
    runtime->rules.spells[2] = (gX_SceneBattle_SpellCandidate){
        GX_BATTLE_ELEMENT_LIGHTNING, -195.0, -26.0, false, false};
    runtime->rules.spells[3] = (gX_SceneBattle_SpellCandidate){
        0, 240.0, -20.0, true, false};
    runtime->party[0].unit = (gX_SceneBattle_DemoUnit){
        780.0, 1000.0, 46.0, 100.0};
    runtime->party[0].role = GX_BATTLE_DEMO_ROLE_VANGUARD;
    runtime->party[0].gridCol = 2;
    runtime->party[0].gridRow = 4;
    runtime->party[0].facing = GX_BATTLE_DEMO_DIRECTION_EAST;
    runtime->party[0].controllerId = GX_BATTLE_DEMO_CONTROLLER_LOCAL;
    runtime->party[0].energy = GX_BATTLE_DEMO_ATB_MAX;
    runtime->party[0].attackScale = 1.0f;
    runtime->party[1].unit = (gX_SceneBattle_DemoUnit){
        620.0, 800.0, 72.0, 100.0};
    runtime->party[1].role = GX_BATTLE_DEMO_ROLE_ARCANIST;
    runtime->party[1].gridCol = 1;
    runtime->party[1].gridRow = 3;
    runtime->party[1].facing = GX_BATTLE_DEMO_DIRECTION_EAST;
    runtime->party[1].controllerId = GX_BATTLE_DEMO_CONTROLLER_AI;
    runtime->party[1].attackScale = 0.78f;
    runtime->party[2].unit = (gX_SceneBattle_DemoUnit){
        680.0, 850.0, 70.0, 100.0};
    runtime->party[2].role = GX_BATTLE_DEMO_ROLE_MEDIC;
    runtime->party[2].gridCol = 1;
    runtime->party[2].gridRow = 5;
    runtime->party[2].facing = GX_BATTLE_DEMO_DIRECTION_EAST;
    runtime->party[2].controllerId = GX_BATTLE_DEMO_CONTROLLER_AI;
    runtime->party[2].attackScale = 0.70f;
    for (gX_U32 i = 0; i < GX_BATTLE_DEMO_PARTY_SIZE; i++)
        gX_SceneBattle_Cooldown_Init(&runtime->party[i].cooldowns);
    runtime->outcome = GX_BATTLE_DEMO_OUTCOME_ACTIVE;
    if (!gX_SceneBattle_Grid_Init(&runtime->grid,
            GX_BATTLE_DEMO_GRID_COLUMNS, GX_BATTLE_DEMO_GRID_ROWS,
            GX_BATTLE_DEMO_GRID_CELL_SIZE,
            (gX_F32v3){-GX_BATTLE_DEMO_GRID_HALF_EXTENT, 0.0f,
                -GX_BATTLE_DEMO_GRID_HALF_EXTENT})) return false;
    for (gX_U32 i = 0; i < GX_BATTLE_DEMO_PARTY_SIZE; i++) {
        if (!gX_SceneBattle_Grid_CellSetOccupied(&runtime->grid,
                runtime->party[i].gridCol, runtime->party[i].gridRow, true))
            return false;
    }
    if (!gX_SceneBattle_Runtime_LoadWave(runtime, 0)) return false;
    gX_SceneBattle_Runtime_Message(runtime,
        "Three-person party // Tab switches direct control.");
    return true;
}

//ai: register battle-only transient tunables and apply the shared environment hook.
gX_I1 gX_SceneBattle_Runtime_RegisterPolisher(gX_I0) {
    if (!gX_BattleDemoPolisherRegistered) {
        gX_Polisher_RegisterFloat("GX_BATTLE_ATTACK_DAMAGE", "Battle",
            &gX_BattleDemoAttackDamage, GX_BATTLE_DEMO_ATTACK_DAMAGE_MIN,
            GX_BATTLE_DEMO_ATTACK_DAMAGE_MAX, 1.0f);
        gX_Polisher_RegisterFloat("GX_BATTLE_POTION_HEAL", "Battle",
            &gX_BattleDemoPotionHeal, GX_BATTLE_DEMO_POTION_HEAL_MIN,
            GX_BATTLE_DEMO_POTION_HEAL_MAX, 1.0f);
        gX_Polisher_RegisterFloat("GX_BATTLE_ENEMY_DELAY", "Battle",
            &gX_BattleDemoEnemyDelay, GX_BATTLE_DEMO_ENEMY_DELAY_MIN,
            GX_BATTLE_DEMO_ENEMY_DELAY_MAX, 0.01f);
        gX_Polisher_RegisterFloat("GX_BATTLE_GUARD_MULTIPLIER", "Battle",
            &gX_BattleDemoGuardMultiplier, GX_BATTLE_DEMO_GUARD_MULTIPLIER_MIN,
            GX_BATTLE_DEMO_GUARD_MULTIPLIER_MAX, 0.01f);
        gX_BattleDemoPolisherRegistered =
            gX_Polisher_Find("GX_BATTLE_ATTACK_DAMAGE")
            && gX_Polisher_Find("GX_BATTLE_POTION_HEAL")
            && gX_Polisher_Find("GX_BATTLE_ENEMY_DELAY")
            && gX_Polisher_Find("GX_BATTLE_GUARD_MULTIPLIER");
    }
    if (!gX_BattleDemoPolisherRegistered) return false;
    gX_Polisher_ApplyEnv();
    return true;
}

//ai: apply bounded live values without coupling the headless runtime to a renderer.
gX_I1 gX_SceneBattle_Runtime_ApplyTuning(gX_SceneBattle_Runtime *runtime) {
    if (!runtime) return false;
    gX_BattleDemoAttackDamage = gX_SceneBattle_Runtime_ClampTuning(
        gX_BattleDemoAttackDamage, GX_BATTLE_DEMO_ATTACK_DAMAGE_MIN,
        GX_BATTLE_DEMO_ATTACK_DAMAGE_MAX, GX_BATTLE_DEMO_ATTACK_DAMAGE_DEFAULT);
    gX_BattleDemoPotionHeal = gX_SceneBattle_Runtime_ClampTuning(
        gX_BattleDemoPotionHeal, GX_BATTLE_DEMO_POTION_HEAL_MIN,
        GX_BATTLE_DEMO_POTION_HEAL_MAX, GX_BATTLE_DEMO_POTION_HEAL_DEFAULT);
    gX_BattleDemoEnemyDelay = gX_SceneBattle_Runtime_ClampTuning(
        gX_BattleDemoEnemyDelay, GX_BATTLE_DEMO_ENEMY_DELAY_MIN,
        GX_BATTLE_DEMO_ENEMY_DELAY_MAX, GX_BATTLE_DEMO_ENEMY_DELAY_DEFAULT);
    gX_BattleDemoGuardMultiplier = gX_SceneBattle_Runtime_ClampTuning(
        gX_BattleDemoGuardMultiplier, GX_BATTLE_DEMO_GUARD_MULTIPLIER_MIN,
        GX_BATTLE_DEMO_GUARD_MULTIPLIER_MAX,
        GX_BATTLE_DEMO_GUARD_MULTIPLIER_DEFAULT);
    runtime->rules.playerAttackDamage = gX_BattleDemoAttackDamage;
    runtime->rules.itemHealAmount = gX_BattleDemoPotionHeal;
    runtime->rules.enemyTurnDelay = gX_BattleDemoEnemyDelay;
    runtime->rules.guardDamageMultiplier = gX_BattleDemoGuardMultiplier;
    gX_SceneBattle_Runtime_UpdatePolisherEntry("GX_BATTLE_ATTACK_DAMAGE",
        gX_BattleDemoAttackDamage);
    gX_SceneBattle_Runtime_UpdatePolisherEntry("GX_BATTLE_POTION_HEAL",
        gX_BattleDemoPotionHeal);
    gX_SceneBattle_Runtime_UpdatePolisherEntry("GX_BATTLE_ENEMY_DELAY",
        gX_BattleDemoEnemyDelay);
    gX_SceneBattle_Runtime_UpdatePolisherEntry("GX_BATTLE_GUARD_MULTIPLIER",
        gX_BattleDemoGuardMultiplier);
    return true;
}

//ai: resolve the actor owned by the local controller without introducing global focus state.
gX_I1 gX_SceneBattle_Runtime_GetControlledActor(
    const gX_SceneBattle_Runtime *runtime, gX_U32 *outActorIndex) {
    if (outActorIndex) *outActorIndex = UINT32_MAX;
    if (!runtime || !outActorIndex) return false;
    for (gX_U32 i = 0; i < GX_BATTLE_DEMO_PARTY_SIZE; i++) {
        if (runtime->party[i].controllerId == GX_BATTLE_DEMO_CONTROLLER_LOCAL
            && gX_SceneBattle_Runtime_PartyAlive(runtime, i)) {
            *outActorIndex = i;
            return true;
        }
    }
    return false;
}

//ai: transfer one controller to one living actor; other actors resume their AI policy.
gX_I1 gX_SceneBattle_Runtime_SetActorController(
    gX_SceneBattle_Runtime *runtime, gX_U32 actorIndex,
    gX_U8 controllerId) {
    if (!runtime || actorIndex >= GX_BATTLE_DEMO_PARTY_SIZE
        || !gX_SceneBattle_Runtime_PartyAlive(runtime, actorIndex)) return false;
    if (controllerId != GX_BATTLE_DEMO_CONTROLLER_AI) {
        for (gX_U32 i = 0; i < GX_BATTLE_DEMO_PARTY_SIZE; i++)
            if (i != actorIndex && runtime->party[i].controllerId == controllerId)
                runtime->party[i].controllerId = GX_BATTLE_DEMO_CONTROLLER_AI;
    }
    runtime->party[actorIndex].controllerId = controllerId;
    return true;
}

//ai: assign direct local control while retaining actor-owned multiplayer identity.
gX_I1 gX_SceneBattle_Runtime_SetControlledActor(
    gX_SceneBattle_Runtime *runtime, gX_U32 actorIndex) {
    if (!gX_SceneBattle_Runtime_SetActorController(runtime, actorIndex,
            GX_BATTLE_DEMO_CONTROLLER_LOCAL)) return false;
    static const gX_I8 *ActorNames[GX_BATTLE_DEMO_PARTY_SIZE] = {
        "Vanguard", "Arcanist", "Medic"};
    gX_I8 Message[64];
    snprintf((char *)Message, sizeof(Message), "%s under direct control.",
        ActorNames[actorIndex]);
    gX_SceneBattle_Runtime_Message(runtime, Message);
    return true;
}

//ai: expose ATB completion separately from input ownership for HUD and AI decisions.
gX_I1 gX_SceneBattle_Runtime_IsActorReady(
    const gX_SceneBattle_Runtime *runtime, gX_U32 actorIndex) {
    return runtime && runtime->outcome == GX_BATTLE_DEMO_OUTCOME_ACTIVE
        && actorIndex < GX_BATTLE_DEMO_PARTY_SIZE
        && gX_SceneBattle_Runtime_PartyAlive(runtime, actorIndex)
        && isfinite(runtime->party[actorIndex].energy)
        && runtime->party[actorIndex].energy >= GX_BATTLE_DEMO_ATB_MAX;
}

//ai: preserve the familiar predicate as "local actor's ATB is ready".
gX_I1 gX_SceneBattle_Runtime_IsPlayerTurn(
    const gX_SceneBattle_Runtime *runtime) {
    gX_U32 ActorIndex = UINT32_MAX;
    return gX_SceneBattle_Runtime_GetControlledActor(runtime, &ActorIndex)
        && gX_SceneBattle_Runtime_IsActorReady(runtime, ActorIndex);
}

//ai: validate and store a living target while a local actor owns the command surface.
gX_I1 gX_SceneBattle_Runtime_SelectTarget(gX_SceneBattle_Runtime *runtime,
    gX_U32 targetIndex) {
    gX_U32 ActorIndex = UINT32_MAX;
    if (!runtime || !gX_SceneBattle_Runtime_GetControlledActor(runtime,
            &ActorIndex)
        || !gX_SceneBattle_Runtime_BoardValid(runtime)
        || !gX_SceneBattle_Runtime_EnemyAlive(runtime, targetIndex)) return false;
    runtime->targetIndex = (gX_U8)targetIndex;
    return true;
}

//ai: move one controller-owned actor to a free cardinal slot and face the movement direction.
gX_I1 gX_SceneBattle_Runtime_MoveActor(gX_SceneBattle_Runtime *runtime,
    gX_U32 actorIndex, gX_I32 direction) {
    if (!runtime || runtime->outcome != GX_BATTLE_DEMO_OUTCOME_ACTIVE
        || actorIndex >= GX_BATTLE_DEMO_PARTY_SIZE
        || runtime->party[actorIndex].controllerId == GX_BATTLE_DEMO_CONTROLLER_AI
        || !gX_SceneBattle_Runtime_PartyAlive(runtime, actorIndex)
        || !gX_SceneBattle_Runtime_BoardValid(runtime)) return false;
    gX_I32 Col = runtime->party[actorIndex].gridCol;
    gX_I32 Row = runtime->party[actorIndex].gridRow;
    if (direction == GX_BATTLE_DEMO_DIRECTION_NORTH) Row--;
    else if (direction == GX_BATTLE_DEMO_DIRECTION_EAST) Col++;
    else if (direction == GX_BATTLE_DEMO_DIRECTION_SOUTH) Row++;
    else if (direction == GX_BATTLE_DEMO_DIRECTION_WEST) Col--;
    else return false;
    if (Col < 0 || Col >= runtime->grid.cols || Row < 0
        || Row >= runtime->grid.rows
        || gX_SceneBattle_Grid_CellIsOccupied(&runtime->grid, Col, Row)) {
        gX_SceneBattle_Runtime_Message(runtime, "That arena slot is blocked.");
        return false;
    }
    if (!gX_SceneBattle_Grid_CellSetOccupied(&runtime->grid,
            runtime->party[actorIndex].gridCol,
            runtime->party[actorIndex].gridRow, false)
        || !gX_SceneBattle_Grid_CellSetOccupied(&runtime->grid, Col, Row, true))
        return false;
    runtime->party[actorIndex].gridCol = (gX_U8)Col;
    runtime->party[actorIndex].gridRow = (gX_U8)Row;
    runtime->party[actorIndex].facing = (gX_U8)direction;
    gX_SceneBattle_Runtime_Message(runtime, "Party member repositions.");
    return true;
}

//ai: rotate a controller-owned actor in place without spending its ATB gauge.
gX_I1 gX_SceneBattle_Runtime_TurnActor(gX_SceneBattle_Runtime *runtime,
    gX_U32 actorIndex, gX_I32 quarterTurns) {
    if (!runtime || runtime->outcome != GX_BATTLE_DEMO_OUTCOME_ACTIVE
        || actorIndex >= GX_BATTLE_DEMO_PARTY_SIZE
        || runtime->party[actorIndex].controllerId == GX_BATTLE_DEMO_CONTROLLER_AI
        || !gX_SceneBattle_Runtime_PartyAlive(runtime, actorIndex)
        || (quarterTurns != -1 && quarterTurns != 1)) return false;
    runtime->party[actorIndex].facing = (gX_U8)((runtime->party[actorIndex].facing
        + (quarterTurns > 0 ? 1U : 3U)) % 4U);
    gX_SceneBattle_Runtime_Message(runtime,
        quarterTurns > 0 ? "Party member turns clockwise."
            : "Party member turns counterclockwise.");
    return true;
}

//ai: preserve the player-oriented movement wrapper while routing it through actor ownership.
gX_I1 gX_SceneBattle_Runtime_MovePlayer(gX_SceneBattle_Runtime *runtime,
    gX_I32 direction, gX_F64 elapsed) {
    gX_U32 ActorIndex = UINT32_MAX;
    return isfinite(elapsed)
        && gX_SceneBattle_Runtime_GetControlledActor(runtime, &ActorIndex)
        && gX_SceneBattle_Runtime_MoveActor(runtime, ActorIndex, direction);
}

//ai: preserve the player-oriented facing wrapper while routing it through actor ownership.
gX_I1 gX_SceneBattle_Runtime_TurnPlayer(gX_SceneBattle_Runtime *runtime,
    gX_I32 quarterTurns, gX_F64 elapsed) {
    gX_U32 ActorIndex = UINT32_MAX;
    return isfinite(elapsed)
        && gX_SceneBattle_Runtime_GetControlledActor(runtime, &ActorIndex)
        && gX_SceneBattle_Runtime_TurnActor(runtime, ActorIndex, quarterTurns);
}

//ai: return the earliest cached enemy action so telegraph and execution stay identical.
gX_I1 gX_SceneBattle_Runtime_GetEnemyIntent(
    const gX_SceneBattle_Runtime *runtime, gX_U32 *outEnemyIndex,
    gX_SceneBattle_EnemyAI_Decision *outDecision) {
    if (outEnemyIndex) *outEnemyIndex = UINT32_MAX;
    if (outDecision) {
        outDecision->action = GX_BATTLE_AI_ACTION_ATTACK;
        outDecision->spellIndex = -1;
    }
    if (!runtime || !outEnemyIndex || !outDecision
        || runtime->outcome != GX_BATTLE_DEMO_OUTCOME_ACTIVE
        || runtime->enemyCount > GX_BATTLE_DEMO_MAX_ENEMIES) return false;
    gX_F64 Earliest = INFINITY;
    for (gX_U32 i = 0; i < runtime->enemyCount; i++) {
        const gX_SceneBattle_DemoEnemy *Enemy = &runtime->enemies[i];
        if (!Enemy->intentValid || !gX_SceneBattle_Runtime_EnemyAlive(runtime, i)
            || !isfinite(Enemy->intentReadyAt)) continue;
        if (Enemy->intentReadyAt < Earliest) {
            Earliest = Enemy->intentReadyAt;
            *outEnemyIndex = i;
            *outDecision = Enemy->intent;
        }
    }
    return *outEnemyIndex != UINT32_MAX;
}

//ai: find the living hostile with the shortest Manhattan distance to a party actor.
static gX_I1 gX_SceneBattle_Runtime_NearestEnemy(
    const gX_SceneBattle_Runtime *runtime, gX_U32 actorIndex,
    gX_U32 *outEnemyIndex) {
    if (outEnemyIndex) *outEnemyIndex = UINT32_MAX;
    if (!runtime || !outEnemyIndex || actorIndex >= GX_BATTLE_DEMO_PARTY_SIZE)
        return false;
    gX_I32 BestDistance = INT32_MAX;
    for (gX_U32 i = 0; i < runtime->enemyCount; i++) {
        if (!gX_SceneBattle_Runtime_EnemyAlive(runtime, i)) continue;
        gX_I32 DeltaCol = (gX_I32)runtime->enemies[i].gridCol
            - runtime->party[actorIndex].gridCol;
        gX_I32 DeltaRow = (gX_I32)runtime->enemies[i].gridRow
            - runtime->party[actorIndex].gridRow;
        if (DeltaCol < 0) DeltaCol = -DeltaCol;
        if (DeltaRow < 0) DeltaRow = -DeltaRow;
        if (DeltaCol + DeltaRow < BestDistance) {
            BestDistance = DeltaCol + DeltaRow;
            *outEnemyIndex = i;
        }
    }
    return *outEnemyIndex != UINT32_MAX;
}

//ai: select the living party member with the lowest normalized HP for support actions.
static gX_I1 gX_SceneBattle_Runtime_LowestParty(
    const gX_SceneBattle_Runtime *runtime, gX_U32 *outActorIndex,
    gX_F64 *outRatio) {
    if (outActorIndex) *outActorIndex = UINT32_MAX;
    if (outRatio) *outRatio = 1.0;
    if (!runtime || !outActorIndex || !outRatio) return false;
    gX_F64 Lowest = INFINITY;
    for (gX_U32 i = 0; i < GX_BATTLE_DEMO_PARTY_SIZE; i++) {
        const gX_SceneBattle_DemoPartyActor *Actor = &runtime->party[i];
        if (!gX_SceneBattle_Runtime_PartyAlive(runtime, i)
            || !isfinite(Actor->unit.hpMax) || Actor->unit.hpMax <= 0.0) continue;
        gX_F64 Ratio = Actor->unit.hp / Actor->unit.hpMax;
        if (Ratio < Lowest) {
            Lowest = Ratio;
            *outActorIndex = i;
        }
    }
    if (*outActorIndex == UINT32_MAX) return false;
    *outRatio = Lowest;
    return true;
}

//ai: choose the nearest living party member for an enemy's cached target.
static GX_BATTLE_NOINLINE gX_I1 gX_SceneBattle_Runtime_NearestParty(
    const gX_SceneBattle_Runtime *runtime, gX_U32 enemyIndex,
    gX_U32 *outActorIndex) {
    if (outActorIndex) *outActorIndex = UINT32_MAX;
    if (!runtime || !outActorIndex || enemyIndex >= runtime->enemyCount)
        return false;
    const gX_SceneBattle_DemoEnemy *Enemy = &runtime->enemies[enemyIndex];
    gX_I32 BestDistance = INT32_MAX;
    for (gX_U32 actor = 0; actor < GX_BATTLE_DEMO_PARTY_SIZE; actor++) {
        if (!gX_SceneBattle_Runtime_PartyAlive(runtime, actor)) continue;
        gX_I32 DeltaCol = (gX_I32)Enemy->gridCol
            - runtime->party[actor].gridCol;
        gX_I32 DeltaRow = (gX_I32)Enemy->gridRow
            - runtime->party[actor].gridRow;
        if (DeltaCol < 0) DeltaCol = -DeltaCol;
        if (DeltaRow < 0) DeltaRow = -DeltaRow;
        if (DeltaCol + DeltaRow < BestDistance) {
            BestDistance = DeltaCol + DeltaRow;
            *outActorIndex = actor;
        }
    }
    return *outActorIndex != UINT32_MAX;
}

//ai: classify a target as ahead, behind, or across the actor's facing axis.
static gX_I32 gX_SceneBattle_Runtime_FacingRelation(
    gX_I32 facing, gX_I32 deltaCol, gX_I32 deltaRow) {
    gX_I32 Dot = 0;
    if (facing == GX_BATTLE_DEMO_DIRECTION_NORTH) Dot = -deltaRow;
    else if (facing == GX_BATTLE_DEMO_DIRECTION_EAST) Dot = deltaCol;
    else if (facing == GX_BATTLE_DEMO_DIRECTION_SOUTH) Dot = deltaRow;
    else if (facing == GX_BATTLE_DEMO_DIRECTION_WEST) Dot = -deltaCol;
    return Dot > 0 ? 1 : (Dot < 0 ? -1 : 0);
}

//ai: close physical strikes reward adjacency and the controlled actor's facing.
static gX_F64 gX_SceneBattle_Runtime_PartyAttackMultiplier(
    const gX_SceneBattle_Runtime *runtime, gX_U32 actorIndex,
    gX_U32 enemyIndex) {
    const gX_SceneBattle_DemoPartyActor *Actor = &runtime->party[actorIndex];
    const gX_SceneBattle_DemoEnemy *Enemy = &runtime->enemies[enemyIndex];
    gX_I32 DeltaCol = (gX_I32)Enemy->gridCol - Actor->gridCol;
    gX_I32 DeltaRow = (gX_I32)Enemy->gridRow - Actor->gridRow;
    gX_I32 ColDistance = DeltaCol < 0 ? -DeltaCol : DeltaCol;
    gX_I32 RowDistance = DeltaRow < 0 ? -DeltaRow : DeltaRow;
    gX_I32 Distance = ColDistance + RowDistance;
    gX_F64 Multiplier = Distance <= 1 ? 1.5 : (Distance <= 3 ? 1.0 : 0.75);
    gX_I32 Relation = gX_SceneBattle_Runtime_FacingRelation(
        Actor->facing, DeltaCol, DeltaRow);
    return Multiplier * (Relation > 0 ? 1.0
        : (Relation < 0 ? 0.65 : 0.85));
}

//ai: rear hits are riskier while a party actor facing an attacker braces better.
static gX_F64 gX_SceneBattle_Runtime_EnemyAttackMultiplier(
    const gX_SceneBattle_Runtime *runtime, gX_U32 enemyIndex,
    gX_U32 actorIndex) {
    const gX_SceneBattle_DemoEnemy *Enemy = &runtime->enemies[enemyIndex];
    const gX_SceneBattle_DemoPartyActor *Actor = &runtime->party[actorIndex];
    gX_I32 DeltaCol = (gX_I32)Enemy->gridCol - Actor->gridCol;
    gX_I32 DeltaRow = (gX_I32)Enemy->gridRow - Actor->gridRow;
    gX_I32 ColDistance = DeltaCol < 0 ? -DeltaCol : DeltaCol;
    gX_I32 RowDistance = DeltaRow < 0 ? -DeltaRow : DeltaRow;
    gX_I32 Distance = ColDistance + RowDistance;
    gX_F64 Multiplier = Distance <= 1 ? 1.25 : (Distance <= 3 ? 1.0 : 0.75);
    gX_I32 Relation = gX_SceneBattle_Runtime_FacingRelation(
        Actor->facing, DeltaCol, DeltaRow);
    return Multiplier * (Relation < 0 ? 1.35 : 1.0);
}

//ai: apply a finite HP change while keeping a demo unit inside its meter bounds.
static gX_I1 gX_SceneBattle_Runtime_ApplyHP(gX_SceneBattle_DemoUnit *target,
    gX_F64 change, gX_F64 *outApplied) {
    if (outApplied) *outApplied = 0.0;
    if (!target || !isfinite(change) || !isfinite(target->hp)
        || !isfinite(target->hpMax) || target->hpMax <= 0.0) return false;
    gX_F64 PreviousHP = target->hp;
    target->hp += change;
    if (target->hp < 0.0) target->hp = 0.0;
    if (target->hp > target->hpMax) target->hp = target->hpMax;
    if (outApplied) *outApplied = target->hp - PreviousHP;
    return true;
}

//ai: reduce one hit through the target's guard and release its occupied cell on defeat.
static gX_I1 gX_SceneBattle_Runtime_ApplyDamage(
    gX_SceneBattle_Runtime *runtime, gX_SceneBattle_DemoUnit *target,
    gX_I1 *guarding, gX_I1 targetIsParty, gX_U8 targetIndex,
    gX_F64 damage, gX_I8 sourceKind, gX_U8 sourceIndex,
    gX_I8 targetKind, gX_U8 element) {
    if (!runtime || !target || !guarding || !isfinite(damage) || damage < 0.0
        || (targetIsParty && targetIndex >= GX_BATTLE_DEMO_PARTY_SIZE)
        || (!targetIsParty && targetIndex >= runtime->enemyCount)) return false;
    if (*guarding) {
        if (!isfinite(runtime->rules.guardDamageMultiplier)
            || runtime->rules.guardDamageMultiplier < 0.0f
            || runtime->rules.guardDamageMultiplier > 1.0f) return false;
        damage *= runtime->rules.guardDamageMultiplier;
    }
    gX_F64 Applied = 0.0;
    if (!gX_SceneBattle_Runtime_ApplyHP(target, -damage, &Applied)) return false;
    if (target->hp <= 0.0) {
        gX_U8 Col = targetIsParty ? runtime->party[targetIndex].gridCol
            : runtime->enemies[targetIndex].gridCol;
        gX_U8 Row = targetIsParty ? runtime->party[targetIndex].gridRow
            : runtime->enemies[targetIndex].gridRow;
        if (!gX_SceneBattle_Grid_CellSetOccupied(&runtime->grid, Col, Row, false))
            return false;
    }
    gX_SceneBattle_Runtime_RecordImpact(runtime, -Applied, sourceKind,
        sourceIndex, targetKind, targetIndex, false, element);
    *guarding = false;
    if (targetIsParty) gX_SceneBattle_Runtime_EnsureLocalController(runtime);
    return true;
}

//ai: record the actual bounded amount restored by a party-originated heal.
static gX_I1 gX_SceneBattle_Runtime_ApplyHealing(
    gX_SceneBattle_Runtime *runtime, gX_SceneBattle_DemoUnit *target,
    gX_F64 amount, gX_U8 sourceIndex, gX_U8 targetIndex, gX_U8 element) {
    gX_F64 Applied = 0.0;
    if (!runtime || !gX_SceneBattle_Runtime_ApplyHP(target, amount, &Applied))
        return false;
    gX_SceneBattle_Runtime_RecordImpact(runtime, Applied,
        GX_BATTLE_DEMO_IMPACT_PLAYER, sourceIndex,
        GX_BATTLE_DEMO_IMPACT_PLAYER, targetIndex, true, element);
    return true;
}

//ai: choose a living healing target first, then a deterministic affordable damage spell.
static gX_I32 gX_SceneBattle_Runtime_ChoosePartySpell(
    const gX_SceneBattle_Runtime *runtime, gX_U32 actorIndex,
    gX_F64 healingThreshold, gX_U32 *outHealTarget) {
    if (outHealTarget) *outHealTarget = UINT32_MAX;
    if (!runtime || actorIndex >= GX_BATTLE_DEMO_PARTY_SIZE || !outHealTarget)
        return -1;
    const gX_SceneBattle_DemoPartyActor *Actor = &runtime->party[actorIndex];
    gX_U32 HealTarget = UINT32_MAX;
    gX_F64 HealthRatio = 1.0;
    if (gX_SceneBattle_Runtime_LowestParty(runtime, &HealTarget, &HealthRatio)
        && HealthRatio <= healingThreshold) {
        gX_I32 HealIndex = gX_SceneBattle_Action_FindHealSpell(
            runtime->rules.spells, GX_BATTLE_DEMO_SPELL_COUNT);
        if (HealIndex >= 0) {
            const gX_SceneBattle_SpellCandidate *Spell =
                &runtime->rules.spells[HealIndex];
            gX_F64 Cost = -Spell->mpChange;
            if (isfinite(Cost) && Cost > 0.0 && Actor->unit.mp >= Cost
                && !gX_SceneBattle_Cooldown_IsActive(&Actor->cooldowns,
                    GX_BATTLE_ACTION_MAGIC, HealIndex)) {
                *outHealTarget = HealTarget;
                return HealIndex;
            }
        }
    }
    for (gX_U32 attempt = 0; attempt < 3; attempt++) {
        gX_I32 SpellIndex = gX_SceneBattle_Action_ChooseDamageSpell(
            runtime->rules.spells, GX_BATTLE_DEMO_SPELL_COUNT,
            Actor->turnOrdinal + attempt);
        if (SpellIndex < 0) break;
        const gX_SceneBattle_SpellCandidate *Spell =
            &runtime->rules.spells[SpellIndex];
        gX_F64 Cost = -Spell->mpChange;
        if (isfinite(Cost) && Cost > 0.0 && Actor->unit.mp >= Cost
            && !gX_SceneBattle_Cooldown_IsActive(&Actor->cooldowns,
                GX_BATTLE_ACTION_MAGIC, SpellIndex)) return SpellIndex;
    }
    return -1;
}

//ai: resolve one party member's command against actor-owned MP, cooldown, and ATB state.
static gX_I1 gX_SceneBattle_Runtime_ResolvePartyAction(
    gX_SceneBattle_Runtime *runtime, gX_U32 actorIndex, gX_I32 action,
    gX_F64 elapsed, gX_I1 isAI) {
    if (!runtime || actorIndex >= GX_BATTLE_DEMO_PARTY_SIZE
        || runtime->party[actorIndex].role < GX_BATTLE_DEMO_ROLE_VANGUARD
        || runtime->party[actorIndex].role > GX_BATTLE_DEMO_ROLE_MEDIC
        || runtime->outcome != GX_BATTLE_DEMO_OUTCOME_ACTIVE
        || !isfinite(elapsed) || !gX_SceneBattle_Runtime_IsActorReady(runtime,
            actorIndex)) return false;
    gX_SceneBattle_DemoPartyActor *Actor = &runtime->party[actorIndex];
    gX_U32 EnemyIndex = runtime->targetIndex;
    if (isAI && !gX_SceneBattle_Runtime_NearestEnemy(runtime, actorIndex,
            &EnemyIndex)) return false;
    if (action == GX_BATTLE_DEMO_ACTION_ATTACK) {
        if (!gX_SceneBattle_Runtime_EnemyAlive(runtime, EnemyIndex)) {
            if (!gX_SceneBattle_Runtime_FirstEnemy(runtime, &EnemyIndex)) return false;
        }
        if (gX_SceneBattle_Cooldown_IsActive(&Actor->cooldowns,
                GX_BATTLE_ACTION_ATTACK, 0)) {
            gX_SceneBattle_Runtime_Message(runtime, "Attack is cooling down.");
            return false;
        }
        if (!isfinite(runtime->rules.playerAttackDamage)
            || runtime->rules.playerAttackDamage <= 0.0f
            || !gX_SceneBattle_Cooldown_Set(&Actor->cooldowns,
                &runtime->cooldownTuning, GX_BATTLE_ACTION_ATTACK, 0, 0, 0, false))
            return false;
        gX_SceneBattle_DemoEnemy *Target = &runtime->enemies[EnemyIndex];
        if (!gX_SceneBattle_Runtime_ApplyDamage(runtime, &Target->unit,
                &Target->guarding, false, (gX_U8)EnemyIndex,
                (gX_F64)runtime->rules.playerAttackDamage * Actor->attackScale
                    * gX_SceneBattle_Runtime_PartyAttackMultiplier(runtime,
                        actorIndex, EnemyIndex),
                GX_BATTLE_DEMO_IMPACT_PLAYER, (gX_U8)actorIndex,
                (gX_I8)Target->kind, 0)) return false;
        static const gX_I8 *AttackMessages[GX_BATTLE_DEMO_PARTY_SIZE] = {
            "Vanguard strikes the target.", "Arcanist strikes the target.",
            "Medic strikes the target."};
        gX_SceneBattle_Runtime_Message(runtime, AttackMessages[Actor->role]);
    } else if (action == GX_BATTLE_DEMO_ACTION_MAGIC) {
        gX_U32 HealTarget = UINT32_MAX;
        gX_F64 HealThreshold = isAI
            && Actor->role == GX_BATTLE_DEMO_ROLE_MEDIC
                ? 0.78 : runtime->rules.magicHealThreshold;
        gX_I32 SpellIndex = gX_SceneBattle_Runtime_ChoosePartySpell(runtime,
            actorIndex, HealThreshold, &HealTarget);
        if (SpellIndex < 0) {
            gX_SceneBattle_Runtime_Message(runtime, "No available magic.");
            return false;
        }
        gX_SceneBattle_SpellCandidate *Spell = &runtime->rules.spells[SpellIndex];
        gX_F64 Cost = -Spell->mpChange;
        gX_I1 TargetIsPlayer = false;
        gX_I1 TargetIsSelf = false;
        if (!gX_SceneBattle_Action_TargetRoute(GX_BATTLE_ACTION_MAGIC, true,
                Spell, GX_BATTLE_SKILL_TARGET_ENEMY,
                &TargetIsPlayer, &TargetIsSelf)
            || !isfinite(Cost) || Cost <= 0.0 || Actor->unit.mp < Cost) {
            gX_SceneBattle_Runtime_Message(runtime, "Not enough MP.");
            return false;
        }
        if (!TargetIsPlayer && !gX_SceneBattle_Runtime_EnemyAlive(runtime, EnemyIndex)) {
            if (!gX_SceneBattle_Runtime_FirstEnemy(runtime, &EnemyIndex)) return false;
        }
        if (TargetIsPlayer && (HealTarget >= GX_BATTLE_DEMO_PARTY_SIZE
            || !gX_SceneBattle_Runtime_PartyAlive(runtime, HealTarget))) return false;
        if (!gX_SceneBattle_Cooldown_Set(&Actor->cooldowns,
                &runtime->cooldownTuning, GX_BATTLE_ACTION_MAGIC, SpellIndex,
                0, 0, false)) return false;
        gX_I1 Applied = false;
        if (Spell->hpChange < 0.0) {
            gX_SceneBattle_DemoEnemy *Target = &runtime->enemies[EnemyIndex];
            Applied = gX_SceneBattle_Runtime_ApplyDamage(runtime, &Target->unit,
                &Target->guarding, false, (gX_U8)EnemyIndex, -Spell->hpChange,
                GX_BATTLE_DEMO_IMPACT_PLAYER, (gX_U8)actorIndex,
                (gX_I8)Target->kind, (gX_U8)Spell->element);
        } else if (Spell->hpChange > 0.0 && TargetIsPlayer) {
            Applied = gX_SceneBattle_Runtime_ApplyHealing(runtime,
                &runtime->party[HealTarget].unit, Spell->hpChange,
                (gX_U8)actorIndex, (gX_U8)HealTarget, (gX_U8)Spell->element);
        }
        if (!Applied) return false;
        Actor->unit.mp -= Cost;
        if (Actor->unit.mp < 0.0) Actor->unit.mp = 0.0;
        static const gX_I8 *SpellMessages[GX_BATTLE_DEMO_SPELL_COUNT] = {
            "Party casts FIRE.", "Party casts ICE.",
            "Party casts LIGHTNING.", "Party casts MEND."};
        gX_SceneBattle_Runtime_Message(runtime, SpellMessages[SpellIndex]);
    } else if (action == GX_BATTLE_DEMO_ACTION_ITEM) {
        gX_U32 HealTarget = UINT32_MAX;
        gX_F64 HealthRatio = 1.0;
        if (!gX_SceneBattle_Runtime_LowestParty(runtime, &HealTarget,
                &HealthRatio) || runtime->rules.potionCount <= 0) {
            gX_SceneBattle_Runtime_Message(runtime, "No potion remaining.");
            return false;
        }
        if (gX_SceneBattle_Cooldown_IsActive(&Actor->cooldowns,
                GX_BATTLE_ACTION_ITEM, 0)) {
            gX_SceneBattle_Runtime_Message(runtime, "Potion is cooling down.");
            return false;
        }
        if (!isfinite(runtime->rules.itemHealAmount)
            || runtime->rules.itemHealAmount <= 0.0f
            || !gX_SceneBattle_Cooldown_Set(&Actor->cooldowns,
                &runtime->cooldownTuning, GX_BATTLE_ACTION_ITEM, 0, 0, 0, false)
            || !gX_SceneBattle_Runtime_ApplyHealing(runtime,
                &runtime->party[HealTarget].unit, runtime->rules.itemHealAmount,
                (gX_U8)actorIndex, (gX_U8)HealTarget, 0)) return false;
        runtime->rules.potionCount--;
        gX_SceneBattle_Runtime_Message(runtime, "Potion restores party HP.");
    } else if (action == GX_BATTLE_DEMO_ACTION_GUARD) {
        if (gX_SceneBattle_Cooldown_IsActive(&Actor->cooldowns,
                GX_BATTLE_ACTION_SKILLS, 0)
            || !gX_SceneBattle_Cooldown_Set(&Actor->cooldowns,
                &runtime->cooldownTuning, GX_BATTLE_ACTION_SKILLS, 0, 0, 0, false))
            return false;
        Actor->guarding = true;
        gX_SceneBattle_Runtime_Message(runtime, "Party member guards the next hit.");
    } else if (action == GX_BATTLE_DEMO_ACTION_RUN) {
        gX_SceneBattle_Runtime_Finish(runtime, GX_BATTLE_DEMO_OUTCOME_ESCAPED,
            "ESCAPED // ENCOUNTER ENDED");
        return true;
    } else {
        gX_SceneBattle_Runtime_Message(runtime, "Unknown command.");
        return false;
    }
    Actor->energy = 0.0f;
    Actor->turnOrdinal++;
    runtime->turnNumber++;
    return gX_SceneBattle_Runtime_AdvanceEncounter(runtime);
}

//ai: accept a command only from the actor's assigned controller and a full gauge.
gX_I1 gX_SceneBattle_Runtime_ConfirmActorAction(
    gX_SceneBattle_Runtime *runtime, gX_U32 actorIndex, gX_I32 action,
    gX_F64 elapsed) {
    if (!runtime || actorIndex >= GX_BATTLE_DEMO_PARTY_SIZE
        || runtime->party[actorIndex].controllerId == GX_BATTLE_DEMO_CONTROLLER_AI
        || action < GX_BATTLE_DEMO_ACTION_ATTACK || action > GX_BATTLE_DEMO_ACTION_RUN)
        return false;
    if (!gX_SceneBattle_Runtime_IsActorReady(runtime, actorIndex)) {
        gX_SceneBattle_Runtime_Message(runtime, "ATB is charging.");
        return false;
    }
    return gX_SceneBattle_Runtime_ResolvePartyAction(runtime, actorIndex,
        action, elapsed, false);
}

//ai: route the local command surface to the currently controller-owned actor.
gX_I1 gX_SceneBattle_Runtime_ConfirmAction(gX_SceneBattle_Runtime *runtime,
    gX_I32 action, gX_F64 elapsed) {
    gX_U32 ActorIndex = UINT32_MAX;
    return gX_SceneBattle_Runtime_GetControlledActor(runtime, &ActorIndex)
        && gX_SceneBattle_Runtime_ConfirmActorAction(runtime, ActorIndex,
            action, elapsed);
}

//ai: create one cached intent per enemy as soon as its independent ATB fills.
static gX_I1 gX_SceneBattle_Runtime_PrepareEnemyIntents(
    gX_SceneBattle_Runtime *runtime, gX_F64 elapsed) {
    for (gX_U32 i = 0; i < runtime->enemyCount; i++) {
        gX_SceneBattle_DemoEnemy *Enemy = &runtime->enemies[i];
        if (!gX_SceneBattle_Runtime_EnemyAlive(runtime, i)
            || Enemy->intentValid || Enemy->energy < GX_BATTLE_DEMO_ATB_MAX) continue;
        if (!gX_SceneBattle_Runtime_ChooseEnemyIntent(runtime, i,
                &Enemy->intent)) return false;
        gX_U32 TargetIndex = UINT32_MAX;
        if (!gX_SceneBattle_Runtime_NearestParty(runtime, i, &TargetIndex))
            return false;
        Enemy->targetPartyIndex = (gX_U8)TargetIndex;
        Enemy->intentReadyAt = elapsed + runtime->rules.enemyTurnDelay;
        Enemy->intentValid = true;
    }
    return true;
}

//ai: execute the cached hostile intent after its telegraph delay.
static gX_I1 gX_SceneBattle_Runtime_ExecuteEnemyIntent(
    gX_SceneBattle_Runtime *runtime, gX_U32 enemyIndex, gX_F64 elapsed) {
    if (!runtime || enemyIndex >= runtime->enemyCount
        || !gX_SceneBattle_Runtime_EnemyAlive(runtime, enemyIndex)) return false;
    gX_SceneBattle_DemoEnemy *Enemy = &runtime->enemies[enemyIndex];
    if (!Enemy->intentValid || elapsed < Enemy->intentReadyAt) return true;
    gX_U32 TargetIndex = Enemy->targetPartyIndex;
    if (!gX_SceneBattle_Runtime_PartyAlive(runtime, TargetIndex)) {
        if (!gX_SceneBattle_Runtime_NearestParty(runtime, enemyIndex,
                &TargetIndex)) {
            gX_SceneBattle_Runtime_Finish(runtime, GX_BATTLE_DEMO_OUTCOME_DEFEAT,
                "DEFEAT // PARTY DOWN");
            return true;
        }
    }
    if (Enemy->intent.action == GX_BATTLE_AI_ACTION_GUARD) {
        Enemy->guarding = true;
        gX_SceneBattle_Runtime_Message(runtime,
            Enemy->kind == GX_BATTLE_DEMO_ENEMY_SENTINEL
                ? "Rift Sentinel braces for an attack."
                : "Rift hostile braces for an attack.");
    } else if (Enemy->intent.action == GX_BATTLE_AI_ACTION_MAGIC) {
        gX_I32 SpellIndex = Enemy->intent.spellIndex;
        if (SpellIndex < 0 || SpellIndex >= GX_BATTLE_DEMO_SPELL_COUNT) return false;
        gX_SceneBattle_SpellCandidate *Spell = &runtime->rules.spells[SpellIndex];
        gX_F64 Cost = -Spell->mpChange;
        gX_I1 TargetIsPlayer = false;
        gX_I1 TargetIsSelf = false;
        if (!gX_SceneBattle_Action_TargetRoute(GX_BATTLE_ACTION_MAGIC, false,
                Spell, GX_BATTLE_SKILL_TARGET_ENEMY,
                &TargetIsPlayer, &TargetIsSelf)
            || !TargetIsPlayer || TargetIsSelf || !isfinite(Cost) || Cost <= 0.0
            || Spell->hpChange >= 0.0 || Enemy->unit.mp < Cost
            || !gX_SceneBattle_Cooldown_Set(&Enemy->cooldowns,
                &runtime->cooldownTuning, GX_BATTLE_ACTION_MAGIC, SpellIndex,
                0, 0, false)) return false;
        gX_SceneBattle_DemoPartyActor *Target = &runtime->party[TargetIndex];
        if (!gX_SceneBattle_Runtime_ApplyDamage(runtime, &Target->unit,
                &Target->guarding, true, (gX_U8)TargetIndex,
                -Spell->hpChange, (gX_I8)Enemy->kind, (gX_U8)enemyIndex,
                GX_BATTLE_DEMO_IMPACT_PLAYER, (gX_U8)Spell->element)) return false;
        Enemy->unit.mp -= Cost;
        gX_SceneBattle_Runtime_Message(runtime,
            Enemy->kind == GX_BATTLE_DEMO_ENEMY_ADEPT
                ? "Rift Adept casts elemental magic."
                : "Rift Warden casts elemental magic.");
    } else if (Enemy->intent.action == GX_BATTLE_AI_ACTION_ATTACK) {
        gX_I1 TargetIsPlayer = false;
        gX_I1 TargetIsSelf = false;
        if (!gX_SceneBattle_Action_TargetRoute(GX_BATTLE_ACTION_ATTACK, false,
                NULL, GX_BATTLE_SKILL_TARGET_ENEMY,
                &TargetIsPlayer, &TargetIsSelf)
            || !TargetIsPlayer || TargetIsSelf
            || !isfinite(Enemy->attackDamage) || Enemy->attackDamage < 0.0f)
            return false;
        gX_SceneBattle_DemoPartyActor *Target = &runtime->party[TargetIndex];
        if (!gX_SceneBattle_Runtime_ApplyDamage(runtime, &Target->unit,
                &Target->guarding, true, (gX_U8)TargetIndex,
                Enemy->attackDamage
                    * gX_SceneBattle_Runtime_EnemyAttackMultiplier(runtime,
                        enemyIndex, TargetIndex), (gX_I8)Enemy->kind,
                (gX_U8)enemyIndex, GX_BATTLE_DEMO_IMPACT_PLAYER, 0)) return false;
        gX_SceneBattle_Runtime_Message(runtime, "Rift hostile strikes the party.");
    } else {
        return false;
    }
    Enemy->energy = 0.0f;
    Enemy->intentValid = false;
    Enemy->turnOrdinal++;
    runtime->turnNumber++;
    return gX_SceneBattle_Runtime_AdvanceEncounter(runtime);
}

//ai: let AI companions autofight while all party and enemy ATB meters progress continuously.
gX_I1 gX_SceneBattle_Runtime_Update(gX_SceneBattle_Runtime *runtime,
    gX_F64 elapsed, gX_F32 deltaSeconds) {
    if (!runtime || runtime->enemyCount == 0
        || runtime->enemyCount > GX_BATTLE_DEMO_MAX_ENEMIES
        || !gX_SceneBattle_Runtime_BoardValid(runtime)
        || !isfinite(elapsed) || !isfinite(deltaSeconds) || deltaSeconds < 0.0f
        || !isfinite(runtime->rules.enemyTurnDelay)
        || runtime->rules.enemyTurnDelay < 0.0f
        || !gX_SceneBattle_Runtime_TickImpacts(runtime, deltaSeconds)) return false;
    for (gX_U32 i = 0; i < GX_BATTLE_DEMO_PARTY_SIZE; i++) {
        gX_SceneBattle_DemoPartyActor *Actor = &runtime->party[i];
        if (!isfinite(Actor->energy) || Actor->energy < 0.0f
            || Actor->energy > GX_BATTLE_DEMO_ATB_MAX) return false;
        gX_SceneBattle_Cooldown_Update(&Actor->cooldowns, deltaSeconds, 1.0f);
        if (gX_SceneBattle_Runtime_PartyAlive(runtime, i))
            Actor->energy = fminf(GX_BATTLE_DEMO_ATB_MAX,
                Actor->energy + GX_BATTLE_DEMO_PARTY_ATB_RATE * deltaSeconds);
    }
    for (gX_U32 i = 0; i < runtime->enemyCount; i++) {
        gX_SceneBattle_DemoEnemy *Enemy = &runtime->enemies[i];
        if (!isfinite(Enemy->energy) || Enemy->energy < 0.0f
            || Enemy->energy > GX_BATTLE_DEMO_ATB_MAX
            || (Enemy->intentValid && !isfinite(Enemy->intentReadyAt)))
            return false;
        gX_SceneBattle_Cooldown_Update(&Enemy->cooldowns, deltaSeconds, 1.0f);
        if (gX_SceneBattle_Runtime_EnemyAlive(runtime, i))
            Enemy->energy = fminf(GX_BATTLE_DEMO_ATB_MAX,
                Enemy->energy + GX_BATTLE_DEMO_ENEMY_ATB_RATE * deltaSeconds);
    }
    if (runtime->outcome != GX_BATTLE_DEMO_OUTCOME_ACTIVE) return true;
    if (gX_SceneBattle_Runtime_LivingPartyCount(runtime) == 0) {
        gX_SceneBattle_Runtime_Finish(runtime, GX_BATTLE_DEMO_OUTCOME_DEFEAT,
            "DEFEAT // PARTY DOWN");
        return true;
    }
    for (gX_U32 i = 0; i < GX_BATTLE_DEMO_PARTY_SIZE; i++) {
        gX_SceneBattle_DemoPartyActor *Actor = &runtime->party[i];
        if (Actor->controllerId != GX_BATTLE_DEMO_CONTROLLER_AI
            || !gX_SceneBattle_Runtime_IsActorReady(runtime, i)) continue;
        gX_I32 Action = GX_BATTLE_DEMO_ACTION_ATTACK;
        gX_U32 HealTarget = UINT32_MAX;
        gX_F64 HealThreshold = Actor->role == GX_BATTLE_DEMO_ROLE_MEDIC
            ? 0.78 : runtime->rules.magicHealThreshold;
        gX_I32 SpellIndex = gX_SceneBattle_Runtime_ChoosePartySpell(runtime,
            i, HealThreshold, &HealTarget);
        gX_I1 WantsMagic = Actor->role == GX_BATTLE_DEMO_ROLE_MEDIC
            ? SpellIndex >= 0 && HealTarget < GX_BATTLE_DEMO_PARTY_SIZE
                && runtime->rules.spells[SpellIndex].isHeal
            : Actor->role == GX_BATTLE_DEMO_ROLE_ARCANIST
                && Actor->turnOrdinal % 3U == 1U && SpellIndex >= 0;
        if (WantsMagic) Action = GX_BATTLE_DEMO_ACTION_MAGIC;
        if (!gX_SceneBattle_Runtime_ResolvePartyAction(runtime, i, Action,
                elapsed, true)) {
            if (Action != GX_BATTLE_DEMO_ACTION_ATTACK
                && !gX_SceneBattle_Runtime_ResolvePartyAction(runtime, i,
                    GX_BATTLE_DEMO_ACTION_ATTACK, elapsed, true))
                return false;
        }
        if (runtime->outcome != GX_BATTLE_DEMO_OUTCOME_ACTIVE) return true;
    }
    if (!gX_SceneBattle_Runtime_PrepareEnemyIntents(runtime, elapsed)) return false;
    for (gX_U32 i = 0; i < runtime->enemyCount; i++) {
        if (runtime->enemies[i].intentValid
            && elapsed >= runtime->enemies[i].intentReadyAt
            && !gX_SceneBattle_Runtime_ExecuteEnemyIntent(runtime, i, elapsed))
            return false;
        if (runtime->outcome != GX_BATTLE_DEMO_OUTCOME_ACTIVE) return true;
    }
    return true;
}
