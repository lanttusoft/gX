/**********************************************************************************************
*
*   [gX: src/renderer/gfx/glb_loader.c]
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


//opencode: post-parse GLB files for all extensions + node hierarchy + animations
//   raylib's LoadModel() discards cgltf data after extracting meshes/materials/textures.
//   This file re-parses the GLB via cgltf (which ships inside raylib) to extract
//   all metadata raylib doesn't pass through: KHR_lights_punctual, KHR_materials_*,
//   KHR_texture_transform, node hierarchy, animations, and extras.
//   NOTE: CGLTF_IMPLEMENTATION is NOT defined here — raylib's rmodels.c defines it
//   and implementation symbols are linked from libraylib.a.
//   D15: header vendored into src/external/ (also works for the GLFW build).
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#include "../../../external/raylib/src/external/cgltf.h"

#include "../../common/log.h"
#include "../gfx.h"
#include "model.h"
#include "light.h"
#include "glb_loader.h"
#include "boilerplate.h"
//ai: COLOR_1 lives in independent VAO attribute 9, outside raylib's 0..8 mesh contract.
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_buf.h"
#include "../../rhi/rhi_state.h"

//ai: opt-in mask extraction mirrors raylib's all-node triangle traversal, including instanced nodes.
gX_I0 gX_RendererGFX_GLB_TerrainMasksLoad(gX_RendererGFX_Model *Model)
{
    if (!Model || !Model->glbData || Model->glbData->terrainMaskBuffers) return;
    gX_I1 Requested = 0;
    for (gX_I32 Index = 0; Index < Model->params.materialOverrideCount; Index++)
        Requested |= Model->params.materialOverrides[Index].terrainBlend;
    if (!Requested || Model->model.meshCount <= 0) return;
    cgltf_options Options = {0};
    cgltf_data *Data = NULL;
    cgltf_result Result = cgltf_parse_file(&Options, Model->path, &Data);
    if (Result == cgltf_result_success) Result = cgltf_load_buffers(&Options, Data, Model->path);
    if (Result == cgltf_result_success) Result = cgltf_validate(Data);
    if (Result != cgltf_result_success)
    {
        gX_TraceLog_GFX(gX_LOG_WARNING, "TERRAIN_BLEND: mask parse failed for '%s' (%d)", Model->path, (gX_I32)Result);
        cgltf_free(Data);
        return;
    }
    gX_U32 *Buffers = calloc((size_t)Model->model.meshCount, sizeof(*Buffers));
    if (!Buffers)
    {
        gX_TraceLog_GFX(gX_LOG_WARNING, "TERRAIN_BLEND: mask buffer allocation failed for '%s'", Model->path);
        cgltf_free(Data);
        return;
    }
    gX_I32 PreviousVAO = 0, PreviousBuffer = 0;
    gX_RHI_GetIntegerv(gX_RHI_VERTEX_ARRAY_BINDING, &PreviousVAO);
    gX_RHI_GetIntegerv(gX_RHI_ARRAY_BUFFER_BINDING, &PreviousBuffer);
    gX_I32 MeshIndex = 0;
    for (cgltf_size NodeIndex = 0; NodeIndex < Data->nodes_count; NodeIndex++)
    {
        cgltf_mesh *Mesh = Data->nodes[NodeIndex].mesh;
        if (!Mesh) continue;
        for (cgltf_size PrimitiveIndex = 0; PrimitiveIndex < Mesh->primitives_count; PrimitiveIndex++)
        {
            cgltf_primitive *Primitive = &Mesh->primitives[PrimitiveIndex];
            if (Primitive->type != cgltf_primitive_type_triangles) continue;
            if (MeshIndex >= Model->model.meshCount) break;
            gX_I32 NativeIndex = MeshIndex++;
            const gX_RendererGFX_MaterialOverride *Override =
                gX_RendererGFX_Model_MeshOverride(Model, NativeIndex);
            if (!Override || !Override->terrainBlend || Override->terrainAlbedoMapID <= 0) continue;
            cgltf_accessor *Mask = NULL;
            cgltf_accessor *Tint = NULL;
            gX_I32 FirstColor = -1;
            for (cgltf_size AttributeIndex = 0; AttributeIndex < Primitive->attributes_count; AttributeIndex++)
            {
                cgltf_attribute *Attribute = &Primitive->attributes[AttributeIndex];
                if (Attribute->type != cgltf_attribute_type_color) continue;
                if (FirstColor < 0) FirstColor = Attribute->index;
                if (Attribute->index == 1) Mask = Attribute->data;
                if (Attribute->index == 0) Tint = Attribute->data;
            }
            gX_Mesh *NativeMesh = &Model->model.meshes[NativeIndex];
            if (!Mask || (Mask->type != cgltf_type_vec3 && Mask->type != cgltf_type_vec4)
                || Mask->count != (cgltf_size)NativeMesh->vertexCount || NativeMesh->vaoId == 0)
            {
                gX_TraceLog_GFX(gX_LOG_WARNING, "TERRAIN_BLEND: mesh %d has no compatible COLOR_1 mask", NativeIndex);
                continue;
            }
            if (Mask->count > 0xFFFFFFFFu / sizeof(gX_F32)) continue;
            gX_F32 *Values = malloc((size_t)Mask->count * sizeof(*Values));
            if (!Values)
            {
                gX_TraceLog_GFX(gX_LOG_WARNING, "TERRAIN_BLEND: mesh %d mask allocation failed", NativeIndex);
                continue;
            }
            gX_I1 Valid = 1;
            for (cgltf_size Vertex = 0; Vertex < Mask->count; Vertex++)
            {
                cgltf_float Color[4] = {0};
                if (!cgltf_accessor_read_float(Mask, Vertex, Color, 4) || !isfinite(Color[0]))
                {
                    Valid = 0;
                    break;
                }
                Values[Vertex] = fmaxf(0.0f, fminf(1.0f, Color[0]));
            }
            if (Valid)
            {
                if (FirstColor != 0 && !Model->params.useTopTint && NativeMesh->colors)
                {
                    for (cgltf_size Vertex = 0; Vertex < Mask->count; Vertex++)
                    {
                        cgltf_float Color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                        if (Tint && Tint->count == Mask->count)
                            cgltf_accessor_read_float(Tint, Vertex, Color, 4);
                        for (gX_I32 Channel = 0; Channel < 4; Channel++)
                            NativeMesh->colors[Vertex * 4 + Channel] = (gX_U8)
                                (fmaxf(0.0f, fminf(1.0f, Color[Channel])) * 255.0f);
                    }
                    if (NativeMesh->vboId && NativeMesh->vboId[3])
                    {
                        gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, NativeMesh->vboId[3]);
                        gX_RHI_Buffer_SubData(gX_RHI_ARRAY_BUFFER, 0,
                            (gX_U32)(Mask->count * 4), NativeMesh->colors);
                    }
                }
                Buffers[NativeIndex] = gX_RHI_Buffer_Create("terrain COLOR_1 mask");
                if (Buffers[NativeIndex])
                {
                    gX_RHI_VAO_Bind(NativeMesh->vaoId);
                    gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, Buffers[NativeIndex]);
                    gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, (gX_U32)(Mask->count * sizeof(*Values)), Values, gX_RHI_STATIC_DRAW);
                    gX_RHI_Attrib_Pointer(9, 1, gX_RHI_FLOAT, 0, 0, NULL);
                    gX_RHI_Attrib_Enable(9);
                }
            }
            else gX_TraceLog_GFX(gX_LOG_WARNING, "TERRAIN_BLEND: mesh %d COLOR_1 read failed", NativeIndex);
            free(Values);
        }
    }
    gX_RHI_VAO_Bind((gX_U32)PreviousVAO);
    gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, (gX_U32)PreviousBuffer);
    Model->glbData->terrainMaskBuffers = Buffers;
    Model->glbData->terrainMaskCount = Model->model.meshCount;
    cgltf_free(Data);
}

static gX_F32v3 cgltf_float3_to_gX(const cgltf_float *v) {
    return (gX_F32v3){v[0], v[1], v[2]};
}

static gX_I0 quat_to_euler(const cgltf_float *q, gX_F32 *outX, gX_F32 *outY, gX_F32 *outZ) {
    // quaternion to euler (degrees), ZYX order
    gX_F32 sinr_cosp = 2.0f * (q[3] * q[0] + q[1] * q[2]);
    gX_F32 cosr_cosp = 1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]);
    *outX = atan2f(sinr_cosp, cosr_cosp) * 180.0f / 3.14159265f;
    gX_F32 sinp = 2.0f * (q[3] * q[1] - q[2] * q[0]);
    *outY = (fabsf(sinp) >= 1.0f) ? copysignf(90.0f, sinp) : asinf(sinp) * 180.0f / 3.14159265f;
    gX_F32 siny_cosp = 2.0f * (q[3] * q[2] + q[0] * q[1]);
    gX_F32 cosy_cosp = 1.0f - 2.0f * (q[1] * q[1] + q[2] * q[2]);
    *outZ = atan2f(siny_cosp, cosy_cosp) * 180.0f / 3.14159265f;
}

static gX_I0 copy_extras(char *dst, cgltf_size dstLen, const cgltf_extras *extras) {
    if (extras && extras->data && extras->data[0]) {
        strncpy(dst, extras->data, dstLen - 1);
        dst[dstLen - 1] = '\0';
        // truncate if too long
        size_t len = strlen(dst);
        if (len >= dstLen - 4) {
            strcpy(dst + dstLen - 5, " ...");
        }
    } else {
        dst[0] = '\0';
    }
}

static gX_I0 fill_tex_transform(gX_RendererGFX_GLB_TexTransform *dst, const cgltf_texture_view *view) {
    dst->hasTransform = view->has_transform ? 1 : 0;
    if (dst->hasTransform) {
        dst->offset.x = view->transform.offset[0];
        dst->offset.y = view->transform.offset[1];
        dst->scale.x  = view->transform.scale[0];
        dst->scale.y  = view->transform.scale[1];
        dst->rotation = view->transform.rotation;
    }
}

gX_I0 gX_RendererGFX_GLB_Free(gX_RendererGFX_GLB_Data *data) {
    if (!data) return;
    for (gX_I32 Index = 0; Index < data->terrainMaskCount; Index++)
        if (data->terrainMaskBuffers[Index]) gX_RHI_Buffer_Delete(data->terrainMaskBuffers[Index]);
    free(data->terrainMaskBuffers);
    //ai: full-source routing arrays share the GLB data lifecycle.
    free(data->alphaModes);
    free(data->alphaCutoffs);
    free(data->blendOpacities);
    for (gX_I32 ai = 0; ai < data->animCount && ai < GX_GLB_MAX_ANIMS; ai++) {
        for (gX_I32 ci = 0; ci < data->animations[ai].channelCount && ci < 8; ci++) {
            gX_RendererGFX_GLB_AnimChannel *ch = &data->animations[ai].channels[ci];
            if (ch->times)  { free(ch->times);  ch->times = 0; }
            if (ch->values) { free(ch->values); ch->values = 0; }
        }
    }
    free(data);
}

//ai: retain the full source material count for selectors beyond the bounded metadata cache.
gX_I0 gX_RendererGFX_GLB_Extract(const gX_I8 *path, gX_RendererGFX *gfx, gX_RendererGFX_Model *model) {
    if (!path || !gfx || !model) return;
    if (model->glbData) return; // already extracted

    cgltf_options options = {0};
    cgltf_data *data = NULL;
    cgltf_result result = cgltf_parse_file(&options, path, &data);
    if (result != cgltf_result_success) {
        gX_TraceLog(gX_LOG_TRACE, "GLB: cgltf_parse_file failed for %s (result=%d)", path, (gX_I32)result);
        return;
    }
    result = cgltf_load_buffers(&options, data, path);
    if (result != cgltf_result_success) {
        gX_TraceLog(gX_LOG_WARNING, "GLB: cgltf_load_buffers failed for %s (result=%d)", path, (gX_I32)result);
        cgltf_free(data);
        return;
    }

    // Allocate GLB data struct
    gX_RendererGFX_GLB_Data *glb = (gX_RendererGFX_GLB_Data*)calloc(1, sizeof(gX_RendererGFX_GLB_Data));
    if (!glb) { cgltf_free(data); return; }
    glb->lightCount = 0;
    for (gX_I32 li = 0; li < 8; li++) glb->lightIndices[li] = -1;

    gX_TraceLog(gX_LOG_INFO, "GLB: %s — %zu meshes, %zu materials, %zu nodes, %zu lights, %zu animations",
        path, data->meshes_count, data->materials_count, data->nodes_count,
        data->lights_count, data->animations_count);

    // ==================================================================
    // Extract materials (with all KHR extensions)
    // ==================================================================
    glb->sourceMaterialCount = (gX_I32)data->materials_count;
    glb->materialCount = (gX_I32)(data->materials_count < GX_GLB_MAX_MATERIALS ? data->materials_count : GX_GLB_MAX_MATERIALS);
    for (gX_I32 mi = 0; mi < glb->materialCount; mi++) {
        cgltf_material *cm = &data->materials[mi];
        gX_RendererGFX_GLB_Material *gm = &glb->materials[mi];

        if (cm->name) {
            strncpy(gm->name, cm->name, sizeof(gm->name) - 1);
            gm->name[sizeof(gm->name) - 1] = '\0';
        }

        // Alpha mode
        gm->alphaMode = (gX_I32)cm->alpha_mode;
        gm->alphaCutoff = cm->alpha_cutoff;

        // KHR_materials_transmission
        gm->hasTransmission = cm->has_transmission ? 1 : 0;
        if (gm->hasTransmission) {
            gm->transmissionFactor = cm->transmission.transmission_factor;
        }

        // KHR_materials_volume
        gm->hasVolume = cm->has_volume ? 1 : 0;
        if (gm->hasVolume) {
            gm->volumeThicknessFactor = cm->volume.thickness_factor;
            gm->volumeAttenDist = cm->volume.attenuation_distance;
            gm->volumeAttenColor = cgltf_float3_to_gX(cm->volume.attenuation_color);
        }

        // KHR_materials_specular
        gm->hasSpecular = cm->has_specular ? 1 : 0;
        if (gm->hasSpecular) {
            gm->specularFactor = cm->specular.specular_factor;
            gm->specularColorFactor = cgltf_float3_to_gX(cm->specular.specular_color_factor);
        }

        // KHR_materials_ior
        gm->hasIor = cm->has_ior ? 1 : 0;
        if (gm->hasIor) gm->ior = cm->ior.ior;

        // KHR_materials_emissive_strength
        gm->hasEmissiveStrength = cm->has_emissive_strength ? 1 : 0;
        if (gm->hasEmissiveStrength) gm->emissiveStrength = cm->emissive_strength.emissive_strength;

        // KHR_materials_clearcoat
        gm->hasClearcoat = cm->has_clearcoat ? 1 : 0;
        if (gm->hasClearcoat) {
            gm->clearcoatFactor = cm->clearcoat.clearcoat_factor;
            gm->clearcoatRoughness = cm->clearcoat.clearcoat_roughness_factor;
        }

        // KHR_materials_sheen
        gm->hasSheen = cm->has_sheen ? 1 : 0;
        if (gm->hasSheen) {
            gm->sheenColorFactor = cgltf_float3_to_gX(cm->sheen.sheen_color_factor);
            gm->sheenRoughnessFactor = cm->sheen.sheen_roughness_factor;
        }

        // PBR metallic-roughness (core glTF material)
        gm->usePBR = cm->has_pbr_metallic_roughness ? 1 : 0;
        if (cm->has_pbr_metallic_roughness) {
            gm->albedo = cm->pbr_metallic_roughness.base_color_factor[0]; // base R as approximating brightness
            gm->metalness = cm->pbr_metallic_roughness.metallic_factor;
            gm->roughness = cm->pbr_metallic_roughness.roughness_factor;
            gm->hasAlbedoMap = (cm->pbr_metallic_roughness.base_color_texture.texture != NULL) ? 1 : 0;
            gm->hasMetalnessMap = (cm->pbr_metallic_roughness.metallic_roughness_texture.texture != NULL) ? 1 : 0;
        } else {
            gm->albedo = 0.9f;
            gm->metalness = 0.5f;
            gm->roughness = 0.5f;
        }
        gm->hasNormalMap = (cm->normal_texture.texture != NULL) ? 1 : 0;
        //opencode: no height texture exists in glTF (views[6] is hardcoded
        //   0 below) — the old has_volume approximation enabled parallax
        //   sampling with no heightmap bound.
        gm->hasHeightMap = 0;
        gm->hasEmissiveMap = (cm->emissive_texture.texture != NULL) ? 1 : 0;
        gm->hasRoughnessMap = gm->hasMetalnessMap; // shared with metallic_roughness texture
        gm->hasAoMap = (cm->occlusion_texture.texture != NULL) ? 1 : 0;
        //opencode: full emissive RGB + luminance (was R channel only: a
        //   green/blue emissive read as black).
        gm->emissiveColorFactor = cgltf_float3_to_gX(cm->emissive_factor);
        gm->emissiveFactor = 0.2126f * cm->emissive_factor[0]
                           + 0.7152f * cm->emissive_factor[1]
                           + 0.0722f * cm->emissive_factor[2];
        gm->specularColor = gm->hasSpecular ? gm->specularColorFactor : (gX_F32v3){1.0f, 1.0f, 1.0f};

        // texture_transform from each map slot
        // map index order: 0=albedo, 1=metalness, 2=normal, 3=roughness, 4=occlusion, 5=emission, 6=height
        const cgltf_texture_view *views[7] = {
            &cm->pbr_metallic_roughness.base_color_texture,
            &cm->pbr_metallic_roughness.metallic_roughness_texture,
            &cm->normal_texture,
            0, // roughness — same as metallic_roughness_texture
            &cm->occlusion_texture,
            &cm->emissive_texture,
            0  // height — typically not in glTF
        };
        for (gX_I32 ti = 0; ti < 7; ti++) {
            if (!views[ti]) continue;
            fill_tex_transform(&gm->texTransform[ti], views[ti]);
            if (views[ti]->has_transform) {
                gm->hasTexCoordOverride[ti] = 1;
                gm->texCoord[ti] = views[ti]->texcoord;
            }
        }
        // For roughness slot (3), reuse metallic_roughness texture transform
        fill_tex_transform(&gm->texTransform[3], &cm->pbr_metallic_roughness.metallic_roughness_texture);

        // extras
        copy_extras(gm->extras, sizeof(gm->extras), &cm->extras);
    }

    // ==================================================================
    // Extract nodes (hierarchy + transforms)
    // ==================================================================
    glb->nodeCount = (gX_I32)(data->nodes_count < GX_GLB_MAX_NODES ? data->nodes_count : GX_GLB_MAX_NODES);
    for (gX_I32 ni = 0; ni < glb->nodeCount; ni++) {
        cgltf_node *cn = &data->nodes[ni];
        gX_RendererGFX_GLB_Node *gn = &glb->nodes[ni];

        if (cn->name) {
            strncpy(gn->name, cn->name, sizeof(gn->name) - 1);
            gn->name[sizeof(gn->name) - 1] = '\0';
        }

        // Find parent
        gn->parentIdx = -1;
        if (cn->parent) {
            for (gX_I32 pni = 0; pni < glb->nodeCount; pni++) {
                if (&data->nodes[pni] == cn->parent) {
                    gn->parentIdx = pni;
                    break;
                }
            }
        }

        // World transform
        cgltf_float world[16];
        cgltf_node_transform_world(cn, world);
        gn->pos.x = world[12];
        gn->pos.y = world[13];
        gn->pos.z = world[14];

        // Local rotation (euler) from quaternion
        if (cn->has_rotation) {
            quat_to_euler(cn->rotation, &gn->rot.x, &gn->rot.y, &gn->rot.z);
        }
        if (cn->has_scale) {
            gn->scale.x = cn->scale[0];
            gn->scale.y = cn->scale[1];
            gn->scale.z = cn->scale[2];
        } else {
            gn->scale = (gX_F32v3){1.0f, 1.0f, 1.0f};
        }

        // Mesh reference
        gn->meshIdx = -1;
        if (cn->mesh) {
            for (gX_I32 mi = 0; mi < (gX_I32)data->meshes_count; mi++) {
                if (&data->meshes[mi] == cn->mesh) {
                    gn->meshIdx = mi;
                    break;
                }
            }
        }

        // Camera
        gn->cameraType = -1;
        if (cn->camera) {
            if (cn->camera->type == cgltf_camera_type_perspective) {
                gn->cameraType = 0;
                gn->cameraFov = cn->camera->data.perspective.yfov * 180.0f / 3.14159265f;
                gn->cameraNear = cn->camera->data.perspective.znear;
                gn->cameraFar = cn->camera->data.perspective.has_zfar ? cn->camera->data.perspective.zfar : 1000.0f;
            } else {
                gn->cameraType = 1; // orthographic
            }
        }

        // Light reference (will be linked after lights are created)
        gn->lightIdx = -1;
        if (cn->light) {
            for (gX_I32 li = 0; li < (gX_I32)data->lights_count; li++) {
                if (&data->lights[li] == cn->light) {
                    gn->lightIdx = (gX_I32)li;
                    break;
                }
            }
        }

        copy_extras(gn->extras, sizeof(gn->extras), &cn->extras);
    }

    // ==================================================================
    // Extract lights (KHR_lights_punctual)
    // ==================================================================
    for (cgltf_size li = 0; li < data->lights_count; li++) {
        cgltf_light *cl = &data->lights[li];
        gX_I32 lightIdx = gX_RendererGFX_Light_Add(gfx);
        if (lightIdx < 0) continue;
        gX_RendererGFX_Light *l = gX_Heap_SeekPtr_Index(&gfx->lights, (gX_U32)lightIdx);
        if (!l) continue;

        //opencode: track the created light's heap index so Model_Unload can
        //   remove it (previously GLB lights leaked — heap + shadow FBOs)
        if (glb->lightCount < 8) glb->lightIndices[glb->lightCount] = lightIdx;
        glb->lightCount++;

        // Type mapping
        if (cl->type == cgltf_light_type_directional) l->type = 0;
        else if (cl->type == cgltf_light_type_point) l->type = 1;
        else if (cl->type == cgltf_light_type_spot) l->type = 2;
        else l->type = 1;

        l->color.r = (gX_U8)(cl->color[0] * 255.0f);
        l->color.g = (gX_U8)(cl->color[1] * 255.0f);
        l->color.b = (gX_U8)(cl->color[2] * 255.0f);
        //opencode: glTF intensity is candela (often 1-1000+); the engine's sun
        //   uses brightness ~3. Normalize with /100 so GLB lights sit in the
        //   same range as hand-placed lights (1.5-8.0).
        l->brightness = cl->intensity / 100.0f;

        if (cl->type == cgltf_light_type_spot) {
            l->fov = cl->spot_outer_cone_angle * 2.0f * 180.0f / 3.14159265f;
        }

        // Find parent node for world transform
        for (gX_I32 ni = 0; ni < glb->nodeCount; ni++) {
            if (glb->nodes[ni].lightIdx == (gX_I32)li) {
                l->pos = glb->nodes[ni].pos;
                l->dir.x = -glb->nodes[ni].pos.x; // approximate: direction from origin toward node
                // Better: extract direction from world matrix column 2
                cgltf_node *cn = &data->nodes[ni];
                cgltf_float world[16];
                cgltf_node_transform_world(cn, world);
                l->dir.x = -world[8];
                l->dir.y = -world[9];
                l->dir.z = -world[10];
                break;
            }
        }

        gX_TraceLog(gX_LOG_INFO, "GLB: light %zu '%s' type=%d pos=(%.1f,%.1f,%.1f) color=(%u,%u,%u) intensity=%.1f",
            li, cl->name ? cl->name : "", l->type, l->pos.x, l->pos.y, l->pos.z,
            l->color.r, l->color.g, l->color.b, l->brightness);
    }

    // Link node light indices to GFX light indices
    {
        gX_I32 gfxLightBase = -1;
        GX_HEAP_FOREACH(gfx->lights, gX_RendererGFX_Light, ll) {
            gX_U32 hi = (gX_U32)(ll - (gX_RendererGFX_Light*)gfx->lights.data);
            // Find matching node by position (approximate)
            for (gX_I32 ni = 0; ni < glb->nodeCount; ni++) {
                if (glb->nodes[ni].lightIdx >= 0) {
                    gX_F32 dx = ll->pos.x - glb->nodes[ni].pos.x;
                    gX_F32 dy = ll->pos.y - glb->nodes[ni].pos.y;
                    gX_F32 dz = ll->pos.z - glb->nodes[ni].pos.z;
                    if (dx * dx + dy * dy + dz * dz < 0.01f) {
                        if (gfxLightBase < 0) gfxLightBase = (gX_I32)hi;
                        break;
                    }
                }
            }
        }
        model->lightStartIdx = gfxLightBase;
        model->lightCount = glb->lightCount;
    }

    // ==================================================================
    // Extract animations
    // ==================================================================
    glb->animCount = (gX_I32)(data->animations_count < GX_GLB_MAX_ANIMS ? data->animations_count : GX_GLB_MAX_ANIMS);
    for (gX_I32 ai = 0; ai < glb->animCount; ai++) {
        cgltf_animation *ca = &data->animations[ai];
        gX_RendererGFX_GLB_Animation *ga = &glb->animations[ai];

        if (ca->name) {
            strncpy(ga->name, ca->name, sizeof(ga->name) - 1);
            ga->name[sizeof(ga->name) - 1] = '\0';
        }

        ga->channelCount = (gX_I32)(ca->channels_count < 8 ? ca->channels_count : 8);
        for (gX_I32 ci = 0; ci < ga->channelCount; ci++) {
            cgltf_animation_channel *cc = &ca->channels[ci];
            gX_RendererGFX_GLB_AnimChannel *gc = &ga->channels[ci];
            cgltf_animation_sampler *cs = cc->sampler;

            // Target node
            gc->targetNode = -1;
            if (cc->target_node) {
                for (gX_I32 ni = 0; ni < glb->nodeCount; ni++) {
                    if (&data->nodes[ni] == cc->target_node) {
                        gc->targetNode = ni;
                        break;
                    }
                }
            }

            // Target path
            if (cc->target_path == cgltf_animation_path_type_translation) gc->targetPath = 0;
            else if (cc->target_path == cgltf_animation_path_type_rotation) gc->targetPath = 1;
            else if (cc->target_path == cgltf_animation_path_type_scale) gc->targetPath = 2;
            else if (cc->target_path == cgltf_animation_path_type_weights) gc->targetPath = 3;
            else gc->targetPath = -1;

            // Keyframe data
            if (cs && cs->input && cs->output && cs->input->buffer_view && cs->input->buffer_view->buffer && cs->output->buffer_view && cs->output->buffer_view->buffer) {
                gc->keyframeCount = (gX_I32)cs->input->count;
                gc->valueStride = (cs->output->type == cgltf_type_vec3) ? 3 :
                                  (cs->output->type == cgltf_type_vec4) ? 4 : 1;

                // Copy times
                size_t timeBytes = (size_t)gc->keyframeCount * sizeof(gX_F32);
                gc->times = (gX_F32*)malloc(timeBytes);
                if (gc->times) {
                    gX_U8 *srcT = (gX_U8*)cs->input->buffer_view->buffer->data + cs->input->offset + cs->input->buffer_view->offset;
                    memcpy(gc->times, srcT, timeBytes);
                    // Convert to seconds if stored as integer
                    if (cs->input->component_type == cgltf_component_type_r_32u) {
                        for (gX_I32 ki = 0; ki < gc->keyframeCount; ki++)
                            gc->times[ki] *= 0.001f; // milliseconds→seconds
                    }
                }

                // Copy values
                size_t valBytes = (size_t)gc->keyframeCount * (size_t)gc->valueStride * sizeof(gX_F32);
                gc->values = (gX_F32*)malloc(valBytes);
                if (gc->values) {
                    gX_U8 *srcV = (gX_U8*)cs->output->buffer_view->buffer->data + cs->output->offset + cs->output->buffer_view->offset;
                    memcpy(gc->values, srcV, valBytes);
                }

                gX_TraceLog(gX_LOG_TRACE, "GLB: anim '%s' channel %d node=%d path=%d keys=%d stride=%d",
                    ga->name, ci, gc->targetNode, gc->targetPath, gc->keyframeCount, gc->valueStride);
            }
        }
    }

    // ==================================================================
    // Apply material params (per-material flags, no model-level blending)
    // ==================================================================
    for (gX_I32 mi = 0; mi < glb->materialCount; mi++) {
        gX_RendererGFX_GLB_Material *gm = &glb->materials[mi];

        // Alpha mode: blend or has transmission
        gX_I1 needsBlend = (gm->alphaMode == 2);
        if (gm->hasTransmission && gm->transmissionFactor > 0.01f) needsBlend = 1;
        if (needsBlend) {
            gm->blendOpacity = gm->hasTransmission ?
                (1.0f - gm->transmissionFactor) : 0.65f;
            if (gm->hasIor && gm->ior > 0.1f) {
                gm->enableRefraction = 1;
            }
            glb->hasBlendedMaterials = 1;
        } else {
            gm->blendOpacity = 1.0f;
            gm->enableRefraction = 0;
        }

        // Alpha mode: mask
        //ai: MASK is resolved per primitive below the rich-material cache, not forwarded model-wide.

        //opencode: KHR_materials_emissive_strength is folded into the
        //   emissive forward below (factor = strength when color forwards).
        //   An early unconditional write here made the forward guard see a
        //   non-default factor and skip the color, leaving strength-bearing
        //   materials with a bright factor over a black color = dead
        //   emission (and no emissiveFromFile flag for per-mesh resolve).

        // Specular
        if (gm->hasSpecular && gm->specularFactor > 0.0f) {
            model->params.specularColor = gm->specularColorFactor;
        }

        //ai: keep metallic/roughness factors per material; the mesh draw
        //   resolves them against scene params so different primitives do not
        //   collapse to one model-wide value. Base color remains on raylib's
        //   material map color and is likewise resolved per primitive.
        //opencode: KHR_materials_ior value -> refractiveIndex (the boolean
        //   forward above only switches refraction on; without this every
        //   transmissive GLB refracted at the 1.0 default = air = no bend).
        //   Same defaults-fill precedence: a scene-set index survives.
        if (gm->hasIor && gm->ior > 0.1f
            && model->params.refractiveIndex == gX_GFX_REFRACTIVE_INDEX_DEF)
            model->params.refractiveIndex = gm->ior;
        //opencode: load-time material audit (one line per material — makes
        //   the PBR forward observable; mirrors the GUEST: spawned line).
        gX_TraceLog(gX_LOG_INFO, "GLB: mat='%s' metal=%.2f rough=%.2f emis=(%.2f,%.2f,%.2f)%s ior=%.2f",
            gm->name, gm->metalness, gm->roughness,
            gm->emissiveColorFactor.x, gm->emissiveColorFactor.y, gm->emissiveColorFactor.z,
            gm->texTransform[0].hasTransform ? " +texTransform" : "",
            gm->hasIor ? gm->ior : 0.0f);

        //opencode: emissive color + factor (was an R-channel scalar into
        //   the default black emissiveColor = permanently dead emission).
        //   glTF default emissive_factor is [0,0,0] = no emission, so this
        //   only lights models that actually emit. Same defaults-fill
        //   precedence as metalness above: a scene-set emissive survives.
        if ((gm->emissiveColorFactor.x > 0.0f || gm->emissiveColorFactor.y > 0.0f
            || gm->emissiveColorFactor.z > 0.0f)
            && model->params.emissiveFactor == gX_GFX_EMISSIVE_FACTOR_DEF
            && model->params.emissiveColor.r == 0
            && model->params.emissiveColor.g == 0
            && model->params.emissiveColor.b == 0) {
            gX_F32 er = gm->emissiveColorFactor.x * 255.0f;
            gX_F32 eg = gm->emissiveColorFactor.y * 255.0f;
            gX_F32 eb = gm->emissiveColorFactor.z * 255.0f;
            model->params.emissiveColor = (gX_Color){
                (gX_U8)(er > 255.0f ? 255.0f : er),
                (gX_U8)(eg > 255.0f ? 255.0f : eg),
                (gX_U8)(eb > 255.0f ? 255.0f : eb), 255};
            model->params.emissiveFactor = (gm->hasEmissiveStrength
                && gm->emissiveStrength > 0.0f) ? gm->emissiveStrength : 1.0f;
            //opencode: this model's emission came from file data, so the
            //   per-mesh scan resolves each material's own emissive (the
            //   model-level params remain the mesh-0 fallback).
            model->params.emissiveFromFile = 1;
        }

        //opencode: KHR_texture_transform scale + offset into the matching
        //   map slots (0=albedo, 1=metalness, 2=normal, 3=roughness,
        //   4=occlusion, 5=emission). Rotation and UV-set overrides have
        //   no params counterpart and stay dropped. Parsed-but-unrendered
        //   by design (no shader terms, no params sink - would need new
        //   shading models, not plumbing): KHR_materials_volume
        //   (thickness/attenuation), KHR_materials_clearcoat (second lobe),
        //   KHR_materials_sheen (fabric BRDF). The specularFactor scalar is
        //   parse-only too (only gates the color forward). Untouched when the GLB
        //   carries no transform (calloc-zeroed hasTransform), and never
        //   over a scene-set scale/offset (same precedence as above).
        if (gm->texTransform[0].hasTransform
            && model->params.albedoMapScale.x == 1.0f && model->params.albedoMapScale.y == 1.0f
            && model->params.albedoMapOffset.x == 0.0f && model->params.albedoMapOffset.y == 0.0f) {
            model->params.albedoMapScale = gm->texTransform[0].scale;
            model->params.albedoMapOffset = gm->texTransform[0].offset;
        }
        if (gm->texTransform[1].hasTransform
            && model->params.metalnessMapScale.x == 1.0f && model->params.metalnessMapScale.y == 1.0f
            && model->params.metalnessMapOffset.x == 0.0f && model->params.metalnessMapOffset.y == 0.0f) {
            model->params.metalnessMapScale = gm->texTransform[1].scale;
            model->params.metalnessMapOffset = gm->texTransform[1].offset;
        }
        if (gm->texTransform[2].hasTransform
            && model->params.normalMapScale.x == gX_GFX_MAP_SCALE_DEFAULT
            && model->params.normalMapScale.y == gX_GFX_MAP_SCALE_DEFAULT
            && model->params.normalMapOffset.x == 0.0f && model->params.normalMapOffset.y == 0.0f) {
            model->params.normalMapScale = gm->texTransform[2].scale;
            model->params.normalMapOffset = gm->texTransform[2].offset;
        }
        if (gm->texTransform[3].hasTransform
            && model->params.roughnessMapScale.x == 1.0f && model->params.roughnessMapScale.y == 1.0f
            && model->params.roughnessMapOffset.x == 0.0f && model->params.roughnessMapOffset.y == 0.0f) {
            model->params.roughnessMapScale = gm->texTransform[3].scale;
            model->params.roughnessMapOffset = gm->texTransform[3].offset;
        }
        if (gm->texTransform[4].hasTransform
            && model->params.aoMapScale.x == 1.0f && model->params.aoMapScale.y == 1.0f
            && model->params.aoMapOffset.x == 0.0f && model->params.aoMapOffset.y == 0.0f) {
            model->params.aoMapScale = gm->texTransform[4].scale;
            model->params.aoMapOffset = gm->texTransform[4].offset;
        }
        if (gm->texTransform[5].hasTransform
            && model->params.emissiveMapScale.x == 1.0f && model->params.emissiveMapScale.y == 1.0f
            && model->params.emissiveMapOffset.x == 0.0f && model->params.emissiveMapOffset.y == 0.0f) {
            model->params.emissiveMapScale = gm->texTransform[5].scale;
            model->params.emissiveMapOffset = gm->texTransform[5].offset;
        }

        // Water detection by name (case-insensitive) — per-material flag only
        gX_I8 lower[128];
        strncpy(lower, gm->name, sizeof(lower) - 1);
        lower[sizeof(lower) - 1] = '\0';
        for (gX_I32 ci = 0; lower[ci]; ci++) {
            if (lower[ci] >= 'A' && lower[ci] <= 'Z') lower[ci] += 'a' - 'A';
        }
        if (strstr(lower, "water") || strstr(lower, "pool") || strstr(lower, "lake")) {
            gm->isWater = 1;
            gm->blendOpacity = gm->hasTransmission ?
                (1.0f - gm->transmissionFactor) : 0.65f;
            if (gm->hasIor && gm->ior > 0.1f) {
                gm->enableRefraction = 1;
            }
            glb->hasBlendedMaterials = 1;
            gX_TraceLog(gX_LOG_INFO, "GLB: material '%s' detected as water — blend=%.2f",
                gm->name, gm->blendOpacity);
        }
    }

    // ==================================================================
    // Store GLB data pointer on model
    // ==================================================================
    //ai: preserve cached water/transmission decisions while retaining uncached glTF alpha modes.
    if (glb->sourceMaterialCount > 0)
    {
        glb->alphaModes = calloc((size_t)glb->sourceMaterialCount, sizeof(*glb->alphaModes));
        glb->alphaCutoffs = calloc((size_t)glb->sourceMaterialCount, sizeof(*glb->alphaCutoffs));
        glb->blendOpacities = calloc((size_t)glb->sourceMaterialCount, sizeof(*glb->blendOpacities));
        if (!glb->alphaModes || !glb->alphaCutoffs || !glb->blendOpacities)
        {
            free(glb->alphaModes);
            free(glb->alphaCutoffs);
            free(glb->blendOpacities);
            glb->alphaModes = NULL;
            glb->alphaCutoffs = NULL;
            glb->blendOpacities = NULL;
            gX_TraceLog(gX_LOG_WARNING, "GLB: full-source alpha routing allocation failed: %s", path);
        }
        else
        {
            for (gX_I32 Index = 0; Index < glb->sourceMaterialCount; Index++)
            {
                const cgltf_material *Material = &data->materials[Index];
                glb->alphaModes[Index] = (gX_I32)Material->alpha_mode;
                glb->alphaCutoffs[Index] = Material->alpha_cutoff;
                gX_I1 Transmission = Material->has_transmission
                    && Material->transmission.transmission_factor > 0.01f;
                glb->blendOpacities[Index] = Index < glb->materialCount
                    ? glb->materials[Index].blendOpacity
                    : (Transmission ? 1.0f - Material->transmission.transmission_factor
                        : (Material->alpha_mode == cgltf_alpha_mode_blend ? 0.65f : 1.0f));
                if (Material->alpha_mode == cgltf_alpha_mode_blend || Transmission)
                    glb->hasBlendedMaterials = 1;
            }
        }
    }
    model->glbData = glb;

    cgltf_free(data);
}
