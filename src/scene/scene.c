/**********************************************************************************************
*
*   [gX: src/scene/scene.c]
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


#include "scene.h"

#include "../common/log.h"
#include "../common/config.h"
#include "../common/enum.h"
#include "../main.h"

gX_Scene scene;

gX_I32 gX_Scene_Load(gX_I0) {
    gX_I32 errors = 0;

    strcpy(scene.name,"Scene");
    strcpy(scene.description,"Scene dispatcher");

    gX_SceneTitle_Load(&(scene.title));
    gX_SceneBattle_Load(&(scene.battle));
    gX_ScenePhysics_Load(&(scene.physicsScene));
    gX_SceneSynthVis_Load(&(scene.synthvis));

    scene.masterIndexSwitchResponse = false;
    scene.loaded = true;

    gX_TraceLog(gX_LOG_INFO, "gX_Scene: Loaded");
    return errors;
}

gX_I32 gX_Scene_Unload(gX_I0) {
    gX_I32 errors = 0;

    gX_SceneTitle_Unload(&(scene.title));
    gX_SceneBattle_Unload(&(scene.battle));
    gX_ScenePhysics_Unload(&(scene.physicsScene));
    gX_SceneSynthVis_Unload(&(scene.synthvis));

    scene.loaded = false;
    //opencode: run header - unload was silent; switches need both ends.
    gX_TraceLog(gX_LOG_NOTICE, "gX_Scene: Unloaded");
    return errors;
}

static gX_I0 gX_Scene_Clear_UpdateDraw(gX_I0) {
    scene.state.UPDATE_TITLE = false;
    scene.state.DRAW_TITLE = false;
    scene.state.UPDATE_BATTLE = false;
    scene.state.DRAW_BATTLE = false;
    scene.state.UPDATE_PHYSICS = false;
    scene.state.DRAW_PHYSICS = false;
    scene.state.UPDATE_SYNTHVIS = false;
    scene.state.DRAW_SYNTHVIS = false;
}

//ai: previous masterIndex of the last response — the scene-switch exit hook
//   for content living in shared modules (physics props/ragdolls are
//   destroyed and their step hook detached when the sandbox is left).
static gX_I32 g_Scene_LastResponded = -1;

static gX_I0 gX_Scene_Respond(gX_I0) {
    scene.masterIndexSwitchResponse = false;
    gX_Scene_Clear_UpdateDraw();

    if (g_Scene_LastResponded == gX_SCENE_PHYSICS
        && maindata.masterIndex != gX_SCENE_PHYSICS) {
        gX_ScenePhysics_Park(&(scene.physicsScene));
    }
    //opencode: leaving the synth: stop its notes/key state (an autoplay arp
    //   would otherwise keep sounding off-scene).
    if (g_Scene_LastResponded == gX_SCENE_SYNTHVIS
        && maindata.masterIndex != gX_SCENE_SYNTHVIS) {
        gX_SceneSynthVis_Park(&(scene.synthvis));
    }
    g_Scene_LastResponded = maindata.masterIndex;

    switch (maindata.masterIndex) {
        case gX_SCENE_TITLE: {
            scene.state.UPDATE_TITLE = true;
            scene.state.DRAW_TITLE = true;
            scene.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_BATTLE: {
            scene.state.UPDATE_BATTLE = true;
            scene.state.DRAW_BATTLE = true;
            scene.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_PHYSICS: {
            scene.state.UPDATE_PHYSICS = true;
            scene.state.DRAW_PHYSICS = true;
            scene.masterIndexSwitchResponse = true;
            break;
        }
        //opencode: synth visualizer — the scene owns the keyboard input and the
        //   canvas composite (renderer modules own the visualizers).
        case gX_SCENE_SYNTHVIS: {
            scene.state.UPDATE_SYNTHVIS = true;
            scene.state.DRAW_SYNTHVIS = true;
            scene.masterIndexSwitchResponse = true;
            break;
        }
        //ai: module-owned scene — a loaded module supplies the full frame;
        //   the engine dispatcher intentionally owns no state or content.
        case gX_SCENE_MODULE: {
            scene.masterIndexSwitchResponse = true;
            break;
        }
        //opencode: renderer scenes respond without scene-level state (the
        //   renderer module dispatch owns update/draw for those)
        case gX_SCENE_RENDERER_ANALOG:
        case gX_SCENE_RENDERER_BACKGROUND:
        case gX_SCENE_RENDERER_CLOCK:
        case gX_SCENE_RENDERER_DEMOSCRIPT:
        case gX_SCENE_RENDERER_FRACTAL:
        case gX_SCENE_RENDERER_GENERATE:
        case gX_SCENE_RENDERER_GFX:
        case gX_SCENE_RENDERER_GLOBE:
        case gX_SCENE_RENDERER_LED:
        case gX_SCENE_RENDERER_PP:
        case gX_SCENE_RENDERER_RAYMARCH:
        case gX_SCENE_RENDERER_SKY:
        case gX_SCENE_RENDERER_TELETEXT:
        case gX_SCENE_RENDERER_TEXTWRITER:
        case gX_SCENE_RENDERER_WATER:
        case gX_SCENE_RENDERER_WIREFRAME: {
            scene.masterIndexSwitchResponse = true;
            break;
        }
        default: break;
    }

    //opencode: run header - switches are rare, so they log at NOTICE
    //   (was DEBUG: scene changes were invisible in default logs).
    gX_TraceLog(gX_LOG_NOTICE, "gX_Scene: switched to masterIndex %i [%s]",
        maindata.masterIndex, gX_Scenes_String(maindata.masterIndex));
}

gX_I0 gX_Scene_Update(gX_I0) {
    if (maindata.masterIndexSwitched) {
        gX_Scene_Respond();
    }

    if (scene.state.UPDATE_TITLE) gX_SceneTitle_Update(&(scene.title));
    if (scene.state.UPDATE_BATTLE) gX_SceneBattle_Update(&(scene.battle));
    if (scene.state.UPDATE_PHYSICS) gX_ScenePhysics_Update(&(scene.physicsScene));
    if (scene.state.UPDATE_SYNTHVIS) gX_SceneSynthVis_Update(&(scene.synthvis));
}

gX_I0 gX_Scene_Draw(gX_I0) {
    if (scene.state.DRAW_TITLE) gX_SceneTitle_Draw(&(scene.title));
    if (scene.state.DRAW_SYNTHVIS) gX_SceneSynthVis_Draw(&(scene.synthvis));
}

//opencode: scene overlays run after the renderer has composited the backdrop.
gX_I0 gX_Scene_Draw_Overlay(gX_I0) {
    if (scene.state.DRAW_BATTLE) gX_SceneBattle_Draw(&(scene.battle));
    //ai: physics HUD joins the overlay so it draws over the renderer frame.
    if (scene.state.DRAW_PHYSICS) gX_ScenePhysics_Draw(&(scene.physicsScene));
}
