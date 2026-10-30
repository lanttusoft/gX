/**********************************************************************************************
*
*   [gX: src/main.c]
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


#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "main.h"
#include "common/types.h"
#include "common/debug.h"

#include "module/module.h"

#include <stdio.h>
#include "common/log.h"
#include "camera.h"
#include "input.h"

extern gX_I32 gX_Parse( int argc, char **argv);
extern gX_I32 gX_Tests( gX_I0);
extern gX_I32 gX_Load(  gX_I0);
extern gX_I32 gX_Unload(gX_I0);
extern gX_I32 gX_Loop(  gX_I0);

gX_MainData maindata;

#if gX_ENABLE_RENDERER == 1
#endif //gX_ENABLE_RENDERER

//ai: make the executable relocatable when launched outside the repository root.
//   Paths such as resources/ are intentionally relative to the project root;
//   resolve that root from /proc/self/exe before settings/tests touch them.
static gX_I1 gX_Main_HasResources(const gX_I8 *root) {
    gX_I8 probe[PATH_MAX];
    if (snprintf(probe, sizeof(probe), "%s/resources", root) >= (gX_I32)sizeof(probe))
        return false;
    return access(probe, R_OK | X_OK) == 0;
}

static gX_I0 gX_Main_ResolveResourceRoot(const gX_I8 *argv0) {
    gX_I8 exe[PATH_MAX];
    gX_I8 base[PATH_MAX];
    ssize_t len;
    gX_I32 level;

    if (gX_Main_HasResources(".")) return;

    len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len <= 0) {
        if (!argv0 || !argv0[0]) return;
        len = (ssize_t)strlen(argv0);
        if (len >= (ssize_t)sizeof(exe)) return;
        memcpy(exe, argv0, (size_t)len);
        exe[len] = '\0';
    } else {
        exe[len] = '\0';
    }

    for (level = 1; level <= 4; level++) {
        gX_I8 *slash;
        gX_I32 strip;
        snprintf(base, sizeof(base), "%s", exe);
        for (strip = 0; strip < level; strip++) {
            slash = strrchr(base, '/');
            if (!slash) break;
            *slash = '\0';
        }
        if (slash && gX_Main_HasResources(base) && chdir(base) == 0) {
            fprintf(stderr, "MAIN: resource root: %s\n", base);
            return;
        }
    }
}

#if gX_ENABLE_RENDERER == 1
#endif //gX_ENABLE_RENDERER

//ai: keep the SymPy corpus-dump entry point in Debug, not the lean Release image.
int main(int argc, char **argv) {
    gX_I32 errors = 0;
    gX_I32 scriptExit = 0;

    //opencode: crash handler (registers + stack) lives in common/debug.c
    gX_Debug_Init();

    errors = gX_Parse(argc,argv);
    if (errors > 0) goto parse_ret;

    //opencode: apply logLevel from arguments
    if (arguments.hasLogLevel) {
        gX_LogState.globalLevel = arguments.logLevel;
    }

    gX_Main_ResolveResourceRoot(argv[0]);

#ifndef GX_LEAN_RELEASE
    //opencode: GX_MATH_DUMP=1 emits the gxmath corpus for the CAS math pass
    //   (scripts/math_pass.py verifies it against SymPy) and exits before any
    //   window/GPU work.
    {
        const gX_I8 *mathDump = getenv("GX_MATH_DUMP");
        if (mathDump && mathDump[0] == '1') {
            extern gX_I32 gX_Tests_MathDump(gX_I0);
            gX_Tests_MathDump();
            return 0;
        }
    }
#endif

    //opencode: GPU capture harnesses validate rendering independently from
    //   headless unit regressions; --test-only always runs the full suite.
    const gX_I8 *skipTests = getenv("GX_SKIP_TESTS");
    if (arguments.hasTestOnly || !skipTests || strcmp(skipTests, "1") != 0)
        errors = gX_Tests();
    else
        errors = 0;
    //opencode: --test-only runs tests and exits immediately (no window, no GPU)
    if (arguments.hasTestOnly) {
        gX_TraceLog(gX_LOG_INFO, "TEST_ONLY: %d errors — exiting", errors);
        return errors;
    }
    if (errors > 0) goto error;
    //opencode: --es-dump-shader writes the ES-rewritten frag source to a file
    //   and exits. The soft-shader oracle harness compiles it on a real ES
    //   context and compares against the CPU reference (validates the ES
    //   rewrite itself). Output path: --es-dump-shader <in.frag> <out>
    if (arguments.hasEsDumpShader) {
        extern gX_I8 *gX_Shader_Compiler_Preprocess(const gX_I8 *path, const gX_I8 *defines, gX_U64 *outLen);
        extern gX_I0 gX_Shader_Compiler_FreeSource(gX_I8 *source);
        extern gX_I0 gX_Shader_Compiler_SetESMode(gX_I1 on);
        gX_Shader_Compiler_SetESMode(1);
        gX_U64 len = 0;
        gX_I8 *src = gX_Shader_Compiler_Preprocess(arguments.esDumpPath, NULL, &len);
        if (!src) {
            gX_TraceLog(gX_LOG_ERROR, "ES_DUMP: preprocess failed for '%s'", arguments.esDumpPath);
            return 1;
        }
        FILE *f = fopen(arguments.esDumpOutPath, "wb");
        if (!f) {
            gX_TraceLog(gX_LOG_ERROR, "ES_DUMP: cannot write '%s'", arguments.esDumpOutPath);
            gX_Shader_Compiler_FreeSource(src);
            return 1;
        }
        fwrite(src, 1, len - 1, f);
        fclose(f);
        gX_Shader_Compiler_FreeSource(src);
        gX_TraceLog(gX_LOG_INFO, "ES_DUMP: wrote %s -> %s", arguments.esDumpPath, arguments.esDumpOutPath);
        return 0;
    }
    //opencode: --vk-dump-shader <in> <out> [--vk-dump-vert] — preprocess in
    //   Vulkan mode + apply the dialect rewrite, write the result for
    //   glslangValidator verification (Stage 1C). --vk-dump-vert selects the
    //   vertex stage (attributes 0-5); default is the fragment stage.
    if (arguments.hasVkDumpShader) {
        extern gX_I8 *gX_Shader_Compiler_Preprocess(const gX_I8 *path, const gX_I8 *defines, gX_U64 *outLen);
        extern gX_I0 gX_Shader_Compiler_FreeSource(gX_I8 *source);
        extern gX_I0 gX_Shader_Compiler_SetVulkanMode(gX_I1 on);
        extern gX_I8 *gX_Shader_Compiler_ToVulkanDialect(const gX_I8 *src, gX_U64 srcLen, gX_I1 isVertex);
        gX_Shader_Compiler_SetVulkanMode(1);
        gX_U64 len = 0;
        gX_I8 *src = gX_Shader_Compiler_Preprocess(arguments.vkDumpPath, NULL, &len);
        if (!src) {
            gX_TraceLog(gX_LOG_ERROR, "VK_DUMP: preprocess failed for '%s'", arguments.vkDumpPath);
            return 1;
        }
        gX_I8 *dialect = gX_Shader_Compiler_ToVulkanDialect(src, len, arguments.vkDumpIsVertex);
        gX_Shader_Compiler_FreeSource(src);
        if (!dialect) {
            gX_TraceLog(gX_LOG_ERROR, "VK_DUMP: dialect transform failed for '%s'", arguments.vkDumpPath);
            return 1;
        }
        FILE *f = fopen(arguments.vkDumpOutPath, "wb");
        if (!f) {
            gX_TraceLog(gX_LOG_ERROR, "VK_DUMP: cannot write '%s'", arguments.vkDumpOutPath);
            free(dialect);
            return 1;
        }
        fwrite(dialect, 1, strlen(dialect), f);
        fclose(f);
        free(dialect);
        gX_TraceLog(gX_LOG_INFO, "VK_DUMP: wrote %s -> %s", arguments.vkDumpPath, arguments.vkDumpOutPath);
        return 0;
    }

    errors = gX_Load();
    if (errors > 0) goto error;

#if gX_ENABLE_RENDERER == 1
    //ai: standalone screen-code presentation; owns its loop and returns to unload.
    if (arguments.hasColorCode) {
        extern gX_I32 gX_ColorCode_Present(const gX_I8 *payload, gX_I1 tv);
        errors = gX_ColorCode_Present(arguments.colorCodePayload,
            arguments.colorCodeTv);
        goto error;
    }
#endif

    errors = gX_Loop();

    //opencode: module harness — a shell script's Quit() code (or any failed
    //   assert) drives the process exit code so gate scripts read pass/fail
    //   from the return.
    scriptExit = gX_Module_ExitCode();

    //opencode: flush the Chrome trace after the loop (no-op unless
    //   GX_PROFILE_TRACE was set)
    {
        extern gX_I0 gX_Profiler_TraceWrite(gX_I0);
        gX_Profiler_TraceWrite();
    }

error:
    {
        //opencode: keep the boot/loop failure code — the unload result only
        //   surfaces when nothing else failed (was: unload overwrote every
        //   boot error, so a failed load still exited 0).
        gX_I32 earlierErrors = errors;
        gX_I32 unloadErrors = gX_Unload();
        errors = earlierErrors > 0 ? earlierErrors : unloadErrors;
    }

    if (scriptExit > 0) errors = scriptExit;

    //opencode: GX_MEM_DEBUG=1 dumps the allocator accounting (and any live
    //   allocations) after the unload, where leaks are real
    {
        const gX_I8 *memDebug = getenv("GX_MEM_DEBUG");
        if (memDebug && memDebug[0] == '1') {
            extern gX_U32 gX_Alloc_Dump(gX_I0);
            gX_Alloc_Dump();
        }
    }
parse_ret:
    return errors;
}
