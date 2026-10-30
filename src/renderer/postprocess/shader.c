/**********************************************************************************************
*
*   [gX: src/renderer/postprocess/shader.c]
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


//opencode: full shader type mapping — all 48 types now have files

#include "../postprocess.h"

#include "../../common/enum.h"
#include "../../common/error.h"
#include "../../common/log.h"
#include "../../common/path.h"
#include "../../common/utils/shader.h"
#include "../../gxmath/vector4.h"
#include "../../wrappers/wrappers.h"
#include "../../rhi/rhi.h"

static const gX_I8 *gX_GetShaderString_Name(gX_I32 type) {
    switch (type) {
        case gX_PP_BLUR:              return "blur.frag";
        case gX_PP_BLUR_GAUSSIAN:     return "blur_gauss.frag";
        case gX_PP_BLUR_H:            return "blur_h.frag";
        case gX_PP_BLUR_V:            return "blur_v.frag";

        case gX_PP_COLOR_BW:          return "color_bw.frag";
        case gX_PP_COLOR_INVERT:      return "color_invert.frag";
        case gX_PP_COLOR_BRIGHTNESS:  return "color_brightness.frag";
        case gX_PP_COLOR_CONTRAST:    return "color_contrast.frag";
        case gX_PP_COLOR_GAMMA:       return "color_gamma.frag";
        case gX_PP_COLOR_BCG:         return "color_bcg.frag";
        case gX_PP_COLOR_R:           return "color_r.frag";
        case gX_PP_COLOR_G:           return "color_g.frag";
        case gX_PP_COLOR_B:           return "color_b.frag";
        case gX_PP_COLOR_RGB:         return "color_rgb.frag";
        case gX_PP_COLOR_HSV:         return "color_hsv.frag";
        case gX_PP_COLOR_RGB2HSV:     return "color_rgb2hsv.frag";
        case gX_PP_COLOR_RGB2YUV:     return "color_rgb2yuv.frag";
        case gX_PP_COLOR_RGB2YIQ:     return "color_rgb2yiq.frag";
        case gX_PP_COLOR_HSV2RGB:     return "color_hsv2rgb.frag";
        case gX_PP_COLOR_YUV2RGB:     return "color_yuv2rgb.frag";
        case gX_PP_COLOR_YIQ2RGB:     return "color_yiq2rgb.frag";
        case gX_PP_COLOR_STYLE:       return "color_style.frag";
        case gX_PP_COLOR_PALETTE:     return "color_palette.frag";
        case gX_PP_COLOR_NORMAL:      return "color_normal.frag";
        case gX_PP_COLOR_TEXCOORD:    return "color_texcoord.frag";
        case gX_PP_COLOR_FRAGPOS:     return "color_fragposition.frag";
        case gX_PP_COLOR_FRAGCOLOR:   return "color_fragcolor.frag";

        case gX_PP_GEOMETRY:          return "geometry.frag";
        case gX_PP_GEOMETRY_SCALE:    return "geometry_scale.frag";
        case gX_PP_GEOMETRY_ROTATE:   return "geometry_rotate.frag";
        case gX_PP_GEOMETRY_FLIPH:    return "geometry_fliph.frag";
        case gX_PP_GEOMETRY_FLIPV:    return "geometry_flipv.frag";
        case gX_PP_GEOMETRY_FLIPHV:   return "geometry_fliphv.frag";
        case gX_PP_GEOMETRY_SKEW:     return "geometry_skew.frag";
        case gX_PP_GEOMETRY_TILT:     return "geometry_tilt.frag";
        case gX_PP_GEOMETRY_RIPPLE:   return "geometry_ripple.frag";
        case gX_PP_GEOMETRY_SPIRAL:   return "geometry_spiral.frag";
        case gX_PP_GEOMETRY_WAVE:     return "geometry_wave.frag";
        case gX_PP_GEOMETRY_WARP:     return "geometry_warp.frag";
        case gX_PP_GEOMETRY_CRT:      return "geometry_crt.frag";

        case gX_PP_MATRIX2D:          return "matrix2d.frag";
        case gX_PP_MATRIX3D:          return "matrix3d.frag";

        case gX_PP_MASK:              return "mask.frag";
        case gX_PP_CHROMAKEY:         return "chromakey.frag";
        case gX_PP_LENSFLARE:         return "lensflare.frag";
        case gX_PP_ABERRATION:        return "aberration.frag";
        case gX_PP_CELSHADE:          return "celshade.frag";
        case gX_PP_FOG:               return "fog.frag";

        case gX_PP_FEEDBACK:          return "feedback.frag";
        case gX_PP_BATTLE_TRANSITION:  return "battle_transition.frag";
        case gX_PP_BATTLE_TRANSITION2: return "battle_transition2.frag";
        case gX_PP_PIXELATION:         return "pixelation.frag";
        case gX_PP_EDGE_DETECT:        return "edge_detect.frag";
        case gX_PP_VIGNETTE:           return "vignette.frag";
        default:                       return "undefined";
    }
}

static gX_I0 gX_GetShaderString(gX_I8 *bfr, gX_I32 type) {
    strcpy(bfr, GX_RESOURCE_DIR "/shader/postprocess/");
    strcat(bfr, gX_GetShaderString_Name(type));
}

gX_I32 gX_RendererPostProcess_Shaders_Clear(gX_RendererPostProcess *pp) {
    while (pp->shadersAmount > 0)
        gX_RendererPostProcess_Shader_Pop(pp);
    return 0;
}

gX_I32 gX_RendererPostProcess_Shader_Push(gX_RendererPostProcess *pp, gX_I32 type) {
    gX_U32 idx = pp->shadersAmount;
    if (idx >= 8) {
        GX_ERROR_SET(GX_ERR_INVALID, "postprocess chain full (max 8)");
        gX_TraceLog_POSTPROCESS(gX_LOG_WARNING, "Shader stack full (max 8)");
        return 1;
    }
    pp->shaders[idx].type = type;

    gX_I8 path[256];
    gX_GetShaderString(path, type);
    //ai: no silent success — a missing shader file reports through the
    //   error-code path (docs/error_handling.md); the loader's default-shader
    //   fallback would otherwise mask it with a valid id.
    if (!FileExists((const char *)path)) {
        GX_ERROR_SET(GX_ERR_NOT_FOUND, "postprocess shader '%s'", path);
        gX_TraceLog_POSTPROCESS(gX_LOG_WARNING, "Cannot load shader '%s'", path);
        return 1;
    }
    pp->shaders[idx].shader = gX_Shader_Load(0, path);

    if (pp->shaders[idx].shader.id == 0) {
        GX_ERROR_SET(GX_ERR_NOT_FOUND, "postprocess shader '%s'", path);
        gX_TraceLog_POSTPROCESS(gX_LOG_WARNING, "Cannot load shader '%s'", path);
        return 1;
    }

    strcpy(pp->shaders[idx].name, gX_GetShaderString_Name(type));
    //ai: chain uniforms are custom names — raylib's location table returns -1
    //   for those, which silently disabled every chain input/uniform (the
    //   pixelation chain was a 1:1 copy). Query the linked program directly;
    //   the desktop-GLSL130 dialect renames `input` to `inputTex`.
    gX_U32 program = pp->shaders[idx].shader.id;
    pp->shaders[idx].locUniform  = gX_RHI_GetUniformLocation(program, "uniforms");
    pp->shaders[idx].locTexInput = gX_RHI_GetUniformLocation(program, "input");
    if (pp->shaders[idx].locTexInput < 0)
        pp->shaders[idx].locTexInput = gX_RHI_GetUniformLocation(program, "inputTex");
    pp->shaders[idx].locTime     = gX_RHI_GetUniformLocation(program, "time");
    pp->shaders[idx].locRes      = gX_RHI_GetUniformLocation(program, "res");
    pp->shaders[idx].locFeedback = gX_RHI_GetUniformLocation(program, "feedback");

    for (gX_U32 i = 0; i < GX_PP_UNIFORM_AMOUNT; i++) {
        pp->shaders[idx].uniforms[i].value    = gX_F32v4_Zero();
        pp->shaders[idx].uniforms[i].deltaAdd = gX_F32v4_Zero();
        pp->shaders[idx].uniforms[i].deltaMul = gX_F32v4_Zero();
    }
    //ai: authoring default (the array capacity is GX_PP_UNIFORM_AMOUNT = 16;
    //   presets/panel raise it when a shader needs more).
    pp->shaders[idx].uniformsAmount = 8;
    pp->shaders[idx].blendMode  = gX_BLEND_ALPHA;
    pp->shaders[idx].filterMode = gX_TEXTURE_FILTER_POINT;
    pp->shaders[idx].wrapMode   = gX_TEXTURE_WRAP_REPEAT;
    pp->shaders[idx].loops      = 1;

    switch (type) {
        case gX_PP_BLUR:
        case gX_PP_BLUR_GAUSSIAN:
            pp->shaders[idx].uniformsAmount = 1;
            pp->shaders[idx].uniforms[0].value    = gX_F32v4_Zero();
            pp->shaders[idx].uniforms[0].deltaAdd = gX_F32v4_One();
            break;
        case gX_PP_COLOR_BCG:
            pp->shaders[idx].uniformsAmount = 3;
            pp->shaders[idx].uniforms[0].value = gX_F32v4_Zero();
            pp->shaders[idx].uniforms[1].value = gX_F32v4_One();
            pp->shaders[idx].uniforms[2].value = gX_F32v4_One();
            break;
        case gX_PP_GEOMETRY_SCALE:
            pp->shaders[idx].uniformsAmount = 2;
            pp->shaders[idx].uniforms[0].value = gX_F32v4_One();
            pp->shaders[idx].uniforms[1].value = gX_F32v4_Zero();
            break;
        case gX_PP_GEOMETRY_ROTATE:
            pp->shaders[idx].uniformsAmount = 2;
            pp->shaders[idx].uniforms[0].value = gX_F32v4_Zero();
            pp->shaders[idx].uniforms[1].value = gX_F32v4_Zero();
            break;
        case gX_PP_ABERRATION:
            pp->shaders[idx].uniformsAmount = 1;
            pp->shaders[idx].uniforms[0].value = gX_F32v4_One();
            break;
        case gX_PP_CHROMAKEY:
        case gX_PP_FOG:
            pp->shaders[idx].uniformsAmount = 1;
            pp->shaders[idx].uniforms[0].value = (gX_F32v4){0.0f, 1.0f, 0.0f, 0.01f};
            break;
        case gX_PP_GEOMETRY_SKEW:
        case gX_PP_GEOMETRY_TILT:
            pp->shaders[idx].uniformsAmount = 1;
            pp->shaders[idx].uniforms[0].value = (gX_F32v4){0.1f, 0.0f, 0.0f, 0.0f};
            break;
        case gX_PP_GEOMETRY_CRT:
            pp->shaders[idx].uniformsAmount = 1;
            pp->shaders[idx].uniforms[0].value = (gX_F32v4){0.05f, 0.0f, 0.0f, 0.0f};
            break;
        case gX_PP_LENSFLARE:
            pp->shaders[idx].uniformsAmount = 2;
            pp->shaders[idx].uniforms[0].value = (gX_F32v4){0.5f, 0.5f, 0.0f, 0.0f};
            break;
        case gX_PP_CELSHADE:
            pp->shaders[idx].uniformsAmount = 1;
            pp->shaders[idx].uniforms[0].value = (gX_F32v4){3.0f, 0.0f, 0.0f, 0.0f};
            break;
        case gX_PP_PIXELATION:
            pp->shaders[idx].uniformsAmount = 1;
            pp->shaders[idx].uniforms[0].value = (gX_F32v4){8.0f, 0.0f, 0.0f, 0.0f};
            break;
        case gX_PP_EDGE_DETECT:
            pp->shaders[idx].uniformsAmount = 1;
            pp->shaders[idx].uniforms[0].value = (gX_F32v4){0.1f, 1.0f, 0.0f, 0.0f};
            break;
        default: break;
    }

    pp->shadersAmount++;
    return 0;
}

gX_I32 gX_RendererPostProcess_Shader_Reload(gX_RendererPostProcess *pp,
    gX_U32 index) {
    if (!pp || index >= pp->shadersAmount) return 1;
    gX_RendererPostProcess_Shader *entry = &pp->shaders[index];
    gX_I8 reloadPath[256];
    gX_GetShaderString(reloadPath, entry->type);
    if (!FileExists((const char *)reloadPath)) {
        GX_ERROR_SET(GX_ERR_NOT_FOUND, "postprocess shader '%s'", reloadPath);
        return 1;
    }
    if (entry->shader.id > 0) gX_Shader_Unload(entry->shader);
    entry->shader = gX_Shader_Load(0, reloadPath);
    if (entry->shader.id == 0) {
        GX_ERROR_SET(GX_ERR_NOT_FOUND, "postprocess shader '%s'", reloadPath);
        return 1;
    }
    gX_U32 program = entry->shader.id;
    entry->locUniform = gX_RHI_GetUniformLocation(program, "uniforms");
    entry->locTexInput = gX_RHI_GetUniformLocation(program, "input");
    if (entry->locTexInput < 0)
        entry->locTexInput = gX_RHI_GetUniformLocation(program, "inputTex");
    entry->locTime = gX_RHI_GetUniformLocation(program, "time");
    entry->locRes = gX_RHI_GetUniformLocation(program, "res");
    entry->locFeedback = gX_RHI_GetUniformLocation(program, "feedback");
    return 0;
}

gX_I32 gX_RendererPostProcess_Shader_Remove(gX_RendererPostProcess *pp,
    gX_U32 index) {
    if (!pp || index >= pp->shadersAmount) return 1;
    if (pp->shaders[index].shader.id > 0)
        gX_Shader_Unload(pp->shaders[index].shader);
    for (gX_U32 i = index; i + 1 < pp->shadersAmount; i++)
        pp->shaders[i] = pp->shaders[i + 1];
    pp->shadersAmount--;
    memset(&pp->shaders[pp->shadersAmount], 0,
        sizeof(pp->shaders[pp->shadersAmount]));
    return 0;
}

gX_I32 gX_RendererPostProcess_Shader_Move(gX_RendererPostProcess *pp,
    gX_U32 index, gX_I32 delta) {
    if (!pp || index >= pp->shadersAmount) return 1;
    gX_I32 target = (gX_I32)index + delta;
    if (target < 0 || target >= (gX_I32)pp->shadersAmount) return 1;
    gX_RendererPostProcess_Shader swap = pp->shaders[index];
    pp->shaders[index] = pp->shaders[target];
    pp->shaders[target] = swap;
    return 0;
}

gX_I32 gX_RendererPostProcess_Shader_Pop(gX_RendererPostProcess *pp) {
    if (pp->shadersAmount == 0) return 0;
    gX_U32 idx = pp->shadersAmount - 1;
    if (pp->shaders[idx].shader.id > 0)
        gX_Shader_Unload(pp->shaders[idx].shader);
    pp->shadersAmount--;
    return 0;
}
