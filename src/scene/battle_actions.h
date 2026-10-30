/**********************************************************************************************
*
*   [gX: src/scene/battle_actions.h]
*   Headless battle action rules — targeting, spell choice, cooldowns
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


#ifndef GX_SCENE_BATTLE_ACTIONS_H
#define GX_SCENE_BATTLE_ACTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

//ai: define gX battle-local action metadata without importing the archived game layer.
#define GX_BATTLE_ACTION_ATTACK 0
#define GX_BATTLE_ACTION_SKILLS 1
#define GX_BATTLE_ACTION_MAGIC 2
#define GX_BATTLE_ACTION_ITEM 3

#define GX_BATTLE_SKILL_TARGET_SELF 0
#define GX_BATTLE_SKILL_TARGET_ALLY 1
#define GX_BATTLE_SKILL_TARGET_ENEMY 2
#define GX_BATTLE_SKILL_TARGET_ALL 3

#define GX_BATTLE_ELEMENT_FIRE 1
#define GX_BATTLE_ELEMENT_ICE 2
#define GX_BATTLE_ELEMENT_LIGHTNING 3

#define GX_BATTLE_COOLDOWN_TRACKS 16

typedef struct gX_SceneBattle_SpellCandidate {
    gX_I32 element;
    gX_F64 hpChange;
    gX_F64 mpChange;
    gX_I1 isHeal;
    gX_I1 isSupport;
} gX_SceneBattle_SpellCandidate;

typedef struct gX_SceneBattle_TargetVitals {
    gX_F64 hp;
    gX_F64 minimum;
} gX_SceneBattle_TargetVitals;

typedef struct gX_SceneBattle_CooldownTrack {
    gX_I32 actionType;
    gX_I32 actionIndex;
    gX_F32 remaining;
    gX_F32 baseDuration;
    gX_I32 repeatCount;
} gX_SceneBattle_CooldownTrack;

typedef struct gX_SceneBattle_CooldownState {
    gX_SceneBattle_CooldownTrack tracks[GX_BATTLE_COOLDOWN_TRACKS];
} gX_SceneBattle_CooldownState;

typedef struct gX_SceneBattle_CooldownTuning {
    gX_F32 baseAttack;
    gX_F32 baseMagic;
    gX_F32 baseSkill;
    gX_F32 baseItem;
    gX_F32 proficiencyReduction;
    gX_F32 minimumDuration;
    gX_F32 repeatStep;
    gX_F32 maximumDurationMultiple;
    gX_F32 disorientationMultiplier;
} gX_SceneBattle_CooldownTuning;

//ai: expose independent action rules so scene code and headless tests share them.
gX_I1 gX_SceneBattle_Action_TargetRoute(gX_I32 actionType,
    gX_I1 actorIsPlayer, const gX_SceneBattle_SpellCandidate *spell,
    gX_I32 skillTargetType, gX_I1 *outTargetIsPlayer, gX_I1 *outTargetIsSelf);
gX_I32 gX_SceneBattle_Action_FindHealSpell(
    const gX_SceneBattle_SpellCandidate *spells, gX_U32 count);
gX_I32 gX_SceneBattle_Action_ChooseDamageSpell(
    const gX_SceneBattle_SpellCandidate *spells, gX_U32 count,
    gX_U32 choiceOrdinal);
gX_I1 gX_SceneBattle_Target_IsAlive(const gX_SceneBattle_TargetVitals *target);
gX_I1 gX_SceneBattle_Target_FirstAlive(
    const gX_SceneBattle_TargetVitals *targets, gX_U32 count,
    gX_U32 *outIndex);

gX_I0 gX_SceneBattle_CooldownTuning_Default(
    gX_SceneBattle_CooldownTuning *tuning);
gX_I0 gX_SceneBattle_Cooldown_Init(gX_SceneBattle_CooldownState *state);
gX_I1 gX_SceneBattle_Cooldown_IsActive(
    const gX_SceneBattle_CooldownState *state, gX_I32 actionType,
    gX_I32 actionIndex);
gX_I1 gX_SceneBattle_Cooldown_BaseDuration(
    const gX_SceneBattle_CooldownTuning *tuning, gX_I32 actionType,
    gX_I32 fightingProficiency, gX_I32 magicProficiency,
    gX_F32 *outDuration);
gX_I1 gX_SceneBattle_Cooldown_Set(
    gX_SceneBattle_CooldownState *state,
    const gX_SceneBattle_CooldownTuning *tuning, gX_I32 actionType,
    gX_I32 actionIndex, gX_I32 fightingProficiency,
    gX_I32 magicProficiency, gX_I1 disoriented);
gX_I0 gX_SceneBattle_Cooldown_Update(
    gX_SceneBattle_CooldownState *state, gX_F32 elapsedSeconds,
    gX_F32 speedMultiplier);

#ifdef __cplusplus
}
#endif

#endif //GX_SCENE_BATTLE_ACTIONS_H
