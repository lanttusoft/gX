/**********************************************************************************************
*
*   [gX: src/module/module.h]
*   Dynamic module host — Route A (curated dynamic-list exports) loader/registry
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


//opencode: capability packaging per docs/dynamic_modules.md — the default exe
//   stays lean; optional subsystems (the AngelScript shell first) build as
//   .so modules and register their frame/scene/tool hooks here. The export
//   list (modules/module_exports.txt) IS the ABI: append-only within an ABI
//   major; the module check gate pins drift.
#ifndef GX_MODULE_H
#define GX_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

//opencode: ABI major — bump on any incompatible host-struct or symbol change;
//   modules built against another major are refused at load.
#define GX_MODULE_ABI_VERSION 1

typedef gX_I0 (*gX_Module_FrameFn)(gX_I0);
typedef gX_I0 (*gX_Module_ShutdownFn)(gX_I0);
typedef gX_I32 (*gX_Module_ExitCodeFn)(gX_I0);
typedef gX_I0 (*gX_Module_MarkupClickFn)(const gX_I8 *scriptPath, const gX_I8 *funcName);
typedef gX_I0 (*gX_Module_EnsureScriptFn)(const gX_I8 *scriptPath);
typedef gX_I0 (*gX_Module_DrawFn)(gX_I0);
typedef gX_I1 (*gX_Module_HasFn)(gX_I0);

//opencode: host table handed to gX_Module_Entry — registration functions plus
//   the handshake. C ABI only; C++ stays inside the module.
typedef struct gX_Module_Host {
    gX_U32 abiVersion;          // GX_MODULE_ABI_VERSION of the host
    gX_U32 structSize;          // sizeof(gX_Module_Host) — shape handshake
    const gX_I8 *engineName;    // "gX"
    const gX_I8 *engineVersion; // version string (gX_Data_String_gX_version)

    // frame/scene/lifecycle registrations (0 = accepted)
    gX_I32 (*RegisterUpdate)(gX_Module_FrameFn fn);
    gX_I32 (*RegisterDraw)(gX_Module_FrameFn fn);
    gX_I32 (*RegisterSceneExit)(gX_Module_FrameFn fn);
    gX_I32 (*RegisterSceneEnter)(gX_Module_FrameFn fn);
    gX_I32 (*RegisterShutdown)(gX_Module_ShutdownFn fn);
    gX_I32 (*RegisterExitCode)(gX_Module_ExitCodeFn fn);

    // interop registrations — kernel features the module implements for the
    // host: markup onclick dispatch + companion-script loading, the ImGui
    // tool-panel draw, and the non-exclusive overlay (console) state.
    gX_I32 (*RegisterMarkupClick)(gX_Module_MarkupClickFn fn);
    gX_I32 (*RegisterMarkupEnsure)(gX_Module_EnsureScriptFn fn);
    gX_I32 (*RegisterToolsDraw)(gX_Module_DrawFn fn);
    gX_I32 (*RegisterOverlayOpen)(gX_Module_HasFn fn);
    gX_I32 (*RegisterOverlayDraw)(gX_Module_DrawFn fn);
} gX_Module_Host;

//opencode: the single exported entry every module must resolve.
typedef gX_I32 (*gX_Module_EntryFn)(const gX_Module_Host *host);

//=============================================================================
// host API (exe side)
//=============================================================================

//opencode: load one module by path (dlopen RTLD_NOW|RTLD_LOCAL, resolve
//   gX_Module_Entry, handshake, run the entry). Returns 0 on success.
gX_I32 gX_Module_Load(const gX_I8 *path);
//opencode: load from --module <path> / GX_MODULE=<path[:path...]>; with
//   neither set, auto-load every modules/*.so (sorted) when the dir exists.
gX_I32 gX_Module_LoadConfigured(gX_I0);

gX_I0 gX_Module_Update(gX_I0);
gX_I0 gX_Module_Draw(gX_I0);
gX_I0 gX_Module_SceneExit(gX_I0);
gX_I0 gX_Module_SceneEnter(gX_I0);
//opencode: shutdown callbacks (console/scripting unload) run before any
//   engine module teardown; handles are never dlclose'd (docs/dynamic_modules.md).
gX_I0 gX_Module_PreUnload(gX_I0);
gX_I0 gX_Module_Unload(gX_I0);
//opencode: max of registered module exit codes (shell Quit/assert failures).
gX_I32 gX_Module_ExitCode(gX_I0);
gX_I1 gX_Module_AnyLoaded(gX_I0);

//opencode: kernel-side interop — call the registered module handler or fall
//   back to the module-less behavior (warning / no-op).
gX_I0 gX_Module_MarkupClick(const gX_I8 *scriptPath, const gX_I8 *funcName);
gX_I0 gX_Module_MarkupEnsure(const gX_I8 *scriptPath);
gX_I0 gX_Module_ToolsDraw(gX_I0);
gX_I1 gX_Module_OverlayOpen(gX_I0);
gX_I0 gX_Module_OverlayDraw(gX_I0);
//opencode: true when a loaded module draws the scripting debugger (tools
//   opener hint path).
gX_I1 gX_Module_ToolsAvailable(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //GX_MODULE_H
