/**********************************************************************************************
*
*   [gX: src/renderer/gfx/constants.h]
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


#ifndef RENDERER_GFX_CONSTANTS_H
#define RENDERER_GFX_CONSTANTS_H

#include "../../common/types/3d.h"

#ifdef __cplusplus
extern "C" {
#endif

//opencode: keep for GLSL uniform array size (fixed in shader); heap auto-grows beyond this
#define gX_GFX_MAX_LIGHTS 32

//opencode: v0.5.0 — SSBO light path ceiling (GX_COMPUTE build). The RG32UI
//   tile mask addresses 64 heap slots (lo/hi 32-bit words), matching the
//   fragment SSBO light-data span.
#ifdef GX_COMPUTE
#define gX_GFX_MAX_LIGHTS_SSBO 64
#endif

//opencode: GFX heap capacity limits
#define gX_GFX_MODELS_HEAP_MAX   1024
#define gX_GFX_LIGHTS_HEAP_MAX   128
#define gX_GFX_TEXTURES_HEAP_MAX 256
#define gX_GFX_MAPS_HEAP_MAX     8
//opencode: decals heap was never initialized, so Decal_Add silently
//   failed forever (FirstEmpty on a zero heap returns NULL)
#define gX_GFX_DECALS_HEAP_MAX   64

//opencode: shadow map defaults
#define gX_GFX_SHADOW_MAP_SIZE  1024
#define gX_GFX_SHADOW_CUBE_SIZE 4096

//opencode: texture unit assignments — avoids silent collisions between material maps (0-11),
//   detail map, IBL cubemaps, shadow maps, cookies, and post-process samplers.
//   All post-process shaders use these same unit numbers for their samplers.
//   CRITICAL: post-process units MUST be in [0, 7] on GPUs with only 8 fragment texture units.
//   GFX main-shader units use [12, 19] (after material maps 0-11, requires 16+ units).
//   See discovery #1 in docs/gfx_godrays.md.
#define gX_GFX_TEXUNIT_BLOOM_BLUR     6   // bloom blur buffer (bloomTex in composite)
#define gX_GFX_TEXUNIT_TERRAIN_HF     7   // opencode: spike 4 heightfield (R32F, REPEAT)
#define gX_GFX_TEXUNIT_SCENE          2   // primary scene color
#define gX_GFX_TEXUNIT_SHADOW         8   // sun shadow map, CSM cascade 0, depth vis
                                            //   (was 7 — collided with material map slot 7/cubemap)
#define gX_GFX_TEXUNIT_DEPTH          3   // depth for SSR/SSAO/DOF/motionblur/TAA
#define gX_GFX_TEXUNIT_COLOR          4   // color for SSR/motionblur/DOF blur/TAA
#define gX_GFX_TEXUNIT_AUX            5   // aux: bgTex, SSAO blur input, DOF blur CoC, TAA history
#define gX_GFX_TEXUNIT_SSAO           12  // ssaoMap for phong_ambient in main shader (was 21)
#define gX_GFX_TEXUNIT_GLOSS          13  // gloss map (was 22)
#define gX_GFX_TEXUNIT_REFRACTION     14  // bgTex for refractive materials (was 23)
#define gX_GFX_TEXUNIT_DETAIL         15  // detail/dirt texture (was 24)
#define gX_GFX_TEXUNIT_IBL_IRRADIANCE 16  // irradiance cubemap (was 25)
#define gX_GFX_TEXUNIT_IBL_PREFILTER  17  // prefilter cubemap (was 26)
#define gX_GFX_TEXUNIT_IBL_BRDF       18  // BRDF integration LUT (was 27)
#define gX_GFX_TEXUNIT_REFLECTION     19  // planar reflection reflTex (was 17)
#define gX_GFX_TEXUNIT_POINT_SHADOW   20  // shared point-light shadow 2D array (layers = light*6+face; single bind, NOT 20+li)
#define gX_GFX_TEXUNIT_COOKIE         11  // light cookie atlas (single bind; was 64, INVALID:
                                             //   exceeds GL_MAX_TEXTURE_IMAGE_UNITS=32, silently unbound;
                                             //   then 23 — collided with point shadow unit 20+3)
#define gX_GFX_TEXUNIT_HIZ           22   // SSR min-reduced Hi-Z depth chain
#define gX_GFX_TEXUNIT_VELOCITY      21   // v0.1.38 motion vector buffer (TAA/GTAO/SSGI)
                                            //   (was 7 — collided with main-shader material slot 7/cubemap)
#define gX_GFX_TEXUNIT_SKY_LUT       23   // SSR equirect sky fallback LUT
#define gX_GFX_TEXUNIT_SSR_ENV       24   // SSR dynamic reflection cubemap fallback (v0.1.11)
#define gX_GFX_TEXUNIT_SKYBOX         9   // sky environment cubemap (cubeMap sampler)
#define gX_GFX_TEXUNIT_CSM_ARRAY      10  // CSM cascade array texture
#define gX_GFX_TEXUNIT_SSGI_RAW       25  // v0.3-M4 raw gather (pre-denoise)
#define gX_GFX_TEXUNIT_SSR_NORMAL     27  //opencode: opt-in SSR shading-normal buffer
#define gX_GFX_TEXUNIT_FOG_CSM1      26   // v0.3.8 fog cascade copy layer 1
#define gX_GFX_TEXUNIT_FOG_CSM2      28   // v0.3.8 fog cascade copy layer 2
#define gX_GFX_TEXUNIT_FOG_CSM3      29   // v0.3.8 fog cascade copy layer 3
#define gX_GFX_TEXUNIT_LIGHTMAP      26   //opencode: opt-in static lightmap (main model pass; not co-bound with fog CSM1)
#define gX_GFX_TEXUNIT_FOG_PTSHADOW   1   // opencode: fog point-shadow atlas (plain-2D tile
                                          //   copies of point array layers; the fog gen program
                                          //   cannot declare sampler2DArray — M3.1 poison.
                                          //   Shares 1 with SSR_VELOCITY (SSR pass only,
                                          //   never co-bound in a fog-gen draw).
#define gX_GFX_TEXUNIT_FOG_LUT       21  // v0.3.8 per-direction fog delta LUT (SSR escapes)
#define gX_GFX_TEXUNIT_SSGI_BLUR      26  // v0.3-M4 spatially denoised current
#define gX_GFX_TEXUNIT_SSGI_HIST      27  // v0.3-M4 temporally accumulated
#define gX_GFX_TEXUNIT_GTAO_ACCUM_CUR 30  // opencode: GTAO accum current (blurMap)
#define gX_GFX_TEXUNIT_GTAO_ACCUM_HIST 31 // opencode: GTAO accum history (parity pair)
#define gX_GFX_TEXUNIT_CONTACT        30  // opencode: contact-shadow map (spike 2; 0/1 batch-danger, 21 shared, all else taken)
#define gX_GFX_TEXUNIT_SSGI_PREV      28
#define gX_GFX_TEXUNIT_SSR_PLANAR     30  // v0.3.4 auxview: mirrored-camera RT for SSR miss fallback  // v0.3-M4 Phase3: previous frame GI (multibounce)
#define gX_GFX_TEXUNIT_SSR_VELOCITY   1  // opencode: M1-b velocity.w glass flag
                                            //   in the SSR program (21 collides
                                            //   with FOG_LUT in the same draw)
#define gX_GFX_TEXUNIT_SSR_PLANAR_DEPTH 31  // audit: planar depth validity mask (31 = last free unit)
                                            //   (was 52 — INVALID: exceeds the 32-unit limit,
                                            //   sampler was never actually bound; then 22 — collided
                                            //   with point shadow unit 20+2, corrupting sampler
                                            //   validation = "program texture usage" draw failures)
#define gX_GFX_TEXUNIT_PROBE_B        31  // opencode: probe-grid second-nearest blend cube (MAIN pass).
                                            //   Shares 31 with SSR_PLANAR_DEPTH, which binds only inside
                                            //   the SSR pass (never co-resident with a main-pass model
                                            //   draw); unit 30 is unsafe here (GX_T_CONTACTAPPLY binds it
                                            //   in the same per-model upload sequence).
#define gX_GFX_TEXUNIT_ORACLE_ALBEDO  29  // opencode: compute-only base-color texture array; pass-local alias.
//ai: main-model terrain samplers; fog/compute aliases run in separate programs and passes.
#define gX_GFX_TEXUNIT_TERRAIN_ALBEDO 28
#define gX_GFX_TEXUNIT_TERRAIN_DETAIL 29

//opencode: model texture binding slots — capped at 8 for 8-unit GPU safety.
#define gX_GFX_MODEL_TEXTURE_SLOTS      8
#define gX_GFX_MAX_MATERIAL_MAPS        8

//opencode: transparent model sort limit (stack array)
#define gX_GFX_MAX_TRANSPARENT        128
#define gX_GFX_MAX_POINT_SHADOW_LIGHTS 64  //opencode: shadow-casting point lights (2D array = 6*N layers)
#define gX_GFX_POINT_SHADOW_ARRAY_SIZE 512 //opencode: shared array slice size

//opencode: SSR tuning defaults
#define gX_GFX_SSR_MAX_STEPS          96
#define gX_GFX_SSR_STEP_SIZE            0.25f  //opencode: closes coarse silhouette misses
#define gX_GFX_SSR_MAX_DISTANCE        100.0f
#define gX_GFX_SSR_DEPTH_BIAS           0.25f
#define gX_GFX_SSR_OPACITY              0.8f
#define gX_GFX_SSR_EDGE_FADE            0.08f   //opencode: screen-edge dissolve band (fraction of screen)
#define gX_GFX_SSR_MAX_BOUNCES          1       //opencode: extra ray bounces after the first hit
#define gX_GFX_SSR_BOUNCE_ATTEN         0.4f    //opencode: per-bounce energy attenuation
#define gX_GFX_SSR_MAX_DEPTH_DELTA      0.6f    //opencode: silhouette crossing rejection
#define gX_GFX_SSR_WORLD_GAP_THRESHOLD  0.5f    //opencode: world-space hit validation
#define gX_GFX_SSR_FINAL_DEPTH_THRESHOLD -0.02f //opencode: post-bisection depth validation
#define gX_GFX_SSR_HIT_FACING_THRESHOLD -1.0f   //opencode: disabled by default
#define gX_GFX_REFLECTION_CUBE_SIZE     512     //opencode: v0.1.14 — dynamic reflection cubemap (was 1024: 4x cheaper, visually identical for the blurry env fallback)

//opencode: SSAO blur edge threshold (common SSAO default)
#define gX_GFX_SSAO_EDGE_THRESHOLD      0.05f

//opencode: IBL BRDF LUT resolution
#define gX_GFX_IBL_LUT_SIZE           256

//opencode: sky visual defaults
#define gX_GFX_SKYBOX_CUBE_SIZE     990.0f
#define gX_GFX_SUN_DISK_SIZE       800.0f
#define gX_GFX_MOON_SIZE           700.0f

//opencode: shader path buffer size
#define gX_GFX_PATH_BUFFER           512

//opencode: sky defaults
#define gX_GFX_AMBIENT_STRENGTH      0.5f
#define gX_GFX_AMBIENT_COLOR        ((gX_Color){230,230,230,255})

//opencode: fog defaults
#define gX_GFX_FOG_NEAR              5.0f
#define gX_GFX_FOG_FAR              20.0f
#define gX_GFX_FOG_HEIGHT_DENSITY    0.0f
#define gX_GFX_FOG_HEIGHT_BASE       0.0f
#define GX_FOGVOL_MAX_POINT_LIGHTS   8   //opencode: item-13 - packed point lights in the fog march
#define GX_FOGVOL_MAX_PT_SHADOW      4   //opencode: shadow-casting point lights in the fog march
                                          //   (atlas tiles; further casters stay unshadowed)

//opencode: bloom defaults (intensity 1.2->6.0: 1.2 measured ~no-op on
//   every probe scene - 12px over threshold on showcase, 0px on minimal;
//   6 gives textbook localized glow on dark baselines - 2.6% footprint,
//   +5.9 local / +0.2 global, bit-stable - and stays negligible on bright
//   scenes; 8 washes out. Vision-verdict: shippable, no blowout.)
#define gX_GFX_BLOOM_THRESHOLD       0.85f
#define gX_GFX_BLOOM_KNEE            0.3f
#define gX_GFX_BLOOM_INTENSITY       6.0f
#define gX_GFX_BLOOM_BLUR_AMOUNT     1.0f
#define gX_GFX_BLOOM_EXPOSURE        1.0f
#define gX_GFX_BLOOM_VIGNETTE        0.5f
#define gX_GFX_BLOOM_SATURATION      1.0f
#define gX_GFX_BLOOM_CONTRAST        1.0f

//opencode: detail texture defaults
#define gX_GFX_DETAIL_UV_SCALE       8.0f
#define gX_GFX_DETAIL_STRENGTH       0.0f
#define gX_GFX_DETAIL_DIST_FADE     20.0f

//opencode: CSM defaults
#define gX_GFX_CSM_SPLIT0           10.0f
#define gX_GFX_CSM_SPLIT1           30.0f
#define gX_GFX_CSM_SPLIT2           60.0f
#define gX_GFX_CSM_SPLIT3          100.0f
#define gX_GFX_CSM_VIEW_SPLIT0      15.0f
#define gX_GFX_CSM_VIEW_SPLIT1      50.0f
#define gX_GFX_CSM_VIEW_SPLIT2     120.0f
#define gX_GFX_CSM_FRUSTUM0          8.0f
#define gX_GFX_CSM_FRUSTUM1         30.0f
#define gX_GFX_CSM_FRUSTUM2         80.0f
#define gX_GFX_CSM_FRUSTUM3        200.0f

//opencode: model parameter defaults
#define gX_GFX_SHININESS_DEFAULT      32.0f
#define gX_GFX_ALBEDO_DEFAULT         0.9f
#define gX_GFX_METALNESS_DEFAULT      0.5f
#define gX_GFX_MAP_SCALE_DEFAULT     10.0f
#define gX_GFX_HEIGHT_FACTOR_DEFAULT  0.3f
#define gX_GFX_ALPHA_CUTOFF_DEFAULT   0.5f
#define gX_GFX_BLEND_OPACITY_DEFAULT  0.5f
#define gX_GFX_ROUGHNESS_DEFAULT      0.5f
#define gX_GFX_AO_DEFAULT             1.0f
#define gX_GFX_EMISSIVE_FACTOR_DEF    0.0f
#define gX_GFX_REFRACTIVE_INDEX_DEF   1.0f
#define gX_GFX_TEXT_FONT_SIZE_DEF    24.0f
#define gX_GFX_GLOSS_FACTOR_DEF       1.0f
#define gX_GFX_REFLECTION_PLANE_HEIGHT (-1.0f)
#define gX_GFX_PARALLAX_MODE_DEF      2

//opencode: model loader limits
#define gX_GFX_MODEL_SIZE_WARN         (5 * 1024 * 1024)
#define gX_GFX_VERTEX_TANGENT_LIMIT    100000

//opencode: light marker defaults
#define gX_GFX_MARKER_BASE_RADIUS     0.10f
#define gX_GFX_MARKER_RINGS          12
#define gX_GFX_MARKER_SLICES         16
#define gX_GFX_MARKER_BRIGHT_COEFF    0.15f
#define gX_GFX_MARKER_SIZE            0.14f
#define gX_GFX_MARKER_SIZE_MIN        0.05f
#define gX_GFX_MARKER_BRIGHTNESS_MAX  8.0f
#define gX_GFX_MARKER_EMISSIVE_MAX    2.0f

//opencode: reflection bias default
#define gX_GFX_REFLECTION_BIAS_DEFAULT 0.5f

//opencode: shader tuning constants (GLSL hardcoded -> uploadable)
#define gX_GFX_SHADOW_BIAS_MAX      0.002f
#define gX_GFX_SHADOW_BIAS_MIN      0.00025f
#define gX_GFX_PARALLAX_MIN_LAYERS  48.0f
#define gX_GFX_PARALLAX_MAX_LAYERS  64.0f
#define gX_GFX_PARALLAX_OFFSET       0.2f
#define gX_GFX_PARALLAX_SHADOW_OFFSET 0.15f
#define gX_GFX_PARALLAX_SHADOW_STEPS 16.0f
#define gX_GFX_POINT_SHADOW_BIAS     0.015f
#define gX_GFX_POINT_SHADOW_EXTRA    0.005f
#define gX_GFX_POINT_SHADOW_MIN_BIAS 0.01f
#define gX_GFX_TANGENT_LENGTH_THRESH 0.001f
#define gX_GFX_NORMAL_Y_THRESHOLD    0.999f

//opencode: sky FOV multiplier — matches gfx.c procedural sky
#define gX_GFX_SKY_FOV_MULTIPLIER     2.5f

//opencode: skeleton debug overlay — bone cap mirrors the wrapper comment (256)
#define gX_GFX_SKELETON_MAX_BONES     256
//opencode: per-draw bone upload cap — the shader uniform arrays declare this
//   many mat4s (standard/depth/point-depth). 128 mat4 = 2048 uniform
//   components, above the GL 3.3 minimum floor of 1024 but within every
//   desktop GPU's budget; the evaluator itself supports 256 bones.
#define gX_GFX_SKIN_MAX_BONES         gX_SKIN_MAX_BONES
#define gX_GFX_SKELETON_JOINT_SIZE    0.05f  // fallback when no AABB is available
#define gX_GFX_SKELETON_JOINT_RATIO   0.02f  // fraction of the world AABB extent
#define gX_GFX_SKELETON_JOINT_MIN     0.005f
#define gX_GFX_SKELETON_JOINT_MAX     0.25f

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_CONSTANTS_H
