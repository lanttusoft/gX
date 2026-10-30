/**********************************************************************************************
*
*   [gX: src/gui/templates/template_registry.c]
*   gX — a game engine
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


#include "template_registry.h"

#include "../../common/log.h"

#include "../enum.h"

//opencode: template registry — all template function declarations in one place,
//registered at init via gX_GUI_Template_InitRegistry().

//=============================================================================
// extern declarations for all template functions
//=============================================================================

extern gX_I0 gX_GUI_Template_Example(gX_I32 canvasIndex);
extern gX_I0 gX_GUI_Template_Demo(gX_I32 canvasIndex);
extern gX_I0 gX_GUI_Template_Demo2(gX_I32 canvasIndex);
extern gX_I0 gX_GUI_Template_Scene(gX_I32 canvasIndex);
extern gX_I0 gX_GUI_Template_ViewportGrid(gX_I32 canvasIndex);
extern gX_I0 gX_GUI_Template_EscapeMenu(gX_I32 canvasIndex);
extern gX_I0 gX_GUI_Template_Message(gX_I32 canvasIndex);
extern gX_I0 gX_GUI_Template_DebugDisplay(gX_I32 canvasIndex);

//=============================================================================
// registry storage
//=============================================================================

gX_GUI_TemplateEntry gX_guiTemplateRegistry[GX_GUI_TEMPLATE_REGISTRY_MAX];
gX_U32 gX_guiTemplateCount = 0;

gX_I0 gX_GUI_Template_Register(gX_I32 id, const gX_I8 *name, gX_GUI_TemplateFunc func) {
    if (gX_guiTemplateCount >= GX_GUI_TEMPLATE_REGISTRY_MAX) {
        gX_TraceLog(gX_LOG_WARNING, "GUI_Template_Register: registry full (%s)", name);
        return;
    }
    gX_U32 idx = gX_guiTemplateCount++;
    gX_guiTemplateRegistry[idx].id   = id;
    gX_guiTemplateRegistry[idx].name = name;
    gX_guiTemplateRegistry[idx].func = func;
}

gX_I32 gX_GUI_Template_Dispatch(gX_I32 id, gX_I32 canvasIndex) {
    for (gX_U32 i = 0; i < gX_guiTemplateCount; i++) {
        if (gX_guiTemplateRegistry[i].id == id) {
            gX_guiTemplateRegistry[i].func(canvasIndex);
            return 0;
        }
    }
    gX_TraceLog(gX_LOG_WARNING, "GUI_Template_Dispatch: template id %i not found", id);
    return 1;
}

extern gX_I0 gX_GUI_Template_Dialog(gX_I32 canvasIndex);
extern gX_I0 gX_GUI_Template_SDFT(gX_I32 canvasIndex);
extern gX_I0 gX_GUI_Template_TitleGX(gX_I32 canvasIndex);
extern gX_I0 gX_GUI_Template_Settings(gX_I32 canvasIndex);
extern gX_I0 gX_GUI_Template_GuiTester(gX_I32 canvasIndex);
extern gX_I0 gX_GUI_Template_GuiEditor(gX_I32 canvasIndex);
extern gX_I0 gX_GUI_Template_ThemLab(gX_I32 canvasIndex);
extern gX_I0 gX_GUI_Template_MaterialsLab(gX_I32 canvasIndex);
extern gX_I0 gX_GUI_Template_ShaderLab(gX_I32 canvasIndex);
#if gX_ENABLE_RENDERER == 1
//opencode: scene-gated — the synthvis keyboard template reads scene.synthvis.
extern gX_I0 gX_GUI_Template_SynthVis_Keyboard(gX_I32 canvasIndex);
#endif

gX_I0 gX_GUI_Template_InitRegistry(gX_I0) {
    gX_TraceLog(gX_LOG_DEBUG, "GUI_Template_InitRegistry: registering templates");

    gX_GUI_Template_Register(gX_GUI_TEMPLATE_EXAMPLE,       "Example",       gX_GUI_Template_Example);
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_DEMO,          "Demo",          gX_GUI_Template_Demo);
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_DEMO2,         "Demo2",         gX_GUI_Template_Demo2);
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_SCENE,         "Scene",         gX_GUI_Template_Scene);
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_VIEWPORTGRID,  "ViewportGrid",  gX_GUI_Template_ViewportGrid);
    //opencode: gX minimal overlays — game menus dropped, these keep the
    //   canvas system fully wired (pause + debug canvases build at Load)
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_ESCAPEMENU,    "EscapeMenu",    gX_GUI_Template_EscapeMenu);
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_MESSAGE,       "Message",       gX_GUI_Template_Message);
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_DIALOG,        "Dialog",        gX_GUI_Template_Dialog);
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_DEBUGDISPLAY,  "DebugDisplay",  gX_GUI_Template_DebugDisplay);
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_SETTINGS,      "Settings",      gX_GUI_Template_Settings);
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_TITLEGX,       "TitleGX",       gX_GUI_Template_TitleGX);
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_SDFT,          "SDFT",          gX_GUI_Template_SDFT);
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_GUITESTER,     "GuiTester",     gX_GUI_Template_GuiTester);
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_GUIEDITOR,     "GuiEditor",     gX_GUI_Template_GuiEditor);
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_THEMLAB,       "ThemLab",       gX_GUI_Template_ThemLab);
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_MATERIALSLAB,  "MaterialsLab",  gX_GUI_Template_MaterialsLab);
    //opencode: shader experiment gallery — translucency, lens, color math.
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_SHADERLAB,     "ShaderLab",     gX_GUI_Template_ShaderLab);
#if gX_ENABLE_RENDERER == 1
    //opencode: synthvis scene keyboard — playable key bed over the visualizer canvas.
    gX_GUI_Template_Register(gX_GUI_TEMPLATE_SYNTHVIS_KEYBOARD, "SynthVisKeyboard", gX_GUI_Template_SynthVis_Keyboard);
#endif

    gX_TraceLog(gX_LOG_DEBUG, "GUI_Template_InitRegistry: %i templates registered", gX_guiTemplateCount);
}
