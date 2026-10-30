/**********************************************************************************************
*
*   [gX: src/renderer/gfx/glb_loader.h]
*   gX — a game engine
*
*   Copyright (c) 2026 lanttu, opencode
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


#ifndef RENDERER_GFX_GLB_LOADER_H
#define RENDERER_GFX_GLB_LOADER_H

#include "../../common/types.h"

#define GX_GLB_MAX_MATERIALS 32
#define GX_GLB_MAX_NODES     256
#define GX_GLB_MAX_ANIMS     16
#define GX_GLB_EXTRAS_LEN    1024

typedef struct gX_RendererGFX gX_RendererGFX;
typedef struct gX_RendererGFX_Model gX_RendererGFX_Model;

typedef struct gX_RendererGFX_GLB_TexTransform {
    gX_F32v2 offset;
    gX_F32v2 scale;
    gX_F32   rotation;
    gX_I1    hasTransform;
} gX_RendererGFX_GLB_TexTransform;

typedef struct gX_RendererGFX_GLB_Material {
    gX_I8  name[128];
    gX_I32 alphaMode; // 0=opaque, 1=mask, 2=blend
    gX_F32 alphaCutoff;

    // KHR_materials_transmission
    gX_I1   hasTransmission;
    gX_F32  transmissionFactor;

    // KHR_materials_volume
    gX_I1   hasVolume;
    gX_F32  volumeThicknessFactor;
    gX_F32  volumeAttenDist;
    gX_F32v3 volumeAttenColor;

    // KHR_materials_specular
    gX_I1    hasSpecular;
    gX_F32   specularFactor;
    gX_F32v3 specularColorFactor;

    // KHR_materials_ior
    gX_I1   hasIor;
    gX_F32  ior;

    // KHR_materials_emissive_strength
    gX_I1   hasEmissiveStrength;
    gX_F32  emissiveStrength;

    // KHR_materials_clearcoat
    gX_I1   hasClearcoat;
    gX_F32  clearcoatFactor;
    gX_F32  clearcoatRoughness;

    // KHR_materials_sheen
    gX_I1    hasSheen;
    gX_F32v3 sheenColorFactor;
    gX_F32   sheenRoughnessFactor;

    // texture_transform per map slot (0=albedo, 1=metalness, 2=normal, 3=roughness ...)
    gX_RendererGFX_GLB_TexTransform texTransform[8];
    gX_I1 hasTexCoordOverride[8];
    gX_I32 texCoord[8]; // UV set index

    // extras
    gX_I8 extras[GX_GLB_EXTRAS_LEN];

    //opencode: per-material rendering flags (applied per-mesh in draw loop)
    gX_I1  isWater;
    gX_F32 blendOpacity;
    gX_I1  enableRefraction;

    //opencode: material map flags — set during auto-detection, stored per-material
    //   for future per-mesh draw support. Currently mirrored to model-level params.
    gX_I1  hasAlbedoMap;
    gX_I1  hasNormalMap;
    gX_I1  hasMetalnessMap;
    gX_I1  hasRoughnessMap;
    gX_I1  hasAoMap;
    gX_I1  hasEmissiveMap;
    gX_I1  hasHeightMap;
    gX_I1  usePBR;
    gX_F32 albedo;
    gX_F32 roughness;
    gX_F32 metalness;
    gX_F32v3 specularColor;
    gX_F32 emissiveFactor;
    gX_F32v3 emissiveColorFactor; //opencode: full emissive RGB (the scalar alone cannot reproduce hue)
} gX_RendererGFX_GLB_Material;

typedef struct gX_RendererGFX_GLB_Node {
    gX_I8   name[128];
    gX_F32v3 pos;
    gX_F32v3 rot; // euler (degrees)
    gX_F32v3 scale;
    gX_I32   parentIdx; // -1 = root
    gX_I32   lightIdx;  // -1 = not a light
    gX_I32   meshIdx;   // -1 = no mesh
    gX_I32   cameraType; // -1 = no camera, 0=perspective, 1=orthographic
    gX_F32   cameraFov;
    gX_F32   cameraNear;
    gX_F32   cameraFar;
    gX_I8    extras[GX_GLB_EXTRAS_LEN];
} gX_RendererGFX_GLB_Node;

typedef struct gX_RendererGFX_GLB_AnimChannel {
    gX_I32  targetNode;     // -1 = none
    gX_I32  targetMaterial; // -1 = none
    gX_I32  targetPath;     // 0=translation, 1=rotation, 2=scale, 3=weights, 4=UV_offset
    gX_I32  keyframeCount;
    gX_F32 *times;   // keyframe times (seconds)
    gX_F32 *values;  // keyframe values
    gX_I32  valueStride; // 1=float, 3=vec3, 4=vec4/quat
} gX_RendererGFX_GLB_AnimChannel;

typedef struct gX_RendererGFX_GLB_Animation {
    gX_I8   name[64];
    gX_I32  channelCount;
    gX_RendererGFX_GLB_AnimChannel channels[8];
} gX_RendererGFX_GLB_Animation;

typedef struct gX_RendererGFX_GLB_Data {
    gX_I32 materialCount;
    //ai: source count allows debug selectors to address materials outside the metadata cache.
    gX_I32 sourceMaterialCount;
    //ai: compact full-source alpha routing is independent of the bounded rich-material cache.
    gX_I32 *alphaModes;
    gX_F32 *alphaCutoffs;
    gX_F32 *blendOpacities;
    //ai: opt-in COLOR_1 VBOs are indexed by the native node/primitive traversal.
    gX_U32 *terrainMaskBuffers;
    gX_I32 terrainMaskCount;
    gX_RendererGFX_GLB_Material materials[GX_GLB_MAX_MATERIALS];
    gX_I32 nodeCount;
    gX_RendererGFX_GLB_Node nodes[GX_GLB_MAX_NODES];
    gX_I32 animCount;
    gX_RendererGFX_GLB_Animation animations[GX_GLB_MAX_ANIMS];
    gX_I32 lightCount; // number of GFX lights created for this GLB
    gX_I32 lightIndices[8]; //opencode: heap indices of created lights — removed on Model_Unload
    gX_I1  hasBlendedMaterials; // quick check: any material needs blending
} gX_RendererGFX_GLB_Data;

gX_I0 gX_RendererGFX_GLB_Extract(const gX_I8 *path, gX_RendererGFX *gfx, gX_RendererGFX_Model *model);
gX_I0 gX_RendererGFX_GLB_Free(gX_RendererGFX_GLB_Data *data);
//ai: preserve COLOR_1 for opted-in native mesh materials without replacing COLOR_0.
gX_I0 gX_RendererGFX_GLB_TerrainMasksLoad(gX_RendererGFX_Model *model);

#endif
