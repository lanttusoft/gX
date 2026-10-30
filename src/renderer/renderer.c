/**********************************************************************************************
*
*   [gX: src/renderer/renderer.c]
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


#include <string.h>
#include <stdio.h>
#include <stdlib.h>



#include "renderer.h"
#include "gfx/scene.h"
#include "renderer_scene_transition.h"
#include "../main.h"

#include "../game/ephemeris.h"

#include "../common/log.h"
#include "../common/enum.h"
#include "../common/macros.h"
#include "../common/profiler.h"

#include "../wrappers/wrappers.h"
#include "../rhi/gl_state.h"
#include "../rhi/rhi.h"
#include "../rhi/rhi_fbo.h"

//opencode: scene config — maps scene enums to which renderers update/draw
typedef enum {
    RENDERER_SETUP_NONE,
    RENDERER_SETUP_BACKGROUND_PRESET,
} gX_Renderer_SceneSetupKind;

typedef struct {
    gX_I32 sceneIndex;
    gX_U32 updateFlags;
    gX_U32 drawFlags;
    gX_Renderer_SceneSetupKind setup;
    gX_I1  usePP;  //opencode: enable post-process for this scene

    //opencode: compositor layers (0 = use old module draw path)
    gX_U8  compositorLayerCount;
    gX_U32 compositorLayerFlags[GX_COMPOSITOR_MAX_LAYERS];
    gX_U8  compositorLayerBlends[GX_COMPOSITOR_MAX_LAYERS];

    //opencode: background preset index for RENDERER_SETUP_BACKGROUND_PRESET scenes
    gX_U8  bgPresetIndex;
} gX_Renderer_SceneConfig;

static const gX_Renderer_SceneConfig rendererSceneConfigs[] = {
    //ai: title scene backdrop — the background module with the TITLE preset
    //   (Mother 2-style palette hue cycling); the GUI composite captures FB0
    //   and the rotating centerpiece draws over it (no compositor layers).
    { gX_SCENE_TITLE,               RENDERER_MODULE_BACKGROUND,            RENDERER_MODULE_BACKGROUND,          RENDERER_SETUP_BACKGROUND_PRESET, 0, 0, {0}, {0}, 2 },
    //opencode: v0.3.8 — background renderer scene (--background): fullscreen
    //   procedural preset (TITLE2), compositor alpha layer into the canvas
    { gX_SCENE_RENDERER_BACKGROUND, RENDERER_MODULE_BACKGROUND,           RENDERER_MODULE_BACKGROUND,          RENDERER_SETUP_BACKGROUND_PRESET, 0, 1, {RENDERER_MODULE_BACKGROUND}, {GX_COMPOSITE_BLEND_ALPHA}, 1 },
    //opencode: synth visualizer scene — the scene composites the module RTs
    //   itself (custom panel layout + keyboard canvas); module update runs here
    //   (spectrum bar smoothing) and the draw flags stay off to avoid a
    //   redundant offscreen pass.
    { gX_SCENE_SYNTHVIS,            RENDERER_MODULE_OSCILLO | RENDERER_MODULE_LISSAJOUS | RENDERER_MODULE_SPECTRUM, \
                                    0, RENDERER_SETUP_NONE, 0 },
    //ai: battle showcase — background under transparent 3D GFX, then PP on
    //   the composite; the battle HUD is drawn afterward by the GUI overlay.
    { gX_SCENE_BATTLE,              RENDERER_MODULE_BACKGROUND | RENDERER_MODULE_GFX, 0, RENDERER_SETUP_BACKGROUND_PRESET, 1, 2, {RENDERER_MODULE_BACKGROUND, RENDERER_MODULE_GFX}, {GX_COMPOSITE_BLEND_ALPHA, GX_COMPOSITE_BLEND_ALPHA}, 3 },
    { gX_SCENE_MENU,                0,                                     0,                                   RENDERER_SETUP_NONE, 0 },
    //opencode: map/world have no scene-owned draw pass yet; keep the shared
    //   procedural sky visible while their content systems are empty.
    { gX_SCENE_MAP,                 RENDERER_MODULE_SKY | RENDERER_MODULE_WS2D, RENDERER_MODULE_SKY,                  RENDERER_SETUP_NONE, 0 },
    { gX_SCENE_WORLD,               RENDERER_MODULE_SKY | RENDERER_MODULE_WS2D, RENDERER_MODULE_SKY,                  RENDERER_SETUP_NONE, 0 },
    { gX_SCENE_INTERIOR,            0,                                     0,                                   RENDERER_SETUP_NONE, 0 },
    //ai: physics sandbox — the scene owns bodies + generated prop models;
    //   the GFX module draws them (battle-style heap ownership).
    { gX_SCENE_PHYSICS,             RENDERER_MODULE_GFX,                   RENDERER_MODULE_GFX,                 RENDERER_SETUP_NONE, 0 },
    { gX_SCENE_GFXDEBUG,            RENDERER_MODULE_GFX,                RENDERER_MODULE_GFX,             RENDERER_SETUP_NONE, 0 },
    { gX_SCENE_RENDERER_GFX,        RENDERER_MODULE_GFX,                RENDERER_MODULE_GFX,             RENDERER_SETUP_NONE, 0 },
    { gX_SCENE_RENDERER_ANALOG,     RENDERER_MODULE_ANALOG,                RENDERER_MODULE_ANALOG,              RENDERER_SETUP_NONE, 0 },
    { gX_SCENE_RENDERER_CLOCK,      RENDERER_MODULE_CLOCK,                 RENDERER_MODULE_CLOCK,               RENDERER_SETUP_NONE, 0 },
    { gX_SCENE_RENDERER_GENERATE,   RENDERER_MODULE_GENERATE,                                          RENDERER_MODULE_GENERATE,            RENDERER_SETUP_NONE, 0 },
    //ai: PP scenes present through the embedded instance (usePP = 1); the
    //   module-heap draw would run a second, redundant chain into the canvas
    //   and overwrite the embedded output (the old drawFlags duplicated it).
    { gX_SCENE_RENDERER_PP,         RENDERER_MODULE_PP,                    0,                                   RENDERER_SETUP_NONE, 1 },
    { gX_SCENE_LOADINGSCREEN,       RENDERER_MODULE_PP,                    0,                                   RENDERER_SETUP_NONE, 1 },
    { gX_SCENE_RENDERER_RAYMARCH,   RENDERER_MODULE_RAYMARCH | RENDERER_MODULE_SKY | RENDERER_MODULE_WS2D, RENDERER_MODULE_RAYMARCH,            RENDERER_SETUP_NONE, 0 },
    { gX_SCENE_RENDERER_SKY,        RENDERER_MODULE_SKY,                   RENDERER_MODULE_SKY,                 RENDERER_SETUP_NONE, 0 },
    { gX_SCENE_RENDERER_TELETEXT,   RENDERER_MODULE_TELETEXT,              RENDERER_MODULE_TELETEXT,            RENDERER_SETUP_NONE, 0 },
    //ai: textwriter scene — the module owns update/draw (gameX had the same
    //   config plus the dropped demoscript flag); it auto-loads
    //   resources/text/textwriter.txt on first update, and scripts drive
    //   file/lines/reveal plus the tagged-draw overlay.
    { gX_SCENE_RENDERER_TEXTWRITER, RENDERER_MODULE_TEXTWRITER,            RENDERER_MODULE_TEXTWRITER,          RENDERER_SETUP_NONE, 0 },
    { gX_SCENE_RENDERER_WATER,      RENDERER_MODULE_WATER,                 RENDERER_MODULE_WATER,                RENDERER_SETUP_NONE, 0 },
    { gX_SCENE_RENDERER_WIREFRAME,  RENDERER_MODULE_WIREFRAME,             RENDERER_MODULE_WIREFRAME,            RENDERER_SETUP_NONE, 0 },
    { gX_SCENE_RENDERER_LED,        0,                                      0,                                     RENDERER_SETUP_NONE, 0 },
    { -1, 0, 0, RENDERER_SETUP_NONE, 0 }
};

gX_Renderer renderer;

//opencode: all per-module expansions from GX_RENDERER_MODULES
// Each entry: (field, type, flag, scene, drawarg)

#define RENDERER_MODULE_HEAP_INIT(field, type, flag, scene, drawarg) \
    gX_Heap_Zero(&renderer.field); \
    gX_Heap_SetName(&renderer.field, #field); \
    gX_Heap_SetBlockSize(&renderer.field, sizeof(gX_Renderer##type)); \
    gX_Heap_SetMax(&renderer.field, 1); \
    gX_Heap_SetAmount(&renderer.field, 1); \
    gX_Heap_Init(&renderer.field); \
    renderer.field.state.UPDATE_ALL = false; \
    renderer.field.state.DRAW_ALL = false;

static gX_I0 gX_Renderer_ModuleHeaps_Init(gX_I0) {
#define X(field, type, flag, scene, drawarg) RENDERER_MODULE_HEAP_INIT(field, type, flag, scene, drawarg)
    GX_RENDERER_MODULES
#undef X
}

//opencode: load/unload only allocated heaps — a boot that fails before
//   gX_Renderer_Load (or a failed module allocation) must not deref NULL on
//   gX_Unload's error path.
#define RENDERER_MODULE_LOAD(field, type, flag, scene, drawarg) \
    { \
        if (renderer.field.data) { \
            gX_Renderer##type *ptrs = renderer.field.data; \
            gX_Renderer##type *ptr = &ptrs[0]; \
            gX_Renderer##type##_Load(ptr); \
            renderer.field.meta[0].UPDATE = true; \
            renderer.field.meta[0].DRAW = true; \
        } \
    }

#define RENDERER_MODULE_UNLOAD(field, type, flag, scene, drawarg) \
    { \
        if (renderer.field.data) { \
            gX_Renderer##type *ptrs = renderer.field.data; \
            gX_Renderer##type *ptr = &ptrs[0]; \
            gX_Renderer##type##_Unload(ptr); \
            gX_Heap_DeInit(&renderer.field); \
        } \
    }

#define RENDERER_MODULE_UPDATE(field, type, flag, scene, drawarg) \
    { \
        gX_Renderer##type *ptrs = renderer.field.data; \
        for (gX_U32 i = 0; i < renderer.field.amount; i++) { \
            if (!renderer.field.state.UPDATE_ALL) break; \
            if (!renderer.field.meta[i].UPDATE) continue; \
            gX_Renderer##type##_Update(&ptrs[i]); \
        } \
    }

//opencode: draw for modules with default signature (drawarg=0)
//   Wraps each module's draw calls in GL state save/restore to prevent
//   state leaks across module boundaries (shader, FBO, viewport, etc.).
#define RENDERER_MODULE_DRAW_0(field, type) \
    { \
        gX_GL_State_Push(); \
        gX_Renderer##type *ptrs = renderer.field.data; \
        for (gX_U32 i = 0; i < renderer.field.amount; i++) { \
            if (!renderer.field.state.DRAW_ALL) break; \
            if (!renderer.field.meta[i].DRAW) continue; \
            gX_Renderer##type##_Draw(&ptrs[i]); \
        } \
        gX_GL_State_Pop(); \
    }

//opencode: no-op for modules with non-standard draw (background, drawarg=1)
#define RENDERER_MODULE_DRAW_1(field, type)

#define RENDERER_MODULE_DRAW(field, type, flag, scene, drawarg) \
    RENDERER_MODULE_DRAW_##drawarg(field, type)

#define RENDERER_MODULE_CLEAR(field, type, flag, scene, drawarg) \
    renderer.field.state.UPDATE_ALL = false; \
    renderer.field.state.DRAW_ALL = false;

#define RENDERER_MODULE_SET_UPDATE(field, type, flag, scene, drawarg) \
    if (flags & RENDERER_MODULE_##flag) renderer.field.state.UPDATE_ALL = true;

#define RENDERER_MODULE_SET_DRAW(field, type, flag, scene, drawarg) \
    if (flags & RENDERER_MODULE_##flag) renderer.field.state.DRAW_ALL = true;

static gX_I0 gX_Renderer_Clear_UpdateDraw(gX_I0) {
#define X(field, type, flag, scene, drawarg) RENDERER_MODULE_CLEAR(field, type, flag, scene, drawarg)
    GX_RENDERER_MODULES
#undef X
}

static gX_I0 gX_Renderer_Canvas_Load(gX_I0) {

    if (renderer.state.GLOW_LOAD_ASAP && !(renderer.state.GLOW_LOADED)) {
        renderer.glow = gX_RenderTexture_Load(maindata.renderWidth,maindata.renderHeight);
        renderer.state.GLOW_LOADED = true;
    }
    if (renderer.state.FEEDBACK_LOAD_ASAP && !(renderer.state.FEEDBACK_LOADED)) {
        //opencode: HDR format matches renderer.canvas (RGBA16F) — prevents clamping of
        //   bloom-range values in the post-process feedback loop (motion blur, temporal).
        renderer.feedback = gX_RenderTexture_Load_HDR(maindata.renderWidth,maindata.renderHeight);
        renderer.state.FEEDBACK_LOADED = true;
    }
    if (renderer.state.CANVAS_LOAD_ASAP && !(renderer.state.CANVAS_LOADED)) {
        //opencode: use HDR format so the bloom bypass glBlitFramebuffer from
        //   hdrScene (RGBA16F) works on the CORE profile (CORE rejects blits
        //   between different formats). This also keeps the HDR range through
        //   the compositing chain; the final display clamp handles the rest.
        renderer.canvas = gX_RenderTexture_Load_HDR(maindata.renderWidth,maindata.renderHeight);
        //opencode: clear canvas to transparent black so undefined content doesn't
        //   show through when no module writes to it (e.g. standalone water scene)
        gX_Drawing_BeginTexture(renderer.canvas);
        gX_Drawing_ClearBG((gX_Color){0,0,0,0});
        gX_Drawing_EndTexture();
        renderer.state.CANVAS_LOADED = true;
    }
    //opencode: reset the per-frame "canvas was drawn" flag at load time too,
    //   in case Load is called mid-frame (e.g. on window resize).
    renderer.canvasDrawnThisFrame = 0;

}

//opencode: GLFW's gX_glfw_Drawing_End uses this to decide whether to
//   composite the canvas to the default framebuffer. Set to 1 by
//   gX_Drawing_BeginTexture when binding the canvas FBO, reset to 0 by
//   gX_Renderer_BeginFrame at the start of each frame.
gX_I0 gX_Renderer_SetCanvasDrawn(gX_I1 drawn) {
    renderer.canvasDrawnThisFrame = drawn ? 1 : 0;
}

gX_I1 gX_Renderer_GetCanvasDrawn(gX_I0) {
    return renderer.canvasDrawnThisFrame;
}

//opencode: GLFW's gX_glfw_Drawing_End checks this flag to skip its own
//   canvas composite when the GUI has already drawn it (via SCENE template
//   or any other canvas that uses renderer.canvas.texture as a texture element).
//   Prevents the 2-canvas-copy visual artifact.
gX_I0 gX_Renderer_SetCanvasDrawnByGUI(gX_I1 drawn) {
    renderer.canvasDrawnByGUI = drawn ? 1 : 0;
}

gX_I1 gX_Renderer_GetCanvasDrawnByGUI(gX_I0) {
    return renderer.canvasDrawnByGUI;
}

gX_I0 gX_Renderer_SetCompositorDrawn(gX_I1 drawn) {
    renderer.compositorDrawnThisFrame = drawn ? 1 : 0;
}

gX_I1 gX_Renderer_GetCompositorDrawn(gX_I0) {
    return renderer.compositorDrawnThisFrame;
}

gX_U32 gX_Renderer_GetCanvasTextureId(gX_I0) {
    return renderer.canvas.id;
}

gX_I0 gX_Renderer_BeginFrame(gX_I0) {
    //opencode: clear the per-frame "canvas was drawn" flags. gX_glfw_Drawing_End
    //   reads canvasDrawnThisFrame to decide whether to composite the canvas
    //   to the screen (if nothing drew to it this frame, the canvas is opaque
    //   black from initial load and would cover any direct-to-screen GUI).
    //   canvasDrawnByGUI is also reset — set back to 1 by gX_GUI_Canvas_Draw
    //   after its buffer composite, informing glfw to skip.
    renderer.canvasDrawnThisFrame = 0;
    renderer.canvasDrawnByGUI = 0;
    renderer.compositorDrawnThisFrame = 0;
}

static gX_I32 gX_Renderer_Canvas_Unload(gX_I0) {
    
    if (renderer.state.GLOW_LOADED) {
        gX_RenderTexture_Unload(renderer.glow);
        renderer.state.GLOW_LOADED = false;
    }
    if (renderer.state.FEEDBACK_LOADED) {
        gX_RenderTexture_Unload(renderer.feedback);
        renderer.state.FEEDBACK_LOADED = false;
    }
    if (renderer.state.CANVAS_LOADED) {
        gX_RenderTexture_Unload(renderer.canvas);
        renderer.state.CANVAS_LOADED = false;
    }
    
    return 0;
}

static gX_I32 gX_Renderer_Canvas_Reload(gX_I0) {
    gX_TRACELOG_PROLOGUE("Renderer_Canvas_Reload","RENDERER: Reloading canvases")
    
    gX_Renderer_Canvas_Unload();
    gX_Renderer_Canvas_Load();
    
    //opencode: compositor RT pool is created at the old resolution — unload it
    //   so the next gX_Compositor_Draw re-creates it at the new render size.
    gX_Compositor_Reload(&renderer.compositor);
    
    gX_TRACELOG_EPILOGUE("RENDERER: Reloading done")
    return 0;
}

gX_I32 gX_Renderer_Load(gX_I0) {
    gX_I32 errors = 0;
    gX_TRACELOG_PROLOGUE("Renderer_Load","RENDERER: Loading renderer")
    
    strcpy(renderer.name,"Renderer");
    strcpy(renderer.description,"renderer");
    gX_TraceLog_RENDERER(gX_LOG_GREEN,"name: %s",renderer.name);
    gX_TraceLog_RENDERER(gX_LOG_GREEN,"desc: %s",renderer.description);
    
    renderer.state.FEEDBACK_LOADED = false;
    renderer.state.FEEDBACK_LOAD_ASAP = true;
    renderer.state.CANVAS_LOADED = false;
    renderer.state.CANVAS_LOAD_ASAP = true;
    
    renderer.masterIndexSwitchResponse = false;
    renderer.maindataRefreshResponse = false;
    
    //opencode: init all module heaps and load from GX_RENDERER_MODULES
    gX_Renderer_ModuleHeaps_Init();
    
#define X(field, type, flag, scene, drawarg) RENDERER_MODULE_LOAD(field, type, flag, scene, drawarg)
    GX_RENDERER_MODULES
#undef X

    //opencode: load the embedded PP instance (separate from heap-based PP modules)
    //   Must happen here so textures are ready before any preset is applied.
    gX_RendererPostProcess_Load(&renderer.pp);
    renderer.ppLoaded = true;
    
    gX_Renderer_Clear_UpdateDraw();

    //opencode: init compositor
    gX_Compositor_Init(&renderer.compositor);

    renderer.loaded = true;
    
    gX_TRACELOG_EPILOGUE("RENDERER: Loading done")
    return errors;
}

gX_I32 gX_Renderer_Unload(gX_I0) {
    gX_I32 errors = 0;
    gX_TRACELOG_PROLOGUE("Renderer_Unload","RENDERER: Unloading renderer")
    
#define X(field, type, flag, scene, drawarg) RENDERER_MODULE_UNLOAD(field, type, flag, scene, drawarg)
    GX_RENDERER_MODULES
#undef X
    
    gX_Renderer_Canvas_Unload();
    
    gX_Compositor_DeInit(&renderer.compositor);

    if (renderer.state.usePP) {
        gX_RendererPostProcess_Unload(&renderer.pp);
    }
    
    renderer.loaded = false;
    
    gX_TRACELOG_EPILOGUE("RENDERER: Unloading done")
    return errors;
}

gX_I0 gX_Renderer_Update(gX_I0) {
    if (maindata.backendLoaded) {

        gX_Renderer_Canvas_Load();

        if (renderer.state.usePP && !(renderer.ppLoaded)) {
            gX_RendererPostProcess_Load(&renderer.pp);
            renderer.ppLoaded = true;
        }
    }

    if (maindata.masterIndexSwitched) {
        gX_Renderer_RaymarchDepth_Set(0);
        renderer.masterIndexSwitchResponse = false;
        gX_Renderer_Clear_UpdateDraw();
        renderer.state.usePP = 0;  //opencode: default off — enabled per scene below
        //ai: module-owned scene — no renderer module owns the frame.
        if (maindata.masterIndex == gX_SCENE_MODULE) {
            renderer.masterIndexSwitchResponse = true;
        }
        for (gX_U32 i = 0; rendererSceneConfigs[i].sceneIndex >= 0; i++) {
            if (rendererSceneConfigs[i].sceneIndex == maindata.masterIndex) {
                const gX_Renderer_SceneConfig *cfg = &rendererSceneConfigs[i];
                
                gX_U32 flags = cfg->updateFlags;
#define X(field, type, flag, scene, drawarg) RENDERER_MODULE_SET_UPDATE(field, type, flag, scene, drawarg)
                GX_RENDERER_MODULES
#undef X
                
                flags = cfg->drawFlags;
#define X(field, type, flag, scene, drawarg) RENDERER_MODULE_SET_DRAW(field, type, flag, scene, drawarg)
                GX_RENDERER_MODULES
#undef X
                
                if (cfg->setup == RENDERER_SETUP_BACKGROUND_PRESET) {
                    gX_RendererBackground *bgHeap = renderer.background.data;
                    gX_RendererBackground_Preset_BuiltIn(&bgHeap[0], cfg->bgPresetIndex);
                }
                if (renderer.gfx.data && renderer.gfx.amount > 0) {
                    gX_RendererGFX *gfxHeap = (gX_RendererGFX *)renderer.gfx.data;
                    const char *transparentBg = getenv("GX_GFX_TRANSPARENT_BG");
                    //ai: content owner from the heap flags (physics wins over
                    //   demo; battle wins over both) vs the destination owner.
                    gX_I32 contentOwner = GX_RENDERER_CONTENT_NONE;
                    if (gfxHeap->common.demoSceneAdded || gfxHeap->common.battleScene
                        || gfxHeap->common.physicsScene) {
                        contentOwner = gfxHeap->common.battleScene
                            ? GX_RENDERER_CONTENT_BATTLE
                            : (gfxHeap->common.physicsScene
                                ? GX_RENDERER_CONTENT_PHYSICS
                                : GX_RENDERER_CONTENT_DEMO);
                    }
                    gX_I32 targetOwner = (maindata.masterIndex == gX_SCENE_BATTLE)
                        ? GX_RENDERER_CONTENT_BATTLE
                        : ((maindata.masterIndex == gX_SCENE_PHYSICS)
                            ? GX_RENDERER_CONTENT_PHYSICS
                            : GX_RENDERER_CONTENT_DEMO);
                    if (gX_Renderer_SceneTransition_ShouldClearGFX(
                            contentOwner != GX_RENDERER_CONTENT_NONE,
                            contentOwner, targetOwner)) {
                        gX_RendererGFX_ClearScene(gfxHeap);
                        gfxHeap->common.demoSceneAdded = false;
                        gfxHeap->common.battleScene = false;
                        gfxHeap->common.physicsScene = false;
                    }
                    gfxHeap->transparentBackground =
                        maindata.masterIndex == gX_SCENE_BATTLE
                        || (transparentBg && transparentBg[0] == '1');
                }
                renderer.state.usePP = cfg->usePP;
                //opencode: configure compositor layers from scene config
                renderer.state.useCompositor = (cfg->compositorLayerCount > 0);
                if (cfg->compositorLayerCount > 0) {
                    gX_Compositor_SetLayers(&renderer.compositor,
                        cfg->compositorLayerCount,
                        cfg->compositorLayerFlags,
                        cfg->compositorLayerBlends);
                }
                renderer.masterIndexSwitchResponse = true;
                break;
            }
        }
    }
    //ai: the standalone postprocess scene needs an input: point the embedded
    //   chain at its test texture (presets/panel can override afterwards).
    if (maindata.masterIndex == gX_SCENE_RENDERER_PP && renderer.ppLoaded) {
        renderer.pp.state.inject = true;
        renderer.pp.inject.connectScale = true;
    }
    //opencode: raymarch module view hook (gfx integration): force the module
    //   into any scene. The editor panel toggles the same heap flags.
    {
        const char *raymarchView = getenv("GX_GFX_RAYMARCH_VIEW");
        if (raymarchView && raymarchView[0] == '1') {
            renderer.raymarch.state.UPDATE_ALL = true;
            renderer.raymarch.state.DRAW_ALL = true;
        }
        const char *raymarchComposite = getenv("GX_GFX_RAYMARCH_COMPOSITE");
        if (raymarchComposite && raymarchComposite[0] == '1')
            gX_Renderer_RaymarchComposite_Set(1);
        else if (raymarchComposite && raymarchComposite[0] == '2')
            gX_Renderer_RaymarchComposite_SetBackdrop(1);
        //ai: the hybrid prototype owns one GFX layer and overlays SDF against current raster depth.
        const char *hybridDepth = getenv("GX_GFX_RAYMARCH_DEPTH");
        if (getenv("GX_RAYMARCH_DEPTH_TEST")) renderer.raymarch.state.UPDATE_ALL = true;
        if (hybridDepth && (hybridDepth[0] == '1' || hybridDepth[0] == '2')
            && maindata.masterIndexSwitched)
            gX_Renderer_RaymarchDepth_Set(hybridDepth[0]-'0');
        //ai: postprocess over the current scene (GX_PP_VIEW=1): the chain runs
        //   after the scene/module draw with the canvas as its input.
        const char *ppView = getenv("GX_PP_VIEW");
        if (ppView && ppView[0] == '1' && renderer.ppLoaded) {
            renderer.state.usePP = 1;
            renderer.pp.state.injectCanvas = 1;
        }
        if (maindata.masterIndex == gX_SCENE_BATTLE) {
            const char *skipGfx = getenv("GX_BATTLE_TEST_NO_GFX");
            if (skipGfx && skipGfx[0] == '1')
                gX_Compositor_SetLayerEnabled(&renderer.compositor, 1, 0);
            const char *skipPP = getenv("GX_BATTLE_TEST_NO_PP");
            if (skipPP && skipPP[0] == '1') renderer.state.usePP = 0;
        }
    }
    if (maindata.refreshOngoing) {
        
        gX_Renderer_Canvas_Reload();
        
        renderer.pp.state.reload = true;
        
        gX_RendererGFX *gfxHeap = (gX_RendererGFX*)renderer.gfx.data;
        if (gfxHeap) gfxHeap->state.RELOAD = true;
        
        renderer.maindataRefreshResponse = true;
    }

    GX_PROFILE_ZONE_BEGIN("renderer:modules_update");
#define X(field, type, flag, scene, drawarg) RENDERER_MODULE_UPDATE(field, type, flag, scene, drawarg)
    GX_RENDERER_MODULES
#undef X
    GX_PROFILE_ZONE_END("renderer:modules_update");

    if (renderer.state.usePP) {
        gX_RendererPostProcess_Update(&renderer.pp);
        if (maindata.masterIndex == gX_SCENE_BATTLE
            && renderer.pp.state.loaded && renderer.pp.shadersAmount == 0) {
            gX_I32 presetResult = gX_RendererPostProcess_Preset_BuiltIn(
                &renderer.pp, gX_PP_PRESET_VIGNETTE);
            if (presetResult == 0)
                gX_TraceLog(gX_LOG_NOTICE, "BATTLE_PP: preset=VIGNETTE");
        }
    }

}

//ai: snapshot layer metadata only, never owning or copying compositor RTs.
gX_I0 gX_Renderer_RaymarchDepth_Set(gX_I32 mode)
{
    static gX_Compositor_Layer savedLayers[GX_COMPOSITOR_MAX_LAYERS];
    static gX_U8 savedCount;
    static gX_U8 savedFlags;
    if (mode < 0 || mode > 2)
    {
        gX_TraceLog(gX_LOG_WARNING,"RAYMARCH_DEPTH: invalid mode %d",mode);
        return;
    }
    if (!!mode == !!renderer.state.raymarchDepth)
    {
        renderer.state.raymarchDepth = mode;
        return;
    }
    if (mode)
    {
        savedCount = renderer.compositor.layerCount;
        memcpy(savedLayers,renderer.compositor.layers,sizeof(savedLayers));
        savedFlags = (renderer.state.useCompositor ? 1 : 0)
            | (renderer.compositor.active ? 2 : 0)
            | (renderer.raymarch.state.UPDATE_ALL ? 4 : 0)
            | (renderer.raymarch.state.DRAW_ALL ? 8 : 0)
            | (renderer.gfx.state.UPDATE_ALL ? 16 : 0)
            | (renderer.gfx.state.DRAW_ALL ? 32 : 0);
        static const gX_U32 flags[1] = {RENDERER_MODULE_GFX};
        static const gX_U8 blends[1] = {GX_COMPOSITE_BLEND_ALPHA};
        renderer.raymarch.state.UPDATE_ALL = true;
        renderer.raymarch.state.DRAW_ALL = false;
        renderer.gfx.state.UPDATE_ALL = true;
        renderer.gfx.state.DRAW_ALL = true;
        gX_Compositor_SetLayers(&renderer.compositor,1,flags,blends);
        renderer.state.useCompositor = true;
    }
    else
    {
        memcpy(renderer.compositor.layers,savedLayers,sizeof(savedLayers));
        renderer.compositor.layerCount = savedCount;
        renderer.state.useCompositor = (savedFlags & 1) != 0;
        renderer.compositor.active = (savedFlags & 2) != 0;
        renderer.raymarch.state.UPDATE_ALL = (savedFlags & 4) != 0;
        renderer.raymarch.state.DRAW_ALL = (savedFlags & 8) != 0;
        renderer.gfx.state.UPDATE_ALL = (savedFlags & 16) != 0;
        renderer.gfx.state.DRAW_ALL = (savedFlags & 32) != 0;
    }
    renderer.state.raymarchDepth = mode;
}

//opencode: composite mode — raymarch draws the backdrop (layer 0, transparent
//   clear) and gfx blends over it (layer 1). Opaque gfx screen-blends (both
//   layers visible); with GX_GFX_TRANSPARENT_BG=1 or in Battle, GFX masks its
//   sky alpha and blends with true ALPHA so the backdrop shows through. The
//   compositor path in gX_Renderer_Draw then owns the frame.
gX_I0 gX_Renderer_RaymarchComposite_Set(gX_I1 on) {
    if (on) {
        renderer.raymarch.state.UPDATE_ALL = true;
        renderer.raymarch.state.DRAW_ALL = true;
        renderer.gfx.state.UPDATE_ALL = true;
        renderer.gfx.state.DRAW_ALL = true;
        static const gX_U32 layerFlags[2] = {
            RENDERER_MODULE_RAYMARCH, RENDERER_MODULE_GFX };
        const char *transparentBg = getenv("GX_GFX_TRANSPARENT_BG");
        gX_U8 layerBlends[2] = {
            GX_COMPOSITE_BLEND_ALPHA, GX_COMPOSITE_BLEND_SCREEN };
        gX_RendererGFX *gfxHeap = renderer.gfx.data;
        if ((transparentBg && transparentBg[0] == '1')
            || (gfxHeap && gfxHeap->transparentBackground))
            layerBlends[1] = GX_COMPOSITE_BLEND_ALPHA;
        gX_Compositor_SetLayers(&renderer.compositor, 2, layerFlags,
            layerBlends);
        renderer.state.useCompositor = 1;
    } else {
        renderer.state.useCompositor = 0;
        gX_Compositor_SetLayers(&renderer.compositor, 0, NULL, NULL);
        renderer.raymarch.state.UPDATE_ALL = false;
        renderer.raymarch.state.DRAW_ALL = false;
    }
}

//opencode: backdrop-only composite (GX_GFX_RAYMARCH_COMPOSITE=2) — the
//   raymarch layer alone through the same compositor/pipeline, the reference
//   frame for the true-alpha compositing gate.
gX_I0 gX_Renderer_RaymarchComposite_SetBackdrop(gX_I1 on) {
    if (on) {
        renderer.raymarch.state.UPDATE_ALL = true;
        renderer.raymarch.state.DRAW_ALL = true;
        static const gX_U32 layerFlags[1] = { RENDERER_MODULE_RAYMARCH };
        static const gX_U8 layerBlends[1] = { GX_COMPOSITE_BLEND_ALPHA };
        gX_Compositor_SetLayers(&renderer.compositor, 1, layerFlags,
            layerBlends);
        renderer.state.useCompositor = 1;
    } else {
        gX_Renderer_RaymarchComposite_Set(0);
    }
}

gX_I0 gX_Renderer_Draw(gX_I0) {

    //opencode: compositor path — when active, handles layer compositing
    //   to canvas and skips the module draw macro (layers define all
    //   drawable modules for the scene).
    //   If the scene already composited its own layers during its Draw
    //   (gX_Renderer_SetCompositorDrawn), skip to avoid double-compositing.
    if (renderer.state.useCompositor && !renderer.compositorDrawnThisFrame) {
        GX_PROFILE_ZONE_BEGIN("renderer:compositor");
        gX_GL_State_Push();
        gX_Compositor_Draw(&renderer.compositor, &renderer.canvas);
        gX_GL_State_Pop();
        GX_PROFILE_ZONE_END("renderer:compositor");
    } else if (!renderer.compositorDrawnThisFrame) {
        //opencode: background has an extra argument, so draw it explicitly in
        //   the legacy module path.
        {
            GX_PROFILE_ZONE_BEGIN("renderer:background");
            gX_GL_State_Push();
            gX_RendererBackground *backgrounds = renderer.background.data;
            for (gX_U32 i = 0; i < renderer.background.amount; i++) {
                if (!renderer.background.state.DRAW_ALL) break;
                if (!renderer.background.meta[i].DRAW) continue;
                gX_RendererBackground *background = &backgrounds[i];
                gX_RendererBackground_Draw(background,0);
            }
            gX_GL_State_Pop();
            GX_PROFILE_ZONE_END("renderer:background");
        }

        //opencode: all other modules from GX_RENDERER_MODULES (drawarg==0)
        GX_PROFILE_ZONE_BEGIN("renderer:modules");
#define X(field, type, flag, scene, drawarg) RENDERER_MODULE_DRAW(field, type, flag, scene, drawarg)
        GX_RENDERER_MODULES
#undef X
        GX_PROFILE_ZONE_END("renderer:modules");
    }

    if (renderer.state.usePP) {
        GX_PROFILE_ZONE_BEGIN("renderer:postprocess");
        gX_GL_State_Push();
        gX_RendererPostProcess_Draw(&renderer.pp);
        //opencode: feedback capture — use glBlitFramebuffer instead of Texture_Draw
        //   This is a direct GPU pixel copy with zero shader overhead. Equivalent to
        //   the old Texture_Draw path but avoids the batch system, vertex shader,
        //   fragment shader, and batch flush — just one GPU copy instruction.
        gX_RHI_Blit(renderer.canvas.id, renderer.feedback.id,
                     0, 0, maindata.renderWidth, maindata.renderHeight,
                     0, 0, maindata.renderWidth, maindata.renderHeight,
                     gX_RHI_COLOR_BUFFER_BIT, gX_RHI_NEAREST);
        gX_GL_State_Pop();
        GX_PROFILE_ZONE_END("renderer:postprocess");
    }

}
