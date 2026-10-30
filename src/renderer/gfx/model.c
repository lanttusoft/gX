/**********************************************************************************************
*
*   [gX: src/renderer/gfx/model.c]
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
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "../gfx.h"
#include "model.h"
#include "../../common/error.h"
#include "animation.h"
#include "motion_path.h"
#include "reflection_plane.h"
#include "boilerplate.h"
#include "../renderer.h"
#include "glb_loader.h"
#include "material_contract.h"
#include "mesh_visibility.h"

#include "../../wrappers/wrappers.h"
#include "../../wrappers/misc.h"
#include "../../resource/resource.h"

#include "../../main.h"
#include "../../camera.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/utils.h"
#include "../../common/schema.h"
#include "../../common/marshal/schema_json.h"
#include "../../gxmath/matrix.h"
#include "../../common/utils/billboard.h" //opencode: 3D text billboard

#include "../../gxmath/matrix.h"
#include "../../gxmath/constants.h"
#include "../../camera.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_state.h"
//opencode: hunt - implicit-decl sweep
#include "gl_level.h"
#include "../../rhi/rhi_buf.h"
#include "../../rhi/gl_state.h"
//ai: terrain-owned external textures use the same mip/filter contract as native material maps.
#include "../../rhi/rhi_tex.h"

//opencode: per-material emissive glow strength for the scene-level params
//   filter (sidecar overrides carry their own `emissive` scalar).
#define GX_EMISSIVE_MATCH_GLOW 2.5f

//opencode: material override sidecar loader. The sidecar sits next to the
//   model file, keyed by stem ("fixture.glb" -> "fixture.materials.json").
//   Format: { "materials": [ { material, emissiveColor, emissive,
//   albedo, roughness, metalness }, ... ] }.
//   GX_MATERIALS_OFF=1 skips loading entirely (A/B captures, debugging).
//opencode: sidecar field table — material is required, authoring values are
//   sparse, and the array lives inside params for simple schema offsets.
static gX_SchemaField gX_materialOverrideFields[] = {
    GX_SCHEMA_REQUIRED_STRING("material", gX_RendererGFX_MaterialOverride, material),
    GX_SCHEMA_SPARSE_COLOR("emissiveColor", gX_RendererGFX_MaterialOverride, emissiveColor),
    GX_SCHEMA_SPARSE_F32("emissive", gX_RendererGFX_MaterialOverride, emissive),
    GX_SCHEMA_SPARSE_COLOR("albedo", gX_RendererGFX_MaterialOverride, albedo),
    GX_SCHEMA_SPARSE_F32("roughness", gX_RendererGFX_MaterialOverride, roughness),
    GX_SCHEMA_SPARSE_F32("metalness", gX_RendererGFX_MaterialOverride, metalness),
    GX_SCHEMA_SPARSE_STRING("albedoMap", gX_RendererGFX_MaterialOverride, albedoMap),
    GX_SCHEMA_SPARSE_STRING("emissiveMap", gX_RendererGFX_MaterialOverride, emissiveMap),
    //ai: terrain blending is explicit and absent fields preserve existing materials.
    GX_SCHEMA_BOOL_DEF("terrainBlend", gX_RendererGFX_MaterialOverride, terrainBlend, 0),
    GX_SCHEMA_SPARSE_STRING("terrainAlbedoMap", gX_RendererGFX_MaterialOverride, terrainAlbedoMap),
    GX_SCHEMA_SPARSE_STRING("terrainDetailMap", gX_RendererGFX_MaterialOverride, terrainDetailMap),
    GX_SCHEMA_SPARSE_F32("terrainDetailScale", gX_RendererGFX_MaterialOverride, terrainDetailScale),
};
static gX_SchemaField gX_materialSidecarFields[] = {
    GX_SCHEMA_ARRAY_OBJECT("materials", gX_RendererGFX_Model_Parameters, materialOverrides,
        gX_materialOverrideFields,
        (gX_U32)(sizeof(gX_materialOverrideFields) / sizeof(gX_materialOverrideFields[0])),
        gX_GFX_MATERIAL_OVERRIDES),
};

//opencode: matching sidecar override for a mesh's GLB material (substring
//   match, first entry wins). Shared by the material scan and the scene
//   export's MTL sidecar so both name the same material.
const gX_RendererGFX_MaterialOverride *gX_RendererGFX_Model_MeshOverride(
    const gX_RendererGFX_Model *model, gX_I32 meshIndex)
{
    if (!model || !model->glbData || !model->model.meshMaterial) return NULL;
    if (meshIndex < 0) return NULL;
    //ai: raylib's native prefix is derived from the full source table, not the metadata cache.
    gX_I32 glbIdx = gfx_glb_material_index(
        model->model.meshMaterial[meshIndex], model->model.materialCount,
        model->glbData->sourceMaterialCount);
    if (glbIdx < 0 || glbIdx >= model->glbData->materialCount) return NULL;
    const gX_I8 *matName = model->glbData->materials[glbIdx].name;
    for (gX_I32 oi = 0; oi < model->params.materialOverrideCount; oi++) {
        const gX_RendererGFX_MaterialOverride *cand =
            &model->params.materialOverrides[oi];
        if (cand->material[0] == '\0') continue;
        if (strstr((const char *)matName, (const char *)cand->material) != NULL)
            return cand;
    }
    return NULL;
}

//opencode: fill the exporter material values for one mesh (MTL/GLB writers).
gX_I0 gX_RendererGFX_Model_MeshMaterialValues(
    const gX_RendererGFX_Model *model, gX_I32 meshIndex,
    gX_RendererGFX_MeshMaterialValues *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    gX_I32 mask = (meshIndex >= 0 && meshIndex < gX_GFX_MODEL_MESH_MASK)
        ? model->params.materialMeshMask[meshIndex] : 0;
    const gX_Model *md = &model->model;
    gX_Color base = {200, 200, 200, 255};
    if (meshIndex >= 0 && md->materialCount > 0 && md->meshMaterial
        && md->materials) {
        gX_U32 mi = (gX_U32)md->meshMaterial[meshIndex];
        if (mi < (gX_U32)md->materialCount && md->materials[mi].maps)
            base = md->materials[mi].maps[gX_MATERIAL_MAP_ALBEDO].color;
    }
    gX_Color tint = (mask & gX_GFX_MMASK_ALBEDO)
        ? model->params.effectiveAlbedo[meshIndex]
        : (model->params.useTopTint ? (gX_Color){255, 255, 255, 255}
                                    : model->params.albedoTint);
    out->albedo = gX_RendererGFX_AlbedoTint_Apply(base,
        model->params.albedo, tint);
    out->emissive = (mask & gX_GFX_MMASK_EMISSIVE)
        ? model->params.effectiveEmissiveColor[meshIndex]
        : model->params.emissiveColor;
    out->emissiveGlow = (mask & gX_GFX_MMASK_EMISSIVE)
        ? model->params.effectiveEmissiveGlow[meshIndex]
        : model->params.emissiveFactor;
    out->roughness = (mask & gX_GFX_MMASK_ROUGHNESS)
        ? model->params.effectiveRoughness[meshIndex] : model->params.roughness;
    out->metalness = (mask & gX_GFX_MMASK_METALNESS)
        ? model->params.effectiveMetalness[meshIndex] : model->params.metalness;
    const gX_RendererGFX_MaterialOverride *ov =
        gX_RendererGFX_Model_MeshOverride(model, meshIndex);
    if (ov) {
        snprintf(out->albedoMap, sizeof(out->albedoMap), "%s", ov->albedoMap);
        snprintf(out->emissiveMap, sizeof(out->emissiveMap), "%s",
            ov->emissiveMap);
    }
}

//opencode: resolve per-mesh material values once (mask + effective values).
//   Per field: sidecar override (by material name) > scene params filter >
//   file material (emissive only). Meshes without a bit fall back to the
//   params values at upload time, so mixed models restore correctly and
//   models with nothing overridden skip the per-mesh path entirely.
gX_I0 gX_RendererGFX_Model_MeshEmissiveScan(gX_RendererGFX_Model *model)
{
    gX_Model *meshModel = &model->model;
    gX_I32 count = meshModel->meshCount;
    if (count > gX_GFX_MODEL_MESH_MASK) count = gX_GFX_MODEL_MESH_MASK;
    for (gX_I32 mi = 0; mi < count; mi++) {
        model->params.materialMeshMask[mi] = 0;
        model->params.effectiveEmissiveGlow[mi] = 0.0f;
        model->params.effectiveEmissiveColor[mi] = (gX_Color){255, 255, 255, 255};
        model->params.effectiveAlbedo[mi] = (gX_Color){255, 255, 255, 255};
        model->params.effectiveRoughness[mi] = -1.0f;
        model->params.effectiveMetalness[mi] = -1.0f;
        model->params.effectiveAlbedoMap[mi] = -1;
        model->params.effectiveEmissiveMap[mi] = -1;
    }
    model->params.emissiveMeshActive = 0;
    model->params.materialSurfaceActive = 0;
    model->params.materialAlbedoMapActive = 0;
    model->params.materialEmissiveMapActive = 0;
    if (!model->glbData || !meshModel->meshMaterial) return;
    //ai: map native assignments against the source count before checking cached metadata.
    for (gX_I32 mi = 0; mi < count; mi++) {
        gX_I32 glbIdx = gfx_glb_material_index(meshModel->meshMaterial[mi],
            meshModel->materialCount, model->glbData->sourceMaterialCount);
        if (glbIdx < 0 || glbIdx >= model->glbData->materialCount) continue;
        const gX_I8 *matName = model->glbData->materials[glbIdx].name;
        //opencode: first matching sidecar entry wins. Entries are scoped per
        //   field: an entry without `emissive` leaves emission to the
        //   lower-precedence sources.
        const gX_RendererGFX_MaterialOverride *ov =
            gX_RendererGFX_Model_MeshOverride(model, mi);
        gX_I8 *mask = &model->params.materialMeshMask[mi];
        //opencode: emissive — sidecar > scene filter > file material.
        if (ov && ov->emissive > 0.0f) {
            *mask |= gX_GFX_MMASK_EMISSIVE;
            model->params.effectiveEmissiveGlow[mi] = ov->emissive;
            model->params.effectiveEmissiveColor[mi] = ov->emissiveColor;
        } else if (model->params.emissiveMaterial[0] != '\0'
            && strstr((const char *)matName,
                (const char *)model->params.emissiveMaterial) != NULL) {
            *mask |= gX_GFX_MMASK_EMISSIVE;
            model->params.effectiveEmissiveGlow[mi] = GX_EMISSIVE_MATCH_GLOW;
            model->params.effectiveEmissiveColor[mi] = model->params.emissiveColor;
        } else if (model->params.emissiveFromFile) {
            //opencode: per-mesh file emissive — multi-material models stop
            //   sharing the first emissive material (glTF emissive_factor).
            //   Zero-factor materials are marked too so they stay dark
            //   instead of inheriting the forwarded material's glow.
            const gX_RendererGFX_GLB_Material *fm = &model->glbData->materials[glbIdx];
            *mask |= gX_GFX_MMASK_EMISSIVE;
            gX_F32 er = fm->emissiveColorFactor.x * 255.0f;
            gX_F32 eg = fm->emissiveColorFactor.y * 255.0f;
            gX_F32 eb = fm->emissiveColorFactor.z * 255.0f;
            model->params.effectiveEmissiveColor[mi] = (gX_Color){
                (gX_U8)(er > 255.0f ? 255.0f : er),
                (gX_U8)(eg > 255.0f ? 255.0f : eg),
                (gX_U8)(eb > 255.0f ? 255.0f : eb), 255};
            model->params.effectiveEmissiveGlow[mi] =
                (fm->emissiveColorFactor.x > 0.0f || fm->emissiveColorFactor.y > 0.0f
                    || fm->emissiveColorFactor.z > 0.0f)
                ? ((fm->hasEmissiveStrength && fm->emissiveStrength > 0.0f)
                    ? fm->emissiveStrength : 1.0f)
                : 0.0f;
        }
        //opencode: albedo tint and scalars — sidecar only (params fallback
        //   happens at upload time).
        if (ov) {
            *mask |= gX_GFX_MMASK_ALBEDO;
            model->params.effectiveAlbedo[mi] = ov->albedo;
        }
        if (ov && ov->roughness >= 0.0f) {
            *mask |= gX_GFX_MMASK_ROUGHNESS;
            model->params.effectiveRoughness[mi] = ov->roughness;
        }
        if (ov && ov->metalness >= 0.0f) {
            *mask |= gX_GFX_MMASK_METALNESS;
            model->params.effectiveMetalness[mi] = ov->metalness;
        }
        //opencode: map overrides — texture replacement (resolved at load).
        if (ov && ov->albedoMapID > 0) {
            *mask |= gX_GFX_MMASK_ALBEDO_MAP;
            model->params.effectiveAlbedoMap[mi] = ov->albedoMapID;
        }
        if (ov && ov->emissiveMapID > 0) {
            *mask |= gX_GFX_MMASK_EMISSIVE_MAP;
            model->params.effectiveEmissiveMap[mi] = ov->emissiveMapID;
        }
        if (*mask & gX_GFX_MMASK_EMISSIVE) model->params.emissiveMeshActive = 1;
        if (*mask & (gX_GFX_MMASK_ALBEDO | gX_GFX_MMASK_ROUGHNESS | gX_GFX_MMASK_METALNESS))
            model->params.materialSurfaceActive = 1;
        if (*mask & gX_GFX_MMASK_ALBEDO_MAP) {
            model->params.materialAlbedoMapActive = 1;
            model->params.hasAlbedoMap = 1;
        }
        if (*mask & gX_GFX_MMASK_EMISSIVE_MAP) {
            model->params.materialEmissiveMapActive = 1;
            model->params.hasEmissiveMap = 1;
        }
    }
    return;
}

//opencode: load `<path>.materials.json` if present. Failures are silent:
//   the override system is authoring data, not a runtime dependency.
//   Preset-loaded entries are left intact when no sidecar exists (the sidecar
//   replaces them when it does).
gX_I0 gX_RendererGFX_Model_MaterialOverridesLoad(gX_RendererGFX_Model *model)
{
    const gX_I8 *off = getenv("GX_MATERIALS_OFF");
    if (off && off[0] == '1') return;
    gX_I8 path[gX_GFX_PATH_BUFFER + 64];
    //opencode: sidecar sits next to the model file, keyed by stem
    //   ("...fixture.glb" -> "...fixture.materials.json"). Callers pass either
    //   a full resource path or a bare name; the extension is stripped in
    //   both cases.
    gX_I8 stem[gX_GFX_PATH_BUFFER];
    snprintf(stem, sizeof(stem), "%s", model->path);
    gX_I8 *dot = strrchr((char *)stem, '.');
    gX_I8 *slash = strrchr((char *)stem, '/');
    if (dot && (!slash || dot > slash)) *dot = '\0';
    if (slash) {
        snprintf(path, sizeof(path), "%s.materials.json", stem);
    } else {
        snprintf(path, sizeof(path), GX_PATH("model", "") "%s.materials.json", stem);
    }
    FILE *probe = fopen(path, "rb");
    if (!probe) return;
    fclose(probe);
    //opencode: a sidecar replaces preset-loaded entries. Parse into a
    //   defaults-prefilled array; an invalid sidecar leaves an empty
    //   override set. Missing fields keep these defaults: albedo white
    //   (identity tint), scalars -1 (no override), emissive 0 (no glow),
    //   map IDs -1 (unresolved).
    model->params.materialOverrideCount = 0;
    memset(model->params.materialOverrides, 0, sizeof(model->params.materialOverrides));
    for (gX_I32 oi = 0; oi < gX_GFX_MATERIAL_OVERRIDES; oi++) {
        model->params.materialOverrides[oi].albedo = (gX_Color){255, 255, 255, 255};
        model->params.materialOverrides[oi].roughness = -1.0f;
        model->params.materialOverrides[oi].metalness = -1.0f;
        model->params.materialOverrides[oi].albedoMapID = -1;
        model->params.materialOverrides[oi].emissiveMapID = -1;
        model->params.materialOverrides[oi].terrainDetailScale = 0.1f;
    }
    gX_I32 loaded = gX_Schema_JSON_Load(gX_materialSidecarFields,
        (gX_U32)(sizeof(gX_materialSidecarFields) / sizeof(gX_materialSidecarFields[0])),
        (gX_I0 *)&model->params, path);
    if (loaded != 0) return;
    //opencode: fill loader defaults where the sidecar left them out.
    for (gX_I32 oi = 0; oi < gX_GFX_MATERIAL_OVERRIDES; oi++) {
        gX_RendererGFX_MaterialOverride *ov = &model->params.materialOverrides[oi];
        if (ov->material[0] == '\0') continue;
        if (ov->emissiveColor.a == 0) ov->emissiveColor = (gX_Color){255, 255, 255, 255};
        if (ov->albedo.a == 0) ov->albedo = (gX_Color){255, 255, 255, 255};
        model->params.materialOverrideCount++;
    }
    if (model->params.materialOverrideCount > 0)
        gX_TraceLog_GFX(gX_LOG_NOTICE, "MATERIALS: %d override(s) loaded from %s",
            model->params.materialOverrideCount, path);
    return;
}

//opencode: resolve override map paths to GL texture ids (resource subpaths
//   under resources/texture/, e.g. "orb.png", "misc/testcard.png"). Runs
//   after the sidecar load and after preset load, so entries from either
//   source bind; resolved ids survive until the next load. Failures leave
//   the id unset (the scan then ignores that map field).
gX_I0 gX_RendererGFX_Model_MaterialOverridesResolve(gX_RendererGFX_Model *model)
{
    for (gX_I32 oi = 0; oi < model->params.materialOverrideCount; oi++) {
        gX_RendererGFX_MaterialOverride *ov = &model->params.materialOverrides[oi];
        if (ov->terrainBlend) {
            const gX_I8 *paths[2] = {ov->terrainAlbedoMap, ov->terrainDetailMap};
            gX_I32 *ids[2] = {&ov->terrainAlbedoMapID, &ov->terrainDetailMapID};
            gX_Texture *textures[2] = {&ov->terrainAlbedoTexture, &ov->terrainDetailTexture};
            for (gX_I32 ti = 0; ti < 2; ti++) {
                if (!paths[ti][0] || *ids[ti] > 0) continue;
                gX_I8 resolved[gX_GFX_PATH_BUFFER];
                snprintf(resolved, sizeof(resolved), "%s%s",
                    paths[ti][0] == '/' ? "" : GX_PATH("texture", ""), paths[ti]);
                FILE *file = fopen(resolved, "rb");
                if (file) {
                    fclose(file);
                    gX_Image image = gX_Image_Load(resolved);
                    if (image.data && image.width > 0 && image.height > 0) {
                        gX_Image_Format(&image, gX_RHI_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
                        *textures[ti] = gX_Texture_LoadFromData((gX_U32)image.width,
                            (gX_U32)image.height, image.data);
                    }
                    gX_Image_Unload(image);
                }
                if (textures[ti]->id > 0) {
                    gX_RHI_Tex_GenMipmap(textures[ti]->id, gX_RHI_TEXTURE_2D);
                    gX_RHI_Tex_Param(textures[ti]->id, gX_RHI_TEXTURE_2D,
                        gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_LINEAR_MIPMAP_LINEAR);
                    gX_RHI_Tex_Param(textures[ti]->id, gX_RHI_TEXTURE_2D,
                        gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_LINEAR);
                    gX_Texture_SetWrap(*textures[ti], gX_TEXTURE_WRAP_REPEAT);
                    *ids[ti] = (gX_I32)textures[ti]->id;
                }
                else gX_TraceLog_GFX(gX_LOG_WARNING,
                    "TERRAIN_BLEND: texture '%s' unresolved for '%s'", paths[ti], model->path);
            }
        }
        if (ov->albedoMap[0] != '\0' && ov->albedoMapID <= 0) {
            gX_Texture *tex = gX_Resource_LoadTexture(ov->albedoMap);
            if (tex && tex->id > 0) {
                ov->albedoMapID = (gX_I32)tex->id;
            } else {
                gX_TraceLog_GFX(gX_LOG_WARNING,
                    "MATERIALS: albedoMap '%s' unresolved for '%s'",
                    ov->albedoMap, model->path);
            }
        }
        if (ov->emissiveMap[0] != '\0' && ov->emissiveMapID <= 0) {
            gX_Texture *tex = gX_Resource_LoadTexture(ov->emissiveMap);
            if (tex && tex->id > 0) {
                ov->emissiveMapID = (gX_I32)tex->id;
            } else {
                gX_TraceLog_GFX(gX_LOG_WARNING,
                    "MATERIALS: emissiveMap '%s' unresolved for '%s'",
                    ov->emissiveMap, model->path);
            }
        }
    }
    return;
}

//ai: resolve large-GLB file emission beyond the bounded sidecar cache without growing that cache.
gX_I0 gX_RendererGFX_Model_MeshEmissive(gX_RendererGFX_Model *model, gX_Shader shaderProg,
    gX_I32 factorLoc, gX_I32 colorLoc, gX_I32 meshIndex)
{
    //opencode: skip redundant uploads — the raw program bind inside
    //   SetValue desyncs rlgl and perturbs speculars (verified on the fixture
    //   shoe highlights). Readback is exact for float uniforms. Meshes
    //   without the bit fall back to the params values, so a model that
    //   mixes overridden and plain meshes restores correctly.
    if ((!model->params.emissiveMeshActive && !model->params.emissiveFromFile) || factorLoc < 0) return;
    if (meshIndex < 0 || meshIndex >= model->model.meshCount) return;
    gX_I1 perMesh = meshIndex < gX_GFX_MODEL_MESH_MASK && (model->params.materialMeshMask[meshIndex]
        & gX_GFX_MMASK_EMISSIVE) ? 1 : 0;
    gX_F32 want = perMesh ? model->params.effectiveEmissiveGlow[meshIndex]
                          : model->params.emissiveFactor;
    gX_Color wantColor = perMesh ? model->params.effectiveEmissiveColor[meshIndex]
                                  : model->params.emissiveColor;
    //ai: fallback follows unresolved material metadata, not just the mesh-mask capacity.
    if (!perMesh && model->params.emissiveFromFile
        && model->glbData && model->model.meshMaterial)
    {
        want = 0.0f;
        wantColor = (gX_Color){0, 0, 0, 255};
        //ai: source indexing remains correct when the metadata table is truncated.
        gX_I32 Index = gfx_glb_material_index(
            model->model.meshMaterial[meshIndex], model->model.materialCount,
            model->glbData->sourceMaterialCount);
        if (Index >= 0 && Index < model->glbData->materialCount)
        {
            const gX_RendererGFX_GLB_Material *Material = &model->glbData->materials[Index];
            gX_F32 Red = Material->emissiveColorFactor.x * 255.0f;
            gX_F32 Green = Material->emissiveColorFactor.y * 255.0f;
            gX_F32 Blue = Material->emissiveColorFactor.z * 255.0f;
            wantColor = (gX_Color){(gX_U8)(Red > 255.0f ? 255.0f : Red),
                (gX_U8)(Green > 255.0f ? 255.0f : Green),
                (gX_U8)(Blue > 255.0f ? 255.0f : Blue), 255};
            want = (Red > 0.0f || Green > 0.0f || Blue > 0.0f)
                ? ((Material->hasEmissiveStrength && Material->emissiveStrength > 0.0f)
                    ? Material->emissiveStrength : 1.0f) : 0.0f;
        }
        //ai: raylib keeps base glTF emissive RGB per native material outside the metadata cache.
        else if (Index >= 0 && model->model.materials)
        {
            gX_I32 NativeIndex = model->model.meshMaterial[meshIndex];
            if (NativeIndex >= 0 && NativeIndex < model->model.materialCount
                && model->model.materials[NativeIndex].maps)
            {
                wantColor = model->model.materials[NativeIndex].maps[gX_MATERIAL_MAP_EMISSION].color;
                want = (wantColor.r || wantColor.g || wantColor.b) ? 1.0f : 0.0f;
            }
        }
    }
    gX_F32 current = 0.0f;
    gX_RHI_GetUniformfv(shaderProg.id, factorLoc, 1, &current);
    gX_I1 factorDiff = (current != want);
    gX_I1 colorDiff = 0;
    //opencode: a zero glow is paired with black, so the factor readback
    //   alone decides that state (slice-1 behavior). A positive glow can
    //   differ in hue while sharing the factor, so color is checked too.
    if (!factorDiff && want > 0.0f && colorLoc >= 0) {
        gX_F32 cur[4] = {0};
        gX_RHI_GetUniformfv(shaderProg.id, colorLoc, 4, cur);
        colorDiff = (cur[0] != (gX_F32)wantColor.r / 255.0f
            || cur[1] != (gX_F32)wantColor.g / 255.0f
            || cur[2] != (gX_F32)wantColor.b / 255.0f
            || cur[3] != (gX_F32)wantColor.a / 255.0f) ? 1 : 0;
    }
    if (!factorDiff && !colorDiff) return;
    if (factorDiff) gX_Shader_SetValue_Float(shaderProg, factorLoc, want);
    if (colorLoc >= 0)
        gX_Shader_SetValue_Color(shaderProg, colorLoc,
            want > 0.0f ? wantColor : (gX_Color){0, 0, 0, 255});
    return;
}

//ai: resolve the file material assigned by raylib's native mesh-material table.
static const gX_RendererGFX_GLB_Material *gfx_model_glb_material_for_mesh(
    const gX_RendererGFX_Model *model, gX_I32 meshIndex)
{
    if (!model || !model->glbData || !model->model.meshMaterial
        || meshIndex < 0 || meshIndex >= model->model.meshCount) return NULL;
    gX_I32 fileIndex = gfx_glb_material_index(
        model->model.meshMaterial[meshIndex], model->model.materialCount,
        model->glbData->sourceMaterialCount);
    if (fileIndex < 0 || fileIndex >= model->glbData->materialCount) return NULL;
    return &model->glbData->materials[fileIndex];
}

//ai: exact resource/material selection prevents diagnostic output leaking to neighbouring assets.
static gX_I32 gfx_model_material_debug_mode(
    const gX_RendererGFX_Model *model, gX_I32 meshIndex)
{
    const gX_I8 *mode = getenv("GX_MATERIAL_DEBUG_MODE");
    if (!mode || strcmp((const char *)mode, "baseColorMap") != 0) return -1;
    const gX_I8 *resource = getenv("GX_MATERIAL_DEBUG_RESOURCE");
    const gX_I8 *material = getenv("GX_MATERIAL_DEBUG_MATERIAL");
    const gX_I8 *materialIndex = getenv("GX_MATERIAL_DEBUG_MATERIAL_INDEX");
    gX_I1 nameSelected = material && material[0];
    gX_I1 indexSelected = materialIndex && materialIndex[0];
    if (!resource || !resource[0] || nameSelected == indexSelected) return -1;
    if (!model || strcmp((const char *)resource, (const char *)model->path) != 0)
        return 0;
    if (!model->glbData || !model->model.meshMaterial
        || meshIndex < 0 || meshIndex >= model->model.meshCount) return 0;
    gX_I32 fileIndex = gfx_glb_material_index(
        (gX_I32)model->model.meshMaterial[meshIndex],
        model->model.materialCount, model->glbData->sourceMaterialCount);
    if (fileIndex < 0) return 0;
    if (indexSelected) {
        char *end = NULL;
        long requestedIndex = strtol((const char *)materialIndex, &end, 10);
        if (!end || end == (char *)materialIndex || *end != '\0'
            || requestedIndex < 0
            || requestedIndex >= model->glbData->sourceMaterialCount) return -1;
        if (fileIndex != (gX_I32)requestedIndex) return 0;
    } else {
        if (fileIndex >= model->glbData->materialCount) return 0;
        const gX_RendererGFX_GLB_Material *fileMaterial =
            &model->glbData->materials[fileIndex];
        if (strcmp((const char *)material,
            (const char *)fileMaterial->name) != 0) return 0;
    }
    return 1;
}

//ai: report the effective texture binding and available file/shader UV transforms once per diagnostic run.
static gX_I0 gfx_model_material_debug_log(
    const gX_RendererGFX_Model *model,
    const gX_RendererGFX_GLB_Material *fileMaterial, gX_I32 fileIndex,
    gX_I32 nativeIndex, gX_U32 textureID, gX_I32 mapFlags)
{
    static gX_I1 reported = 0;
    if (reported || !model) return;
    const gX_I8 *materialName = fileMaterial
        ? fileMaterial->name : (const gX_I8 *)"<metadata-uncached>";
    gX_I32 uvSet = fileMaterial
        ? fileMaterial->texCoord[gX_MATERIAL_MAP_ALBEDO] : -1;
    gX_I1 uvOverride = fileMaterial
        ? fileMaterial->hasTexCoordOverride[gX_MATERIAL_MAP_ALBEDO] : 0;
    gX_I1 hasTransform = fileMaterial
        ? fileMaterial->texTransform[gX_MATERIAL_MAP_ALBEDO].hasTransform : 0;
    gX_F32 rotation = fileMaterial
        ? fileMaterial->texTransform[gX_MATERIAL_MAP_ALBEDO].rotation : 0.0f;
    gX_F32v2 glbScale = fileMaterial
        ? fileMaterial->texTransform[gX_MATERIAL_MAP_ALBEDO].scale
        : (gX_F32v2){0.0f, 0.0f};
    gX_F32v2 glbOffset = fileMaterial
        ? fileMaterial->texTransform[gX_MATERIAL_MAP_ALBEDO].offset
        : (gX_F32v2){0.0f, 0.0f};
    gX_TraceLog_GFX(gX_LOG_NOTICE,
        "MATERIAL_DEBUG: mode=baseColorMap resource=%s material=%s materialIndex=%d nativeMaterialIndex=%d metadataCached=%d texture=%u hasMap=%d uvSet=%d uvOverride=%d texTransform=%d rotation=%.4f glbScale=(%.4f,%.4f) glbOffset=(%.4f,%.4f) shaderScale=(%.4f,%.4f) shaderOffset=(%.4f,%.4f)",
        (const char *)model->path, (const char *)materialName, fileIndex,
        nativeIndex, fileMaterial ? 1 : 0, textureID,
        (mapFlags & (1 << gX_MATERIAL_MAP_ALBEDO)) != 0,
        uvSet, uvOverride, hasTransform, rotation,
        glbScale.x, glbScale.y, glbOffset.x, glbOffset.y,
        model->params.albedoMapScale.x, model->params.albedoMapScale.y,
        model->params.albedoMapOffset.x, model->params.albedoMapOffset.y);
    reported = 1;
}

//ai: resolve file and sidecar surface values per primitive without synchronous uniform readbacks.
gX_I0 gX_RendererGFX_Model_MeshSurface(gX_RendererGFX_Model *model, gX_Shader shaderProg,
    gX_Color baseColor, gX_I32 albedoLoc, gX_I32 roughnessLoc, gX_I32 metalnessLoc,
    gX_I32 meshIndex, gX_Color *boundAlbedo, gX_F32 *boundRoughness,
    gX_F32 *boundMetalness)
{
    const gX_RendererGFX_GLB_Material *fileMaterial =
        gfx_model_glb_material_for_mesh(model, meshIndex);
    //ai: native raylib materials retain base-color factors even when GLB metadata is uncached.
    if (!model->params.materialSurfaceActive && !fileMaterial && !model->glbData) return;
    gX_I32 mask = meshIndex >= 0 && meshIndex < gX_GFX_MODEL_MESH_MASK
        ? model->params.materialMeshMask[meshIndex] : 0;
    if (albedoLoc >= 0) {
        gX_Color tint = (mask & gX_GFX_MMASK_ALBEDO)
            ? model->params.effectiveAlbedo[meshIndex]
            : (model->params.useTopTint ? (gX_Color){255, 255, 255, 255}
                                        : model->params.albedoTint);
        gX_Color want = gX_RendererGFX_AlbedoTint_Apply(baseColor,
            model->params.albedo, tint);
        if (!boundAlbedo || boundAlbedo->r != want.r || boundAlbedo->g != want.g
            || boundAlbedo->b != want.b || boundAlbedo->a != want.a)
        {
            gX_Shader_SetValue_Color(shaderProg, albedoLoc, want);
            if (boundAlbedo) *boundAlbedo = want;
        }
    }
    if (roughnessLoc >= 0) {
        gX_F32 want = model->params.roughness;
        if (mask & gX_GFX_MMASK_ROUGHNESS)
            want = model->params.effectiveRoughness[meshIndex];
        else if (fileMaterial && fileMaterial->usePBR
            && model->params.roughness == gX_GFX_ROUGHNESS_DEFAULT)
            want = fileMaterial->roughness;
        if (!boundRoughness || *boundRoughness != want)
        {
            gX_Shader_SetValue_Float(shaderProg, roughnessLoc, want);
            if (boundRoughness) *boundRoughness = want;
        }
    }
    if (metalnessLoc >= 0) {
        gX_F32 want = model->params.metalness;
        if (mask & gX_GFX_MMASK_METALNESS)
            want = model->params.effectiveMetalness[meshIndex];
        else if (fileMaterial && fileMaterial->usePBR
            && model->params.metalness == gX_GFX_METALNESS_DEFAULT)
            want = fileMaterial->metalness;
        if (!boundMetalness || *boundMetalness != want)
        {
            gX_Shader_SetValue_Float(shaderProg, metalnessLoc, want);
            if (boundMetalness) *boundMetalness = want;
        }
    }
    return;
}

//ai: bind each primitive's GLB maps, apply overrides, and optionally select one diagnostic material.
gX_I0 gX_RendererGFX_Model_MeshBindMaps(gX_RendererGFX_Model *model,
    gX_Shader shaderProg,
    gX_RendererGFX_ShaderLocs *locations, gX_Material *baseMat,
    gX_Material *meshMat, gX_I32 meshIndex,
    const gX_U32 *injectedMaps, gX_U32 *boundMaps, gX_I32 *boundFlags)
{
    if (!baseMat || !baseMat->shader.locs || !meshMat
        || !injectedMaps || !boundMaps || meshIndex < 0) return;
    gX_I32 mask = meshIndex < gX_GFX_MODEL_MESH_MASK
        ? model->params.materialMeshMask[meshIndex] : 0;
    gX_I32 mapFlags = 0;
    gX_I32 flagLocations[7] = {
        locations ? locations->hasAlbedoMapLoc : -1,
        locations ? locations->hasMetalnessMapLoc : -1,
        locations ? locations->hasNormalMapLoc : -1,
        locations ? locations->hasRoughnessMapLoc : -1,
        locations ? locations->hasAoMapLoc : -1,
        locations ? locations->hasEmissiveMapLoc : -1,
        locations ? locations->hasHeightMapLoc : -1,
    };
    gX_U32 albedoTextureID = 0;
    for (gX_I32 mapIndex = 0; mapIndex <= gX_MATERIAL_MAP_HEIGHT; mapIndex++)
    {
        gX_Texture texture = {0};
        if (meshMat->maps) texture = meshMat->maps[mapIndex].texture;
        if (injectedMaps[mapIndex] > 0) texture.id = injectedMaps[mapIndex];
        if (mapIndex == gX_MATERIAL_MAP_ALBEDO
            && (mask & gX_GFX_MMASK_ALBEDO_MAP)
            && model->params.effectiveAlbedoMap[meshIndex] > 0)
        {
            texture.id = (gX_U32)model->params.effectiveAlbedoMap[meshIndex];
        }
        if (mapIndex == gX_MATERIAL_MAP_EMISSION
            && (mask & gX_GFX_MMASK_EMISSIVE_MAP)
            && model->params.effectiveEmissiveMap[meshIndex] > 0)
        {
            texture.id = (gX_U32)model->params.effectiveEmissiveMap[meshIndex];
        }
        if (texture.id > 0) mapFlags |= 1 << mapIndex;
        if (mapIndex == gX_MATERIAL_MAP_ALBEDO) albedoTextureID = texture.id;
        if (texture.id == 0) texture.id = gX_RHI_Tex_GetDefault();
        gX_U32 textureID = texture.id;
        if (textureID == boundMaps[mapIndex]) continue;
        gX_I32 samplerLoc = baseMat->shader.locs[15 + mapIndex];
        if (samplerLoc >= 0)
        {
            gX_Shader_SetValue_TextureSlot(shaderProg, samplerLoc, texture,
                mapIndex);
            boundMaps[mapIndex] = textureID;
        }
    }
    if (boundFlags && *boundFlags != mapFlags)
    {
        gX_RHI_Shader_Use(shaderProg.id);
        for (gX_I32 mapIndex = 0; mapIndex <= gX_MATERIAL_MAP_HEIGHT; mapIndex++)
        {
            if (flagLocations[mapIndex] >= 0)
                gX_Shader_SetValue_Int(shaderProg, flagLocations[mapIndex],
                    (mapFlags >> mapIndex) & 1);
        }
        *boundFlags = mapFlags;
    }
    gX_I32 debugMode = gfx_model_material_debug_mode(model, meshIndex);
    if (locations && locations->terrainBlendLoc >= 0) {
        const gX_RendererGFX_MaterialOverride *ov =
            gX_RendererGFX_Model_MeshOverride(model, meshIndex);
        gX_I1 active = ov && ov->terrainBlend && ov->terrainAlbedoMapID > 0
            && albedoTextureID > 0 && model->glbData
            && meshIndex < model->glbData->terrainMaskCount
            && model->glbData->terrainMaskBuffers[meshIndex] > 0;
        gX_RHI_Shader_Use(shaderProg.id);
        gX_RHI_UniformInt(locations->terrainBlendLoc, active);
        if (active) {
            gX_RHI_Tex_BindOnUnit((gX_U32)ov->terrainAlbedoMapID,
                gX_GFX_TEXUNIT_TERRAIN_ALBEDO);
            gX_RHI_UniformInt(locations->terrainAlbedoMapLoc, gX_GFX_TEXUNIT_TERRAIN_ALBEDO);
            gX_RHI_Tex_BindOnUnit(ov->terrainDetailMapID > 0
                ? (gX_U32)ov->terrainDetailMapID : gX_RHI_Tex_GetDefault(),
                gX_GFX_TEXUNIT_TERRAIN_DETAIL);
            gX_RHI_UniformInt(locations->terrainDetailMapLoc, gX_GFX_TEXUNIT_TERRAIN_DETAIL);
            gX_RHI_UniformInt(locations->terrainHasDetailLoc, ov->terrainDetailMapID > 0);
            gX_Shader_SetValue_Float(shaderProg, locations->terrainDetailScaleLoc, ov->terrainDetailScale);
        }
    }
    if (debugMode >= 0 && locations && locations->debugBaseColorMapLoc >= 0) {
        gX_RHI_Shader_Use(shaderProg.id);
        gX_Shader_SetValue_Int(shaderProg, locations->debugBaseColorMapLoc, debugMode);
        if (debugMode == 1) {
            gX_I32 fileIndex = gfx_glb_material_index(
                (gX_I32)model->model.meshMaterial[meshIndex],
                model->model.materialCount, model->glbData->sourceMaterialCount);
            const gX_RendererGFX_GLB_Material *fileMaterial = fileIndex >= 0
                && fileIndex < model->glbData->materialCount
                ? &model->glbData->materials[fileIndex] : NULL;
            gfx_model_material_debug_log(model,
                fileMaterial, fileIndex,
                (gX_I32)model->model.meshMaterial[meshIndex],
                albedoTextureID, mapFlags);
        }
    }
    return;
}

//opencode: reposition orbit camera to frame a model's bounding box
gX_I0 gX_RendererGFX_Model_FrameCamera(gX_RendererGFX *gfx, gX_RendererGFX_Model *model) {
    (void)gfx;
    if (!model || !model->aabbValid) return;
    gX_F32 extentX = (model->aabbMax[0] - model->aabbMin[0]) * model->params.scale.x;
    gX_F32 extentY = (model->aabbMax[1] - model->aabbMin[1]) * model->params.scale.y;
    gX_F32 extentZ = (model->aabbMax[2] - model->aabbMin[2]) * model->params.scale.z;
    gX_F32 maxExtent = fmaxf(fmaxf(extentX, extentY), extentZ);
    gX_F32 dist = maxExtent * 0.5f / tanf(camera.camera3D.fovy * 0.5f * gX_DEG2RAD) * 1.5f;
    if (dist < 3.0f) dist = 3.0f;
    if (dist > 5000.0f) dist = 5000.0f;
    //opencode: FrameCamera was overriding orbit params on every model load,
    //   causing the camera to jump between frames. Disabled for now — the
    //   scene init already sets a fixed camera position.
    //scene.gfxdebug.orbitTarget = center;
    //scene.gfxdebug.orbitDistance = dist;
    //gX_TraceLog(gX_LOG_INFO, "FRAME: target=(%.1f,%.1f,%.1f) distance=%.1f extent=%.1f",
    //    center.x, center.y, center.z, dist, maxExtent);
}

//ai: mesh-bound ownership is local to a model and never survives a reload.
static gX_I0 gfx_model_mesh_bounds_clear(gX_RendererGFX_Model *Model)
{
    free(Model->meshBounds);
    Model->meshBounds = NULL;
    Model->meshBoundsOwner = NULL;
    Model->meshBoundsCount = 0;
}

//ai: bounds are allocated only when culling is used; allocation failure leaves all meshes visible.
static gX_I0 gfx_model_mesh_bounds_ensure(gX_RendererGFX_Model *Model)
{
    if (Model->meshBounds && Model->meshBoundsOwner == Model->model.meshes
        && Model->meshBoundsCount == Model->model.meshCount) return;
    gfx_model_mesh_bounds_clear(Model);
    if (!Model->model.meshes || Model->model.meshCount <= 0) return;
    Model->meshBounds = calloc((size_t)Model->model.meshCount, 6 * sizeof(gX_F32));
    if (!Model->meshBounds)
    {
        gX_TraceLog(gX_LOG_WARNING, "Mesh bounds allocation failed; drawing all meshes");
        return;
    }
    Model->meshBoundsOwner = Model->model.meshes;
    Model->meshBoundsCount = Model->model.meshCount;
    for (gX_I32 Index = 0; Index < Model->meshBoundsCount; Index++)
        gfx_mesh_bounds_build(&Model->model.meshes[Index], Model->meshBounds + Index * 6);
}

//ai: zero-default label offset preserves legacy model-anchored placement.
gX_I32 gX_RendererGFX_Model_Init(gX_RendererGFX_Model *model, const gX_I8 *path) {
    model->meshBounds = NULL;
    model->meshBoundsOwner = NULL;
    model->meshBoundsCount = 0;
    model->loadAsap = true;
    
    model->useTextures = true;
    for (int i = 0; i < gX_GFX_MODEL_TEXTURE_SLOTS; i++) {
        model->textureID[i] = -1;
        model->texturePurpose[i] = -1;
    }

    //opencode: occlusion query state — lazy-created GL query on first check
    model->occlusionQueryId = 0;
    model->lastVisible = 1;
    model->frustumVisible = 1;
    model->occlusionVisible = 1;
    model->queryIssued = 0;
    model->queryActive = 0;
    
    strcpy(model->path,path);
    
    model->params.pos = (gX_F32v3){0.0f,0.0f,0.0f};
    model->params.scale = (gX_F32v3){1.0f, 1.0f, 1.0f};
    model->params.rotation = (gX_F32v3){0.0f,0.0f,0.0f};
    //opencode: motion path defaults match the light orbit contract (disabled).
    model->params.orbitEnabled = 0;
    model->params.orbitPaused = 0;
    model->params.orbitLoop = 1;
    model->params.orbitPlane = 1;
    model->params.orbitCenter = model->params.pos;
    model->params.orbitRadius = 2.0f;
    model->params.orbitSpeed = 45.0f;
    model->params.orbitPhase = 0.0f;
    model->params.orbitElapsed = 0.0;
    //opencode: waypoint paths start empty; circle mode is the default.
    model->params.pathMode = 0;
    model->params.pathWaypointCount = 0;
    model->params.pathPingPong = 0;
    model->params.pathSmooth = 0;
    memset(model->params.pathWaypoints, 0, sizeof(model->params.pathWaypoints));
    //opencode: in-handles mirror the out side until authored independently.
    model->params.pathHandlesIndependent = 0;
    memset(model->params.pathTangentsIn, 0,
        sizeof(model->params.pathTangentsIn));
    //opencode: clip playback starts disabled (-1 = no clip); playing a clip is
    //   an explicit authoring action.
    model->params.animClip = -1;
    model->params.animTime = 0.0;
    model->params.animPlaying = 0;
    model->params.animLoop = 1;
    model->params.animSpeed = 1.0f;
    //opencode: trim range starts as the full clip (end <= 0 = clip end).
    model->params.animStart = 0.0f;
    model->params.animEnd = -1.0f;
    //opencode: no secondary clip until authored (-1) and no blend weight.
    model->params.animClip2 = -1;
    model->params.animBlend = 0.0f;
    model->params.poseOverrideCount = 0;
    memset(model->params.poseOverrides, 0, sizeof(model->params.poseOverrides));
    //opencode: default to Phong (legacy) for backward compat with existing presets
    model->params.usePBR = false;
    //ai: per-model surface controls stay off for legacy presets.
    model->params.doubleSided = false;
    model->params.flipNormals = false;
    //opencode: M6 - quality snapshot starts empty (nothing forced yet)
    model->qualitySaved = false;
    model->savedUsePBR = false;
    model->savedParallaxMode = 2;

    model->params.shininess = gX_GFX_SHININESS_DEFAULT;
    
    model->params.albedo = gX_GFX_ALBEDO_DEFAULT;
    model->params.albedoTint = (gX_Color){255, 255, 255, 255};
    model->params.albedoTintTop = (gX_Color){255, 255, 255, 255};
    model->params.useTopTint = 0;
    model->params.hasAlbedoMap = false;
    model->params.albedoMapFactor = 1.0f;
    model->params.albedoMapScale = (gX_F32v2){1.0f,1.0f};
    model->params.albedoMapOffset = (gX_F32v2){0.0f,0.0f};
    
    model->params.metalness = gX_GFX_METALNESS_DEFAULT;
    model->params.hasMetalnessMap = false;
    model->params.metalnessMapFactor = 1.0f;
    model->params.metalnessMapScale = (gX_F32v2){1.0f,1.0f};
    model->params.metalnessMapOffset = (gX_F32v2){0.0f,0.0f};
    
    model->params.hasNormalMap = false;
    model->params.normalMapFactor = 1.0f;
    model->params.normalMapScale = (gX_F32v2){gX_GFX_MAP_SCALE_DEFAULT, gX_GFX_MAP_SCALE_DEFAULT};
    model->params.normalMapOffset = (gX_F32v2){0.0f,0.0f};
    
    model->params.hasHeightMap = false;
    model->params.heightMapFactor = gX_GFX_HEIGHT_FACTOR_DEFAULT;
    model->params.heightMapScale = (gX_F32v2){gX_GFX_MAP_SCALE_DEFAULT, gX_GFX_MAP_SCALE_DEFAULT};
    model->params.heightMapOffset = (gX_F32v2){0.0f,0.0f};
    model->params.parallaxMode = gX_GFX_PARALLAX_MODE_DEF;

    model->params.enableReflections = false;
    model->params.balancedReflection = false;
    //opencode: keep existing SSR coverage unless a model opts out explicitly.
    model->params.receiveSSR = true;
    model->params.planarReflection = false;
    model->params.excludeFromPlanarCapture = false;
    model->params.reflectionPlaneFollowsModel = false;
    model->params.nonPlanarReflection = false;
    model->params.mirrorPair = 0;
    model->params.waveAmplitude = 0.0f;
    model->params.reflectionPlaneHeight = gX_GFX_REFLECTION_PLANE_HEIGHT;
    model->params.reflectionNormal = (gX_F32v3){0.0f, 1.0f, 0.0f}; //opencode: default horizontal floor
    model->params.hasEmissiveMap = false;
    model->params.emissiveFactor = gX_GFX_EMISSIVE_FACTOR_DEF;
    model->params.emissiveColor = (gX_Color){0,0,0,255};
    model->params.emissiveMaterial[0] = '\0';
    model->params.emissiveFromFile = 0;
    model->params.emissiveMeshActive = 0;
    model->params.materialSurfaceActive = 0;
    model->params.materialAlbedoMapActive = 0;
    model->params.materialEmissiveMapActive = 0;
    model->params.materialOverrideCount = 0;
    model->params.emissiveMapScale = (gX_F32v2){1.0f, 1.0f};
    model->params.emissiveMapOffset = (gX_F32v2){0.0f, 0.0f};
    model->params.emissiveMapFactor = 1.0f;
    model->params.enableAlphaTest = false;
    model->params.alphaCutoff = gX_GFX_ALPHA_CUTOFF_DEFAULT;
    model->params.enableBlending = false;
    model->params.blendOpacity = gX_GFX_BLEND_OPACITY_DEFAULT;
    //opencode: specular color default (white — neutral)
    model->params.specularColor = (gX_F32v3){1.0f, 1.0f, 1.0f};

    //opencode: 3D text defaults
    model->params.textVisible = false;
    model->params.textBillboard = true;
    model->params.textString[0] = '\0';
    model->params.textFontSize = gX_GFX_TEXT_FONT_SIZE_DEF;
    model->params.textFontPath[0] = '\0';
    model->params.textOffset = (gX_F32v3){0.0f, 0.0f, 0.0f};
    model->params.textAnchorTop = false;
    model->params.textGap = 0.1f;
    model->params.textWorldSize = (gX_F32v2){2.0f, 0.5f};
    model->params.textFit = false;
    model->params.textColor = gX_WHITE;
    model->params.textShadowColor = (gX_Color){0,0,0,0};
    model->params.textShadowPixels = 0.0f;
    model->textFont = (gX_Font){0};
    model->textFontLoaded = 0;
    model->textFontPathLoaded[0] = '\0';

    model->params.hasGlossMap = false;
    model->params.glossMapFactor = gX_GFX_GLOSS_FACTOR_DEF;
    model->params.glossMapID = -1; //opencode: disabled by default
    model->params.injectedNormalMapID = -1; //opencode: disabled by default
    model->params.refractiveIndex = gX_GFX_REFRACTIVE_INDEX_DEF;
    model->params.enableRefraction = false;

    //opencode: PBR defaults
    model->params.roughness = gX_GFX_ROUGHNESS_DEFAULT;
    model->params.hasRoughnessMap = false;
    model->params.roughnessMapFactor = 1.0f;
    model->params.roughnessMapScale = (gX_F32v2){1.0f, 1.0f};
    model->params.roughnessMapOffset = (gX_F32v2){0.0f, 0.0f};
    model->params.ao = gX_GFX_AO_DEFAULT;
    model->params.hasAoMap = false;
    model->params.aoMapFactor = 1.0f;
    model->params.aoMapScale = (gX_F32v2){1.0f, 1.0f};
    model->params.aoMapOffset = (gX_F32v2){0.0f, 0.0f};

    //opencode: static lightmap defaults are inert until a preset opts in.
    model->params.hasLightmap = false;
    model->params.lightmapID = -1;
    model->params.lightmapStrength = 1.0f;
    model->params.lightmapScale = (gX_F32v2){1.0f, 1.0f};
    model->params.lightmapOffset = (gX_F32v2){0.0f, 0.0f};

    //opencode: GLB post-parse results — cleared until extracted
    model->params.hasWater = false;
    model->params.waterScrollU = 0.0f;
    model->params.waterScrollV = 0.0f;
    model->params.instanceCount = 0;
    model->lightStartIdx = -1;
    model->lightCount = 0;
    model->glbData = 0;
    
    return 0;
}

//opencode: simple file size check (stat) — warn on large files that may softlock raylib
static gX_I64 localFileSize(const gX_I8 *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (gX_I64)st.st_size;
}

//opencode: per-face grass-block split — injects vertex colors into
//   colorless meshes when params.useTopTint is set: +Y-normal verts take
//   albedoTintTop, the rest take albedoTint (absolute 0-255; the draw
//   path uploads a white base for these models so nothing double-tints).
//   Local normals make it rotation-proof (scene cubes are rotated).
//   GX_TINT=0 skips injection (kill hook collapses to uniform white).
//   Meshes with file vertex colors are never touched. The color VBO is
//   created when the mesh is already GPU-resident (raylib uploads at
//   G_Model_Load); not-yet-uploaded meshes ride the normal upload path.
static gX_I0 Model_InjectTopTint(gX_RendererGFX_Model *model)
{
    if (!model->params.useTopTint) return;
    {
        const gX_I8 *hook = getenv("GX_TINT");
        if (hook && hook[0] == '0') return;
    }
    for (gX_U32 mi = 0; mi < (gX_U32)model->model.meshCount; mi++)
    {
        gX_Mesh *mesh = &model->model.meshes[mi];
        gX_U32 vc;
        gX_U8 *cols;
        if (mesh->colors != NULL) continue;
        if (mesh->vertices == NULL || mesh->normals == NULL) continue;
        if (mesh->vertexCount < 1) continue;
        vc = (gX_U32)mesh->vertexCount;
        cols = (gX_U8*)malloc((size_t)vc * 4);
        if (!cols) continue;
        for (gX_U32 v = 0; v < vc; v++)
        {
            gX_Color c;
            if (mesh->normals[v * 3 + 1] > 0.5f) c = model->params.albedoTintTop;
            else c = model->params.albedoTint;
            cols[v * 4 + 0] = c.r;
            cols[v * 4 + 1] = c.g;
            cols[v * 4 + 2] = c.b;
            cols[v * 4 + 3] = 255;
        }
        mesh->colors = cols;
        if (mesh->vaoId > 0 && mesh->vboId != NULL)
        {
            gX_RHI_VAO_Bind(mesh->vaoId);
            mesh->vboId[3] = gX_RHI_Buffer_Create("mesh_color");
            gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, mesh->vboId[3]);
            gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, (gX_U32)(vc * 4 * sizeof(gX_U8)),
                mesh->colors, gX_RHI_STATIC_DRAW);
            gX_RHI_Attrib_Pointer(3, 4, gX_RHI_UNSIGNED_BYTE, 1, 0, NULL);
            gX_RHI_Attrib_Enable(3);
            gX_RHI_VAO_Bind(0);
            gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, 0);
        }
    }
    return;
}

gX_I32 gX_RendererGFX_Model_Load(gX_RendererGFX_Model *model, gX_RendererGFX *gfx) {
    gfx_model_mesh_bounds_clear(model);
    //opencode: warn on large model files (>5 MB) that may cause raylib to softlock
    gX_I64 fileSize = localFileSize(model->path);
    if (fileSize > gX_GFX_MODEL_SIZE_WARN) {
        gX_TraceLog_GFX(gX_LOG_WARNING, "Loading large model (%lld MB): %s — raylib may hang for several seconds",
            (long long)(fileSize / (1024 * 1024)), model->path);
    }

    //opencode: for large .obj files, use async threaded parser to avoid blocking the main thread
    gX_I8 *ext = strrchr(model->path, '.');
    gX_I1 isLargeObj = (ext && (strcmp(ext, ".obj") == 0 || strcmp(ext, ".OBJ") == 0) && fileSize > gX_GFX_MODEL_SIZE_WARN);
    if (isLargeObj) {
        gX_Model_AsyncLoadStart(model->path);
        model->asyncLoading = true;
        gX_TraceLog_GFX(gX_LOG_INFO, "Model async parse started: %s (%lld MB)", model->path, (long long)(fileSize / (1024 * 1024)));
        return 0; // model not loaded yet — Update will check async progress
    }

    gX_TraceLog_GFX(gX_LOG_INFO, "Model load: %s (meshes=%d, size=%lld bytes)",
                    model->path, model->model.meshCount, (long long)fileSize);
    gX_F64 loadStart = gX_Time_Get();
    model->model = gX_Model_Load(model->path);
    gX_F64 loadEnd = gX_Time_Get();
    gX_TraceLog_GFX(gX_LOG_INFO, "Model loaded in %.0f ms: %s (meshes=%d vcount=%d)",
                    (loadEnd - loadStart) * 1000.0, model->path,
                    model->model.meshCount,
                    model->model.meshCount > 0 ? model->model.meshes[0].vertexCount : 0);

    //ai: oversized rigs cannot fit one boneMatrices upload. The draw path
    //    falls back to the authored bind pose (see MeshSkinning); surface the
    //    trade-off so a silently static character is not mistaken for a fix.
    if (model->model.skeleton.boneCount > gX_GFX_SKIN_MAX_BONES) {
        gX_TraceLog_GFX(gX_LOG_WARNING,
            "Model rig '%s' has %d bones (cap %d) — rendering bind pose, animation disabled",
            model->path, model->model.skeleton.boneCount,
            gX_GFX_SKIN_MAX_BONES);
    }

    //opencode: guard against failed load — 0 meshes means file was not found or corrupt
    if (model->model.meshCount == 0) {
        gX_TraceLog_GFX(gX_LOG_WARNING, "Model load FAILED: %s (0 meshes) — removing",
            model->path);
        GX_ERROR_SET(GX_ERR_NOT_FOUND, "model load failed: %s (0 meshes)",
            model->path);
        gX_Model_Unload(model->model);
        model->model = (gX_Model){0};
        return -1;
    }



    //opencode: generate normals + tangents for meshes that lack them (e.g., OBJ files)
    // Guard against large meshes — tangent gen is O(n²) and can softlock
    for (gX_U32 mi = 0; mi < (gX_U32)model->model.meshCount; mi++) {
        gX_Mesh *mesh = &model->model.meshes[mi];
        gX_U32 vc = (gX_U32)mesh->vertexCount;
        if (vc > gX_GFX_VERTEX_TANGENT_LIMIT) {
            gX_TraceLog_GFX(gX_LOG_WARNING, "Mesh %u has %u vertices — skipping normal/tangent gen (too large)", mi, vc);
            continue;
        }
        // Generate normals if missing
        //opencode: loader-sentinel detection - raylib fills missing OBJ
        //   normals with uniform (0,1,0), which shades everything as
        //   up-facing (and fools the NULL check below). A degenerate-
        //   constant normal field carries no information; regenerate it
        //   smooth like a missing one. Genuine flat assets keep file
        //   normals (varied) and skip this. Limitation: OBJ smoothing
        //   groups (s records) are not parsed - weld-all matches s=1
        //   single-group exports (the common Blender case); creased
        //   multi-group meshes smooth over creases (documented).
        {
            gX_I1 needNormals = (mesh->normals == NULL) ? 1 : 0;
            if (!needNormals && mesh->vertices != NULL && mesh->vertexCount >= 3) {
                gX_F32 x0 = mesh->normals[0], y0 = mesh->normals[1], z0 = mesh->normals[2];
                needNormals = 1;
                for (int v = 1; v < mesh->vertexCount; v++) {
                    if (mesh->normals[v * 3] != x0
                        || mesh->normals[v * 3 + 1] != y0
                        || mesh->normals[v * 3 + 2] != z0) {
                        needNormals = 0;
                        break;
                    }
                }
            }
            if (needNormals) {
                free(mesh->normals);
                mesh->normals = NULL;
            }
        }
        if (mesh->normals == NULL && mesh->vertices != NULL && mesh->vertexCount >= 3) {
            mesh->normals = (gX_F32*)malloc((gX_U32)mesh->vertexCount * 3 * sizeof(gX_F32));
            memset(mesh->normals, 0, (gX_U32)mesh->vertexCount * 3 * sizeof(gX_F32));
            //opencode: smooth normals — weld by quantized position (hash
            //   buckets, O(n)) and average area-weighted face normals, so
            //   normal-less OBJs (smooth-shaded in Blender, exported
            //   without vn) render smooth instead of faceted. Vertices
            //   sharing a position but differing in UV stay split (seams
            //   preserved). Indexed meshes resolve faces via indices;
            //   soup meshes use sequential triples.
            {
                gX_U32 nverts = (gX_U32)mesh->vertexCount;
                gX_U32 indexCount = (mesh->indices != NULL && mesh->triangleCount > 0)
                    ? (gX_U32)mesh->triangleCount * 3 : nverts;
                gX_I32 *weldHead = (gX_I32*)malloc(4096 * sizeof(gX_I32));
                gX_I32 *weldNext = (gX_I32*)malloc((size_t)nverts * sizeof(gX_I32));
                gX_U32 *weldRep = (gX_U32*)malloc((size_t)nverts * sizeof(gX_U32));
                if (weldHead && weldNext && weldRep) {
                    for (gX_U32 b = 0; b < 4096; b++) weldHead[b] = -1;
                    for (gX_U32 v = 0; v < nverts; v++) {
                        weldNext[v] = -1;
                        weldRep[v] = v;
                        gX_F32 *p = &mesh->vertices[v * 3];
                        gX_I32 qx = (gX_I32)(p[0] * 1000.0f);
                        gX_I32 qy = (gX_I32)(p[1] * 1000.0f);
                        gX_I32 qz = (gX_I32)(p[2] * 1000.0f);
                        gX_U32 h = (gX_U32)((qx * 1973) ^ (qy * 9277) ^ (qz * 26699)) & 4095u;
                        gX_I32 found = -1;
                        for (gX_I32 u = weldHead[h]; u >= 0; u = weldNext[(gX_U32)u]) {
                            gX_F32 *q = &mesh->vertices[(gX_U32)u * 3];
                            if (q[0] == p[0] && q[1] == p[1] && q[2] == p[2]) {
                                gX_I1 uvSame = 1;
                                if (mesh->texcoords != NULL) {
                                    gX_F32 *t0 = &mesh->texcoords[(gX_U32)u * 2];
                                    gX_F32 *t1 = &mesh->texcoords[v * 2];
                                    if (t0[0] != t1[0] || t0[1] != t1[1]) uvSame = 0;
                                }
                                if (uvSame) { found = u; break; }
                            }
                        }
                        if (found >= 0) {
                            weldRep[v] = weldRep[(gX_U32)found];
                        } else {
                            weldNext[v] = weldHead[h];
                            weldHead[h] = (gX_I32)v;
                        }
                    }
                    for (gX_U32 f = 0; f + 2 < indexCount; f += 3) {
                        gX_U32 i0, i1, i2;
                        if (mesh->indices != NULL && mesh->triangleCount > 0) {
                            if (mesh->triangleCount * 3 > 65535) {
                                gX_U32 *idx = (gX_U32*)mesh->indices;
                                i0 = idx[f]; i1 = idx[f + 1]; i2 = idx[f + 2];
                            } else {
                                gX_U16 *idx = (gX_U16*)mesh->indices;
                                i0 = idx[f]; i1 = idx[f + 1]; i2 = idx[f + 2];
                            }
                            if (i0 >= nverts || i1 >= nverts || i2 >= nverts) continue;
                        } else {
                            i0 = f; i1 = f + 1; i2 = f + 2;
                            if (i2 >= nverts) break;
                        }
                        gX_F32 *v0 = &mesh->vertices[i0 * 3];
                        gX_F32 *v1 = &mesh->vertices[i1 * 3];
                        gX_F32 *v2 = &mesh->vertices[i2 * 3];
                        gX_F32 e1[3] = {v1[0]-v0[0], v1[1]-v0[1], v1[2]-v0[2]};
                        gX_F32 e2[3] = {v2[0]-v0[0], v2[1]-v0[1], v2[2]-v0[2]};
                        gX_F32 n[3] = {e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0]};
                        gX_F32 *n0 = &mesh->normals[weldRep[i0] * 3];
                        gX_F32 *n1 = &mesh->normals[weldRep[i1] * 3];
                        gX_F32 *n2 = &mesh->normals[weldRep[i2] * 3];
                        n0[0] += n[0]; n0[1] += n[1]; n0[2] += n[2];
                        n1[0] += n[0]; n1[1] += n[1]; n1[2] += n[2];
                        n2[0] += n[0]; n2[1] += n[1]; n2[2] += n[2];
                    }
                    for (gX_U32 v = 0; v < nverts; v++) {
                        gX_F32 *nv = &mesh->normals[weldRep[v] * 3];
                        gX_F32 len = sqrtf(nv[0]*nv[0]+nv[1]*nv[1]+nv[2]*nv[2]);
                        if (len > 0.0001f) { nv[0]/=len; nv[1]/=len; nv[2]/=len; }
                    }
                    for (gX_U32 v = 0; v < nverts; v++) {
                        if (weldRep[v] != v) {
                            mesh->normals[v * 3] = mesh->normals[weldRep[v] * 3];
                            mesh->normals[v * 3 + 1] = mesh->normals[weldRep[v] * 3 + 1];
                            mesh->normals[v * 3 + 2] = mesh->normals[weldRep[v] * 3 + 2];
                        }
                    }
                    free(weldHead);
                    free(weldNext);
                    free(weldRep);
                }
            }
            // Normalize accumulated normals
            for (int v = 0; v < mesh->vertexCount; v++) {
                gX_F32 *nv = &mesh->normals[v*3];
                gX_F32 len = sqrtf(nv[0]*nv[0]+nv[1]*nv[1]+nv[2]*nv[2]);
                if (len > 0.0001f) { nv[0]/=len; nv[1]/=len; nv[2]/=len; }
            }
        }
        // Generate tangents if UVs exist but tangents don't
        //opencode: defensive guard — raylib's GenTangents reads out of bounds and derefs
        //   uninit memory on meshes with vertexCount 0, 1, 2, or not multiple of 3.
        //   Also requires normals to orthogonalize against. Without these guards,
        //   calling it on a degenerate mesh crashes the loader (see pending.txt #12).
        if (mesh->tangents == NULL
            && mesh->texcoords != NULL
            && mesh->vertices != NULL
            && mesh->normals != NULL
            && mesh->vertexCount >= 3
            && (mesh->vertexCount % 3) == 0) {
            gX_Mesh_GenTangents(mesh);
        }
    }
    Model_InjectTopTint(model);
    model->loaded = true;
    
    //opencode: re-upload normal VBO with CPU data (safety net for GPU read issues)
    for (gX_U32 mi = 0; mi < (gX_U32)model->model.meshCount; mi++) {
        gX_Mesh *mesh = &model->model.meshes[mi];
        if (mesh->normals != NULL && mesh->vboId != NULL && mesh->vboId[2] > 0) {
            gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, mesh->vboId[2]);
            gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, (gX_U32)(mesh->vertexCount * 3 * sizeof(gX_F32)),
                mesh->normals, gX_RHI_STATIC_DRAW);
            gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, 0);
        }
    }
    
    //opencode: compute AABB from mesh vertices for occlusion culling
    {
        model->aabbValid = false;
        gX_F32 bmin[3] = { 1e30f, 1e30f, 1e30f };
        gX_F32 bmax[3] = { -1e30f, -1e30f, -1e30f };
        for (gX_U32 mi = 0; mi < (gX_U32)model->model.meshCount; mi++)
        {
            gX_Mesh *mesh = &model->model.meshes[mi];
            if (mesh->vertices && mesh->vertexCount > 0)
            {
                for (gX_I32 vi = 0; vi < mesh->vertexCount; vi++)
                {
                    gX_F32 vx = mesh->vertices[vi * 3 + 0];
                    gX_F32 vy = mesh->vertices[vi * 3 + 1];
                    gX_F32 vz = mesh->vertices[vi * 3 + 2];
                    if (vx < bmin[0]) bmin[0] = vx;
                    if (vy < bmin[1]) bmin[1] = vy;
                    if (vz < bmin[2]) bmin[2] = vz;
                    if (vx > bmax[0]) bmax[0] = vx;
                    if (vy > bmax[1]) bmax[1] = vy;
                    if (vz > bmax[2]) bmax[2] = vz;
                }
            }
        }
        if (bmin[0] < bmax[0] && bmin[1] < bmax[1] && bmin[2] < bmax[2])
        {
            model->aabbMin[0] = bmin[0]; model->aabbMin[1] = bmin[1]; model->aabbMin[2] = bmin[2];
            model->aabbMax[0] = bmax[0]; model->aabbMax[1] = bmax[1]; model->aabbMax[2] = bmax[2];
            model->aabbValid = true;
        }
    }

    //opencode: auto-detect texture maps from ALL loaded materials.
    //   Iterates every material so multi-material models (common in GLB exports)
    //   don't miss textures on non-zero material indices.
    //   Sets flags both up (any material has the texture → enable) and down
    //   (no material has it → disable) to handle presets that incorrectly set
    //   hasXxxMap=true on textureless models.
    if (model->model.materialCount > 0) {
        // Downward: force all flags to false first (overrides bad presets)
        model->params.hasAlbedoMap = false;
        model->params.hasMetalnessMap = false;
        model->params.hasNormalMap = false;
        model->params.hasHeightMap = false;
        model->params.hasEmissiveMap = false;
        model->params.hasRoughnessMap = false;
        model->params.hasAoMap = false;

        // Upward: enable each flag if ANY material has the texture
        for (gX_I32 mi = 0; mi < (gX_I32)model->model.materialCount; mi++) {
            gX_Material *mat = &model->model.materials[mi];
            if (mat->maps[gX_MATERIAL_MAP_ALBEDO].texture.id > 0)
                model->params.hasAlbedoMap = true;
            if (mat->maps[gX_MATERIAL_MAP_METALNESS].texture.id > 0)
                model->params.hasMetalnessMap = true;
            if (mat->maps[gX_MATERIAL_MAP_NORMAL].texture.id > 0)
                model->params.hasNormalMap = true;
            if (mat->maps[gX_MATERIAL_MAP_HEIGHT].texture.id > 0)
                model->params.hasHeightMap = true;
            if (mat->maps[gX_MATERIAL_MAP_EMISSION].texture.id > 0)
                model->params.hasEmissiveMap = true;
            if (mat->maps[gX_MATERIAL_MAP_ROUGHNESS].texture.id > 0)
                model->params.hasRoughnessMap = true;
            if (mat->maps[gX_MATERIAL_MAP_OCCLUSION].texture.id > 0)
                model->params.hasAoMap = true;
        }
        //opencode: preset texture bindings (textureID/texturePurpose) are map
        //   sources too — without this the refresh clobbered hasAlbedoMap for
        //   injected maps and the shader ignored the bound sampler.
        for (gX_I32 si = 0; si < gX_GFX_MODEL_TEXTURE_SLOTS; si++) {
            gX_I32 purpose = model->texturePurpose[si];
            if (model->textureID[si] < 0 || purpose < 0) continue;
            if (purpose == gX_MATERIAL_MAP_ALBEDO)
                model->params.hasAlbedoMap = true;
            else if (purpose == gX_MATERIAL_MAP_METALNESS)
                model->params.hasMetalnessMap = true;
            else if (purpose == gX_MATERIAL_MAP_NORMAL)
                model->params.hasNormalMap = true;
            else if (purpose == gX_MATERIAL_MAP_HEIGHT)
                model->params.hasHeightMap = true;
            else if (purpose == gX_MATERIAL_MAP_EMISSION) {
                model->params.hasEmissiveMap = true;
                //opencode: preset-injected emission maps need an emissive tint
                //   too — the shader multiplies the map by emissiveColor, whose
                //   default is black (permanently dead emission), so an injected
                //   map would sample but contribute nothing. Fill white on black
                //   with the same precedence as the GLB loader: a scene-set
                //   emissive color survives.
                if (model->params.emissiveColor.r == 0
                    && model->params.emissiveColor.g == 0
                    && model->params.emissiveColor.b == 0) {
                    model->params.emissiveColor = (gX_Color){255, 255, 255, 255};
                    if (model->params.emissiveFactor <= 0.0f)
                        model->params.emissiveFactor = 1.0f;
                }
            }
            else if (purpose == gX_MATERIAL_MAP_ROUGHNESS)
                model->params.hasRoughnessMap = true;
            else if (purpose == gX_MATERIAL_MAP_OCCLUSION)
                model->params.hasAoMap = true;
        }
    }

    //opencode: M8 - emission-mapped bloom probe. Wires orb.png post-load,
    //   AFTER the auto-detect above (which wipes scene-set hasXxxMap when
    //   no texture is present yet; setup-time assignment would also deref
    //   an empty materials array since content loads deferred). Cube gate
    //   (0.5 scale, not the 8.0 floor slab sharing the path) + id>0
    //   re-entry guard make it one-shot.
    if (getenv("GX_T_EMISSIVEMAP")
        && strstr(model->path, "primitive/cube.glb") != NULL
        && model->params.scale.x < 2.0f
        && model->model.materials != NULL && model->model.materialCount > 0
        && model->model.materials[0].maps[gX_MATERIAL_MAP_EMISSION].texture.id == 0) {
        gX_Texture *orb = gX_Resource_LoadTexture("texture/orb.png");
        if (orb && orb->id > 0) {
            model->model.materials[0].maps[gX_MATERIAL_MAP_EMISSION].texture = *orb;
            model->params.hasEmissiveMap = 1;
        }
    }
    //opencode: force-disable texture map flags for raw GL draw path. The GFX
    //   shader uses explicit per-model texture overrides, not raylib's material map
    //   binding. hasXxxMap flags enable texture sampling in phong_diffuse — if the
    //   sampler is unbound or bound to a placeholder texture, it returns black.
    //   NOTE: only hasGlossMap needs the downward reset here — the material-map
    //   flags were already corrected by the auto-detect above (which re-enables
    //   them for GLB/OBJ embedded textures; previously this block wiped them,
    //   leaving every textured model sampling unbound samplers = flat albedo).
    model->params.hasGlossMap = 0;
    //opencode: re-derive file emissive on (re)load; the loader sets it only
    //   when it forwards a file value (scene-set params keep priority).
    model->params.emissiveFromFile = 0;

    //opencode: post-parse GLB for KHR_lights_punctual lights + material names (water detection)
    gX_RendererGFX_GLB_Extract(model->path, gfx, model);

    //opencode: authored animation keys persist in a `<model>.anim.json`
    //   sidecar (same convention as the material override sidecar).
    gX_RendererGFX_Animation_SidecarLoad(model);

    //opencode: material override sidecar + effective-material precompute.
    gX_RendererGFX_Model_MaterialOverridesLoad(model);
    gX_RendererGFX_Model_MaterialOverridesResolve(model);
    gX_RendererGFX_GLB_TerrainMasksLoad(model);
    gX_RendererGFX_Model_MeshEmissiveScan(model);
    
    //opencode: auto-frame camera to show the newly loaded model
    if (model->aabbValid)
        gX_RendererGFX_Model_FrameCamera(gfx, model);
    
    return 0;
}

gX_I32 gX_RendererGFX_Model_Unload(gX_RendererGFX *gfx, gX_RendererGFX_Model *model) {
    gfx_model_mesh_bounds_clear(model);
    for (gX_I32 Index = 0; Index < model->params.materialOverrideCount; Index++) {
        gX_RendererGFX_MaterialOverride *Override = &model->params.materialOverrides[Index];
        if (Override->terrainAlbedoTexture.id) gX_Texture_Unload(Override->terrainAlbedoTexture);
        if (Override->terrainDetailTexture.id) gX_Texture_Unload(Override->terrainDetailTexture);
        Override->terrainAlbedoTexture = (gX_Texture){0};
        Override->terrainDetailTexture = (gX_Texture){0};
        Override->terrainAlbedoMapID = 0;
        Override->terrainDetailMapID = 0;
    }
    if (model->ownsMesh) {
        //opencode: generated-mesh model — unload the mesh + material memory
        //   directly (gX_Model_Unload on a hand-built model would leak; the
        //   GLB path owns separate memory).
        for (gX_I32 mi = 0; mi < model->model.meshCount; mi++) {
            gX_Mesh_Unload(model->model.meshes[mi]);
        }
        free(model->model.meshes);
        if (model->model.materials) {
            free(model->model.materials);
        }
        free(model->model.meshMaterial);
        model->model = (gX_Model){0};
    } else {
        gX_Model_Unload(model->model);
    }
    model->loaded = false;
    if (model->textFontLoaded) {
        gX_Font_Unload(model->textFont);
        model->textFont = (gX_Font){0};
        model->textFontLoaded = 0;
        model->textFontPathLoaded[0] = '\0';
    }
    if (model->occlusionQueryId > 0) {
        gX_RHI_Query_Delete(model->occlusionQueryId);
        model->occlusionQueryId = 0;
    }
    model->queryIssued = 0;
    model->queryActive = 0;
    model->lastVisible = 1;
    model->frustumVisible = 1;
    model->occlusionVisible = 1;
    //opencode: free GLB post-parse data + remove GLB-created lights (heap + shadow
    //   FBOs) — previously lights leaked on model removal/reload
    if (model->glbData) {
        for (gX_I32 li = 0; li < model->glbData->lightCount && li < 8; li++) {
            gX_I32 idx = model->glbData->lightIndices[li];
            if (idx >= 0) gX_RendererGFX_Light_Remove(gfx, (gX_U32)idx);
        }
        gX_RendererGFX_GLB_Free(model->glbData);
        model->glbData = 0;
    }
    return 0;
}

//opencode: waypoint helpers for the model polyline path.
static gX_I0 gfx_model_waypoint_path_update(
    gX_RendererGFX_Model_Parameters *params, gX_F64 deltaTime) {
    gX_MotionPath_Waypoint_Update(params->pathWaypoints, params->pathTangents,
        params->pathTangentsIn, params->pathHandlesIndependent,
        &params->pathWaypointCount, gX_GFX_MODEL_WAYPOINTS, params->orbitLoop,
        params->pathPingPong, params->pathSmooth, params->pathBezier,
        params->orbitSpeed, deltaTime, &params->orbitElapsed,
        &params->orbitPaused, &params->pos);
}

//opencode: evaluate a model's motion path in world space with a bounded frame
//   delta so window stalls do not teleport the model around it. Circle and
//   waypoint modes share the authored playback state; the path is
//   preset-persisted.
gX_I0 gX_RendererGFX_Model_Orbit_Update(gX_RendererGFX_Model *model,
    gX_F64 deltaTime) {
    if (!model || !model->params.orbitEnabled) return;
    gX_RendererGFX_Model_Parameters *params = &model->params;
    if (params->pathMode == 1 && params->pathWaypointCount >= 2) {
        gfx_model_waypoint_path_update(params, deltaTime);
    } else {
        if (!isfinite(params->orbitElapsed) || params->orbitElapsed < 0.0)
            params->orbitElapsed = 0.0;
        if (!params->orbitPaused && isfinite(deltaTime) && deltaTime > 0.0) {
            if (deltaTime > 0.25) deltaTime = 0.25;
            params->orbitElapsed += deltaTime;
        }
        gX_F64 speed = isfinite(params->orbitSpeed) ? params->orbitSpeed : 0.0;
        if (!params->orbitLoop && !params->orbitPaused && fabs(speed) > 0.000001) {
            gX_F64 duration = 360.0 / fabs(speed);
            if (params->orbitElapsed >= duration) {
                params->orbitElapsed = duration;
                params->orbitPaused = 1;
            }
        }
        gX_F64 phase = isfinite(params->orbitPhase) ? params->orbitPhase : 0.0;
        gX_F64 angle = fmod(phase + params->orbitElapsed * speed, 360.0);
        if (angle < 0.0) angle += 360.0;
        gX_F32 radians = (gX_F32)angle * gX_DEG2RAD;
        gX_F32 radius = isfinite(params->orbitRadius) && params->orbitRadius > 0.0f
            ? params->orbitRadius : 0.0f;
        gX_F32 x = cosf(radians) * radius;
        gX_F32 y = sinf(radians) * radius;
        switch (params->orbitPlane) {
            case 0:
                params->pos = (gX_F32v3){params->orbitCenter.x + x,
                    params->orbitCenter.y + y, params->orbitCenter.z};
                break;
            case 2:
                params->pos = (gX_F32v3){params->orbitCenter.x,
                    params->orbitCenter.y + x, params->orbitCenter.z + y};
                break;
            default:
                params->pos = (gX_F32v3){params->orbitCenter.x + x,
                    params->orbitCenter.y, params->orbitCenter.z + y};
                break;
        }
    }
    //opencode: cache the trace hook — this runs per orbiting model per frame.
    static gX_I1 s_motionTraceChecked = 0;
    static gX_I1 s_motionTrace = 0;
    if (!s_motionTraceChecked) {
        const gX_I8 *trace = getenv("GX_MOTION_TRACE");
        s_motionTrace = (trace && trace[0] && trace[0] != '0') ? 1 : 0;
        s_motionTraceChecked = 1;
    }
    if (s_motionTrace) {
        gX_TraceLog(gX_LOG_INFO,
            "GFX_MOTION: kind=model pos=(%.3f,%.3f,%.3f) elapsed=%.3f paused=%d",
            params->pos.x, params->pos.y, params->pos.z,
            params->orbitElapsed, params->orbitPaused);
    }
}

gX_I0 gX_RendererGFX_Model_Update(gX_RendererGFX_Model *model, gX_RendererGFX *gfx) {
    if (model->loadAsap && maindata.backendLoaded && !(model->loaded)) {
        //opencode: check if async OBJ load has completed
        if (model->asyncLoading) {
            if (gX_Model_AsyncLoadIsDone(model->path)) {
                model->model = gX_Model_AsyncLoadFinish(model->path);
                model->asyncLoading = false;
                if (model->model.meshCount > 0) {
    model->loaded = true;
                    gX_TraceLog_GFX(gX_LOG_INFO, "Model async load completed: %s (%d meshes)", model->path, model->model.meshCount);
                    //opencode: async path skipped AABB + texture auto-detect —
                    //   without them the model gets no frustum/occlusion culling
                    //   and textured OBJs render flat albedo
                    {
                        model->aabbValid = false;
                        gX_F32 bmin[3] = { 1e30f, 1e30f, 1e30f };
                        gX_F32 bmax[3] = { -1e30f, -1e30f, -1e30f };
                        for (gX_U32 mi = 0; mi < (gX_U32)model->model.meshCount; mi++) {
                            gX_Mesh *mesh = &model->model.meshes[mi];
                            if (mesh->vertices && mesh->vertexCount > 0) {
                                for (gX_I32 vi = 0; vi < mesh->vertexCount; vi++) {
                                    gX_F32 vx = mesh->vertices[vi*3+0];
                                    gX_F32 vy = mesh->vertices[vi*3+1];
                                    gX_F32 vz = mesh->vertices[vi*3+2];
                                    if (vx < bmin[0]) bmin[0] = vx;
                                    if (vy < bmin[1]) bmin[1] = vy;
                                    if (vz < bmin[2]) bmin[2] = vz;
                                    if (vx > bmax[0]) bmax[0] = vx;
                                    if (vy > bmax[1]) bmax[1] = vy;
                                    if (vz > bmax[2]) bmax[2] = vz;
                                }
                            }
                        }
                        if (bmin[0] < bmax[0] && bmin[1] < bmax[1] && bmin[2] < bmax[2]) {
                            model->aabbMin[0] = bmin[0]; model->aabbMin[1] = bmin[1]; model->aabbMin[2] = bmin[2];
                            model->aabbMax[0] = bmax[0]; model->aabbMax[1] = bmax[1]; model->aabbMax[2] = bmax[2];
                            model->aabbValid = true;
                        }
                    }
                    if (model->model.materialCount > 0) {
                        model->params.hasAlbedoMap = false;
                        model->params.hasMetalnessMap = false;
                        model->params.hasNormalMap = false;
                        model->params.hasHeightMap = false;
                        model->params.hasEmissiveMap = false;
                        model->params.hasRoughnessMap = false;
                        model->params.hasAoMap = false;
                        for (gX_I32 mi = 0; mi < (gX_I32)model->model.materialCount; mi++) {
                            gX_Material *mat = &model->model.materials[mi];
                            if (mat->maps[gX_MATERIAL_MAP_ALBEDO].texture.id > 0) model->params.hasAlbedoMap = true;
                            if (mat->maps[gX_MATERIAL_MAP_METALNESS].texture.id > 0) model->params.hasMetalnessMap = true;
                            if (mat->maps[gX_MATERIAL_MAP_NORMAL].texture.id > 0) model->params.hasNormalMap = true;
                            if (mat->maps[gX_MATERIAL_MAP_HEIGHT].texture.id > 0) model->params.hasHeightMap = true;
                            if (mat->maps[gX_MATERIAL_MAP_EMISSION].texture.id > 0) model->params.hasEmissiveMap = true;
                            if (mat->maps[gX_MATERIAL_MAP_ROUGHNESS].texture.id > 0) model->params.hasRoughnessMap = true;
                            if (mat->maps[gX_MATERIAL_MAP_OCCLUSION].texture.id > 0) model->params.hasAoMap = true;
                        }
                        model->params.hasGlossMap = false;
                    }
                }
            }
            // Still loading — skip this frame
            return;
        }
        //opencode: try to load — if it fails, free the slot (mark as unused)
        if (gX_RendererGFX_Model_Load(model, gfx) != 0) {
            gX_TraceLog_GFX(gX_LOG_WARNING, "Model load FAILED: %s — freeing slot", model->path);
            model->loadAsap = false;
            //opencode: zero out model struct to avoid crashes in draw/update
            gX_Model zero = {0};
            model->model = zero;
            model->loaded = false;
        }
    }
}

//opencode: fill per-model material UBO from params, then upload once per model draw.
//   Replaces ~35 individual glUniform calls with a single glBufferSubData.
//   Samplers (texture slots), material colors, and conditional uniforms (cubePos,
//   reflMVP, bgTex) stay as individual calls because they can't live in a UBO.
//opencode: per-model albedo color — file baseColor × scalar × diffuse tint.
//   Single choke point for the main draw and the planar raw fill (which
//   otherwise inherits stale main-pass values). White tint = identity.
//   GX_TINT=0 forces white (kill hook for tint-source A/Bs); read once.
gX_Color gX_RendererGFX_AlbedoTint_Apply(gX_Color base, gX_F32 albedo, gX_Color tint) {
    static gX_I1 checked = 0;
    static gX_I1 enabled = 1;
    gX_F32 albedoScale;
    gX_F32 r;
    gX_F32 g;
    gX_F32 b;
    gX_Color out;
    if (!checked) {
        checked = 1;
        {
            const gX_I8 *hook = getenv("GX_TINT");
            if (hook && hook[0] == '0') enabled = 0;
        }
    }
    if (!enabled) tint = (gX_Color){255, 255, 255, 255};
    albedoScale = (albedo > 0.01f) ? albedo : 1.0f;
    r = (gX_F32)base.r * albedoScale * (gX_F32)tint.r / 255.0f;
    g = (gX_F32)base.g * albedoScale * (gX_F32)tint.g / 255.0f;
    b = (gX_F32)base.b * albedoScale * (gX_F32)tint.b / 255.0f;
    out.r = (gX_U8)((r > 255.0f) ? 255.0f : r);
    out.g = (gX_U8)((g > 255.0f) ? 255.0f : g);
    out.b = (gX_U8)((b > 255.0f) ? 255.0f : b);
    out.a = base.a;
    return out;
}
//opencode: world matrix from params (scale → rotate → translate), shared by
//   the draw path, frustum culling, and the skeleton debug overlay.
gX_Matrix gX_RendererGFX_Model_WorldMatrix(const gX_RendererGFX_Model *model) {
    gX_Matrix s_mat = gX_Matrix_Scale(model->params.scale.x,
        model->params.scale.y, model->params.scale.z);
    gX_Matrix r_mat = gX_Matrix_RotateXYZ((gX_F32v3){
        model->params.rotation.x * gX_DEG2RAD,
        model->params.rotation.y * gX_DEG2RAD,
        model->params.rotation.z * gX_DEG2RAD
    });
    gX_Matrix t_mat = gX_Matrix_Translate(model->params.pos.x,
        model->params.pos.y, model->params.pos.z);
    gX_Matrix trs = gX_Matrix_Multiply(s_mat, r_mat);
    return gX_Matrix_Multiply(trs, t_mat);
}

//opencode: per-mesh skinning switch — the raw draw path binds the bone
//   attributes for meshes that carry skin data, so the switch must be 0 for
//   unskinned meshes of an otherwise skinned model.
static gX_I0 gX_RendererGFX_Model_MeshSkinning(gX_RendererGFX_Model *model,
    gX_I32 useSkinningLoc, gX_I32 meshIndex) {
    if (useSkinningLoc < 0) return;
    gX_I1 skin = 0;
    if (model->model.meshes && meshIndex >= 0
        && meshIndex < model->model.meshCount) {
        gX_Mesh *mesh = &model->model.meshes[meshIndex];
        skin = (mesh->boneIndices != NULL && mesh->boneWeights != NULL) ? 1 : 0;
    }
    //ai: rigs above the shader's per-draw upload cap cannot be skinned
    //    correctly (raylib stores joint indices as u8 and the uniform array is
    //    boneMatrices[128]). glTF vertices are authored in bind pose, so
    //    disabling the shader path renders the model intact instead of
    //    collapsing vertices whose joints fall outside the uploaded prefix.
    if (model->model.skeleton.boneCount > gX_GFX_SKIN_MAX_BONES) skin = 0;
    gX_RHI_UniformInt(useSkinningLoc, skin);
}

//opencode: depth-program gX_UseSkinning location (the depth pass runs a
//   different program, so the standard shader's loc index must not be reused).
static gX_I32 g_depthSkinningSwitchLoc = -1;

//ai: opt-in trace uniforms target the probe regression's one known edge pixel.
#if defined(GX_PROBE_COVERAGE_EXPERIMENT)
static gX_I0 gfx_probe_proxy_upload(gX_Shader shader, gX_RendererGFX *gfx)
{
    const gX_I8 *fixture = getenv("GX_PROBE_PROXY_FIXTURE");
    const gX_I8 *hook = getenv("GX_PROBE_PROXY_DIAGNOSTIC");
    gX_I32 mode = 0;
    if (fixture && strcmp((const char *)fixture, "1") == 0
        && hook && hook[0] >= '1' && hook[0] <= '9' && hook[1] == '\0')
        mode = (gX_I32)(hook[0] - '0');
    else if (fixture && strcmp((const char *)fixture, "1") == 0
        && hook && hook[0] == '1' && hook[1] >= '0' && hook[1] <= '4' && hook[2] == '\0')
        mode = 10 + (gX_I32)(hook[1] - '0');
    const gX_I8 *split = getenv("GX_PROBE_PROXY_SPLIT");
    const gX_I8 *coverage = getenv("GX_PROBE_COVERAGE");
    gX_I1 skyReady = mode > 0 && split && strcmp((const char *)split, "1") == 0
        && coverage && strcmp((const char *)coverage, "2") == 0
        && !gfx->state.ENABLE_SSR && gfx->state.ENABLE_PROCEDURAL_SKY
        && gfx->proceduralSky.shader.id > 0 && gfx->ssr.skyLut.wrapper.loadingState.loaded
        && gfx->ssr.skyLut.wrapper.rt.id > 0 && gfx->ssr.skyLut.wrapper.rt.texture.id > 0;
    gX_RHI_Shader_Use(shader.id);
    gX_RHI_UniformInt(gX_RHI_GetUniformLocation(shader.id,
        (const gX_I8 *)"gXProbeProxyDiagnostic"), mode);
    gX_RHI_UniformInt(gX_RHI_GetUniformLocation(shader.id,
        (const gX_I8 *)"gXProbeProxySkyReady"), skyReady ? 1 : 0);
    if (skyReady)
    {
        gX_RHI_Tex_BindOnUnit(gfx->ssr.skyLut.wrapper.rt.texture.id, gX_GFX_TEXUNIT_SKY_LUT);
        gX_RHI_UniformInt(gX_RHI_GetUniformLocation(shader.id,
            (const gX_I8 *)"gXProbeProxySky"), gX_GFX_TEXUNIT_SKY_LUT);
    }
}
#endif

//ai: trace activation remains independent of the compile-isolated proxy fixture.
static gX_I0 gfx_probe_trace_upload(gX_Shader shader)
{
    const gX_I8 *hook = getenv("GX_PROBE_TRACE_MODE");
    const gX_I8 *correctHook = getenv("GX_PROBE_BOX_CORRECT");
    gX_I1 correctOn = correctHook && strcmp((const char *)correctHook, "1") == 0;
    gX_I32 mode = 0;
    if (hook && hook[0] >= '1' && hook[0] <= '9' && hook[1] == '\0')
        mode = (gX_I32)(hook[0] - '0');
    else if (hook && hook[0] == '1' && hook[1] >= '0'
        && hook[1] <= '5' && hook[2] == '\0')
        mode = 10 + (gX_I32)(hook[1] - '0');
    if (mode == 0 && !correctOn) return;
    if (maindata.renderWidth <= 609 || maindata.renderHeight <= 142) mode = 0;
    gX_I32 modeLoc = gX_RHI_GetUniformLocation(shader.id,
        (const gX_I8 *)"gXProbeTraceMode");
    gX_I32 pixelLoc = gX_RHI_GetUniformLocation(shader.id,
        (const gX_I8 *)"gXProbeTracePixel");
    gX_I32 correctLoc = gX_RHI_GetUniformLocation(shader.id,
        (const gX_I8 *)"gXProbeBoxCorrect");
    if (modeLoc < 0 || pixelLoc < 0) return;
    gX_F32 pixel[4] = {
        609.0f,
        mode > 0 ? (gX_F32)(maindata.renderHeight - 1u - 142u) : -1.0f,
        0.0f,
        0.0f
    };
    gX_RHI_Shader_Use(shader.id);
    gX_RHI_UniformInt(correctLoc, correctOn ? 1 : 0);
    gX_RHI_UniformInt(modeLoc, mode);
    gX_RHI_UniformVec4(pixelLoc, 1, pixel);
}

//ai: candidate sky classification follows texture ownership, not the receiver's plane.
static gX_I0 gfx_probe_sky_upload(gX_Shader shader, gX_RendererGFX *gfx,
    gX_U32 primaryTexture, gX_U32 secondaryTexture)
{
    const gX_I8 *hook = getenv("GX_PROBE_BOX_CORRECT");
    if (!hook || strcmp((const char *)hook, "1") != 0) return;
    gX_I1 primarySkyOnly = gfx->reflection.dynamicSkyOnly;
    gX_I1 secondarySkyOnly = 0;
    for (gX_U32 index = 0; index < gfx->reflection.probeCount; index++)
    {
        gX_RendererGFX_Probe *probe = &gfx->reflection.probes[index];
        if (!probe->baked || probe->cube.id == 0) continue;
        if (probe->cube.id == primaryTexture) primarySkyOnly = probe->skyOnly;
        if (probe->cube.id == secondaryTexture) secondarySkyOnly = probe->skyOnly;
    }
    gX_RHI_Shader_Use(shader.id);
    gX_RHI_UniformInt(gX_RHI_GetUniformLocation(shader.id,
        (const gX_I8 *)"gXProbeSkyOnlyA"), primarySkyOnly ? 1 : 0);
    gX_RHI_UniformInt(gX_RHI_GetUniformLocation(shader.id,
        (const gX_I8 *)"gXProbeSkyOnlyB"), secondarySkyOnly ? 1 : 0);
}

//ai: opt-in capture hook pins reflection consumers to one baked probe without affecting default selection.
static gX_RendererGFX_Probe *gfx_probe_debug_select(gX_RendererGFX *gfx)
{
    const gX_I8 *hook = getenv("GX_PROBE_DEBUG_INDEX");
    if (!hook || hook[0] < '0' || hook[0] > '3' || hook[1] != '\0') return 0;
    gX_U32 index = (gX_U32)(hook[0] - '0');
    if (!gfx->reflection.probesReady || index >= gfx->reflection.probeCount) return 0;
    gX_RendererGFX_Probe *probe = &gfx->reflection.probes[index];
    if (!probe->baked || probe->cube.id == 0) return 0;
    return probe;
}

//ai: normalize native material indices before reading per-mesh file opacity.
//ai: blended-loop upload scratch is draw-local; first opacity always uploads and repair flag stays phase-constant.
//ai: optional conservative file-mesh culling replaces unsafe model-corner rejection; non-main/unsupported geometry stays visible.
gX_I0 gX_RendererGFX_Model_Draw(gX_RendererGFX_Model *model, gX_RendererGFX *gfx, gX_I1 depthMode) {
    //opencode: per-draw detail sits at TRACE2 — `trace` keeps the coarse
    //   flow, TRACE2 adds the per-model granularity
    gX_TraceLog(gX_LOG_TRACE2, "DRAW: model '%s' meshes=%d depth=%d",
        (const char *)model->path, (gX_I32)model->model.meshCount,
        (gX_I32)depthMode);
    if (model->loaded) {
        gX_I1 lightmapEnabled = model->params.hasLightmap;
        if (lightmapEnabled) {
            for (gX_I32 lmi = 0; lmi < model->model.meshCount; lmi++) {
                if (model->model.meshes[lmi].texcoords2 == NULL) {
                    lightmapEnabled = 0;
                    break;
                }
            }
        }
        if (!depthMode && (gfx->shader.shader.id == 0 || !gfx->shader.loaded)) return;
        if (depthMode && (gfx->depth.shader.id == 0 || !gfx->depth.loaded)) return;
        //ai: authored double-sided models keep their backfaces in color/depth.
        gX_I1 cullWas = gX_RHI_IsEnabled(gX_RHI_CULL_FACE);
        gX_I32 cullMode = 0;
        gX_RHI_GetIntegerv(gX_RHI_CULL_FACE_MODE, &cullMode);
        if (model->params.doubleSided) gX_RHI_Disable(gX_RHI_CULL_FACE);
        gX_Model m = model->model;
        //opencode: guard against NULL meshMaterial (raylib may not populate it
        //   for all model types, e.g., certain GLB exports). The instanced
        //   path (line 905) already has this guard; the single-model path needs it too.
        gX_Material *meshmaterial = NULL;
        gX_Material defaultMat = {0};
        static gX_MaterialMap defaultMaps[12] = {0};
        defaultMat.maps = defaultMaps;
        defaultMat.maps[gX_MATERIAL_MAP_ALBEDO].color = (gX_Color){200, 200, 200, 255};
        if (m.meshMaterial != NULL && m.materialCount > 0) {
            gX_U32 matIdx = (gX_U32)m.meshMaterial[0];
            if (matIdx < (gX_U32)m.materialCount)
                meshmaterial = &m.materials[matIdx];
        }
        if (meshmaterial == NULL) {
            meshmaterial = &defaultMat;
            if (!depthMode) meshmaterial->shader = gfx->shader.shader;
            else meshmaterial->shader = gfx->depth.shader;
            gX_TraceLog_GFX(gX_LOG_INFO, "TRACE_MODEL: '%s' using defaultMat (materialCount=%d)",
                model->path, m.materialCount);
        }
        gX_U32 injectedMapIDs[gX_MATERIAL_MAP_HEIGHT + 1] = {0};
        gX_RendererGFX_Shader *rshader = &gfx->shader;
        gX_Shader shaderProg = rshader->shader;
        //opencode: injected material-purpose maps must enable their shader
        //   feature before the per-model uniform upload below.
        if (model->params.injectedNormalMapID >= 0)
            model->params.hasNormalMap = 1;
        for (gX_I32 mapSlot = 0; mapSlot < gX_GFX_MODEL_TEXTURE_SLOTS; mapSlot++) {
            if (model->textureID[mapSlot] < 0) continue;
            if (model->texturePurpose[mapSlot] == gX_MATERIAL_MAP_NORMAL)
                model->params.hasNormalMap = 1;
            if (model->texturePurpose[mapSlot] == gX_MATERIAL_MAP_HEIGHT)
                model->params.hasHeightMap = 1;
        }
        gX_I1 oracleGpuOn = gX_RendererGFX_OracleGPU_Enabled();
        gX_I1 oracleMatteOn = getenv("GX_T_ORACLE_MATTE")
            && getenv("GX_T_ORACLE_MATTE")[0] != '0';

        //opencode: opt-in SSR normal pass reuses the production vertex path,
        //   so its interpolated model normals match the visible geometry.
        if (!depthMode && rshader->locations.ssrNormalPassLoc >= 0)
            gX_Shader_SetValue_Int(shaderProg, rshader->locations.ssrNormalPassLoc,
                gfx->ssr.normalPass ? 1 : 0);

        //opencode: Paper 1 M2 matte fixture — upload the specular gate at the
        //   draw boundary as well as the frame-global update. Reflection and
        //   cubemap passes can change the current program between those two
        //   points; the opt-in fixture must remain Lambert-only on every draw.
        if (oracleMatteOn || oracleGpuOn)
            gX_Shader_SetValue_Int(shaderProg, rshader->locations.enableLightingSpecularLoc, 0);
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.oracleDiffuseOnlyLoc,
            (oracleMatteOn || oracleGpuOn) ? 1 : 0);
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.oracleGgxLoc,
            (getenv("GX_T_ORACLE_GGX") && getenv("GX_T_ORACLE_GGX")[0] != '0') ? 1 : 0);
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.oracleBounceLoc,
            (getenv("GX_T_ORACLE_BOUNCE") && getenv("GX_T_ORACLE_BOUNCE")[0] != '0'
            && !oracleGpuOn) ? 1 : 0);
        //ai: invert the shaded normal only; winding/culling stay authored.
        if (!depthMode && rshader->locations.flipNormalsLoc >= 0)
            gX_Shader_SetValue_Int(shaderProg,
                rshader->locations.flipNormalsLoc,
                model->params.flipNormals ? 1 : 0);
        
        //opencode: per-model uniforms via individual glUniform* calls (the
        //   UBO branch was deleted: glGetUniformBlockIndex always returns
        //   INVALID_INDEX, so it never executed)
        gX_I1 blendingThisDraw = model->params.enableBlending
            && model->params.blendOpacity < 0.999f;
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.hasNormalMapLoc, model->params.hasNormalMap);
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.normalMapFactorLoc, model->params.normalMapFactor);
        gX_Shader_SetValue_V2(shaderProg, rshader->locations.normalMapScaleLoc, model->params.normalMapScale);
        gX_Shader_SetValue_V2(shaderProg, rshader->locations.normalMapOffsetLoc, model->params.normalMapOffset);
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.hasHeightMapLoc, model->params.hasHeightMap);
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.heightMapFactorLoc, model->params.heightMapFactor);
        gX_Shader_SetValue_V2(shaderProg, rshader->locations.heightMapScaleLoc, model->params.heightMapScale);
        gX_Shader_SetValue_V2(shaderProg, rshader->locations.heightMapOffsetLoc, model->params.heightMapOffset);
        if (rshader->locations.parallaxModeLoc >= 0)
            gX_Shader_SetValue_Int(shaderProg, rshader->locations.parallaxModeLoc, model->params.parallaxMode);
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.hasAlbedoMapLoc, model->params.hasAlbedoMap);
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.hasMetalnessMapLoc, model->params.hasMetalnessMap);
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.albedoFactorLoc, model->params.albedo);
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.metalnessFactorLoc, model->params.metalness);
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.modelShininessLoc, model->params.shininess);
        if (rshader->locations.usePBRLoc >= 0)
            gX_Shader_SetValue_Int(shaderProg, rshader->locations.usePBRLoc, model->params.usePBR);
        gX_Shader_SetValue_V2(shaderProg, rshader->locations.albedoMapScaleLoc, model->params.albedoMapScale);
        gX_Shader_SetValue_V2(shaderProg, rshader->locations.albedoMapOffsetLoc, model->params.albedoMapOffset);
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.albedoMapFactorLoc, model->params.albedoMapFactor);
        gX_Shader_SetValue_V2(shaderProg, rshader->locations.metalnessMapScaleLoc, model->params.metalnessMapScale);
        gX_Shader_SetValue_V2(shaderProg, rshader->locations.metalnessMapOffsetLoc, model->params.metalnessMapOffset);
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.metalnessMapFactorLoc, model->params.metalnessMapFactor);
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.hasEmissiveMapLoc, model->params.hasEmissiveMap);
        //opencode: oracle (Paper 1) - per-frame PCF seed. Deterministic
        //   frame counter (NOT wall time): hermetic repeats stay bit-exact.
        //   Hook-gated (GX_T_ORACLESEED or GX_T_ORACLEPATH); default off =
        //   unrotated taps.
        {
            const gX_I8 *seedHook = getenv("GX_T_ORACLESEED");
            const gX_I8 *pathHook = getenv("GX_T_ORACLEPATH");
            const gX_I8 *bounceHook = getenv("GX_T_ORACLE_BOUNCE");
            gX_I1 bounceOn = (bounceHook && bounceHook[0] != '0'
                && !oracleGpuOn) ? 1 : 0;
            gX_I1 seedOn = ((seedHook && seedHook[0] != '0')
                || (pathHook && pathHook[0] != '0')) ? 1 : 0;
            if (rshader->locations.oracleSeedLoc >= 0)
                gX_Shader_SetValue_Float(shaderProg, rshader->locations.oracleSeedLoc,
                    bounceOn ? 0.0f : (gX_F32)maindata.frameCount);
            if (rshader->locations.oracleSeedActiveLoc >= 0)
                gX_Shader_SetValue_Int(shaderProg, rshader->locations.oracleSeedActiveLoc, seedOn);
            //opencode: oracle estimator (Paper 1) - single-light MC pick in
            //   standard.frag (sun stays exact). Hook GX_T_ORACLEEST. The
            //   M1 path hook also enables this direct-light estimator so one
            //   oracle capture exercises both transport terms.
            {
                const gX_I8 *estHook = getenv("GX_T_ORACLEEST");
                const gX_I8 *pathHook = getenv("GX_T_ORACLEPATH");
                gX_I1 estOn = ((estHook && estHook[0] != '0')
                    || (pathHook && pathHook[0] != '0')) ? 1 : 0;
                if (rshader->locations.oracleEstimateActiveLoc >= 0)
                    gX_Shader_SetValue_Int(shaderProg, rshader->locations.oracleEstimateActiveLoc, estOn);
            }
            //opencode: oracle multi-ray (Paper 1) - rays per pixel 1..4
            //   (GX_T_ORACLERAYS). Default 1 = original single-ray output.
            {
                const gX_I8 *rayHook = getenv("GX_T_ORACLERAYS");
                gX_I32 rays = rayHook ? atoi((const char *)rayHook) : 1;
                if (rays < 1) rays = 1;
                if (rays > 4) rays = 4;
                if (rshader->locations.oracleRayCountLoc >= 0)
                    gX_Shader_SetValue_Int(shaderProg, rshader->locations.oracleRayCountLoc, rays);
            }
            //opencode: Paper 1 M1 - cosine-hemisphere environment transport
            //   replaces only the diffuse IBL integral; default is unchanged.
            {
                const gX_I8 *pathHook = getenv("GX_T_ORACLEPATH");
                gX_I1 pathOn = (pathHook && pathHook[0] != '0') ? 1 : 0;
                if (rshader->locations.oraclePathActiveLoc >= 0)
                    gX_Shader_SetValue_Int(shaderProg, rshader->locations.oraclePathActiveLoc, pathOn);
            }
        }
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.emissiveFactorLoc, model->params.emissiveFactor);
        if (rshader->locations.emissiveColorLoc >= 0)
            gX_Shader_SetValue_Color(shaderProg, rshader->locations.emissiveColorLoc, model->params.emissiveColor);
        gX_Shader_SetValue_V2(shaderProg, rshader->locations.emissiveMapScaleLoc, model->params.emissiveMapScale);
        gX_Shader_SetValue_V2(shaderProg, rshader->locations.emissiveMapOffsetLoc, model->params.emissiveMapOffset);
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.emissiveMapFactorLoc, model->params.emissiveMapFactor);
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.hasRoughnessMapLoc, model->params.hasRoughnessMap);
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.roughnessLoc, model->params.roughness);
        gX_Shader_SetValue_V2(shaderProg, rshader->locations.roughnessMapScaleLoc, model->params.roughnessMapScale);
        gX_Shader_SetValue_V2(shaderProg, rshader->locations.roughnessMapOffsetLoc, model->params.roughnessMapOffset);
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.roughnessMapFactorLoc, model->params.roughnessMapFactor);
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.hasAoMapLoc, model->params.hasAoMap);
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.aoLoc, model->params.ao);
        gX_Shader_SetValue_V2(shaderProg, rshader->locations.aoMapScaleLoc, model->params.aoMapScale);
        gX_Shader_SetValue_V2(shaderProg, rshader->locations.aoMapOffsetLoc, model->params.aoMapOffset);
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.aoMapFactorLoc, model->params.aoMapFactor);
        //opencode: lightmap controls are per-model; the texture is bound after
        //   the draw shader is attached below through the raw RHI unit path.
        if (rshader->locations.hasLightmapLoc >= 0)
            gX_Shader_SetValue_Int(shaderProg, rshader->locations.hasLightmapLoc,
                (!depthMode && lightmapEnabled) ? 1 : 0);
        if (rshader->locations.lightmapStrengthLoc >= 0)
            gX_Shader_SetValue_Float(shaderProg, rshader->locations.lightmapStrengthLoc,
                model->params.lightmapStrength);
        if (rshader->locations.lightmapScaleLoc >= 0)
            gX_Shader_SetValue_V2(shaderProg, rshader->locations.lightmapScaleLoc,
                model->params.lightmapScale);
        if (rshader->locations.lightmapOffsetLoc >= 0)
            gX_Shader_SetValue_V2(shaderProg, rshader->locations.lightmapOffsetLoc,
                model->params.lightmapOffset);
        //opencode: gloss map flag
        if (rshader->locations.hasGlossMapLoc >= 0)
            gX_Shader_SetValue_Int(shaderProg, rshader->locations.hasGlossMapLoc, model->params.hasGlossMap);
        //opencode: per-model specular color (overrides global white default)
        if (rshader->locations.specularColorLoc >= 0)
            gX_Shader_SetValue_V3(shaderProg, rshader->locations.specularColorLoc, model->params.specularColor);
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.enableReflectionsLoc,
            model->params.enableReflections && gfx->state.ENABLE_REFLECTIONS);
        //opencode: SSR reception is separate from direct environment shading.
        if (rshader->locations.receiveSSRLoc >= 0)
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.receiveSSRLoc,
                model->params.receiveSSR);
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.enableAlphaTestLoc, model->params.enableAlphaTest);
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.alphaCutoffLoc, model->params.alphaCutoff);
        //ai: upload on every model, including OFF, so the opt-in cannot leak across receivers.
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.balancedReflectionLoc,
            model->params.balancedReflection);
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.enableRefractionLoc,
            model->params.enableRefraction && gfx->debugVis.refractionReady);
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.refractiveIndexLoc, model->params.refractiveIndex);
        gX_I1 planarActive = gfx && model->params.planarReflection
            && gfx->state.ENABLE_PLANAR_REFLECTIONS
            && gfx->reflection.filledThisFrame
            && gfx->reflection.planar.wrapper.loadingState.loaded;
        gX_F32v3 worldReflectionNormal = model->params.reflectionNormal;
        gX_F32 worldReflectionDistance = model->params.reflectionPlaneHeight;
        if (!gfx_reflection_plane_world(model,
                &worldReflectionNormal, &worldReflectionDistance)) {
            worldReflectionNormal = (gX_F32v3){0.0f, 1.0f, 0.0f};
            worldReflectionDistance = 0.0f;
        }
        gX_Shader_SetValue_Int(shaderProg, rshader->locations.planarReflectionLoc,
            planarActive ? 1 : 0);
        //opencode: non-planar reflection — per-fragment local plane + the
        //   global mirror plane the RT was rendered from. Uploaded for
        //   every planar-reflection model (its own plane matches the
        //   scene's mirror plane in the demo).
        if (rshader->locations.nonPlanarReflectionLoc >= 0)
            gX_Shader_SetValue_Int(shaderProg, rshader->locations.nonPlanarReflectionLoc,
                (planarActive && model->params.nonPlanarReflection) ? 1 : 0);
        if (rshader->locations.reflectionPlaneNormalLoc >= 0)
            gX_Shader_SetValue_V3(shaderProg, rshader->locations.reflectionPlaneNormalLoc,
                worldReflectionNormal);
        if (rshader->locations.reflectionPlaneHeightLoc >= 0)
            gX_Shader_SetValue_Float(shaderProg, rshader->locations.reflectionPlaneHeightLoc,
                worldReflectionDistance);
        if (rshader->locations.waveAmplitudeLoc >= 0)
            gX_Shader_SetValue_Float(shaderProg, rshader->locations.waveAmplitudeLoc,
                model->params.waveAmplitude);
        gX_Shader_SetValue_Float(shaderProg, rshader->locations.blendOpacityLoc,
            blendingThisDraw ? model->params.blendOpacity : 1.0f);
        //opencode: M1 - transparent roughness-alpha repair pass: emit
        //   ssrRoughness (standard.frag) while keeping pass-1 coverage.
        if (rshader->locations.roughnessAlphaLoc >= 0)
            gX_Shader_SetValue_Int(shaderProg, rshader->locations.roughnessAlphaLoc,
                gfx->state.transparentRoughnessPass ? 1 : 0);
        
        //opencode: material colors combined with per-model albedo factor.
        //   The mesh material's albedoColor is multiplied by params.albedo so that
        //   per-model brightness control (set via add_model's albedo parameter) works
        //   for PBR and Phong paths alike. The file's baseColorFactor is
        //   respected as-is (a prior force-bright override to (200,200,200)
        //   for textureless/dark materials is removed: it destroyed
        //   legitimate dark materials - e.g. fixture.glb is pure
        //   black by design and rendered light gray. Dark-file complaints
        //   should be fixed per-model via params, not by clobbering).
        {
            gX_Color tint = model->params.useTopTint
                ? (gX_Color){255, 255, 255, 255}
                : model->params.albedoTint;
            gX_Color ac = gX_RendererGFX_AlbedoTint_Apply(
                meshmaterial->maps[gX_MATERIAL_MAP_ALBEDO].color,
                model->params.albedo, tint);
            gX_Shader_SetValue_Color(shaderProg, rshader->locations.albedoColorLoc, ac);
        }
        gX_Shader_SetValue_Color(shaderProg,rshader->locations.metalnessColorLoc,   meshmaterial->maps[gX_MATERIAL_MAP_METALNESS].color);
        
        //opencode: alpha blending GL state — opaque models MUST draw with blend
        //   OFF: the fragment alpha carries the SSR roughness (standard.frag
        //   writes ssrRoughness into finalColor.a) and any leftover blend would
        //   darken the draw by (1-roughness)
        gX_I1 blendWasEnabled = 0, depthMaskWas = 1;
        if (blendingThisDraw) {
            blendWasEnabled = gX_RHI_IsEnabled(gX_RHI_BLEND);
            gX_RHI_GetBooleanv(gX_RHI_DEPTH_WRITEMASK, &depthMaskWas);
            gX_RHI_Enable(gX_RHI_BLEND);
            //opencode: M1 - roughness-alpha repair pass keeps RGB and
            //   replaces alpha (the per-material default below would
            //   clobber the pass blend funcs set by Draw_Transparent).
            if (gfx && gfx->state.transparentRoughnessPass)
                gX_RHI_Blend_Func_Separate(gX_RHI_ZERO, gX_RHI_ONE,
                    gX_RHI_ONE, gX_RHI_ZERO);
            else
                gX_RHI_Blend_Func_Separate(gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA, gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA);
            gX_RHI_Depth_Write(0);
        } else {
            blendWasEnabled = gX_RHI_IsEnabled(gX_RHI_BLEND);
            gX_RHI_Disable(gX_RHI_BLEND);
        }
        
        //opencode: per-model texture binding via textureID[]/texturePurpose[] arrays
        for (int i = 0; i < gX_GFX_MODEL_TEXTURE_SLOTS; i++) {
            if (model->texturePurpose[i] >= 0 && model->textureID[i] >= 0) {
                //opencode: M7 - purpose range guard. raylib Material.maps has
                //   12 entries but only slots 0-7 are ever bound (8+ are
                //   effect units: SHADOW/SKYBOX/CSM_ARRAY/COOKIE). An
                //   unchecked schema purpose >= 12 wrote out of bounds;
                //   8-11 injected silently nowhere. Warn once and skip.
                if (model->texturePurpose[i] >= gX_GFX_MAX_MATERIAL_MAPS) {
                    static gX_I1 purposeWarned = 0;
                    if (!purposeWarned) {
                        purposeWarned = 1;
                        gX_TraceLog_GFX(gX_LOG_WARNING,
                            "texturePurpose[%d]=%d out of bindable range 0-%d - injection skipped",
                            i, model->texturePurpose[i], gX_GFX_MAX_MATERIAL_MAPS - 1);
                    }
                    continue;
                }
                gX_RendererGFX_Texture *tex = gX_Heap_SeekPtr_Index(&gfx->textures, (gX_U32)model->textureID[i]);
                if (tex && gfx->textures.meta[model->textureID[i]].USED) {
                    meshmaterial->maps[model->texturePurpose[i]].texture = tex->wrapper.texture;
                    if (model->texturePurpose[i] <= gX_MATERIAL_MAP_HEIGHT)
                        injectedMapIDs[model->texturePurpose[i]] = tex->wrapper.texture.id;
                }
            }
        }
        //opencode: texture injection — override normal map from textures heap
        if (model->params.injectedNormalMapID >= 0) {
            gX_RendererGFX_Texture *injTex = gX_Heap_SeekPtr_Index(&gfx->textures,
                (gX_U32)model->params.injectedNormalMapID);
            if (injTex && gfx->textures.meta[model->params.injectedNormalMapID].USED) {
                meshmaterial->maps[gX_MATERIAL_MAP_NORMAL].texture = injTex->wrapper.texture;
                injectedMapIDs[gX_MATERIAL_MAP_NORMAL] = injTex->wrapper.texture.id;
                if (!model->params.hasNormalMap) model->params.hasNormalMap = true;
            }
        }
        //opencode: dynamic cubemap — override material cubemap for models with enableReflections.
        //   Planar models need it too: the projective fallback samples the
        //   env cubemap where the mirror camera cannot see (grazing angles);
        //   without this the sampler holds stale/black content there.
        if (gfx && (model->params.enableReflections || model->params.planarReflection) && gfx->reflection.cubeReady) {
            //opencode: probe grid MVP-1.5 — direct AND planar consumers bind
            //   the nearest baked probe instead of the mirror-camera capture;
            //   the shared cubemap slot needs no new texture units and the
            //   box projection consumes the probe position unmodified. The
            //   planar branch blends mirror-RT by confidence and falls back
            //   to this cubemap at grazing angles — exactly where the
            //   mirror-camera parallax error is worst, so probes help most.
            //opencode: MVP-2 blend — second-nearest probe on unit 31 with a
            //   distance-crossfade weight (GX_PROBE_BLEND=0 restores nearest-
            //   only). Kills the reflection seam between models bound to
            //   different probes and the pop on moving-consumer handoff.
            gX_I1 useProbe = false;
            gX_F32v3 probeCenter = {0.0f, 0.0f, 0.0f};
            gX_Texture probeCubeB = {0};
            gX_F32v3 probeCenterB = {0.0f, 0.0f, 0.0f};
            gX_F32 probeBlendB = 0.0f;
            gX_I1 debugProbeOverride = false;
            if (gfx->reflection.probesReady
                && gfx->reflection.probeCount > 0) {
                gX_F32 bestD = -1.0f;
                gX_F32 bestD2 = -1.0f;
                gX_Texture bestTex = {0};
                gX_F32v3 bestC = {0.0f, 0.0f, 0.0f};
                for (gX_U32 pi = 0; pi < gfx->reflection.probeCount; pi++) {
                    gX_RendererGFX_Probe *probe = &gfx->reflection.probes[pi];
                    if (!probe->baked || probe->cube.id == 0) continue;
                    gX_F32v3 delta = gX_F32v3_Sub(model->params.pos, probe->pos);
                    gX_F32 dist = gX_F32v3_Length(delta);
                    if (!useProbe || dist < bestD) {
                        //opencode: demote the old nearest — it is a
                        //   second-best candidate, not automatically second.
                        if (useProbe && (bestD2 < 0.0f || bestD < bestD2)) {
                            bestD2 = bestD;
                            probeCubeB = bestTex;
                            probeCenterB = bestC;
                        }
                        bestD = dist;
                        bestTex = probe->cube;
                        bestC = probe->pos;
                        useProbe = true;
                    } else if (bestD2 < 0.0f || dist < bestD2) {
                        bestD2 = dist;
                        probeCubeB = probe->cube;
                        probeCenterB = probe->pos;
                    }
                }
                gX_RendererGFX_Probe *debugProbe = gfx_probe_debug_select(gfx);
                if (debugProbe) {
                    bestTex = debugProbe->cube;
                    bestC = debugProbe->pos;
                    bestD2 = -1.0f;
                    probeCubeB = (gX_Texture){0};
                    probeCenterB = debugProbe->pos;
                    useProbe = true;
                    debugProbeOverride = true;
                }
                if (useProbe) {
                    probeCenter = bestC;
                    meshmaterial->maps[gX_MATERIAL_MAP_CUBEMAP].texture = bestTex;
                }
                //opencode: crossfade width in meters (probe spacing is 6m):
                //   0.5/0.5 at equidistance, pure nearest beyond the width.
                const gX_I8 *blendHook = getenv("GX_PROBE_BLEND");
                gX_I1 blendOn = (!blendHook || blendHook[0] == '\0' || blendHook[0] != '0') ? 1 : 0;
                if (blendOn && !debugProbeOverride && useProbe
                    && bestD2 >= 0.0f && probeCubeB.id > 0) {
                    gX_F32 t = (bestD2 - bestD) / 3.0f;
                    if (t < 0.0f) t = 0.0f;
                    if (t > 1.0f) t = 1.0f;
                    probeBlendB = 0.5f * (1.0f - t);
                }
            }
            if (useProbe) {
                gX_Shader_SetValue_V3(shaderProg, rshader->locations.cubePosLoc, probeCenter);
                if (rshader->locations.cubeMapBLoc >= 0 && probeBlendB > 0.0f)
                    gX_Shader_SetValue_CubemapSlot(shaderProg, rshader->locations.cubeMapBLoc,
                        probeCubeB, gX_GFX_TEXUNIT_PROBE_B);
                if (rshader->locations.cubePosBLoc >= 0)
                    gX_Shader_SetValue_V3(shaderProg, rshader->locations.cubePosBLoc, probeCenterB);
            } else {
                meshmaterial->maps[gX_MATERIAL_MAP_CUBEMAP].texture = gfx->reflection.dynamicCubemap;
                //ai: candidate binding uses capture ownership; legacy remains the default.
                const gX_I8 *correctHook = getenv("GX_PROBE_BOX_CORRECT");
                gX_F32v3 cubeCenter = gfx->reflection.dynamicCapturePosition;
                if (!correctHook || strcmp((const char *)correctHook, "1") != 0) {
                    cubeCenter = camera.camera3D.position;
                    gX_F32 d = gX_F32v3_Dot(cubeCenter, worldReflectionNormal)
                        - worldReflectionDistance;
                    cubeCenter = gX_F32v3_Sub(cubeCenter,
                        gX_F32v3_MulScalar(worldReflectionNormal, 2.0f * d));
                }
                gX_Shader_SetValue_V3(shaderProg, rshader->locations.cubePosLoc,
                    cubeCenter);
            }
            if (rshader->locations.probeBlendBLoc >= 0)
                gX_Shader_SetValue_Float(shaderProg, rshader->locations.probeBlendBLoc, probeBlendB);
            gX_Shader_SetValue_V3(shaderProg, rshader->locations.cubeSizeLoc,
                (gX_F32v3){50.0f, 12.0f, 50.0f});
            gfx_probe_sky_upload(shaderProg, gfx,
                meshmaterial->maps[gX_MATERIAL_MAP_CUBEMAP].texture.id,
                probeCubeB.id);
        }
        //opencode: refractive material — bind bgTex sampler (dedicated unit 23
        //   to avoid conflict with SSAO blur which also uses TEXUNIT_AUX=20).
        if (model->params.enableRefraction && gfx->debugVis.refractionReady
            && gfx->debugVis.refractionBg.wrapper.loadingState.loaded) {
            gX_Shader_SetValue_TextureSlot(shaderProg, rshader->locations.bgTexLoc,
                gfx->debugVis.refractionBg.wrapper.rt.texture, gX_GFX_TEXUNIT_REFRACTION);
        }
        //opencode: planar reflection — bind reflection RT + mirrored MVP
        //   (two-mirror pair slabs sample their own fresh bounce; legacy
        //   single-plane scenes keep the shared planar RT exactly).
        if (planarActive) {
            gX_Texture planeTex = gfx->reflection.planar.wrapper.rt.texture;
            gX_Matrix planeMvp = gfx->reflection.mvp;
            if (model->params.mirrorPair > 0 && gX_RendererGFX_Mirror_Active(gfx)) {
                gX_I32 tag = model->params.mirrorPair - 1;
                if (tag < 0) tag = 0;
                if (tag > 1) tag = 1;
                planeTex = gX_RendererGFX_Mirror_FreshTex(gfx, tag);
                planeMvp = gX_RendererGFX_Mirror_FreshMvp(gfx, tag);
            }
            gX_Shader_SetValue_TextureSlot(shaderProg, rshader->locations.reflTexLoc, planeTex, gX_GFX_TEXUNIT_REFLECTION);
            gX_Shader_SetValue_Matrix(shaderProg, rshader->locations.reflMVPLoc, planeMvp);
        }
        //opencode: SSAO map binding — RAW GL (raylib's SetShaderValueTextureSlot only
    //   supports units 0-11 via rlSetTexture; the SSAO unit is 12, so the raylib
    //   path silently failed and the sampler read texture 0 = white → SSAO was a
    //   no-op). Bind the texture on the unit directly and set the sampler to it.
        if (gfx && gfx->state.ENABLE_SSAO && gfx->ssao.blurMap.wrapper.loadingState.loaded) {
            gX_Texture ssaoTex = gfx->ssao.blurMap.wrapper.rt.texture;
            if (rshader->locations.ssaoMapLoc >= 0) {
                gX_RHI_Tex_BindOnUnit(ssaoTex.id, gX_GFX_TEXUNIT_SSAO);
                gX_RHI_Shader_Use(shaderProg.id);
                gX_RHI_UniformInt(rshader->locations.ssaoMapLoc, gX_GFX_TEXUNIT_SSAO);
            }
        }
        //opencode: gloss map texture binding — look up texture from heap by glossMapID
        if (model->params.hasGlossMap && model->params.glossMapID >= 0) {
            gX_RendererGFX_Texture *glossTex = gX_Heap_SeekPtr_Index(&gfx->textures,
                (gX_U32)model->params.glossMapID);
            if (glossTex && gfx->textures.meta[model->params.glossMapID].USED) {
                gX_Shader_SetValue_TextureSlot(shaderProg, rshader->locations.glossMapLoc,
                    glossTex->wrapper.texture, gX_GFX_TEXUNIT_GLOSS);
            }
        }
        
        //opencode: attach the draw shader BEFORE the maps loop — the loop
        //   reads sampler locs from meshmaterial->shader.locs[15+mi], which
        //   only holds the GFX sampler mapping (BindTextureUniforms) after
        //   attach. Pre-attach it holds the loader's raylib-default locs,
        //   so frame 1 wrote unit ints into foreign uniforms (apitrace:
        //   loc1=viewPosition <- 0, GL_INVALID_OPERATION 1x/run).
        if (depthMode) gX_Model_AttachShader(&m, gfx->depth.shader);
        else gX_Model_AttachShader(&m, gfx->shader.shader);
        if (!depthMode) gfx_probe_trace_upload(shaderProg);
#if defined(GX_PROBE_COVERAGE_EXPERIMENT)
        if (!depthMode) gfx_probe_proxy_upload(shaderProg, gfx);
#endif

        //opencode: static lightmap sampler — unit 26 is outside raylib's
        //   rlSetTexture 0-11 helper range, so bind it through the RHI after
        //   activating the final draw program. Invalid or unloaded IDs keep
        //   the shader gate off and cannot consume stale texture state.
        {
            gX_I1 lightmapBound = 0;
            if (!depthMode && lightmapEnabled
                && model->params.lightmapID >= 0
                && (gX_U32)model->params.lightmapID < gfx->textures.amount
                && gfx->textures.meta[model->params.lightmapID].USED) {
                gX_RendererGFX_Texture *lightmap = gX_Heap_SeekPtr_Index(&gfx->textures,
                    (gX_U32)model->params.lightmapID);
                if (lightmap && lightmap->wrapper.loadingState.loaded
                    && lightmap->wrapper.texture.id > 0
                    && rshader->locations.lightmapMapLoc >= 0) {
                    gX_Texture_SetWrap(lightmap->wrapper.texture, gX_TEXTURE_WRAP_CLAMP);
                    gX_RHI_Tex_BindOnUnit(lightmap->wrapper.texture.id, gX_GFX_TEXUNIT_LIGHTMAP);
                    gX_RHI_Shader_Use(shaderProg.id);
                    gX_RHI_UniformInt(rshader->locations.lightmapMapLoc, gX_GFX_TEXUNIT_LIGHTMAP);
                    lightmapBound = 1;
                }
            }
            if (!lightmapBound && rshader->locations.hasLightmapLoc >= 0) {
                gX_RHI_Shader_Use(shaderProg.id);
                gX_RHI_UniformInt(rshader->locations.hasLightmapLoc, 0);
            }
        }

        //opencode: bind material map textures — bindless or unit-based
        {
            for (int mi = 0; mi < gX_GFX_MAX_MATERIAL_MAPS; mi++) {
                gX_U32 texId = meshmaterial->maps[mi].texture.id;
                if (texId == 0) continue;
                gX_I32 loc = meshmaterial->shader.locs[15 + mi];
                if (loc < 0) continue;
#if defined(GX_BINDLESS_SUPPORTED)
                //opencode: bindless material-map path — deliberately compiled
                //   out (GX_BINDLESS_SUPPORTED undefined everywhere): the
                //   bindless loop was skipped for model-loaded textures not in
                //   the GFX texture heap, leaving all samplers at unit 0 and
                //   dark models (session 2026-07-23). The unit-based loop below
                //   is the active path. Bindless machinery lives in RHI
                //   (rhi_bindless.c) for future re-enablement.
                if (gX_BindlessSupported) {
                    GX_HEAP_FOREACH(gfx->textures, gX_RendererGFX_Texture, t) {
                        if (t->wrapper.texture.id == texId &&
                            t->wrapper.bindlessHandle != 0) {
                            gX_Shader_SetValue_Handle64(shaderProg, loc,
                                t->wrapper.bindlessHandle);
                            break;
                        }
                    }
                } else
#endif
                {
                    gX_Texture matTex = {0};
                    matTex.id = texId;
                    matTex.width = meshmaterial->maps[mi].texture.width;
                    matTex.height = meshmaterial->maps[mi].texture.height;
                    //opencode: cube-target maps (env cubemap override) must
                    //   bind as GL_TEXTURE_CUBE_MAP — the 2D bind fails with
                    //   GL_INVALID_OPERATION (target mismatch), the sampler
                    //   reads black, and ENABLE_REFLECTIONS models show zero
                    //   response (slab A/B byte-identical, apitrace-proven).
                    if (mi == gX_MATERIAL_MAP_CUBEMAP
                        || mi == gX_MATERIAL_MAP_IRRADIANCE
                        || mi == gX_MATERIAL_MAP_PREFILTER)
                        gX_Shader_SetValue_CubemapSlot(shaderProg, loc, matTex, mi);
                    else
                        gX_Shader_SetValue_TextureSlot(shaderProg, loc, matTex, mi);
                }
            }
        }

        //opencode: build SRT matrix from params (scale → rotate → translate, origin at model center)
        //   gX_Matrix_Multiply computes left·right = right @ left in standard notation,
        //   so S·R·T in gxmath code = T * R * S in standard column vector convention (correct).
        gX_Matrix s_mat = gX_Matrix_Scale(model->params.scale.x, model->params.scale.y, model->params.scale.z);
        gX_Matrix r_mat = gX_Matrix_RotateXYZ((gX_F32v3){
            model->params.rotation.x * gX_DEG2RAD,   // deg2rad
            model->params.rotation.y * gX_DEG2RAD,
            model->params.rotation.z * gX_DEG2RAD
        });
        gX_Matrix t_mat = gX_Matrix_Translate(model->params.pos.x, model->params.pos.y, model->params.pos.z);
        gX_Matrix trs = gX_Matrix_Multiply(s_mat, r_mat);   // S·R = R @ S
        trs = gX_Matrix_Multiply(trs, t_mat);                 // (S·R)·T = T @ R @ S
        gX_Matrix savedTransform = m.transform;
        m.transform = trs;

        //opencode: upload per-model matrix uniforms via cached locations.
        //   CRITICAL: in depthMode, the model is drawn with gfx->depth.shader
        //   (attached below), not the main shader. We must upload matModel to
        //   the depth shader or it remains zero → all vertices clipped → empty
        //   shadow map → ENABLE_SHADOWS toggles have zero visual effect.
        if (depthMode) {
            gX_Shader depthShader = gfx->depth.shader;
            //opencode: enable depth shader and upload uniforms via RHI wrappers.
            //   Locations cached once per program id — glGetUniformLocation per
            //   model per draw was ~2 driver round-trips x N models x 2 passes.
            gX_RHI_Shader_Use((gX_U32)depthShader.id);
            static gX_U32 cachedDepthProg = 0;
            static gX_I32 cachedMatModelLoc = -1;
            static gX_I32 cachedLightSpaceLoc = -1;
            static gX_I32 cachedBoneMatricesLoc = -1;
            static gX_I32 cachedUseSkinningLoc = -1;
            if (cachedDepthProg != depthShader.id) {
                cachedDepthProg = depthShader.id;
                cachedMatModelLoc = gX_RHI_GetUniformLocation((gX_U32)depthShader.id, "matModelDepth");
                cachedLightSpaceLoc = gX_RHI_GetUniformLocation((gX_U32)depthShader.id, "matLightSpace");
                cachedBoneMatricesLoc = gX_RHI_GetUniformLocation((gX_U32)depthShader.id, "boneMatrices");
                cachedUseSkinningLoc = gX_RHI_GetUniformLocation((gX_U32)depthShader.id, "gX_UseSkinning");
                g_depthSkinningSwitchLoc = cachedUseSkinningLoc;
            }
            if (cachedMatModelLoc >= 0)
                gX_RHI_UniformMatrix(cachedMatModelLoc, 1, 1, &m.transform.m0);
            if (cachedLightSpaceLoc >= 0)
                gX_RHI_UniformMatrix(cachedLightSpaceLoc, 1, 1, &gfx->sun.camera.viewProjection.m0);
            //opencode: GPU skinning in the depth pass — animated models must
            //   cast posed shadows, not bind-pose ones. The backend's per-mesh
            //   switch refines the model-level flag.
            if (cachedBoneMatricesLoc >= 0) {
                gX_I32 sourceBones = model->model.skeleton.boneCount;
                gX_I32 boneCount = sourceBones;
                if (boneCount > gX_GFX_SKIN_MAX_BONES)
                    boneCount = gX_GFX_SKIN_MAX_BONES;
                //opencode: per-model trace at a settled frame — a global cap
                //   let unskinned scene models consume the budget before the
                //   rigged model was drawn.
                if (maindata.frameCount >= 60 && maindata.frameCount <= 62
                    && sourceBones > 0) {
                    const char *trace = getenv("GX_SKIN_TRACE");
                    const char *name = strrchr((const char *)model->path, '/');
                    if (trace && trace[0] && trace[0] != '0') {
                        gX_TraceLog(gX_LOG_INFO,
                            "GX_SKIN_TRACE: pass=depth model=%s bones=%d uploaded=%d cap=%d",
                            name ? name + 1 : (const char *)model->path,
                            sourceBones, boneCount, gX_GFX_SKIN_MAX_BONES);
                    }
                }
                if (boneCount > 0 && model->model.boneMatrices)
                    gX_RHI_UniformMatrix(cachedBoneMatricesLoc,
                        (gX_U32)boneCount, 1, &model->model.boneMatrices[0].m0);
                if (cachedUseSkinningLoc >= 0)
                    gX_RHI_UniformInt(cachedUseSkinningLoc,
                        (boneCount > 0 && model->model.boneMatrices) ? 1 : 0);
            }
        } else {
            if (rshader->locations.matModelLoc >= 0)
                gX_Shader_SetValue_Matrix(shaderProg, rshader->locations.matModelLoc, m.transform);
            if (rshader->locations.gX_ModelMatrixLoc >= 0)
                gX_Shader_SetValue_Matrix(shaderProg, rshader->locations.gX_ModelMatrixLoc, m.transform);
            if (rshader->locations.matNormalLoc >= 0) {
                gX_Matrix normalMat = gX_Matrix_Transpose(gX_Matrix_Invert(m.transform));
                gX_Shader_SetValue_Matrix(shaderProg, rshader->locations.matNormalLoc, normalMat);
            }
            //opencode: GPU skinning — the renderer draws meshes directly (not
            //   through raylib's model path), so the bone matrices are uploaded
            //   here; the backend sets the per-mesh switch at mesh draw time.
            if (rshader->locations.boneMatricesLoc >= 0) {
                gX_I32 sourceBones = model->model.skeleton.boneCount;
                gX_I32 boneCount = sourceBones;
                if (boneCount > gX_GFX_SKIN_MAX_BONES)
                    boneCount = gX_GFX_SKIN_MAX_BONES;
                //opencode: per-model trace at a settled frame (see the depth
                //   path note); the gate keys on model=skinprobe80.glb.
                if (maindata.frameCount >= 60 && maindata.frameCount <= 62
                    && sourceBones > 0) {
                    const char *trace = getenv("GX_SKIN_TRACE");
                    const char *name = strrchr((const char *)model->path, '/');
                    if (trace && trace[0] && trace[0] != '0') {
                        gX_TraceLog(gX_LOG_INFO,
                            "GX_SKIN_TRACE: pass=main model=%s bones=%d uploaded=%d cap=%d",
                            name ? name + 1 : (const char *)model->path,
                            sourceBones, boneCount, gX_GFX_SKIN_MAX_BONES);
                    }
                }
                if (boneCount > 0 && model->model.boneMatrices)
                    gX_RHI_UniformMatrix(rshader->locations.boneMatricesLoc,
                        (gX_U32)boneCount, 1, &model->model.boneMatrices[0].m0);
                if (rshader->locations.gX_UseSkinningLoc >= 0)
                    gX_RHI_UniformInt(rshader->locations.gX_UseSkinningLoc,
                        (boneCount > 0 && model->model.boneMatrices) ? 1 : 0);
            }
        }

        //opencode: wireframe mode — switch to line rendering when enabled.
        //   Only affects the opaque (non-depth) pass; shadow maps stay filled.
        gX_I1 wireframe = (!depthMode && gfx && gfx->state.ENABLE_WIREFRAME);
        if (wireframe) gX_RHI_PolygonMode(gX_RHI_FRONT_AND_BACK, gX_RHI_LINE);

        //opencode: hardware instancing — draw N copies with per-instance transforms
        if (!depthMode && gfx && gX_GL.hasInstancing
            && gfx->state.ENABLE_INSTANCING && model->params.instanceCount > 0) {
            gX_U32 n = model->params.instanceCount;
            if (n > 64) {
                gX_TraceLog_GFX(gX_LOG_WARNING, "instanceCount %u clamped to 64 (shader uniform array limit)",
                    (gX_U32)model->params.instanceCount);
                n = 64;
            }
            gX_Matrix xforms[64];
            gX_I32 side = (gX_I32)ceilf(sqrtf((gX_F32)n));
            gX_F32 spacing = 1.5f;
            gX_F32v3 basePos = model->params.pos;
            for (gX_U32 ii = 0; ii < n; ii++) {
                gX_I32 ix = (gX_I32)ii % side;
                gX_I32 iz = (gX_I32)ii / side;
                gX_F32v3 instancePos = {basePos.x + (gX_F32)(ix - side/2) * spacing,
                                         basePos.y,
                                         basePos.z + (gX_F32)(iz - side/2) * spacing};
                gX_Matrix s = gX_Matrix_Scale(model->params.scale.x, model->params.scale.y, model->params.scale.z);
                gX_Matrix r = gX_Matrix_RotateXYZ((gX_F32v3){
                    model->params.rotation.x * gX_DEG2RAD,
                    model->params.rotation.y * gX_DEG2RAD,
                    model->params.rotation.z * gX_DEG2RAD});
                gX_Matrix t = gX_Matrix_Translate(instancePos.x, instancePos.y, instancePos.z);
                xforms[ii] = gX_Matrix_Multiply(s, r);
                xforms[ii] = gX_Matrix_Multiply(xforms[ii], t);
            }
            gX_U32 modelIdx = (gX_U32)(model - (gX_RendererGFX_Model*)gfx->models.data);
            gX_RendererGFX_Model_DrawInstanced(gfx, modelIdx, n, xforms);
        } else {

        //ai: cache per-draw map and surface state so multi-material GLBs do
        //   not inherit mesh-zero samplers or issue redundant uniform uploads.
        gX_U32 boundMaps[gX_MATERIAL_MAP_HEIGHT + 1] = {0};
        gX_I32 boundMapFlags = -1;
        gX_Color boundAlbedo = gX_RendererGFX_AlbedoTint_Apply(
            meshmaterial->maps[gX_MATERIAL_MAP_ALBEDO].color,
            model->params.albedo, model->params.useTopTint
                ? (gX_Color){255, 255, 255, 255} : model->params.albedoTint);
        gX_F32 boundRoughness = model->params.roughness;
        gX_F32 boundMetalness = model->params.metalness;
        gX_RendererGFX_Shader *mapShader = depthMode ? &gfx->depth : &gfx->shader;
        gX_Shader mapProgram = mapShader->shader;
        gX_I1 MeshCulling = !depthMode && gfx->state.ENABLE_FRUSTUM_CULLING
            && !gfx->state.ENABLE_TAA && !gfx->state.ENABLE_REFLECTIONS
            && !model->ownsMesh && isfinite(model->params.waveAmplitude)
            && model->params.waveAmplitude == 0.0f
            && camera.camera3D.projection == gX_CAMERA_PERSPECTIVE;
        gX_Matrix MeshMVP = {0};
        if (MeshCulling)
        {
            extern gX_Camera camera;
            gfx_model_mesh_bounds_ensure(model);
            gX_Matrix View = gX_Matrix_LookAt(camera.camera3D.position,
                camera.camera3D.target, camera.camera3D.up);
            gX_Matrix Projection = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD,
                (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight,
                camera.frustum.near, camera.frustum.far);
            MeshMVP = gX_Matrix_Multiply(m.transform, View);
            MeshMVP = gX_Matrix_Multiply(MeshMVP, Projection);
            MeshCulling = model->meshBounds != NULL;
            model->frustumVisible = 1;
            model->lastVisible = 1;
        }
        //ai: one shared primitive traversal keeps GLB material resolution and draw state in sync.
        gX_I1 BlendedMaterials = !depthMode && model->glbData
            && model->glbData->hasBlendedMaterials;
        gX_I1 pBlend = 0;
        gX_I1 pDepthMask = 1;
        if (BlendedMaterials) {
            pBlend = gX_RHI_IsEnabled(gX_RHI_BLEND);
            gX_RHI_GetBooleanv(gX_RHI_DEPTH_WRITEMASK, &pDepthMask);
        }
        gX_I1 blendOn = 0;
        gX_I1 HaveOpacity = 0;
        gX_F32 PreviousOpacity = 0.0f;
        if (BlendedMaterials && rshader->locations.roughnessAlphaLoc >= 0)
            gX_Shader_SetValue_Int(shaderProg, rshader->locations.roughnessAlphaLoc,
                (gfx && gfx->state.transparentRoughnessPass) ? 1 : 0);
        for (gX_I32 mi = 0; mi < m.meshCount; mi++) {
            if (MeshCulling && gfx_mesh_bounds_outside(model->meshBounds + mi * 6, MeshMVP)) continue;
            if (!BlendedMaterials && m.meshes[mi].vaoId == 0) continue;
            if (!BlendedMaterials
                && (model->params.materialOverrideCount > 0
                    || model->params.emissiveMaterial[0] != '\0')
                && !model->params.emissiveMeshActive
                && !model->params.materialSurfaceActive
                && !model->params.materialAlbedoMapActive
                && !model->params.materialEmissiveMapActive)
                gX_RendererGFX_Model_MeshEmissiveScan(model);
            gX_U32 matIdx = m.meshMaterial ? (gX_U32)m.meshMaterial[mi] : 0;
            gX_Material *meshMat = meshmaterial;
            if ((m.meshMaterial || BlendedMaterials)
                && (gX_I32)matIdx < m.materialCount)
                meshMat = &m.materials[matIdx];
            gX_I1 needBlend = 0;
            gX_F32 bFac = 1.0f;
            //ai: alpha-test state resets per primitive, independent of blend scheduling.
            gX_I1 AlphaTest = model->params.enableAlphaTest;
            gX_F32 AlphaCutoff = model->params.alphaCutoff;
            gX_I32 AlphaIndex = model->glbData
                ? gfx_glb_material_index((gX_I32)matIdx, m.materialCount,
                    model->glbData->sourceMaterialCount) : -1;
            if (model->glbData && AlphaIndex >= 0 && model->glbData->alphaModes)
            {
                if (!AlphaTest && model->glbData->alphaModes[AlphaIndex] == 1)
                {
                    AlphaTest = 1;
                    AlphaCutoff = model->glbData->alphaCutoffs[AlphaIndex];
                }
                bFac = model->glbData->blendOpacities[AlphaIndex];
                needBlend = !depthMode && (model->glbData->alphaModes[AlphaIndex] == 2 || bFac < 0.999f);
            }
            gX_Shader_SetValue_Int(shaderProg, rshader->locations.enableAlphaTestLoc, AlphaTest);
            gX_Shader_SetValue_Float(shaderProg, rshader->locations.alphaCutoffLoc, AlphaCutoff);
            if (BlendedMaterials && !model->glbData->alphaModes) {
                //ai: resolve opacity in the full source table, then bound the metadata read.
                gX_I32 FileIndex = gfx_glb_material_index((gX_I32)matIdx,
                    m.materialCount, model->glbData->sourceMaterialCount);
                if (FileIndex >= 0 && FileIndex < model->glbData->materialCount) {
                    bFac = model->glbData->materials[FileIndex].blendOpacity;
                    if (bFac < 0.999f) needBlend = 1;
                }
            }
            gX_F32 Opacity = needBlend ? bFac : 1.0f;
            if (BlendedMaterials && (!HaveOpacity || PreviousOpacity != Opacity))
            {
                gX_Shader_SetValue_Float(shaderProg, rshader->locations.blendOpacityLoc, Opacity);
                PreviousOpacity = Opacity;
                HaveOpacity = 1;
            }
            gX_Color meshAlbedo = meshMat->maps
                ? meshMat->maps[gX_MATERIAL_MAP_ALBEDO].color
                : (gX_Color){200, 200, 200, 255};
            if (!depthMode) gX_RendererGFX_Model_MeshSurface(model, shaderProg,
                meshAlbedo,
                rshader->locations.albedoColorLoc, rshader->locations.roughnessLoc,
                rshader->locations.metalnessFactorLoc, mi, &boundAlbedo,
                &boundRoughness, &boundMetalness);
            gX_RendererGFX_Model_MeshEmissive(model, shaderProg,
                rshader->locations.emissiveFactorLoc, rshader->locations.emissiveColorLoc, mi);
            //ai: read-only opt-in draw-state diagnostic; no uniform uploads or shader changes.
            if (!depthMode && maindata.frameCount == 60 && getenv("GX_MATERIAL_UNIFORM_TRACE"))
            {
                const gX_I8 *Names[] = {"emissiveFactor", "emissiveColor", "enablePhong",
                    "usePBR", "hasLightmap", "enableReflections", "enableRefraction",
                    "hasAlbedoMap", "enableAlphaTest", "alphaCutoff", "blendOpacity"};
                gX_I32 TraceMaterial = model->glbData
                    ? gfx_glb_material_index((gX_I32)matIdx, m.materialCount,
                        model->glbData->sourceMaterialCount) : -1;
                gX_TraceLog_GFX(gX_LOG_NOTICE,
                    "MATERIAL_UNIFORM_TRACE: path=%s mesh=%d native=%u source=%d needBlend=%d opacity=%g albedoTexture=%u",
                    model->path, mi, matIdx, TraceMaterial, needBlend, Opacity,
                    meshMat->maps ? meshMat->maps[gX_MATERIAL_MAP_ALBEDO].texture.id : 0);
                for (gX_U32 TraceIndex = 0; TraceIndex < sizeof(Names) / sizeof(Names[0]); TraceIndex++)
                {
                    gX_I32 TraceLoc = gX_Shader_GetLocation(shaderProg, Names[TraceIndex]);
                    gX_F32 Values[4] = {0};
                    if (TraceLoc < 0) continue;
                    gX_RHI_GetUniformfv(shaderProg.id, TraceLoc,
                        TraceIndex == 1 ? 4 : 1, Values);
                    gX_TraceLog_GFX(gX_LOG_NOTICE,
                        "MATERIAL_UNIFORM_TRACE: path=%s mesh=%d uniform=%s value=%g,%g,%g,%g",
                        model->path, mi, Names[TraceIndex], Values[0], Values[1], Values[2], Values[3]);
                }
            }
            if (needBlend && !blendOn) {
                gX_RHI_Enable(gX_RHI_BLEND);
                if (gfx && gfx->state.transparentRoughnessPass)
                    gX_RHI_Blend_Func_Separate(gX_RHI_ZERO, gX_RHI_ONE,
                        gX_RHI_ONE, gX_RHI_ZERO);
                else
                    gX_RHI_Blend_Func_Separate(gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA, gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA);
                gX_RHI_Depth_Write(0);
                blendOn = 1;
            } else if (!needBlend && blendOn) {
                if (!pBlend) gX_RHI_Disable(gX_RHI_BLEND);
                if (pDepthMask) gX_RHI_Depth_Write(1);
                blendOn = 0;
            }
            gX_RendererGFX_Model_MeshBindMaps(model, mapProgram,
                &mapShader->locations, meshmaterial, meshMat, mi,
                injectedMapIDs, boundMaps, &boundMapFlags);
            gX_RendererGFX_Model_MeshSkinning(model,
                depthMode ? g_depthSkinningSwitchLoc
                          : rshader->locations.gX_UseSkinningLoc, mi);
            gX_Mesh_Draw(m.meshes[mi], *meshMat, m.transform);
        }
        if (blendOn) {
            if (!pBlend) gX_RHI_Disable(gX_RHI_BLEND);
            if (pDepthMask) gX_RHI_Depth_Write(1);
        }
        } //opencode: end of non-instancing else block
        //opencode: restore polygon mode in BOTH branches — the instancing branch
        //   previously leaked GL_LINE (wireframe) to subsequent draws
        if (wireframe) gX_RHI_PolygonMode(gX_RHI_FRONT_AND_BACK, gX_RHI_FILL);

        //opencode: 3D text label — DISABLED in model loop. Text labels are drawn
        //   in a separate pass after all models to avoid GL state corruption from
        //   raylib's text/font rendering (shader, depth, blend, cull state changes).
        //   See gX_RendererGFX_Draw_TextLabels in draw.c.

        m.transform = savedTransform;
        
        //opencode: restore GL state after blending
        if (blendingThisDraw) {
            if (!blendWasEnabled) gX_RHI_Disable(gX_RHI_BLEND);
            if (depthMaskWas) gX_RHI_Depth_Write(1);
        }
        if (model->params.doubleSided) {
            if (cullWas) {
                gX_RHI_Enable(gX_RHI_CULL_FACE);
                gX_RHI_Cull_Mode((gX_U32)cullMode);
            } else {
                gX_RHI_Disable(gX_RHI_CULL_FACE);
            }
        }
    }
}

//opencode: occlusion query — create GL query object on first use
static gX_I0 gX_Model_EnsureQuery(gX_RendererGFX_Model *model)
{
    if (model->occlusionQueryId == 0) {
        model->occlusionQueryId = gX_RHI_Query_Create(gX_RHI_ANY_SAMPLES_PASSED);
        model->queryIssued = 0;
        model->lastVisible = 1;
        model->occlusionVisible = 1;
        model->frustumVisible = 1;
    }
}

//opencode: begin occlusion query — draws AABB with color writes disabled so only
//   the depth test determines if any fragments would be visible. Always call
//   before drawing the full model (even if occlusion-culled) to keep queries fresh.
gX_I0 gX_RendererGFX_Model_Occlusion_Begin(gX_RendererGFX *gfx, gX_RendererGFX_Model *model)
{
    if (!gfx->state.ENABLE_OCCLUSION_CULLING) return;
    gX_Model_EnsureQuery(model);
    //opencode: skip the query while the previous one is still pending —
    //   glBeginQuery on an in-use object is GL_INVALID_OPERATION, and the
    //   non-blocking poll may not have consumed it yet
    if (model->queryIssued) return;
    gX_RHI_Query_Begin(gX_RHI_ANY_SAMPLES_PASSED, model->occlusionQueryId);
    model->queryActive = 1;
}

//opencode: end occlusion query — restores normal drawing state
gX_I0 gX_RendererGFX_Model_Occlusion_End(gX_RendererGFX *gfx, gX_RendererGFX_Model *model)
{
    if (!gfx->state.ENABLE_OCCLUSION_CULLING) return;
    //opencode: 0x502 hunt — EndQuery without a matching BeginQuery (Begin
    //   skipped above on a still-pending query) is GL_INVALID_OPERATION.
    if (!model->queryActive) return;
    gX_RHI_Query_End(gX_RHI_ANY_SAMPLES_PASSED, model->occlusionQueryId);
    model->queryIssued = 1;
    model->queryActive = 0;
}

//opencode: check occlusion from last frame — returns 1 if visible (or no data yet)
//   The caller (Model_Draw_Occluded) checks lastVisible which combines frustum + occlusion.
gX_I1 gX_RendererGFX_Model_Occlusion_IsVisible(gX_RendererGFX *gfx, gX_RendererGFX_Model *model)
{
    if (!gfx->state.ENABLE_OCCLUSION_CULLING) return 1;
    if (!model->queryIssued) return 1; // first frame — always visible
    return model->lastVisible;
}

//opencode: read back occlusion query result from previous frame, update visibility flags
gX_I0 gX_RendererGFX_Model_Occlusion_Poll(gX_RendererGFX *gfx, gX_RendererGFX_Model *model)
{
    if (!gfx->state.ENABLE_OCCLUSION_CULLING) return;
    if (!model->queryIssued) return;
    if (model->occlusionQueryId == 0) return;

    gX_U64 samplesPassed = 0;
    //opencode: GetResult is non-blocking (GL_QUERY_RESULT_AVAILABLE) — returns 0
    //   when the result isn't ready yet. Only update visibility when available;
    //   otherwise keep the previous result (treating a pending query as
    //   "invisible" would falsely cull the model for a frame).
    if (gX_RHI_Query_GetResult(model->occlusionQueryId, &samplesPassed)) {
        model->occlusionVisible = (samplesPassed > 0) ? 1 : 0;
        model->queryIssued = 0;
    }
    // lastVisible = OR of all active culling methods (conservative: draw if any says visible)
    model->lastVisible = model->frustumVisible || model->occlusionVisible;
}

//opencode: draw the model's AABB as a filled cube for occlusion testing.
//   Color writes are disabled (depth-only) so only the depth test cost matters.
//   IMPORTANT: applies the full SRT transform (scale → rotate → translate) to
//   the local-space AABB corners so the occlusion cube matches the rotated model.
//   Previous version ignored rotation, producing a misaligned cube for rotated
//   models, which caused false occlusion culling.
gX_I0 gX_RendererGFX_Model_Occlusion_DrawBBox(gX_RendererGFX *gfx, gX_RendererGFX_Model *model)
{
    if (!model->aabbValid) return;

    // Build rotation matrix from euler angles
    gX_F32 rx = model->params.rotation.x * gX_DEG2RAD;
    gX_F32 ry = model->params.rotation.y * gX_DEG2RAD;
    gX_F32 rz = model->params.rotation.z * gX_DEG2RAD;
    gX_Matrix r_mat = gX_Matrix_RotateXYZ((gX_F32v3){rx, ry, rz});

    gX_F32v3 scale = model->params.scale;
    gX_F32v3 pos = model->params.pos;
    gX_F32 *m = (gX_F32*)&r_mat;

    // Transform all 8 AABB corners by SRT, compute world-space AABB
    gX_F32 wsMin[3] = { 1e30f, 1e30f, 1e30f };
    gX_F32 wsMax[3] = { -1e30f, -1e30f, -1e30f };

    for (gX_I32 i = 0; i < 8; i++) {
        // Local-space corner
        gX_F32 lx = (i & 1) ? model->aabbMax[0] : model->aabbMin[0];
        gX_F32 ly = (i & 2) ? model->aabbMax[1] : model->aabbMin[1];
        gX_F32 lz = (i & 4) ? model->aabbMax[2] : model->aabbMin[2];

        // Scale first
        gX_F32 sx = lx * scale.x;
        gX_F32 sy = ly * scale.y;
        gX_F32 sz = lz * scale.z;

        // Rotate: gX_Matrix stores fields as m0..m3 = row0, m4..m7 = row1, etc.
        // As flat array: m[0]=m0(row0), m[4]=m4(row1), m[8]=m8(row2)
        // So m[0]*sx + m[4]*sy + m[8]*sz computes the sum for cx correctly.
        gX_F32 rx2 = m[0]*sx + m[4]*sy + m[8]*sz;
        gX_F32 ry2 = m[1]*sx + m[5]*sy + m[9]*sz;
        gX_F32 rz2 = m[2]*sx + m[6]*sy + m[10]*sz;

        // Translate
        gX_F32 wx = rx2 + pos.x;
        gX_F32 wy = ry2 + pos.y;
        gX_F32 wz = rz2 + pos.z;

        // Track world-space AABB
        if (wx < wsMin[0]) wsMin[0] = wx;
        if (wx > wsMax[0]) wsMax[0] = wx;
        if (wy < wsMin[1]) wsMin[1] = wy;
        if (wy > wsMax[1]) wsMax[1] = wy;
        if (wz < wsMin[2]) wsMin[2] = wz;
        if (wz > wsMax[2]) wsMax[2] = wz;
    }

    // Compute center and size of world-space AABB
    gX_F32 cx = (wsMin[0] + wsMax[0]) * 0.5f;
    gX_F32 cy = (wsMin[1] + wsMax[1]) * 0.5f;
    gX_F32 cz = (wsMin[2] + wsMax[2]) * 0.5f;
    gX_F32 sx2 = wsMax[0] - wsMin[0];
    gX_F32 sy2 = wsMax[1] - wsMin[1];
    gX_F32 sz2 = wsMax[2] - wsMin[2];

    //opencode: draw a filled cube as an occlusion test (depth-only, no color).
    gX_Cube_Draw((gX_Cube){{cx, cy, cz}, {sx2, sy2, sz2}}, (gX_Color){0, 0, 0, 0});
}

//opencode: draw model with occlusion culling — checks last frame's visibility,
//   issues occlusion query for next frame, only draws full model if visible.
//   Called from the opaque pass in place of the standard Model_Draw call.
//   Returns 1 if the model was drawn (visible), 0 if occlusion-culled.
gX_I1 gX_RendererGFX_Model_Draw_Occluded(gX_RendererGFX_Model *model, gX_RendererGFX *gfx, gX_I1 depthMode)
{
    //opencode: poll last frame's query result, check visibility
    gX_RendererGFX_Model_Occlusion_Poll(gfx, model);
    gX_I1 visible = gX_RendererGFX_Model_Occlusion_IsVisible(gfx, model);

    //opencode: only draw the full model if it was visible last frame
    //   NOTE: AABB query runs AFTER model draw so the depth buffer already has
    //   the scene's geometry (wall, floor, etc.) from previous models in this
    //   pass. The AABB is depth-tested against this scene depth, not its own.
    if (visible) {
        gX_RendererGFX_Model_Draw(model, gfx, depthMode);
    }

    //opencode: occlusion query bbox — draw after the model so the depth buffer
    //   contains wall/floor geometry from earlier models. The AABB is tested
    //   against this scene depth to determine if it would be visible.
    //   Depth writes are DISABLED: the box is invisible geometry and must not
    //   land in the depth buffer — SSR/SSAO/godrays reconstruct from it, and
    //   box surfaces with no color behind them produced box-shaped reflection
    //   artifacts (rays "hit" the box, sampled the background color).
    //   The query only needs the depth TEST, which is unaffected by the mask.
    if (gfx->state.ENABLE_OCCLUSION_CULLING && model->aabbValid) {
        gX_GL_State_Push();
        gX_RHI_Color_Mask(0, 0, 0, 0);
        gX_RHI_Depth_Write(0);
        gX_RendererGFX_Model_Occlusion_Begin(gfx, model);
        gX_RendererGFX_Model_Occlusion_DrawBBox(gfx, model);
        gX_RendererGFX_Model_Occlusion_End(gfx, model);
        gX_GL_State_Pop();
    }

    return visible;
}

//opencode: hardware instancing — draw N instances of a model with per-instance
//   transforms uploaded as a uniform array. The vertex shader reads
//   instanceMatrices[gl_InstanceID] when instanceCount > 0.
gX_I0 gX_RendererGFX_Model_DrawInstanced(gX_RendererGFX *gfx, gX_U32 modelIndex,
    gX_U32 instanceCount, gX_Matrix *transforms)
{
    if (!gfx || !gX_GL.hasInstancing || instanceCount == 0) return;
    if (instanceCount > 64) instanceCount = 64;
    gX_RendererGFX_Model *model = gX_Heap_SeekPtr_Index(&gfx->models, modelIndex);
    if (!model || !model->loaded) return;
    //ai: the instanced path applies the same per-model surface controls.
    gX_I1 cullWas = gX_RHI_IsEnabled(gX_RHI_CULL_FACE);
    gX_I32 cullMode = 0;
    gX_RHI_GetIntegerv(gX_RHI_CULL_FACE_MODE, &cullMode);
    if (model->params.doubleSided) gX_RHI_Disable(gX_RHI_CULL_FACE);
    if (gfx->shader.locations.flipNormalsLoc >= 0)
        gX_Shader_SetValue_Int(gfx->shader.shader,
            gfx->shader.locations.flipNormalsLoc,
            model->params.flipNormals ? 1 : 0);

    //opencode: upload instanceCount + instanceMatrices uniforms.
    //   instanceMatrices is uploaded element-by-element, each with GL_TRUE transpose.
    gX_Shader_SetValue_Int(gfx->shader.shader, gfx->shader.locations.instanceCountLoc,
        (gX_I32)instanceCount);
    for (gX_U32 ii = 0; ii < instanceCount; ii++) {
        gX_Shader_SetValue_Matrix(gfx->shader.shader,
            gfx->shader.locations.instanceMatricesLoc + (gX_I32)ii,
            transforms[ii]);
    }

    //opencode: draw the model with instancing
    gX_Model m = model->model;
    gX_Model_AttachShader(&m, gfx->shader.shader);

    //opencode: shared SRT construction (used only for material binding; actual
    //   transforms come from the instanceMatrices uniform array — but we still
    //   need a valid transform for raylib's material drawing path)
    gX_Matrix trs = gX_RendererGFX_Model_WorldMatrix(model);
    gX_Matrix savedTransform = m.transform;
    m.transform = trs;

    //opencode: draw instanced
    for (gX_I32 mi = 0; mi < m.meshCount; mi++) {
        gX_Mesh *mesh = &m.meshes[mi];
        if (mesh->vaoId == 0 || mesh->triangleCount == 0) continue;

        gX_U32 matIdx = (m.meshMaterial != NULL) ? (gX_U32)m.meshMaterial[mi] : 0;
        if (m.materialCount == 0) continue;
        if ((gX_I32)matIdx >= m.materialCount) matIdx = 0;

        //opencode: bind material + shader + VAO, draw instanced
        gX_Material *mat = &m.materials[matIdx];
        gX_RHI_VAO_Bind(mesh->vaoId);

        //opencode: bind the mesh's element buffer — glDrawElementsInstanced needs
        //   a valid GL_ELEMENT_ARRAY_BUFFER (same fix as gX_Raw_Mesh_Draw)
        if (mesh->indices != NULL && mesh->vboId != NULL && mesh->vboId[6] > 0) {
            gX_RHI_Buffer_Bind(gX_RHI_ELEMENT_ARRAY_BUFFER, mesh->vboId[6]);
        }

        //opencode: bind this mesh's material maps on units 0-7 — the instanced
        //   path bypasses raylib_Mesh_Draw, so per-mesh materials were never
        //   bound (mesh 1+ sampled material 0's textures)
        for (gX_I32 mapi = 0; mapi < 8; mapi++) {
            if (mat->maps[mapi].texture.id > 0) {
                gX_RHI_Tex_BindOnUnit(mat->maps[mapi].texture.id, (gX_U32)mapi);
            }
        }

        //opencode: bind shader + VAO, draw instanced.
        //   Per-frame uniforms (view, projection, lights, etc.) are already
        //   uploaded by gX_RendererGFX_Shader_SetValues earlier in the frame.
        //   We just need to ensure the shader program is active for this mesh.
        //   The per-material model transform (matModel) is NOT uploaded here —
        //   the vertex shader reads instanceMatrices[gl_InstanceID] when
        //   instanceCount>0, and computes gl_Position from
        //   matProjection * matView * instanceModel.
        //opencode: GPU skinning is not wired into the instanced path (the bone
        //   attributes are bound by gX_Mesh_Draw / gX_Raw_Mesh_Draw only), so
        //   pin the switch off — a stale 1 would collapse the mesh instead of
        //   rendering the bind pose.
        gX_RHI_Shader_Use(mat->shader.id);
        if (gfx->shader.locations.gX_UseSkinningLoc >= 0)
            gX_RHI_UniformInt(gfx->shader.locations.gX_UseSkinningLoc, 0);
        gX_RHI_VAO_Bind(mesh->vaoId);

        if (mesh->indices != NULL) {
            //opencode: index type is ALWAYS short (U16 struct contract;
            //   count sizes the draw, never the type - see gl_draw.c).
            gX_U32 idxType = gX_RHI_UNSIGNED_SHORT;
            gX_RHI_DrawElementsInstanced(gX_RHI_TRIANGLES,
                (gX_U32)(mesh->triangleCount * 3),
                idxType, (const void*)0,
                (gX_U32)instanceCount);
        } else {
            gX_RHI_DrawArraysInstanced(gX_RHI_TRIANGLES, 0,
                (gX_U32)mesh->vertexCount,
                (gX_U32)instanceCount);
        }
        gX_RHI_VAO_Bind(0);
    }

    //opencode: reset instanceCount to 0 for non-instanced draws
    gX_Shader_SetValue_Int(gfx->shader.shader, gfx->shader.locations.instanceCountLoc, 0);

    m.transform = savedTransform;
    if (model->params.doubleSided) {
        if (cullWas) {
            gX_RHI_Enable(gX_RHI_CULL_FACE);
            gX_RHI_Cull_Mode((gX_U32)cullMode);
        } else {
            gX_RHI_Disable(gX_RHI_CULL_FACE);
        }
    }
}

//opencode: dynamic scene management via heap
gX_I32 gX_RendererGFX_Model_Add(gX_RendererGFX *gfx, const gX_I8 *path) {
    gX_RendererGFX_Model *model = gX_Heap_Add_FirstEmpty_NoPtr(&gfx->models);
    if (!model) return -1;
    gX_RendererGFX_Model_Init(model, path);
    return (gX_I32)((gX_U8*)model - (gX_U8*)gfx->models.data) / (gX_I32)sizeof(gX_RendererGFX_Model);
}

//opencode: add a runtime-generated mesh as a model (no GLB). The model owns
//   the mesh + one default material (meshMaterial[0]=0), loaded immediately.
//   Used by the non-planar reflection demo (wavy subdivided plane).
gX_I32 gX_RendererGFX_Model_AddGeneratedMesh(gX_RendererGFX *gfx, gX_Mesh mesh) {
    gX_RendererGFX_Model *model = gX_Heap_Add_FirstEmpty_NoPtr(&gfx->models);
    if (!model) return -1;
    gX_I32 index = (gX_I32)((gX_U8*)model - (gX_U8*)gfx->models.data) / (gX_I32)sizeof(gX_RendererGFX_Model);
    gX_RendererGFX_Model_Init(model, "");
    if (mesh.vertexCount <= 0 || mesh.triangleCount <= 0) {
        gX_Heap_Remove_Index_NoPtr(&gfx->models, (gX_U32)index);
        return -1;
    }
    gX_Mesh *meshes = (gX_Mesh*)malloc(sizeof(gX_Mesh));
    if (!meshes) return -1;
    *meshes = mesh;
    gX_Material *mats = (gX_Material*)malloc(sizeof(gX_Material));
    if (!mats) { free(meshes); return -1; }
    *mats = gX_Material_LoadDefault();
    gX_I32 *meshMat = (gX_I32*)malloc(sizeof(gX_I32));
    if (!meshMat) { free(meshes); free(mats); return -1; }
    *meshMat = 0;
    model->model.meshes = meshes;
    model->model.materials = mats;
    model->model.meshMaterial = meshMat;
    model->model.meshCount = 1;
    model->model.materialCount = 1;
    model->ownsMesh = true;
    model->loaded = true;
    //opencode: AABB from the mesh bounds
    {
        gX_F32 bmin[3] = { 1e30f, 1e30f, 1e30f };
        gX_F32 bmax[3] = { -1e30f, -1e30f, -1e30f };
        for (gX_I32 vi = 0; vi < mesh.vertexCount; vi++) {
            gX_F32 vx = mesh.vertices[vi*3+0];
            gX_F32 vy = mesh.vertices[vi*3+1];
            gX_F32 vz = mesh.vertices[vi*3+2];
            if (vx < bmin[0]) bmin[0] = vx;
            if (vy < bmin[1]) bmin[1] = vy;
            if (vz < bmin[2]) bmin[2] = vz;
            if (vx > bmax[0]) bmax[0] = vx;
            if (vy > bmax[1]) bmax[1] = vy;
            if (vz > bmax[2]) bmax[2] = vz;
        }
        if (bmin[0] < bmax[0] && bmin[1] < bmax[1] && bmin[2] < bmax[2]) {
            model->aabbMin[0] = bmin[0]; model->aabbMin[1] = bmin[1]; model->aabbMin[2] = bmin[2];
            model->aabbMax[0] = bmax[0]; model->aabbMax[1] = bmax[1]; model->aabbMax[2] = bmax[2];
            model->aabbValid = true;
        }
    }
    return index;
}

gX_I32 gX_RendererGFX_Model_Remove(gX_RendererGFX *gfx, gX_U32 index) {
    if (index >= gfx->models.amount) return -1;
    gX_RendererGFX_Model *model = gX_Heap_SeekPtr_Index(&gfx->models, index);
    if (model && model->loaded) gX_RendererGFX_Model_Unload(gfx, model);
    gX_Heap_Remove_Index_NoPtr(&gfx->models, index);
    return 0;
}

gX_I32 gX_RendererGFX_Model_RemoveAll(gX_RendererGFX *gfx) {
    GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, model) {
        if (model->loaded) gX_RendererGFX_Model_Unload(gfx, model);
    }
    GX_HEAP_REMOVE_ALL(gfx->models);
    return 0;
}
