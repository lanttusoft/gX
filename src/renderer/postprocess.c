/**********************************************************************************************
*
*   [gX: src/renderer/postprocess.c]
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
#include <stdlib.h>

#include "postprocess.h"
#include "postprocess/schema2.h"

#include "../common/log.h"
#include "../common/error.h"
#include "../common/enum.h"
#include "../common/macros.h"
#include "../common/utils.h"

#include "../main.h"
#include "../resource/resource.h"
#include "../wrappers/wrappers.h"

#include "renderer.h"
#include "../rhi/rhi.h"
#include "../rhi/gl_state.h"

#define gX_ENUMNAME_CASE(name,stri) \
    case name: return stri; break;

const gX_I8 *gX_RendererPostProcess_ShaderTypes_String(gX_I32 shadertype) {
#define X(type) gX_ENUMNAME_CASE(gX_PP_##type,#type);
    switch (shadertype) {
        gX_RENDERERPP_SHADERTYPES
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_RendererPostProcess_Presets_String(gX_I32 preset) {
#define X(preset) gX_ENUMNAME_CASE(gX_PP_PRESET_##preset,#preset);
    switch (preset) {
        gX_PP_PRESETS
        default: return "undefined";
    }
#undef X
}

static gX_RendererPostProcess *s_ppHooked = NULL;
static gX_I1 s_ppCheckDone = 0;
//ai: opt-in probe is compiled out of the shipping image.
#ifndef GX_LEAN_RELEASE
static gX_I0 gX_RendererPostProcess_Probe_Routing(gX_I0);
static gX_I0 gX_RendererPostProcess_Probe_Target(const gX_I8 *Stage, gX_RenderTexture Target);
#endif

//ai: start-texture assignment for the panel (see postprocess.h).
gX_I0 gX_RendererPostProcess_Texture_SetStartPath(gX_RendererPostProcess *pp,
    const gX_I8 *path) {
    if (!pp || !path || !path[0]) return;
    gX_Texture *texture = gX_Resource_LoadTexture(path);
    if (!texture) return;
    pp->texture.texture = *texture;
    gX_Texture_SetFilter(pp->texture.texture, pp->texture.startFilterMode);
    gX_Texture_SetWrap(pp->texture.texture, pp->texture.startWrapMode);
}

//ai: inject-slot texture assignment for the panel (see postprocess.h).
gX_I0 gX_RendererPostProcess_Inject_SetTexturePath(gX_RendererPostProcess *pp,
    gX_I32 slot, const gX_I8 *path) {
    if (!pp || slot < 0 || slot >= GX_PP_INJECT_SLOTS) return;
    snprintf((char *)pp->inject.texturePaths[slot],
        sizeof(pp->inject.texturePaths[slot]), "%s",
        path ? (const char *)path : "");
    if (!path || !path[0]) {
        pp->inject.textures[slot] = NULL;
        return;
    }
    gX_Texture *texture = gX_Resource_LoadTexture(path);
    if (texture) pp->inject.textures[slot] = texture;
}

gX_I32 gX_RendererPostProcess_Preset_BuiltIn(gX_RendererPostProcess *pp, gX_I32 index) {
    gX_TRACELOG_PROLOGUE("RendererPostProcess_Preset_BuiltIn","RENDERER_PP: Loading builtin pp preset")
    
    gX_RendererPostProcess_Shaders_Clear(pp);
    
    switch (index) {
        case gX_PP_PRESET_TEST: {
            gX_RendererPostProcess_Shader_Push(pp, gX_PP_GEOMETRY_SCALE);
            pp->shaders[0].uniforms[0].value = (gX_F32v4){1.001f,1.001f,1.0f,1.0f};
            break;
        }
        case gX_PP_PRESET_BATTLE: {
            gX_RendererPostProcess_Shader_Push(pp, gX_PP_BATTLE_TRANSITION);
            //opencode: uniforms[0].x = transition progress (0→1, advanced in Update)
            //          uniforms[0].y = feedback blend factor
            pp->shaders[0].uniforms[0].value = (gX_F32v4){0.0f, 0.5f, 0.0f, 0.0f};
            //opencode: feedback = previous frame's PP output for motion-blur trail
            //          injectCanvas = current frame's scene so the shader has content
            //          inject = disabled (no texture overlay)
            pp->state.feedback = true;
            pp->state.injectCanvas = true;
            pp->state.inject = false;
            break;
        }
        case gX_PP_PRESET_BATTLE2: {
            gX_RendererPostProcess_Shader_Push(pp, gX_PP_BATTLE_TRANSITION2);
            pp->shaders[0].uniforms[0].value = (gX_F32v4){0.0f, 0.5f, 0.0f, 0.0f};
            pp->state.feedback = true;
            pp->state.injectCanvas = true;
            pp->state.inject = false;
            break;
        }
        case gX_PP_PRESET_BLUR: {
            //opencode: gaussian blur with 4 iterations = strong effect
            pp->texture.renderScale = 0.5f;
            pp->texture.renderScaleChanged = 1;
            gX_RendererPostProcess_Shader_Push(pp, gX_PP_BLUR_GAUSSIAN);
            pp->shaders[0].loops = 4;
            pp->shaders[0].uniforms[0].value = (gX_F32v4){0.0f, 1.0f, 0.0f, 0.0f};
            pp->shaders[0].uniforms[0].deltaAdd = (gX_F32v4){0.0f, 1.0f, 0.0f, 0.0f};
            pp->state.feedback = false;
            pp->state.injectCanvas = true;
            pp->state.inject = false;
            break;
        }
        //ai: STRONGBLUR was advertised in the preset enum but had no handler
        //   (it produced an empty chain) — a stronger gaussian blur: half-res
        //   chain, 8 loops, blur radius growing +1 per loop.
        case gX_PP_PRESET_STRONGBLUR: {
            pp->texture.renderScale = 0.5f;
            pp->texture.renderScaleChanged = 1;
            gX_RendererPostProcess_Shader_Push(pp, gX_PP_BLUR_GAUSSIAN);
            pp->shaders[0].loops = 8;
            pp->shaders[0].uniforms[0].value = (gX_F32v4){0.0f, 1.0f, 0.0f, 0.0f};
            pp->shaders[0].uniforms[0].deltaAdd = (gX_F32v4){0.0f, 1.0f, 0.0f, 0.0f};
            pp->state.feedback = false;
            pp->state.injectCanvas = true;
            pp->state.inject = false;
            break;
        }
        case gX_PP_PRESET_VIGNETTE: {
            gX_RendererPostProcess_Shader_Push(pp, gX_PP_VIGNETTE);
            pp->shaders[0].uniformsAmount = 3;
            pp->shaders[0].uniforms[0].value = (gX_F32v4){0.5f, 0.3f, 0.8f, 0.0f};
            pp->shaders[0].blendMode = gX_BLEND_ALPHA;
            pp->state.feedback = false;
            pp->state.injectCanvas = true;
            pp->state.inject = false;
            break;
        }
        case gX_PP_PRESET_PIXELATION: {
            gX_RendererPostProcess_Shader_Push(pp, gX_PP_PIXELATION);
            pp->shaders[0].uniformsAmount = 1;
            pp->shaders[0].uniforms[0].value = (gX_F32v4){12.0f, 0.0f, 0.0f, 0.0f};
            pp->shaders[0].filterMode = gX_TEXTURE_FILTER_POINT;
            pp->state.feedback = false;
            pp->state.injectCanvas = true;
            pp->state.inject = false;
            break;
        }
        default: break;
    }
    //ai: the standalone postprocess scene has no upstream canvas — keep the
    //   test-texture inject as the chain input instead of injectCanvas, so a
    //   preset always has content to show (composite/loading is untouched).
    if (maindata.masterIndex == gX_SCENE_RENDERER_PP) {
        pp->state.inject = true;
        pp->inject.connectScale = true;
        pp->state.injectCanvas = false;
    }
    
    gX_TRACELOG_EPILOGUE("RENDERER_PP: Loading done")
    return 0;
}

//ai: new chains use the scene, not implicit stale feedback; explicit authored feedback remains available.
gX_I32 gX_RendererPostProcess_Load(gX_RendererPostProcess *pp) {
    gX_TRACELOG_PROLOGUE("RendererPostProcess_Load","RENDERER_PP: Loading pp renderer")
    
    strcpy(pp->name,"[gX] RendererPostProcess");
    strcpy(pp->description,"postprocessing");
    gX_TraceLog_POSTPROCESS(gX_LOG_GREEN,"name: %s",pp->name);
    gX_TraceLog_POSTPROCESS(gX_LOG_GREEN,"desc: %s",pp->description);
    
    pp->state.enabled = true;
    
    pp->state.load = true;
    pp->state.feedback = false;
    
    //opencode: feedback decay and blend (0.0 decay = full overwrite, ALPHA blend)
    pp->state.feedbackDecay = 0.0f;
    pp->state.feedbackBlendMode = gX_BLEND_ALPHA;
    
    //opencode: render scale (1.0 = full res, 0.5 = half res chain)
    pp->texture.renderScale = 1.0f;
    pp->texture.renderScaleChanged = 0;
    
    pp->state.inject = false;
    pp->state.injectCanvas = false;
    //ai: the standalone postprocess scene starts from its test texture so the
    //   chain always has content (--pp); presets/panel can still change it.
    if (maindata.masterIndex == gX_SCENE_RENDERER_PP) {
        pp->state.inject = true;
        pp->inject.connectScale = true;
    }
    pp->state.injectCanvasBlendMode = gX_BLEND_ALPHA;
    pp->state.injectCanvasTint = gX_WHITE;
    
    pp->inject.scale[0] = gX_F32v2_One();
    pp->inject.color[0] = gX_WHITE;
    for (gX_U32 i = 0; i < GX_PP_INJECT_SLOTS; i++) {
        pp->inject.blendMode[i] = gX_BLEND_ALPHA;
        //opencode: per-slot textures start NULL (use shared fallback)
        pp->inject.textures[i] = NULL;
        pp->inject.texturePaths[i][0] = '\0';
    }
    gX_RenBG_ParamV2_Init(&pp->inject.coordsParam,gX_F32v2_Zero());
    gX_RenBG_ParamV2_Init(&pp->inject.scaleParam,gX_F32v2_One());
    gX_RenBG_ParamScalar_Init(&pp->inject.rotateParam,0.0f);
    gX_RenBG_ParamV4_Init(&pp->inject.colorParam,gX_F32v4_One());
    
    gX_TRACELOG_EPILOGUE("RENDERER_PP: Loading done")
    return 0;
}

gX_I32 gX_RendererPostProcess_Unload(gX_RendererPostProcess *pp) {
    gX_TRACELOG_PROLOGUE("RendererPostProcess_Unload","RENDERER_PP: Unloading pp renderer")
    
    for (gX_U32 i = 0; i < pp->shadersAmount; i++) {
        gX_Shader_Unload(pp->shaders[i].shader);
    }
    
    gX_Texture_Unload(pp->texture.texture);
    gX_RenderTexture_Unload(pp->texture.start);
    gX_RenderTexture_Unload(pp->texture.ping);
    gX_RenderTexture_Unload(pp->texture.pong);
    
    pp->state.loaded = false;
    
    gX_TRACELOG_EPILOGUE("RENDERER_PP: Unloading done")
    return 0;
}

static gX_I0 gX_RendererPostProcess_Inject_Update(gX_RendererPostProcess_Inject *inj) {
    if (inj->useParam) {
        gX_RenBG_ParamV2_Update(&inj->coordsParam);
        gX_RenBG_ParamV2_Update(&inj->scaleParam);
        gX_RenBG_ParamScalar_Update(&inj->rotateParam);
        gX_RenBG_ParamV4_Update(&inj->colorParam);
    }
}

gX_I0 gX_RendererPostProcess_Update(gX_RendererPostProcess *pp) {
    if (pp->state.reload && maindata.backendLoaded) {
        pp->state.unload = true;
        pp->state.load = true;
        pp->state.reload = false;
    }
    if (pp->state.unload && maindata.backendLoaded) {
        if (pp->state.loaded) {
            gX_RendererPostProcess_Unload(pp);
        }
        pp->state.unload = false;
    }
    if (pp->state.load && maindata.backendLoaded && !(pp->state.loaded)) {
        { gX_Texture *_rsct = gX_Resource_LoadTexture("texture/texture.png"); if (_rsct) pp->texture.texture = *_rsct; }
        gX_Texture_SetFilter(pp->texture.texture, gX_TEXTURE_FILTER_BILINEAR);
        gX_Texture_SetWrap(pp->texture.texture, gX_TEXTURE_WRAP_CLAMP);
        
        pp->texture.startBlendMode = gX_BLEND_ALPHA;
        pp->texture.startFilterMode = gX_TEXTURE_FILTER_BILINEAR;
        pp->texture.startWrapMode = gX_TEXTURE_WRAP_CLAMP;
        //opencode: apply renderScale to PP RT dimensions
        gX_U32 ppW = (gX_U32)(maindata.renderWidth * pp->texture.renderScale);
        gX_U32 ppH = (gX_U32)(maindata.renderHeight * pp->texture.renderScale);
        if (ppW < 2) ppW = 2;
        if (ppH < 2) ppH = 2;
        pp->texture.start = gX_RenderTexture_Load_Parameters(ppW, ppH,
                                                            pp->texture.startFilterMode, pp->texture.startWrapMode);
        
        pp->texture.ping = gX_RenderTexture_Load_Parameters(ppW, ppH,
                                                            gX_TEXTURE_FILTER_BILINEAR, gX_TEXTURE_WRAP_CLAMP);
        pp->texture.pong = gX_RenderTexture_Load_Parameters(ppW, ppH,
                                                            gX_TEXTURE_FILTER_BILINEAR, gX_TEXTURE_WRAP_CLAMP);
        pp->state.loaded = true;
        pp->state.load = false;
        pp->texture.renderScaleChanged = 0;
    }
    //ai: headless authoring hooks (gates + scripted captures), applied once
    //   after the first successful load:
    //     GX_PP_PRESET=<index>  load a built-in chain preset
    //     GX_PP_INJECT=1        enable the test-texture inject (chain input)
    //     GX_PP_BADTYPE=1       probe the error path (invalid shader type)
    if (pp->state.loaded && &renderer.pp == pp && s_ppHooked != pp) {
        s_ppHooked = pp;
        const char *presetHook = getenv("GX_PP_PRESET");
        if (presetHook && presetHook[0]) {
            gX_I32 preset = (gX_I32)atoi(presetHook);
            gX_I32 result = gX_RendererPostProcess_Preset_BuiltIn(pp, preset);
            gX_TraceLog(gX_LOG_NOTICE, "PP_HOOK: preset=%d %s", preset,
                result == 0 ? "loaded" : "failed");
        }
        const char *injectHook = getenv("GX_PP_INJECT");
        if (injectHook && injectHook[0] == '1') {
            pp->state.inject = true;
            gX_TraceLog(gX_LOG_NOTICE, "PP_HOOK: inject=1");
        }
        const char *badHook = getenv("GX_PP_BADTYPE");
        if (badHook && badHook[0] == '1') {
            gX_I32 pushResult = gX_RendererPostProcess_Shader_Push(pp, 9999);
            const gX_ErrorInfo *info = gX_Error_Get();
            gX_TraceLog(gX_LOG_NOTICE, "PP_HOOK: badtype push=%d code=%d",
                pushResult, info ? (gX_I32)info->code : -1);
        }

    }
    //ai: headless preset round-trip hook (gate): save -> mutate -> reload and
    //   trace the restored values (mirrors GX_GFX_RAYMARCH_CHECK).
    {
        const char *checkHook = getenv("GX_PP_CHECK");
        if (checkHook && checkHook[0] && pp == &renderer.pp && !s_ppCheckDone
            && pp->state.loaded) {
            gX_I8 path[512];
            snprintf((char *)path, sizeof(path), "%s/pp_roundtrip.json",
                checkHook);
            if (pp->shadersAmount == 0)
                gX_RendererPostProcess_Shader_Push(pp, gX_PP_VIGNETTE);
            if (pp->shadersAmount > 0) {
                pp->shaders[0].loops = 3;
                pp->shaders[0].uniforms[0].value =
                    (gX_F32v4){0.111f, 0.222f, 0.333f, 0.444f};
            }
            pp->texture.renderScale = 0.75f;
            pp->inject.amount = 2;
            pp->inject.coords[1] = (gX_F32v2){1.5f, 2.5f};
            gX_I32 saveOK = gX_RendererPostProcess_Preset_Save_New(pp, path);
            if (pp->shadersAmount > 0) {
                pp->shaders[0].loops = 1;
                pp->shaders[0].uniforms[0].value =
                    (gX_F32v4){9.0f, 9.0f, 9.0f, 9.0f};
            }
            pp->texture.renderScale = 0.5f;
            pp->inject.amount = 0;
            gX_I32 loadOK = gX_RendererPostProcess_Preset_Load_New(pp, path);
            gX_I1 restored = (pp->shadersAmount > 0
                && pp->shaders[0].loops == 3
                && fabsf(pp->shaders[0].uniforms[0].value.x - 0.111f) < 1e-4f
                && fabsf(pp->texture.renderScale - 0.75f) < 1e-4f
                && pp->inject.amount == 2
                && fabsf(pp->inject.coords[1].x - 1.5f) < 1e-4f);
            gX_I1 pass = (saveOK == 0 && loadOK == 0 && restored);
            gX_TraceLog(pass ? gX_LOG_NOTICE : gX_LOG_ERROR,
                "GFX_PP_CHECK: %s save=%d load=%d restored=%d chain=%u",
                pass ? "PASS" : "FAIL", saveOK, loadOK, restored,
                pp->shadersAmount);
            s_ppCheckDone = 1;
        }
    }
    //opencode: re-create RTs when renderScale changes after initial load
    if (pp->texture.renderScaleChanged && pp->state.loaded) {
        gX_RenderTexture_Unload(pp->texture.start);
        gX_RenderTexture_Unload(pp->texture.ping);
        gX_RenderTexture_Unload(pp->texture.pong);
        gX_U32 ppW = (gX_U32)(maindata.renderWidth * pp->texture.renderScale);
        gX_U32 ppH = (gX_U32)(maindata.renderHeight * pp->texture.renderScale);
        if (ppW < 2) ppW = 2;
        if (ppH < 2) ppH = 2;
        pp->texture.start = gX_RenderTexture_Load_Parameters(ppW, ppH,
                                                            pp->texture.startFilterMode, pp->texture.startWrapMode);
        pp->texture.ping = gX_RenderTexture_Load_Parameters(ppW, ppH,
                                                            gX_TEXTURE_FILTER_BILINEAR, gX_TEXTURE_WRAP_CLAMP);
        pp->texture.pong = gX_RenderTexture_Load_Parameters(ppW, ppH,
                                                            gX_TEXTURE_FILTER_BILINEAR, gX_TEXTURE_WRAP_CLAMP);
        pp->texture.renderScaleChanged = 0;
    }
    if (pp->state.pushShader) {
        gX_RendererPostProcess_Shader_Push(pp, pp->state.typeToken);
        pp->state.pushShader = false;
    }
    if (pp->state.popShader) {
        gX_RendererPostProcess_Shader_Pop(pp);
        pp->state.popShader = false;
    }
    
    if (pp->texture.filterChanged && pp->state.loaded) {
        gX_Texture_SetFilter(pp->texture.start.texture,pp->texture.startFilterMode);
        pp->texture.filterChanged = false;
    }
    if (pp->texture.wrapChanged && pp->state.loaded) {
        gX_Texture_SetWrap(pp->texture.start.texture,pp->texture.startWrapMode);
        pp->texture.wrapChanged = false;
    }
    if (pp->texture.blendChanged) {
        //opencode: blend mode is applied in Draw_Start — just consume the flag here
        pp->texture.blendChanged = false;
    }

    //opencode: advance battle transition progress across frames
    //   deltaAdd only works within multi-loop shader passes (same frame),
    //   so we advance uniforms[0].x manually here for frame-to-frame animation.
    if (pp->shadersAmount > 0) {
        gX_I32 type = pp->shaders[0].type;
        if (type == gX_PP_BATTLE_TRANSITION || type == gX_PP_BATTLE_TRANSITION2) {
            if (pp->shaders[0].uniforms[0].value.x < 1.0f) {
                pp->shaders[0].uniforms[0].value.x += GX_PP_BATTLE_TRANSITION_SPEED;
                if (pp->shaders[0].uniforms[0].value.x > 1.0f)
                    pp->shaders[0].uniforms[0].value.x = 1.0f;
            }
        }
    }
    
    gX_RendererPostProcess_Inject_Update(&pp->inject);
    
    //opencode: time uses main clock; res uses scaled PP RT dimensions
    gX_F32v2 ppRes = (gX_F32v2){
        (gX_F32)(pp->texture.start.texture.width > 0 ? pp->texture.start.texture.width : maindata.renderWidth),
        (gX_F32)(pp->texture.start.texture.height > 0 ? pp->texture.start.texture.height : maindata.renderHeight)
    };
    for (gX_U32 i = 0; i < pp->shadersAmount; i++) {
        gX_Shader_SetValue_Float( pp->shaders[i].shader, pp->shaders[i].locTime, (gX_F32)maindata.timeElapsed);
        gX_Shader_SetValue_V2( pp->shaders[i].shader, pp->shaders[i].locRes, ppRes);
    }
    
}

//ai: assemble at the start target's actual size, retaining authored layers and default scene input.
static gX_I0 gX_RendererPostProcess_Draw_Start(gX_RendererPostProcess *pp) {
    gX_F32 Width = (gX_F32)pp->texture.start.texture.width;
    gX_F32 Height = (gX_F32)pp->texture.start.texture.height;
    gX_Drawing_BeginTexture(pp->texture.start);
#ifndef GX_LEAN_RELEASE
    gX_RendererPostProcess_Probe_Target("start", pp->texture.start);
#endif
    
        gX_Drawing_ClearBG(gX_BLACK);
        gX_Drawing_BeginBlend(pp->texture.startBlendMode);
        
        //opencode: feedback: composite previous frame's PP output as background
        //   renderer.feedback is updated AFTER the PP draw each frame,
        //   so it contains the previous frame's post-processed output.
        //   feedbackDecay controls the fade rate: 0.0 = full strength, 0.5 = half fade
        //   feedbackBlendMode controls how feedback composits (ALPHA, ADDITIVE, etc.)
        if (pp->state.feedback) {
            gX_U8 decayAlpha = (gX_U8)(255.0f * (1.0f - pp->state.feedbackDecay));
            gX_Drawing_BeginBlend(pp->state.feedbackBlendMode);
            gX_Texture_Draw(renderer.feedback.texture, (gX_Texture_Draw_Params){
                .dst = (gX_Rectangle){0.0f,0.0f,Width,Height},
                .tint = (gX_Color){255, 255, 255, decayAlpha},
                .scale = {1, 1}
            });
            gX_Drawing_BeginBlend(gX_BLEND_ALPHA);
        }
        
        //opencode: inject system — overlay texture(s) onto start RT
        //   useCustomTexture: when true and inject.texture != NULL, use that instead of the default
        //   connectScale: when true, scale = {1,1} fills full render area (not relative to tex size)
        //   blendMode[s]: per-slot blend mode (ALPHA, ADDITIVE, etc.)
        //   textures[s]: per-slot texture override (NULL = use shared fallback)
        if (pp->state.inject) {
            //opencode: select fallback source texture — custom or default
            gX_Texture *fallback = &pp->texture.texture;
            if (pp->inject.useCustomTexture && pp->inject.texture != NULL && pp->inject.texture->id > 0) {
                fallback = pp->inject.texture;
            }
            gX_U32 count = pp->inject.amount > 0 ? pp->inject.amount : 1;
            for (gX_U32 s = 0; s < count && s < GX_PP_INJECT_SLOTS; s++) {
                //opencode: per-slot texture selection
                gX_Texture *src = fallback;
                if (pp->inject.textures[s] != NULL && pp->inject.textures[s]->id > 0) {
                    src = pp->inject.textures[s];
                }
                if (src->id <= 0) continue;
                gX_F32v2 coords = pp->inject.coords[s];
                gX_F32v2 scale  = pp->inject.scale[s];
                gX_F32  rotate  = pp->inject.rotate[s];
                gX_Color color  = pp->inject.color[s];
                // slot 0 uses animated params if useParam is set
                if (s == 0 && pp->inject.useParam) {
                    coords = pp->inject.coordsParam.value;
                    scale  = pp->inject.scaleParam.value;
                    rotate = pp->inject.rotateParam.value;
                    color.r = (gX_U8)(255.0f*pp->inject.colorParam.value.x);
                    color.g = (gX_U8)(255.0f*pp->inject.colorParam.value.y);
                    color.b = (gX_U8)(255.0f*pp->inject.colorParam.value.z);
                    color.a = (gX_U8)(255.0f*pp->inject.colorParam.value.w);
                }
                //opencode: per-slot blend mode
                gX_Drawing_BeginBlend(pp->inject.blendMode[s]);
                //opencode: connectScale — scale relative to render dimensions
                gX_F32 drawW, drawH;
                if (pp->inject.connectScale) {
                    drawW = Width * scale.x;
                    drawH = Height * scale.y;
                } else {
                    drawW = (gX_F32)src->width * scale.x;
                    drawH = (gX_F32)src->height * scale.y;
                }
                gX_Texture_Draw(*src, (gX_Texture_Draw_Params){
                    .dst = (gX_Rectangle){coords.x, coords.y, drawW, drawH},
                    .rotation = rotate,
                    .tint = color,
                    .scale = {1, 1}
                });
            }
            //opencode: restore default blend mode after inject
            gX_Drawing_BeginBlend(gX_BLEND_ALPHA);
        }
        
        //opencode: injectCanvas — scene content with configurable blend mode and tint
        if ((pp->state.injectCanvas || (!pp->state.inject && !pp->state.feedback))
            && renderer.canvas.texture.id > 0) {
            gX_Drawing_BeginBlend(pp->state.injectCanvas ? pp->state.injectCanvasBlendMode : gX_BLEND_ALPHA);
            gX_Texture_Draw(renderer.canvas.texture, (gX_Texture_Draw_Params){
                .dst = (gX_Rectangle){0.0f,0.0f,Width,Height},
                .tint = pp->state.injectCanvas ? pp->state.injectCanvasTint : gX_WHITE,
                .scale = {1, 1}
            });
            gX_Drawing_BeginBlend(gX_BLEND_ALPHA);
        }
    
    gX_Drawing_EndTexture();
}

//ai: the committed iteration count identifies the current source, independent of empty authored slots.
static gX_Texture *gX_RendererPostProcess_Input(gX_RendererPostProcess *Pp, gX_U32 Iterations)
{
    if (Iterations == 0) return &Pp->texture.start.texture;
    return Iterations % 2 == 0 ? &Pp->texture.pong.texture : &Pp->texture.ping.texture;
}

//ai: presentation is separate from shader execution; skip a source/target alias before clearing it.
static gX_I0 gX_RendererPostProcess_Present(gX_Texture Input, gX_RenderTexture Target)
{
    if (!Input.id || Input.id == Target.texture.id) return;
    gX_Drawing_BeginTexture(Target);
#ifndef GX_LEAN_RELEASE
    gX_RendererPostProcess_Probe_Target("present", Target);
#endif
    gX_Drawing_BeginBlend(gX_BLEND_ALPHA);
    gX_Drawing_ClearBG(gX_BLACK);
    gX_Texture_Draw(Input, (gX_Texture_Draw_Params){
        .dst = {0, 0, (gX_F32)Target.texture.width, (gX_F32)Target.texture.height},
        .tint = gX_WHITE,
        .scale = {1, 1}
    });
    gX_Drawing_EndTexture();
}

//ai: matching native-size shader passes followed by one destination-size presentation.
// Empty unconfigured chains preserve the current scene; explicit inject/feedback layers still assemble.
gX_I0 gX_RendererPostProcess_DrawTo(gX_RendererPostProcess *pp, gX_RenderTexture *outputRT)
{
#ifndef GX_LEAN_RELEASE
    if (pp == &renderer.pp) gX_RendererPostProcess_Probe_Routing();
#endif
    if (!pp) return;
    if (pp->state.load && maindata.backendLoaded && !pp->state.loaded)
        gX_RendererPostProcess_Update(pp);
    if (pp->texture.start.texture.id == 0) return;
    gX_RenderTexture finalTarget = outputRT ? *outputRT : renderer.canvas;
    if (!finalTarget.id || !finalTarget.texture.id
        || finalTarget.texture.width <= 0 || finalTarget.texture.height <= 0) return;
    gX_I1 HasPass = false;
    for (gX_U32 i = 0; i < pp->shadersAmount; i++)
        if (pp->shaders[i].loops > 0) HasPass = true;
    gX_GL_State_Push();
    if (!HasPass && !pp->state.inject && !pp->state.feedback && !pp->state.injectCanvas)
    {
        gX_RendererPostProcess_Present(renderer.canvas.texture, finalTarget);
        gX_GL_State_Pop();
        return;
    }
    gX_RendererPostProcess_Draw_Start(pp);
    gX_U32 iterations = 0;
    gX_U32 amount = pp->shadersAmount;
    for (gX_U32 i = 0; i < amount; i++)
    {
        gX_U32 loops = pp->shaders[i].loops;
        for (gX_U32 k = 0; k < loops; k++)
        {
            gX_Texture *Input = gX_RendererPostProcess_Input(pp, iterations);
            gX_RenderTexture Target = iterations % 2 == 0 ? pp->texture.ping : pp->texture.pong;
            gX_U32 curW = Target.texture.width;
            gX_U32 curH = Target.texture.height;
            gX_F32v2 Resolution = {(gX_F32)curW, (gX_F32)curH};
            gX_Drawing_BeginTexture(Target);
#ifndef GX_LEAN_RELEASE
            gX_RendererPostProcess_Probe_Target("effect", Target);
#endif
            gX_Texture_SetFilter(*Input, pp->shaders[i].filterMode);
            gX_Texture_SetWrap(*Input, pp->shaders[i].wrapMode);
            gX_F32v4 unif[GX_PP_UNIFORM_AMOUNT] = { 0 };
            for (gX_U32 m = 0; m < GX_PP_UNIFORM_AMOUNT; m++)
            {
                gX_F32 kf = (gX_F32)k;
                gX_F32v4 value = pp->shaders[i].uniforms[m].value;
                gX_F32v4 deltaAdd = pp->shaders[i].uniforms[m].deltaAdd;
                gX_F32v4 deltaMul = pp->shaders[i].uniforms[m].deltaMul;
                unif[m].x = (value.x + kf*(deltaAdd.x)) * (kf*(deltaMul.x)+1.0f);
                unif[m].y = (value.y + kf*(deltaAdd.y)) * (kf*(deltaMul.y)+1.0f);
                unif[m].z = (value.z + kf*(deltaAdd.z)) * (kf*(deltaMul.z)+1.0f);
                unif[m].w = (value.w + kf*(deltaAdd.w)) * (kf*(deltaMul.w)+1.0f);
            }
            gX_Drawing_BeginBlend(pp->shaders[i].blendMode);
            gX_Drawing_ClearBG(gX_BLACK);
            gX_Drawing_BeginShader(pp->shaders[i].shader);
                gX_Shader_SetValue_Array(pp->shaders[i].shader, pp->shaders[i].locUniform,
                    unif, gX_SHADER_UNIFORM_VEC4, GX_PP_UNIFORM_AMOUNT);
                gX_Shader_SetValue_V2(pp->shaders[i].shader, pp->shaders[i].locRes, Resolution);
                gX_Shader_SetValue_TextureSlot(pp->shaders[i].shader, pp->shaders[i].locTexInput, *Input, 0);
                if (pp->shaders[i].locFeedback >= 0 && renderer.feedback.texture.id > 0)
                {
                    gX_Shader_SetValue_TextureSlot(pp->shaders[i].shader,
                        pp->shaders[i].locFeedback, renderer.feedback.texture, 15);
                }
                gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)curW, (gX_F32)curH);
            gX_Drawing_EndShader();
            gX_Drawing_BeginBlend(gX_BLEND_ALPHA);
            gX_Drawing_EndTexture();
            iterations++;
        }
    }
    gX_RendererPostProcess_Present(*gX_RendererPostProcess_Input(pp, iterations), finalTarget);
    gX_GL_State_Pop();
}

//ai: Debug witness invokes the production path with synthetic owned resources.
#include "postprocess/routing_probe.h"
