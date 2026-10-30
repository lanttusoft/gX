/**********************************************************************************************
*
*   [gX: src/renderer/compositor.h]
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


#ifndef RENDERER_COMPOSITOR_H
#define RENDERER_COMPOSITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

//opencode: compositor — a generic layer-based compositing system.
//   Manages a stack of render layers, each produced by a renderer module,
//   blends them together with configurable blend modes, and composites
//   the final result to the canvas.
//
//   Phase 1 supports SKY and GFX layers. Adding new layer types just
//   requires a case in gX_Compositor_Draw's dispatch switch.
//
//   Opt-in: only active for scenes with compositorLayerCount > 0 in
//   their scene config. All existing scenes continue using the old
//   direct-to-canvas module draw path.

#define GX_COMPOSITOR_MAX_LAYERS 8

//opencode: per-layer blend mode, applied when compositing this layer
//   over the accumulated result.
typedef enum {
    GX_COMPOSITE_BLEND_ALPHA,    // src over dst with alpha blending
    GX_COMPOSITE_BLEND_ADD,      // src + dst (linear dodge)
    GX_COMPOSITE_BLEND_MULTIPLY, // src * dst
    GX_COMPOSITE_BLEND_SCREEN,   // 1 - (1-src)*(1-dst)
} gX_Composite_BlendMode;

//opencode: a single layer in the compositor stack
typedef struct {
    gX_U32 moduleFlag;           // RENDERER_MODULE_* identifying which module
    gX_I1  enabled;
    gX_Composite_BlendMode blend;
} gX_Compositor_Layer;

//opencode: the compositor — owns the layer stack and an internal RT pool
//   for ping-pong blending. Layers are drawn in order 0..N-1, each
//   blended over the accumulated result.
typedef struct gX_Compositor {
    gX_U8 layerCount;
    gX_Compositor_Layer layers[GX_COMPOSITOR_MAX_LAYERS];

    //opencode: internal RT pool (2 entries for ping-pong between layers)
    gX_RenderTexture rtPool[2];
    gX_I1 poolLoaded;

    //opencode: set from scene config on switch
    gX_I1 active;
} gX_Compositor;

// Lifecycle
gX_I32 gX_Compositor_Init(gX_Compositor *cmp);
gX_I32 gX_Compositor_DeInit(gX_Compositor *cmp);

//opencode: unload the internal RT pool so it re-creates at the next Draw's
//   resolution (used on window resize via gX_Renderer_Canvas_Reload).
//   Keeps layer config + active state intact (unlike DeInit).
gX_I0 gX_Compositor_Reload(gX_Compositor *cmp);

//opencode: configure the layer stack from scene config data
gX_I0 gX_Compositor_SetLayers(gX_Compositor *cmp, gX_U8 count,
    const gX_U32 *moduleFlags, const gX_U8 *blendModes);

//opencode: draw all layers, blending them together, and composite the
//   final result to targetRT (typically renderer.canvas).
gX_I0 gX_Compositor_Draw(gX_Compositor *cmp, gX_RenderTexture *targetRT);

//opencode: runtime enable/disable a specific layer by index (0..layerCount-1).
//   Disabled layers are skipped during compositing. Returns -1 on invalid index.
gX_I32 gX_Compositor_SetLayerEnabled(gX_Compositor *cmp, gX_U8 layerIndex, gX_I1 enabled);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_COMPOSITOR_H
