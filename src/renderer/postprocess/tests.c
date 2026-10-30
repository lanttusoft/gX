/**********************************************************************************************
*
*   [gX: src/renderer/postprocess/tests.c]
*   gX — a game engine
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


//ai: postprocess headless tests — chain editing (remove/move/capacity) and the
//   preset file shape. Shader loads need a GL context, so entries here are
//   seeded by hand with shader id 0 (Remove/Move then touch no GL objects).

#include "../postprocess.h"
#include "schema2.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../common/log.h"

gX_I32 gX_Tests_PostProcess(gX_I0)
{
    gX_I32 errors = 0;
    gX_RendererPostProcess pp;
    memset(&pp, 0, sizeof(pp));

    pp.shadersAmount = 4;
    for (gX_U32 i = 0; i < 4; i++) {
        pp.shaders[i].type = (gX_I32)i;
        snprintf((char *)pp.shaders[i].name, sizeof(pp.shaders[i].name),
            "entry%u", (unsigned)i);
    }

    // remove the middle entry -> {0, 2, 3}
    if (gX_RendererPostProcess_Shader_Remove(&pp, 1) != 0
        || pp.shadersAmount != 3 || pp.shaders[0].type != 0
        || pp.shaders[1].type != 2 || pp.shaders[2].type != 3) {
        gX_TraceLog(gX_LOG_ERROR, "PP TEST FAIL: remove middle");
        errors++;
    }
    // move first down -> {2, 0, 3}; then move last up -> {2, 3, 0}
    if (gX_RendererPostProcess_Shader_Move(&pp, 0, 1) != 0
        || pp.shaders[0].type != 2 || pp.shaders[1].type != 0) {
        gX_TraceLog(gX_LOG_ERROR, "PP TEST FAIL: move down");
        errors++;
    }
    if (gX_RendererPostProcess_Shader_Move(&pp, 2, -1) != 0
        || pp.shaders[1].type != 3 || pp.shaders[2].type != 0) {
        gX_TraceLog(gX_LOG_ERROR, "PP TEST FAIL: move up");
        errors++;
    }
    // out-of-range edits are no-ops that report failure
    if (gX_RendererPostProcess_Shader_Remove(&pp, 9) != 1
        || gX_RendererPostProcess_Shader_Move(&pp, 0, -1) != 1
        || gX_RendererPostProcess_Shader_Move(&pp, 2, 1) != 1
        || pp.shadersAmount != 3) {
        gX_TraceLog(gX_LOG_ERROR, "PP TEST FAIL: out-of-range edits");
        errors++;
    }
    // capacity: a full chain rejects the next entry before any GL load
    while (pp.shadersAmount < GX_PP_SHADERS_MAX) {
        pp.shaders[pp.shadersAmount].type = 0;
        pp.shadersAmount++;
    }
    if (gX_RendererPostProcess_Shader_Push(&pp, 0) != 1
        || pp.shadersAmount != GX_PP_SHADERS_MAX) {
        gX_TraceLog(gX_LOG_ERROR, "PP TEST FAIL: chain capacity guard");
        errors++;
    }
    // reload bounds: out-of-range entries report failure without touching GL
    if (gX_RendererPostProcess_Shader_Reload(&pp, 99) != 1) {
        gX_TraceLog(gX_LOG_ERROR, "PP TEST FAIL: reload bounds");
        errors++;
    }
    // the shipped shaders declare uniforms[16]; the engine capacity must match
    if (GX_PP_UNIFORM_AMOUNT < 16) {
        gX_TraceLog(gX_LOG_ERROR,
            "PP TEST FAIL: uniform capacity %d < 16", GX_PP_UNIFORM_AMOUNT);
        errors++;
    }
    // inject slot assignment bounds (no path -> clears)
    gX_RendererPostProcess_Inject_SetTexturePath(&pp, -1, "x");
    gX_RendererPostProcess_Inject_SetTexturePath(&pp, GX_PP_INJECT_SLOTS, "x");
    if (pp.inject.texturePaths[0][0] != '\0') {
        gX_TraceLog(gX_LOG_ERROR, "PP TEST FAIL: inject slot bounds");
        errors++;
    }

    // preset file shape: the saved JSON carries the sections the loader reads
    {
        //ai: honor the harness scratch contract (GX_SCRATCH) instead of the
        //   hardcoded /tmp/opencode path.
        const gX_I8 *scratch = getenv("GX_SCRATCH");
        if (!scratch || !scratch[0]) scratch = "/tmp/opencode";
        gX_I8 path[256];
        //ai: per-process filename — parallel suite instances (concurrent audit
        //   captures) must not clobber each other's preset round-trip file.
        snprintf((char *)path, sizeof(path),
            "%s/pp_tests_roundtrip_%ld.json", scratch, (long)getpid());
        if (gX_RendererPostProcess_Preset_Save_New(&pp, path) != 0) {
            gX_TraceLog(gX_LOG_ERROR, "PP TEST FAIL: preset save");
            errors++;
        } else {
            FILE *file = fopen((const char *)path, "rb");
            if (!file) {
                gX_TraceLog(gX_LOG_ERROR, "PP TEST FAIL: preset file missing");
                errors++;
            } else {
                fseek(file, 0, SEEK_END);
                long size = ftell(file);
                fseek(file, 0, SEEK_SET);
                if (size <= 0 || size > (1 << 20)) {
                    gX_TraceLog(gX_LOG_ERROR,
                        "PP TEST FAIL: unexpected preset size %ld", size);
                    errors++;
                } else {
                    gX_I8 *text = (gX_I8 *)malloc((size_t)size + 1);
                    if (!text || fread(text, 1, (size_t)size, file)
                            != (size_t)size) {
                        gX_TraceLog(gX_LOG_ERROR,
                            "PP TEST FAIL: preset read-back");
                        errors++;
                    } else {
                        text[size] = '\0';
                        if (!strstr((const char *)text, "\"shaders\"")
                            || !strstr((const char *)text, "\"inject\"")
                            || !strstr((const char *)text, "\"texture\"")) {
                            gX_TraceLog(gX_LOG_ERROR,
                                "PP TEST FAIL: preset sections missing");
                            errors++;
                        }
                    }
                    free(text);
                }
                fclose(file);
            }
        }
        remove((char *)path);
    }

    if (errors == 0)
        gX_TraceLog(gX_LOG_INFO, "PP_TESTS: all passed");
    return errors;
}
