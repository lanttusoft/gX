/**********************************************************************************************
*
*   [gX: src/scripting/script.cpp]
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


//ai: ported from gameX src/scripting/script.cpp — file loading (optionally
//   with library files prepended), AngelScript module compilation and the
//   per-script context, including the watchdog line callback hookup.
#include "script.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <angelscript.h>

#include "../common/log.h"

static gX_I64 gX_Script_GetFileMtime(const gX_I8 *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (gX_I64)st.st_mtime;
}

static gX_I0 gX_Script_SetNameFromPath(gX_Script *s, const gX_I8 *path) {
    const gX_I8 *base = strrchr(path, '/');
    if (!base) base = strrchr(path, '\\');
    if (!base) base = path; else base++;
    strncpy(s->name, base, GX_SCRIPT_NAME_MAX - 1);
    s->name[GX_SCRIPT_NAME_MAX - 1] = '\0';
    gX_I8 *dot = strrchr(s->name, '.');
    if (dot) *dot = '\0';
}

gX_I32 gX_Script_Init(gX_Script *s) {
    memset(s, 0, sizeof(gX_Script));
    return 0;
}

gX_I32 gX_Script_LoadFile(gX_Script *s, const gX_I8 *path) {
    gX_TRACELOG_PROLOGUE("ScriptLoadFile", "SCRIPT: Loading script file")
    if (!path || !path[0]) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "no path provided");
        gX_TRACELOG_EPILOGUE("SCRIPT: Load failed")
        return -1;
    }
    strncpy(s->path, path, GX_SCRIPT_PATH_MAX - 1);
    s->path[GX_SCRIPT_PATH_MAX - 1] = '\0';

    FILE *f = fopen(path, "rb");
    if (!f) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "failed to open '%s'", path);
        gX_TRACELOG_EPILOGUE("SCRIPT: Load failed")
        return -2;
    }

    fseek(f, 0, SEEK_END);
    s->length = (gX_U32)ftell(f);
    fseek(f, 0, SEEK_SET);

    if (s->text) free(s->text);
    s->text = (gX_I8*)malloc(s->length + 1);
    if (!s->text) {
        fclose(f);
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "malloc failed");
        gX_TRACELOG_EPILOGUE("SCRIPT: Load failed")
        return -3;
    }

    size_t readBytes = fread(s->text, 1, s->length, f);
    fclose(f);
    s->text[readBytes] = '\0';

    gX_Script_SetNameFromPath(s, path);

    s->loaded = true;
    s->mtime = gX_Script_GetFileMtime(path);
    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "loaded '%s' (%u bytes)", path, s->length);
    gX_TRACELOG_EPILOGUE("SCRIPT: Load done")
    return 0;
}

gX_I32 gX_Script_LoadFileWithLibs(gX_Script *s, const gX_I8 *mainPath, const gX_I8 *libPaths[], gX_U32 libCount) {
    if (libCount == 0) return gX_Script_LoadFile(s, mainPath);

    typedef struct { gX_I8 *data; gX_U32 len; } LibBuf;
    LibBuf *libs = (LibBuf*)calloc(libCount, sizeof(LibBuf));
    if (!libs) return gX_Script_LoadFile(s, mainPath);
    gX_U32 totalLibLen = 0;
    gX_I1 allOk = 1;

    for (gX_U32 i = 0; i < libCount; i++) {
        FILE *f = fopen(libPaths[i], "rb");
        if (!f) {
            gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "lib file '%s' not found", libPaths[i]);
            allOk = 0;
            break;
        }
        fseek(f, 0, SEEK_END);
        libs[i].len = (gX_U32)ftell(f);
        fseek(f, 0, SEEK_SET);
        libs[i].data = (gX_I8*)malloc(libs[i].len);
        if (!libs[i].data) { fclose(f); allOk = 0; break; }
        size_t got = fread(libs[i].data, 1, libs[i].len, f);
        (void)got;
        fclose(f);
        totalLibLen += libs[i].len;
    }

    if (!allOk) {
        for (gX_U32 i = 0; i < libCount; i++) { if (libs[i].data) free(libs[i].data); }
        free(libs);
        return gX_Script_LoadFile(s, mainPath);
    }

    FILE *fMain = fopen(mainPath, "rb");
    if (!fMain) {
        for (gX_U32 i = 0; i < libCount; i++) { if (libs[i].data) free(libs[i].data); }
        free(libs);
        return -1;
    }
    fseek(fMain, 0, SEEK_END);
    gX_U32 mainLen = (gX_U32)ftell(fMain);
    fseek(fMain, 0, SEEK_SET);

    gX_U32 totalLen = totalLibLen + libCount + mainLen + 1;
    gX_I8 *combined = (gX_I8*)malloc(totalLen);
    if (!combined) {
        fclose(fMain);
        for (gX_U32 i = 0; i < libCount; i++) { if (libs[i].data) free(libs[i].data); }
        free(libs);
        return -3;
    }

    gX_U32 offset = 0;
    for (gX_U32 i = 0; i < libCount; i++) {
        memcpy(combined + offset, libs[i].data, libs[i].len);
        offset += libs[i].len;
        combined[offset++] = '\n';
        free(libs[i].data);
    }
    free(libs);

    size_t got = fread(combined + offset, 1, mainLen, fMain);
    offset += (gX_U32)got;
    combined[offset] = '\0';
    fclose(fMain);

    if (s->text) free(s->text);
    s->text = combined;
    s->length = offset;

    gX_Script_SetNameFromPath(s, mainPath);

    s->loaded = true;
    s->mtime = gX_Script_GetFileMtime(mainPath);
    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "loaded '%s' + %u libs (%u bytes)", mainPath, libCount, s->length);
    return 0;
}

gX_I32 gX_Script_LoadFileWithLib(gX_Script *s, const gX_I8 *mainPath, const gX_I8 *libPath) {
    const gX_I8 *libs[1] = { libPath };
    return gX_Script_LoadFileWithLibs(s, mainPath, libs, 1);
}

gX_I32 gX_Script_CompileModule(gX_Script *s, struct asIScriptEngine *engine) {
    gX_TRACELOG_PROLOGUE("ScriptCompile", "SCRIPT: Compiling script")
    if (!s->loaded || !s->text) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "script not loaded");
        gX_TRACELOG_EPILOGUE("SCRIPT: Compile failed")
        return -1;
    }

    if (s->module) {
        s->module->Discard();
        s->module = 0;
    }

    s->module = engine->GetModule(s->name, asGM_ALWAYS_CREATE);
    if (!s->module) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "failed to create module");
        gX_TRACELOG_EPILOGUE("SCRIPT: Compile failed")
        return -2;
    }

    int r = s->module->AddScriptSection("main", s->text, s->length);
    if (r < 0) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "AddScriptSection failed (%d)", r);
        gX_TRACELOG_EPILOGUE("SCRIPT: Compile failed")
        return -3;
    }

    r = s->module->Build();
    if (r < 0) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "Build() failed (%d)", r);
        gX_TRACELOG_EPILOGUE("SCRIPT: Compile failed")
        return -4;
    }

    s->compiled = true;
    gX_TRACELOG_EPILOGUE("SCRIPT: Compile done")
    return 0;
}

gX_I32 gX_Script_CreateContext(gX_Script *s, struct asIScriptEngine *engine) {
    if (s->context) {
        s->context->Release();
        s->context = 0;
    }

    s->context = engine->CreateContext();
    if (!s->context) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "failed to create context");
        return -1;
    }

    //runaway-script watchdog — the line callback counts executed lines and
    //   suspends + kills a script that exceeds the per-frame budget without a
    //   Sleep (see gX_Scripting_LineCallback).
    extern void gX_Scripting_LineCallback(asIScriptContext *ctx);
    s->context->SetLineCallback(asFUNCTION(gX_Scripting_LineCallback), 0, asCALL_CDECL);
    return 0;
}

gX_I32 gX_Script_RunFunction(gX_Script *s, const gX_I8 *decl) {
    if (!s->compiled || !s->module) return -1;
    if (!s->context) return -2;

    asIScriptFunction *func = s->module->GetFunctionByDecl(decl);
    if (!func) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "function '%s' not found in '%s'", decl, s->name);
        return -3;
    }

    int r = s->context->Prepare(func);
    if (r < 0) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "Prepare() failed (%d)", r);
        return -4;
    }

    r = s->context->Execute();
    if (r == asEXECUTION_EXCEPTION) {
        s->exceptionLine = s->context->GetExceptionLineNumber();
        const gX_I8 *msg = s->context->GetExceptionString();
        strncpy(s->exceptionMessage, msg ? msg : "unknown", sizeof(s->exceptionMessage) - 1);
        s->exceptionMessage[sizeof(s->exceptionMessage) - 1] = '\0';
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "exception in '%s' line %d: %s",
                              s->name, s->exceptionLine, s->exceptionMessage);
        return -5;
    }

    //pass through the execution result (0=finished, 1=suspended, ...) so the
    //   caller can detect suspension and resume later.
    return r;
}
