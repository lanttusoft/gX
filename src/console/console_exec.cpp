/**********************************************************************************************
*
*   [gX: src/console/console_exec.cpp]
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


//ai: console <-> AngelScript bridge — wraps console input in void main() as a
//   throwaway module, runs it, then discards the module (gameX leaked the
//   console_N modules; gX releases each one after execution).
#include "console.h"

#include <stdio.h>
#include <string.h>

#include <angelscript.h>

#include "../scripting.h"

static gX_U32 g_consoleModuleCounter = 0;

gX_I32 gX_Console_ExecScript(const gX_I8 *line) {
    if (!line || !line[0]) return -1;
    if (!g_scripting.loaded || !g_scripting.engine) return -2;

    gX_I8 moduleName[64];
    snprintf(moduleName, sizeof(moduleName), "console_%u", g_consoleModuleCounter++);

    asIScriptModule *mod = g_scripting.engine->GetModule(moduleName, asGM_ALWAYS_CREATE);
    if (!mod) return -3;

    gX_I8 expr[GX_CONSOLE_INPUT_MAX];
    snprintf(expr, sizeof(expr), "%s", line);
    size_t exprLen = strlen(expr);
    while (exprLen > 0 && (expr[exprLen - 1] == ' ' || expr[exprLen - 1] == '\t')) {
        expr[--exprLen] = '\0';
    }
    if (exprLen > 0 && expr[exprLen - 1] != ';' && expr[exprLen - 1] != '}'
        && exprLen < sizeof(expr) - 1) {
        expr[exprLen] = ';';
        expr[exprLen + 1] = '\0';
    }

    gX_I8 code[GX_CONSOLE_INPUT_MAX + 64];
    snprintf(code, sizeof(code), "void main() {\n%s\n}\n", expr);

    int r = mod->AddScriptSection("console", code, strlen(code));
    if (r < 0) {
        gX_Console_AddLog("compile failed: %s", g_scripting.lastError);
        mod->Discard();
        return -4;
    }

    r = mod->Build();
    if (r < 0) {
        gX_Console_AddLog("compile failed: %s", g_scripting.lastError);
        mod->Discard();
        return -5;
    }

    asIScriptFunction *func = mod->GetFunctionByDecl("void main()");
    if (!func) {
        mod->Discard();
        return -6;
    }

    asIScriptContext *ctx = g_scripting.engine->CreateContext();
    if (!ctx) {
        mod->Discard();
        return -7;
    }

    r = ctx->Prepare(func);
    if (r >= 0) r = ctx->Execute();
    if (r == asEXECUTION_EXCEPTION) {
        const gX_I8 *msg = ctx->GetExceptionString();
        gX_Console_AddLog("script exception: %s", msg ? msg : "unknown");
    }
    ctx->Release();
    mod->Discard();

    return (r == asEXECUTION_FINISHED) ? 0 : 1;
}
