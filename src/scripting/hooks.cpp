/**********************************************************************************************
*
*   [gX: src/scripting/hooks.cpp]
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


//ai: ported from gameX src/scripting/hooks.cpp — registry storage (dedupe by
//   name+function, AddRef on store) and firing on fresh one-shot contexts.
#include "hooks.h"

#include <string.h>

#include <angelscript.h>

#include "../common/log.h"

gX_I32 gX_Scripting_Hook_Register(const gX_I8 *name, asIScriptFunction *func) {
    if (!name || !func) return -1;

    for (gX_U32 i = 0; i < g_scripting.hooksCount; i++) {
        if (strcmp(g_scripting.hooks[i].name, name) == 0 &&
            g_scripting.hooks[i].func == func) {
            return 0;
        }
    }

    if (g_scripting.hooksCount >= GX_SCRIPT_MAX_HOOKS) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "hook limit reached (%d)", GX_SCRIPT_MAX_HOOKS);
        return -2;
    }

    gX_ScriptHook *hook = &g_scripting.hooks[g_scripting.hooksCount++];
    strncpy(hook->name, name, sizeof(hook->name) - 1);
    hook->name[sizeof(hook->name) - 1] = '\0';
    hook->func = func;
    func->AddRef();

    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "registered hook '%s'", name);
    return 0;
}

gX_I32 gX_Scripting_Hook_Trigger(const gX_I8 *name) {
    if (!name) return -1;
    if (!g_scripting.engine) return -1;

    int count = 0;
    for (gX_U32 i = 0; i < g_scripting.hooksCount; i++) {
        if (strcmp(g_scripting.hooks[i].name, name) != 0) continue;

        asIScriptContext *ctx = g_scripting.engine->CreateContext();
        if (!ctx) continue;

        int r = ctx->Prepare(g_scripting.hooks[i].func);
        if (r >= 0) {
            r = ctx->Execute();
            if (r == asEXECUTION_EXCEPTION) {
                gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "hook '%s' exception: %s",
                    name, ctx->GetExceptionString());
            }
        }
        ctx->Release();
        count++;
    }

    return count;
}

gX_I32 gX_Scripting_Hook_RemoveAll(const gX_I8 *name) {
    int removed = 0;
    gX_U32 writeIdx = 0;
    for (gX_U32 i = 0; i < g_scripting.hooksCount; i++) {
        if (name && strcmp(g_scripting.hooks[i].name, name) == 0) {
            g_scripting.hooks[i].func->Release();
            removed++;
            continue;
        }
        if (writeIdx != i) {
            g_scripting.hooks[writeIdx] = g_scripting.hooks[i];
        }
        writeIdx++;
    }
    g_scripting.hooksCount = writeIdx;
    return removed;
}
