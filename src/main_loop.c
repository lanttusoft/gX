/**********************************************************************************************
*
*   [gX: src/main_loop.c]
*   Looping stage (looptime)
*
*   Copyright (c) 2026 lanttu
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


#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "main.h"
#include "common/debug.h"
#include "input.h"
#include "rhi/rhi.h"
#include "rhi/gl_state.h"
#include "rhi/rhi_matrix.h"
#include "rhi/rhi_state.h"
#include "wrappers/wrappers.h"

#include "audio.h"
#include "camera.h"
#include "settings.h"

#include "gui/gui.h"

#if gX_ENABLE_RENDERER == 1
    #include "renderer/renderer.h"
#endif
#if gX_ENABLE_PHYSICS == 1
    #include "physics/physics.h"
#endif
#if gX_ENABLE_SCENE == 1
    #include "scene/scene.h"
#endif

#include "module/module.h"

#include "resource/resource.h"

#include "common/enum.h"
#include "common/log.h"
#include "common/macros.h"
#include "common/profiler.h"
#include "common/random.h"
#include "common/utils.h"
#include "rhi/renderdoc_hook.h"

#include "game/game.h"

//opencode: profiler checkpoints wired into main loop
#define GX_PROFILER_CHECK(func, name) \
    do { func; gX_Profiler_Check(name); } while(0)

static gX_I32 gX_Loop_Update(gX_I0) {
    gX_I32 errors = 0;
    
    //no backend
    //the order should be such that whatever depends on the previous modules,
    //should be updated after those
    //ex. gui may depend on audio, so that audio gets updated before gui
    
    GX_PROFILER_CHECK( gX_Profiler_Reset(),                                   "begin" );
    GX_PROFILER_CHECK( gX_MainData_Update_Before(),                           "maindata" );
    GX_PROFILER_CHECK( gX_ResourceManager_Update(),                           "resource" );
    GX_PROFILER_CHECK( gX_GameData_Update(),                                  "gamedata" );
    GX_PROFILER_CHECK( gX_Input_Update(),                                     "input" );
    GX_PROFILER_CHECK( gX_Camera_Update(),                                    "camera" );
    GX_PROFILER_CHECK( gX_Random_Update(),                                    "random" );
#if gX_ENABLE_PHYSICS == 1
    //ai: step the shared world before scenes sync their transforms
    GX_PROFILER_CHECK( gX_Physics_Update(),                                   "physics" );
#endif
#if gX_ENABLE_SCENE == 1
    GX_PROFILER_CHECK( gX_Scene_Update(),                                     "scene" );
#endif
    //opencode: dynamic modules — after the scene so hooks see the current
    //   state, before renderer/gui so module changes apply in the same frame
    //   (gx_shell: scripting main() + console one-shots).
    GX_PROFILER_CHECK( gX_Module_Update(),                                    "modules" );
#if gX_ENABLE_RENDERER == 1
    GX_PROFILER_CHECK( gX_Renderer_Update(),                                  "renderer" );
#endif
    GX_PROFILER_CHECK( gX_Audio_Update(),                                     "audio" );
    GX_PROFILER_CHECK( gX_GUI_Update(),                                       "gui" );
    GX_PROFILER_CHECK( gX_Settings_Update(),                                  "settings" );
    errors = gX_MainData_Update_After();
    GX_PROFILER_CHECK( gX_Debug_Update(),                                     "debug" );
    gX_Profiler_Check("update end");
    
    //backend
    if (!(maindata.backendLoaded)) return errors;
    return errors;
}

static gX_I32 gX_Loop_Draw(gX_I0) {
    gX_I32 errors = 0;
    gX_I1 drawDiag = gX_RHI_Diagnostics_IsEnabled();
    gX_GL_State frameBefore = {0};

    if (!(maindata.backendLoaded)) return errors;
    gX_Profiler_Check("draw begin");
    gX_Drawing_Begin();
    if (drawDiag) {
        gX_GL_State_Save(&frameBefore);
        if (gX_GL_State_GetDepth() != -1)
            gX_TraceLog(gX_LOG_ERROR, "GL_AUDIT frame begin: state depth=%d", gX_GL_State_GetDepth());
    }
    
        GX_PROFILER_CHECK( gX_Drawing_ClearBG(maindata.bgColor),              "clear bg" );
#if gX_ENABLE_SCENE == 1
        gX_GL_State sceneBefore = {0};
        if (drawDiag) gX_GL_State_Save(&sceneBefore);
        GX_PROFILER_CHECK( gX_Scene_Draw(),                                   "scene draw" );
        if (drawDiag) {
            gX_GL_State sceneAfter = {0};
            gX_GL_State_Save(&sceneAfter);
            gX_GL_State_Diff(&sceneBefore, &sceneAfter, "scene_draw");
        }
#endif
#if gX_ENABLE_RENDERER == 1
        gX_GL_State rendererBefore = {0};
        if (drawDiag) gX_GL_State_Save(&rendererBefore);
        GX_PROFILER_CHECK( gX_Renderer_Draw(),                                "renderer draw" );
        if (drawDiag) {
            gX_GL_State rendererAfter = {0};
            gX_GL_State_Save(&rendererAfter);
            gX_GL_State_Diff(&rendererBefore, &rendererAfter, "renderer_draw");
        }
#endif
        GX_PROFILER_CHECK( gX_GUI_Draw(),                                     "gui draw" );
        //opencode: module draw hooks run after the GUI so tour overlays and
        //   captions sit on top of every layer.
        gX_Module_Draw();

        //opencode: --layout-dump — one structured element-rect dump at a stable
        //   frame; the data source for UI diff/layout report tools.
        if (arguments.hasLayoutDump && maindata.frameCount ==
                (gX_U64)(arguments.layoutDumpFrame > 0
                    ? arguments.layoutDumpFrame : 8)) {
            extern gX_I32 gX_UI_Layout_Dump(const gX_I8 *path);
            gX_UI_Layout_Dump(arguments.layoutDumpPath);
        }

    if (drawDiag) {
        gX_GL_State frameAfter = {0};
        gX_GL_State_Save(&frameAfter);
        gX_GL_State_Diff(&frameBefore, &frameAfter, "frame_draw");
        if (gX_GL_State_GetDepth() != -1)
            gX_TraceLog(gX_LOG_ERROR, "GL_AUDIT frame end: state depth=%d", gX_GL_State_GetDepth());
        for (gX_U32 mode = 0; mode <= GX_RHI_MATRIX_TEXTURE; mode++) {
            if (gX_RHI_Matrix_GetStackDepth(mode) != 0)
                gX_TraceLog(gX_LOG_ERROR, "GL_AUDIT frame end: matrix mode=%u depth=%d",
                mode, gX_RHI_Matrix_GetStackDepth(mode));
        }
    }
    gX_Drawing_End();

    gX_Profiler_Check("draw end");
    return errors;
}

gX_I32 gX_Loop(gX_I0);
gX_I32 gX_Loop(gX_I0) {
    gX_I32 errors = 0;
    
    gX_TraceLog(gX_LOG_MAGENTA,"//////////////////////");
    
    //opencode: v0.1.33 — GX_FRAMES env hook: deterministic frame budget for
    //   trace captures (apitrace/RenderDoc) and CI render tests.
    {
        const char *fr = getenv("GX_FRAMES");
        const char *benchmarkPath = getenv("GX_BENCHMARK_CPU_CSV");
        FILE *benchmarkFile = NULL;
        if (benchmarkPath && benchmarkPath[0]) {
            benchmarkFile = fopen(benchmarkPath, "w");
            if (benchmarkFile) {
                fprintf(benchmarkFile, "frame,cpu_ms\n");
                fflush(benchmarkFile);
            } else {
                gX_TraceLog(gX_LOG_ERROR, "BENCHMARK: cannot open CPU timing CSV %s",
                    benchmarkPath);
            }
        }
        static gX_I32 s_frameBudget = 0;
        if (fr && fr[0]) s_frameBudget = atoi(fr);
        while (maindata.loop) {
            struct timespec frameStart;
            struct timespec frameEnd;
            if (benchmarkFile) clock_gettime(CLOCK_MONOTONIC, &frameStart);
            maindata.frameCount++;
            errors = gX_Loop_Update();
            errors = gX_Loop_Draw();
            if (benchmarkFile) {
                clock_gettime(CLOCK_MONOTONIC, &frameEnd);
                gX_F64 startSeconds = (gX_F64)frameStart.tv_sec
                    + (gX_F64)frameStart.tv_nsec / 1000000000.0;
                gX_F64 endSeconds = (gX_F64)frameEnd.tv_sec
                    + (gX_F64)frameEnd.tv_nsec / 1000000000.0;
                fprintf(benchmarkFile, "%llu,%.4f\n",
                    (unsigned long long)maindata.frameCount,
                    (endSeconds - startSeconds) * 1000.0);
            }
            //opencode: hunt - RenderDoc frame trigger: GX_RD_FRAME=N fires
            //   the in-app capture at frame N (headless scenes never reach
            //   the SDF-flush trigger). GX_FRAMES should exceed N slightly
            //   so the capture finalizes before teardown.
            {
                const char *rf = getenv("GX_RD_FRAME");
                if (rf && rf[0] && (gX_I64)maindata.frameCount >= (gX_I64)atoi(rf))
                    gX_RenderDoc_Trigger();
            }
            if (s_frameBudget > 0 && (gX_I64)maindata.frameCount >= (gX_I64)s_frameBudget) maindata.loop = false;
        }
        if (benchmarkFile) fclose(benchmarkFile);
    }
    
    gX_TraceLog(gX_LOG_MAGENTA,"//////////////////////");
    
    return errors;
}
