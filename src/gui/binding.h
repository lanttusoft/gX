/**********************************************************************************************
*
*   [gX: src/gui/binding.h]
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


#ifndef GUI_BINDING_H
#define GUI_BINDING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "element/struct.h"

//opencode: simplified click binding — the ONLY reliable way to handle clicks.
//Multiple data modules on one element are broken (data[1+] never fires).
//Use gX_GUI_OnClick for all click interactions. It creates a single data
//module at index 0 with isFunctionPtr, bypassing the agent/operator/WritePtrs
//system entirely. The callback runs on click — no hidden state, no corruption.

typedef gX_I0 (*gX_GUI_ClickCallback)(gX_I0 *userData);

//opencode: owner data for a settings keyboard-bind capture button.
typedef struct gX_GUI_BindCaptureContext {
    gX_U32 bindIndex;
    gX_U32 keySlot;
} gX_GUI_BindCaptureContext;

// Register a callback that fires when the element is clicked.
// The callback receives the userData pointer.
gX_I0 gX_GUI_OnClick(gX_GUI_Element *element, gX_GUI_ClickCallback callback, gX_I0 *userData);

//opencode: v0.6.x — owner-scoped callback context (GUI audit finding 4).
//   Copies contextSize bytes into the owning element's data module and points
//   the callback at that inline copy, so the context lives and dies with the
//   element instead of a global fixed pool that a partial canvas refresh could
//   alias. Used by the widget builders; plain gX_GUI_OnClick keeps arbitrary
//   external userData behaviour. Returns false when the element cannot take a
//   data module (invalid element or data heap exhausted).
gX_I1 gX_GUI_OnClick_Context(gX_GUI_Element *element,
    gX_GUI_ClickCallback callback, const gX_I0 *context, gX_U32 contextSize);

//reusable callbacks for common OnClick patterns
gX_I0 gX_Widget_OnClick_CallSwitch(gX_I0 *arg); // calls gX_MainData_Switch((gX_I32)(intptr_t)arg)
gX_I0 gX_Widget_OnClick_CloseEscapeMenu(gX_I0 *arg);     // hide menu + clear focus
gX_I0 gX_Widget_OnClick_OpenSettings(gX_I0 *arg);         // open settings template on menu canvas
gX_I0 gX_Widget_OnClick_BackToEscapeMenu(gX_I0 *arg);     // return to escape menu from settings
gX_I0 gX_Widget_OnClick_OpenKeyBindings(gX_I0 *arg);      // open settings keyboard binds
gX_I0 gX_Widget_OnClick_BackToSettings(gX_I0 *arg);       // return from keyboard binds
gX_I0 gX_Widget_OnClick_CaptureBind(gX_I0 *arg);          // arm one keyboard bind capture
gX_I0 gX_GUI_Settings_Refresh(gX_I0);                     // refresh the active settings canvas
gX_I1 gX_GUI_Handle_Back(gX_I0);                           // route Escape/gamepad B to the active UI subview
gX_I0 gX_Widget_OnClick_SwitchToTitle(gX_I0 *arg);        // switch to title scene + close menu
gX_I0 gX_Widget_OnClick_DismissToast(gX_I0 *arg);         // hide message toast + clear text
gX_I0 gX_Widget_OnClick_TitleSaves(gX_I0 *arg);            // title: swap to save-slot sub-view
gX_I0 gX_Widget_OnClick_TitleBack(gX_I0 *arg);             // title: back to main menu sub-view
//opencode: v0.6.5 — EXTRAS/CREDITS sub-views + in-app tool openers.
gX_I0 gX_Widget_OnClick_TitleExtras(gX_I0 *arg);           // title: swap to extras (tools) sub-view
gX_I0 gX_Widget_OnClick_TitleCredits(gX_I0 *arg);          // title: swap to credits sub-view
//ai: title demo hub for Battle and the GFX showcase.
gX_I0 gX_Widget_OnClick_TitleDemos(gX_I0 *arg);
gX_I0 gX_Widget_OnClick_OpenEditor(gX_I0 *arg);            // extras: open GUI editor over current scene
gX_I0 gX_Widget_OnClick_OpenTheme(gX_I0 *arg);             // extras: open theme editor over current scene
gX_I0 gX_Widget_OnClick_OpenThemLab(gX_I0 *arg);           // extras: open theme lab preview + theme editor
gX_I0 gX_Widget_OnClick_OpenMaterials(gX_I0 *arg);         // extras: open material response lab
//opencode: extras shader experiment gallery opener.
gX_I0 gX_Widget_OnClick_OpenShaderLab(gX_I0 *arg);         // extras: open shader experiment lab
gX_I0 gX_Widget_OnClick_OpenGfx(gX_I0 *arg);               // extras: open GFX editor over current scene
gX_I0 gX_Widget_OnClick_OpenWavetable(gX_I0 *arg);         // extras: open the synth wavetable editor
gX_I0 gX_Widget_OnClick_OpenTextwriter(gX_I0 *arg);        // extras: open the textwriter editor (routes to the scene)
gX_I0 gX_Widget_OnClick_OpenPhysics(gX_I0 *arg);           // extras: open the physics editor (routes to the scene)
gX_I0 gX_Widget_OnClick_OpenRail(gX_I0 *arg);              // extras: open the camera rail editor (module panel)
gX_I0 gX_Widget_OnClick_OpenBackground(gX_I0 *arg);        // extras: open the background editor (routes to the scene)
gX_I0 gX_Widget_OnClick_OpenSky(gX_I0 *arg);               // extras: open the sky editor (routes to the scene)
gX_I0 gX_GUI_Template_TitleGX_SetView(gX_I32 view);        // titlegx.c view state (defined there)
gX_I1 gX_GUI_Template_TitleGX_Back(gX_I0);                 // titlegx.c: return to title menu from a subview
gX_I0 gX_Widget_OnClick_SwitchToTitleState(gX_I0 *arg);   // switch to title scene with subtemplate state
gX_I0 gX_Widget_OnClick_SaveGame(gX_I0 *arg);              // save game
gX_I0 gX_Widget_OnClick_ComingSoon(gX_I0 *arg);           // coming-soon toast
gX_I0 gX_Widget_OnClick_Quit(gX_I0 *arg);                 // close the application

#ifdef __cplusplus
}
#endif

#endif //GUI_BINDING_H
