/**********************************************************************************************
*
*   [gX: src/renderer/fractal.c]
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

#include "fractal.h"
#include "fractal/fractal_preset.h"

#include "../common/log.h"
#include "../common/macros.h"
#include "../common/utils.h"
#include "../wrappers/wrappers.h"
#include "../wrappers/input.h"

#include "../common/path.h"
#include "../main.h"
#include "renderer.h"
#include "../rhi/rhi.h"

//opencode: build the #define string for the shader based on current gen settings
static gX_I0 gX_RendererFractal_Shader_BuildDefines(gX_RendererFractal *frac) {
    gX_I8 *ptr = frac->shader.shader.defines;
    gX_I32 remaining = (gX_I32)sizeof(frac->shader.shader.defines);
    gX_I32 written = 0;
    
    // Precision mode
    written = snprintf(ptr, (gX_U64)remaining,
        "#define FRACTAL_PRECISION %d\n", frac->gen.precisionMode);
    if (written > 0 && written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    // Algorithm
    written = snprintf(ptr, (gX_U64)remaining,
        "#define FRACTAL_ALGORITHM %d\n", frac->gen.algorithm);
    if (written > 0 && written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "FRACTAL: shader defines:\n%s", frac->shader.shader.defines);
}

gX_I32 gX_RendererFractal_Load(gX_RendererFractal *frac) {
    gX_TRACELOG_PROLOGUE("RendererFractal_Load","RENDERER_FRACTAL: Loading fractal renderer")
    
    strcpy(frac->name,"Fractal");
    strcpy(frac->description,"julia set");
    gX_TraceLog_RENDERER(gX_LOG_GREEN,"name: %s",frac->name);
    gX_TraceLog_RENDERER(gX_LOG_GREEN,"desc: %s",frac->description);
    
    frac->gen.iterations = 128;
    frac->gen.seed = (gX_F32v2){-1.0f,0.0f};
    frac->gen.exponent = 2.0f;
    frac->gen.escape = 2.0f;
    //opencode: scale factor for internal perturbation (1.0 = no perturbation, 1e-10 = extreme deep zoom)
    frac->gen.scale = 1.0f;
    //opencode: fractal type (0=Julia, 1=Mandelbrot)
    frac->gen.fractalType = 0;
    //opencode: precision (0=32-bit, 1=48-bit double-double)
    frac->gen.precisionMode = 0;
    //opencode: algorithm (0=standard, 1=Burning Ship, 2=Tricorn, 3=Newton, 4=Phoenix)
    frac->gen.algorithm = 0;
    frac->gen.y_offset = (gX_F32v2){0.0f, 0.0f};
    //opencode: palette mode (0=sinusoid default)
    frac->gen.paletteMode = 0;
    //opencode: Julia morphing (0=off)
    frac->gen.morphSpeed = 0.0f;
    frac->gen.morphPhase = 0.0f;
    
    //opencode: orbit trap defaults (off)
    frac->trap.trapMode = 0;
    frac->trap.trapPos = gX_F32v2_Zero();
    frac->trap.trapRadius = 0.5f;
    
    //opencode: initialize tracking fields for shader reload detection
    frac->prevPrecisionMode = -1;
    frac->prevFractalType = -1;
    frac->prevAlgorithm = -1;
    
    //opencode: init oscillators (all off by default)
    for (gX_I32 i = 0; i < GX_FRACTAL_OSC_COUNT; i++) {
        frac->oscillators[i].target = -1; // disabled
        frac->oscillators[i].speed = 0.0f;
        frac->oscillators[i].amplitude = 0.0f;
        frac->oscillators[i].phase = 0.0f;
        frac->oscillators[i].offset = 0.0f;
        frac->oscillators[i].accumulator = 0.0f;
    }
    
    //opencode: mouse interaction init
    frac->mouseDragging = false;
    frac->mousePrevX = 0;
    frac->mousePrevY = 0;
    
    //opencode: ensure loadingstate is properly initialized
    frac->loadingstate.load = true;
    frac->loadingstate.loadAsap = true;
    
    frac->cam.pos = gX_F32v2_Zero();
    frac->cam.zoom = 1.0f;
    frac->cam.angle = 0.0f;
    frac->cam.tilt = gX_F32v2_Zero();
    
    frac->cam.pos_speed = gX_F32v2_Zero();
    frac->cam.zoom_speed = 0.0f;
    frac->cam.angle_speed = 0.0f;
    frac->cam.tilt_speed = gX_F32v2_Zero();
    
    frac->pal.palette_amount = 16;
    frac->pal.palette_factor = (gX_F32v3){1.01f,1.02f,1.03f};
    frac->pal.palette_speed = (gX_F32v3){0.1f,0.1f,0.1f};
    frac->pal.palette_phase = (gX_F32v3){0.0f,1.0f,2.0f};
    frac->pal.palette_amplitude = (gX_F32v3){1.0f,1.0f,1.0f};
    
    //opencode: build shader defines from gen settings (precision, algorithms, etc.)
    //   Must be called before shader wrapper loads (defines are used at compile time)
    gX_RendererFractal_Shader_BuildDefines(frac);
    
    frac->shader.shader.loadingState.load = true;
    frac->shader.shader.loadingState.loadAsap = true;
    frac->shader.shader.useDefaultVert = true;
    strcpy(frac->shader.shader.pathFrag,GX_PATH("shader", "fractal.frag"));
    frac->shader.shader.version = 330;
    
    gX_TRACELOG_EPILOGUE("RENDERER_FRACTAL: Loading done")
    return 0;
}

gX_I32 gX_RendererFractal_Unload(gX_RendererFractal *frac) {
    
    gX_Shader_Wrapper_Unload(&frac->shader.shader);
    
    return 0;
}

static gX_I0 gX_RendererFractal_Shader_GetLocs(gX_RendererFractal *frac) {
    frac->shader.locs.iterations_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"iterations");
    frac->shader.locs.exponent_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"exponent");
    frac->shader.locs.escape_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"escape");
    frac->shader.locs.seed_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"seed");
    
    frac->shader.locs.pos_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"pos");
    frac->shader.locs.zoom_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"zoom");
    frac->shader.locs.angle_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"angle");
    frac->shader.locs.tilt_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"tilt");
    
    frac->shader.locs.paletteamount_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"paletteAmount");
    frac->shader.locs.palettefactor_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"paletteFactor");
    frac->shader.locs.palettespeed_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"paletteSpeed");
    frac->shader.locs.palettephase_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"palettePhase");
    frac->shader.locs.paletteamplitude_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"paletteAmplitude");
    
    //opencode: scale and fractal type
    frac->shader.locs.scale_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"scale");
    frac->shader.locs.fractalType_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"fractalType");
    //opencode: algorithm-specific uniforms
    frac->shader.locs.y_offset_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"y_offset");
    //opencode: orbit trap uniforms
    frac->shader.locs.trapMode_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"trapMode");
    frac->shader.locs.trapPos_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"trapPos");
    frac->shader.locs.trapRadius_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"trapRadius");
    //opencode: palette mode + morph
    frac->shader.locs.paletteMode_loc = gX_Shader_GetLocation(frac->shader.shader.shader,"paletteMode");
    frac->shader.locs.morphLoc = gX_Shader_GetLocation(frac->shader.shader.shader,"morph");
}

static gX_I0 gX_RendererFractal_Shader_SetLocs(gX_RendererFractal *frac) {
    gX_Shader_SetValue_Float(frac->shader.shader.shader, frac->shader.shader.locs.locTime, (gX_F32)maindata.timeElapsed);
    gX_Shader_SetValue_V2(frac->shader.shader.shader, frac->shader.shader.locs.locRes,
                          (gX_F32v2){maindata.renderWidth,maindata.renderHeight});
    
    gX_Shader_SetValue_Int(frac->shader.shader.shader, frac->shader.locs.iterations_loc, frac->gen.iterations);
    gX_Shader_SetValue_Float(frac->shader.shader.shader, frac->shader.locs.exponent_loc, frac->gen.exponent);
    gX_Shader_SetValue_Float(frac->shader.shader.shader, frac->shader.locs.escape_loc, frac->gen.escape);
    gX_Shader_SetValue_V2(frac->shader.shader.shader, frac->shader.locs.seed_loc, frac->gen.seed);
    
    gX_Shader_SetValue_V2(frac->shader.shader.shader, frac->shader.locs.pos_loc, frac->cam.pos);
    gX_Shader_SetValue_Float(frac->shader.shader.shader, frac->shader.locs.zoom_loc, frac->cam.zoom);
    gX_Shader_SetValue_Float(frac->shader.shader.shader, frac->shader.locs.angle_loc, frac->cam.angle);
    gX_Shader_SetValue_V2(frac->shader.shader.shader, frac->shader.locs.tilt_loc, frac->cam.tilt);
    
    gX_Shader_SetValue_Int(frac->shader.shader.shader, frac->shader.locs.paletteamount_loc, frac->pal.palette_amount);
    gX_Shader_SetValue_V3(frac->shader.shader.shader, frac->shader.locs.palettefactor_loc, frac->pal.palette_factor);
    gX_Shader_SetValue_V3(frac->shader.shader.shader, frac->shader.locs.palettespeed_loc, frac->pal.palette_speed);
    gX_Shader_SetValue_V3(frac->shader.shader.shader, frac->shader.locs.palettephase_loc, frac->pal.palette_phase);
    gX_Shader_SetValue_V3(frac->shader.shader.shader, frac->shader.locs.paletteamplitude_loc, frac->pal.palette_amplitude);
    
    //opencode: scale and fractal type
    gX_Shader_SetValue_Float(frac->shader.shader.shader, frac->shader.locs.scale_loc, frac->gen.scale);
    gX_Shader_SetValue_Int(frac->shader.shader.shader, frac->shader.locs.fractalType_loc, frac->gen.fractalType);
    //opencode: algorithm-specific uniforms
    if (frac->shader.locs.y_offset_loc >= 0) {
        gX_Shader_SetValue_V2(frac->shader.shader.shader, frac->shader.locs.y_offset_loc, frac->gen.y_offset);
    }
    //opencode: orbit trap uniforms
    if (frac->shader.locs.trapMode_loc >= 0) {
        gX_Shader_SetValue_Int(frac->shader.shader.shader, frac->shader.locs.trapMode_loc, frac->trap.trapMode);
    }
    if (frac->shader.locs.trapPos_loc >= 0) {
        gX_Shader_SetValue_V2(frac->shader.shader.shader, frac->shader.locs.trapPos_loc, frac->trap.trapPos);
    }
    if (frac->shader.locs.trapRadius_loc >= 0) {
        gX_Shader_SetValue_Float(frac->shader.shader.shader, frac->shader.locs.trapRadius_loc, frac->trap.trapRadius);
    }
    //opencode: palette mode
    if (frac->shader.locs.paletteMode_loc >= 0) {
        gX_Shader_SetValue_Int(frac->shader.shader.shader, frac->shader.locs.paletteMode_loc, frac->gen.paletteMode);
    }
    //opencode: Julia morph uniform (vec2: sin(phase), cos(phase))
    if (frac->shader.locs.morphLoc >= 0) {
        gX_F32 c = cosf(frac->gen.morphPhase);
        gX_F32 s = sinf(frac->gen.morphPhase);
        gX_Shader_SetValue_V2(frac->shader.shader.shader, frac->shader.locs.morphLoc,
                              (gX_F32v2){c, s});
    }
}

gX_I0 gX_RendererFractal_Update(gX_RendererFractal *frac) {
    
    //opencode: apply camera speeds with frame delta time for auto-animation
    {
        gX_F32 dt = (gX_F32)gX_FrameTime_Get();
        frac->cam.pos.x += frac->cam.pos_speed.x * dt;
        frac->cam.pos.y += frac->cam.pos_speed.y * dt;
        frac->cam.zoom *= 1.0f + frac->cam.zoom_speed * dt;
        frac->cam.angle += frac->cam.angle_speed * dt;
        frac->cam.tilt.x += frac->cam.tilt_speed.x * dt;
        frac->cam.tilt.y += frac->cam.tilt_speed.y * dt;
    }
    
    //opencode: Julia morphing — animate seed along a circle in parameter space
    if (frac->gen.morphSpeed != 0.0f) {
        gX_F32 dt = (gX_F32)gX_FrameTime_Get();
        frac->gen.morphPhase += frac->gen.morphSpeed * dt;
        gX_F32 c = cosf(frac->gen.morphPhase);
        gX_F32 s = sinf(frac->gen.morphPhase);
        gX_F32 baseR = sqrtf(frac->gen.seed.x * frac->gen.seed.x + frac->gen.seed.y * frac->gen.seed.y);
        if (baseR < GX_FRACTAL_MORPH_MIN_RADIUS) baseR = 1.0f;
        frac->gen.seed.x = baseR * c;
        frac->gen.seed.y = baseR * s;
    }
    
    //opencode: apply oscillators — drive parameters sinusoidally
    {
        gX_F32 dt = (gX_F32)gX_FrameTime_Get();
        for (gX_I32 i = 0; i < GX_FRACTAL_OSC_COUNT; i++) {
            gX_RendererFractal_Osc *osc = &frac->oscillators[i];
            if (osc->target < 0 || osc->target >= GX_FRACTAL_OSC_TARGETS) continue;
            osc->accumulator += osc->speed * dt;
            gX_F32 val = osc->offset + osc->amplitude * sinf(osc->accumulator + osc->phase);
            switch (osc->target) {
                case 0: frac->cam.pos.x = val; break;
                case 1: frac->cam.pos.y = val; break;
                case 2: frac->cam.zoom = fmaxf(GX_FRACTAL_OSC_ZOOM_MIN, val); break;
                case 3: frac->cam.angle = val; break;
                case 4: frac->gen.seed.x = val; break;
                case 5: frac->gen.seed.y = val; break;
                case 6: frac->pal.palette_phase.x = val; break;
                case 7: frac->pal.palette_phase.y = val; break;
                default: break;
            }
        }
    }
    
    //opencode: mouse interaction — click to set Julia seed, drag to pan, scroll to zoom
    {
        gX_F32 halfW = (gX_F32)maindata.windowWidth * 0.5f;
        gX_F32 halfH = (gX_F32)maindata.windowHeight * 0.5f;
        gX_F32 zoom = frac->cam.zoom;
        gX_F32 angle = frac->cam.angle;
        gX_F32 ca = cosf(angle);
        gX_F32 sa = sinf(angle);
        
        // Mouse wheel zoom
        gX_F32 wheel = gX_Mouse_GetWheelMove();
        if (wheel != 0.0f) {
            frac->cam.zoom *= (wheel > 0.0f) ? GX_FRACTAL_ZOOM_WHEEL_FACTOR : (1.0f / GX_FRACTAL_ZOOM_WHEEL_FACTOR);
            if (frac->cam.zoom < GX_FRACTAL_ZOOM_MIN) frac->cam.zoom = GX_FRACTAL_ZOOM_MIN;
            if (frac->cam.zoom > GX_FRACTAL_ZOOM_MAX) frac->cam.zoom = GX_FRACTAL_ZOOM_MAX;
        }
        
        gX_F32v2 mousePos = gX_Mouse_GetPos();
        gX_I32 mx = (gX_I32)mousePos.x;
        gX_I32 my = (gX_I32)mousePos.y;
        
        // Right-click: screen pixel -> complex plane, set as Julia seed
        if (gX_Mouse_IsPressed(gX_MOUSE_BUTTON_RIGHT)) {
            gX_F32 nx = ((gX_F32)mx - halfW) / halfH;
            gX_F32 ny = ((gX_F32)my - halfH) / halfH;
            gX_F32 rx = nx * ca + ny * sa;
            gX_F32 ry = -nx * sa + ny * ca;
            gX_F32 cx = rx / zoom + frac->cam.pos.x;
            gX_F32 cy = ry / zoom + frac->cam.pos.y;
            frac->gen.seed = (gX_F32v2){cx, cy};
            frac->gen.fractalType = 0; // switch to Julia
        }
        
        // Left-click drag: pan camera
        if (gX_Mouse_IsDown(gX_MOUSE_BUTTON_LEFT)) {
            if (frac->mouseDragging) {
                gX_F32 dx = (gX_F32)(mx - frac->mousePrevX);
                gX_F32 dy = (gX_F32)(my - frac->mousePrevY);
                gX_F32 scale = GX_FRACTAL_DRAG_SENSITIVITY / (zoom * halfH);
                frac->cam.pos.x -= dx * scale * ca;
                frac->cam.pos.y += dy * scale * ca;
            }
            frac->mouseDragging = true;
            frac->mousePrevX = mx;
            frac->mousePrevY = my;
        } else {
            frac->mouseDragging = false;
        }
    }
    
    //opencode: detect precision/fractalType/algorithm change → rebuild defines and reload shader
    if (frac->gen.precisionMode != frac->prevPrecisionMode ||
        frac->gen.fractalType != frac->prevFractalType ||
        frac->gen.algorithm != frac->prevAlgorithm) {
        frac->prevPrecisionMode = frac->gen.precisionMode;
        frac->prevFractalType = frac->gen.fractalType;
        frac->prevAlgorithm = frac->gen.algorithm;
        gX_RendererFractal_Shader_BuildDefines(frac);
        if (frac->shader.shader.loadingState.loaded) {
            frac->shader.shader.loadingState.reload = true;
        }
    }
    
    gX_Shader_Wrapper_Update(&frac->shader.shader);
    if (frac->shader.shader.loadingState.loaded) {
        gX_RendererFractal_Shader_GetLocs(frac);
    }
    if (frac->shader.shader.loadingState.loaded) {
        gX_RendererFractal_Shader_SetLocs(frac);
    }
    
    //opencode: --fractal-preset flag loads a preset at startup
    if (arguments.hasFractalPreset && frac->shader.shader.loadingState.loaded) {
        gX_I8 path[256];
        snprintf(path, sizeof(path), "%s/%.200s.json", GX_PATH("presets", "renderer_fractal"),
                 arguments.fractalPresetPath);
        gX_RendererFractal_Preset_Load(frac, path);
        arguments.hasFractalPreset = false;
    }
    
}

gX_I0 gX_RendererFractal_DrawTo(gX_RendererFractal *frac, gX_RenderTexture *outputRT) {
    gX_Rectangle renderRect = (gX_Rectangle){0.0f,0.0f,maindata.renderWidth,maindata.renderHeight};
    (void)renderRect;

    gX_RenderTexture target = outputRT ? *outputRT : renderer.canvas;
    gX_Drawing_BeginTexture(target);
    
        gX_Drawing_ClearBG(gX_RED_PURE);
        
        gX_Drawing_BeginShader(frac->shader.shader.shader);
            gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
        gX_Drawing_EndShader();
        
    gX_Drawing_EndTexture();
    
}
