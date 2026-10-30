/**********************************************************************************************
*
*   [gX: src/renderer/raymarch.h]
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


#ifndef RENDERER_RAYMARCH_H
#define RENDERER_RAYMARCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/text.h"

#include "../wrappers/shader.h"
#include "../wrappers/texture.h"

#include "../camera.h"
#include "../rhi/rhi_query.h"

//opencode: named constants for raymarch defaults and limits — must match Load()
#define GX_RM_MAX_OBJECTS          8
#define GX_RM_MAX_LIGHTS           8
#define GX_RM_MAX_TEXTURES         8
//ai: 8x8 authoring slots compile to a compact, GL33-friendly reachable graph.
#define GX_RM_GRAPH_SLOTS          64
#define GX_RM_GRAPH_PROGRAM        32
#define GX_RM_POLYNOMIAL_SIZE      8
//ai: conservative tracing needs room to converge at grazing angles.
#define GX_RM_DEFAULT_STEPS        128
#define GX_RM_DEFAULT_BOUNCES      2
#define GX_RM_DEFAULT_NEAR         0.01f
#define GX_RM_DEFAULT_FAR          200.0f
#define GX_RM_DEFAULT_STEP_MAX     5.0f
#define GX_RM_DEFAULT_EPSILON      0.0005f
#define GX_RM_DEFAULT_SHININESS    32.0f
#define GX_RM_WATER_SPEED_DEFAULT  0.1f
#define GX_RM_WATER_SEED_DEFAULT   0
#define GX_RM_WATER_GRADCLAMP_DEFAULT 0.2f
//opencode: Padé [1/1] correction = num / (1.0 + den_linear*idx)
//   poly[0..3] = numerator coeffs (const, linear, quad, cubic)
//   poly[4..6] = denominator coeffs (linear, quad, cubic)
//   poly[7] = minimum corr floor (0.0 = use shader default 0.1). Old Taylor presets
//   (poly[4..6]=0) → denom=1.0 → backward compat.
//   Denominator asymptotes naturally — no zero-crossing, no clamp cliff.
//   Default: corr = (1.2 + 0.006*idx) / (1.0 + 0.02*idx)
//   Step 0: 1.2 (0.83×), Step 50: 0.75 (1.33×), Step 128: 0.55 (1.8×), Step ∞: 0.3 (3.3×)
#define GX_RM_POLY_COEFF_0_DEFAULT 1.2f   // num const
#define GX_RM_POLY_COEFF_1_DEFAULT 0.006f // num linear
#define GX_RM_POLY_COEFF_2_DEFAULT 0.0f   // num quad (unused in [1/1])
#define GX_RM_POLY_COEFF_3_DEFAULT 0.0f   // num cubic (unused in [1/1])
#define GX_RM_POLY_COEFF_4_DEFAULT 0.02f  // den linear
#define GX_RM_POLY_COEFF_7_DEFAULT 0.0f   // min corr floor (0.0 = shader default 0.1)

typedef struct gX_Raymarch_State {
    gX_LoadingState loadingState;
    
    gX_I1   useInternalTime;
    gX_I1   useInternalCamera;
    
    gX_I1   colorIsRGB;
    
    gX_F32  time;
} gX_Raymarch_State;

typedef struct gX_Raymarch_Camera {
    gX_Camera camera;
} gX_Raymarch_Camera;

typedef struct gX_Raymarch_Parameters {
    gX_I32   bounces;
    gX_I32   steps;
    gX_I32   stepsPerBounce; //opencode: max steps per bounce (0 = use global steps)
    gX_F32   near;
    gX_F32   far;
    gX_F32   stepMax; //opencode: max distance per step (0 = no clamp)
    gX_F32   distTotal; //opencode: total path length limit (0 = use far)
    gX_F32   epsilon;
    gX_F32   correctionPolynomial[GX_RM_POLYNOMIAL_SIZE];
    gX_F32   renderScale; //opencode: per-module resolution scale (1.0 = full, 0.5 = half)
} gX_Raymarch_Parameters;

typedef struct gX_Raymarch_Fog {
    gX_F32   near;
    gX_F32   far;
    gX_Color color;
} gX_Raymarch_Fog;

//ai: volumetric clouds — a procedural density slab marched after the
//   atmosphere (opt-in; enableClouds defaults off).
typedef struct gX_Raymarch_Clouds {
    gX_F32   bottom;
    gX_F32   top;
    gX_F32   coverage;   // 0..1 threshold on the FBM field
    gX_F32   density;    // extinction scale
    gX_F32   scale;      // noise frequency (1/world unit)
    gX_F32   speed;      // wind speed along +x (needs state.useInternalTime)
    gX_F32   absorb;     // light absorption
    gX_F32   powder;     // powder term strength (thin-edge light bleed)
    gX_Color color;
} gX_Raymarch_Clouds;

//ai: volumetric height fog + sun shafts (opt-in; mirrors the gfx fog_volume
//   parameter semantics).
typedef struct gX_Raymarch_VolFog {
    gX_F32   density;
    gX_F32   heightFalloff;
    gX_F32   scattering;
    gX_F32   hgAsymmetry;
    gX_Color color;
} gX_Raymarch_VolFog;

typedef struct gX_Raymarch_Sun {
    gX_F32   brightness;
    gX_F32   size;
    gX_F32v3 dir;
    gX_Color color;
} gX_Raymarch_Sun;

typedef struct gX_Raymarch_Terrain {
    gX_F32v3    offset;
    gX_F32v3    scale;
    gX_F32Qua   rotate;
    gX_I32      seed;
    gX_Color    color;
    gX_Color    colorSteep;
    gX_Color    colorBeach;
    gX_F32v4    parameters;
} gX_Raymarch_Terrain;

typedef struct gX_Raymarch_Water {
    gX_F32v3    offset;
    gX_F32v3    scale;
    gX_F32Qua   rotate;
    gX_F32v3    speed;
    gX_I32      seed;
    gX_Color    color;
    gX_F32v4    texturing;
    gX_F32v4    ripple;
    gX_F32v4    whirlpool;
    gX_F32      gradClamp;
} gX_Raymarch_Water;

typedef struct gX_Raymarch_Objects {
    gX_U32      index;
    gX_I32      amount;
    gX_I32      type[GX_RM_MAX_OBJECTS];
    gX_F32v3    pos[GX_RM_MAX_OBJECTS];
    gX_F32v3    matX[GX_RM_MAX_OBJECTS];
    gX_F32v3    matY[GX_RM_MAX_OBJECTS];
    gX_F32v3    matZ[GX_RM_MAX_OBJECTS];
    gX_F32v4    color[GX_RM_MAX_OBJECTS];
    gX_F32v4    parameters[GX_RM_MAX_OBJECTS];
    gX_F32v4    material[GX_RM_MAX_OBJECTS];
    //ai: independent perceptual roughness for shared HDR/SSR; historical default is matte.
    gX_F32      roughness[GX_RM_MAX_OBJECTS];
    gX_F32v4    texturing[GX_RM_MAX_OBJECTS];   //x: textureindex, y: texturetype, z: wrap, w:
    gX_F32v4    lighting[GX_RM_MAX_OBJECTS];
} gX_Raymarch_Objects;

typedef struct gX_Raymarch_Lights {
    gX_U32      index;
    gX_I32      amount;
    gX_F32      brightness[GX_RM_MAX_LIGHTS];
    gX_F32v3    pos[GX_RM_MAX_LIGHTS];
    gX_F32v4    color[GX_RM_MAX_LIGHTS];
} gX_Raymarch_Lights;

//ai: node = (opcode,A,B,smoothness); domain = (periodXYZ, packed 8-bit XYZ limits).
typedef struct gX_Raymarch_Graph
{
    gX_I8 name[128];
    gX_I8 description[256];
    gX_I32 root;
    gX_I32 count;
    gX_F32v4 nodes[GX_RM_GRAPH_SLOTS];
    gX_F32v4 domains[GX_RM_GRAPH_SLOTS];
    gX_F32v4 program[GX_RM_GRAPH_PROGRAM];
    gX_F32v4 programDomains[GX_RM_GRAPH_PROGRAM];
    gX_I8 error[128];
} gX_Raymarch_Graph;

typedef struct gX_Raymarch_Flags {
    gX_I1   enableSunShadows;
    
    gX_I1   enableObjects;
    gX_I1   enableObjectGlow;
    
    gX_I1   enableLights;
    gX_I1   enableLightShadows;
    gX_I1   enableLightGlow;
    gX_I1   enableLightIllumination;
    
    gX_I1   enableFrag;
    
    gX_I1   enableFog;
    
    gX_I1   enableTerrain;
    gX_I1   enableTerrainGrid;
    
    gX_I1   enableWater;
    
    gX_I1   enableSampleSign;
    gX_I1   enableSampleLastStep;
    
    gX_I1   enableSky;
    gX_I1   enableAO; //opencode
    gX_I1   enableProceduralSky; //opencode
    gX_I1   enableClouds; //ai: volumetric cloud layer
    gX_I1   enableProxy; //ai: proxy wireframe mode (SDF march skipped)
    gX_I1   enableVolumetricFog; //ai: height fog + godrays

} gX_Raymarch_Flags;

typedef struct gX_Raymarch_Misc {
    gX_Color lightAmbient;
    gX_F32v3 attenuation;
    gX_Color firstColor;
    gX_Color nullColor;
} gX_Raymarch_Misc;

typedef struct gX_Raymarch_Debug {
    gX_I1 showPos;
    gX_I1 showDir; //opencode
    gX_I1 showRay; //opencode
    gX_I1 showGradient;
    gX_I1 showNormal;
    gX_I1 showBounceCount; //opencode
    gX_I1 showPathSteps;
    gX_I1 showPathLength;
    gX_I1 showDist; //opencode
} gX_Raymarch_Debug;

typedef struct gX_Raymarch_Preset {
    gX_I32 id;
} gX_Raymarch_Preset;

typedef struct gX_Raymarch_Shader_Locations {
    //ai: bounded CSG program uniforms.
    gX_I32 graphCount;
    gX_I32 graphNodes;
    gX_I32 graphDomains;
    
    gX_I32 RMbounces;
    gX_I32 RMsteps;
    gX_I32 RMstepsPerBounce; //opencode
    gX_I32 RMnear;
    gX_I32 RMfar;
    gX_I32 RMstepMax; //opencode
    gX_I32 RMdistTotal; //opencode
    gX_I32 RMepsilon;
    gX_I32 RMpoly;
    
    //opencode: water animation
    gX_I32 waterSpeed;
    gX_I32 waterSeed;
    gX_I32 gradClamp;
    
    gX_I32 texSky;
    
    gX_I32 sunBright;
    gX_I32 sunPos;
    gX_I32 sunColor;
    gX_I32 sunSize; //opencode
    
    gX_I32 terrainSeed;
    gX_I32 terrainOffset;
    gX_I32 terrainScale;
    gX_I32 terrainColor;
    gX_I32 terrainColorSteep;
    gX_I32 terrainColorBeach;
    gX_I32 terrainParameters; //opencode
    gX_I32 terrainRotate; //opencode: quaternion terrain rotation
    
    gX_I32 waterOffset;
    gX_I32 waterColor;
    gX_I32 waterRipple;
    gX_I32 waterWhirlpool;
    
    gX_I32 objectAmount;
    gX_I32 objectType;
    gX_I32 objectPos;
    gX_I32 objectMatX;
    gX_I32 objectMatY;
    gX_I32 objectMatZ;
    gX_I32 objectColor;
    gX_I32 objectAttribute;
    gX_I32 objectMaterial;
    gX_I32 objectTexturing; //opencode: per-object texture mapping
    gX_I32 objectLighting;
    
    gX_I32 lightAmount;
    gX_I32 lightBrightness;
    gX_I32 lightPos;
    gX_I32 lightColor;
    gX_I32 lightAmbient;
    
    gX_I32 fogNear;
    gX_I32 fogFar;
    gX_I32 fogColor;
    
    gX_I32 enableShadows;
    gX_I32 enableGlow;
    gX_I32 enableLightGlow;
    gX_I32 enableIllu;
    gX_I32 enableFrag;
    gX_I32 enableFog;
    gX_I32 enableTerrain;
    gX_I32 enableWater;
    gX_I32 enableObjects;
    gX_I32 enableLights;
    gX_I32 enableGrid;
    gX_I32 enableLSS;
    gX_I32 enableSS;
    gX_I32 enableSky;
    
    gX_I32 firstColor;
    gX_I32 nullColor;
    gX_I32 lightAttenuation;
    
    //opencode: debug visualizer uniforms
    gX_I32 debugShowPos;
    gX_I32 debugShowDir;
    gX_I32 debugShowRay;
    gX_I32 debugShowGradient;
    gX_I32 debugShowNormal;
    gX_I32 debugShowBounceCount;
    gX_I32 debugShowPathSteps;
    gX_I32 debugShowPathLength;
    gX_I32 debugShowDist;
    
    //opencode: AO
    gX_I32 enableAO;
    gX_I32 enableLightShadows; //opencode
    gX_I32 texNoise; //opencode
    //opencode: procedural sky
    gX_I32 enableProceduralSky;
    //ai: volumetric clouds
    gX_I32 enableClouds;
    gX_I32 cloudBottom;
    gX_I32 cloudTop;
    gX_I32 cloudCoverage;
    gX_I32 cloudDensity;
    gX_I32 cloudScale;
    gX_I32 cloudSpeed;
    gX_I32 cloudAbsorb;
    gX_I32 cloudPowder;
    gX_I32 cloudColor;
    //ai: volumetric fog
    gX_I32 enableVolFog;
    gX_I32 volFogDensity;
    gX_I32 volFogHeightFalloff;
    gX_I32 volFogScattering;
    gX_I32 volFogHG;
    gX_I32 volFogColor;
    
} gX_Raymarch_Shader_Locations;

typedef struct gX_Raymarch_Shader {
    gX_Shader_Wrapper shader;
    gX_Raymarch_Shader_Locations locations;
    //opencode: track whether locations have been queried
    gX_I1 locationsLoaded;
} gX_Raymarch_Shader;

typedef struct gX_Raymarch_Texture {
    gX_Texture_Wrapper skybox;
    gX_Texture_Wrapper textures[GX_RM_MAX_TEXTURES];
    gX_Texture          noiseTex; //opencode: pre-generated noise texture
    gX_I32              noiseSeed; //opencode: terrain.seed the noiseTex was generated with
} gX_Raymarch_Texture;

//opencode: cached render target for per-module resolution scaling
typedef struct gX_Raymarch_CachedRT {
    gX_RenderTexture rt;
    gX_U16 width;
    gX_U16 height;
} gX_Raymarch_CachedRT;



typedef struct gX_RendererRaymarch {
    gX_I8 name[128];
    gX_I8 description[256];
    
    gX_Raymarch_State state;
    
    gX_Raymarch_Camera camera;
    
    gX_Raymarch_Parameters parameters;
    gX_Raymarch_Fog fog;
    gX_Raymarch_Clouds clouds; //ai: volumetric cloud layer
    gX_Raymarch_VolFog volFog; //ai: volumetric height fog + sun shafts
    gX_Raymarch_Sun sun;
    gX_Raymarch_Terrain terrain;
    gX_Raymarch_Water water;
    gX_Raymarch_Objects objects;
    //ai: root -1 preserves the legacy independent-object union.
    gX_Raymarch_Graph graph;
    gX_Raymarch_Lights lights;
    gX_Raymarch_Flags flags;
    gX_Raymarch_Misc misc;
    gX_Raymarch_Debug debug;
    
    gX_Raymarch_Preset preset;
    
    gX_Raymarch_Shader shader;
    gX_Raymarch_Texture texture;
    
    //opencode: GPU timer queries
    gX_RHI_Query_Timer timerDraw;
    gX_RHI_Query_Timer timerTotal;
    gX_I1 timersInited;
    
    //opencode: cached render target for half-res / scaled rendering
    gX_Raymarch_CachedRT cachedRT;
    
} gX_RendererRaymarch;

gX_I0 gX_RendererRaymarch_Load(gX_RendererRaymarch *rm);
gX_I0 gX_RendererRaymarch_Unload(gX_RendererRaymarch *rm);

gX_I0 gX_RendererRaymarch_LoadClear(gX_RendererRaymarch *rm);
gX_I0 gX_RendererRaymarch_LoadDefault(gX_RendererRaymarch *rm);
gX_I0 gX_RendererRaymarch_LoadRandom(gX_RendererRaymarch *rm);
gX_I0 gX_RendererRaymarch_Preset_BuiltIn(gX_RendererRaymarch *rm, gX_I32 index);
gX_I0 gX_RendererRaymarch_Preset_Load(gX_RendererRaymarch *rm, const gX_I8 *path);
gX_I0 gX_RendererRaymarch_Preset_Save(gX_RendererRaymarch *rm, const gX_I8 *path);

//opencode: pure CPU terrain-noise octave (shared by the texture bake and the
//   unit tests) — value noise over `cells` per axis, seamless tiling.
gX_F32 gX_RendererRaymarch_Noise_Octave(gX_U32 x, gX_U32 y, gX_U32 size,
    gX_I32 cells, gX_F32 seed);

//ai: author-facing texture assignment (panel/browser/preset). slot -1 is the
//   skybox; 0..2 are tex0..tex2. SetPath copies the path and requests an
//   immediate reload; Reload re-reads the current file in place.
gX_I0 gX_RendererRaymarch_Texture_SetPath(gX_RendererRaymarch *rm,
    gX_I32 slot, const gX_I8 *path);
gX_I0 gX_RendererRaymarch_Texture_Reload(gX_RendererRaymarch *rm, gX_I32 slot);

//opencode: object/light slot initializers (defaults + editor add buttons).
gX_I0 gX_RendererRaymarch_Object_Init(gX_RendererRaymarch *rm, gX_U32 i);
gX_I0 gX_RendererRaymarch_Light_Init(gX_RendererRaymarch *rm, gX_U32 i);

//ai: validate/compile reachable nodes; invalid graphs render the legacy object union with an explicit error.
gX_I1 gX_RendererRaymarch_Graph_Compile(gX_RendererRaymarch *rm);
gX_F32 gX_RendererRaymarch_Graph_Combine(gX_I32 operation, gX_F32 a, gX_F32 b, gX_F32 smoothness);

//ai: pure authoring helpers; quality indices 0..3 are Draft/Balanced/High/Full.
gX_I1 gX_RendererRaymarch_Quality_Set(gX_RendererRaymarch *rm, gX_I32 level);
gX_I32 gX_RendererRaymarch_Quality_Get(const gX_RendererRaymarch *rm);
gX_I1 gX_RendererRaymarch_Object_Type_Set(gX_RendererRaymarch *rm,
    gX_I32 index, gX_I32 type);
gX_F32v3 gX_RendererRaymarch_Object_Scale_Get(const gX_RendererRaymarch *rm, gX_I32 index);
gX_I1 gX_RendererRaymarch_Object_Scale_Set(gX_RendererRaymarch *rm,
    gX_I32 index, gX_F32v3 scale);
gX_I1 gX_RendererRaymarch_Object_Rotate(gX_RendererRaymarch *rm,
    gX_I32 index, gX_F32v3 degrees);
gX_I1 gX_RendererRaymarch_Object_Remove(gX_RendererRaymarch *rm, gX_I32 index);
gX_F32v3 gX_RendererRaymarch_Object_ToWorld(const gX_Raymarch_Objects *objects,
    gX_I32 index, gX_F32v3 local);

//opencode: compile-time feature toggles (panel checkboxes; reload on change).
gX_I1 gX_RendererRaymarch_Defines_Get(gX_RendererRaymarch *rm,
    const gX_I8 *name);
gX_I0 gX_RendererRaymarch_Defines_Set(gX_RendererRaymarch *rm,
    const gX_I8 *name, gX_I1 value);

gX_I0 gX_RendererRaymarch_Shader_Reload(gX_RendererRaymarch *rm);

gX_I0 gX_RendererRaymarch_Update(gX_RendererRaymarch *rm);
gX_I0 gX_RendererRaymarch_DrawTo(gX_RendererRaymarch *rm, gX_RenderTexture *outputRT);
//ai: opt-in full-resolution opaque overlay using the external perspective camera and raster depth.
gX_I0 gX_RendererRaymarch_DrawHybrid(gX_RendererRaymarch *rm,
    gX_RenderTexture *outputRT, gX_Texture rasterDepth);
//ai: writes opaque SDF HDR color and projected depth; never samples attached depth.
gX_I0 gX_RendererRaymarch_DrawSharedDepth(gX_RendererRaymarch *rm,
    gX_RenderTexture *outputRT);
//ai: full-res primary depth without material shading; color comes from DrawHybrid.
gX_I0 gX_RendererRaymarch_DrawSharedDepthOnly(gX_RendererRaymarch *rm,
    gX_RenderTexture *outputRT);
//ai: encoded world normals and binary coverage into the SSR target's private depth.
gX_I0 gX_RendererRaymarch_DrawSharedNormals(gX_RendererRaymarch *rm,
    gX_RenderTexture *outputRT);
//ai: replace covered raster vectors with static-camera fallback ownership markers.
gX_I0 gX_RendererRaymarch_DrawSharedTemporal(gX_RendererRaymarch *rm,
    gX_RenderTexture *outputRT, gX_Texture sharedDepth);
//ai: authored-input fingerprint excludes external camera and runtime query state.
gX_U64 gX_RendererRaymarch_Temporal_Signature(const gX_RendererRaymarch *rm);

//opencode: backward-compatible alias for module macro
#define gX_RendererRaymarch_Draw(rm) gX_RendererRaymarch_DrawTo(rm, NULL)

//opencode: generate an environment cubemap from the raymarch scene
gX_Texture gX_RendererRaymarch_GenerateCubemap(gX_RendererRaymarch *rm,
    gX_U32 size, gX_F32v3 position);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_RAYMARCH_H
