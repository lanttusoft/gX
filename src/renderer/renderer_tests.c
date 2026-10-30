/**********************************************************************************************
*
*   [gX: src/renderer/renderer_tests.c]
*   Headless renderer scene-transition regressions
*
*   Copyright (c) 2026 lanttu, gX contributors
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


//ai: pin GFX content ownership so unrelated scenes cannot clear the editor heap.
#include "renderer_scene_transition.h"
//ai: hybrid toggles are metadata-only and testable without a GL context.
#if gX_ENABLE_RENDERER
#include "renderer.h"
#include <string.h>
#endif

#include "../common/log.h"

gX_I32 gX_Tests_Renderer_SceneTransition(gX_I0);
gX_I32 gX_Tests_Renderer_SceneTransition(gX_I0) {
    gX_I32 errors = 0;
#if gX_ENABLE_RENDERER
    gX_Renderer savedRenderer = renderer;
    renderer = (gX_Renderer){0};
    renderer.state.useCompositor = 1;
    renderer.compositor.active = 1;
    renderer.compositor.layerCount = 2;
    renderer.compositor.layers[0] = (gX_Compositor_Layer){RENDERER_MODULE_SKY,1,GX_COMPOSITE_BLEND_ADD};
    renderer.compositor.layers[1] = (gX_Compositor_Layer){RENDERER_MODULE_GFX,0,GX_COMPOSITE_BLEND_SCREEN};
    renderer.compositor.rtPool[0].id = 123;
    renderer.gfx.state.DRAW_ALL = 1;
    renderer.raymarch.state.DRAW_ALL = 1;
    gX_Renderer expectedRenderer = renderer;
    gX_Renderer_RaymarchDepth_Set(1);
    gX_Renderer_RaymarchDepth_Set(1);
    if (!renderer.state.raymarchDepth || renderer.compositor.layerCount != 1
        || renderer.compositor.layers[0].moduleFlag != RENDERER_MODULE_GFX
        || renderer.raymarch.state.DRAW_ALL || !renderer.raymarch.state.UPDATE_ALL)
    {
        gX_TraceLog(gX_LOG_ERROR,"RENDERER_SCENE_TEST FAIL: hybrid activation routing");
        errors++;
    }
    gX_Renderer_RaymarchDepth_Set(0);
    gX_Renderer_RaymarchDepth_Set(0);
    if (memcmp(&renderer,&expectedRenderer,sizeof(renderer)))
    {
        gX_TraceLog(gX_LOG_ERROR,"RENDERER_SCENE_TEST FAIL: hybrid did not restore routing/RT ownership");
        errors++;
    }
    gX_Renderer_RaymarchDepth_Set(1);
    gX_Renderer_RaymarchDepth_Set(2);
    if (renderer.state.raymarchDepth != 2)
    {
        gX_TraceLog(gX_LOG_ERROR,"RENDERER_SCENE_TEST FAIL: shared-depth mode selection");
        errors++;
    }
    gX_Renderer_RaymarchDepth_Set(1);
    gX_Renderer_RaymarchDepth_Set(0);
    if (memcmp(&renderer,&expectedRenderer,sizeof(renderer)))
    {
        gX_TraceLog(gX_LOG_ERROR,"RENDERER_SCENE_TEST FAIL: mode switching overwrote restoration snapshot");
        errors++;
    }
    renderer = savedRenderer;
#endif
    if (gX_Renderer_SceneTransition_ShouldClearGFX(true,
            GX_RENDERER_CONTENT_DEMO, GX_RENDERER_CONTENT_DEMO)) {
        gX_TraceLog(gX_LOG_ERROR,
            "RENDERER_SCENE_TEST FAIL: non-Battle switch cleared GFX content");
        errors++;
    }
    if (!gX_Renderer_SceneTransition_ShouldClearGFX(true,
            GX_RENDERER_CONTENT_DEMO, GX_RENDERER_CONTENT_BATTLE)) {
        gX_TraceLog(gX_LOG_ERROR,
            "RENDERER_SCENE_TEST FAIL: entering Battle kept non-Battle content");
        errors++;
    }
    if (!gX_Renderer_SceneTransition_ShouldClearGFX(true,
            GX_RENDERER_CONTENT_BATTLE, GX_RENDERER_CONTENT_DEMO)) {
        gX_TraceLog(gX_LOG_ERROR,
            "RENDERER_SCENE_TEST FAIL: leaving Battle kept Battle content");
        errors++;
    }
    if (gX_Renderer_SceneTransition_ShouldClearGFX(true,
            GX_RENDERER_CONTENT_BATTLE, GX_RENDERER_CONTENT_BATTLE)) {
        gX_TraceLog(gX_LOG_ERROR,
            "RENDERER_SCENE_TEST FAIL: Battle-to-Battle switch cleared content");
        errors++;
    }
    if (gX_Renderer_SceneTransition_ShouldClearGFX(false,
            GX_RENDERER_CONTENT_BATTLE, GX_RENDERER_CONTENT_DEMO)) {
        gX_TraceLog(gX_LOG_ERROR,
            "RENDERER_SCENE_TEST FAIL: empty GFX heap requested clearing");
        errors++;
    }
    //ai: physics sandbox content ownership — any owner change clears,
    //   physics-to-physics keeps the built prop set.
    if (!gX_Renderer_SceneTransition_ShouldClearGFX(true,
            GX_RENDERER_CONTENT_PHYSICS, GX_RENDERER_CONTENT_DEMO)) {
        gX_TraceLog(gX_LOG_ERROR,
            "RENDERER_SCENE_TEST FAIL: leaving Physics kept Physics content");
        errors++;
    }
    if (!gX_Renderer_SceneTransition_ShouldClearGFX(true,
            GX_RENDERER_CONTENT_DEMO, GX_RENDERER_CONTENT_PHYSICS)) {
        gX_TraceLog(gX_LOG_ERROR,
            "RENDERER_SCENE_TEST FAIL: entering Physics kept non-Physics content");
        errors++;
    }
    if (!gX_Renderer_SceneTransition_ShouldClearGFX(true,
            GX_RENDERER_CONTENT_BATTLE, GX_RENDERER_CONTENT_PHYSICS)) {
        gX_TraceLog(gX_LOG_ERROR,
            "RENDERER_SCENE_TEST FAIL: Battle-to-Physics switch kept content");
        errors++;
    }
    if (!gX_Renderer_SceneTransition_ShouldClearGFX(true,
            GX_RENDERER_CONTENT_PHYSICS, GX_RENDERER_CONTENT_BATTLE)) {
        gX_TraceLog(gX_LOG_ERROR,
            "RENDERER_SCENE_TEST FAIL: Physics-to-Battle switch kept content");
        errors++;
    }
    if (gX_Renderer_SceneTransition_ShouldClearGFX(true,
            GX_RENDERER_CONTENT_PHYSICS, GX_RENDERER_CONTENT_PHYSICS)) {
        gX_TraceLog(gX_LOG_ERROR,
            "RENDERER_SCENE_TEST FAIL: Physics-to-Physics switch cleared content");
        errors++;
    }
    if (errors == 0)
        gX_TraceLog(gX_LOG_INFO, "RENDERER_SCENE_TESTS: all passed");
    return errors;
}
