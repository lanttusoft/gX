/**********************************************************************************************
*
*   [gX: src/scene/battle_ai.c]
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


//ai: distill gameX balanced combat choices into a predictable one-enemy policy.
#include "battle_ai.h"

#include <math.h>

//ai: choose affordable damage magic by deterministic element order when its weighted slot opens.
static gX_I32 gX_SceneBattle_EnemyAI_FindMagic(
    const gX_SceneBattle_EnemyAI_Context *context) {
    for (gX_U32 attempt = 0; attempt < 3; attempt++) {
        gX_I32 spellIndex = gX_SceneBattle_Action_ChooseDamageSpell(
            context->spells, context->spellCount, context->turnOrdinal + attempt);
        if (spellIndex < 0) break;
        const gX_SceneBattle_SpellCandidate *spell = &context->spells[spellIndex];
        gX_F64 cost = -spell->mpChange;
        if (isfinite(cost) && cost > 0.0 && context->mp >= cost
            && !gX_SceneBattle_Cooldown_IsActive(context->cooldowns,
                GX_BATTLE_ACTION_MAGIC, spellIndex)) return spellIndex;
    }
    return -1;
}

//ai: map the reference AI probability profiles onto deterministic 20-turn cycles.
gX_I1 gX_SceneBattle_EnemyAI_ChooseForProfile(
    const gX_SceneBattle_EnemyAI_Context *context, gX_I32 profile,
    gX_SceneBattle_EnemyAI_Decision *outDecision) {
    if (outDecision) {
        outDecision->action = GX_BATTLE_AI_ACTION_ATTACK;
        outDecision->spellIndex = -1;
    }
    if (!context || !outDecision
        || profile < GX_BATTLE_AI_PROFILE_BALANCED
        || profile > GX_BATTLE_AI_PROFILE_DEFENSIVE
        || !isfinite(context->hp)
        || !isfinite(context->hpMax) || context->hpMax <= 0.0
        || context->hp <= 0.0 || !isfinite(context->mp) || context->mp < 0.0
        || (context->spellCount > 0 && !context->spells)) return false;

    gX_U32 phaseOffset = profile == GX_BATTLE_AI_PROFILE_BALANCED ? 5U : 0U;
    gX_U32 slot = ((context->turnOrdinal % GX_BATTLE_AI_BALANCED_PERIOD) * 7U
        + phaseOffset) % GX_BATTLE_AI_BALANCED_PERIOD;
    gX_I32 spellIndex = -1;
    if (profile == GX_BATTLE_AI_PROFILE_AGGRESSIVE) {
        if (slot < GX_BATTLE_AI_AGGRESSIVE_MAGIC_SLOTS)
            spellIndex = gX_SceneBattle_EnemyAI_FindMagic(context);
        if (spellIndex >= 0) {
            outDecision->action = GX_BATTLE_AI_ACTION_MAGIC;
            outDecision->spellIndex = spellIndex;
        }
        return true;
    }
    if (profile == GX_BATTLE_AI_PROFILE_DEFENSIVE) {
        if (slot < GX_BATTLE_AI_DEFENSIVE_GUARD_SLOTS
            && context->hp / context->hpMax
                < GX_BATTLE_AI_DEFENSIVE_GUARD_HP_RATIO)
            outDecision->action = GX_BATTLE_AI_ACTION_GUARD;
        return true;
    }
    if (slot < GX_BATTLE_AI_BALANCED_MAGIC_SLOTS)
        spellIndex = gX_SceneBattle_EnemyAI_FindMagic(context);
    if (spellIndex >= 0) {
        outDecision->action = GX_BATTLE_AI_ACTION_MAGIC;
        outDecision->spellIndex = spellIndex;
    } else if (slot < GX_BATTLE_AI_BALANCED_GUARD_SLOTS
        && context->hp / context->hpMax < GX_BATTLE_AI_BALANCED_GUARD_HP_RATIO) {
        outDecision->action = GX_BATTLE_AI_ACTION_GUARD;
    }
    return true;
}

//ai: retain the original balanced helper as a small compatibility wrapper.
gX_I1 gX_SceneBattle_EnemyAI_ChooseBalanced(
    const gX_SceneBattle_EnemyAI_Context *context,
    gX_SceneBattle_EnemyAI_Decision *outDecision) {
    return gX_SceneBattle_EnemyAI_ChooseForProfile(context,
        GX_BATTLE_AI_PROFILE_BALANCED, outDecision);
}
