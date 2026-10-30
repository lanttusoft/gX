/**********************************************************************************************
*
*   [gX: src/scene/battle_ai.h]
*   Headless deterministic enemy decision rules for the battle demo
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


#ifndef GX_SCENE_BATTLE_AI_H
#define GX_SCENE_BATTLE_AI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "battle_actions.h"

#define GX_BATTLE_AI_ACTION_ATTACK GX_BATTLE_ACTION_ATTACK
#define GX_BATTLE_AI_ACTION_MAGIC GX_BATTLE_ACTION_MAGIC
#define GX_BATTLE_AI_ACTION_GUARD 4
//ai: distill the reference AI type probabilities into fixed deterministic cycles.
#define GX_BATTLE_AI_PROFILE_BALANCED 0
#define GX_BATTLE_AI_PROFILE_AGGRESSIVE 1
#define GX_BATTLE_AI_PROFILE_DEFENSIVE 2
#define GX_BATTLE_AI_BALANCED_PERIOD 20
#define GX_BATTLE_AI_BALANCED_MAGIC_SLOTS 4
#define GX_BATTLE_AI_BALANCED_GUARD_SLOTS 7
#define GX_BATTLE_AI_BALANCED_GUARD_HP_RATIO 0.4
#define GX_BATTLE_AI_AGGRESSIVE_MAGIC_SLOTS 3
#define GX_BATTLE_AI_DEFENSIVE_GUARD_SLOTS 12
#define GX_BATTLE_AI_DEFENSIVE_GUARD_HP_RATIO 0.3

typedef struct gX_SceneBattle_EnemyAI_Context {
    gX_F64 hp;
    gX_F64 hpMax;
    gX_F64 mp;
    gX_U32 turnOrdinal;
    const gX_SceneBattle_SpellCandidate *spells;
    gX_U32 spellCount;
    const gX_SceneBattle_CooldownState *cooldowns;
} gX_SceneBattle_EnemyAI_Context;

typedef struct gX_SceneBattle_EnemyAI_Decision {
    gX_I32 action;
    gX_I32 spellIndex;
} gX_SceneBattle_EnemyAI_Decision;

//ai: expose the distilled single-enemy balanced policy as deterministic CPU logic.
gX_I1 gX_SceneBattle_EnemyAI_ChooseBalanced(
    const gX_SceneBattle_EnemyAI_Context *context,
    gX_SceneBattle_EnemyAI_Decision *outDecision);
//ai: choose a bounded deterministic profile distilled from the reference AI types.
gX_I1 gX_SceneBattle_EnemyAI_ChooseForProfile(
    const gX_SceneBattle_EnemyAI_Context *context, gX_I32 profile,
    gX_SceneBattle_EnemyAI_Decision *outDecision);

#ifdef __cplusplus
}
#endif

#endif //GX_SCENE_BATTLE_AI_H
