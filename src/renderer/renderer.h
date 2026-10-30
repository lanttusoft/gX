/**********************************************************************************************
*
*   [gX: src/renderer/renderer.h]
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


#ifndef RENDERER_H
#define RENDERER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/heap.h"

#include "analog.h"
#include "background.h"
#include "clock.h"
#include "fractal.h"
#include "generate.h"
#include "gfx.h"
#include "postprocess.h"
#include "raymarch.h"
#include "sky.h"
#include "teletext.h"
#include "textwriter.h"
#include "water.h"
#include "wireframe.h"
#include "oscillo.h"
#include "lissajous.h"
#include "spectrum.h"
#include "ws2d.h"

#include "compositor.h"
    
//opencode: single source of truth for all wired-up renderer modules.
// Each entry: (structField, TypeCase, FlagSuffix, sceneEnum, drawHasExtraArg)
#define GX_RENDERER_MODULES \
    X(analog, Analog, ANALOG, gX_SCENE_RENDERER_ANALOG, 0) \
    X(background, Background, BACKGROUND, gX_SCENE_RENDERER_BACKGROUND, 1) \
    X(clock, Clock, CLOCK, gX_SCENE_RENDERER_CLOCK, 0) \
    X(fractal, Fractal, FRACTAL, gX_SCENE_RENDERER_FRACTAL, 0) \
    X(gfx, GFX, GFX, gX_SCENE_RENDERER_GFX, 0) \
    X(postprocess, PostProcess, PP, gX_SCENE_RENDERER_PP, 0) \
    X(raymarch, Raymarch, RAYMARCH, gX_SCENE_RENDERER_RAYMARCH, 0) \
    X(sky, Sky, SKY, gX_SCENE_RENDERER_SKY, 0) \
    X(teletext, Teletext, TELETEXT, gX_SCENE_RENDERER_TELETEXT, 0) \
    X(textwriter, TextWriter, TEXTWRITER, gX_SCENE_RENDERER_TEXTWRITER, 0) \
    X(water, Water, WATER, gX_SCENE_RENDERER_WATER, 0) \
    X(wireframe, Wireframe, WIREFRAME, gX_SCENE_RENDERER_WIREFRAME, 0) \
    X(generate, Generate, GENERATE, gX_SCENE_RENDERER_GENERATE, 0) \
    X(oscillo, Oscillo, OSCILLO, gX_SCENE_SYNTHVIS, 0) \
    X(lissajous, Lissajous, LISSAJOUS, gX_SCENE_SYNTHVIS, 0) \
    X(spectrum, Spectrum, SPECTRUM, gX_SCENE_SYNTHVIS, 0) \
    X(ws2d, WS2D, WS2D, gX_SCENE_RENDERER_GFX, 0)

//opencode: module flag enum — MUST be powers of 2 (bitmask) for selective enable/disable.
//Order matches GX_RENDERER_MODULES table entries.
typedef enum {
    RENDERER_MODULE_ANALOG     = 1 << 0,
    RENDERER_MODULE_BACKGROUND = 1 << 1,
    RENDERER_MODULE_CLOCK      = 1 << 2,
    RENDERER_MODULE_FRACTAL    = 1 << 3,
    RENDERER_MODULE_GFX        = 1 << 4,
    RENDERER_MODULE_PP         = 1 << 7,
    RENDERER_MODULE_RAYMARCH   = 1 << 8,
    RENDERER_MODULE_SKY        = 1 << 9,
    RENDERER_MODULE_TELETEXT   = 1 << 10,
    RENDERER_MODULE_TEXTWRITER = 1 << 11,
    RENDERER_MODULE_WATER      = 1 << 12,
    RENDERER_MODULE_WIREFRAME  = 1 << 13,
    RENDERER_MODULE_OSCILLO    = 1 << 14,
    RENDERER_MODULE_LISSAJOUS  = 1 << 15,
    RENDERER_MODULE_SPECTRUM   = 1 << 16,
    //opencode: world-space render texture manager (dynamic RTs for 3D models)
    RENDERER_MODULE_WS2D       = 1 << 17,
    //opencode: procedural pattern generator
    RENDERER_MODULE_GENERATE   = 1 << 18,
} gX_Renderer_ModuleFlag;

typedef struct gX_Renderer_State {
    
    gX_I1 FEEDBACK_LOADED;
    gX_I1 GLOW_LOADED;
    gX_I1 CANVAS_LOADED;
    
    gX_I1 FEEDBACK_LOAD_ASAP;
    gX_I1 GLOW_LOAD_ASAP;
    gX_I1 CANVAS_LOAD_ASAP;
    
    gX_I1 usePP;
    gX_I1 useCompositor;
    //ai: transient opaque hybrid visibility, never saved in scene presets.
    gX_I32 raymarchDepth;
    
} gX_Renderer_State;

typedef struct gX_Renderer {
    gX_I8 name[128];
    gX_I8 description[256];
    
    gX_I1 loaded;
    gX_Renderer_State state;
    gX_I1 masterIndexSwitchResponse;
    gX_I1 maindataRefreshResponse;
    
    gX_Heap analog;
    gX_Heap background;
    gX_Heap clock;
    gX_Heap fractal;
    gX_Heap generate;
    gX_Heap gfx;
    gX_Heap postprocess;
    gX_Heap raymarch;
    gX_Heap sky;
    gX_Heap teletext;
    gX_Heap textwriter;
    gX_Heap water;
    gX_Heap wireframe;
    gX_Heap oscillo;
    gX_Heap lissajous;
    gX_Heap spectrum;
    
    gX_Heap ws2d;
    
    gX_Heap renderTextures;
    
    gX_RenderTexture feedback;
    gX_RenderTexture glow;
    gX_RenderTexture canvas;

    //opencode: set by gX_Drawing_BeginTexture when binding the canvas FBO,
    //   cleared in gX_Renderer_BeginFrame (called from gX_Drawing_Begin).
    //   GLFW's gX_glfw_Drawing_End composites the canvas to the default
    //   framebuffer — this flag lets it skip the composite when no scene
    //   drew to the canvas this frame (e.g. title, scenes that draw
    //   directly to screen). Without the flag, the black canvas would
    //   cover up the title scene's GUI in GLFW mode.
    gX_I1 canvasDrawnThisFrame;

    //opencode: set to 1 when a GUI canvas draws to screen (gX_GUI_Canvas_Draw
    //   after buffer composite). GLFW's gX_glfw_Drawing_End checks this flag
    //   to skip its own redundant canvas composite, preventing the 2-canvas-copy
    //   visual artifact (the canvas texture drawn once by glfw aligned and once
    //   by the GUI SCENE template misaligned down-left).
    gX_I1 canvasDrawnByGUI;

    //opencode: set to 1 when a scene draws the compositor itself during its
    //   own Draw (e.g. battle composites its backdrop at the top of its draw,
    //   before its 3D/HUD content). gX_Renderer_Draw then skips the compositor
    //   branch to avoid double-compositing. Reset in gX_Renderer_BeginFrame.
    gX_I1 compositorDrawnThisFrame;

    gX_I1 ppLoaded;
    gX_RendererPostProcess pp;

    //opencode: compositor — layer-based compositing (opt-in, per-scene)
    gX_Compositor compositor;

} gX_Renderer;

extern gX_Renderer renderer;

//opencode: composite mode — the raymarch module renders the backdrop and the
//   gfx module blends over it (compositor layers, alpha). Off restores the
//   scene's own module set.
gX_I0 gX_Renderer_RaymarchComposite_Set(gX_I1 on);
//ai: enabling owns one GFX compositor layer; disabling restores the prior layer configuration.
gX_I0 gX_Renderer_RaymarchDepth_Set(gX_I32 mode);
//opencode: backdrop-only composite (GX_GFX_RAYMARCH_COMPOSITE=2) — the
//   raymarch layer alone, the reference frame for the true-alpha gate.
gX_I0 gX_Renderer_RaymarchComposite_SetBackdrop(gX_I1 on);

//opencode: canvas-drawn-this-frame tracking — GLFW's gX_glfw_Drawing_End
//   reads the flag to decide whether to composite the canvas. Set by
//   gX_Drawing_BeginTexture when binding the canvas, cleared by
//   gX_Renderer_BeginFrame at the start of each frame.
gX_I0 gX_Renderer_SetCanvasDrawn(gX_I1 drawn);
gX_I1 gX_Renderer_GetCanvasDrawn(gX_I0);
gX_I0 gX_Renderer_SetCanvasDrawnByGUI(gX_I1 drawn);
gX_I1 gX_Renderer_GetCanvasDrawnByGUI(gX_I0);
gX_U32 gX_Renderer_GetCanvasTextureId(gX_I0);
gX_I0 gX_Renderer_BeginFrame(gX_I0);

//opencode: compositor-drawn-this-frame tracking — a scene that composites
//   its own layers (e.g. battle) sets this so gX_Renderer_Draw skips the
//   compositor branch (avoids double-compositing the same frame).
gX_I0 gX_Renderer_SetCompositorDrawn(gX_I1 drawn);
gX_I1 gX_Renderer_GetCompositorDrawn(gX_I0);


gX_I32 gX_Renderer_Load(gX_I0);
gX_I32 gX_Renderer_Unload(gX_I0);

gX_I0 gX_Renderer_Update(gX_I0);
gX_I0 gX_Renderer_Draw(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_H
