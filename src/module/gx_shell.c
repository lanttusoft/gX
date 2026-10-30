/**********************************************************************************************
*
*   [gX: src/module/gx_shell.c]
*   gx_shell module — the AngelScript shell (scripting + console + debugger panel)
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

//opencode: first dynamic module (docs/dynamic_modules.md): the shell sources
//   build into gx_shell.so instead of a second exe. The entry loads the
//   scripting engine + console, registers the frame/scene hooks and the
//   kernel-side interop (markup onclick, companion scripts, ImGui tool panel,
//   console overlay), and hands the shell exit code back to the host.
#include "module.h"

#include <string.h>

#include "../common/log.h"
#include "../main.h"
#include "../scripting.h"
#include "../console/console.h"

//opencode: tools panel runtime lives in the module too (script_panel.cpp);
//   the exe's open flag/theme opener stays kernel-side (exported).
extern gX_I0 gX_Tools_Script_Draw(gX_I0);
extern gX_I0 gX_Tools_Rail_Draw(gX_I0);
extern gX_I1 *gX_Tools_RuntimeRailFlag(gX_I0);
extern gX_I1 *gX_Tools_RuntimeScriptFlag(gX_I0);

//=============================================================================
// engine hooks
//=============================================================================

static gX_I0 gx_shell_update(gX_I0) {
    gX_Scripting_Update();
    gX_Console_Update();
}

static gX_I0 gx_shell_draw(gX_I0) {
    gX_Scripting_Draw();
}

static gX_I0 gx_shell_scene_exit(gX_I0) {
    gX_Scripting_TriggerHook("scene_exit");
}

static gX_I0 gx_shell_scene_enter(gX_I0) {
    gX_Scripting_TriggerHook("scene_enter");
}

static gX_I0 gx_shell_shutdown(gX_I0) {
    gX_Console_Uninit();
    gX_Scripting_Unload();
}

static gX_I32 gx_shell_exit_code(gX_I0) {
    gX_I32 code = gX_Scripting_Shell_ExitCode();
    if (code == 0 && gX_Scripting_Shell_FailCount() > 0) code = 1;
    return code;
}

//=============================================================================
// kernel-side interop
//=============================================================================

static gX_I0 gx_shell_markup_click(const gX_I8 *scriptPath, const gX_I8 *funcName) {
    gX_TraceLog(gX_LOG_INFO, "MARKUP: onclick '%s' in '%s'", funcName, scriptPath);
    gX_Scripting_RunFunctionByName(scriptPath, funcName);
}

//opencode: companion-script lazy load — same dedupe/AddScript logic the engine
//   kept inline while the shell was an exe build.
static gX_I0 gx_shell_markup_ensure(const gX_I8 *scriptPath) {
    if (!scriptPath || !scriptPath[0] || !g_scripting.loaded) return;
    gX_Script *scripts = (gX_Script*)g_scripting.scripts.data;
    for (gX_U32 i = 0; i < g_scripting.scripts.amount; i++) {
        if (g_scripting.scripts.meta[i].USED && scripts[i].path[0] &&
            strcmp(scripts[i].path, scriptPath) == 0) return;
    }
    gX_Scripting_AddScript(scriptPath);
}

static gX_I0 gx_shell_tools_draw(gX_I0) {
    //opencode: the exe carries one tools-draw slot; the module multiplexes
    //   its panels off the exe flags (script debugger / camera rail editor).
    if (*gX_Tools_RuntimeRailFlag()) {
        gX_Tools_Rail_Draw();
        return;
    }
    gX_Tools_Script_Draw();
}

static gX_I1 gx_shell_overlay_open(gX_I0) {
    return gX_Console_IsOpen();
}

static gX_I0 gx_shell_overlay_draw(gX_I0) {
    gX_Console_Draw();
}

//=============================================================================
// module entry
//=============================================================================

gX_I32 gX_Module_Entry(const gX_Module_Host *host);
gX_I32 gX_Module_Entry(const gX_Module_Host *host) {
    if (!host || host->abiVersion != GX_MODULE_ABI_VERSION
        || host->structSize < sizeof(gX_Module_Host)) {
        gX_TraceLog(gX_LOG_ERROR,
            "GX_SHELL: host ABI mismatch (host %u/%u, module %u)",
            host ? host->abiVersion : 0, host ? host->structSize : 0,
            (gX_U32)GX_MODULE_ABI_VERSION);
        return 1;
    }
    gX_TraceLog(gX_LOG_NOTICE, "GX_SHELL: entry (engine %s)", host->engineVersion);

    //opencode: scripting + console load — same position the exe build used
    //   (after audio, before the tools/markup builds).
    gX_I32 errors = 0;
    errors += gX_Scripting_Load() != 0 ? 1 : 0;
    if (arguments.hasAsScript) {
        if (gX_Scripting_AddScript(arguments.asScriptPath) < 0) {
            gX_TraceLog(gX_LOG_ERROR, "SCRIPT: failed to add '%s'",
                arguments.asScriptPath);
            errors++;
        }
    }
    gX_Console_Init();
    if (arguments.console) gX_Console_SetOpen(1);

    errors += host->RegisterUpdate(gx_shell_update) != 0 ? 1 : 0;
    errors += host->RegisterDraw(gx_shell_draw) != 0 ? 1 : 0;
    errors += host->RegisterSceneExit(gx_shell_scene_exit) != 0 ? 1 : 0;
    errors += host->RegisterSceneEnter(gx_shell_scene_enter) != 0 ? 1 : 0;
    errors += host->RegisterShutdown(gx_shell_shutdown) != 0 ? 1 : 0;
    errors += host->RegisterExitCode(gx_shell_exit_code) != 0 ? 1 : 0;
    errors += host->RegisterMarkupClick(gx_shell_markup_click) != 0 ? 1 : 0;
    errors += host->RegisterMarkupEnsure(gx_shell_markup_ensure) != 0 ? 1 : 0;
    errors += host->RegisterToolsDraw(gx_shell_tools_draw) != 0 ? 1 : 0;
    errors += host->RegisterOverlayOpen(gx_shell_overlay_open) != 0 ? 1 : 0;
    errors += host->RegisterOverlayDraw(gx_shell_overlay_draw) != 0 ? 1 : 0;
    return errors != 0 ? 1 : 0;
}

//opencode: optional ABI declaration — the host refuses a mismatch before the
//   entry runs (both directions of the handshake).
gX_U32 gX_Module_ABI(gX_I0);
gX_U32 gX_Module_ABI(gX_I0) {
    return GX_MODULE_ABI_VERSION;
}
