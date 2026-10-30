/**********************************************************************************************
*
*   [gX: src/scene/battle.h]
*   Native battle presentation — 3D arena and HUD
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


#ifndef GX_SCENE_BATTLE_H
#define GX_SCENE_BATTLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "battle_runtime.h"

//ai: reserve presentation models for all party members plus every enemy slot.
#define GX_BATTLE_GFX_ACTOR_COUNT (GX_BATTLE_DEMO_MAX_ENEMIES + GX_BATTLE_DEMO_PARTY_SIZE)
#define GX_BATTLE_GFX_PART_COUNT 4U

typedef struct gX_SceneBattle_State {
    gX_F64 elapsed;
    gX_F64 previousElapsed;
    gX_F64 actionConfirmedUntil;
    gX_I32 selectedAction;
    gX_U8 backdropWave;
    gX_I32 gfxActorModels[GX_BATTLE_GFX_ACTOR_COUNT][GX_BATTLE_GFX_PART_COUNT];
    gX_I32 gfxFacingModels[GX_BATTLE_GFX_ACTOR_COUNT];
    //ai: renderer-owned transient impact ripple model index.
    gX_I32 gfxImpactRippleModel;
    //ai: renderer-owned queued-intent target-cell model index.
    gX_I32 gfxIntentCellModel;
    gX_SceneBattle_Runtime runtime;
} gX_SceneBattle_State;

typedef struct gX_SceneBattle {
    gX_I8 name[128];
    gX_I8 description[256];
    gX_I1 loaded;
    gX_SceneBattle_State state;
} gX_SceneBattle;

gX_I32 gX_SceneBattle_Load(gX_SceneBattle *battle);
gX_I32 gX_SceneBattle_Unload(gX_SceneBattle *battle);
gX_I32 gX_SceneBattle_Update(gX_SceneBattle *battle);
gX_I32 gX_SceneBattle_Draw(gX_SceneBattle *battle);

#ifdef __cplusplus
}
#endif

#endif //GX_SCENE_BATTLE_H
