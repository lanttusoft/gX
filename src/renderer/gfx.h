/**********************************************************************************************
*
*   [gX: src/renderer/gfx.h]
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


#ifndef RENDERER_GFX_H
#define RENDERER_GFX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "gfx/constants.h"

#include "../common/types.h"
#include "../common/text.h"
#include "../common/heap.h"
#include "../rhi/rhi.h"

#include "gfx/camera.h"
#include "gfx/light.h"
#include "gfx/model.h"
#include "gfx/shader.h"
#include "gfx/texture.h"
#include "gfx/godray.h"
#include "gfx/terrain.h"
#include "gfx/fog_volume.h"
#include "gfx/contact.h"
#include "gfx/clusters.h"
#include "gfx/lensflare.h"
#include "gfx/particle.h"
#include "water.h"

//opencode: state is now all UPPER_CASE flags + quality/shader tuning — subsystem tuning moved to their structs
typedef struct gX_RendererGFX_State {
    gX_I1   SHOW_DEPTH_VISUALIZER;
    gX_I32  depthVisSource;     // 0=shadow map, 1=scene depth, 2=point shadow
    gX_I1   SHOW_SSR_VISUALIZER; //opencode: draw raw SSR map as overlay for debugging
    gX_I32  ssrDebugMode;         //opencode: SSR debug visualizer mode (0=normal, 1=normals, 2=reflectDir, 3=depth, 4=hitQuality, 5=stateMachine, 6=temporalRejection, 7=confidence, 28=normal disagreement)
    
    gX_I1   ENABLE_LIGHTING_AMBIENT;
    gX_I1   ENABLE_LIGHTING_DIFFUSE;
    gX_I1   ENABLE_LIGHTING_SPECULAR;
    
    gX_I1   ENABLE_MAP_ALBEDO;
    gX_I1   ENABLE_MAP_METALNESS;
    gX_I1   ENABLE_MAP_NORMAL;
    gX_I1   ENABLE_MAP_PARALLAX;
    gX_I1   ENABLE_DETAIL;  //opencode: base detail/dirt texture toggle
    
    gX_I1   ENABLE_FLAT;
    gX_I1   ENABLE_GOURAUD;
    gX_I1   ENABLE_PHONG;
    
    gX_I1   ENABLE_FOG;
    gX_I1   ENABLE_SHADOWS;
    gX_I1   ENABLE_REFLECTIONS;
    //opencode: planar capture has its own gate, independent of environment reflection and SSR.
    gX_I1   ENABLE_PLANAR_REFLECTIONS;
    gX_I1   ENABLE_SKYBOX;         //opencode: cubemap skybox (gfx_sky)
    gX_I1   ENABLE_PROCEDURAL_SKY; //opencode: procedural Rayleigh sky (sky_new.frag), takes priority over ENABLE_SKYBOX
    gX_I1   ENABLE_POINT_SHADOWS;  //opencode: point light shadow cubemaps
    gX_I1   ENABLE_COOKIES;        //opencode: light cookie projection textures
    
    gX_I1   SHOW_POSITION;
    gX_I1   SHOW_TEXCOORD;
    gX_I1   SHOW_TEXCOORD2;
    gX_I1   SHOW_NORMAL;
    gX_I1   SHOW_COLOR;
    gX_I1   SHOW_TANGENT;
    gX_I32  DIAG_UNIFORM_MODE; // 0=off, 1=albedo, 2=metalness, 3=lightPos[0]
    
    //opencode: bloom / hdr
    gX_I1   ENABLE_BLOOM;
    gX_I1   RELOAD; // reload RTs on resolution change
    gX_I1   ENABLE_SSR;
    gX_I1   ENABLE_SSR_AUXVIEW;    //opencode: v0.3.4 hybrid miss->planar fallback

    //opencode: post-process toggles (motion blur, DOF, TAA)
    gX_I1   ENABLE_MOTION_BLUR;
    gX_I1   ENABLE_DOF;
    gX_I1   ENABLE_TAA;
    gX_I1   ENABLE_FXAA; //opencode: v0.1.18 — final-composite anti-aliasing

    //opencode: SSAO toggle — tuning moved into ssao struct
    gX_I1   ENABLE_SSAO;
    //opencode: M1 - transparent roughness-alpha repair pass active (pass 2
    //   rewrites hdrScene alpha with roughness; standard.frag emits
    //   ssrRoughness while keeping pass-1 coverage).
    gX_I1   transparentRoughnessPass;

    //opencode: rim/back lighting — view-dependent edge glow
    gX_I1   ENABLE_RIM;          // 0 = off, 1 = on
    gX_F32  rimIntensity;       // 0 = off, ~0.5 default
    gX_F32  rimPower;           // falloff exponent (~2.0 default)
    gX_Color rimColor;          // tint color

    //opencode: environment-aware ambient — blend cubemap into ambient term
    gX_I1   ENABLE_ENV_AMBIENT; // 0 = flat ambient only, 1 = blend cubemap
    gX_F32  envAmbientFactor;   // 0 = flat ambient, ~0.3 default blend

    //opencode: configurable shader tuning constants (category 3)
    gX_F32  shadowBiasMax;
    gX_F32  shadowBiasMin;
    gX_F32  parallaxMinLayers;
    gX_F32  parallaxMaxLayers;
    gX_F32  parallaxOffset;
    gX_F32  parallaxShadowOffset;
    gX_F32  parallaxShadowSteps;
    gX_F32  pointShadowBias;
    gX_F32  pointShadowExtra;
    gX_F32  pointShadowMinBias;
    gX_F32  tangentLengthThreshold;
    gX_F32  normalYThreshold;

    //opencode: quality tuning
    gX_I32  textureFilter;
    gX_I32  shaderQuality;
    gX_F32  mipmapBias;
    gX_I32  msaaLevel;
    gX_I32  qualityPreset;

    gX_I1   ENABLE_GODRAYS;
    gX_I1   ENABLE_PARTICLES;
    gX_I1   ENABLE_VOLUME_FOG;
    gX_I1   ENABLE_LENS_FLARE;

    //opencode: occlusion culling — skips drawing models whose bounding box
    //   was fully occluded by the depth buffer on the previous frame.
    //   Uses GL_ANY_SAMPLES_PASSED queries per model.
    gX_I1   ENABLE_OCCLUSION_CULLING;
    gX_I1   ENABLE_INSTANCING;       //opencode: SSBO hardware instancing (GL 4.3+)

    //opencode: bindless textures — uses GL_ARB_bindless_texture to bypass
    //   the fixed texture unit limit. Requires GL 4.x + extension support.
    //   When enabled, shaders receive 64-bit texture handles instead of
    //   sampler bindings. Runtime-checked; falls back to legacy if unsupported.
    gX_I1   ENABLE_BINDLESS;

    //opencode: ACES filmic tone mapping — replaces simple Reinhard with the
    //   Academy Color Encoding System curve (Narkowicz 2015). Produces
    //   superior highlight rolloff and color saturation preservation.
    gX_I1   ENABLE_ACES_TONEMAP;
    //opencode: v0.3-M1 — AgX display transform (Sobotka), precedence over ACES
    gX_I1   ENABLE_AGX_TONEMAP;
    //opencode: v0.3.2 — CPU-culled tile light clustering (Forward+)
    gX_I1   ENABLE_CLUSTERED;
    gX_I1   ENABLE_GAMMA_CORRECTION; //opencode: final linear→sRGB gamma encode
    gX_I1   ENABLE_STENCIL;          //opencode: stencil testing
    gX_I1   ENABLE_STENCIL_OUTLINE;  //opencode: stencil-based outline effect
    gX_I1   ENABLE_FRUSTUM_CULLING;  //opencode: skip invisible models

    //opencode: debug visualization toggles
    gX_I1   SHOW_GRID;
    gX_I1   SHOW_GIZMO;
    gX_I1   SHOW_BOUNDING_BOX;
    gX_I1   SHOW_SKELETON;      //opencode: bind-pose bone overlay for rigged models
    gX_I1   SHOW_LIGHT_MARKERS;
    gX_I1   ENABLE_WIREFRAME;   //opencode: renders all models as wireframe overlay
    
    //opencode: decal rendering toggle
    gX_I1   ENABLE_DECALS;

    //opencode: CPU falling snow toggle (GL33-safe weather layer)
    gX_I1   ENABLE_SNOWFALL;

    //opencode: CPU rain streak toggle (GL33-safe weather layer)
    gX_I1   ENABLE_RAIN;

    //opencode: CPU fog sprite toggle (GL33-safe weather layer)
    gX_I1   ENABLE_FOGSPRITE;

    //opencode: gaussian splat research fixture (GL33 rasterizer)
    gX_I1   ENABLE_SPLAT;

    //opencode: water surface toggle (Gerstner waves + ripples)
    gX_I1   ENABLE_WATER;

    //opencode: geometry shader demo — renders a test mesh with V+G+F shader
    gX_I1   ENABLE_GEO_DEMO;

    //opencode: scene exposure — multiplies HDR color before tone mapping
    gX_F32  exposure;

} gX_RendererGFX_State;

//typedef struct gX_RendererGFX_Common;
//typedef struct gX_RendererGFX_Debug;

typedef struct gX_RendererGFX_Sky {
    gX_F32   strength;
    gX_Color color;

    gX_RendererGFX_Texture skybox;
    gX_TextLine skyboxModelPath;
    //opencode: selected cubemap source path for GFX editor presets.
    gX_TextLine skyboxPath;
    //ai: image background is opt-in and independent of lighting/capture sources.
    gX_I1 visibleImageSky;

    //opencode: Sky Lab — procedural-sky art direction (was hardcoded per
    //   frame in draw.c). atmosphere = altitude km (0-5) + turbidity (1-10).
    gX_F32   altitude;
    gX_F32   turbidity;
    gX_F32   density;
    gX_F32   hue;
    gX_F32   horizon;
    gX_F32   sunIntensity;
    gX_F32   contrast;
    gX_F32   starDensity;
    gX_F32   starBrightness;
    gX_F32   starTwinkle;
} gX_RendererGFX_Sky;

typedef struct gX_RendererGFX_Fog {
    gX_F32      near;
    gX_F32      far;
    gX_Color    color;
    gX_F32      heightDensity;
    gX_F32      heightBase;
    //opencode: optional authoring link from fog color to the void/sky clear color.
    gX_I1       matchVoidColor;
} gX_RendererGFX_Fog;

//opencode: detail texture (HL2/Source style)
typedef struct gX_RendererGFX_Detail {
    gX_F32      uvScale;
    gX_F32      strength;
    gX_F32      distFade;
    //opencode: source path keeps the editor-authored detail map portable in presets.
    gX_TextLine mapPath;
    gX_Texture  map;
} gX_RendererGFX_Detail;

//opencode: decal — projected surface decal (bullet hole, blood, impact)
typedef struct gX_RendererGFX_Decal {
    gX_F32v3  pos;
    gX_F32v3  normal;
    gX_F32    size;
    gX_F32    rotation;
    gX_I32    textureIndex;  // index into gfx->textures, -1 = none
    gX_F32    alpha;         // blend opacity; texture alpha multiplies in
    gX_Color  color;         // albedo tint (white = untinted)
    gX_I1     snowLayer;     //opencode: bounded snow material override
    gX_F64    expireTime;    // maindata.timeElapsed deadline, -1 = permanent
} gX_RendererGFX_Decal;

typedef struct gX_RendererGFX_Render {
    gX_I1 ready;
    gX_I1 depthMap;
    gX_Color *clearColor;
    
    gX_I1       useLightCamera;
    gX_I1       lightCameraSun;
    gX_I1       skipBlended;
    gX_I1       skipColorClear;
    gX_Camera3D *camera;
    
    gX_RenderTexture *rt;
} gX_RendererGFX_Render;

//opencode: subsystem includes — struct definitions moved to individual headers
#include "gfx/bloom.h"
#include "gfx/motionblur.h"
#include "gfx/velocity.h"
#include "gfx/dof.h"
#include "gfx/taa.h"
#include "gfx/rtview.h"
#include "gfx/ssr.h"
#include "gfx/ssao.h"
#include "gfx/ibl.h"
#include "gfx/csm.h"
#include "gfx/reflection.h"
#include "gfx/common.h"
#include "gfx/debug_vis.h"
#include "gfx/geo_demo.h"
#include "gfx/point_depth.h"
#include "gfx/viewport_grid.h"
#include "gfx/oracle_gpu.h"
#include "gfx/weather.h"
#include "gfx/splat.h"
#include "gfx/mirror.h"

//ai: the optional CSV profiler is separately allocated only when requested.
struct gX_RendererGFX_Profile;
typedef struct gX_RendererGFX {
    gX_I8 name[128];
    gX_I8 description[128];
    
    gX_RendererGFX_State state;
    //opencode: transparent-background mode (GX_GFX_TRANSPARENT_BG=1) — the
    //   scene-mask pass writes depth-derived alpha (geometry 1 / sky 0) so the
    //   raymarch compositor can blend this layer with true alpha.
    gX_I1 transparentBackground;
    
    //env
    gX_RendererGFX_Sky sky;
    gX_RendererGFX_Fog fog;
    
    gX_RendererGFX_Detail detail;
    
    gX_RendererGFX_Light sun;
    gX_Heap lights;
    gX_Heap decals;
    gX_Model decalQuad;
    gX_RendererGFX_Common common;

    gX_RendererGFX_Shader shader;
    gX_RendererGFX_Shader depth;
    gX_RendererGFX_Shader depthVisualizer;
    gX_RendererGFX_Shader proceduralSky;
    gX_RendererGFX_Shader fxaaShader; //opencode: v0.1.18 — final-composite AA
    gX_RendererGFX_Shader paletteShader; //opencode: opt-in final palette quantizer
    gX_RendererGFX_Shader interlaceShader; //opencode: opt-in final scanline treatment
    gX_RendererGFX_Shader alphaMaskShader; //opencode: transparent-background depth mask
    gX_RendererGFX_PointDepth pointDepth;

    gX_Heap models;
    gX_Heap textures;
    gX_Heap maps;

    gX_RendererGFX_Render first;
    gX_RendererGFX_Render second;

    gX_RendererGFX_SSR ssr;
    gX_RendererGFX_SSAO ssao;
    gX_RendererGFX_IBL ibl;
    gX_RendererGFX_CSM csm;
    gX_RendererGFX_Reflection reflection;
    gX_RendererGFX_DebugVis debugVis;
    gX_RendererGFX_GeoDemo geoDemo;

    gX_RendererGFX_RenderTexture sceneColorLDR;
    gX_Matrix  motionBlurPrevVP;
    //opencode: TAA's own previous-VP snapshot. It used to share
    //   motionBlurPrevVP, but MotionBlur_Draw overwrites that mid-pipeline
    //   (before TAA runs), so the depth-fallback reprojection read the
    //   CURRENT frame as previous (history sampled in place; motion
    //   compensation silently lost, masked only by the 3x3 clamp).
    gX_Matrix  taaPrevVP;

    gX_RendererGFX_MotionBlur motionBlur;
    gX_RendererGFX_Velocity velocity; //opencode: v0.1.38 motion vector buffer
    gX_RendererGFX_DOF dof;
    gX_RendererGFX_TAA taa;
    gX_RendererGFX_RtView rtview; //opencode: RT inspector (GX_RT_VIEW)
    gX_RendererGFX_Bloom bloom;
    gX_RendererGFX_Godray godray;
    gX_RendererGFX_Terrain terrain; //opencode: Paper 2 spike — G-buffer terrain depth
    gX_RendererGFX_FogVolume fogVolume;
    gX_RendererGFX_Contact contact; //opencode: screen-space contact shadows (spike 1)
    gX_RendererGFX_LightClusters clusters; //opencode: v0.3.2 Forward+ tile grid
    gX_RendererGFX_LensFlare lensflare;
    gX_RendererGFX_Particle particle;
    gX_RendererGFX_Weather weather;
    gX_RendererGFX_Splat splat;
    gX_RendererGFX_Mirror mirror;
    gX_RendererGFX_ViewportGrid viewportGrid;
    gX_RendererGFX_OracleGPU oracleGpu;

    gX_RendererWater water;

    //opencode: GPU timer queries per render pass
    gX_RHI_Query_Timer timerShadow;
    gX_RHI_Query_Timer timerReflections;
    gX_RHI_Query_Timer timerOpaque;
    gX_RHI_Query_Timer timerBloom;
    gX_RHI_Query_Timer timerSSR;
    gX_RHI_Query_Timer timerTotal;
    gX_I1 timersInited;

    //ai: per-renderer opt-in frame/draw-matched profiling lifecycle.
    struct gX_RendererGFX_Profile *profile;
    gX_I1 profileAttempted;

} gX_RendererGFX;

gX_I32 gX_RendererGFX_Render_Init(          gX_RendererGFX_Render *render,  gX_RendererGFX *gfx, gX_I1 depth);

gX_I0 gX_RendererGFX_Model_Update(          gX_RendererGFX_Model *model,    gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Shader_Update(         gX_RendererGFX_Shader *shader,  gX_RendererGFX *gfx);

gX_I0 gX_RendererGFX_Model_Draw(    gX_RendererGFX_Model *model,    gX_RendererGFX *gfx, gX_I1 depthMode);
gX_I0 gX_RendererGFX_Render_Draw(   gX_RendererGFX_Render *render,  gX_RendererGFX *gfx);

gX_I32 gX_RendererGFX_Load(gX_RendererGFX *gfx);
gX_I32 gX_RendererGFX_Unload(gX_RendererGFX *gfx);

gX_I0 gX_RendererGFX_Update(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_DrawTo(gX_RendererGFX *gfx, gX_RenderTexture *outputRT);

//opencode: backward-compatible alias — module macro calls this (draws to canvas)
#define gX_RendererGFX_Draw(gfx) gX_RendererGFX_DrawTo(gfx, NULL)

//opencode: dynamic scene management
gX_I32 gX_RendererGFX_Light_Add(    gX_RendererGFX *gfx);
gX_I32 gX_RendererGFX_Light_Remove( gX_RendererGFX *gfx, gX_U32 index);
gX_I32 gX_RendererGFX_Light_RemoveAll(gX_RendererGFX *gfx);

gX_I32 gX_RendererGFX_Texture_Add(    gX_RendererGFX *gfx, const gX_I8 *path);
gX_I32 gX_RendererGFX_Texture_Remove( gX_RendererGFX *gfx, gX_U32 index);
gX_I32 gX_RendererGFX_Texture_RemoveAll(gX_RendererGFX *gfx);

gX_I32 gX_RendererGFX_ClearScene(gX_RendererGFX *gfx);

//opencode: decal system
gX_I32 gX_RendererGFX_Decal_Add(gX_RendererGFX *gfx, gX_F32v3 pos, gX_F32v3 normal,
    gX_F32 size, gX_I32 textureIndex, gX_F64 lifetime);
gX_I0  gX_RendererGFX_Decal_UpdateAll(gX_RendererGFX *gfx);
gX_I0  gX_RendererGFX_Decal_RemoveAll(gX_RendererGFX *gfx);

//opencode: raycast model picking — returns model index or -1
gX_I32 gX_RendererGFX_Model_Pick(gX_RendererGFX *gfx, gX_F32v2 screenPos, gX_F32 *outDistance);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_H
