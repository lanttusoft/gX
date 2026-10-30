/**********************************************************************************************
*
*   [gX: src/gui/respond.c]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu
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


#include "gui.h"
#include "binding.h"
#include "markup.h"

#include <stdlib.h>

#include "../main.h"

#include "../common/log.h"

//opencode: replaced monolithic switch with data-driven lookup tables (sceneTemplates, transitionSetupScenes)

typedef struct {
    gX_I32 sceneIndex;
    gX_I32 templateID;
    gX_I32 canvasIndex;
} gX_GUI_SceneTemplate;

static const gX_GUI_SceneTemplate sceneTemplates[] = {
    //opencode: v0.1.41 — real title through the GUI builder (canvas 0);
    //   the wrapper-draw placeholder in scene/title.c is gone.
    { gX_SCENE_TITLE,              gX_GUI_TEMPLATE_TITLEGX,       0 },
    //{ gX_SCENE_TITLE,              gX_GUI_TEMPLATE_LENS,          2 }, //for testing gui shader effects and debugging
    //{ gX_SCENE_TITLE,              gX_GUI_TEMPLATE_BLUR,          2 }, //opencode: needs canvas buffer compositing for GUI-drawn content
    /* SYNTHVIS canvases are managed directly by the scene — see gX_SceneSynthVis_Load */
    //opencode: battle HUD/Menu is drawn entirely by gX_SceneBattle_Draw in scene/battle.c.
    //   GUI canvas templates are disabled here to avoid double UI rendering.
    //   Canvas 0 (scene bg) is also handled by the battle Draw — no template needed.
    { gX_SCENE_RENDERER_BACKGROUND, gX_GUI_TEMPLATE_SCENE,        0 },
    //{ gX_SCENE_RENDERER_BACKGROUND, gX_GUI_TEMPLATE_BLUR,         2 }, //opencode: blur now toggled with escape menu (gui.c Load + update.c)
    { gX_SCENE_RENDERER_DEMOSCRIPT,gX_GUI_TEMPLATE_SCENE,         0 },
    { gX_SCENE_RENDERER_FRACTAL,   gX_GUI_TEMPLATE_SCENE,         0 },
    { gX_SCENE_RENDERER_GENERATE,  gX_GUI_TEMPLATE_SCENE,         0 },
    //{ gX_SCENE_RENDERER_FRACTAL,   gX_GUI_TEMPLATE_BLUR,          2 },
    { gX_SCENE_RENDERER_GFX_DEMO,  gX_GUI_TEMPLATE_SCENE,         0 },
    { gX_SCENE_RENDERER_PP,        gX_GUI_TEMPLATE_SCENE,         0 },
    { gX_SCENE_RENDERER_RAYMARCH,  gX_GUI_TEMPLATE_SCENE,         0 },
    //{ gX_SCENE_RENDERER_RAYMARCH,  gX_GUI_TEMPLATE_BLUR,          2 },
    { gX_SCENE_RENDERER_GLOBE,     gX_GUI_TEMPLATE_SCENE,         0 },
    //{ gX_SCENE_RENDERER_GLOBE,     gX_GUI_TEMPLATE_BLUR,          2 },
    { gX_SCENE_RENDERER_TEXTWRITER,gX_GUI_TEMPLATE_SCENE,         0 },
    { gX_SCENE_RENDERER_ANALOG,    gX_GUI_TEMPLATE_SCENE,         0 },
    { gX_SCENE_RENDERER_CLOCK,     gX_GUI_TEMPLATE_SCENE,         0 },
    { gX_SCENE_RENDERER_SKY,       gX_GUI_TEMPLATE_SCENE,         0 },
    { gX_SCENE_RENDERER_TELETEXT,  gX_GUI_TEMPLATE_SCENE,         0 },
    { gX_SCENE_RENDERER_VIDEO,    gX_GUI_TEMPLATE_SCENE,         0 },
    { gX_SCENE_RENDERER_WATER,     gX_GUI_TEMPLATE_SCENE,         0 },
    { gX_SCENE_RENDERER_WIREFRAME, gX_GUI_TEMPLATE_SCENE,         0 },
    //{ gX_SCENE_RENDERER_TEXTWRITER,gX_GUI_TEMPLATE_BLUR,          2 },
    { gX_SCENE_ROOMS,              gX_GUI_TEMPLATE_SCENE,         0 },
    //opencode: markup loader demo — built into canvas 1 (also dispatched for
    //   the gui-test scene; pixel-verified by the gui harness). Skipped on
    //   GLFW: its direct-draw mode (no canvas buffers) composites the
    //   full-screen markup panel over the GuiTester canvas, breaking the
    //   screen-read checkpoints.
    //opencode: v0.4.0 — gX Tools suite: the GUIPLAYGROUND scene is driven by
    //   the markup template (registered by --tools). ID 1000 is a dynamic
    //   id (not in the enum); the row is inert unless --tools registered it.
    { gX_SCENE_GUIPLAYGROUND,      GX_GUI_TEMPLATE_MARKUP,         0 },
    { -1, 0, 0 }
};

static const gX_I32 transitionSetupScenes[] = {
    gX_SCENE_TITLE,
    -1
};

//opencode: renderer/GUI seam for the phase-1 four-view dashboard. The GFX
//   renderer produces tiles during Draw; this template owns their later GUI
//   composition, preserving the normal single-canvas path when disabled.
static gX_I1 gX_GUI_Respond_UseViewportGrid(gX_I0) {
    const gX_I8 *grid = getenv("GX_GFX_GRID");
    return (maindata.masterIndex == gX_SCENE_RENDERER_GFX && arguments.showcase
        && !arguments.disableModuleGUI && grid && atoi(grid) != 0) ? 1 : 0;
}

static gX_I0 gX_GUI_Respond_IsTransitionScene(gX_I32 sceneIndex) {
    for (gX_U32 i = 0; transitionSetupScenes[i] >= 0; i++) {
        if (transitionSetupScenes[i] == sceneIndex) {
            //opencode: v0.4.0 — first launch: short color fade from black
            //   (INITIAL_LOAD set in gX_Load before gX_GUI_Load; the old
            //   gate was dead — never set anywhere, so the swing always
            //   played on startup). Re-entries keep the (tamed) skew-slide.
            if (gui.state.INITIAL_LOAD) {
                gX_GUI_Transition_FadeFromBlack(0.25f);
                return;
            }
            //ai: avoid the skew-slide swing that reads like a pendulum when
            //   returning from a demo; keep title re-entry to a quiet fade.
            gX_GUI_Transition_Fade(0.35f, 0.15f, 0.35f, 0, 0);
            return;
        }
    }
}

gX_I0 gX_GUI_Respond(gX_I0);
gX_I0 gX_GUI_Respond(gX_I0) {
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_TraceLog_logtypeLevel, "GUI_Respond","Responding")
    gX_GUI_Trace_Event("respond", maindata.masterIndex, -1, NULL);
    gui.masterIndexSwitchResponse = false;
    gX_TraceLog_GUI(gX_LOG_DEBUG,"maindata.masterIndex: %i",maindata.masterIndex);
    gX_GUI_Transition_Clear();
    
    gui.clearAll = true;
    
    if (gX_GUI_Respond_UseViewportGrid()) {
        gX_GUI_AddMake(gX_GUI_TEMPLATE_VIEWPORTGRID, 0);
    } else {
        for (gX_U32 i = 0; sceneTemplates[i].sceneIndex >= 0; i++) {
            if (sceneTemplates[i].sceneIndex == maindata.masterIndex) {
                gX_GUI_AddMake(sceneTemplates[i].templateID, sceneTemplates[i].canvasIndex);
            }
        }
    }
    //opencode: v0.1.40 — audit template rides every respond so clearAll can't
    //   permanently erase it (one-shot builds die to the post-switch wipe)
    if (gui.auditTemplateID > 0) {
        gX_GUI_AddMake(gui.auditTemplateID, 2);
    }
    
    gX_GUI_Respond_IsTransitionScene(maindata.masterIndex);
    //opencode: clear the initial-load flag on the first Respond for any scene,
    //   so that transitions are suppressed only on program startup. For scenes
    //   that call Transition_Setup (e.g. title), Setup handles this internally;
    //   for non-title scenes, this fallback ensures it's still cleared.
    gui.state.INITIAL_LOAD = false;
    
    gui.masterIndexSwitchResponse = true;
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_TraceLog_logtypeLevel, "Responded")
}
