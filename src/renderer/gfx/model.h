/**********************************************************************************************
*
*   [gX: src/renderer/gfx/model.h]
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


#ifndef RENDERER_GFX_MODEL_H
#define RENDERER_GFX_MODEL_H

#include "../../common/types.h"
#include "../../common/text.h"

#ifdef __cplusplus
extern "C" {
#endif

//opencode: forward decl for add/remove functions (defined in gfx.h after model.h include)
typedef struct gX_RendererGFX gX_RendererGFX;
//ai: forward declaration breaks the gfx/model/shader header include cycle.
typedef struct gX_RendererGFX_ShaderLocs gX_RendererGFX_ShaderLocs;

//opencode: per-material emissive target mask size (addressing applies to
//   GLB meshes; larger models skip the filter rather than overflow).
#define gX_GFX_MODEL_MESH_MASK 16
//opencode: material override sidecar (`<model>.materials.json`) capacity.
#define gX_GFX_MATERIAL_OVERRIDES 8

//opencode: per-mesh material resolution bits (see materialMeshMask).
#define gX_GFX_MMASK_EMISSIVE      1
#define gX_GFX_MMASK_ALBEDO        2
#define gX_GFX_MMASK_ROUGHNESS     4
#define gX_GFX_MMASK_METALNESS     8
#define gX_GFX_MMASK_ALBEDO_MAP    16
#define gX_GFX_MMASK_EMISSIVE_MAP  32

//opencode: one per-material override entry. `material` is a substring match
//   against GLB material names. Fields are independently opt-in:
//   `emissive` > 0 drives the glow, `albedo` is a tint (white = identity),
//   `roughness`/`metalness` >= 0 override the params scalar (-1 = unset,
//   prefilled by the loader), and `albedoMap`/`emissiveMap` are texture
//   resource subpaths (under resources/texture/) that replace the material's
//   map. Map IDs are resolved once at load into the runtime ID fields.
//   Per-field precedence: sidecar > scene params > file material (emissive
//   only; albedo/scalars have no per-mesh file sink).
typedef struct gX_RendererGFX_MaterialOverride {
    gX_I8   material[64];
    gX_Color emissiveColor;
    gX_F32  emissive;
    gX_Color albedo;
    gX_F32  roughness;
    gX_F32  metalness;
    gX_I8   albedoMap[128];
    gX_I8   emissiveMap[128];
    gX_I32  albedoMapID;    // runtime: resolved GL texture id (-1 = none)
    gX_I32  emissiveMapID;  // runtime: resolved GL texture id (-1 = none)
    //ai: opt-in COLOR_1 terrain blend, independent of COLOR_0 vertex tint.
    gX_I1   terrainBlend;
    gX_I8   terrainAlbedoMap[128];
    gX_I8   terrainDetailMap[128];
    gX_F32  terrainDetailScale;
    gX_I32  terrainAlbedoMapID;
    gX_I32  terrainDetailMapID;
    gX_Texture terrainAlbedoTexture;
    gX_Texture terrainDetailTexture;
} gX_RendererGFX_MaterialOverride;

//opencode: fixed waypoint capacity for authored model motion paths.
#define gX_GFX_MODEL_WAYPOINTS 8

//opencode: fixed pose-override capacity for authored per-bone transforms.
#define gX_GFX_MODEL_POSE_OVERRIDES 8

typedef struct gX_RendererGFX_Model_Parameters {
    gX_F32v3 pos;
    //opencode: per-axis scale (x,y,z) for non-uniform scaling
    gX_F32v3 scale;
    //opencode: rotation in euler angles (degrees)
    gX_F32v3 rotation;
    //opencode: authored circular motion path (world space), mirroring lights.
    gX_I1       orbitEnabled;
    gX_I1       orbitPaused;
    gX_I1       orbitLoop;
    gX_I32      orbitPlane;   //opencode: 0=XY, 1=XZ, 2=YZ
    gX_F32v3    orbitCenter;
    gX_F32      orbitRadius;
    gX_F32      orbitSpeed;
    gX_F32      orbitPhase;
    gX_F64      orbitElapsed;
    //opencode: waypoint motion path — flat xyz triples so the fixed-size F32
    //   schema array round-trips without a vector-array marshaller.
    gX_I32      pathMode;          //opencode: 0=circle, 1=waypoints
    gX_I32      pathWaypointCount; //opencode: 2..gX_GFX_MODEL_WAYPOINTS
    gX_F32      pathWaypoints[gX_GFX_MODEL_WAYPOINTS * 3];
    gX_I1       pathPingPong;      //opencode: waypoint loop direction reversal
    gX_I1       pathSmooth;        //opencode: Catmull-Rom segments instead of lines
    //opencode: per-waypoint Bezier handles: pathTangents are the out-handles,
    //   pathTangentsIn the in-handles. With pathHandlesIndependent = 0 the in
    //   side mirrors the out side (the original behavior); zero handles
    //   reproduce the straight polyline.
    gX_I1       pathBezier;
    gX_F32      pathTangents[gX_GFX_MODEL_WAYPOINTS * 3];
    gX_F32      pathTangentsIn[gX_GFX_MODEL_WAYPOINTS * 3];
    gX_I1       pathHandlesIndependent;

    //opencode: GLB clip playback — clip index (-1 = none), playhead in
    //   seconds, playback flags, and a non-destructive trim range. animPlaying
    //   = 0 still evaluates the static pose at animTime so scrubbing works;
    //   animEnd <= 0 (or an invalid start/end pair) means the full clip.
    gX_I32      animClip;
    gX_F64      animTime;
    gX_I1       animPlaying;
    gX_I1       animLoop;
    gX_F32      animSpeed;
    gX_F32      animStart;
    gX_F32      animEnd;
    //opencode: cross-fade — a secondary clip (index, -1 = none) and the blend
    //   weight toward it (0 = primary only, 1 = secondary only). Both clips
    //   share the playhead; the secondary wraps within its own duration when
    //   animLoop is set and clamps to its end otherwise.
    gX_I32      animClip2;
    gX_F32      animBlend;

    //opencode: pose editing — per-bone local transform overrides applied after
    //   clip sampling. Each entry is 11 floats: bone index, translation (3),
    //   rotation quaternion (4), scale (3); the count bounds the array. The
    //   editor's Key button writes the effective pose into the selected clip.
    gX_I32      poseOverrideCount;
    gX_F32      poseOverrides[gX_GFX_MODEL_POSE_OVERRIDES * 11];

    //opencode: lighting model — selects between Blinn-Phong (legacy, default)
    //   and Cook-Torrance PBR. Per-model flag, lets existing presets keep
    //   their Phong shading while new models opt into PBR.
    gX_I1   usePBR;

    gX_F32 shininess;
    
    gX_F32      albedo;
    //opencode: per-model diffuse tint (multiplied over file baseColor ×
    //   scalar; white = identity). The MC block diorama's grass/dirt read.
    gX_Color    albedoTint;
    //opencode: per-face grass-block split — top faces (+Y normals) take
    //   albedoTintTop, the rest take albedoTint. Implemented as injected
    //   vertex colors (CPU, load-time, local normals = rotation-proof);
    //   the draw path uploads a white base so colors stay absolute.
    gX_Color    albedoTintTop;
    gX_I1       useTopTint;
    gX_I1       hasAlbedoMap;
    gX_F32      albedoMapFactor;
    gX_F32v2    albedoMapScale;
    gX_F32v2    albedoMapOffset;
    
    gX_F32      metalness;
    gX_I1       hasMetalnessMap;
    gX_F32      metalnessMapFactor;
    gX_F32v2    metalnessMapScale;
    gX_F32v2    metalnessMapOffset;
    
    gX_I1       hasNormalMap;
    gX_F32      normalMapFactor;
    gX_F32v2    normalMapScale;
    gX_F32v2    normalMapOffset;
    
    gX_I1       hasHeightMap;
    gX_F32      heightMapFactor;
    gX_F32v2    heightMapScale;
    gX_F32v2    heightMapOffset;
    //opencode: parallax quality — 0=simple (single tap, cheap), 1=steep+POM
    //   (48-64 layers + jitter), 2=steep+POM+self-shadow (default, ray-march
    //   shadow on the height map). Per-model so users can dial down on
    //   low-end HW without affecting other models.
    gX_I32      parallaxMode;

    gX_I1       enableReflections; // default false
    //ai: opt-in bounded environment mixing; legacy additive reflections remain default.
    gX_I1       balancedReflection;
    //opencode: SSR receiver participation is independent of direct env shading.
    gX_I1       receiveSSR;        // default true
    gX_I1       planarReflection;   // enable planar reflection (mirror across plane)
    //opencode: omit this backing/occluding model from planar reflection captures.
    gX_I1       excludeFromPlanarCapture;
    gX_I1       reflectionSource;   //opencode: v0.3.4 - owns the mirror view (fill-only, not a consumer)
    gX_I32      mirrorPair;         //opencode: two-mirror regress tag (0=off, 1/2=facing pair)
    gX_F32      reflectionPlaneHeight;
    gX_F32v3    reflectionNormal;   //opencode: plane normal — default (0,1,0)=horizontal, (1,0,0)=vertical wall
    //opencode: interpret plane normal/distance in model-local space and follow its transform.
    gX_I1       reflectionPlaneFollowsModel;
    //opencode: non-planar reflection — per-fragment local-plane lookup
    //   (GTA4/5 style): the reflection UV bends with the surface normal
    //   instead of the single global plane. Flat surfaces are unchanged.
    gX_I1       nonPlanarReflection;
    //opencode: procedural wave displacement (vertex shader, world-Y sin field)
    gX_F32      waveAmplitude;
    gX_I1       hasEmissiveMap;
    gX_F32      emissiveFactor;
    gX_Color    emissiveColor;
    //opencode: per-material emissive targeting — substring match against
    //   GLB material names (empty = off, params apply whole-model). Matched
    //   meshes get emissiveColor/GLOW; all other meshes stay dark.
    gX_I8       emissiveMaterial[64];
    //opencode: loader forwarded per-material file emissive into params
    //   (defaults -> file -> scene). When set, every mesh resolves its own
    //   GLB material's emissive so multi-material models stop sharing the
    //   first emissive material.
    gX_I1       emissiveFromFile;
    //opencode: per-mesh material resolution — bit mask per mesh
    //   (gX_GFX_MMASK_*), effective values resolved once by the scan, and
    //   aggregate gates so the draw loops skip entirely when nothing is
    //   overridden (production models stay bit-identical).
    gX_I8       materialMeshMask[gX_GFX_MODEL_MESH_MASK];
    gX_I1       emissiveMeshActive;
    gX_I1       materialSurfaceActive;
    //opencode: per-map-type aggregates so the draw loops only touch the
    //   units a sidecar actually overrides.
    gX_I1       materialAlbedoMapActive;
    gX_I1       materialEmissiveMapActive;
    //opencode: sidecar material overrides + precomputed effective values
    //   (resolved once by the scan; the draw loop only uploads on change).
    gX_RendererGFX_MaterialOverride materialOverrides[gX_GFX_MATERIAL_OVERRIDES];
    gX_I32      materialOverrideCount;
    gX_Color    effectiveEmissiveColor[gX_GFX_MODEL_MESH_MASK];
    gX_F32      effectiveEmissiveGlow[gX_GFX_MODEL_MESH_MASK];
    gX_Color    effectiveAlbedo[gX_GFX_MODEL_MESH_MASK];
    gX_F32      effectiveRoughness[gX_GFX_MODEL_MESH_MASK];
    gX_F32      effectiveMetalness[gX_GFX_MODEL_MESH_MASK];
    gX_I32      effectiveAlbedoMap[gX_GFX_MODEL_MESH_MASK];
    gX_I32      effectiveEmissiveMap[gX_GFX_MODEL_MESH_MASK];
    //opencode: emission map scale/offset
    gX_F32v2    emissiveMapScale;
    gX_F32v2    emissiveMapOffset;
    gX_F32      emissiveMapFactor;
    
    gX_I1       enableAlphaTest;
    gX_F32      alphaCutoff;
    
    gX_I1       enableBlending;
    gX_F32      blendOpacity;
    
    //opencode: 3D text label — renders text as billboard or flat sign at model position
    gX_I1       textVisible;     // 0 = hidden, 1 = show text label
    gX_I1       textBillboard;   // 1 = camera-facing billboard, 0 = flat 3D sign
    gX_I8       textString[128]; // the text to display
    gX_F32      textFontSize;    // font size in pixels
    //opencode: optional custom label font path; empty = engine default font.
    gX_I8       textFontPath[128];
    //ai: world-axis offset from the model anchor; zero preserves legacy label placement.
    gX_F32v3    textOffset;
    //ai: new-label top anchoring and bounded styling; old presets retain neutral defaults.
    gX_I1       textAnchorTop;
    gX_F32      textGap;
    gX_F32v2    textWorldSize;
    gX_I1       textFit;
    gX_Color    textColor;
    gX_Color    textShadowColor;
    gX_F32      textShadowPixels;

    //opencode: specular color tint — per-model, overrides global white default
    gX_F32v3    specularColor;

    //opencode: gloss map — modulates shininess (Phong) or roughness (PBR)
    gX_I1       hasGlossMap;
    gX_F32      glossMapFactor;

    //opencode: texture injection — override model's normal map from textures heap
    gX_I32      injectedNormalMapID; // index into gfx->textures, -1 = disabled

    //opencode: gloss map texture — heap index, -1 = disabled
    gX_I32      glossMapID;
    //opencode: refractive/glass materials
    gX_F32      refractiveIndex; // 1.0 = air (no refraction), 1.33 = water, 1.5 = glass
    gX_I1       enableRefraction;

    //opencode: PBR metallic-roughness fields (used when usePBR==1)
    gX_F32      roughness;           // 0.0 (mirror) to 1.0 (rough), default 0.5
    gX_I1       hasRoughnessMap;
    gX_F32      roughnessMapFactor;
    gX_F32v2    roughnessMapScale;
    gX_F32v2    roughnessMapOffset;
    gX_F32      ao;                  // 0.0 (fully occluded) to 1.0 (unoccluded), default 1.0
    gX_I1       hasAoMap;
    gX_F32      aoMapFactor;
    gX_F32v2    aoMapScale;
    gX_F32v2    aoMapOffset;

    //opencode: opt-in static lightmap. lightmapID indexes gfx->textures and
    //   the shader samples the mesh's second UV channel (TEXCOORD_1).
    //   Baked RGB is an additive linear irradiance multiplier over albedo.
    gX_I1       hasLightmap;
    gX_I32      lightmapID;
    gX_F32      lightmapStrength;
    gX_F32v2    lightmapScale;
    gX_F32v2    lightmapOffset;

    //opencode: GLB water material detection — auto-set from material name match
    gX_I1       hasWater;
    gX_F32      waterScrollU;
    gX_F32      waterScrollV;

    //opencode: hardware instancing — draw N copies with per-instance transforms
    gX_U32      instanceCount;

} gX_RendererGFX_Model_Parameters;

typedef struct gX_RendererGFX_Model {
    gX_I1 loadAsap;
    gX_I1 loaded;
    gX_I1 reload;
    gX_I1 asyncLoading;    //opencode: background thread is parsing this model
    gX_I1 depthMode;
    gX_I1 useTextures;
    gX_I32 textureID[gX_GFX_MODEL_TEXTURE_SLOTS];
    gX_I32 texturePurpose[gX_GFX_MODEL_TEXTURE_SLOTS];
    gX_TextLine path;
    gX_Model model;
    gX_RendererGFX_Model_Parameters params;

    //opencode: occlusion query — GL_ARB_occlusion_query2 for visibility culling.
    //   queryId=0 = not yet created. lastVisible=1 means visible on previous frame (OR
    //   of frustumVisible + occlusionVisible, for backward compat with Occlusion_IsVisible).
    //   frustumVisible/occlusionVisible track each culling method independently
    //   so frustum culling doesn't overwrite occlusion results.
    gX_U32 occlusionQueryId;
    gX_I1  lastVisible;
    gX_I1  frustumVisible;   // 1 = within frustum (frustum culling)
    gX_I1  occlusionVisible; // 1 = passed occlusion query (occlusion culling)
    gX_I1  queryIssued;
    //opencode: 0x502 hunt — queryActive tracks whether THIS Begin/End pair
    //   actually opened a GL query. Begin skips when a previous query is
    //   still pending (slow drivers); End must then skip too — EndQuery
    //   without BeginQuery is GL_INVALID_OPERATION every frame per model.
    gX_I1  queryActive;
    //opencode: AABB computed from mesh vertices during load (for occlusion test)
    gX_F32 aabbMin[3];
    gX_F32 aabbMax[3];
    gX_I1  aabbValid;
    //ai: per-model immutable file-mesh bounds, invalidated on load/unload or storage replacement.
    gX_F32 *meshBounds;
    gX_Mesh *meshBoundsOwner;
    gX_I32 meshBoundsCount;
    //opencode: M6 - quality-preset snapshot: the LOW/MEDIUM cascades force
    //   usePBR=0 / cap parallaxMode, which must restore when the preset
    //   returns to HIGH (Q-key cycling made the clobber permanent).
    gX_I1  qualitySaved;
    gX_I1  savedUsePBR;
    gX_I32 savedParallaxMode;
    //opencode: generated-mesh model (gX_RendererGFX_Model_AddGeneratedMesh) —
    //   owns the mesh + material memory and must unload it
    gX_I1  ownsMesh;

    //opencode: GLB post-parse results (populated by gX_RendererGFX_GLB_Extract)
    gX_I32 lightStartIdx;
    gX_I32 lightCount;
    struct gX_RendererGFX_GLB_Data *glbData;

    //opencode: lazily loaded per-model label font (empty path = default font).
    gX_Font textFont;
    gX_I1   textFontLoaded;
    gX_I8   textFontPathLoaded[128];

    //opencode: v0.1.38 — previous-frame model transform for the velocity
    //   buffer (per-object motion vectors). prevModelValid=0 until the
    //   velocity pass has seen this model once (first frame = zero motion).
    gX_Matrix prevModelMat;
    gX_I1     prevModelValid;
} gX_RendererGFX_Model;

gX_I32 gX_RendererGFX_Model_Init(gX_RendererGFX_Model *model, const gX_I8 *path);
gX_I32 gX_RendererGFX_Model_Load(  gX_RendererGFX_Model *model, gX_RendererGFX *gfx);
//opencode: advance a configured circular path by a frame delta; paused or
//   disabled paths preserve the current position.
gX_I0 gX_RendererGFX_Model_Orbit_Update(gX_RendererGFX_Model *model,
    gX_F64 deltaTime);
//opencode: world matrix from params (scale → rotate → translate), shared by
//   the draw path, frustum culling, and the skeleton debug overlay.
gX_Matrix gX_RendererGFX_Model_WorldMatrix(const gX_RendererGFX_Model *model);
//opencode: per-model albedo color (file base × scalar × tint; GX_TINT hook).
gX_Color gX_RendererGFX_AlbedoTint_Apply(gX_Color base, gX_F32 albedo, gX_Color tint);
gX_I32 gX_RendererGFX_Model_Unload(gX_RendererGFX *gfx, gX_RendererGFX_Model *model);

//opencode: occlusion culling — query management + culled draw
gX_I0 gX_RendererGFX_Model_Occlusion_Begin(gX_RendererGFX *gfx, gX_RendererGFX_Model *model);
gX_I0 gX_RendererGFX_Model_Occlusion_End(gX_RendererGFX *gfx, gX_RendererGFX_Model *model);
gX_I1 gX_RendererGFX_Model_Occlusion_IsVisible(gX_RendererGFX *gfx, gX_RendererGFX_Model *model);
gX_I0 gX_RendererGFX_Model_Occlusion_Poll(gX_RendererGFX *gfx, gX_RendererGFX_Model *model);
gX_I0 gX_RendererGFX_Model_Occlusion_DrawBBox(gX_RendererGFX *gfx, gX_RendererGFX_Model *model);
gX_I1 gX_RendererGFX_Model_Draw_Occluded(gX_RendererGFX_Model *model, gX_RendererGFX *gfx, gX_I1 depthMode);

//opencode: camera framing — reposition orbit camera to show a model
gX_I0 gX_RendererGFX_Model_FrameCamera(gX_RendererGFX *gfx, gX_RendererGFX_Model *model);

//opencode: hardware instancing — draw N copies with per-instance transforms
gX_I0 gX_RendererGFX_Model_DrawInstanced(gX_RendererGFX *gfx, gX_U32 modelIndex,
    gX_U32 instanceCount, gX_Matrix *transforms);

//opencode: dynamic scene management
gX_I32 gX_RendererGFX_Model_Add(     gX_RendererGFX *gfx, const gX_I8 *path);
//opencode: per-material emissive targeting for the draw loops (see model.c).
gX_I0 gX_RendererGFX_Model_MaterialOverridesLoad(gX_RendererGFX_Model *model);
gX_I0 gX_RendererGFX_Model_MaterialOverridesResolve(gX_RendererGFX_Model *model);
//opencode: resolved per-mesh material values for the exporters (MTL/GLB) —
//   the same combination the render path uploads (base color x albedo x
//   effective tint, effective emissive/scalars, override map subpaths).
typedef struct gX_RendererGFX_MeshMaterialValues {
    gX_Color albedo;
    gX_Color emissive;
    gX_F32   emissiveGlow;
    gX_F32   roughness;
    gX_F32   metalness;
    gX_I8    albedoMap[128];
    gX_I8    emissiveMap[128];
} gX_RendererGFX_MeshMaterialValues;

//opencode: matching sidecar override for a mesh's GLB material (substring
//   match, first entry wins). Shared by the material scan and the scene
//   export's MTL sidecar so both name the same material.
const gX_RendererGFX_MaterialOverride *gX_RendererGFX_Model_MeshOverride(
    const gX_RendererGFX_Model *model, gX_I32 meshIndex);
//opencode: fill the exporter material values for one mesh (see above).
gX_I0 gX_RendererGFX_Model_MeshMaterialValues(
    const gX_RendererGFX_Model *model, gX_I32 meshIndex,
    gX_RendererGFX_MeshMaterialValues *out);
gX_I0 gX_RendererGFX_Model_MeshEmissiveScan(gX_RendererGFX_Model *model);
gX_I0 gX_RendererGFX_Model_MeshEmissive(gX_RendererGFX_Model *model, gX_Shader shaderProg,
    gX_I32 factorLoc, gX_I32 colorLoc, gX_I32 meshIndex);
//ai: per-mesh surface values combine file material and scene/sidecar overrides.
gX_I0 gX_RendererGFX_Model_MeshSurface(gX_RendererGFX_Model *model, gX_Shader shaderProg,
    gX_Color baseColor, gX_I32 albedoLoc, gX_I32 roughnessLoc, gX_I32 metalnessLoc,
    gX_I32 meshIndex, gX_Color *boundAlbedo, gX_F32 *boundRoughness,
    gX_F32 *boundMetalness);
//ai: per-mesh material map binding; boundMaps/boundFlags are draw-local caches.
gX_I0 gX_RendererGFX_Model_MeshBindMaps(gX_RendererGFX_Model *model,
    gX_Shader shaderProg,
    gX_RendererGFX_ShaderLocs *locations, gX_Material *baseMat,
    gX_Material *meshMat, gX_I32 meshIndex,
    const gX_U32 *injectedMaps, gX_U32 *boundMaps, gX_I32 *boundFlags);
//opencode: add a runtime-generated mesh as a model (no GLB file) — the model
//   takes ownership of the mesh + a default material. Used by the non-planar
//   reflection demo (wavy subdivided plane).
gX_I32 gX_RendererGFX_Model_AddGeneratedMesh(gX_RendererGFX *gfx, gX_Mesh mesh);
gX_I32 gX_RendererGFX_Model_Remove(  gX_RendererGFX *gfx, gX_U32 index);
gX_I32 gX_RendererGFX_Model_RemoveAll(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_MODEL_H
