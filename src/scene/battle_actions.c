/**********************************************************************************************
*
*   [gX: src/scene/battle_actions.c]
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


//ai: port pure action routing, selector, and cooldown rules without game content dependencies.
#include "battle_actions.h"

#include <limits.h>
#include <math.h>
#include <string.h>

//ai: validate all mutable cooldown tuning before it can enter timer arithmetic.
static gX_I1 gX_SceneBattle_CooldownTuning_Valid(
    const gX_SceneBattle_CooldownTuning *tuning) {
    if (!tuning || !isfinite(tuning->baseAttack) || tuning->baseAttack < 0.0f
        || !isfinite(tuning->baseMagic) || tuning->baseMagic < 0.0f
        || !isfinite(tuning->baseSkill) || tuning->baseSkill < 0.0f
        || !isfinite(tuning->baseItem) || tuning->baseItem < 0.0f
        || !isfinite(tuning->proficiencyReduction) || tuning->proficiencyReduction < 0.0f
        || !isfinite(tuning->minimumDuration) || tuning->minimumDuration < 0.0f
        || !isfinite(tuning->repeatStep) || tuning->repeatStep < 0.0f
        || !isfinite(tuning->maximumDurationMultiple)
        || tuning->maximumDurationMultiple < 1.0f
        || !isfinite(tuning->disorientationMultiplier)
        || tuning->disorientationMultiplier <= 0.0f) return false;
    return true;
}

//ai: accept only action types supported by the pure cooldown model.
static gX_I1 gX_SceneBattle_ActionType_Valid(gX_I32 actionType) {
    return actionType == GX_BATTLE_ACTION_ATTACK
        || actionType == GX_BATTLE_ACTION_SKILLS
        || actionType == GX_BATTLE_ACTION_MAGIC
        || actionType == GX_BATTLE_ACTION_ITEM;
}

//ai: select ally/self/enemy routing from the action metadata used by gameX.
gX_I1 gX_SceneBattle_Action_TargetRoute(gX_I32 actionType,
    gX_I1 actorIsPlayer, const gX_SceneBattle_SpellCandidate *spell,
    gX_I32 skillTargetType, gX_I1 *outTargetIsPlayer, gX_I1 *outTargetIsSelf) {
    if (outTargetIsPlayer) *outTargetIsPlayer = false;
    if (outTargetIsSelf) *outTargetIsSelf = false;
    if (!outTargetIsPlayer || !outTargetIsSelf) return false;
    switch (actionType) {
        case GX_BATTLE_ACTION_ATTACK:
            *outTargetIsPlayer = !actorIsPlayer;
            return true;
        case GX_BATTLE_ACTION_MAGIC:
            *outTargetIsPlayer = actorIsPlayer
                ? (spell && (spell->isHeal || spell->isSupport))
                : (!spell || (!spell->isHeal && !spell->isSupport));
            return true;
        case GX_BATTLE_ACTION_SKILLS:
            if (skillTargetType == GX_BATTLE_SKILL_TARGET_SELF) {
                *outTargetIsPlayer = actorIsPlayer;
                *outTargetIsSelf = true;
            } else if (skillTargetType == GX_BATTLE_SKILL_TARGET_ALLY
                || skillTargetType == GX_BATTLE_SKILL_TARGET_ALL) {
                *outTargetIsPlayer = true;
            }
            return true;
        case GX_BATTLE_ACTION_ITEM:
            *outTargetIsPlayer = true;
            return true;
        default:
            return false;
    }
}

//ai: choose the lowest-MP-cost positive heal; strict comparison preserves first-match ties.
gX_I32 gX_SceneBattle_Action_FindHealSpell(
    const gX_SceneBattle_SpellCandidate *spells, gX_U32 count) {
    if (!spells || count > INT32_MAX) return -1;
    gX_I32 best = -1;
    gX_F64 bestCost = 999.0;
    for (gX_U32 i = 0; i < count; i++) {
        if (spells[i].isHeal && spells[i].hpChange > 0.0) {
            gX_F64 cost = -spells[i].mpChange;
            if (cost > 0.0 && cost < bestCost) {
                bestCost = cost;
                best = (gX_I32)i;
            }
        }
    }
    return best;
}

//ai: form the ordered elemental damage list, then select by a caller-owned ordinal.
gX_I32 gX_SceneBattle_Action_ChooseDamageSpell(
    const gX_SceneBattle_SpellCandidate *spells, gX_U32 count,
    gX_U32 choiceOrdinal) {
    if (!spells || count > INT32_MAX) return -1;
    const gX_I32 elements[] = {
        GX_BATTLE_ELEMENT_FIRE,
        GX_BATTLE_ELEMENT_ICE,
        GX_BATTLE_ELEMENT_LIGHTNING};
    gX_I32 candidates[3];
    gX_U32 candidateCount = 0;
    for (gX_U32 e = 0; e < sizeof(elements) / sizeof(elements[0]); e++) {
        for (gX_U32 i = 0; i < count; i++) {
            if (spells[i].element == elements[e] && !spells[i].isHeal
                && !spells[i].isSupport && isfinite(spells[i].hpChange)
                && spells[i].hpChange < 0.0) {
                candidates[candidateCount++] = (gX_I32)i;
                break;
            }
        }
    }
    if (candidateCount == 0) return -1;
    return candidates[choiceOrdinal % candidateCount];
}

//ai: match the battle rule that a unit is alive only while HP exceeds its minimum.
gX_I1 gX_SceneBattle_Target_IsAlive(const gX_SceneBattle_TargetVitals *target) {
    return target && isfinite(target->hp) && isfinite(target->minimum)
        && target->hp > target->minimum;
}

//ai: find the earliest living target while making the no-target result explicit.
gX_I1 gX_SceneBattle_Target_FirstAlive(
    const gX_SceneBattle_TargetVitals *targets, gX_U32 count,
    gX_U32 *outIndex) {
    if (outIndex) *outIndex = UINT32_MAX;
    if (!targets || !outIndex) return false;
    for (gX_U32 i = 0; i < count; i++) {
        if (gX_SceneBattle_Target_IsAlive(&targets[i])) {
            *outIndex = i;
            return true;
        }
    }
    return false;
}

//ai: initialize cooldown tuning to the reference action rates and progression constants.
gX_I0 gX_SceneBattle_CooldownTuning_Default(
    gX_SceneBattle_CooldownTuning *tuning) {
    if (!tuning) return;
    tuning->baseAttack = 1.5f;
    tuning->baseMagic = 2.5f;
    tuning->baseSkill = 1.0f;
    tuning->baseItem = 2.0f;
    tuning->proficiencyReduction = 0.15f;
    tuning->minimumDuration = 0.3f;
    tuning->repeatStep = 0.5f;
    tuning->maximumDurationMultiple = 3.0f;
    tuning->disorientationMultiplier = 1.3f;
}

//ai: reserve empty cooldown slots with the same -1 action sentinel as the reference.
gX_I0 gX_SceneBattle_Cooldown_Init(gX_SceneBattle_CooldownState *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    for (gX_U32 i = 0; i < GX_BATTLE_COOLDOWN_TRACKS; i++)
        state->tracks[i].actionType = -1;
}

//ai: report an active cooldown only for a matching action key with finite positive time.
gX_I1 gX_SceneBattle_Cooldown_IsActive(
    const gX_SceneBattle_CooldownState *state, gX_I32 actionType,
    gX_I32 actionIndex) {
    if (!state) return false;
    for (gX_U32 i = 0; i < GX_BATTLE_COOLDOWN_TRACKS; i++) {
        const gX_SceneBattle_CooldownTrack *track = &state->tracks[i];
        if (track->actionType == actionType && track->actionIndex == actionIndex
            && isfinite(track->remaining) && track->remaining > 0.0f) return true;
    }
    return false;
}

//ai: compute base action time with the reference fighting/magic proficiency reductions.
gX_I1 gX_SceneBattle_Cooldown_BaseDuration(
    const gX_SceneBattle_CooldownTuning *tuning, gX_I32 actionType,
    gX_I32 fightingProficiency, gX_I32 magicProficiency,
    gX_F32 *outDuration) {
    if (outDuration) *outDuration = 0.0f;
    if (!outDuration || !gX_SceneBattle_CooldownTuning_Valid(tuning)
        || !gX_SceneBattle_ActionType_Valid(actionType)
        || fightingProficiency < 0 || magicProficiency < 0) return false;
    gX_F32 base = 0.0f;
    if (actionType == GX_BATTLE_ACTION_ATTACK) {
        base = tuning->baseAttack;
        base *= 1.0f - (gX_F32)fightingProficiency * tuning->proficiencyReduction;
    } else if (actionType == GX_BATTLE_ACTION_MAGIC) {
        base = tuning->baseMagic;
        base *= 1.0f - (gX_F32)magicProficiency * tuning->proficiencyReduction;
    } else if (actionType == GX_BATTLE_ACTION_SKILLS) {
        base = tuning->baseSkill;
    } else {
        base = tuning->baseItem;
    }
    if (!isfinite(base)) return false;
    if (base < tuning->minimumDuration) base = tuning->minimumDuration;
    *outDuration = base;
    return true;
}

//ai: set or progressively extend one keyed track, refusing invalid tuning or exhausted storage.
gX_I1 gX_SceneBattle_Cooldown_Set(
    gX_SceneBattle_CooldownState *state,
    const gX_SceneBattle_CooldownTuning *tuning, gX_I32 actionType,
    gX_I32 actionIndex, gX_I32 fightingProficiency,
    gX_I32 magicProficiency, gX_I1 disoriented) {
    if (!state || actionIndex < 0) return false;
    gX_F32 base = 0.0f;
    if (!gX_SceneBattle_Cooldown_BaseDuration(tuning, actionType,
            fightingProficiency, magicProficiency, &base) || base <= 0.0f) return false;
    if (disoriented) base *= tuning->disorientationMultiplier;
    gX_F32 maxDuration = base * tuning->maximumDurationMultiple;
    if (!isfinite(base) || !isfinite(maxDuration)) return false;

    gX_I32 trackIndex = -1;
    gX_I32 emptyIndex = -1;
    for (gX_I32 i = 0; i < GX_BATTLE_COOLDOWN_TRACKS; i++) {
        gX_SceneBattle_CooldownTrack *track = &state->tracks[i];
        if (track->actionType < 0) {
            if (emptyIndex < 0) emptyIndex = i;
        } else if (track->actionType == actionType
            && track->actionIndex == actionIndex
            && isfinite(track->remaining) && track->remaining > 0.0f) {
            trackIndex = i;
            break;
        }
    }
    gX_I1 extending = trackIndex >= 0;
    if (trackIndex < 0) trackIndex = emptyIndex;
    if (trackIndex < 0) return false;

    gX_SceneBattle_CooldownTrack *track = &state->tracks[trackIndex];
    if (extending) {
        if (track->repeatCount < INT32_MAX) track->repeatCount++;
        gX_F32 added = base * (1.0f + (gX_F32)track->repeatCount * tuning->repeatStep);
        gX_F32 remaining = track->remaining + added;
        track->remaining = !isfinite(remaining) || remaining > maxDuration
            ? maxDuration : remaining;
    } else {
        track->actionType = actionType;
        track->actionIndex = actionIndex;
        track->remaining = base;
        track->baseDuration = base;
        track->repeatCount = 0;
    }
    return true;
}

//ai: decay all finite timers using scaled battle time and clear tracks at expiry.
gX_I0 gX_SceneBattle_Cooldown_Update(
    gX_SceneBattle_CooldownState *state, gX_F32 elapsedSeconds,
    gX_F32 speedMultiplier) {
    if (!state || !isfinite(elapsedSeconds) || elapsedSeconds < 0.0f
        || !isfinite(speedMultiplier) || speedMultiplier < 0.0f) return;
    gX_F32 delta = elapsedSeconds * speedMultiplier;
    if (!isfinite(delta)) return;
    for (gX_U32 i = 0; i < GX_BATTLE_COOLDOWN_TRACKS; i++) {
        gX_SceneBattle_CooldownTrack *track = &state->tracks[i];
        if (!isfinite(track->remaining)) {
            track->actionType = -1;
            track->remaining = 0.0f;
            track->repeatCount = 0;
        } else if (track->remaining > 0.0f) {
            track->remaining -= delta;
            if (track->remaining <= 0.0f) {
                track->actionType = -1;
                track->remaining = 0.0f;
                track->repeatCount = 0;
            }
        }
    }
}
