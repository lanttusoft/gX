/**********************************************************************************************
*
*   [gX: src/main_update.c]
*   Update stage — scene switching, refresh, screenshot
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


#include "main.h"
#include <stdlib.h>
#include "wrappers/wrappers.h"

#include "gui/gui.h"
#include "input.h"
#if gX_ENABLE_RENDERER == 1
    #include "renderer/renderer.h"
    #include "renderer/gfx/boilerplate.h"
#endif
#if gX_ENABLE_SCENE == 1
    #include "scene/scene.h"
#endif
#include "module/module.h"
#include "camera.h"

#include "common/enum.h"
#include "common/log.h"
#include "common/utils.h"

gX_I32 gX_MainData_Switch(gX_I32 masterIndex) {
    gX_TraceLog_MAIN(gX_LOG_INFO,"Switching masterindex to %i [%s]",masterIndex,
                gX_Scenes_String(masterIndex));
    maindata.masterIndex = masterIndex;
    maindata.masterIndexSwitch = true;
    maindata.masterIndexSwitched = false;
    gui.directdraw.drawLoadingScreen = true;
    return 0;
}

#define MAIN_RESPONSE_MODULEISLOADED(modulc,moduuc) \
    if (!(modulc.loaded)) { \
        gX_TraceLog(gX_LOG_WARNING,"MAIN: "#moduuc" not loaded"); \
    } \

static gX_I32 gX_MainData_Responses(gX_I0) {
    gX_I32 errors = 0;
    gX_TraceLog_MAIN(gX_LOG_INFO,"Masterindex switched");
    maindata.masterIndexSuccessful = false;
    gX_I32 switcherrors = 0;
    
    MAIN_RESPONSE_MODULEISLOADED(gui,GUI)
    MAIN_RESPONSE_MODULEISLOADED(input,Input)
#if gX_ENABLE_RENDERER == 1
    MAIN_RESPONSE_MODULEISLOADED(renderer,Renderer)
#endif
#if gX_ENABLE_SCENE == 1
    MAIN_RESPONSE_MODULEISLOADED(scene,Scene)
#endif
    
    if (!gui.masterIndexSwitchResponse) {
        gX_TraceLog(gX_LOG_WARNING,"MAIN: GUI failed to respond");
        switcherrors++;
    }
    else gX_TraceLog(gX_LOG_DEBUG,"MAIN: GUI responded");
    
    if (!input.masterIndexSwitchResponse) {
        gX_TraceLog(gX_LOG_WARNING,"MAIN: Input failed to respond");
        switcherrors++;
    }
    else gX_TraceLog(gX_LOG_DEBUG,"MAIN: Input responded");

#if gX_ENABLE_RENDERER == 1
    if (!renderer.masterIndexSwitchResponse) {
        gX_TraceLog(gX_LOG_WARNING,"MAIN: Renderer failed to respond");
        switcherrors++;
    }
    else gX_TraceLog(gX_LOG_DEBUG,"MAIN: Renderer responded");
#else
    //opencode: kernel-only — renderer compiled out, no response needed.
#endif
#if gX_ENABLE_SCENE == 1
    if (!scene.masterIndexSwitchResponse) {
        gX_TraceLog(gX_LOG_WARNING,"MAIN: Scene failed to respond");
        switcherrors++;
    }
    else gX_TraceLog(gX_LOG_DEBUG,"MAIN: Scene responded");
#else
    //opencode: kernel-only — scene compiled out, no response needed.
#endif
    
    if (!camera.masterIndexSwitchResponse) {
        gX_TraceLog(gX_LOG_WARNING,"MAIN: Camera failed to respond");
        switcherrors++;
    }
    else gX_TraceLog(gX_LOG_DEBUG,"MAIN: Camera responded");
    
    if (!switcherrors) {
        gX_TraceLog(gX_LOG_INFO,"MAIN: Masterindex switch successful");
        maindata.masterIndexSuccessful = true;
    }
    else {
        gX_TraceLog(gX_LOG_WARNING,"MAIN: Masterindex switch errors: %i",errors);
        maindata.masterIndexSuccessful = false;
    }
    
    gui.directdraw.drawLoadingScreen = false;
    maindata.masterIndexSwitched = false;
    
    return errors;
}

static gX_I32 gX_MainData_RefreshResponses(gX_I0) {
    gX_I32 errors = 0;
    gX_TraceLog_MAIN(gX_LOG_INFO,"Maindata refreshed");
    maindata.refreshSuccessful = false;
    gX_I32 refresherrors = 0;

#if gX_ENABLE_RENDERER == 1
    MAIN_RESPONSE_MODULEISLOADED(renderer,Renderer)

    if (!renderer.maindataRefreshResponse) {
        gX_TraceLog(gX_LOG_WARNING,"MAIN: Renderer failed to respond");
        refresherrors++;
    }
    else gX_TraceLog(gX_LOG_DEBUG,"MAIN: Renderer responded");
#else
    //opencode: kernel-only — renderer compiled out, refresh needs no response.
#endif

    if (!refresherrors) {
        gX_TraceLog(gX_LOG_INFO,"MAIN: Maindata refresh successful");
        maindata.refreshSuccessful = true;
    }
    
    maindata.refreshOngoing = false;
    
    return errors;
}

gX_I32 gX_MainData_Update_Before(gX_I0) {
    gX_I32 errors = 0;
    
    if (maindata.refresh) {
        gX_MainData_Refresh();
        maindata.refreshOngoing = true;
        maindata.refresh = false;
    }
    
    return errors;
}

gX_I32 gX_MainData_Update_After(gX_I0) {
    gX_I32 errors = 0;
    
    gX_I1 shouldclose = false;
    if (maindata.backendUse && maindata.backendLoaded) {
        shouldclose = gX_Window_ShouldClose();
        maindata.timeElapsed += gX_FrameTime_Get();
    }
    //opencode: exit confirmation — F4 first press marks exit, second closes
    //   Window close button (X) exits immediately
    if (input.keyboard.keys[input.exitKey].state.PRESSED) {
        if (maindata.exitRequested) {
            maindata.loop = false;
        } else {
            maindata.exitRequested = true;
        }
    }
    if (shouldclose || maindata.close) maindata.loop = false;
    
    if (maindata.masterIndexSwitched) {
        //opencode: module scene hooks — scene_exit fires while the old
        //   scene's flags are still set (before the module responses), and
        //   scene_enter after the new flags are in place.
        gX_Module_SceneExit();
        gX_MainData_Responses();
        gX_Module_SceneEnter();
    }
    if (maindata.masterIndexSwitch) {
        maindata.masterIndexSwitched = true;
        maindata.masterIndexSwitch = false;
    }
    
    if (maindata.refreshOngoing) {
        gX_MainData_RefreshResponses();
    }

    //opencode: v0.3.8 — headless transition testing: GX_TRANSITION_TEST=style
    //   (0=fade 1=slide 2=zoom 3=skew-slide 4=diag-wipe 5=scanline 6=iris)
    //   fires gX_GUI_Transition_Style once at frame GX_TRANSITION_FRAME
    //   (default 10), so GX_SHOT_FRAME captures can freeze mid-transition.
    {
        const gX_I8 *tt = getenv("GX_TRANSITION_TEST");
        if (tt && tt[0]) {
            static gX_I1 s_tranTestFired = false;
            if (!s_tranTestFired) {
                gX_U64 fireAt = 10;
                const gX_I8 *tf = getenv("GX_TRANSITION_FRAME");
                if (tf && tf[0]) fireAt = (gX_U64)strtoull(tf, NULL, 10);
                if (maindata.frameCount >= fireAt) {
                    gX_GUI_Transition_Style(0.5f, 0.25f, 0.5f,
                        (gX_GUI_TransitionStyle)atoi(tt), 0, 0);
                    s_tranTestFired = true;
                    gX_TraceLog(gX_LOG_INFO,
                        "TRANSITION_TEST: fired style %s at frame %llu",
                        tt, maindata.frameCount);
                }
            }
        }
    }

    //opencode: --ai-screenshot-final / --ai-screenshot-after N — capture the
    //   previously presented frame and exit (capture runs in the UPDATE phase
    //   so the backbuffer is intact; mid-pipeline reads give a white frame)
    if ((arguments.aiScreenshotFinal || arguments.hasAiScreenshotAfter) && !maindata.screenshot) {
        gX_U64 target = arguments.hasAiScreenshotAfter
            ? (gX_U64)arguments.aiScreenshotAfter : 180;
        //opencode: DEBUG-FLICKER — burst-capture 12 consecutive frames from one
        //   live run (temporal-nondeterminism probe; separate runs hide it)
        if (getenv("GX_BURST") && arguments.hasAiScreenshotAfter && maindata.frameCount >= target && maindata.frameCount < target + 12) {
            static gX_U64 lastBurst = 0;
            if (lastBurst != maindata.frameCount) {
                lastBurst = maindata.frameCount;
                gX_I8 burstPath[256];
                gX_Text_Screenshot(burstPath);
                const gX_I8 *screenshotDir = getenv("GX_SCREENSHOT_DIR");
                if (!screenshotDir || !screenshotDir[0]) screenshotDir = "screenshot";
                snprintf(burstPath, sizeof(burstPath), "%s/burst_%03llu.png",
                         screenshotDir,
                         (unsigned long long)(maindata.frameCount - target));
                gX_TakeScreenShot(burstPath);
                if (maindata.frameCount >= target + 11) maindata.loop = false;
                return errors;
            }
        }
        //opencode: v0.3.7 - model-load gate: fixed-frame captures raced the
        //   deferred resource loader in heavy scenes (late models missing
        //   from frames). Wait until every scene model reports loaded, with
        //   a 900-frame grace so failed/blacklisted loads cannot deadlock;
        //   GX_SHOT_FRAME overrides the frame target, GX_SHOT_FORCE=1
        //   bypasses the gate entirely.
        //opencode: v0.3.8 - slow-scene escape hatch: when the frame target is
        //   unreachable because heavy content (multi-thousand-mesh guest GLBs)
        //   collapses the frame rate, capture GX_SHOT_READY_FRAMES after the
        //   last model finishes loading instead. Normal scenes stay on the
        //   fixed frame target (byte-identical captures); slow scenes no
        //   longer need GX_SHOT_FORCE (which skips the load gate entirely).
        {
            const gX_I8 *sf = getenv("GX_SHOT_FRAME");
            if (sf && sf[0]) target = (gX_U64)strtoull(sf, NULL, 10);
        }
        gX_I1 shotForce = false;
        {
            const gX_I8 *sforce = getenv("GX_SHOT_FORCE");
            if (sforce && sforce[0] == '1') shotForce = true;
        }
        gX_I1 modelsReady = true;
#if gX_ENABLE_RENDERER == 1
        if (renderer.gfx.data && !shotForce) {
            //opencode: v0.3.8 — the load gate only applies to scenes that
            //   actually update the GFX module (gfx scene + gfxdebug). The
            //   gfx module spawns cube/sphere/cone at STARTUP for every
            //   scene, and its deferred loader only runs in gfx-module
            //   scenes — gating on them elsewhere blocked every non-gfx
            //   capture for the full 900-frame grace (title captures were
            //   landing at frame 1080 instead of 180).
            gX_I1 gfxModuleScene = (maindata.masterIndex == gX_SCENE_RENDERER_GFX
                || maindata.masterIndex == gX_SCENE_GFXDEBUG);
            if (gfxModuleScene) {
                gX_RendererGFX *gfxReady = (gX_RendererGFX *)renderer.gfx.data;
                static gX_I1 s_pendingLogged = false;
                GX_HEAP_FOREACH(gfxReady->models, gX_RendererGFX_Model, mready) {
                    //opencode: v0.3.8 — failed loads (loadAsap=false) no longer
                    //   block the gate; only pending loads do
                    if (!mready->loaded && mready->loadAsap) {
                        if (!s_pendingLogged) {
                            s_pendingLogged = true;
                            gX_TraceLog(gX_LOG_WARNING, "SHOT_GATE: pending model '%s' (loaded=%d asap=%d)",
                                mready->path, (int)mready->loaded, (int)mready->loadAsap);
                        }
                        modelsReady = false; break;
                    }
                }
            }
        }
#else
        //opencode: kernel-only — no renderer models; gate stays ready.
        (void)shotForce;
#endif
        static gX_U64 s_readyFrame = 0;
        if (modelsReady && s_readyFrame == 0) s_readyFrame = maindata.frameCount;
        gX_U64 readyFrames = 30;
        {
            const gX_I8 *rf = getenv("GX_SHOT_READY_FRAMES");
            if (rf && rf[0]) readyFrames = (gX_U64)strtoull(rf, NULL, 10);
        }
        //opencode: v0.3.8 — slow-scene escape: heavy content (multi-thousand-
        //   mesh guest GLBs) collapses fps, so the fixed frame target becomes
        //   unreachable in any sane timeout. Once everything has loaded and
        //   settled (GX_SHOT_READY_FRAMES after the last load), capture even
        //   before the target IF the run already took > 30s wall-clock.
        //   Normal scenes hit the fixed target in ~5s and stay byte-identical.
        gX_I1 readySettled = s_readyFrame > 0
            && maindata.frameCount >= s_readyFrame + readyFrames;
        gX_I1 slowEscape = false;
        if (readySettled) {
            static gX_F64 s_t0 = 0;
            if (s_t0 == 0) s_t0 = gX_Time_Get();
            slowEscape = (gX_Time_Get() - s_t0) > 30.0;
        }
        if ((maindata.frameCount >= target && modelsReady)
            || shotForce || slowEscape
            || maindata.frameCount >= target + 900) {
            gX_TraceLog(gX_LOG_INFO, "AI_SCREENSHOT: capturing at frame %llu (ready=%d readyFrame=%llu slow=%d)",
                maindata.frameCount, modelsReady, (unsigned long long)s_readyFrame, slowEscape);
            maindata.screenshot = true;
            maindata.loop = false;
        }
    }
    
    if (maindata.screenshot) {
        gX_TextLine text;
        gX_Text_Screenshot(text);
        gX_TakeScreenShot(text);
        maindata.screenshot = false;
    }
    
    return errors;
}
