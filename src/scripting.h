/**********************************************************************************************
*
*   [gX: src/scripting.h]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu, opencode
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


//ai: ported from gameX src/scripting.h + scripting.cpp (feature-first distill:
//   host lifecycle, hot-reload, watchdog, hooks, per-script virtual input,
//   persistent state and the companion-script events; the debugger panel,
//   mod scan, game-content API modules and demo-CLI helpers stayed out).
#ifndef GX_SCRIPTING_H
#define GX_SCRIPTING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/types.h"
#include "common/file.h"
#include "common/heap.h"
#include "input.h"

#define GX_SCRIPT_PATH_MAX 256
#define GX_SCRIPT_NAME_MAX 64
#define GX_SCRIPT_MAX_HOOKS 64

typedef struct gX_Script {
    gX_I8 name[GX_SCRIPT_NAME_MAX];
    gX_I8 description[256];
    gX_I8 path[GX_SCRIPT_PATH_MAX];

    gX_I1 loaded;
    gX_I1 compiled;
    gX_I1 execute;

    gX_U32 length;
    gX_I8 *text;

    gX_I64 mtime; //file modification time for hot-reload

    struct asIScriptModule *module;
    struct asIScriptContext *context;

    gX_F64 resumeTime;    //Sleep() — absolute time to resume
    gX_I1 watchdogKilled; //runaway loop tripped the line-callback budget

    //lightweight debugger — breakpoints + stepping, driven through the same
    //   line callback as the watchdog (one callback per context).
    #define GX_SCRIPT_DEBUG_BREAKPOINTS_MAX 16
    #define GX_SCRIPT_DEBUG_ACTION_CONTINUE 0
    #define GX_SCRIPT_DEBUG_ACTION_STEP     1
    #define GX_SCRIPT_DEBUG_ACTION_OVER     2
    #define GX_SCRIPT_DEBUG_ACTION_OUT      3
    gX_I1  debugWait;                    //paused at a breakpoint (Update skips it)
    gX_I32 debugAction;                  //GX_SCRIPT_DEBUG_ACTION_* (0 = run)
    gX_U32 debugStackLevel;              //stack depth captured at step start
    gX_I32 debugLine;                    //last line hit (for the panel)
    gX_I32 debugSkipLine;                //line to pass once after Continue (no re-pause)
    gX_I8  debugFile[128];
    gX_I32 debugBreakpoints[GX_SCRIPT_DEBUG_BREAKPOINTS_MAX];

    //per-script virtual input — each script has its own mouse/key override
    //   slot so concurrent scripts never clobber each other's pending input.
    gX_Input_Virtual virt;

    gX_I32 exceptionLine;
    gX_I8 exceptionMessage[256];
} gX_Script;

typedef struct gX_ScriptHook {
    gX_I8 name[64];
    struct asIScriptFunction *func;
} gX_ScriptHook;

//binding-module frame hooks — tour modules (camera rails, orbit helpers,
//   overlays) register here instead of touching the kernel. Update hooks run
//   after scripts each frame; draw hooks run in the draw phase after the
//   GUI layer (topmost).
typedef gX_I0 (*gX_ScriptingFrameFn)(gX_I0);
#define GX_SCRIPTING_MAX_FRAME_HOOKS 16
typedef struct gX_ScriptingFrameHook {
    gX_I8 name[64];
    gX_ScriptingFrameFn fn;
} gX_ScriptingFrameHook;

typedef struct gX_Scripting {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_I1 loaded;
    struct asIScriptEngine *engine;

    gX_Heap scripts;

    gX_U32 hooksCount;
    gX_ScriptHook hooks[GX_SCRIPT_MAX_HOOKS];

    gX_U32 updateHookCount;
    gX_ScriptingFrameHook updateHooks[GX_SCRIPTING_MAX_FRAME_HOOKS];
    gX_U32 drawHookCount;
    gX_ScriptingFrameHook drawHooks[GX_SCRIPTING_MAX_FRAME_HOOKS];

    //script persistent state — C-side KV store that survives hot-reload
    //   (module rebuild resets script globals). File serialization is a
    //   later slice; gX has no game-save system.
    #define GX_SCRIPT_STATE_MAX 64
    gX_I8  stateNames[GX_SCRIPT_STATE_MAX][64];
    gX_I8  stateTypes[GX_SCRIPT_STATE_MAX];   //'i' int, 'f' float, 's' string
    gX_I32 stateInts[GX_SCRIPT_STATE_MAX];
    gX_F32 stateFloats[GX_SCRIPT_STATE_MAX];
    gX_I8  stateStrings[GX_SCRIPT_STATE_MAX][256];
    gX_I1  stateUsed[GX_SCRIPT_STATE_MAX];
    gX_U32 stateCount;

    //shell harness — Assert*/Fail/Quit feed the process exit code so a gate
    //   can run a script and read pass/fail from the return value.
    gX_I32 shellFailures;
    gX_I32 shellExitCode;

    gX_I8 lastError[512];
} gX_Scripting;

extern gX_Scripting g_scripting;

gX_I32 gX_Scripting_Load(gX_I0);
gX_I32 gX_Scripting_Unload(gX_I0);
gX_I0  gX_Scripting_Update(gX_I0);

//boot/mod directory scans — load every .as under resources/script (boot) and
//   <mods>/<name>/script (mods). Boot: GX_SCRIPT_SCAN=0 opts out, GX_SCRIPT_DIR
//   overrides the directory. Mods: GX_SCRIPT_MOD_DIR overrides the root.
gX_I32 gX_Scripting_ScanScripts(gX_I0);
gX_I32 gX_Scripting_ScanModScripts(gX_I0);

gX_I32 gX_Script_Load(gX_Script *s, const gX_I8 *path);
gX_I32 gX_Script_Unload(gX_Script *s);
gX_I32 gX_Script_Compile(gX_Script *s);
gX_I32 gX_Script_Execute(gX_Script *s);
gX_I32 gX_Script_Reload(gX_Script *s);

gX_I32 gX_Scripting_AddScript(const gX_I8 *path);
gX_I32 gX_Scripting_AddScriptWithLibs(const gX_I8 *mainPath, const gX_I8 *libPaths[], gX_U32 libCount);
gX_Script *gX_Scripting_GetScript(gX_U32 index);
gX_I32 gX_Scripting_RemoveScript(gX_U32 index);
gX_I32 gX_Scripting_RemoveAllScripts(gX_I0);
gX_I0  gX_Scripting_CheckReload(gX_I0);

gX_I32 gX_Scripting_RegisterHook(const gX_I8 *name, struct asIScriptFunction *func);
gX_I32 gX_Scripting_TriggerHook(const gX_I8 *name);

//binding-module frame hooks (see gX_ScriptingFrameFn). Update hooks tick in
//   gX_Scripting_Update after the scripts; draw hooks are invoked by
//   gX_Scripting_Draw from the draw phase. Registration is idempotent by
//   function pointer.
gX_I32 gX_Scripting_RegisterUpdateHook(const gX_I8 *name, gX_ScriptingFrameFn fn);
gX_I32 gX_Scripting_RegisterDrawHook(const gX_I8 *name, gX_ScriptingFrameFn fn);
gX_I0  gX_Scripting_Draw(gX_I0);

//per-script virtual input — the virt slot the currently executing script
//   should write to (its own, or the shared slot when not inside a script).
//   Folded into input.virtInput at the end of Update.
struct gX_Input_Virtual *gX_Scripting_GetActiveVirt(gX_I0);

//run a named function in the first loaded script whose path matches
//   scriptPath, on a fresh context (does not touch the script's own main()
//   context — safe even while main() is suspended). 0 on success.
gX_I32 gX_Scripting_RunFunctionByName(const gX_I8 *scriptPath, const gX_I8 *funcName);

//true when a script with this exact path is loaded and compiled.
gX_I1 gX_Scripting_IsLoaded(const gX_I8 *scriptPath);

//lazy-load a companion script by path (dedupe) without executing main().
gX_I32 gX_Scripting_EnsureLoaded(const gX_I8 *scriptPath);

//companion-script gameplay event: EnsureLoaded + IsLoaded guard +
//   RunFunctionByName. Absent scripts are a silent no-op. 0 when the handler
//   ran (or the script was absent), non-zero on handler failure.
gX_I32 gX_Scripting_FireEvent(const gX_I8 *scriptPath, const gX_I8 *funcName);

//script persistent state — C-side KV store surviving hot-reload.
gX_I0 gX_Scripting_State_Clear(gX_I0);
gX_I1 gX_Scripting_State_SetInt(const gX_I8 *name, gX_I32 value);
gX_I1 gX_Scripting_State_SetFloat(const gX_I8 *name, gX_F32 value);
gX_I1 gX_Scripting_State_SetString(const gX_I8 *name, const gX_I8 *value);
gX_I32 gX_Scripting_State_GetInt(const gX_I8 *name, gX_I32 def);
gX_F32 gX_Scripting_State_GetFloat(const gX_I8 *name, gX_F32 def);
const gX_I8 *gX_Scripting_State_GetString(const gX_I8 *name);
gX_I1 gX_Scripting_State_Has(const gX_I8 *name);

//file persistence — JSON array of typed entries; default home
//   save/script_state.json (GX_SCRIPT_STATE=<path> overrides, =0 disables).
//   Loaded during gX_Scripting_Load, saved during Unload; scripts can call
//   ScriptSaveState()/ScriptLoadState() explicitly.
gX_I0 gX_Scripting_State_Save(gX_I0);
gX_I0 gX_Scripting_State_Load(gX_I0);
struct json_object *gX_Scripting_State_ToJSON(gX_I0);
gX_I0 gX_Scripting_State_FromJSON(struct json_object *obj);

//shell harness accessors — failures and the requested exit code.
gX_I32 gX_Scripting_Shell_FailCount(gX_I0);
gX_I32 gX_Scripting_Shell_ExitCode(gX_I0);
gX_I0 gX_Scripting_Shell_Fail(gX_I0);
gX_I0 gX_Scripting_Shell_SetExitCode(gX_I32 code);

//lightweight debugger — script list accessors + breakpoints/stepping
//   (the ImGui panel and gate probes drive this; the line callback owns the
//   pause). Script indices are heap slots [0, ScriptCount()).
gX_U32 gX_Scripting_ScriptCount(gX_I0);
const gX_I8 *gX_Scripting_ScriptPath(gX_U32 scriptIndex);
const gX_I8 *gX_Scripting_ScriptName(gX_U32 scriptIndex);
gX_I32 gX_Scripting_Debug_GetLine(gX_U32 scriptIndex);
gX_I1  gX_Scripting_Debug_IsPaused(gX_U32 scriptIndex);
gX_I1  gX_Scripting_Debug_AnyPaused(gX_I0);
gX_I0  gX_Scripting_Debug_ToggleBreakpoint(gX_U32 scriptIndex, gX_I32 line);
gX_I0  gX_Scripting_Debug_ClearBreakpoints(gX_U32 scriptIndex);
gX_I1  gX_Scripting_Debug_HasBreakpoint(gX_U32 scriptIndex, gX_I32 line);
gX_I0  gX_Scripting_Debug_Continue(gX_U32 scriptIndex);
gX_I0  gX_Scripting_Debug_Step(gX_U32 scriptIndex);
gX_I0  gX_Scripting_Debug_StepOver(gX_U32 scriptIndex);
gX_I0  gX_Scripting_Debug_StepOut(gX_U32 scriptIndex);

#ifdef __cplusplus
}
#endif

#endif //GX_SCRIPTING_H
