/**********************************************************************************************
*
*   [gX: src/renderer/water.h]
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


#ifndef RENDERER_WATER_H
#define RENDERER_WATER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/types/state.h"

#define GX_WATER_MAX_RIPPLES 16

//opencode: water plane mesh
#define GX_WATER_PLANE_SIZE_X   40.0f
#define GX_WATER_PLANE_SIZE_Z   40.0f
// N64 uses lower tessellation (32x32 = 1K verts) vs PC (128x128 = 16K verts)
#define GX_WATER_PLANE_SEGMENTS 128 //opencode: 257^2 verts overflows the U16 index buffer — do not raise without U32 indices
#define GX_WATER_MAX_WAVES      6

//opencode: ripple operational constants
#define GX_WATER_RIPPLE_SPAWN_INTERVAL 1.5f     // seconds between automatic spawns
#define GX_WATER_RIPPLE_POS_RANGE      15.0f    // spawn position range from origin
#define GX_WATER_RIPPLE_AMP_MIN        0.1f     // minimum random amplitude at spawn
#define GX_WATER_RIPPLE_AMP_MAX        0.5f     // maximum random amplitude at spawn
#define GX_WATER_RIPPLE_INIT_RADIUS    0.5f     // initial ripple radius at spawn
#define GX_WATER_RIPPLE_INIT_PHASE     0.0f     // initial ripple phase at spawn
#define GX_WATER_RIPPLE_INIT_LIFETIME  0.0f     // initial ripple lifetime at spawn
#define GX_WATER_RIPPLE_ANGULAR_SPEED  4.0f     // phase change rate (radians/sec)
#define GX_WATER_RIPPLE_EXPAND_SPEED   2.0f     // radial expansion speed (units/sec)
#define GX_WATER_RIPPLE_FADE_FACTOR    0.997f   // per-60Hz-step amplitude decay reference
#define GX_WATER_RIPPLE_AMP_THRESHOLD  0.005f   // cull ripples below this amplitude
#define GX_WATER_RIPPLE_MAX_LIFETIME   8.0f     // maximum ripple lifetime (seconds)

//opencode: standalone water camera
#define GX_WATER_STANDALONE_CAM_POS_Y  2.0f
#define GX_WATER_STANDALONE_CAM_POS_Z  5.0f
#define GX_WATER_STANDALONE_CAM_TAR_Y -0.3f

//opencode: external ripple spawn
#define GX_WATER_RIPPLE_SPAWN_RADIUS 0.3f

//opencode: forward decl for GFX integration
typedef struct gX_RendererGFX gX_RendererGFX;

typedef struct gX_RendererWater {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_LoadingState loadingstate;

    // Shader
    gX_Shader shader;
    gX_I32 shader_timeLoc;
    gX_I32 shader_resLoc;
    gX_I32 shader_camposLoc;
    gX_I32 shader_camtarLoc;
    gX_I32 shader_camupLoc;
    gX_I32 shader_camfovLoc;
    //ai: scene fog on the above-water surface (standard-shader contract).
    gX_I32 shader_fogEnabledLoc;
    gX_I32 shader_fogNearLoc;
    gX_I32 shader_fogFarLoc;
    gX_I32 shader_fogColorLoc;

    // Wave uniform locations
    gX_I32 shader_waveCountLoc;
    gX_I32 shader_waveDirsLoc[GX_WATER_MAX_WAVES];
    gX_I32 shader_waveAmpsLoc[GX_WATER_MAX_WAVES];
    gX_I32 shader_waveFreqsLoc[GX_WATER_MAX_WAVES];
    gX_I32 shader_waveSpeedsLoc[GX_WATER_MAX_WAVES];
    gX_I32 shader_waveSteepsLoc[GX_WATER_MAX_WAVES];

    // Ripple uniform locations
    gX_I32 shader_rippleCountLoc;
    gX_I32 shader_ripplePosLoc[GX_WATER_MAX_RIPPLES];
    gX_I32 shader_rippleAmpLoc[GX_WATER_MAX_RIPPLES];
    gX_I32 shader_rippleRadiusLoc[GX_WATER_MAX_RIPPLES];
    gX_I32 shader_ripplePhaseLoc[GX_WATER_MAX_RIPPLES];

    // Sky/sun uniform locations
    gX_I32 shader_skyBoxLoc;
    gX_I32 shader_skyTex2DLoc;   // 2D panorama fallback
    gX_I32 shader_skyUse2DLoc;   // bool: 1=use skyTex2D, 0=use skyBox cubemap
    gX_I32 shader_underwaterLoc; // bool: 1=camera below water surface
    gX_I32 shader_sunDirLoc;
    gX_I32 shader_sunColorLoc;
    gX_I32 shader_sunBrightLoc;

    // GFX shared uniform locations (matView, matProjection)
    gX_I32 shader_matViewLoc;
    gX_I32 shader_matProjLoc;
    gX_I32 shader_matModelLoc;
    //opencode: velocity replay uses the same water program with a compact
    //   current/previous clip-space branch; color shading remains mode 0.
    gX_I32 shader_velocityModeLoc;
    gX_I32 shader_velocityCurrVPLoc;
    gX_I32 shader_velocityPrevVPLoc;
    gX_I32 shader_velocityPrevTimeLoc;

    // Mesh
    gX_Model model;
    gX_Texture tex;

    //opencode: custom VAO (bypasses raylib mesh upload which produces invalid VAOs)
    gX_U32 customVao;
    gX_U32 customVbo;
    gX_U32 customEbo;
    gX_I32 customTriCount;

    //opencode: editable water appearance persists as source paths and values.
    gX_Texture normalMapTex;
    gX_TextLine normalMapPath;
    gX_F32      normalMapStrength;
    gX_F32v2    normalMapScale;
    //ai: independent shader-generated surface detail controls, saved with GFX presets.
    gX_F32      proceduralNormalStrength;
    gX_F32      proceduralNormalScale;
    //ai: authored UV-units-per-second rates; zero freezes detail without disabling normals.
    gX_F32v2    normalMapScroll;
    gX_Color    tint;
    gX_I32     shader_normalMapLoc;
    gX_I32     shader_normalStrengthLoc;
    gX_I32     shader_normalScaleLoc;
    gX_I32     shader_proceduralNormalStrengthLoc;
    gX_I32     shader_proceduralNormalScaleLoc;
    gX_I32     shader_normalScrollLoc;
    gX_I32     shader_tintLoc;

    // Wave state
    gX_I32  waveCount;
    gX_F32  waveDirections[GX_WATER_MAX_WAVES];
    gX_F32  waveAmplitudes[GX_WATER_MAX_WAVES];
    gX_F32  waveFrequencies[GX_WATER_MAX_WAVES];
    gX_F32  waveSpeeds[GX_WATER_MAX_WAVES];
    gX_F32  waveSteepness[GX_WATER_MAX_WAVES];

    // Ripple state
    gX_I32   rippleCount;
    gX_F32v2 ripplePos[GX_WATER_MAX_RIPPLES];
    gX_F32   rippleAmp[GX_WATER_MAX_RIPPLES];
    gX_F32   rippleRadius[GX_WATER_MAX_RIPPLES];
    gX_F32   ripplePhase[GX_WATER_MAX_RIPPLES];
    gX_F32   rippleLifetime[GX_WATER_MAX_RIPPLES];
    gX_F32   rippleTimer;
    //ai: authored gate for periodic ripple sources only; explicit splashes remain independent.
    gX_I1    automaticRipples;

    // Sky cubemap (standalone mode)
    gX_Texture skyCubemap;
    gX_I1      skyCubemapLoaded;

    // Sun direction (standalone mode)
    gX_F32v3 sunDirection;
    gX_F32v3 sunColorVec;
    gX_F32   sunBrightnessVal;

    // GFX reference (when integrated)
    gX_RendererGFX *gfxRef;

    // Plane position / height
    gX_F32 planeHeight;

    //opencode: buoyancy demo — floating test object
    gX_Model  buoyModel;              // sphere mesh
    gX_F32v3  buoyPos;                // world position
    gX_F32v3  buoyVel;                // velocity
    gX_F32    buoyRadius;             // sphere radius
    gX_I1     buoySplashDone;         // prevent repeat splash ripples
    gX_I1     enableAudio;            //opencode: disable when used as GFX sub-renderer
    //opencode: --lake calm water - still-lake state: ripple rain off,
    //   wave amps scaled at scene setup (uploads read struct per frame).
    gX_I1     calmWater;

#ifdef GX_WATER_REFRACTION_EXPERIMENT
    //ai: authored experiment enable only; production builds omit this field and its controls.
    gX_I1 sceneRefractionEnabled;
    //ai: pass-local, opt-in immutable pre-water inputs; never part of the preset schema.
    gX_RenderTexture refractionCapture;
    gX_I1 refractionValid;
    gX_I1 refractionTraced;
#endif

} gX_RendererWater;

gX_I32 gX_RendererWater_Load(gX_RendererWater *water);
gX_I32 gX_RendererWater_Unload(gX_RendererWater *water);

gX_I0 gX_RendererWater_Update(gX_RendererWater *water);
gX_I0 gX_RendererWater_Draw(gX_RendererWater *water);

//opencode: draw to specific RT with camera override (for GFX integration)
gX_I0 gX_RendererWater_DrawTo(gX_RendererWater *water, gX_RenderTexture *target,
                              gX_Camera3D *cam, gX_Matrix *viewMat, gX_Matrix *projMat);
gX_I0 gX_RendererWater_DrawVelocity(gX_RendererWater *water,
                                    gX_Matrix *currVP, gX_Matrix *prevVP,
                                    gX_I1 hasPrevVP);

//opencode: spawn a ripple at world position
gX_I0 gX_RendererWater_SpawnRipple(gX_RendererWater *water, gX_F32 x, gX_F32 z,
                                    gX_F32 amplitude);

//opencode: wave height query — evaluates Gerstner + ripple height at (x,z)
//   at the current time. Returns displacement above planeHeight.
//   Useful for buoyancy, displacement, or placing objects on the water surface.
gX_F32 gX_RendererWater_GetHeight(gX_RendererWater *water, gX_F32 x, gX_F32 z);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_WATER_H
