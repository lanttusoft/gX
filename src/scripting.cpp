/**********************************************************************************************
*
*   [gX: src/scripting.cpp]
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


//ai: ported from gameX src/scripting.cpp — the scripting host: engine
//   lifecycle, script heap + per-frame execution, mtime hot-reload, the
//   runaway watchdog (line-callback instruction budget), per-script virtual
//   input merge, companion-script events and the shell harness counters.
//   The debugger panel, mod scan, GUI-test scan, demo-CLI helpers and the
//   game-content API modules stayed out (see docs/scripting.md).
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

#include <angelscript.h>
#include <scriptstdstring/scriptstdstring.h>
#include <scriptarray/scriptarray.h>

#include "common/log.h"
#include "common/heap.h"
#include "main.h"
#include "input.h"

#include "scripting/script.h"
#include "scripting/hooks.h"
#include "scripting/api_core.h"
#include "scripting/api_state.h"
#include "scripting/tour/tour.h"

#include "wrappers/file.h"

gX_Scripting g_scripting;

#define GX_SCRIPTING_MAX_LINES_PER_FRAME 10000000ull
static gX_U64 g_scriptingLinesThisFrame = 0;
static gX_I1 g_scriptingWatchdogFired = 0;

static gX_Script *gX_Scripting_FindScriptByContext(asIScriptContext *ctx);
static gX_I1 gX_Scripting_Debug_CheckBreakpoint(asIScriptContext *ctx, gX_Script *s);

//runaway-script watchdog — SetLineCallback fires on every executed line. A
//   script exceeding the per-frame budget (infinite loop with no Sleep) is
//   suspended and killed so the frame loop can never hang. The same callback
//   services the debugger: breakpoints/steps suspend the context before the
//   watchdog budget.
void gX_Scripting_LineCallback(asIScriptContext *ctx) {
    if (!ctx) return;
    if (g_scriptingLinesThisFrame++ < GX_SCRIPTING_MAX_LINES_PER_FRAME) {
        gX_Script *s = gX_Scripting_FindScriptByContext(ctx);
        if (s && !s->debugWait) {
            gX_Script *scripts = (gX_Script*)g_scripting.scripts.data;
            gX_U32 index = (gX_U32)(s - scripts);
            if (s->debugAction != GX_SCRIPT_DEBUG_ACTION_CONTINUE ||
                gX_Scripting_Debug_HasBreakpoint(index, ctx->GetLineNumber(0, 0, 0))) {
                if (gX_Scripting_Debug_CheckBreakpoint(ctx, s)) {
                    ctx->Suspend();
                }
            }
        }
        return;
    }

    if (!g_scriptingWatchdogFired) {
        gX_Script *scripts = (gX_Script*)g_scripting.scripts.data;
        for (gX_U32 i = 0; i < g_scripting.scripts.amount; i++) {
            if (g_scripting.scripts.meta[i].USED && scripts[i].context == ctx) {
                gX_TraceLog_SCRIPTING(gX_LOG_WARNING,
                    "script '%s' exceeded instruction budget — killed", scripts[i].name);
                scripts[i].execute = false;
                scripts[i].watchdogKilled = 1;
                break;
            }
        }
        g_scriptingWatchdogFired = 1;
    }
    ctx->Suspend();
}

static gX_I0 gX_Scripting_MessageCallback(const asSMessageInfo *msg, gX_I0 *param) {
    (void)param;
    const char *type = "ERR ";
    if (msg->type == asMSGTYPE_WARNING)          type = "WARN";
    else if (msg->type == asMSGTYPE_INFORMATION) type = "INFO";

    gX_TraceLog_SCRIPTING(gX_LOG_INFO, "[%s] (%d,%d): %s: %s",
        msg->section, msg->row, msg->col, type, msg->message);

    if (msg->type == asMSGTYPE_ERROR) {
        snprintf(g_scripting.lastError, sizeof(g_scripting.lastError),
            "[%s] (%d,%d): %s", msg->section, msg->row, msg->col, msg->message);
    }
}

static gX_I32 gX_Scripting_RegisterAll(asIScriptEngine *engine) {
    RegisterStdString(engine);
    RegisterScriptArray(engine, true);

    gX_Scripting_RegisterCoreAPI(engine);
    gX_Scripting_RegisterVec3Type(engine);
    gX_Scripting_RegisterHookAPI(engine);
    gX_Scripting_RegisterStateAPI(engine);
    gX_TourCamera_Register(engine);
    gX_TourGfx_Register(engine);
    gX_TourAudio_Register(engine);
    gX_TourBackground_Register(engine);
    gX_TourSynth_Register(engine);
    gX_TourOPX_Register(engine);
    gX_TourPad_Register(engine);
    gX_TourTextWriter_Register(engine);
    gX_TourOverlay_Register(engine);
    gX_TourRaymarch_Register(engine);
    gX_TourPostProcess_Register(engine);

    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "all APIs registered");
    return 0;
}

//scan a directory for *.as files and load each one (sorted for a
//   deterministic boot order). Shared by the boot scan and the mod scan.
static gX_I32 gX_Scripting_ScanDirectory(const gX_I8 *dir, const gX_I8 *tag) {
    if (!dir || !dir[0]) return 0;

    gX_File_Directory listing;
    if (gX_File_Directory_List(dir, &listing) <= 0) {
        gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "%s scan: no scripts in '%s'", tag, dir);
        return 0;
    }

    for (gX_I32 i = 1; i < listing.count; i++) {
        gX_TextLine256 key;
        memcpy(key, listing.names[i], sizeof(gX_TextLine256));
        gX_I32 j = i - 1;
        while (j >= 0 && strcmp(listing.names[j], key) > 0) {
            memcpy(listing.names[j + 1], listing.names[j], sizeof(gX_TextLine256));
            j--;
        }
        memcpy(listing.names[j + 1], key, sizeof(gX_TextLine256));
    }

    gX_I32 loaded = 0;
    for (gX_I32 i = 0; i < listing.count; i++) {
        const gX_I8 *name = listing.names[i];
        if (!name[0] || name[0] == '.') continue;
        const gX_I8 *dot = strrchr(name, '.');
        if (!dot || strcmp(dot, ".as") != 0) continue;

        gX_I8 fullPath[512];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dir, name);
        if (gX_Scripting_AddScript(fullPath) >= 0) loaded++;
    }

    if (loaded > 0) {
        gX_TraceLog_SCRIPTING(gX_LOG_INFO, "%s scan: loaded %d script(s) from '%s'", tag, loaded, dir);
    }
    return loaded;
}

//boot scan — every .as in the resources/script directory (GX_SCRIPT_DIR
//   overrides the directory, GX_SCRIPT_SCAN=0 opts out).
gX_I32 gX_Scripting_ScanScripts(gX_I0) {
    const gX_I8 *off = getenv("GX_SCRIPT_SCAN");
    if (off && off[0] == '0') return 0;
    const gX_I8 *dir = getenv("GX_SCRIPT_DIR");
    if (!dir || !dir[0]) dir = "resources/script";
    return gX_Scripting_ScanDirectory(dir, "script");
}

//mod scan — <mods>/<name>/script/*.as for each entry (GX_SCRIPT_MOD_DIR
//   overrides the mod root, default "custom").
gX_I32 gX_Scripting_ScanModScripts(gX_I0) {
    const gX_I8 *root = getenv("GX_SCRIPT_MOD_DIR");
    if (!root || !root[0]) root = "custom";

    gX_File_Directory entries;
    if (gX_File_Directory_List(root, &entries) <= 0) return 0;

    gX_I32 totalLoaded = 0;
    for (gX_I32 i = 0; i < entries.count; i++) {
        const gX_I8 *entry = entries.names[i];
        if (!entry[0] || entry[0] == '.') continue;

        gX_I8 scriptDir[512];
        snprintf(scriptDir, sizeof(scriptDir), "%s/%s/script", root, entry);
        totalLoaded += gX_Scripting_ScanDirectory(scriptDir, "mod");
    }
    return totalLoaded;
}

gX_I32 gX_Scripting_Load(gX_I0) {
    gX_TRACELOG_PROLOGUE("ScriptingLoad", "SCRIPTING: Loading scripting system")

    memset(&g_scripting, 0, sizeof(g_scripting));
    strcpy(g_scripting.name, "[gX] Scripting");
    strcpy(g_scripting.description, "AngelScript scripting shell");
    gX_TraceLog_SCRIPTING(gX_LOG_GREEN, "name: %s", g_scripting.name);
    gX_TraceLog_SCRIPTING(gX_LOG_GREEN, "desc: %s", g_scripting.description);

    g_scripting.engine = asCreateScriptEngine();
    if (!g_scripting.engine) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "failed to create AngelScript engine");
        gX_TRACELOG_EPILOGUE("SCRIPTING: Load failed")
        return 1;
    }

    g_scripting.engine->SetMessageCallback(
        asFUNCTION(gX_Scripting_MessageCallback), 0, asCALL_CDECL);

    gX_Scripting_RegisterAll(g_scripting.engine);

    gX_Heap_Zero(&g_scripting.scripts);
    gX_Heap_SetBlockSize(&g_scripting.scripts, sizeof(gX_Script));
    gX_Heap_SetMax(&g_scripting.scripts, 256);
    gX_Heap_SetAmount(&g_scripting.scripts, 0);
    gX_Heap_Init(&g_scripting.scripts);

    gX_Scripting_State_Load();
    gX_Scripting_ScanScripts();
    gX_Scripting_ScanModScripts();

    g_scripting.loaded = true;
    maindata.moduleinfo.scriptingExists = true;

    gX_TraceLog_SCRIPTING(gX_LOG_INFO, "Scripting system loaded");
    gX_TRACELOG_EPILOGUE("SCRIPTING: Load done")
    return 0;
}

gX_I32 gX_Scripting_Unload(gX_I0) {
    gX_TRACELOG_PROLOGUE("ScriptingUnload", "SCRIPTING: Unloading")

    gX_Scripting_State_Save();

    gX_Scripting_Hook_RemoveAll(0);

    gX_Script *scripts = (gX_Script*)g_scripting.scripts.data;
    for (gX_U32 i = 0; i < g_scripting.scripts.amount; i++) {
        if (g_scripting.scripts.meta[i].USED) {
            gX_Script_Unload(&scripts[i]);
        }
    }
    gX_Heap_DeInit(&g_scripting.scripts);

    if (g_scripting.engine) {
        g_scripting.engine->ShutDownAndRelease();
        g_scripting.engine = 0;
    }

    g_scripting.loaded = false;
    maindata.moduleinfo.scriptingExists = false;

    gX_TraceLog_SCRIPTING(gX_LOG_INFO, "Scripting system unloaded");
    gX_TRACELOG_EPILOGUE("SCRIPTING: Unload done")
    return 0;
}

//mtime hot-reload — watches loaded .as files (rate-limited, matching the
//   CSS/markup/shader pattern). A suspended main() restarts from the top on
//   reload (the context is recreated).
static gX_U32 g_scriptReloadCounter = 0;

gX_I0 gX_Scripting_CheckReload(gX_I0) {
    g_scriptReloadCounter++;
    if (g_scriptReloadCounter % 30 != 0) return;
    if (!g_scripting.loaded || !g_scripting.engine) return;

    gX_Script *scripts = (gX_Script*)g_scripting.scripts.data;
    for (gX_U32 i = 0; i < g_scripting.scripts.amount; i++) {
        if (!g_scripting.scripts.meta[i].USED) continue;
        gX_Script *s = &scripts[i];
        if (!s->loaded || !s->path[0]) continue;

        struct stat st;
        if (stat(s->path, &st) != 0) continue;
        gX_I64 mt = (gX_I64)st.st_mtime;
        if (mt == s->mtime) continue;

        gX_TraceLog_SCRIPTING(gX_LOG_INFO, "SCRIPT: hot-reload '%s'", s->path);
        s->mtime = mt;
        gX_Script_Reload(s);
    }
}

static gX_Script *gX_Scripting_FindScriptByContext(asIScriptContext *ctx) {
    if (!ctx) return 0;
    gX_Script *scripts = (gX_Script*)g_scripting.scripts.data;
    for (gX_U32 i = 0; i < g_scripting.scripts.amount; i++) {
        if (g_scripting.scripts.meta[i].USED && scripts[i].context == ctx)
            return &scripts[i];
    }
    return 0;
}

static gX_Script *gX_Scripting_ActiveScript(void) {
    asIScriptContext *ctx = asGetActiveContext();
    if (!ctx) return 0;
    return gX_Scripting_FindScriptByContext(ctx);
}

//the virt slot the currently executing script should write to (its own, or
//   the shared slot when not inside a script).
gX_Input_Virtual *gX_Scripting_GetActiveVirt(void) {
    gX_Script *s = gX_Scripting_ActiveScript();
    return s ? &s->virt : &input.virtInput;
}

//merge per-script virt slots into the shared input.virtInput (called at the
//   end of Update so the next frame's gX_Input_Update applies them). A slot
//   only contributes when it has pending state.
static gX_I0 gX_Scripting_MergeVirtualInput(void) {
    input.virtInput.active = 0;
    input.virtInput.clickPending = 0;
    input.virtInput.keyPending = 0;
    input.virtInput.keyShift = 0;

    gX_Script *scripts = (gX_Script*)g_scripting.scripts.data;
    for (gX_U32 i = 0; i < g_scripting.scripts.amount; i++) {
        if (!g_scripting.scripts.meta[i].USED) continue;
        gX_Script *s = &scripts[i];
        if (!s->compiled) continue;
        if (!s->virt.active) continue;

        input.virtInput.active = 1;
        //only a pending CLICK carries a mouse position — a key-only slot
        //   (mouseX/Y = 0) must not clobber a concurrent script's click target.
        if (s->virt.clickPending) {
            input.virtInput.mouseX = s->virt.mouseX;
            input.virtInput.mouseY = s->virt.mouseY;
            input.virtInput.clickPending = 1;
            s->virt.clickPending = 0;
        }
        if (s->virt.keyPending) {
            input.virtInput.keyPending = 1;
            input.virtInput.keyCode = s->virt.keyCode;
            input.virtInput.keyShift = s->virt.keyShift;
            s->virt.keyPending = 0;
        }
    }
}

gX_I0 gX_Scripting_Update(gX_I0) {
    if (!g_scripting.loaded || !g_scripting.engine) return;

    g_scriptingLinesThisFrame = 0;
    g_scriptingWatchdogFired = 0;

    gX_Scripting_CheckReload();

    gX_Script *scripts = (gX_Script*)g_scripting.scripts.data;
    for (gX_U32 i = 0; i < g_scripting.scripts.amount; i++) {
        if (!g_scripting.scripts.meta[i].USED) continue;
        gX_Script *s = &scripts[i];
        if (!s->compiled) continue;
        if (!s->execute) continue;

        if (s->debugWait) continue;

        if (s->resumeTime > 0.0 && maindata.timeElapsed < s->resumeTime)
            continue;

        int r;
        asEContextState ctxState = s->context->GetState();
        if (ctxState == asEXECUTION_SUSPENDED || ctxState == asEXECUTION_ACTIVE) {
            r = s->context->Execute();
        } else {
            s->resumeTime = 0.0;
            r = gX_Script_RunFunction(s, "void main()");
        }

        if (r == asEXECUTION_SUSPENDED) {
            //keep alive only for cooperative suspend (Sleep/Yield) — a
            //   watchdog kill suspends the context too but stays dead.
            if (s->watchdogKilled) {
                s->execute = false;
                s->resumeTime = 0.0;
                s->watchdogKilled = 0;
            } else {
                s->execute = true;
            }
        } else {
            s->execute = false;
            s->resumeTime = 0.0;
        }
    }

    gX_Scripting_MergeVirtualInput();

    for (gX_U32 i = 0; i < g_scripting.updateHookCount; i++) {
        if (g_scripting.updateHooks[i].fn) g_scripting.updateHooks[i].fn();
    }
}

gX_I32 gX_Script_Load(gX_Script *s, const gX_I8 *path) {
    return gX_Script_LoadFile(s, path);
}

gX_I32 gX_Script_Unload(gX_Script *s) {
    if (s->context) {
        s->context->Release();
        s->context = 0;
    }
    if (s->module) {
        s->module->Discard();
        s->module = 0;
    }
    if (s->text) {
        free(s->text);
        s->text = 0;
    }
    s->loaded = false;
    s->compiled = false;
    return 0;
}

gX_I32 gX_Script_Compile(gX_Script *s) {
    return gX_Script_CompileModule(s, g_scripting.engine);
}

gX_I32 gX_Script_Execute(gX_Script *s) {
    s->execute = true;
    return 0;
}

gX_I32 gX_Script_Reload(gX_Script *s) {
    //ai: fix carried from gameX — Init memsets the struct, so the path must be
    //   saved before it is cleared (gameX's original read s->path after Init
    //   and reloaded an empty path, silently killing the script), and the
    //   script resumes running after the rebuild.
    gX_I8 path[GX_SCRIPT_PATH_MAX];
    snprintf(path, sizeof(path), "%s", s->path);

    gX_Script_Unload(s);
    gX_Script_Init(s);

    int r = gX_Script_LoadFile(s, path);
    if (r != 0) return r;
    r = gX_Script_CompileModule(s, g_scripting.engine);
    if (r != 0) return r;
    r = gX_Script_CreateContext(s, g_scripting.engine);
    if (r != 0) return r;
    s->execute = true;
    return r;
}

//lazy-load a companion script (dedupe by path) WITHOUT executing main() —
//   for optional handler-only scripts (onclick companions, event handlers).
gX_I32 gX_Scripting_EnsureLoaded(const gX_I8 *path) {
    if (!path || !path[0] || !g_scripting.loaded) return -1;
    if (gX_Scripting_IsLoaded(path)) return 0;
    int idx = gX_Scripting_AddScript(path);
    if (idx >= 0) {
        gX_Script *s = gX_Scripting_GetScript((gX_U32)idx);
        if (s) s->execute = false;
    }
    return idx;
}

//one-shot companion event — EnsureLoaded (dedupe) + IsLoaded guard +
//   RunFunctionByName. Absent scripts are a silent no-op (no warning spam).
gX_I32 gX_Scripting_FireEvent(const gX_I8 *scriptPath, const gX_I8 *funcName) {
    if (!scriptPath || !funcName || !g_scripting.loaded) return 0;
    gX_Scripting_EnsureLoaded(scriptPath);
    if (!gX_Scripting_IsLoaded(scriptPath)) return 0;
    return gX_Scripting_RunFunctionByName(scriptPath, funcName);
}

gX_I32 gX_Scripting_AddScript(const gX_I8 *path) {
    gX_Script *s = (gX_Script*)gX_Heap_Add_FirstEmpty_NoPtr(&g_scripting.scripts);
    if (!s) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "failed to add script (heap full)");
        return -1;
    }

    gX_Script_Init(s);
    int r = gX_Script_LoadFile(s, path);
    if (r != 0) return r;
    r = gX_Script_CompileModule(s, g_scripting.engine);
    if (r != 0) return r;
    r = gX_Script_CreateContext(s, g_scripting.engine);
    if (r != 0) return r;

    s->execute = true;

    int idx = (int)((gX_U8*)s - (gX_U8*)g_scripting.scripts.data) / (int)sizeof(gX_Script);
    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "added script '%s' at index %d", path, idx);
    return idx;
}

gX_I32 gX_Scripting_AddScriptWithLibs(const gX_I8 *mainPath, const gX_I8 *libPaths[], gX_U32 libCount) {
    gX_Script *s = (gX_Script*)gX_Heap_Add_FirstEmpty_NoPtr(&g_scripting.scripts);
    if (!s) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "failed to add script (heap full)");
        return -1;
    }

    gX_Script_Init(s);
    int r = gX_Script_LoadFileWithLibs(s, mainPath, libPaths, libCount);
    if (r != 0) return r;
    r = gX_Script_CompileModule(s, g_scripting.engine);
    if (r != 0) return r;
    r = gX_Script_CreateContext(s, g_scripting.engine);
    if (r != 0) return r;

    s->execute = true;

    int idx = (int)((gX_U8*)s - (gX_U8*)g_scripting.scripts.data) / (int)sizeof(gX_Script);
    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "added script '%s' (with %u libs) at index %d", mainPath, libCount, idx);
    return idx;
}

gX_Script *gX_Scripting_GetScript(gX_U32 index) {
    gX_Script *scripts = (gX_Script*)g_scripting.scripts.data;
    if (index >= g_scripting.scripts.amount) return 0;
    if (!g_scripting.scripts.meta[index].USED) return 0;
    return &scripts[index];
}

gX_I32 gX_Scripting_RemoveScript(gX_U32 index) {
    gX_Script *s = (gX_Script*)gX_Heap_SeekPtr_Index(&g_scripting.scripts, index);
    if (!s || !g_scripting.scripts.meta[index].USED) return -1;
    gX_Script_Unload(s);
    g_scripting.scripts.meta[index].USED = false;
    return 0;
}

gX_I32 gX_Scripting_RemoveAllScripts(gX_I0) {
    gX_Script *scripts = (gX_Script*)g_scripting.scripts.data;
    for (gX_U32 i = 0; i < g_scripting.scripts.amount; i++) {
        if (g_scripting.scripts.meta[i].USED) {
            gX_Script_Unload(&scripts[i]);
            g_scripting.scripts.meta[i].USED = false;
        }
    }
    return 0;
}

gX_I32 gX_Scripting_RegisterHook(const gX_I8 *name, asIScriptFunction *func) {
    return gX_Scripting_Hook_Register(name, func);
}

gX_I32 gX_Scripting_TriggerHook(const gX_I8 *name) {
    return gX_Scripting_Hook_Trigger(name);
}

static gX_I32 gX_Scripting_RegisterFrameHook(gX_ScriptingFrameHook *hooks,
                                             gX_U32 *count, const gX_I8 *name,
                                             gX_ScriptingFrameFn fn) {
    if (!fn) return -1;
    for (gX_U32 i = 0; i < *count; i++) {
        if (hooks[i].fn == fn) return 0;
    }
    if (*count >= GX_SCRIPTING_MAX_FRAME_HOOKS) return -2;
    gX_ScriptingFrameHook *hook = &hooks[(*count)++];
    snprintf(hook->name, sizeof(hook->name), "%s", name ? name : "hook");
    hook->fn = fn;
    return 0;
}

gX_I32 gX_Scripting_RegisterUpdateHook(const gX_I8 *name, gX_ScriptingFrameFn fn) {
    return gX_Scripting_RegisterFrameHook(g_scripting.updateHooks,
        &g_scripting.updateHookCount, name, fn);
}

gX_I32 gX_Scripting_RegisterDrawHook(const gX_I8 *name, gX_ScriptingFrameFn fn) {
    return gX_Scripting_RegisterFrameHook(g_scripting.drawHooks,
        &g_scripting.drawHookCount, name, fn);
}

gX_I0 gX_Scripting_Draw(gX_I0) {
    if (!g_scripting.loaded) return;
    for (gX_U32 i = 0; i < g_scripting.drawHookCount; i++) {
        if (g_scripting.drawHooks[i].fn) g_scripting.drawHooks[i].fn();
    }
}

gX_I1 gX_Scripting_IsLoaded(const gX_I8 *scriptPath) {
    if (!scriptPath || !scriptPath[0] || !g_scripting.loaded) return 0;
    gX_Script *scripts = (gX_Script*)g_scripting.scripts.data;
    for (gX_U32 i = 0; i < g_scripting.scripts.amount; i++) {
        if (!g_scripting.scripts.meta[i].USED) continue;
        gX_Script *s = &scripts[i];
        if (s->compiled && s->path[0] && strcmp(s->path, scriptPath) == 0) return 1;
    }
    return 0;
}

//run a named function in the script whose path matches scriptPath, on a
//   FRESH context (does not reuse s->context, which may be suspended).
gX_I32 gX_Scripting_RunFunctionByName(const gX_I8 *scriptPath, const gX_I8 *funcName) {
    if (!scriptPath || !funcName || !g_scripting.loaded || !g_scripting.engine) return -1;

    gX_Script *scripts = (gX_Script*)g_scripting.scripts.data;
    gX_Script *match = 0;
    for (gX_U32 i = 0; i < g_scripting.scripts.amount; i++) {
        if (!g_scripting.scripts.meta[i].USED) continue;
        gX_Script *s = &scripts[i];
        if (s->compiled && s->path[0] && strcmp(s->path, scriptPath) == 0) {
            match = s;
            break;
        }
    }
    if (!match || !match->module) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "RunFunctionByName: script '%s' not loaded", scriptPath);
        return -2;
    }

    asIScriptFunction *func = match->module->GetFunctionByName(funcName);
    if (!func) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "RunFunctionByName: function '%s' not found in '%s'", funcName, scriptPath);
        return -3;
    }

    asIScriptContext *ctx = g_scripting.engine->CreateContext();
    if (!ctx) return -4;
    int r = ctx->Prepare(func);
    if (r < 0) {
        ctx->Release();
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "RunFunctionByName: Prepare(%s) failed (%d)", funcName, r);
        return -5;
    }
    r = ctx->Execute();
    if (r == asEXECUTION_EXCEPTION) {
        const gX_I8 *msg = ctx->GetExceptionString();
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "RunFunctionByName: exception in '%s' (%s): %s",
            scriptPath, funcName, msg ? msg : "unknown");
    }
    ctx->Release();
    return (r == asEXECUTION_FINISHED) ? 0 : 1;
}

// ========================================================================
// Lightweight debugger — script list accessors + breakpoints & stepping.
//   The line callback (watchdog) owns the pause; these drive it from the
//   ImGui panel and the script-facing bindings.
// ========================================================================

gX_U32 gX_Scripting_ScriptCount(gX_I0) {
    return g_scripting.scripts.amount;
}

const gX_I8 *gX_Scripting_ScriptPath(gX_U32 scriptIndex) {
    gX_Script *s = gX_Scripting_GetScript(scriptIndex);
    return s ? s->path : "";
}

const gX_I8 *gX_Scripting_ScriptName(gX_U32 scriptIndex) {
    gX_Script *s = gX_Scripting_GetScript(scriptIndex);
    return s ? s->name : "";
}

gX_I1 gX_Scripting_Debug_IsPaused(gX_U32 scriptIndex) {
    gX_Script *s = gX_Scripting_GetScript(scriptIndex);
    return s ? s->debugWait : 0;
}

gX_I1 gX_Scripting_Debug_AnyPaused(gX_I0) {
    gX_Script *scripts = (gX_Script*)g_scripting.scripts.data;
    for (gX_U32 i = 0; i < g_scripting.scripts.amount; i++) {
        if (g_scripting.scripts.meta[i].USED && scripts[i].debugWait) return 1;
    }
    return 0;
}

gX_I32 gX_Scripting_Debug_GetLine(gX_U32 scriptIndex) {
    gX_Script *s = gX_Scripting_GetScript(scriptIndex);
    return s ? s->debugLine : 0;
}

gX_I1 gX_Scripting_Debug_HasBreakpoint(gX_U32 scriptIndex, gX_I32 line) {
    gX_Script *s = gX_Scripting_GetScript(scriptIndex);
    if (!s || line <= 0) return 0;
    for (gX_I32 i = 0; i < GX_SCRIPT_DEBUG_BREAKPOINTS_MAX; i++)
        if (s->debugBreakpoints[i] == line) return 1;
    return 0;
}

gX_I0 gX_Scripting_Debug_ToggleBreakpoint(gX_U32 scriptIndex, gX_I32 line) {
    gX_Script *s = gX_Scripting_GetScript(scriptIndex);
    if (!s || line <= 0) return;
    if (gX_Scripting_Debug_HasBreakpoint(scriptIndex, line)) {
        for (gX_I32 i = 0; i < GX_SCRIPT_DEBUG_BREAKPOINTS_MAX; i++)
            if (s->debugBreakpoints[i] == line) { s->debugBreakpoints[i] = 0; break; }
    } else {
        for (gX_I32 i = 0; i < GX_SCRIPT_DEBUG_BREAKPOINTS_MAX; i++) {
            if (s->debugBreakpoints[i] == 0) { s->debugBreakpoints[i] = line; break; }
        }
    }
}

gX_I0 gX_Scripting_Debug_ClearBreakpoints(gX_U32 scriptIndex) {
    gX_Script *s = gX_Scripting_GetScript(scriptIndex);
    if (!s) return;
    memset(s->debugBreakpoints, 0, sizeof(s->debugBreakpoints));
}

gX_I0 gX_Scripting_Debug_Continue(gX_U32 scriptIndex) {
    gX_Script *s = gX_Scripting_GetScript(scriptIndex);
    if (!s) return;
    //pass the line it paused on once, so resuming does not immediately
    //   re-trigger the same breakpoint (the callback re-fires on resume).
    s->debugSkipLine = s->debugWait ? s->debugLine : 0;
    s->debugWait = 0;
    s->debugAction = GX_SCRIPT_DEBUG_ACTION_CONTINUE;
    s->debugLine = 0;
    s->debugFile[0] = '\0';
}

static gX_I0 gX_Scripting_Debug_StartStep(gX_U32 scriptIndex, gX_I32 action) {
    gX_Script *s = gX_Scripting_GetScript(scriptIndex);
    if (!s) return;
    s->debugWait = 0;
    s->debugAction = action;
    s->debugSkipLine = 0;
    if (s->context) s->debugStackLevel = s->context->GetCallstackSize();
    s->debugLine = 0;
    s->debugFile[0] = '\0';
}

gX_I0 gX_Scripting_Debug_Step(gX_U32 scriptIndex)     { gX_Scripting_Debug_StartStep(scriptIndex, GX_SCRIPT_DEBUG_ACTION_STEP); }
gX_I0 gX_Scripting_Debug_StepOver(gX_U32 scriptIndex) { gX_Scripting_Debug_StartStep(scriptIndex, GX_SCRIPT_DEBUG_ACTION_OVER); }
gX_I0 gX_Scripting_Debug_StepOut(gX_U32 scriptIndex)  { gX_Scripting_Debug_StartStep(scriptIndex, GX_SCRIPT_DEBUG_ACTION_OUT); }

//called from the line callback — decide whether the current line pauses.
static gX_I1 gX_Scripting_Debug_CheckBreakpoint(asIScriptContext *ctx, gX_Script *s) {
    gX_I32 line = ctx->GetLineNumber(0, 0, 0);

    if (s->debugAction == GX_SCRIPT_DEBUG_ACTION_CONTINUE && !s->debugWait) {
        if (!gX_Scripting_Debug_HasBreakpoint((gX_U32)(s - (gX_Script*)g_scripting.scripts.data), line))
            return 0;
        if (s->debugSkipLine == line) {
            s->debugSkipLine = 0;
            return 0;
        }
    } else {
        //step modes pause on the next line at/below the start depth.
        asUINT depth = ctx->GetCallstackSize();
        if (s->debugAction == GX_SCRIPT_DEBUG_ACTION_OVER && depth > s->debugStackLevel) return 0;
        if (s->debugAction == GX_SCRIPT_DEBUG_ACTION_OUT && depth >= s->debugStackLevel) return 0;
    }

    const gX_I8 *file = 0;
    line = ctx->GetLineNumber(0, 0, &file);
    s->debugWait = 1;
    s->debugAction = GX_SCRIPT_DEBUG_ACTION_CONTINUE;
    s->debugLine = line;
    snprintf(s->debugFile, sizeof(s->debugFile), "%s", file ? file : "{unnamed}");
    gX_TraceLog_SCRIPTING(gX_LOG_INFO, "SCRIPT: debug pause '%s' %s:%d",
        s->name, s->debugFile, s->debugLine);
    return 1;
}

//shell harness — failures and the requested exit code feed the process exit.
gX_I32 gX_Scripting_Shell_FailCount(gX_I0) {
    return g_scripting.shellFailures;
}

gX_I32 gX_Scripting_Shell_ExitCode(gX_I0) {
    return g_scripting.shellExitCode;
}

gX_I0 gX_Scripting_Shell_Fail(gX_I0) {
    g_scripting.shellFailures++;
}

gX_I0 gX_Scripting_Shell_SetExitCode(gX_I32 code) {
    g_scripting.shellExitCode = code;
}
