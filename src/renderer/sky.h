/**********************************************************************************************
*
*   [gX: src/renderer/sky.h]
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


#ifndef RENDERER_SKY_H
#define RENDERER_SKY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/types/state.h"
#include <stddef.h>

//opencode: forward declaration — ephemeris is in src/game/
struct gX_Ephemeris;

#define GX_SKY_MAX_PLANETS 4

//opencode: operational constants
//opencode: v0.1.25 — FOV multiplier 1.8 → 1.0. The wide-angle hack (added to
//   "show more Rayleigh gradient") rendered the MAIN sky at 1.8x the scene
//   projection while every direction-based consumer — SSR env fallback, the
//   equirect sky LUT, cubemap faces — uses TRUE directions. Sky features
//   (moon/sun discs) therefore appeared angularly compressed on screen and
//   their mirror reflections separated from them / showed a different size
//   (found via --showcase-minimal: reflected moon larger than sky moon).
#define GX_SKY_FOV_MULTIPLIER         1.0f    // MUST match the scene projection or directional features desync from their reflections
#define GX_SKY_FOV_MAX              179.9f  // Maximum cfov (degrees) to prevent negative planeDist in shader
#define GX_SKY_PROJECT_Z_THRESHOLD    0.001f  // behind-camera rejection for dir projection
#define GX_SKY_HORIZON_PADDING        10.0f   // screen-space padding for horizon line clamp
#define GX_SKY_HORIZON_LINE_THICKNESS 2.0f
#define GX_SKY_HORIZON_CUY_THRESHOLD  0.001f  // minimum camera-up Y to avoid div-by-zero
#define GX_SKY_ALT_CIRCLE_SEGMENTS    36      // segments per altitude circle
#define GX_SKY_ALT_ANGLE_1            30.0f   // first altitude circle angle (degrees)
#define GX_SKY_ALT_ANGLE_2            60.0f   // second altitude circle angle (degrees)
#define GX_SKY_CARDINAL_DOT_SIZE      7       // cardinal direction marker pixel size
#define GX_SKY_CARDINAL_ARM           5       // crosshair arm offset for cardinal markers
#define GX_SKY_ORBIT_BODY_STEPS       48      // orbit path segment count
#define GX_SKY_ORBIT_PATH_DOT_SIZE    2       // orbit path dot pixel size
#define GX_SKY_ORBIT_MARKER_SIZE      8       // current position marker dot size
#define GX_SKY_ALT_CIRCLE_DOT_SIZE    2       // altitude circle dot size

typedef struct gX_RendererSky {
    gX_I8 name[128];
    gX_I8 description[256];
    
    gX_LoadingState loadingstate;
    gX_U32 state;
    
    gX_F32v3 sunpos;
    gX_F32v3 moonpos;
    gX_F32   skyDensity;
    gX_F32   skyHue;
    gX_F32   horizonGlow;
    gX_F32   skyContrast;       // color contrast boost (0.5-2.0, default 1.2)
    gX_F32   altitude;          // observer altitude in km (0-5), controls horizon width + sky darkness
    gX_F32   turbidity;         // atmospheric haziness (1-10), controls Mie scattering
    gX_F32   sunDiskSize;
    gX_F32   sunDiskIntensity;
    
    // Starfield controls
    gX_F32   starDensity;
    gX_F32   starBrightness;
    gX_F32   starTwinkleSpeed;
    
    // Moon
    gX_F32v3 moonDir;
    gX_F32   moonSize;
    gX_F32   moonBrightness;
    
    // Planets
    gX_I32   planetCount;
    gX_F32v3 planetDirs[GX_SKY_MAX_PLANETS];
    gX_F32   planetSizes[GX_SKY_MAX_PLANETS];
    gX_F32v3 planetColors[GX_SKY_MAX_PLANETS];
    gX_F32   planetBrightnesses[GX_SKY_MAX_PLANETS];
    
    gX_Shader shader;
    gX_I32 shader_timeLoc;
    gX_I32 shader_resLoc;
    gX_I32 shader_cposLoc;
    gX_I32 shader_ctargetLoc;
    gX_I32 shader_cupLoc;
    gX_I32 shader_cfovLoc;
    gX_I32 shader_sunDirLoc;
    gX_I32 shader_atmosphereStateLoc;
    gX_I32 shader_skyDensityLoc;
    gX_I32 shader_skyHueLoc;
    gX_I32 shader_horizonGlowLoc;
    gX_I32 shader_skyContrastLoc;
    gX_I32 shader_sunDiskSizeLoc;
    gX_I32 shader_sunDiskIntensityLoc;
    
    // Starfield uniform locations
    gX_I32 shader_starDensityLoc;
    gX_I32 shader_starBrightnessLoc;
    gX_I32 shader_starTwinkleSpeedLoc;
    
    // Moon uniform locations
    gX_I32 shader_moonDirLoc;
    gX_I32 shader_moonSizeLoc;
    gX_I32 shader_moonBrightnessLoc;
    
    // Planet uniform locations
    gX_I32 shader_planetCountLoc;
    gX_I32 shader_planetDirLocs[GX_SKY_MAX_PLANETS];
    gX_I32 shader_planetSizeLocs[GX_SKY_MAX_PLANETS];
    gX_I32 shader_planetColorLocs[GX_SKY_MAX_PLANETS];
    gX_I32 shader_planetBrightnessLocs[GX_SKY_MAX_PLANETS];

    //opencode: debug overlay flags
    gX_I1 showHelpers;          // 1 = draw horizon + coordinate grid + orbit paths
    
} gX_RendererSky;

gX_I32 gX_RendererSky_Load(gX_RendererSky *sky);
gX_I32 gX_RendererSky_Unload(gX_RendererSky *sky);

gX_I0 gX_RendererSky_Update(gX_RendererSky *sky);
//opencode: DrawTo with optional camera override (NULL = use global camera.camera3D)
gX_I0 gX_RendererSky_DrawTo(gX_RendererSky *sky, gX_RenderTexture *outputRT, const gX_Camera3D *camOverride);

//opencode: backward-compatible alias — module macro calls this (draws to canvas)
#define gX_RendererSky_Draw(sky) gX_RendererSky_DrawTo(sky, NULL, NULL)

//opencode: draw procedural sky to the CURRENT render target (no BeginTexture/ClearBG).
//   Useful for scenes that want the sky as background behind their 3D content.
//   The caller is responsible for clearing/binding the framebuffer.
//   camOverride: optional camera override (NULL = use global camera.camera3D)
gX_I0 gX_RendererSky_DrawBackground(gX_RendererSky *sky, const gX_Camera3D *camOverride);

//opencode: preset IO (0 ok / 1 fail) — look parameters + the ephemeris clock
//   time; saves refuse shipped preset paths under the harness guard.
gX_I32 gX_RendererSky_Preset_Save(gX_RendererSky *sky, const gX_I8 *path);
gX_I32 gX_RendererSky_Preset_Load(gX_RendererSky *sky, const gX_I8 *path);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_SKY_H
