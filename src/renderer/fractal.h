/**********************************************************************************************
*
*   [gX: src/renderer/fractal.h]
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


#ifndef RENDERER_FRACTAL_H
#define RENDERER_FRACTAL_H

#ifdef __cplusplus
extern "C" {
#endif
  
#include "../common/types.h"
#include "../wrappers/shader.h"
    
typedef struct gX_RendererFractal_Gen {
    gX_I32 iterations;
    gX_F32v2 seed;
    gX_F32 exponent;
    gX_F32 escape;
    //opencode: scale factor for internal iteration perturbation (values down to 1e-10 for deep zoom)
    gX_F32 scale;
    //opencode: fractal type: 0=Julia, 1=Mandelbrot
    gX_I32 fractalType;
    //opencode: precision mode: 0=32-bit, 1=48-bit double-double emulated
    gX_I32 precisionMode;
    //opencode: fractal algorithm: 0=standard, 1=Burning Ship, 2=Tricorn, 3=Newton, 4=Phoenix
    gX_I32 algorithm;
    //opencode: Phoenix y-offset (used as initial y in two-term recurrence)
    gX_F32v2 y_offset;
    //opencode: palette coloring mode: 0=sinusoid, 1=raw, 2=heatmap, 3=rainbow, 4=HSV, 5=grayscale
    gX_I32 paletteMode;
    //opencode: Julia morphing speed (radians/sec, 0=off)
    gX_F32 morphSpeed;
    //opencode: Julia morphing phase offset
    gX_F32 morphPhase;
} gX_RendererFractal_Gen;

typedef struct gX_RendererFractal_Cam {
    gX_F32v2 pos;
    gX_F32 zoom;
    gX_F32 angle;
    gX_F32v2 tilt;
    
    gX_F32v2 pos_speed;
    gX_F32 zoom_speed;
    gX_F32 angle_speed;
    gX_F32v2 tilt_speed;
} gX_RendererFractal_Cam;

//opencode: oscillator slot — drives a fractal parameter sinusoidally (background-style)
#define GX_FRACTAL_OSC_COUNT 4
#define GX_FRACTAL_OSC_TARGETS 8 // number of parameter targets

//opencode: zoom/clamp constants
#define GX_FRACTAL_ZOOM_MIN          0.001f     // minimum zoom limit
#define GX_FRACTAL_ZOOM_MAX          1000000.0f // maximum zoom limit
#define GX_FRACTAL_ZOOM_WHEEL_FACTOR 1.1f       // mouse wheel zoom multiplier
#define GX_FRACTAL_DRAG_SENSITIVITY  2.0f       // mouse drag normalization factor (pixel-to-complex)
#define GX_FRACTAL_MORPH_MIN_RADIUS  0.001f     // minimum seed radius to prevent div-by-zero in morphing
#define GX_FRACTAL_OSC_ZOOM_MIN      0.001f     // minimum zoom clamp in oscillator output

typedef struct gX_RendererFractal_Osc {
    gX_I32  target;     // 0=pos.x, 1=pos.y, 2=zoom, 3=angle, 4=seed.x, 5=seed.y, 6=palettePhase.x, 7=palettePhase.y
    gX_F32  speed;      // angular velocity (rad/s)
    gX_F32  amplitude;  // oscillation amplitude
    gX_F32  phase;      // base phase offset
    gX_F32  offset;     // base value added on top
    gX_F32  accumulator; // running phase
} gX_RendererFractal_Osc;

typedef struct gX_RendererFractal_Trap {
    //opencode: orbit trap: 0=off, 1=circle, 2=line, 3=cross, 4=point
    gX_I32 trapMode;
    gX_F32v2 trapPos;
    gX_F32 trapRadius;
} gX_RendererFractal_Trap;

typedef struct gX_RendererFractal_Pal {
    gX_I32 palette_amount;
    gX_F32v3 palette_factor;
    gX_F32v3 palette_speed;
    gX_F32v3 palette_phase;
    gX_F32v3 palette_amplitude;
} gX_RendererFractal_Pal;

typedef struct gX_RendererFractal_ShaderLocs {
    gX_I32 iterations_loc;
    gX_I32 exponent_loc;
    gX_I32 escape_loc;
    gX_I32 seed_loc;
    
    gX_I32 pos_loc;
    gX_I32 zoom_loc;
    gX_I32 angle_loc;
    gX_I32 tilt_loc;
    
    gX_I32 paletteamount_loc;
    gX_I32 palettefactor_loc;
    gX_I32 palettespeed_loc;
    gX_I32 palettephase_loc;
    gX_I32 paletteamplitude_loc;

    //opencode: scale and fractal type uniform locations
    gX_I32 scale_loc;
    gX_I32 fractalType_loc;
    //opencode: algorithm-specific uniforms
    gX_I32 y_offset_loc;
    //opencode: orbit trap uniforms
    gX_I32 trapMode_loc;
    gX_I32 trapPos_loc;
    gX_I32 trapRadius_loc;
    //opencode: palette mode uniform
    gX_I32 paletteMode_loc;
    //opencode: Julia morphing uniform
    gX_I32 morphLoc;
} gX_RendererFractal_ShaderLocs;

typedef struct gX_RendererFractal_Shader {
    gX_Shader_Wrapper shader;
    gX_RendererFractal_ShaderLocs locs;
} gX_RendererFractal_Shader;
    
typedef struct gX_RendererFractal {
    gX_I8 name[128];
    gX_I8 description[256];
    
    gX_LoadingState loadingstate;
    
    gX_RendererFractal_Gen gen;
    gX_RendererFractal_Cam cam;
    gX_RendererFractal_Trap trap;
    gX_RendererFractal_Pal pal;
    
    //opencode: tracks last compiled precision/fractalType/algorithm for shader reload detection
    gX_I32 prevPrecisionMode;
    gX_I32 prevFractalType;
    gX_I32 prevAlgorithm;
    
    gX_RendererFractal_Shader shader;
    
    //opencode: background-style oscillators for parameter animation
    gX_RendererFractal_Osc oscillators[GX_FRACTAL_OSC_COUNT];
    
    //opencode: mouse interaction state
    gX_I1   mouseDragging;
    gX_I32  mousePrevX;
    gX_I32  mousePrevY;
    
} gX_RendererFractal;

gX_I32 gX_RendererFractal_Load(gX_RendererFractal *frac);
gX_I32 gX_RendererFractal_Unload(gX_RendererFractal *frac);

gX_I0 gX_RendererFractal_Update(gX_RendererFractal *frac);
//opencode: DrawTo with explicit output RT (NULL = renderer.canvas)
gX_I0 gX_RendererFractal_DrawTo(gX_RendererFractal *frac, gX_RenderTexture *outputRT);
#define gX_RendererFractal_Draw(frac) gX_RendererFractal_DrawTo((frac), NULL)

#ifdef __cplusplus
}
#endif

#endif //RENDERER_FRACTAL_H
