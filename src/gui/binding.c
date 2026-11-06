/**********************************************************************************************
*
*   [gX: src/gui/binding.c]
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


#include <stdio.h>
#include <string.h>

#include "binding.h"
#include "gui.h"
#include "canvas.h"
#include "element/element.h"
#include "../common/log.h"
#include "../common/enum.h"
#include "enum.h"
#if gX_ENABLE_SCENE == 1
    #include "../scene/scene.h"
#endif
#if gX_ENABLE_GAME == 1
    #include "../game/save.h"
#else
    //opencode: gX — game content dropped; save widget uses the fallback toast
    //   (GX_SAVE_PATH fallback now shared via common/config.h)
#endif
#include "../audio.h"
#include "../input.h"
#include "../settings.h"
#include "console_state.h"
#include "console_playback.h"

extern gX_I32 gX_MainData_Switch(gX_I32 masterIndex);
extern gX_I0 gX_Renderer_GFX_ColdBoot(gX_I0);
extern gX_I0 gX_RendererRaymarch_Cartridge_Apply(gX_I0);
extern gX_MainArguments arguments;
extern gX_GUI gui;

//=============================================================================
// New callback-based OnClick — clean, minimal, works
//=============================================================================

//opencode: v0.4.0 — direct callback pointers (B6 audit): the old scheme
//   stored callback+arg in a 256-slot pool and wrote a dispatch stub +
//   slot index into the element's data module. A pool reset (refresh) could
//   invalidate indices mid-frame; the slot cap silently dropped bindings.
//   Now the callback and its arg live DIRECTLY in the data module
//   (funcPtr/funcPtrArg) — no pool, no index, no reset coupling. The
//   element data module already carried these fields for shader wires.

//opencode: v0.6.x — resolve (creating on demand) the isFunctionPtr data
//   module that carries a click binding. Shared by gX_GUI_OnClick and
//   gX_GUI_OnClick_Context so the two APIs cannot diverge.
static gX_GUI_Element_Data *gX_GUI_OnClick_ResolveData(gX_GUI_Element *element) {
    gX_GUI_Element_Data *data = NULL;
    if (!element) return NULL;

    //opencode: reuse an existing isFunctionPtr data module instead of
    //   clobbering data[0] — data[0] may hold a widget's potentiometer or
    //   operator binding (e.g. Slider), and overwriting funcPtr would kill it.
    if (element->datas.amount > 0 && element->datas.data) {
        gX_GUI_Element_Data *datas = element->datas.data;
        for (gX_U32 i = 0; i < element->datas.amount; i++) {
            if (datas[i].isFunctionPtr) { data = &datas[i]; break; }
        }
    }
    if (!data) {
        //opencode: Add_Data reuses the first heap hole. amount-1 identifies
        //   the wrong module after a removal and could overwrite an unrelated
        //   callback/operator binding, so capture the insertion slot first.
        gX_U32 addIndex = element->datas.amount;
        if (element->datas.meta) {
            for (gX_U32 i = 0; i < element->datas.amount; i++) {
                if (!element->datas.meta[i].USED) {
                    addIndex = i;
                    break;
                }
            }
        }
        if (gX_GUI_Element_Add_Data(element) == 0 &&
            element->datas.data && addIndex < element->datas.amount &&
            element->datas.meta && element->datas.meta[addIndex].USED) {
            data = &((gX_GUI_Element_Data*)element->datas.data)[addIndex];
        }
    }
    return data;
}

gX_I0 gX_GUI_OnClick(gX_GUI_Element *element, gX_GUI_ClickCallback callback, gX_I0 *userData) {
    gX_GUI_Element_Data *data = gX_GUI_OnClick_ResolveData(element);
    if (!data || !callback) return;

    data->isFunctionPtr = true;
    data->usesCallbackContext = false;
    data->callbackReceivesOwnerData = false;
    data->funcPtr = callback;
    data->funcPtrArg = userData;
    data->execute.clickedLeft = true;
}

//opencode: v0.6.x — owner-scoped callback context (GUI audit finding 4). The
//   packed arguments are copied into the element's own data module so a
//   partial canvas refresh (which resets no global state any more) cannot make
//   a click mutate another widget's pointers or state.
gX_I1 gX_GUI_OnClick_Context(gX_GUI_Element *element,
    gX_GUI_ClickCallback callback, const gX_I0 *context, gX_U32 contextSize) {
    //opencode: reject invalid payloads instead of silently truncating a
    //   callback context that its callback would interpret incorrectly.
    gX_GUI_Element_Data *data = NULL;
    if (!callback) return false;
    if ((contextSize > 0 && !context) || contextSize > gX_GUI_ELEMENT_CALLBACK_CTX_MAX) return false;
    data = gX_GUI_OnClick_ResolveData(element);
    if (!data) return false;
    memset(data->callbackCtx.bytes, 0, sizeof(data->callbackCtx.bytes));
    if (contextSize > 0) {
        memcpy(data->callbackCtx.bytes, context, contextSize);
    }
    data->isFunctionPtr = true;
    data->usesCallbackContext = true;
    data->callbackReceivesOwnerData = false;
    data->funcPtr = callback;
    data->funcPtrArg = NULL;
    data->execute.clickedLeft = true;
    return true;
}

//=============================================================================
// Reusable callbacks for common patterns
//=============================================================================

//opencode: v0.6.x — gX_Widget_PackArgs / g_argsPool were removed. Widget
//   builders now copy their arguments into the owning element's data module
//   via gX_GUI_OnClick_Context, so no global fixed pool exists to alias across
//   partial canvas refreshes (GUI audit finding 4).

gX_I0 gX_Widget_OnClick_CallSwitch(gX_I0 *arg) {
    gX_I32 sceneIndex = (gX_I32)(gX_U64)(uintptr_t)arg;
    gX_MainData_Switch(sceneIndex);
}

//opencode: close escape menu — hide menu, clear focus
//opencode: v0.4.1 — dismiss the message toast (X button on the Message
//   template; mirrors the update.c click-anywhere dismiss)
gX_I0 gX_Widget_OnClick_DismissToast(gX_I0 *arg) {
    (void)arg;
    gui.state.SHOW_MSG = false;
    gui.state.toastMessage[0] = '\0';
}

//ai: cartridge Resume action uses this shared close callback and trace.
gX_I0 gX_Widget_OnClick_CloseEscapeMenu(gX_I0 *arg) {
    (void)arg;
    gui.state.SHOW_MENU = false;
    gX_GUI_Focus_Clear();
    if (arguments.cartridgeY2K || arguments.cartridgeRay1
        || arguments.cartridgeParticle
        || (arguments.consoleShell && maindata.masterIndex != gX_SCENE_TITLE))
        gX_TraceLog(gX_LOG_NOTICE, "CONSOLE_MENU: resume");
}

//opencode: open settings — rebuild menu canvas with settings template
gX_I0 gX_Widget_OnClick_OpenSettings(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Focus_Clear();
    settings.bindView = false;
    input.bindCaptureActive = false;
#if gX_ENABLE_SCENE == 1
    //opencode: title settings must use the title canvas; the pause-menu canvas
    //   is hidden whenever SHOW_MENU is false, so the old route was a no-op.
    if (maindata.masterIndex == gX_SCENE_TITLE) {
        gX_GUI_Template_TitleGX_SetView(4);
        gX_GUI_Canvas *canvas = gX_Heap_SeekPtr_Index(&gui.canvases, 0);
        if (canvas) canvas->refresh = true;
        return;
    }
#endif
    gX_GUI_AddMake(gX_GUI_TEMPLATE_SETTINGS, gui.menuCanvasIndex);
    if (gui.menuCanvas) gui.menuCanvas->refresh = true;
    gui.focusFirstPending = true;
}

//opencode: settings sub-view refresh stays valid for both title and pause-menu canvases.
gX_I0 gX_GUI_Settings_Refresh(gX_I0) {
    gX_GUI_Canvas *canvas = NULL;
#if gX_ENABLE_SCENE == 1
    if (maindata.masterIndex == gX_SCENE_TITLE) canvas = gX_GUI_Canvas_FromIndex(0);
    else canvas = gui.menuCanvas;
#else
    canvas = gui.menuCanvas;
#endif
    if (canvas) canvas->refresh = true;
    for (gX_U32 i = 0; i < gui.canvases.amount; i++) {
        canvas = gX_GUI_Canvas_FromIndex((gX_I32)i);
        if (canvas && canvas->templateID == gX_GUI_TEMPLATE_SETTINGS) canvas->refresh = true;
    }
    gui.focusFirstPending = true;
}

//opencode: route keyboard and gamepad Back through the active menu hierarchy.
gX_I1 gX_GUI_Handle_Back(gX_I0)
{
#if gX_ENABLE_SCENE == 1
    if (maindata.masterIndex == gX_SCENE_TITLE) {
        if (settings.bindView) {
            gX_Widget_OnClick_BackToSettings(NULL);
            return true;
        }
        return gX_GUI_Template_TitleGX_Back();
    }
#endif
    if (!gui.state.SHOW_MENU) return false;
    if (settings.bindView) {
        gX_Widget_OnClick_BackToSettings(NULL);
        return true;
    }
    if (gui.menuCanvas && gui.menuCanvas->templateID == gX_GUI_TEMPLATE_SETTINGS) {
        gX_Widget_OnClick_BackToEscapeMenu(NULL);
        return true;
    }
    return false;
}

gX_I0 gX_Widget_OnClick_OpenKeyBindings(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Focus_Clear();
    input.bindCaptureActive = false;
    settings.bindView = true;
    gX_GUI_Settings_Refresh();
}

gX_I0 gX_Widget_OnClick_BackToSettings(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Focus_Clear();
    input.bindCaptureActive = false;
    settings.bindView = false;
    gX_GUI_Settings_Refresh();
}

gX_I0 gX_Widget_OnClick_CaptureBind(gX_I0 *arg) {
    gX_GUI_BindCaptureContext *context = (gX_GUI_BindCaptureContext*)arg;
    if (!context || context->bindIndex >= gX_BIND_COMMON_AMOUNT || context->keySlot >= 2) return;
    input.bindCaptureIndex = context->bindIndex;
    input.bindCaptureSlot = context->keySlot;
    input.bindCaptureActive = true;
    gX_GUI_Focus_Clear();
    gX_GUI_Settings_Refresh();
}

//opencode: back to escape menu from settings
gX_I0 gX_Widget_OnClick_BackToEscapeMenu(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Focus_Clear();
    settings.bindView = false;
    input.bindCaptureActive = false;
#if gX_ENABLE_SCENE == 1
    //opencode: Settings opened from the title returns to the title canvas,
    //   while the same button from pause still returns to EscapeMenu.
    if (maindata.masterIndex == gX_SCENE_TITLE) {
        gX_GUI_Template_TitleGX_SetView(0);
        gX_GUI_Canvas *canvas = gX_Heap_SeekPtr_Index(&gui.canvases, 0);
        if (canvas) canvas->refresh = true;
        return;
    }
#endif
    if (settings.dirtyFlag) {
        strncpy(gui.state.toastMessage, "Unsaved changes — click Save to keep them", sizeof(gui.state.toastMessage) - 1);
        gui.state.SHOW_MSG = true;
    }
    gX_GUI_AddMake(gX_GUI_TEMPLATE_ESCAPEMENU, gui.menuCanvasIndex);
    if (gui.menuCanvas) gui.menuCanvas->refresh = true;
    gui.focusFirstPending = true;
}

//opencode: v0.2.0-pre4 — title sub-view navigation. LOAD GAME swaps the title
//   canvas build to the save-slot view; BACK returns to the main menu view.
//   The canvas index matches the respond table (SCENE_TITLE builds on 0).
gX_I0 gX_Widget_OnClick_TitleSaves(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Focus_Clear();
    gX_GUI_Template_TitleGX_SetView(1);
    gX_GUI_Canvas *canvas = gX_Heap_SeekPtr_Index(&gui.canvases, 0);
    if (canvas) canvas->refresh = true;
}

gX_I0 gX_Widget_OnClick_TitleBack(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Focus_Clear();
    gX_GUI_Template_TitleGX_SetView(0);
    gX_GUI_Canvas *canvas = gX_Heap_SeekPtr_Index(&gui.canvases, 0);
    if (canvas) canvas->refresh = true;
}

//opencode: v0.6.5 — EXTRAS/CREDITS navigation (same view+refresh pattern
//   as TitleSaves/TitleBack; canvas 0 = title per the respond table).
gX_I0 gX_Widget_OnClick_TitleExtras(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Focus_Clear();
    gX_GUI_Template_TitleGX_SetView(2);
    gX_GUI_Canvas *canvas = gX_Heap_SeekPtr_Index(&gui.canvases, 0);
    if (canvas) canvas->refresh = true;
}

gX_I0 gX_Widget_OnClick_TitleCredits(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Focus_Clear();
    gX_GUI_Template_TitleGX_SetView(3);
    gX_GUI_Canvas *canvas = gX_Heap_SeekPtr_Index(&gui.canvases, 0);
    if (canvas) canvas->refresh = true;
}

//ai: open the title's demo hub using the same refresh path as other subviews.
gX_I0 gX_Widget_OnClick_TitleDemos(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Focus_Clear();
    gX_GUI_Template_TitleGX_SetView(5);
    gX_GUI_Canvas *canvas = gX_Heap_SeekPtr_Index(&gui.canvases, 0);
    if (canvas) canvas->refresh = true;
}

//ai: gX-240 console shell — a boot view over the title canvas. The shell
//   launches cartridges cold so the selected profile rebuilds the demo scene.
static gX_I0 console_refresh_view(gX_I0) {
    gX_GUI_Canvas *canvas = gX_Heap_SeekPtr_Index(&gui.canvases, 0);
    if (canvas) canvas->refresh = true;
}

gX_I0 gX_Widget_OnClick_TitleConsole(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Focus_Clear();
    gX_GUI_Template_TitleGX_SetView(6);
    gX_TraceLog(gX_LOG_NOTICE, "CONSOLE: view=shell");
    console_refresh_view();
}

gX_I0 gX_Widget_OnClick_ConsoleVideo(gX_I0 *arg) {
    gX_I32 mode = (gX_I32)(gX_U64)(uintptr_t)arg;
    if (mode == 2) {
        settings.screenWidth = maindata.windowWidth;
        settings.screenHeight = maindata.windowHeight;
        settings.presentFilter = 0;
        settings.consoleSSAA2x = false;
    } else {
        settings.screenWidth = 320;
        settings.screenHeight = 240;
        settings.presentFilter = mode == 1 ? 0 : 1;
        settings.consoleSSAA2x = mode == 3;
    }
    gX_Settings_Apply_VirtualScreen();
    settings.dirtyFlag = 1;
    gX_TraceLog(gX_LOG_NOTICE,
        "CONSOLE: video=%s window=%ux%u render=%ux%u out=%ux%u filter=%d",
        mode == 2 ? "native" : (mode == 3 ? "ssaa2x"
            : (mode == 0 ? "soft" : "sharp")),
        maindata.windowWidth, maindata.windowHeight,
        maindata.renderWidth, maindata.renderHeight,
        maindata.consoleOutputWidth, maindata.consoleOutputHeight,
        maindata.presentFilter);
    console_refresh_view();
}

gX_I0 gX_Widget_OnClick_ConsoleBoot(gX_I0 *arg) {
    gX_I32 cartridge = (gX_I32)(gX_U64)(uintptr_t)arg;
    gX_I32 sceneIndex = gX_SCENE_RENDERER_GFX;
    if (cartridge == 0) {
        arguments.showcase = true;
        arguments.showcaseY2K = true;
        arguments.cartridgeY2K = true;
        arguments.cartridgeRay1 = false;
        arguments.cartridgeParticle = false;
        //ai: honor the shell's selected 240p filter/SSAA; native falls back
        //   to the cartridge's soft 240p default.
        if (settings.screenWidth != 320 || settings.screenHeight != 240)
            gX_Widget_OnClick_ConsoleVideo((gX_I0*)(gX_U64)0);
        else
            gX_Settings_Apply_VirtualScreen();
        gX_Renderer_GFX_ColdBoot();
    } else if (cartridge == 1) {
        arguments.showcase = false;
        arguments.showcaseY2K = false;
        arguments.cartridgeY2K = false;
        arguments.cartridgeRay1 = true;
        arguments.cartridgeParticle = false;
        //ai: honor the shell's 240p/SSAA/PRISM selection, reset the module and
        //   apply Full quality before the scene switch; the camera response
        //   installs the authored RAY-1 view.
        gX_Settings_Apply_VirtualScreen();
        gX_RendererRaymarch_Cartridge_Apply();
        sceneIndex = gX_SCENE_RENDERER_RAYMARCH;
    } else {
        arguments.showcase = false;
        arguments.showcaseY2K = false;
        arguments.cartridgeY2K = false;
        arguments.cartridgeRay1 = false;
        arguments.cartridgeParticle = true;
        //ai: the particle sandbox's GUI-composite background consumes the
        //   same resolved/PRISM present target; reset to the authored entry
        //   view while keeping the selected video profile.
        gX_Settings_Apply_VirtualScreen();
        gX_ScenePhysics_Cartridge_Apply(&scene.physicsScene);
        sceneIndex = gX_SCENE_PHYSICS;
    }
    gX_TraceLog(gX_LOG_NOTICE,
        "CONSOLE: video=%s window=%ux%u render=%ux%u out=%ux%u filter=%d",
        settings.consoleSSAA2x ? "ssaa2x"
            : (settings.screenWidth == maindata.windowWidth
                && settings.screenHeight == maindata.windowHeight ? "native"
                : (settings.presentFilter ? "soft" : "sharp")),
        maindata.windowWidth, maindata.windowHeight,
        maindata.renderWidth, maindata.renderHeight,
        maindata.consoleOutputWidth, maindata.consoleOutputHeight,
        maindata.presentFilter);
    gX_TraceLog(gX_LOG_NOTICE, "CONSOLE: boot=%d scene=%d", cartridge,
        sceneIndex);
    gX_MainData_Switch(sceneIndex);
}

//ai: PRISM-1 is an independent 240p output toggle layered over Soft/Sharp/
//   SSAA; toggling it does not reload the scene targets.
gX_I0 gX_Widget_OnClick_ConsolePRISM(gX_I0 *arg) {
    (void)arg;
    settings.consolePRISM1 = settings.consolePRISM1 ? 0 : 1;
    gX_Settings_Apply_ConsolePRISM();
    //ai: both the shell and pause templates cache their labels; refresh the
    //   owning canvas and restore the same focused row after the rebuild.
    if (gui.menuCanvas) {
        gui.focusRestoreCanvasID = gui.focusCanvasID;
        gui.focusRestoreElementID = gui.focusElementID;
        gui.menuCanvas->refresh = true;
    }
    console_refresh_view();
    gX_TraceLog(gX_LOG_NOTICE,
        "CONSOLE: prism=%d render=%ux%u out=%ux%u",
        maindata.consolePRISM1, maindata.renderWidth, maindata.renderHeight,
        maindata.consoleOutputWidth, maindata.consoleOutputHeight);
}

//ai: the console shell routes save-state management through the existing
//   save-slot view and traces the intent for the shell gate.
gX_I0 gX_Widget_OnClick_ConsoleSaves(gX_I0 *arg) {
    gX_TraceLog(gX_LOG_NOTICE, "CONSOLE: save=slots");
    gX_Widget_OnClick_TitleSaves(arg);
}

//ai: load a console save slot from the shell; the state module owns the
//   cartridge/video/camera restore and the scene switch.
gX_I0 gX_Widget_OnClick_ConsoleLoadSlot(gX_I0 *arg) {
    gX_I32 slot = (gX_I32)(gX_U64)(uintptr_t)arg;
    gX_TraceLog(gX_LOG_NOTICE, "CONSOLE: load_slot=%d", slot);
    gX_ConsoleState_Load(slot);
}

//ai: direct pause-menu save/load use slot 1, matching the cartridge hotkeys.
gX_I0 gX_Widget_OnClick_ConsolePauseSave(gX_I0 *arg) {
    (void)arg;
    gX_I32 Result = gX_ConsoleState_Save(1);
    if (Result == 0) {
        if (gui.menuCanvas) gui.menuCanvas->refresh = true;
        gui.focusFirstPending = true;
    } else {
        snprintf((char *)gui.state.toastMessage,
            sizeof(gui.state.toastMessage), "%s", "Save failed");
        gui.state.SHOW_MENU = false;
        gui.state.SHOW_MSG = true;
        gX_GUI_Focus_Clear();
    }
    gX_TraceLog(gX_LOG_NOTICE,
        "CONSOLE_MENU: save slot=1 result=%d", Result);
}

gX_I0 gX_Widget_OnClick_ConsolePauseLoad(gX_I0 *arg) {
    (void)arg;
    if (!gX_ConsoleState_Exists(1)) {
        gX_GUI_ConsoleOSD_Notify(1, "EMPTY");
        gX_TraceLog(gX_LOG_NOTICE, "CONSOLE_MENU: load slot=1 empty");
        return;
    }
    gX_I32 Result = gX_ConsoleState_Load(1);
    if (Result == 0) {
        gui.state.SHOW_MENU = false;
        gX_GUI_Focus_Clear();
    } else {
        snprintf((char *)gui.state.toastMessage,
            sizeof(gui.state.toastMessage), "%s", "Load failed");
        gui.state.SHOW_MENU = false;
        gui.state.SHOW_MSG = true;
        gX_GUI_Focus_Clear();
    }
    gX_TraceLog(gX_LOG_NOTICE,
        "CONSOLE_MENU: load slot=1 result=%d", Result);
}

gX_I0 gX_Widget_OnClick_ConsolePauseVideo(gX_I0 *arg) {
    (void)arg;
    gX_I32 Mode = 0;
    if (settings.screenWidth == 320 && settings.screenHeight == 240) {
        if (settings.consoleSSAA2x) Mode = 2;
        else if (settings.presentFilter) Mode = 1;
        else Mode = 3;
    }
    gX_Widget_OnClick_ConsoleVideo(
        (gX_I0 *)(gX_U64)(uintptr_t)Mode);
    if (gui.menuCanvas) gui.menuCanvas->refresh = true;
    gui.focusFirstPending = true;
}

//ai: playback controls share the console playback clock; Step keeps the
//   pause menu open, Fast Forward arms the next resume speed.
gX_I0 gX_Widget_OnClick_ConsolePauseStep(gX_I0 *arg) {
    (void)arg;
    gX_ConsolePlayback_RequestStep();
    gX_TraceLog(gX_LOG_NOTICE, "CONSOLE_MENU: step");
}

gX_I0 gX_Widget_OnClick_ConsolePauseFastForward(gX_I0 *arg) {
    (void)arg;
    gX_ConsolePlayback_CycleFastForward();
    if (gui.menuCanvas) gui.menuCanvas->refresh = true;
    gui.focusFirstPending = true;
    gX_TraceLog(gX_LOG_NOTICE, "CONSOLE_MENU: fast-forward=%d",
        gX_ConsolePlayback_FastForward());
}

gX_I0 gX_Widget_OnClick_ConsoleExitShell(gX_I0 *arg) {
    (void)arg;
    gX_ConsolePlayback_Reset();
    arguments.consoleShell = true;
    arguments.cartridgeY2K = false;
    arguments.cartridgeRay1 = false;
    arguments.cartridgeParticle = false;
    arguments.showcase = false;
    arguments.showcaseY2K = false;
    gui.state.SHOW_MENU = false;
    gui.state.SHOW_MSG = false;
    gui.state.toastMessage[0] = '\0';
    gX_GUI_Focus_Clear();
    gX_GUI_Template_TitleGX_SetView(6);
    gX_TraceLog(gX_LOG_NOTICE, "CONSOLE: exit=shell");
    gX_MainData_Switch(gX_SCENE_TITLE);
}

//opencode: v0.6.5 — in-app tool openers (EXTRAS buttons). Runtime API in
//   tools.c; tools overlay the current scene and persist independently of
//   the title sub-view (close via window X / Close).
gX_I0 gX_Widget_OnClick_OpenEditor(gX_I0 *arg) {
    (void)arg;
    extern gX_I0 gX_Tools_OpenEditor_Runtime(gX_I0);
    gX_Tools_OpenEditor_Runtime();
}

gX_I0 gX_Widget_OnClick_OpenTheme(gX_I0 *arg) {
    (void)arg;
    extern gX_I0 gX_Tools_OpenTheme_Runtime(gX_I0);
    gX_Tools_OpenTheme_Runtime();
}

gX_I0 gX_Widget_OnClick_OpenThemLab(gX_I0 *arg) {
    (void)arg;
    extern gX_I0 gX_Tools_OpenThemLab_Runtime(gX_I0);
    gX_Tools_OpenThemLab_Runtime();
}

//opencode: EXTRAS material response gallery opener.
gX_I0 gX_Widget_OnClick_OpenMaterials(gX_I0 *arg) {
    (void)arg;
    extern gX_I0 gX_Tools_OpenMaterials_Runtime(gX_I0);
    gX_Tools_OpenMaterials_Runtime();
}

//opencode: EXTRAS shader experiment gallery opener.
gX_I0 gX_Widget_OnClick_OpenShaderLab(gX_I0 *arg) {
    (void)arg;
    extern gX_I0 gX_Tools_OpenShaderLab_Runtime(gX_I0);
    gX_Tools_OpenShaderLab_Runtime();
}

gX_I0 gX_Widget_OnClick_OpenGfx(gX_I0 *arg) {
    (void)arg;
    extern gX_I0 gX_Tools_OpenGfx_Runtime(gX_I0);
    //opencode: menu GFX button routes to the GFX scene first — there is
    //   nothing to light on the title (mirrors --gfx-editor CLI routing).
    //   Stays put on scenes that already carry 3D content.
#if gX_ENABLE_RENDERER == 1
    if (maindata.masterIndex == gX_SCENE_TITLE) {
        gX_GUI_Focus_Clear();
        gX_MainData_Switch(gX_SCENE_RENDERER_GFX);
    }
#endif
    gX_Tools_OpenGfx_Runtime();
}

//opencode: wavetable editor — opens over the current scene (title default);
//   it edits engine synth state, so no routing is needed.
gX_I0 gX_Widget_OnClick_OpenWavetable(gX_I0 *arg) {
    (void)arg;
    extern gX_I0 gX_Tools_OpenWavetable_Runtime(gX_I0);
    gX_Tools_OpenWavetable_Runtime();
}

//opencode: textwriter editor — routes to the textwriter scene (the preview
//   surface) before opening, mirroring the GFX menu card.
gX_I0 gX_Widget_OnClick_OpenTextwriter(gX_I0 *arg) {
    (void)arg;
    extern gX_I0 gX_Tools_OpenTextwriter_Runtime(gX_I0);
#if gX_ENABLE_RENDERER == 1
    if (maindata.masterIndex != gX_SCENE_RENDERER_TEXTWRITER) {
        gX_GUI_Focus_Clear();
        gX_MainData_Switch(gX_SCENE_RENDERER_TEXTWRITER);
    }
#endif
    gX_Tools_OpenTextwriter_Runtime();
}

//ai: physics editor — the opener routes to the physics sandbox (the tuning
//   target) itself, so the card just calls the runtime API.
gX_I0 gX_Widget_OnClick_OpenPhysics(gX_I0 *arg) {
    (void)arg;
    extern gX_I0 gX_Tools_OpenPhysics_Runtime(gX_I0);
    gX_Tools_OpenPhysics_Runtime();
}

//opencode: camera rail editor — the rail is shell-owned; the exe opener only
//   sets the flag the gx_shell module draws from (no routing needed).
gX_I0 gX_Widget_OnClick_OpenRail(gX_I0 *arg) {
    (void)arg;
    extern gX_I0 gX_Tools_OpenRail_Runtime(gX_I0);
    gX_Tools_OpenRail_Runtime();
}

//opencode: background editor — routes to the background scene in the opener.
gX_I0 gX_Widget_OnClick_OpenBackground(gX_I0 *arg) {
    (void)arg;
    extern gX_I0 gX_Tools_OpenBackground_Runtime(gX_I0);
    gX_Tools_OpenBackground_Runtime();
}

//opencode: sky editor — routes to the sky scene in the opener.
gX_I0 gX_Widget_OnClick_OpenSky(gX_I0 *arg) {
    (void)arg;
    extern gX_I0 gX_Tools_OpenSky_Runtime(gX_I0);
    gX_Tools_OpenSky_Runtime();
}

//opencode: switch to title scene
gX_I0 gX_Widget_OnClick_SwitchToTitle(gX_I0 *arg) {
    (void)arg;
    gui.state.SHOW_MENU = false;
    gX_GUI_Focus_Clear();
    gX_MainData_Switch(gX_SCENE_TITLE);
}

//opencode: switch to title scene with a specific subtemplate state
gX_I0 gX_Widget_OnClick_SwitchToTitleState(gX_I0 *arg) {
    gui.state.SHOW_MENU = false;
    gX_GUI_Focus_Clear();
    gX_MainData_Switch(gX_SCENE_TITLE);
}

//back button: set state + trigger refresh + transition
//opencode: v0.6.x — moved to builder_widgets.c as the owner-scoped
//   back_onclick_cb; the packed-args global pool it used is gone.

//opencode: gX — game overlays (inventory/questlog/bestiary) dropped

//opencode: save game from pause menu — show toast confirmation + update timestamp
gX_I0 gX_Widget_OnClick_SaveGame(gX_I0 *arg) {
    (void)arg;
#if gX_ENABLE_GAME == 1
    gX_I32 saveResult = gX_Save_Save(GX_SAVE_PATH);
    if (saveResult == 0) {
        strncpy(gui.state.toastMessage, "Game saved!", sizeof(gui.state.toastMessage) - 1);
        strncpy(gui.state.lastSavedText, "Last saved: just now", sizeof(gui.state.lastSavedText) - 1);
        gX_Audio_SoundPlay("successful");
    } else {
        strncpy(gui.state.toastMessage, "Save failed!", sizeof(gui.state.toastMessage) - 1);
        gX_Audio_SoundPlay("error");
    }
#else
    strncpy(gui.state.toastMessage, "Save unavailable (game data disabled)", sizeof(gui.state.toastMessage) - 1);
    gX_Audio_SoundPlay("error");
#endif
    gui.state.SHOW_MSG = true;
    gui.state.SHOW_MENU = false;
}

//opencode: coming-soon toast for unimplemented menu features
gX_I0 gX_Widget_OnClick_ComingSoon(gX_I0 *arg) {
    (void)arg;
    strncpy(gui.state.toastMessage, "Not yet implemented!", sizeof(gui.state.toastMessage) - 1);
    gui.state.SHOW_MSG = true;
    gX_Audio_SoundPlay("warning");
}

//opencode: quit confirm — the destructive action fires only from the dialog.
static gX_I0 gX_Quit_Confirmed(gX_I0 *arg) {
    (void)arg;
    maindata.close = true;
}

//opencode: quit callback — opens a confirm dialog instead of quitting instantly
gX_I0 gX_Widget_OnClick_Quit(gX_I0 *arg) {
    (void)arg;
    gX_GUI_Dialog_Open("Quit", "Quit to desktop?",
        "Quit", "Cancel",
        gX_Quit_Confirmed, NULL, NULL, NULL);
}

//opencode: Legacy Bind API removed — all click interactions use gX_GUI_OnClick (binding.h)
