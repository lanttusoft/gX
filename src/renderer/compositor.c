/**********************************************************************************************
*
*   [gX: src/renderer/compositor.c]
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


//opencode: compositor — layer-based compositing system
#include "compositor.h"

#include "../common/log.h"
#include "../common/macros.h"
#include "../main.h"
#include "../wrappers/wrappers.h"

// Module draw functions (with outputRT parameter)
#include "sky.h"
#include "gfx.h"
#include "raymarch.h"
#include "background.h"
#include "water.h"
#include "renderer.h"
#include "../rhi/rhi.h"
#include "../rhi/gl_state.h"

//opencode: all module headers are pulled in via renderer.h (included above).
//   Each module now exposes DrawTo(ptr, outputRT) for compositor routing.

// ---------------------------------------------------------------------------
// Blend mode → rlgl blend mode constants (from raylib rlgl.h)
// ---------------------------------------------------------------------------
#define GX_COMPOSITE_RL_BLEND_ALPHA      0
#define GX_COMPOSITE_RL_BLEND_ADDITIVE   1
#define GX_COMPOSITE_RL_BLEND_MULTIPLIED 2

static gX_I32 blend_to_rlmode(gX_Composite_BlendMode mode) {
    switch (mode) {
        case GX_COMPOSITE_BLEND_ALPHA:    return GX_COMPOSITE_RL_BLEND_ALPHA;
        case GX_COMPOSITE_BLEND_ADD:      return GX_COMPOSITE_RL_BLEND_ADDITIVE;
        case GX_COMPOSITE_BLEND_MULTIPLY: return GX_COMPOSITE_RL_BLEND_MULTIPLIED;
        case GX_COMPOSITE_BLEND_SCREEN:   return GX_COMPOSITE_RL_BLEND_ALPHA; // handled via custom separate in compositor_blend
        default:                          return GX_COMPOSITE_RL_BLEND_ALPHA;
    }
}

// ---------------------------------------------------------------------------
// Internal: blend src texture over dst RT with the given blend mode.
// Draws a fullscreen quad with src texture, using blend mode, into dst RT.
// ---------------------------------------------------------------------------
static gX_I0 compositor_blend(gX_Texture srcTex, gX_RenderTexture *dstRT,
    gX_Composite_BlendMode mode, gX_F32 srcW, gX_F32 srcH)
{
    if (!dstRT || dstRT->id == 0) return;
    gX_Drawing_BeginTexture(*dstRT);
        //opencode: SCREEN uses custom separate blend for correct 1-(1-src)*(1-dst)
        if (mode == GX_COMPOSITE_BLEND_SCREEN) {
            gX_Drawing_BeginBlendCustomSeparate(
                gX_BLEND_ONE, gX_BLEND_ONE_MINUS_SRC_COLOR,
                gX_BLEND_ONE, gX_BLEND_ONE_MINUS_SRC_ALPHA);
        } else {
            gX_Drawing_BeginBlend(blend_to_rlmode(mode));
        }
        gX_Texture_Draw(srcTex, (gX_Texture_Draw_Params){
            .dst = (gX_Rectangle){0, 0, srcW, srcH},
            .tint = gX_WHITE,
            .scale = {1, 1}
        });
        gX_Drawing_BeginBlend(GX_COMPOSITE_RL_BLEND_ALPHA);
    gX_Drawing_EndTexture();
}

// ---------------------------------------------------------------------------
// Dispatch: call the right module's DrawTo with the given output RT.
// Each case calls the module's DrawTo(gX_Renderer##type *ptr, gX_RenderTexture *target)
// with the correct instance pointer from the renderer heap. All modules expose
// DrawTo; the legacy Draw() macro aliases pass NULL (= default target).
// ---------------------------------------------------------------------------
static gX_I0 compositor_dispatch_layer(gX_U32 moduleFlag, gX_RenderTexture *target) {
    switch (moduleFlag) {
        // Modules with explicit DrawTo functions
        case RENDERER_MODULE_SKY: {
            if (renderer.sky.amount > 0 && renderer.sky.data) {
                gX_RendererSky *sky = (gX_RendererSky*)renderer.sky.data;
                gX_RendererSky_DrawTo(sky, target, NULL);
            }
            break;
        }
        case RENDERER_MODULE_GFX: {
            if (renderer.gfx.amount > 0 && renderer.gfx.data) {
                gX_RendererGFX *gfx = (gX_RendererGFX*)renderer.gfx.data;
                gX_RendererGFX_DrawTo(gfx, target);
                //ai: depth overlay is deliberately after raster effects, not a shared G-buffer yet.
                if (renderer.state.raymarchDepth == 1 && renderer.raymarch.amount > 0
                    && renderer.raymarch.data)
                {
                    gX_RendererRaymarch_DrawHybrid(renderer.raymarch.data,target,
                        gfx->bloom.hdrScene.wrapper.rt.depth);
                }
            }
            break;
        }
        case RENDERER_MODULE_RAYMARCH: {
            if (renderer.raymarch.amount > 0 && renderer.raymarch.data) {
                gX_RendererRaymarch *rm = (gX_RendererRaymarch*)renderer.raymarch.data;
                gX_RendererRaymarch_DrawTo(rm, target);
            }
            break;
        }
        case RENDERER_MODULE_BACKGROUND: {
            if (renderer.background.amount > 0 && renderer.background.data) {
                gX_RendererBackground *bg = (gX_RendererBackground*)renderer.background.data;
                gX_RendererBackground_Draw(bg, target);
            }
            break;
        }
        case RENDERER_MODULE_WATER: {
            if (renderer.water.amount > 0 && renderer.water.data) {
                gX_RendererWater *water = (gX_RendererWater*)renderer.water.data;
                gX_RendererWater_DrawTo(water, target, NULL, NULL, NULL);
            }
            break;
        }
        case RENDERER_MODULE_ANALOG: {
            if (renderer.analog.amount > 0 && renderer.analog.data) {
                gX_RendererAnalog *ptr = (gX_RendererAnalog*)renderer.analog.data;
                gX_RendererAnalog_DrawTo(ptr, target);
            }
            break;
        }
        case RENDERER_MODULE_CLOCK: {
            if (renderer.clock.amount > 0 && renderer.clock.data) {
                gX_RendererClock *ptr = (gX_RendererClock*)renderer.clock.data;
                gX_RendererClock_DrawTo(ptr, target);
            }
            break;
        }
        case RENDERER_MODULE_FRACTAL: {
            if (renderer.fractal.amount > 0 && renderer.fractal.data) {
                gX_RendererFractal *ptr = (gX_RendererFractal*)renderer.fractal.data;
                gX_RendererFractal_DrawTo(ptr, target);
            }
            break;
        }
        case RENDERER_MODULE_TELETEXT: {
            if (renderer.teletext.amount > 0 && renderer.teletext.data) {
                gX_RendererTeletext *ptr = (gX_RendererTeletext*)renderer.teletext.data;
                gX_RendererTeletext_DrawTo(ptr, target);
            }
            break;
        }
        case RENDERER_MODULE_TEXTWRITER: {
            if (renderer.textwriter.amount > 0 && renderer.textwriter.data) {
                gX_RendererTextWriter *ptr = (gX_RendererTextWriter*)renderer.textwriter.data;
                gX_RendererTextWriter_DrawTo(ptr, target);
            }
            break;
        }
        case RENDERER_MODULE_WIREFRAME: {
            if (renderer.wireframe.amount > 0 && renderer.wireframe.data) {
                gX_RendererWireframe *ptr = (gX_RendererWireframe*)renderer.wireframe.data;
                gX_RendererWireframe_DrawTo(ptr, target);
            }
            break;
        }
        case RENDERER_MODULE_GENERATE: {
            if (renderer.generate.amount > 0 && renderer.generate.data) {
                gX_RendererGenerate *ptr = (gX_RendererGenerate*)renderer.generate.data;
                gX_RendererGenerate_DrawTo(ptr, target);
            }
            break;
        }
        case RENDERER_MODULE_OSCILLO: {
            if (renderer.oscillo.amount > 0 && renderer.oscillo.data) {
                gX_RendererOscillo *ptr = (gX_RendererOscillo*)renderer.oscillo.data;
                gX_RendererOscillo_DrawTo(ptr, target);
            }
            break;
        }
        case RENDERER_MODULE_LISSAJOUS: {
            if (renderer.lissajous.amount > 0 && renderer.lissajous.data) {
                gX_RendererLissajous *ptr = (gX_RendererLissajous*)renderer.lissajous.data;
                gX_RendererLissajous_DrawTo(ptr, target);
            }
            break;
        }
        case RENDERER_MODULE_SPECTRUM: {
            if (renderer.spectrum.amount > 0 && renderer.spectrum.data) {
                gX_RendererSpectrum *ptr = (gX_RendererSpectrum*)renderer.spectrum.data;
                gX_RendererSpectrum_DrawTo(ptr, target);
            }
            break;
        }
        case RENDERER_MODULE_PP: {
            if (renderer.postprocess.amount > 0 && renderer.postprocess.data) {
                gX_RendererPostProcess *ptr = (gX_RendererPostProcess*)renderer.postprocess.data;
                gX_RendererPostProcess_DrawTo(ptr, target);
            }
            break;
        }
        case RENDERER_MODULE_WS2D: {
            if (renderer.ws2d.amount > 0 && renderer.ws2d.data) {
                gX_RendererWS2D *ptr = (gX_RendererWS2D*)renderer.ws2d.data;
                gX_RendererWS2D_DrawTo(ptr, target);
            }
            break;
        }

        default:
            gX_TraceLog_WRAPPERS(gX_LOG_WARNING,
                "COMPOSITOR: unhandled module flag 0x%X — layer skipped", moduleFlag);
            break;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
gX_I32 gX_Compositor_Init(gX_Compositor *cmp) {
    if (!cmp) return -1;
    cmp->layerCount = 0;
    cmp->poolLoaded = 0;
    cmp->active = 0;
    cmp->rtPool[0] = (gX_RenderTexture){0};
    cmp->rtPool[1] = (gX_RenderTexture){0};
    return 0;
}

gX_I32 gX_Compositor_DeInit(gX_Compositor *cmp) {
    if (!cmp) return -1;
    if (cmp->poolLoaded) {
        if (cmp->rtPool[0].id > 0) gX_RenderTexture_Unload(cmp->rtPool[0]);
        if (cmp->rtPool[1].id > 0) gX_RenderTexture_Unload(cmp->rtPool[1]);
        cmp->rtPool[0] = (gX_RenderTexture){0};
        cmp->rtPool[1] = (gX_RenderTexture){0};
        cmp->poolLoaded = 0;
    }
    cmp->active = 0;
    cmp->layerCount = 0;
    return 0;
}

gX_I0 gX_Compositor_Reload(gX_Compositor *cmp) {
    if (!cmp) return;
    if (cmp->poolLoaded) {
        if (cmp->rtPool[0].id > 0) gX_RenderTexture_Unload(cmp->rtPool[0]);
        if (cmp->rtPool[1].id > 0) gX_RenderTexture_Unload(cmp->rtPool[1]);
        cmp->rtPool[0] = (gX_RenderTexture){0};
        cmp->rtPool[1] = (gX_RenderTexture){0};
        cmp->poolLoaded = 0;
    }
}

gX_I0 gX_Compositor_SetLayers(gX_Compositor *cmp, gX_U8 count,
    const gX_U32 *moduleFlags, const gX_U8 *blendModes)
{
    if (!cmp) return;
    if (count > GX_COMPOSITOR_MAX_LAYERS) count = GX_COMPOSITOR_MAX_LAYERS;
    cmp->layerCount = count;
    for (gX_U8 i = 0; i < count; i++) {
        cmp->layers[i].moduleFlag = moduleFlags ? moduleFlags[i] : 0;
        cmp->layers[i].enabled = 1;
        cmp->layers[i].blend = blendModes ? (gX_Composite_BlendMode)blendModes[i]
                                          : GX_COMPOSITE_BLEND_ALPHA;
    }
    cmp->active = (count > 0);
}

gX_I0 gX_Compositor_Draw(gX_Compositor *cmp, gX_RenderTexture *targetRT) {
    if (!cmp || !cmp->active || !targetRT || targetRT->id == 0) return;
    if (cmp->layerCount == 0) return;

    // Lazy-load RT pool at render resolution
    if (!cmp->poolLoaded) {
        cmp->rtPool[0] = gX_RenderTexture_Load_HDR(maindata.renderWidth, maindata.renderHeight);
        cmp->rtPool[1] = gX_RenderTexture_Load_HDR(maindata.renderWidth, maindata.renderHeight);
        cmp->poolLoaded = 1;
    }
    if (cmp->rtPool[0].id == 0 || cmp->rtPool[1].id == 0) return;

    // Render each layer in order, ping-pong blending into rtPool[0]
    // Layer 0: clear rtPool[0] then draw directly into it, no blending needed.
    //   The clear (color+depth+stencil via ClearBG) prevents stale pixels from
    //   the previous frame leaking through when the layer is partial-coverage.
    //   Respects the layer's enabled flag — a disabled layer 0 leaves rtPool[0]
    //   transparent black (so subsequent layers show through).
    {
        gX_RenderTexture *outRT = &cmp->rtPool[0];
        gX_Drawing_BeginTexture(*outRT);
            gX_Drawing_ClearBG((gX_Color){0, 0, 0, 0});
        gX_Drawing_EndTexture();
        if (cmp->layers[0].enabled) {
            gX_GL_State_Push();
            compositor_dispatch_layer(cmp->layers[0].moduleFlag, outRT);
            gX_GL_State_Pop();
        }
    }

    // Layers 1..N-1: draw to rtPool[1], then blend over rtPool[0]
    for (gX_U8 i = 1; i < cmp->layerCount; i++) {
        if (!cmp->layers[i].enabled) continue;

        gX_RenderTexture *layerRT = &cmp->rtPool[1];
        // Clear the layer RT
        gX_Drawing_BeginTexture(*layerRT);
            gX_Drawing_ClearBG((gX_Color){0, 0, 0, 0});
        gX_Drawing_EndTexture();
        // Draw module content to it
        gX_GL_State_Push();
        compositor_dispatch_layer(cmp->layers[i].moduleFlag, layerRT);
        gX_GL_State_Pop();

        // Blend layerRT over accumulated rtPool[0]
        compositor_blend(layerRT->texture, &cmp->rtPool[0],
            cmp->layers[i].blend,
            (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
    }

    // Final composite: copy rtPool[0] to the target (renderer.canvas)
    {
        gX_Drawing_BeginTexture(*targetRT);
            gX_Texture_Draw(cmp->rtPool[0].texture, (gX_Texture_Draw_Params){
                .dst = (gX_Rectangle){0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight},
                .tint = gX_WHITE,
                .scale = {1, 1}
            });
        gX_Drawing_EndTexture();
    }
}

gX_I32 gX_Compositor_SetLayerEnabled(gX_Compositor *cmp, gX_U8 layerIndex, gX_I1 enabled) {
    if (!cmp) return -1;
    if (layerIndex >= cmp->layerCount) return -1;
    cmp->layers[layerIndex].enabled = enabled;
    return 0;
}
