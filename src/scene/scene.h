/**********************************************************************************************
*
*   [gX: src/scene/scene.h]
*   Scene dispatcher — masterIndex, lifecycle, state flags
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


#ifndef GX_SCENE_H
#define GX_SCENE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

#include "title.h"
#include "battle.h"
#include "physics.h"
#include "synthvis.h"

typedef struct gX_Scene_State {
    gX_I1 UPDATE_TITLE;
    gX_I1 DRAW_TITLE;
    gX_I1 UPDATE_BATTLE;
    gX_I1 DRAW_BATTLE;
    gX_I1 UPDATE_PHYSICS;
    gX_I1 DRAW_PHYSICS;
    gX_I1 UPDATE_SYNTHVIS;
    gX_I1 DRAW_SYNTHVIS;
} gX_Scene_State;

typedef struct gX_Scene {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_I1 loaded;
    gX_Scene_State state;
    gX_I1 masterIndexSwitchResponse;

    gX_SceneTitle title;
    gX_SceneBattle battle;
    gX_ScenePhysics physicsScene;
    gX_SceneSynthVis synthvis;
} gX_Scene;

extern gX_Scene scene;

gX_I32 gX_Scene_Load(gX_I0);
gX_I32 gX_Scene_Unload(gX_I0);
gX_I0  gX_Scene_Update(gX_I0);
gX_I0  gX_Scene_Draw(gX_I0);
gX_I0  gX_Scene_Draw_Overlay(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //GX_SCENE_H
