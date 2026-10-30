/**********************************************************************************************
*
*   [gX: src/renderer/postprocess.h]
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


#ifndef RENDERER_POSTPROCESS_H
#define RENDERER_POSTPROCESS_H

#ifdef __cplusplus
extern "C" {
#endif
  
#include "../common/types.h"
    
#define gX_RENDERERPP_SHADERTYPES \
    X(BLUR) \
    X(BLUR_GAUSSIAN) \
    X(BLUR_H) \
    X(BLUR_V) \
    X(COLOR_BW) \
    X(COLOR_INVERT) \
    X(COLOR_BRIGHTNESS) \
    X(COLOR_CONTRAST) \
    X(COLOR_GAMMA) \
    X(COLOR_BCG) \
    X(COLOR_R) \
    X(COLOR_G) \
    X(COLOR_B) \
    X(COLOR_RGB) \
    X(COLOR_HSV) \
    X(COLOR_RGB2HSV) \
    X(COLOR_RGB2YUV) \
    X(COLOR_RGB2YIQ) \
    X(COLOR_HSV2RGB) \
    X(COLOR_YUV2RGB) \
    X(COLOR_YIQ2RGB) \
    X(COLOR_STYLE) \
    X(COLOR_PALETTE) \
    X(COLOR_NORMAL) \
    X(COLOR_TEXCOORD) \
    X(COLOR_FRAGPOS) \
    X(COLOR_FRAGCOLOR) \
    X(GEOMETRY) \
    X(GEOMETRY_SCALE) \
    X(GEOMETRY_ROTATE) \
    X(GEOMETRY_FLIPH) \
    X(GEOMETRY_FLIPV) \
    X(GEOMETRY_FLIPHV) \
    X(GEOMETRY_SKEW) \
    X(GEOMETRY_TILT) \
    X(GEOMETRY_RIPPLE) \
    X(GEOMETRY_SPIRAL) \
    X(GEOMETRY_WAVE) \
    X(GEOMETRY_WARP) \
    X(GEOMETRY_CRT) \
    X(MATRIX2D) \
    X(MATRIX3D) \
    X(MASK) \
    X(CHROMAKEY) \
    X(LENSFLARE) \
    X(ABERRATION) \
    X(CELSHADE) \
    X(FOG) \
    X(FEEDBACK) \
    X(BATTLE_TRANSITION) \
    X(BATTLE_TRANSITION2) \
    X(PIXELATION) \
    X(EDGE_DETECT) \
    X(VIGNETTE) \

enum gX_RendererPostProcess_ShaderTypes {
    gX_PP_NULL                = -1,
    //blur
#define X(type) gX_PP_##type,
    gX_RENDERERPP_SHADERTYPES
#undef X
};
const gX_I8 *gX_RendererPostProcess_ShaderTypes_String(gX_I32 shadertype);
//ai: number of chain shader types (keep in sync with the X list above).
#define GX_PP_TYPE_COUNT 54


#define gX_PP_PRESETS \
    X(TEST) \
    X(BLUR) \
    X(STRONGBLUR) \
    X(BATTLE) \
    X(BATTLE2) \
    X(VIGNETTE) \
    X(PIXELATION) \
    
enum gX_RendererPostProcess_Presets {
#define X(preset) gX_PP_PRESET_##preset,
    gX_PP_PRESETS
#undef X
};
const gX_I8 *gX_RendererPostProcess_Presets_String(gX_I32 preset);
//ai: number of built-in chain presets (keep in sync with the X list above).
#define GX_PP_PRESET_COUNT 7
    

typedef struct gX_RendererPostProcess_State {
    gX_I1 load;
    gX_I1 unload;
    gX_I1 reload;
    gX_I1 loaded;
    gX_I1 unloadAfter;
    
    gX_I1 enabled;
    
    gX_I1 pushShader;
    gX_I1 popShader;
    gX_I1 reloadShader;
    gX_I32 typeToken;
    gX_I32 indexToken;
    
    //opencode: === Composite Mode Flags ===
    //   These three flags control what gets drawn into pp->texture.start
    //   BEFORE the shader chain runs. They are evaluated in order:
    //
    //   1. feedback (bool):
    //      Draws renderer.canvas.texture — the previous frame's FINAL output
    //      (after all rendering + postprocessing) — as the background of the
    //      PP start RT. This creates a RECURSIVE FEEDBACK LOOP: each frame's
    //      output bleeds into the next frame's input. Essential for motion-blur
    //      trails, echo effects, and FF-style battle transitions.
    //      The feedback texture is stored in renderer.feedback.
    //
    //   2. inject (bool):
    //      Overlays a texture image (pp->texture.texture) onto the scene using
    //      up to 8 configurable slots with per-slot coords, scale, rotate,
    //      and color. Slot 0 can use animated Background params (coordsParam,
    //      scaleParam, rotateParam, colorParam) for dynamic movement.
    //      Useful for HUD elements, lens flares, animated overlays.
    //
    //   3. injectCanvas (bool):
    //      Draws renderer.canvas.texture AGAIN after the inject step.
    //      This lets you re-process the current frame's scene content through
    //      the shader chain (e.g., apply a blur to the scene but not to the
    //      injected overlay). Without injectCanvas, the shader chain only
    //      processes the feedback composite + injected texture.
    //
    //   All three can be combined: feedback creates the trail, inject adds
    //   an overlay, injectCanvas ensures the current scene is processed too.
    gX_I1 feedback;
    gX_I1 inject;
    gX_I1 injectCanvas;
    //opencode: injectCanvas blend mode and tint (ALPHA/ADDITIVE etc., white=no tint)
    gX_I32 injectCanvasBlendMode;
    gX_Color injectCanvasTint;
    
    //opencode: feedback decay and blend mode for temporal trails
    gX_F32 feedbackDecay;           // 0.0 = full overwrite (no trail), 0.5 = half-life fade, 1.0 = frozen
    gX_I32 feedbackBlendMode;       // gX_BLEND_ALPHA / ADDITIVE / MULTIPLIED etc.
    
} gX_RendererPostProcess_State;

//lets experiment with bg parameters
#include "background/param.h"

//ai: the shipped chain shaders declare `uniform vec4 uniforms[16]`; the
//   engine used to upload only 8, so uniforms 8..15 were silently zero.
#define GX_PP_UNIFORM_AMOUNT 16
#define GX_PP_INJECT_SLOTS  8
#define GX_PP_SHADERS_MAX   8
#define GX_PP_BATTLE_TRANSITION_SPEED 0.005f

typedef struct gX_RendererPostProcess_Inject {
    gX_I1 useCustomTexture;
    gX_I1 useParam;
    gX_I1 connectScale;
    
    gX_U32 amount;
    
    gX_F32v2 coords[GX_PP_INJECT_SLOTS];
    gX_F32v2 scale[GX_PP_INJECT_SLOTS];
    gX_F32 rotate[GX_PP_INJECT_SLOTS];
    gX_Color color[GX_PP_INJECT_SLOTS];
    gX_I32 blendMode[GX_PP_INJECT_SLOTS];
    
    gX_RenBG_ParamV2 coordsParam;
    gX_RenBG_ParamV2 scaleParam;
    gX_RenBG_ParamScalar rotateParam;
    gX_RenBG_ParamV4 colorParam;
    
    gX_Texture *texture;
    //opencode: per-slot texture overrides (NULL = use shared texture for that slot)
    gX_Texture *textures[GX_PP_INJECT_SLOTS];
    //opencode: per-slot texture paths for schema serialization (empty = not loaded via path)
    gX_I8 texturePaths[GX_PP_INJECT_SLOTS][256];
} gX_RendererPostProcess_Inject;

typedef struct gX_RendererPostProcess_ShaderUniform {
    gX_F32v4 value;
    gX_F32v4 deltaAdd; //add
    gX_F32v4 deltaMul; //mul
} gX_RendererPostProcess_ShaderUniform;

typedef struct gX_RendererPostProcess_Shader {
    gX_I1       loadAsap;
    gX_I1       loaded;
    gX_I1       reload;
    gX_TextLine name;
    gX_TextLine path;
    gX_I32      type;
    gX_I32      version;
    gX_Shader   shader;
    
    gX_I32 locTexInput;
    gX_I32 locUniform;
    gX_I32 locTime;
    gX_I32 locRes;
    gX_I32 locFeedback;
    
    gX_U32 loops;
    gX_I32 blendMode;
    gX_I32 filterMode;
    gX_I32 wrapMode;
    gX_I1 blendChanged;
    gX_I1 filterChanged;
    gX_I1 wrapChanged;
    
    gX_U32 uniformsAmount;
    gX_RendererPostProcess_ShaderUniform uniforms[GX_PP_UNIFORM_AMOUNT];
    
    gX_F32 time;
    
} gX_RendererPostProcess_Shader;

typedef struct gX_RendererPostProcess_Texture {
    gX_Texture texture;
    
    gX_F32 renderScale;             // 0.25-1.0, RTs created at this fraction of render resolution
    gX_I1 renderScaleChanged;       // triggers RT recreation on next Update
    
    gX_RenderTexture start;
    gX_I32 startBlendMode;
    gX_I32 startFilterMode;
    gX_I32 startWrapMode;
    gX_I1 blendChanged;
    gX_I1 filterChanged;
    gX_I1 wrapChanged;
    
    gX_RenderTexture ping;
    gX_RenderTexture pong;
} gX_RendererPostProcess_Texture;

//typedef struct gX_RendererPostProcess_Pipe;
    
typedef struct gX_RendererPostProcess {
    gX_I8 name[128];
    gX_I8 description[256];
    
    gX_RendererPostProcess_State state;
    gX_RendererPostProcess_Inject inject;
    
    gX_U32 shadersAmount;
    gX_RendererPostProcess_Shader shaders[GX_PP_SHADERS_MAX];
    
    gX_RendererPostProcess_Texture texture;
    
} gX_RendererPostProcess;

gX_I32 gX_RendererPostProcess_Shaders_Clear(gX_RendererPostProcess *pp);
gX_I32 gX_RendererPostProcess_Shader_Push(  gX_RendererPostProcess *pp, gX_I32 type);
//ai: chain editing (editor panel/authoring): remove an entry and shift the
//   rest down; move an entry up/down the order. Both no-op out of range.
gX_I32 gX_RendererPostProcess_Shader_Remove(gX_RendererPostProcess *pp,
    gX_U32 index);
gX_I32 gX_RendererPostProcess_Shader_Move(gX_RendererPostProcess *pp,
    gX_U32 index, gX_I32 delta);
//ai: recompile one chain entry's program in place (keeps type, name, loops,
//   modes and uniforms), for shader editing/hot-reload.
gX_I32 gX_RendererPostProcess_Shader_Reload(gX_RendererPostProcess *pp,
    gX_U32 index);
//ai: inject-slot texture assignment (panel/authoring): copies the path and
//   loads the texture through the resource manager, mirroring the preset
//   loader; slot 0..GX_PP_INJECT_SLOTS-1.
gX_I0 gX_RendererPostProcess_Inject_SetTexturePath(gX_RendererPostProcess *pp,
    gX_I32 slot, const gX_I8 *path);
//ai: start-texture assignment (panel/authoring): loads the chain input and
//   applies the start filter/wrap.
gX_I0 gX_RendererPostProcess_Texture_SetStartPath(gX_RendererPostProcess *pp,
    const gX_I8 *path);
gX_I32 gX_RendererPostProcess_Shader_Pop(   gX_RendererPostProcess *pp);

gX_I32 gX_RendererPostProcess_Preset_BuiltIn(gX_RendererPostProcess *pp, gX_I32 index);
gX_I32 gX_RendererPostProcess_Preset_Load(gX_RendererPostProcess *pp, const gX_I8 *path);
gX_I32 gX_RendererPostProcess_Preset_Save(gX_RendererPostProcess *pp, const gX_I8 *path);

gX_I32 gX_RendererPostProcess_Load(gX_RendererPostProcess *pp);
gX_I32 gX_RendererPostProcess_Unload(gX_RendererPostProcess *pp);

gX_I0 gX_RendererPostProcess_Update(gX_RendererPostProcess *pp);
//opencode: DrawTo with explicit output RT (NULL = renderer.canvas). Only the
//   final shader pass is redirected; the start/ping/pong chain is unchanged.
gX_I0 gX_RendererPostProcess_DrawTo(gX_RendererPostProcess *pp, gX_RenderTexture *outputRT);
#define gX_RendererPostProcess_Draw(pp) gX_RendererPostProcess_DrawTo((pp), NULL)

#ifdef __cplusplus
}
#endif

#endif //RENDERER_POSTPROCESS_H
