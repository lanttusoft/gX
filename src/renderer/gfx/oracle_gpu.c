/**********************************************************************************************
*
*   [gX: src/renderer/gfx/oracle_gpu.c]
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


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../gfx.h"
#include "oracle_gpu.h"
#include "boilerplate.h"
#include "gl_level.h"
#include "model.h"

#include "../../camera.h"
#include "../../common/log.h"
#include "../../common/path.h"
#include "../../main.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_gl.h"
#include "../../rhi/batch.h"
#include "../../rhi/rhi_buf.h"
#include "../../rhi/rhi_tex.h"
#include "../../rhi/rhi_matrix.h"
#include "../../wrappers/wrappers.h"
#include "../../gxmath/constants.h"
#include "../../gxmath/matrix.h"
#include "../../gxmath/vector3.h"

#define GX_ORACLE_GPU_MAX_POINT_LIGHTS 32
#define GX_ORACLE_GPU_MAX_ALBEDO_TEXTURES 32
#define GX_ORACLE_GPU_MAX_ALBEDO_DIMENSION 1024

typedef struct gX_RendererGFX_OracleGPU_Triangle {
    gX_F32 p0[4];
    gX_F32 p1[4];
    gX_F32 p2[4];
    gX_F32 n0[4];
    gX_F32 n1[4];
    gX_F32 n2[4];
    gX_F32 albedo[4];
    gX_F32 material[4];
    gX_F32 uv0[4];
    gX_F32 uv1[4];
    gX_F32 uv2[4];
    gX_F32 textureUV[4];
} gX_RendererGFX_OracleGPU_Triangle;

typedef struct gX_RendererGFX_OracleGPU_Texture {
    gX_U32 id;
    gX_U32 layer;
    gX_U32 width;
    gX_U32 height;
    gX_I32 wrapS;
    gX_I32 wrapT;
} gX_RendererGFX_OracleGPU_Texture;

//opencode: Paper 1 M7 — CPU-built BVH nodes and leaf indices mirror the
//   std430 layouts consumed by oracle_gpu.comp.
typedef struct gX_RendererGFX_OracleGPU_BvhNode {
    gX_F32 boundsMin[4];
    gX_F32 boundsMax[4];
    gX_I32 data[4];
} gX_RendererGFX_OracleGPU_BvhNode;

static gX_I1 gX_OracleGPU_HookOn(gX_I0)
{
    const gX_I8 *hook = getenv("GX_T_ORACLE_GPU");
    return hook && hook[0] != '0';
}

//opencode: signatures invalidate progressive samples on all authored inputs
//   and keep immutable loaded-mesh BVH uploads out of unchanged preview frames.
static gX_U64 gX_OracleGPU_HashData(gX_U64 hash, const gX_I0 *data, size_t size)
{
    const gX_U8 *bytes = (const gX_U8 *)data;
    for (size_t index = 0; index < size; index++) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static gX_U64 gX_OracleGPU_HashValue(gX_U64 hash, const gX_I0 *value, size_t size)
{
    return gX_OracleGPU_HashData(hash, value, size);
}

static gX_I32 gX_OracleGPU_BounceCount(gX_I0)
{
    const gX_I8 *hook = getenv("GX_T_ORACLE_GPU_BOUNCES");
    gX_I32 count = hook ? (gX_I32)atoi((const char *)hook) : 1;
    if (count < 1) count = 1;
    if (count > 2) count = 2;
    return count;
}

static gX_I1 gX_OracleGPU_ReflectionOn(gX_I0)
{
    const gX_I8 *hook = getenv("GX_T_ORACLE_GPU_REFLECTION");
    return gX_OracleGPU_HookOn() && hook && hook[0] != '0';
}

static gX_F32v3 gX_OracleGPU_Normalize(gX_F32v3 value)
{
    gX_F32 length = sqrtf(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length < 1.0e-12f) return (gX_F32v3){0.0f, 1.0f, 0.0f};
    return (gX_F32v3){value.x / length, value.y / length, value.z / length};
}

static gX_F32v3 gX_OracleGPU_TransformDirection(gX_F32v3 value, gX_Matrix matrix)
{
    return (gX_F32v3){
        matrix.m0 * value.x + matrix.m4 * value.y + matrix.m8 * value.z,
        matrix.m1 * value.x + matrix.m5 * value.y + matrix.m9 * value.z,
        matrix.m2 * value.x + matrix.m6 * value.y + matrix.m10 * value.z
    };
}

static gX_I8 *gX_OracleGPU_ReadFile(const gX_I8 *path)
{
    FILE *file = fopen((const char *)path, "rb");
    gX_I64 length;
    gX_I8 *source;
    size_t readLength;
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    length = (gX_I64)ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    source = (gX_I8 *)malloc((size_t)length + 1u);
    if (!source) { fclose(file); return NULL; }
    readLength = fread(source, 1, (size_t)length, file);
    source[readLength] = '\0';
    fclose(file);
    return source;
}

static gX_Matrix gX_OracleGPU_ModelMatrix(const gX_RendererGFX_Model *model)
{
    gX_Matrix scale = gX_Matrix_Scale(model->params.scale.x,
        model->params.scale.y, model->params.scale.z);
    gX_Matrix rotation = gX_Matrix_RotateXYZ((gX_F32v3){
        model->params.rotation.x * gX_DEG2RAD,
        model->params.rotation.y * gX_DEG2RAD,
        model->params.rotation.z * gX_DEG2RAD
    });
    gX_Matrix translation = gX_Matrix_Translate(model->params.pos.x,
        model->params.pos.y, model->params.pos.z);
    gX_Matrix result = gX_Matrix_Multiply(scale, rotation);
    return gX_Matrix_Multiply(result, translation);
}

static gX_Material *gX_OracleGPU_MaterialForMesh(
    const gX_RendererGFX_Model *model, gX_I32 meshIndex)
{
    gX_Model source = model->model;
    gX_I32 materialIndex = source.meshMaterial
        ? source.meshMaterial[meshIndex] : 0;
    if (!source.materials || materialIndex < 0
        || materialIndex >= source.materialCount) return NULL;
    return &source.materials[materialIndex];
}

static gX_U32 gX_OracleGPU_AlbedoTextureId(
    const gX_RendererGFX_Model *model, const gX_Material *material,
    gX_I32 meshIndex)
{
    gX_U32 textureId = material && material->maps
        ? material->maps[gX_MATERIAL_MAP_ALBEDO].texture.id : 0;
    if (meshIndex >= 0 && meshIndex < gX_GFX_MODEL_MESH_MASK
        && (model->params.materialMeshMask[meshIndex]
            & gX_GFX_MMASK_ALBEDO_MAP)
        && model->params.effectiveAlbedoMap[meshIndex] > 0)
        textureId = (gX_U32)model->params.effectiveAlbedoMap[meshIndex];
    if (textureId == gX_RHI_Tex_GetDefault()) return 0;
    return textureId;
}

static gX_F32v3 gX_OracleGPU_Albedo(const gX_RendererGFX_Model *model,
    const gX_Material *material, gX_I32 meshIndex)
{
    gX_Color base = (gX_Color){200, 200, 200, 255};
    gX_Color tint;
    if (material && material->maps)
        base = material->maps[gX_MATERIAL_MAP_ALBEDO].color;
    tint = model->params.useTopTint
        ? (gX_Color){255, 255, 255, 255} : model->params.albedoTint;
    if (meshIndex >= 0 && meshIndex < gX_GFX_MODEL_MESH_MASK
        && (model->params.materialMeshMask[meshIndex]
            & gX_GFX_MMASK_ALBEDO))
        tint = model->params.effectiveAlbedo[meshIndex];
    gX_Color color = gX_RendererGFX_AlbedoTint_Apply(
        base,
        model->params.albedo, tint);
    return (gX_F32v3){
        (gX_F32)color.r / 255.0f,
        (gX_F32)color.g / 255.0f,
        (gX_F32)color.b / 255.0f
    };
}

static gX_I1 gX_OracleGPU_TextureQuery(gX_U32 textureId,
    gX_RendererGFX_OracleGPU_Texture *texture)
{
    GLint activeTexture = GL_TEXTURE0;
    GLint previousTexture = 0;
    GLint width = 0;
    GLint height = 0;
    GLint internalFormat = 0;
    GLint wrapS = GL_REPEAT;
    GLint wrapT = GL_REPEAT;
    gX_U32 target = gX_RHI_Tex_GetTarget(textureId);
    if (target != 0 && target != GL_TEXTURE_2D) return 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
        &internalFormat);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrapS);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &wrapT);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
    glActiveTexture((GLenum)activeTexture);
    if (width <= 0 || height <= 0 || internalFormat != GL_RGBA8) return 0;
    texture->id = textureId;
    texture->width = (gX_U32)width;
    texture->height = (gX_U32)height;
    texture->wrapS = wrapS == GL_CLAMP_TO_EDGE ? 1
        : (wrapS == GL_MIRRORED_REPEAT ? 2 : 0);
    texture->wrapT = wrapT == GL_CLAMP_TO_EDGE ? 1
        : (wrapT == GL_MIRRORED_REPEAT ? 2 : 0);
    return 1;
}

static gX_U32 gX_OracleGPU_TextureLayer(
    const gX_RendererGFX_OracleGPU_Texture *textures, gX_U32 textureCount,
    gX_U32 textureId)
{
    for (gX_U32 index = 0; index < textureCount; index++)
        if (textures[index].id == textureId) return textures[index].layer;
    return 0;
}

static gX_I1 gX_OracleGPU_CollectTextures(gX_RendererGFX *gfx,
    gX_RendererGFX_OracleGPU_Texture *textures, gX_U32 *textureCount,
    gX_U32 *arrayWidth, gX_U32 *arrayHeight)
{
    *textureCount = 0;
    *arrayWidth = 1;
    *arrayHeight = 1;
    GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, model) {
        if (!model->loaded || model->asyncLoading || !model->model.meshes) continue;
        for (gX_I32 meshIndex = 0; meshIndex < model->model.meshCount; meshIndex++) {
            gX_Material *material = gX_OracleGPU_MaterialForMesh(model, meshIndex);
            gX_U32 textureId = gX_OracleGPU_AlbedoTextureId(model, material,
                meshIndex);
            gX_RendererGFX_OracleGPU_Texture texture = {0};
            if (!model->model.meshes[meshIndex].texcoords || textureId == 0)
                continue;
            if (gX_OracleGPU_TextureLayer(textures, *textureCount, textureId) > 0)
                continue;
            if (*textureCount >= GX_ORACLE_GPU_MAX_ALBEDO_TEXTURES) continue;
            if (!gX_OracleGPU_TextureQuery(textureId, &texture)) continue;
            if (texture.width > GX_ORACLE_GPU_MAX_ALBEDO_DIMENSION
                || texture.height > GX_ORACLE_GPU_MAX_ALBEDO_DIMENSION)
                continue;
            texture.layer = *textureCount + 1u;
            textures[*textureCount] = texture;
            *textureCount += 1u;
            *arrayWidth = (*arrayWidth > texture.width)
                ? *arrayWidth : texture.width;
            *arrayHeight = (*arrayHeight > texture.height)
                ? *arrayHeight : texture.height;
        }
    }
    return 1;
}

static gX_U32 gX_OracleGPU_CreateTextureArray(
    const gX_RendererGFX_OracleGPU_Texture *textures, gX_U32 textureCount,
    gX_U32 arrayWidth, gX_U32 arrayHeight)
{
    GLint activeTexture = GL_TEXTURE0;
    GLint previousTexture = 0;
    gX_U32 textureArray = 0;
    gX_I1 success = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &previousTexture);
    textureArray = gX_RHI_Tex_Gen(gX_RHI_TEXTURE_2D_ARRAY,
        (const gX_I8 *)"oracle_albedo");
    if (textureArray == 0) goto cleanup;
    if (gX_RHI_Tex_Storage3D(textureArray, gX_RHI_TEXTURE_2D_ARRAY, 1,
            gX_RHI_RGBA8, arrayWidth, arrayHeight, textureCount + 1u) < 0)
        goto cleanup;
    gX_RHI_Tex_Param(textureArray, gX_RHI_TEXTURE_2D_ARRAY,
        GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gX_RHI_Tex_Param(textureArray, gX_RHI_TEXTURE_2D_ARRAY,
        GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gX_RHI_Tex_Param(textureArray, gX_RHI_TEXTURE_2D_ARRAY,
        GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gX_RHI_Tex_Param(textureArray, gX_RHI_TEXTURE_2D_ARRAY,
        GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    for (gX_U32 index = 0; index < textureCount; index++) {
        const gX_RendererGFX_OracleGPU_Texture *texture = &textures[index];
        glCopyImageSubData(texture->id, GL_TEXTURE_2D, 0, 0, 0, 0,
            textureArray, GL_TEXTURE_2D_ARRAY, 0, 0, 0,
            (GLint)texture->layer, (GLsizei)texture->width,
            (GLsizei)texture->height, 1);
    }
    success = 1;
cleanup:
    glBindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)previousTexture);
    glActiveTexture((GLenum)activeTexture);
    if (!success) {
        if (textureArray > 0) gX_RHI_Tex_Delete(textureArray);
        gX_TraceLog_GFX(gX_LOG_ERROR, "Oracle texture array failed");
        return 0;
    }
    return textureArray;
}

static gX_U64 gX_OracleGPU_SceneSignature(gX_RendererGFX *gfx)
{
    gX_U64 hash = 14695981039346656037ULL;
    GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, model) {
        gX_Model source = model->model;
        size_t pathLength = 0;
        hash = gX_OracleGPU_HashValue(hash, &model->loaded, sizeof(model->loaded));
        while (pathLength < sizeof(model->path) && model->path[pathLength])
            pathLength++;
        hash = gX_OracleGPU_HashData(hash, model->path, pathLength);
        hash = gX_OracleGPU_HashValue(hash, &model->params.pos, sizeof(model->params.pos));
        hash = gX_OracleGPU_HashValue(hash, &model->params.rotation, sizeof(model->params.rotation));
        hash = gX_OracleGPU_HashValue(hash, &model->params.scale, sizeof(model->params.scale));
        hash = gX_OracleGPU_HashValue(hash, &model->params.metalness,
            sizeof(model->params.metalness));
        hash = gX_OracleGPU_HashValue(hash, &model->params.roughness,
            sizeof(model->params.roughness));
        hash = gX_OracleGPU_HashValue(hash, &model->params.albedoMapFactor,
            sizeof(model->params.albedoMapFactor));
        hash = gX_OracleGPU_HashValue(hash, &model->params.albedoMapScale,
            sizeof(model->params.albedoMapScale));
        hash = gX_OracleGPU_HashValue(hash, &model->params.albedoMapOffset,
            sizeof(model->params.albedoMapOffset));
        if (!model->loaded || model->asyncLoading) continue;
        hash = gX_OracleGPU_HashValue(hash, &source.meshes, sizeof(source.meshes));
        hash = gX_OracleGPU_HashValue(hash, &source.meshCount, sizeof(source.meshCount));
        if (!source.meshes) continue;
        for (gX_I32 meshIndex = 0; meshIndex < source.meshCount; meshIndex++) {
            gX_Mesh *mesh = &source.meshes[meshIndex];
            gX_Material *material = gX_OracleGPU_MaterialForMesh(model,
                meshIndex);
            gX_F32v3 albedo = gX_OracleGPU_Albedo(model, material, meshIndex);
            gX_U32 textureId = gX_OracleGPU_AlbedoTextureId(model, material,
                meshIndex);
            hash = gX_OracleGPU_HashValue(hash, &mesh->vertices, sizeof(mesh->vertices));
            hash = gX_OracleGPU_HashValue(hash, &mesh->texcoords,
                sizeof(mesh->texcoords));
            hash = gX_OracleGPU_HashValue(hash, &mesh->normals, sizeof(mesh->normals));
            hash = gX_OracleGPU_HashValue(hash, &mesh->indices, sizeof(mesh->indices));
            hash = gX_OracleGPU_HashValue(hash, &textureId, sizeof(textureId));
            hash = gX_OracleGPU_HashValue(hash, &albedo, sizeof(albedo));
            hash = gX_OracleGPU_HashValue(hash, &mesh->vertexCount,
                sizeof(mesh->vertexCount));
            hash = gX_OracleGPU_HashValue(hash, &mesh->triangleCount,
                sizeof(mesh->triangleCount));
        }
    }
    return hash;
}

static gX_U64 gX_OracleGPU_SampleSignature(gX_RendererGFX *gfx,
    gX_U64 sceneSignature)
{
    gX_U64 hash = 14695981039346656037ULL;
    hash = gX_OracleGPU_HashValue(hash, &sceneSignature, sizeof(sceneSignature));
    hash = gX_OracleGPU_HashValue(hash, &camera.camera3D.position,
        sizeof(camera.camera3D.position));
    hash = gX_OracleGPU_HashValue(hash, &camera.camera3D.target,
        sizeof(camera.camera3D.target));
    hash = gX_OracleGPU_HashValue(hash, &camera.camera3D.up,
        sizeof(camera.camera3D.up));
    hash = gX_OracleGPU_HashValue(hash, &camera.camera3D.fovy,
        sizeof(camera.camera3D.fovy));
    hash = gX_OracleGPU_HashValue(hash, &camera.camera3D.projection,
        sizeof(camera.camera3D.projection));
    hash = gX_OracleGPU_HashValue(hash, &maindata.renderWidth,
        sizeof(maindata.renderWidth));
    hash = gX_OracleGPU_HashValue(hash, &maindata.renderHeight,
        sizeof(maindata.renderHeight));
    hash = gX_OracleGPU_HashValue(hash, &gfx->state.exposure,
        sizeof(gfx->state.exposure));
    hash = gX_OracleGPU_HashValue(hash, &gfx->state.ENABLE_SHADOWS,
        sizeof(gfx->state.ENABLE_SHADOWS));
    hash = gX_OracleGPU_HashValue(hash, &gfx->state.ENABLE_POINT_SHADOWS,
        sizeof(gfx->state.ENABLE_POINT_SHADOWS));
    hash = gX_OracleGPU_HashValue(hash, &gfx->sky.color, sizeof(gfx->sky.color));
    GX_HEAP_FOREACH(gfx->lights, gX_RendererGFX_Light, light) {
        hash = gX_OracleGPU_HashValue(hash, &light->type, sizeof(light->type));
        hash = gX_OracleGPU_HashValue(hash, &light->sun, sizeof(light->sun));
        hash = gX_OracleGPU_HashValue(hash, &light->pos, sizeof(light->pos));
        hash = gX_OracleGPU_HashValue(hash, &light->color, sizeof(light->color));
        hash = gX_OracleGPU_HashValue(hash, &light->brightness, sizeof(light->brightness));
        hash = gX_OracleGPU_HashValue(hash, &light->diffuseFactor,
            sizeof(light->diffuseFactor));
        hash = gX_OracleGPU_HashValue(hash, &light->attenConst,
            sizeof(light->attenConst));
        hash = gX_OracleGPU_HashValue(hash, &light->attenLinear,
            sizeof(light->attenLinear));
        hash = gX_OracleGPU_HashValue(hash, &light->attenQuad,
            sizeof(light->attenQuad));
        hash = gX_OracleGPU_HashValue(hash, &light->castShadows,
            sizeof(light->castShadows));
    }
    hash = gX_OracleGPU_HashValue(hash, &gfx->sun.dir, sizeof(gfx->sun.dir));
    hash = gX_OracleGPU_HashValue(hash, &gfx->sun.color, sizeof(gfx->sun.color));
    hash = gX_OracleGPU_HashValue(hash, &gfx->sun.brightness,
        sizeof(gfx->sun.brightness));
    hash = gX_OracleGPU_HashValue(hash, &gfx->sun.diffuseFactor,
        sizeof(gfx->sun.diffuseFactor));
    hash = gX_OracleGPU_HashValue(hash, &gfx->sun.diffuseTint,
        sizeof(gfx->sun.diffuseTint));
    hash = gX_OracleGPU_HashValue(hash, &gfx->sun.castShadows,
        sizeof(gfx->sun.castShadows));
    return hash;
}

static gX_I1 gX_OracleGPU_Append(
    gX_RendererGFX_OracleGPU_Triangle **triangles,
    gX_U32 *amount,
    gX_U32 *capacity,
    gX_RendererGFX_OracleGPU_Triangle triangle)
{
    gX_U32 nextCapacity;
    gX_RendererGFX_OracleGPU_Triangle *grown;
    if (*amount >= *capacity) {
        nextCapacity = *capacity ? *capacity * 2u : 1024u;
        grown = (gX_RendererGFX_OracleGPU_Triangle *)realloc(*triangles,
            (size_t)nextCapacity * sizeof(*grown));
        if (!grown) return 0;
        *triangles = grown;
        *capacity = nextCapacity;
    }
    (*triangles)[*amount] = triangle;
    *amount += 1u;
    return 1;
}

static gX_F32 gX_OracleGPU_Centroid(
    const gX_RendererGFX_OracleGPU_Triangle *triangle, gX_I32 axis)
{
    gX_F32 value = triangle->p0[axis] + triangle->p1[axis] + triangle->p2[axis];
    return value / 3.0f;
}

static gX_I0 gX_OracleGPU_SortIndices(
    gX_RendererGFX_OracleGPU_Triangle *triangles,
    gX_U32 *indices, gX_U32 begin, gX_U32 end, gX_I32 axis)
{
    for (gX_U32 index = begin + 1u; index < end; index++) {
        gX_U32 value = indices[index];
        gX_F32 centroid = gX_OracleGPU_Centroid(&triangles[value], axis);
        gX_U32 cursor = index;
        while (cursor > begin) {
            gX_U32 previous = indices[cursor - 1u];
            if (gX_OracleGPU_Centroid(&triangles[previous], axis) <= centroid) break;
            indices[cursor] = previous;
            cursor--;
        }
        indices[cursor] = value;
    }
}

static gX_I32 gX_OracleGPU_BuildBvhNode(
    gX_RendererGFX_OracleGPU_Triangle *triangles,
    gX_U32 *indices, gX_U32 begin, gX_U32 end,
    gX_RendererGFX_OracleGPU_BvhNode *nodes, gX_U32 *nodeCount,
    gX_U32 nodeCapacity, gX_U32 *leafIndices, gX_U32 *leafCount,
    gX_U32 leafCapacity)
{
    gX_RendererGFX_OracleGPU_BvhNode *node;
    gX_F32 low[3] = {3.402823466e+38f, 3.402823466e+38f, 3.402823466e+38f};
    gX_F32 high[3] = {-3.402823466e+38f, -3.402823466e+38f, -3.402823466e+38f};
    gX_U32 amount = end - begin;
    gX_I32 axis = 0;
    gX_F32 extent;
    gX_I32 nodeIndex;

    if (amount == 0 || *nodeCount >= nodeCapacity) return -1;
    nodeIndex = (gX_I32)*nodeCount;
    node = &nodes[*nodeCount];
    *nodeCount += 1u;
    for (gX_U32 cursor = begin; cursor < end; cursor++) {
        gX_RendererGFX_OracleGPU_Triangle *triangle = &triangles[indices[cursor]];
        for (gX_I32 component = 0; component < 3; component++) {
            gX_F32 triangleLow = fminf(fminf(triangle->p0[component], triangle->p1[component]), triangle->p2[component]);
            gX_F32 triangleHigh = fmaxf(fmaxf(triangle->p0[component], triangle->p1[component]), triangle->p2[component]);
            low[component] = fminf(low[component], triangleLow);
            high[component] = fmaxf(high[component], triangleHigh);
        }
    }
    for (gX_I32 component = 0; component < 4; component++) {
        node->boundsMin[component] = component < 3 ? low[component] : 0.0f;
        node->boundsMax[component] = component < 3 ? high[component] : 0.0f;
    }
    node->data[0] = -1;
    node->data[1] = -1;
    node->data[2] = -1;
    node->data[3] = 0;
    if (amount <= 8u) {
        if (*leafCount + amount > leafCapacity) return -1;
        node->data[2] = (gX_I32)*leafCount;
        node->data[3] = (gX_I32)amount;
        for (gX_U32 cursor = begin; cursor < end; cursor++)
            leafIndices[(*leafCount)++] = indices[cursor];
        return nodeIndex;
    }
    extent = high[0] - low[0];
    if (high[1] - low[1] > extent) { axis = 1; extent = high[1] - low[1]; }
    if (high[2] - low[2] > extent) axis = 2;
    gX_OracleGPU_SortIndices(triangles, indices, begin, end, axis);
    {
        gX_U32 middle = begin + amount / 2u;
        gX_I32 left = gX_OracleGPU_BuildBvhNode(triangles, indices, begin, middle,
            nodes, nodeCount, nodeCapacity, leafIndices, leafCount, leafCapacity);
        gX_I32 right = gX_OracleGPU_BuildBvhNode(triangles, indices, middle, end,
            nodes, nodeCount, nodeCapacity, leafIndices, leafCount, leafCapacity);
        if (left < 0 || right < 0) return -1;
        node->data[0] = left;
        node->data[1] = right;
    }
    return nodeIndex;
}

static gX_I1 gX_OracleGPU_BuildBvh(
    gX_RendererGFX_OracleGPU_Triangle *triangles, gX_U32 triangleCount,
    gX_RendererGFX_OracleGPU_BvhNode **nodes, gX_U32 *nodeCount,
    gX_U32 **leafIndices, gX_U32 *leafCount)
{
    gX_U32 *indices;
    gX_U32 nodeCapacity;
    gX_U32 leafCapacity;
    gX_I32 root;
    if (!triangles || triangleCount == 0) return 0;
    nodeCapacity = triangleCount * 2u;
    leafCapacity = triangleCount;
    indices = (gX_U32 *)malloc((size_t)triangleCount * sizeof(*indices));
    *nodes = (gX_RendererGFX_OracleGPU_BvhNode *)malloc(
        (size_t)nodeCapacity * sizeof(**nodes));
    *leafIndices = (gX_U32 *)malloc((size_t)leafCapacity * sizeof(**leafIndices));
    if (!indices || !*nodes || !*leafIndices) {
        free(indices);
        free(*nodes);
        free(*leafIndices);
        *nodes = NULL;
        *leafIndices = NULL;
        return 0;
    }
    for (gX_U32 index = 0; index < triangleCount; index++) indices[index] = index;
    *nodeCount = 0;
    *leafCount = 0;
    root = gX_OracleGPU_BuildBvhNode(triangles, indices, 0, triangleCount,
        *nodes, nodeCount, nodeCapacity, *leafIndices, leafCount, leafCapacity);
    free(indices);
    if (root < 0) {
        free(*nodes);
        free(*leafIndices);
        *nodes = NULL;
        *leafIndices = NULL;
        *nodeCount = 0;
        *leafCount = 0;
        return 0;
    }
    return 1;
}

static gX_I1 gX_OracleGPU_AppendModel(
    const gX_RendererGFX_Model *model,
    gX_RendererGFX_OracleGPU_Triangle **triangles,
    gX_U32 *amount,
    gX_U32 *capacity,
    const gX_RendererGFX_OracleGPU_Texture *textures,
    gX_U32 textureCount, gX_U32 arrayWidth, gX_U32 arrayHeight)
{
    gX_Model source = model->model;
    gX_Material defaultMaterial = {0};
    gX_MaterialMap defaultMaps[12] = {0};
    gX_Matrix transform;
    gX_Matrix normalMatrix;

    defaultMaterial.maps = defaultMaps;
    defaultMaps[gX_MATERIAL_MAP_ALBEDO].color = (gX_Color){200, 200, 200, 255};
    if (!source.meshes) return 1;
    transform = gX_OracleGPU_ModelMatrix(model);
    normalMatrix = gX_Matrix_Transpose(gX_Matrix_Invert(transform));

    for (gX_I32 meshIndex = 0; meshIndex < source.meshCount; meshIndex++) {
        gX_Mesh *mesh = &source.meshes[meshIndex];
        gX_Material *material = gX_OracleGPU_MaterialForMesh(model, meshIndex);
        gX_U32 textureId = gX_OracleGPU_AlbedoTextureId(model, material,
            meshIndex);
        gX_U32 textureLayer = mesh->texcoords
            ? gX_OracleGPU_TextureLayer(textures, textureCount, textureId) : 0;
        gX_F32v3 albedo;
        if (!material) material = &defaultMaterial;
        albedo = gX_OracleGPU_Albedo(model, material, meshIndex);
        if (!mesh->vertices || mesh->vertexCount <= 0) continue;
        gX_U32 triangleCount = mesh->triangleCount > 0
            ? (gX_U32)mesh->triangleCount : (gX_U32)mesh->vertexCount / 3u;
        for (gX_U32 triangleIndex = 0; triangleIndex < triangleCount; triangleIndex++) {
            gX_U32 baseIndex = triangleIndex * 3u;
            gX_U32 index0 = mesh->indices ? mesh->indices[baseIndex] : baseIndex;
            gX_U32 index1 = mesh->indices ? mesh->indices[baseIndex + 1u] : baseIndex + 1u;
            gX_U32 index2 = mesh->indices ? mesh->indices[baseIndex + 2u] : baseIndex + 2u;
            gX_F32v3 local0;
            gX_F32v3 local1;
            gX_F32v3 local2;
            gX_F32v3 world0;
            gX_F32v3 world1;
            gX_F32v3 world2;
            gX_F32v3 normal0;
            gX_F32v3 normal1;
            gX_F32v3 normal2;
            gX_F32v3 faceNormal;
            gX_F32v2 uv0 = {0};
            gX_F32v2 uv1 = {0};
            gX_F32v2 uv2 = {0};
            gX_RendererGFX_OracleGPU_Triangle triangle = {0};

            if (index0 >= (gX_U32)mesh->vertexCount
                || index1 >= (gX_U32)mesh->vertexCount
                || index2 >= (gX_U32)mesh->vertexCount) continue;
            local0 = (gX_F32v3){mesh->vertices[index0 * 3u], mesh->vertices[index0 * 3u + 1u], mesh->vertices[index0 * 3u + 2u]};
            local1 = (gX_F32v3){mesh->vertices[index1 * 3u], mesh->vertices[index1 * 3u + 1u], mesh->vertices[index1 * 3u + 2u]};
            local2 = (gX_F32v3){mesh->vertices[index2 * 3u], mesh->vertices[index2 * 3u + 1u], mesh->vertices[index2 * 3u + 2u]};
            world0 = gX_F32v3_Transform(local0, transform);
            world1 = gX_F32v3_Transform(local1, transform);
            world2 = gX_F32v3_Transform(local2, transform);
            faceNormal = gX_OracleGPU_Normalize(gX_F32v3_Cross(
                gX_F32v3_Sub(world1, world0), gX_F32v3_Sub(world2, world0)));
            if (mesh->normals) {
                normal0 = gX_OracleGPU_Normalize(gX_OracleGPU_TransformDirection(
                    (gX_F32v3){mesh->normals[index0 * 3u], mesh->normals[index0 * 3u + 1u], mesh->normals[index0 * 3u + 2u]}, normalMatrix));
                normal1 = gX_OracleGPU_Normalize(gX_OracleGPU_TransformDirection(
                    (gX_F32v3){mesh->normals[index1 * 3u], mesh->normals[index1 * 3u + 1u], mesh->normals[index1 * 3u + 2u]}, normalMatrix));
                normal2 = gX_OracleGPU_Normalize(gX_OracleGPU_TransformDirection(
                    (gX_F32v3){mesh->normals[index2 * 3u], mesh->normals[index2 * 3u + 1u], mesh->normals[index2 * 3u + 2u]}, normalMatrix));
            } else {
                normal0 = faceNormal;
                normal1 = faceNormal;
                normal2 = faceNormal;
            }
            if (mesh->texcoords) {
                uv0 = (gX_F32v2){
                    mesh->texcoords[index0 * 2u] * model->params.albedoMapScale.x
                        - model->params.albedoMapOffset.x,
                    mesh->texcoords[index0 * 2u + 1u] * model->params.albedoMapScale.y
                        - model->params.albedoMapOffset.y};
                uv1 = (gX_F32v2){
                    mesh->texcoords[index1 * 2u] * model->params.albedoMapScale.x
                        - model->params.albedoMapOffset.x,
                    mesh->texcoords[index1 * 2u + 1u] * model->params.albedoMapScale.y
                        - model->params.albedoMapOffset.y};
                uv2 = (gX_F32v2){
                    mesh->texcoords[index2 * 2u] * model->params.albedoMapScale.x
                        - model->params.albedoMapOffset.x,
                    mesh->texcoords[index2 * 2u + 1u] * model->params.albedoMapScale.y
                        - model->params.albedoMapOffset.y};
            }
            memcpy(triangle.p0, (gX_F32[4]){world0.x, world0.y, world0.z, 1.0f}, sizeof(triangle.p0));
            memcpy(triangle.p1, (gX_F32[4]){world1.x, world1.y, world1.z, 1.0f}, sizeof(triangle.p1));
            memcpy(triangle.p2, (gX_F32[4]){world2.x, world2.y, world2.z, 1.0f}, sizeof(triangle.p2));
            memcpy(triangle.n0, (gX_F32[4]){normal0.x, normal0.y, normal0.z, 0.0f}, sizeof(triangle.n0));
            memcpy(triangle.n1, (gX_F32[4]){normal1.x, normal1.y, normal1.z, 0.0f}, sizeof(triangle.n1));
            memcpy(triangle.n2, (gX_F32[4]){normal2.x, normal2.y, normal2.z, 0.0f}, sizeof(triangle.n2));
            memcpy(triangle.albedo, (gX_F32[4]){albedo.x, albedo.y, albedo.z, 1.0f}, sizeof(triangle.albedo));
            memcpy(triangle.material, (gX_F32[4]){
                model->params.metalness, model->params.roughness,
                (gX_F32)textureLayer,
                model->params.albedoMapFactor},
                sizeof(triangle.material));
            memcpy(triangle.uv0, (gX_F32[4]){uv0.x, uv0.y, 0.0f, 0.0f},
                sizeof(triangle.uv0));
            memcpy(triangle.uv1, (gX_F32[4]){uv1.x, uv1.y, 0.0f, 0.0f},
                sizeof(triangle.uv1));
            memcpy(triangle.uv2, (gX_F32[4]){uv2.x, uv2.y, 0.0f, 0.0f},
                sizeof(triangle.uv2));
            if (textureLayer > 0) {
                for (gX_U32 textureIndex = 0; textureIndex < textureCount;
                     textureIndex++) {
                    const gX_RendererGFX_OracleGPU_Texture *texture =
                        &textures[textureIndex];
                    if (texture->layer != textureLayer) continue;
                    triangle.uv0[2] = (gX_F32)texture->wrapS;
                    triangle.uv0[3] = (gX_F32)texture->wrapT;
                    triangle.textureUV[0] = texture->width > 1
                        ? (gX_F32)(texture->width - 1u) / (gX_F32)arrayWidth : 0.0f;
                    triangle.textureUV[1] = texture->height > 1
                        ? (gX_F32)(texture->height - 1u) / (gX_F32)arrayHeight : 0.0f;
                    triangle.textureUV[2] = 0.5f / (gX_F32)arrayWidth;
                    triangle.textureUV[3] = 0.5f / (gX_F32)arrayHeight;
                    break;
                }
            }
            if (!gX_OracleGPU_Append(triangles, amount, capacity, triangle)) return 0;
        }
    }
    return 1;
}

static gX_RendererGFX_OracleGPU_Triangle *gX_OracleGPU_BuildTriangles(
    gX_RendererGFX *gfx, gX_U32 *amount,
    const gX_RendererGFX_OracleGPU_Texture *textures,
    gX_U32 textureCount, gX_U32 arrayWidth, gX_U32 arrayHeight)
{
    gX_RendererGFX_OracleGPU_Triangle *triangles = NULL;
    gX_U32 capacity = 0;
    *amount = 0;
    GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, model) {
        if (!model->loaded) continue;
        if (!gX_OracleGPU_AppendModel(model, &triangles, amount, &capacity,
                textures, textureCount, arrayWidth, arrayHeight)) {
            free(triangles);
            *amount = 0;
            return NULL;
        }
    }
    return triangles;
}

static gX_I1 gX_OracleGPU_EnsureProgram(gX_RendererGFX *gfx)
{
    gX_RendererGFX_OracleGPU *gpu = &gfx->oracleGpu;
    gX_I8 path[gX_GFX_PATH_BUFFER];
    gX_I8 errorLog[1024] = {0};
    gX_I8 *source;
    if (gpu->initialized) return gpu->program != 0;
    gpu->initialized = 1;
    if (!gX_RendererGFX_OracleGPU_Available()) return 0;
    snprintf((char *)path, sizeof(path), "%s", GX_PATH("shader", "oracle_gpu.comp"));
    source = gX_OracleGPU_ReadFile(path);
    if (!source) {
        gX_TraceLog_GFX(gX_LOG_ERROR, "Oracle GPU shader not found: %s", path);
        return 0;
    }
    gpu->program = gX_RHI_Shader_Compile_Compute(source, "oracle_gpu", errorLog, sizeof(errorLog));
    free(source);
    if (!gpu->program) {
        gX_TraceLog_GFX(gX_LOG_ERROR, "Oracle GPU compute shader: %s", errorLog);
        return 0;
    }
    gpu->resolutionLoc = gX_RHI_GetUniformLocation(gpu->program, "resolution");
    gpu->cameraPositionLoc = gX_RHI_GetUniformLocation(gpu->program, "cameraPosition");
    gpu->cameraForwardLoc = gX_RHI_GetUniformLocation(gpu->program, "cameraForward");
    gpu->cameraRightLoc = gX_RHI_GetUniformLocation(gpu->program, "cameraRight");
    gpu->cameraUpLoc = gX_RHI_GetUniformLocation(gpu->program, "cameraUp");
    gpu->tanHalfFovLoc = gX_RHI_GetUniformLocation(gpu->program, "tanHalfFov");
    gpu->cameraProjectionLoc = gX_RHI_GetUniformLocation(gpu->program,
        "cameraProjection");
    gpu->orthoHalfHeightLoc = gX_RHI_GetUniformLocation(gpu->program,
        "orthoHalfHeight");
    gpu->aspectLoc = gX_RHI_GetUniformLocation(gpu->program, "aspect");
    gpu->pointCountLoc = gX_RHI_GetUniformLocation(gpu->program, "pointCount");
    gpu->pointPositionsLoc = gX_RHI_GetUniformLocation(gpu->program, "pointPositions");
    gpu->pointRadianceLoc = gX_RHI_GetUniformLocation(gpu->program, "pointRadiance");
    gpu->pointAttenLoc = gX_RHI_GetUniformLocation(gpu->program, "pointAtten");
    gpu->sunDirectionLoc = gX_RHI_GetUniformLocation(gpu->program, "sunDirection");
    gpu->sunRadianceLoc = gX_RHI_GetUniformLocation(gpu->program, "sunRadiance");
    gpu->sunCastShadowsLoc = gX_RHI_GetUniformLocation(gpu->program, "sunCastShadows");
    gpu->environmentColorLoc = gX_RHI_GetUniformLocation(gpu->program,
        "environmentColor");
    gpu->exposureLoc = gX_RHI_GetUniformLocation(gpu->program, "exposure");
    gpu->bounceCountLoc = gX_RHI_GetUniformLocation(gpu->program, "bounceCount");
    gpu->bvhNodeCountLoc = gX_RHI_GetUniformLocation(gpu->program, "bvhNodeCount");
    gpu->reflectionModeLoc = gX_RHI_GetUniformLocation(gpu->program, "reflectionMode");
    gpu->sampleIndexLoc = gX_RHI_GetUniformLocation(gpu->program, "sampleIndex");
    gpu->previewModeLoc = gX_RHI_GetUniformLocation(gpu->program, "previewMode");
    gpu->albedoTextureArrayLoc = gX_RHI_GetUniformLocation(gpu->program,
        "albedoTextureArray");
    return 1;
}

gX_I0 gX_RendererGFX_OracleGPU_Init(gX_RendererGFX *gfx)
{
    memset(&gfx->oracleGpu, 0, sizeof(gfx->oracleGpu));
}

gX_I1 gX_RendererGFX_OracleGPU_Enabled(gX_I0)
{
    return gX_OracleGPU_HookOn() && gX_RendererGFX_OracleGPU_Available()
        ? 1 : 0;
}

//opencode: one capability predicate shared by the editor and compute pass.
gX_I1 gX_RendererGFX_OracleGPU_Available(gX_I0)
{
    return gX_GL.hasCompute && gX_GL.hasSSBO
        && gX_GL.level >= GX_GL_LEVEL_430 && gX_GL.useLevel >= GX_GL_LEVEL_430;
}

//opencode: allocate a fixed-width HDR tracing surface while preserving the
//   active camera aspect ratio (320x180 at 16:9).
static gX_I1 gX_OracleGPU_EnsurePreviewTargets(gX_RendererGFX *gfx)
{
    gX_RendererGFX_OracleGPU *gpu = &gfx->oracleGpu;
    gX_I1 sideBySide = gpu->previewMode == GX_GFX_RAY_PREVIEW_SIDE_BY_SIDE;
    gX_U32 width = sideBySide ? 160u : 320u;
    gX_U32 displayWidth;
    gX_U32 height;
    if (maindata.renderWidth <= 0 || maindata.renderHeight <= 0) return 0;
    displayWidth = (gX_U32)maindata.renderWidth / (sideBySide ? 2u : 1u);
    if (displayWidth == 0) return 0;
    height = (gX_U32)lroundf((gX_F32)width * (gX_F32)maindata.renderHeight
        / (gX_F32)displayWidth);
    if (height < 96) height = 96;
    if (height > 384) height = 384;
    if ((gpu->previewTexture > 0 || gpu->previewAccumTexture > 0)
        && (gpu->previewWidth != width || gpu->previewHeight != height)) {
        if (gpu->previewTexture > 0) {
            gX_Texture_RT_Untrack(gpu->previewTexture);
            gX_RHI_Tex_Delete(gpu->previewTexture);
        }
        gpu->previewTexture = 0;
        if (gpu->previewAccumTexture > 0)
            gX_RHI_Tex_Delete(gpu->previewAccumTexture);
        gpu->previewAccumTexture = 0;
        gpu->previewSamples = 0;
    }
    if (gpu->previewAccumTexture == 0) {
        gpu->previewAccumTexture = gX_RHI_Tex_Gen(gX_RHI_TEXTURE_2D,
            (const gX_I8 *)"oracle_ray_accumulation");
        if (gpu->previewAccumTexture == 0) return 0;
        if (gX_RHI_Tex_Storage2D(gpu->previewAccumTexture, gX_RHI_TEXTURE_2D, 1,
                gX_RHI_RGBA16F, width, height) < 0) {
            gX_RHI_Tex_Delete(gpu->previewAccumTexture);
            gpu->previewAccumTexture = 0;
            return 0;
        }
    }
    if (gpu->previewTexture == 0) {
        gpu->previewTexture = gX_RHI_Tex_Gen(gX_RHI_TEXTURE_2D,
            (const gX_I8 *)"oracle_ray_preview");
        if (gpu->previewTexture == 0) return 0;
        if (gX_RHI_Tex_Storage2D(gpu->previewTexture, gX_RHI_TEXTURE_2D, 1,
                gX_RHI_RGBA16F, width, height) < 0) {
            gX_RHI_Tex_Delete(gpu->previewTexture);
            gpu->previewTexture = 0;
            return 0;
        }
        gX_RHI_Tex_Param(gpu->previewTexture, gX_RHI_TEXTURE_2D,
            gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_LINEAR);
        gX_RHI_Tex_Param(gpu->previewTexture, gX_RHI_TEXTURE_2D,
            gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_LINEAR);
        gX_RHI_Tex_Param(gpu->previewTexture, gX_RHI_TEXTURE_2D,
            gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_EDGE);
        gX_RHI_Tex_Param(gpu->previewTexture, gX_RHI_TEXTURE_2D,
            gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_EDGE);
        gX_Texture_RT_Track(gpu->previewTexture);
        gpu->previewWidth = width;
        gpu->previewHeight = height;
        gpu->previewSamples = 0;
    }
    if (gpu->previewDisplayTarget.id > 0
        && (gpu->previewDisplayWidth != displayWidth
            || gpu->previewDisplayHeight != maindata.renderHeight)) {
        gX_RenderTexture_Unload(gpu->previewDisplayTarget);
        memset(&gpu->previewDisplayTarget, 0, sizeof(gpu->previewDisplayTarget));
    }
    if (gpu->previewDisplayTarget.id == 0) {
        gpu->previewDisplayTarget = gX_RenderTexture_Load(
            displayWidth, (gX_U32)maindata.renderHeight);
        if (gpu->previewDisplayTarget.id == 0) return 0;
        gX_Texture_SetFilter(gpu->previewDisplayTarget.texture,
            gX_TEXTURE_FILTER_BILINEAR);
        gpu->previewDisplayWidth = displayWidth;
        gpu->previewDisplayHeight = (gX_U32)maindata.renderHeight;
    }
    if (sideBySide && gpu->comparisonRasterTarget.id > 0
        && (gpu->comparisonRasterWidth != (gX_U32)maindata.renderWidth
            || gpu->comparisonRasterHeight != (gX_U32)maindata.renderHeight)) {
        gX_RenderTexture_Unload(gpu->comparisonRasterTarget);
        memset(&gpu->comparisonRasterTarget, 0,
            sizeof(gpu->comparisonRasterTarget));
    }
    if (sideBySide && gpu->comparisonRasterTarget.id == 0) {
        gpu->comparisonRasterTarget = gX_RenderTexture_Load(
            (gX_U32)maindata.renderWidth, (gX_U32)maindata.renderHeight);
        if (gpu->comparisonRasterTarget.id == 0) return 0;
        gX_Texture_SetFilter(gpu->comparisonRasterTarget.texture,
            gX_TEXTURE_FILTER_BILINEAR);
        gpu->comparisonRasterWidth = (gX_U32)maindata.renderWidth;
        gpu->comparisonRasterHeight = (gX_U32)maindata.renderHeight;
    } else if (!sideBySide && gpu->comparisonRasterTarget.id > 0) {
        gX_RenderTexture_Unload(gpu->comparisonRasterTarget);
        memset(&gpu->comparisonRasterTarget, 0,
            sizeof(gpu->comparisonRasterTarget));
        gpu->comparisonRasterWidth = 0;
        gpu->comparisonRasterHeight = 0;
    }
    return 1;
}

//opencode: retain audit compositing or dispatch the independent progressive preview.
gX_I0 gX_RendererGFX_OracleGPU_Draw(gX_RendererGFX *gfx)
{
    gX_RendererGFX_OracleGPU *gpu = &gfx->oracleGpu;
    gX_RendererGFX_OracleGPU_Triangle *triangles = NULL;
    gX_RendererGFX_OracleGPU_BvhNode *bvhNodes = NULL;
    gX_RendererGFX_OracleGPU_Texture textures[
        GX_ORACLE_GPU_MAX_ALBEDO_TEXTURES] = {{0}};
    gX_U32 *bvhIndices = NULL;
    gX_F32v4 pointPositions[GX_ORACLE_GPU_MAX_POINT_LIGHTS] = {{0}};
    gX_F32v4 pointRadiance[GX_ORACLE_GPU_MAX_POINT_LIGHTS] = {{0}};
    gX_F32v4 pointAtten[GX_ORACLE_GPU_MAX_POINT_LIGHTS] = {{0}};
    gX_F32v3 forward;
    gX_F32v3 right;
    gX_F32v3 up;
    gX_F32v3 sunDirection;
    gX_F32v3 sunRadiance;
    gX_F32 aspect;
    gX_F32 tanHalfFov;
    gX_U32 triangleCount = 0;
    gX_U32 textureCount = 0;
    gX_U32 arrayWidth = 1;
    gX_U32 arrayHeight = 1;
    gX_U32 newTextureArray = 0;
    gX_U32 bvhNodeCount = 0;
    gX_U32 bvhIndexCount = 0;
    gX_U32 pointCount = 0;
    gX_U32 width;
    gX_U32 height;
    gX_U32 targetTexture;
    gX_U32 previousAlbedoTexture;
    gX_U32 previousAlbedoTarget;
    gX_U32 albedoTextureUnit = gX_GFX_TEXUNIT_ORACLE_ALBEDO;
    GLint previousAlbedoArrayBinding = 0;
    GLint previousActiveTexture = GL_TEXTURE0;
    gX_U64 sceneSignature;
    gX_U64 sampleSignature;
    gX_I1 previewActive = gpu->previewMode != GX_GFX_RAY_PREVIEW_RASTER
        && gX_RendererGFX_OracleGPU_Available();

    if (!previewActive && !gX_RendererGFX_OracleGPU_Enabled()) return;
    if (!gfx->bloom.hdrScene.wrapper.loadingState.loaded) return;
    if (previewActive && !gX_OracleGPU_EnsurePreviewTargets(gfx)) return;
    if (!gX_OracleGPU_EnsureProgram(gfx)) return;
    sceneSignature = gX_OracleGPU_SceneSignature(gfx);
    if (!gpu->sceneReady || sceneSignature != gpu->sceneSignature) {
        gpu->previewSamples = 0;
        gpu->sampleSignature = 0;
        gX_OracleGPU_CollectTextures(gfx, textures, &textureCount,
            &arrayWidth, &arrayHeight);
        newTextureArray = gX_OracleGPU_CreateTextureArray(textures,
            textureCount, arrayWidth, arrayHeight);
        if (newTextureArray == 0) return;
        triangles = gX_OracleGPU_BuildTriangles(gfx, &triangleCount,
            textures, textureCount, arrayWidth, arrayHeight);
        if (!triangles || triangleCount == 0) {
            free(triangles);
            gX_RHI_Tex_Delete(newTextureArray);
            gpu->sceneReady = 0;
            return;
        }
        if (!gX_OracleGPU_BuildBvh(triangles, triangleCount,
                &bvhNodes, &bvhNodeCount, &bvhIndices, &bvhIndexCount)) {
            free(triangles);
            gX_RHI_Tex_Delete(newTextureArray);
            return;
        }
        if (gpu->triangleSsbo == 0)
            gpu->triangleSsbo = gX_RHI_Buffer_Create("oracle_gpu_triangles");
        if (gpu->bvhSsbo == 0)
            gpu->bvhSsbo = gX_RHI_Buffer_Create("oracle_gpu_bvh_nodes");
        if (gpu->bvhIndexSsbo == 0)
            gpu->bvhIndexSsbo = gX_RHI_Buffer_Create("oracle_gpu_bvh_indices");
        if (gpu->triangleSsbo == 0 || gpu->bvhSsbo == 0
            || gpu->bvhIndexSsbo == 0) {
            if (gpu->triangleSsbo > 0) gX_RHI_Buffer_Delete(gpu->triangleSsbo);
            if (gpu->bvhSsbo > 0) gX_RHI_Buffer_Delete(gpu->bvhSsbo);
            if (gpu->bvhIndexSsbo > 0) gX_RHI_Buffer_Delete(gpu->bvhIndexSsbo);
            gpu->triangleSsbo = 0;
            gpu->bvhSsbo = 0;
            gpu->bvhIndexSsbo = 0;
            gpu->triangleCapacity = 0;
            gpu->bvhNodeCapacity = 0;
            gpu->bvhIndexCapacity = 0;
            free(triangles);
            free(bvhNodes);
            free(bvhIndices);
            gX_RHI_Tex_Delete(newTextureArray);
            return;
        }
        gX_RHI_Buffer_Bind(gX_RHI_SHADER_STORAGE_BUFFER, gpu->triangleSsbo);
        if (gpu->triangleCapacity < triangleCount) {
            gX_RHI_Buffer_Data(gX_RHI_SHADER_STORAGE_BUFFER,
                triangleCount * (gX_U32)sizeof(*triangles), triangles,
                gX_RHI_DYNAMIC_DRAW);
            gpu->triangleCapacity = triangleCount;
        } else {
            gX_RHI_Buffer_SubData(gX_RHI_SHADER_STORAGE_BUFFER, 0,
                triangleCount * (gX_U32)sizeof(*triangles), triangles);
        }
        gpu->triangleCount = triangleCount;
        gX_RHI_Buffer_Bind(gX_RHI_SHADER_STORAGE_BUFFER, gpu->bvhSsbo);
        if (gpu->bvhNodeCapacity < bvhNodeCount) {
            gX_RHI_Buffer_Data(gX_RHI_SHADER_STORAGE_BUFFER,
                bvhNodeCount * (gX_U32)sizeof(*bvhNodes), bvhNodes,
                gX_RHI_DYNAMIC_DRAW);
            gpu->bvhNodeCapacity = bvhNodeCount;
        } else {
            gX_RHI_Buffer_SubData(gX_RHI_SHADER_STORAGE_BUFFER, 0,
                bvhNodeCount * (gX_U32)sizeof(*bvhNodes), bvhNodes);
        }
        gpu->bvhNodeCount = bvhNodeCount;
        gX_RHI_Buffer_Bind(gX_RHI_SHADER_STORAGE_BUFFER, gpu->bvhIndexSsbo);
        if (gpu->bvhIndexCapacity < bvhIndexCount) {
            gX_RHI_Buffer_Data(gX_RHI_SHADER_STORAGE_BUFFER,
                bvhIndexCount * (gX_U32)sizeof(*bvhIndices), bvhIndices,
                gX_RHI_DYNAMIC_DRAW);
            gpu->bvhIndexCapacity = bvhIndexCount;
        } else {
            gX_RHI_Buffer_SubData(gX_RHI_SHADER_STORAGE_BUFFER, 0,
                bvhIndexCount * (gX_U32)sizeof(*bvhIndices), bvhIndices);
        }
        gpu->bvhIndexCount = bvhIndexCount;
        if (gpu->albedoTextureArray > 0)
            gX_RHI_Tex_Delete(gpu->albedoTextureArray);
        gpu->albedoTextureArray = newTextureArray;
        gpu->sceneSignature = sceneSignature;
        gpu->sceneReady = 1;
        free(triangles);
        free(bvhNodes);
        free(bvhIndices);
    }
    triangleCount = gpu->triangleCount;
    bvhNodeCount = gpu->bvhNodeCount;
    bvhIndexCount = gpu->bvhIndexCount;
    if (triangleCount == 0 || bvhNodeCount == 0 || bvhIndexCount == 0) return;

    GX_HEAP_FOREACH(gfx->lights, gX_RendererGFX_Light, light) {
        if (pointCount >= GX_ORACLE_GPU_MAX_POINT_LIGHTS || light->sun
            || light->type != 1) continue;
        pointPositions[pointCount] = (gX_F32v4){light->pos.x, light->pos.y, light->pos.z, 1.0f};
        pointRadiance[pointCount] = (gX_F32v4){
            (gX_F32)light->color.r / 255.0f * light->brightness * light->diffuseFactor,
            (gX_F32)light->color.g / 255.0f * light->brightness * light->diffuseFactor,
            (gX_F32)light->color.b / 255.0f * light->brightness * light->diffuseFactor, 1.0f};
        pointAtten[pointCount] = (gX_F32v4){light->attenConst, light->attenLinear,
            light->attenQuad, (gfx->state.ENABLE_POINT_SHADOWS
                && light->castShadows) ? 1.0f : 0.0f};
        pointCount++;
    }
    forward = gX_OracleGPU_Normalize(gX_F32v3_Sub(camera.camera3D.target, camera.camera3D.position));
    right = gX_OracleGPU_Normalize(gX_F32v3_Cross(forward, camera.camera3D.up));
    up = gX_OracleGPU_Normalize(gX_F32v3_Cross(right, forward));
    sunDirection = gX_OracleGPU_Normalize(gX_F32v3_Scale(gfx->sun.dir, -1.0f));
    sunRadiance = (gX_F32v3){
        (gX_F32)gfx->sun.color.r / 255.0f * gfx->sun.brightness * gfx->sun.diffuseFactor
            * (gX_F32)gfx->sun.diffuseTint.r / 255.0f,
        (gX_F32)gfx->sun.color.g / 255.0f * gfx->sun.brightness * gfx->sun.diffuseFactor
            * (gX_F32)gfx->sun.diffuseTint.g / 255.0f,
        (gX_F32)gfx->sun.color.b / 255.0f * gfx->sun.brightness * gfx->sun.diffuseFactor
            * (gX_F32)gfx->sun.diffuseTint.b / 255.0f};
    tanHalfFov = tanf(camera.camera3D.fovy * gX_DEG2RAD * 0.5f);
    if (previewActive) {
        width = gpu->previewWidth;
        height = gpu->previewHeight;
        aspect = (gX_F32)width / (gX_F32)height;
        targetTexture = gpu->previewTexture;
        sampleSignature = gX_OracleGPU_SampleSignature(gfx, sceneSignature);
        if (sampleSignature != gpu->sampleSignature) {
            gpu->sampleSignature = sampleSignature;
            gpu->previewSamples = 0;
        }
        if (gpu->previewSamples >= 256) return;
    } else {
        width = gfx->bloom.hdrScene.wrapper.rt.texture.width;
        height = gfx->bloom.hdrScene.wrapper.rt.texture.height;
        aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
        targetTexture = gfx->bloom.hdrScene.wrapper.rt.texture.id;
    }

    gX_Batch_Flush();
    gX_RHI_Shader_Use(gpu->program);
    previousAlbedoTexture = gX_RHI_Tex_GetBoundOnUnit(albedoTextureUnit);
    previousAlbedoTarget = gX_RHI_Tex_GetUnitTarget(albedoTextureUnit);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0 + albedoTextureUnit);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &previousAlbedoArrayBinding);
    glActiveTexture((GLenum)previousActiveTexture);
    gX_RHI_Tex_ClearUnit(albedoTextureUnit);
    gX_RHI_Tex_BindOnUnit(gpu->albedoTextureArray, albedoTextureUnit);
    gX_RHI_UniformInt(gpu->albedoTextureArrayLoc,
        (gX_I32)albedoTextureUnit);
    gX_RHI_Image_Bind(targetTexture, 0,
        gX_RHI_READ_WRITE, gX_RHI_RGBA16F);
    gX_RHI_Image_Bind(previewActive ? gpu->previewAccumTexture : 0, 1,
        gX_RHI_READ_WRITE, gX_RHI_RGBA16F);
    gX_RHI_SSBO_Bind(gpu->triangleSsbo, 2);
    gX_RHI_SSBO_Bind(gpu->bvhSsbo, 3);
    gX_RHI_SSBO_Bind(gpu->bvhIndexSsbo, 4);
    gX_RHI_Uniform_Vec2(gpu->resolutionLoc, (gX_F32)width, (gX_F32)height);
    gX_RHI_Uniform_Vec3(gpu->cameraPositionLoc, camera.camera3D.position.x,
        camera.camera3D.position.y, camera.camera3D.position.z);
    gX_RHI_Uniform_Vec3(gpu->cameraForwardLoc, forward.x, forward.y, forward.z);
    gX_RHI_Uniform_Vec3(gpu->cameraRightLoc, right.x, right.y, right.z);
    gX_RHI_Uniform_Vec3(gpu->cameraUpLoc, up.x, up.y, up.z);
    gX_RHI_Uniform_Float(gpu->tanHalfFovLoc, tanHalfFov);
    gX_RHI_UniformInt(gpu->cameraProjectionLoc,
        camera.camera3D.projection == gX_CAMERA_ORTHOGRAPHIC ? 1 : 0);
    gX_RHI_Uniform_Float(gpu->orthoHalfHeightLoc,
        camera.camera3D.fovy * 0.5f);
    gX_RHI_Uniform_Float(gpu->aspectLoc, aspect);
    gX_RHI_UniformInt(gpu->pointCountLoc, (gX_I32)pointCount);
    gX_RHI_UniformVec4(gpu->pointPositionsLoc, GX_ORACLE_GPU_MAX_POINT_LIGHTS,
        &pointPositions[0].x);
    gX_RHI_UniformVec4(gpu->pointRadianceLoc, GX_ORACLE_GPU_MAX_POINT_LIGHTS,
        &pointRadiance[0].x);
    gX_RHI_UniformVec4(gpu->pointAttenLoc, GX_ORACLE_GPU_MAX_POINT_LIGHTS,
        &pointAtten[0].x);
    gX_RHI_Uniform_Vec3(gpu->sunDirectionLoc, sunDirection.x, sunDirection.y, sunDirection.z);
    gX_RHI_Uniform_Vec3(gpu->sunRadianceLoc, sunRadiance.x, sunRadiance.y, sunRadiance.z);
    gX_RHI_UniformInt(gpu->sunCastShadowsLoc,
        (gfx->state.ENABLE_SHADOWS && gfx->sun.castShadows) ? 1 : 0);
    gX_RHI_Uniform_Vec3(gpu->environmentColorLoc,
        (gX_F32)gfx->sky.color.r / 255.0f,
        (gX_F32)gfx->sky.color.g / 255.0f,
        (gX_F32)gfx->sky.color.b / 255.0f);
    gX_RHI_Uniform_Float(gpu->exposureLoc, gfx->state.exposure);
    gX_RHI_UniformInt(gpu->bounceCountLoc, gX_OracleGPU_BounceCount());
    gX_RHI_UniformInt(gpu->bvhNodeCountLoc, (gX_I32)bvhNodeCount);
    gX_RHI_UniformInt(gpu->reflectionModeLoc, gX_OracleGPU_ReflectionOn());
    gX_RHI_UniformInt(gpu->sampleIndexLoc,
        previewActive ? (gX_I32)gpu->previewSamples : 0);
    gX_RHI_UniformInt(gpu->previewModeLoc, previewActive ? 1 : 0);
    gX_RHI_DispatchCompute((width + 7u) / 8u, (height + 7u) / 8u, 1);
    gX_RHI_MemoryBarrier(gX_RHI_SHADER_IMAGE_ACCESS_BARRIER_BIT
        | gX_RHI_TEXTURE_FETCH_BARRIER_BIT);
    gX_RHI_Image_Bind(0, 0, gX_RHI_READ_WRITE, gX_RHI_RGBA16F);
    gX_RHI_Image_Bind(0, 1, gX_RHI_READ_WRITE, gX_RHI_RGBA16F);
    gX_RHI_SSBO_Bind(0, 2);
    gX_RHI_SSBO_Bind(0, 3);
    gX_RHI_SSBO_Bind(0, 4);
    gX_RHI_Shader_Use(0);
    glActiveTexture(GL_TEXTURE0 + albedoTextureUnit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)previousAlbedoArrayBinding);
    glActiveTexture((GLenum)previousActiveTexture);
    gX_RHI_Tex_ClearUnit(albedoTextureUnit);
    if (previousAlbedoTexture > 0)
        gX_RHI_Tex_SetUnit(albedoTextureUnit, previousAlbedoTexture,
            previousAlbedoTarget);
    if (previewActive) gpu->previewSamples++;
}

//opencode: present the independent ray target without feeding raster effects
//   into it; split mode wipes the right half so both images share one framing.
gX_I0 gX_RendererGFX_OracleGPU_Present(gX_RendererGFX *gfx,
    gX_RenderTexture *outputRT)
{
    gX_RendererGFX_OracleGPU *gpu = &gfx->oracleGpu;
    gX_RenderTexture *target = outputRT;
    gX_Texture previewTexture;
    gX_U32 targetWidth;
    gX_U32 targetHeight;
    if (gpu->previewMode == GX_GFX_RAY_PREVIEW_RASTER
        || !gX_RendererGFX_OracleGPU_Available()
        || gpu->previewTexture == 0 || gpu->previewSamples == 0
        || !target || target->id == 0 || gpu->previewDisplayTarget.id == 0)
        return;

    previewTexture = (gX_Texture){.id = gpu->previewTexture,
        .width = gpu->previewWidth, .height = gpu->previewHeight};
    if (gfx->state.ENABLE_BLOOM && gfx->bloom.compositeShader.loaded) {
        gX_Shader shader = gfx->bloom.compositeShader.shader;
        gX_Matrix mvp;
        gX_I32 ssgiTextureLoc;
        gX_I32 ssgiGainLoc;
        gX_I32 ditherMode = 1;
        gX_I32 paletteMode = 0;
        gX_F32 ditherSeed = 0.0f;
        gX_F32 ditherStrength = 1.5f;
        const gX_I8 *ditherModeHook = getenv("GX_T_DITHER_MODE");
        const gX_I8 *ditherSeedHook = getenv("GX_T_DITHER_SEED");
        const gX_I8 *ditherStrengthHook = getenv("GX_T_DITHER_STRENGTH");
        const gX_I8 *animateHook = getenv("GX_T_DITHER_ANIMATE");
        const gX_I8 *paletteHook = getenv("GX_T_PALETTE_MODE");
        if (ditherModeHook && ditherModeHook[0])
            ditherMode = atoi((const char *)ditherModeHook);
        if (ditherSeedHook && ditherSeedHook[0])
            ditherSeed = (gX_F32)atof((const char *)ditherSeedHook);
        if (ditherStrengthHook && ditherStrengthHook[0])
            ditherStrength = (gX_F32)atof((const char *)ditherStrengthHook);
        if (paletteHook && paletteHook[0])
            paletteMode = atoi((const char *)paletteHook);
        if (animateHook && animateHook[0] && atoi((const char *)animateHook) != 0)
            ditherSeed += (gX_F32)maindata.frameCount;
        if (ditherMode < 0) ditherMode = 0;
        if (ditherMode > 3) ditherMode = 3;
        if (ditherStrength < 0.0f) ditherStrength = 0.0f;
        if (ditherStrength > 4.0f) ditherStrength = 4.0f;
        if (paletteMode < 0) paletteMode = 0;
        if (paletteMode > 2) paletteMode = 2;
        if (paletteMode > 0) ditherMode = 0;
        gX_Drawing_BeginTexture(gpu->previewDisplayTarget);
            gX_Drawing_BeginShader(shader);
            mvp = gX_RHI_Matrix_GetProjection();
            gX_RHI_UniformMatrix(gX_RHI_GetUniformLocation(shader.id, "mvp"),
                1, 1, &mvp.m0);
            gpu->displaySceneLoc = gX_RHI_GetUniformLocation(shader.id, "sceneTex");
            gpu->displayBloomLoc = gX_RHI_GetUniformLocation(shader.id, "bloomTex");
            gpu->displayIntensityLoc = gX_RHI_GetUniformLocation(shader.id, "intensity");
            gpu->displayExposureLoc = gX_RHI_GetUniformLocation(shader.id, "exposure");
            gpu->displayVignetteLoc = gX_RHI_GetUniformLocation(shader.id, "vignette");
            gpu->displaySaturationLoc = gX_RHI_GetUniformLocation(shader.id, "saturation");
            gpu->displayContrastLoc = gX_RHI_GetUniformLocation(shader.id, "contrast");
            gpu->displayACESLoc = gX_RHI_GetUniformLocation(shader.id, "enableACES");
            gpu->displayAgXLoc = gX_RHI_GetUniformLocation(shader.id, "enableAgX");
            gpu->displayGammaLoc = gX_RHI_GetUniformLocation(shader.id, "enableGamma");
            gpu->displayDitherModeLoc = gX_RHI_GetUniformLocation(shader.id, "ditherMode");
            gpu->displayDitherSeedLoc = gX_RHI_GetUniformLocation(shader.id, "ditherSeed");
            gpu->displayDitherStrengthLoc = gX_RHI_GetUniformLocation(shader.id,
                "ditherStrength");
            ssgiTextureLoc = gX_RHI_GetUniformLocation(shader.id, "ssgiTex");
            ssgiGainLoc = gX_RHI_GetUniformLocation(shader.id, "ssgiGain");
            gX_RHI_Tex_BindOnUnit(previewTexture.id, gX_GFX_TEXUNIT_SCENE);
            gX_RHI_Tex_BindOnUnit(previewTexture.id, gX_GFX_TEXUNIT_BLOOM_BLUR);
            gX_RHI_Tex_BindOnUnit(previewTexture.id, gX_GFX_TEXUNIT_SSGI_HIST);
            gX_RHI_UniformInt(gpu->displaySceneLoc, gX_GFX_TEXUNIT_SCENE);
            gX_RHI_UniformInt(gpu->displayBloomLoc, gX_GFX_TEXUNIT_BLOOM_BLUR);
            gX_RHI_UniformInt(ssgiTextureLoc, gX_GFX_TEXUNIT_SSGI_HIST);
            gX_RHI_Uniform_Float(ssgiGainLoc, 0.0f);
            gX_RHI_Uniform_Float(gpu->displayIntensityLoc, 0.0f);
            gX_RHI_Uniform_Float(gpu->displayExposureLoc, gfx->bloom.exposure);
            gX_RHI_Uniform_Float(gpu->displayVignetteLoc, gfx->bloom.vignette);
            gX_RHI_Uniform_Float(gpu->displaySaturationLoc, gfx->bloom.saturation);
            gX_RHI_Uniform_Float(gpu->displayContrastLoc, gfx->bloom.contrast);
            gX_RHI_UniformInt(gpu->displayACESLoc,
                gfx->state.ENABLE_ACES_TONEMAP ? 1 : 0);
            gX_RHI_UniformInt(gpu->displayAgXLoc,
                gfx->state.ENABLE_AGX_TONEMAP ? 1 : 0);
            gX_RHI_UniformInt(gpu->displayGammaLoc,
                gfx->state.ENABLE_GAMMA_CORRECTION ? 1 : 0);
            gX_RHI_UniformInt(gpu->displayDitherModeLoc, ditherMode);
            gX_RHI_Uniform_Float(gpu->displayDitherSeedLoc, ditherSeed);
            gX_RHI_Uniform_Float(gpu->displayDitherStrengthLoc, ditherStrength);
            gX_Drawing_DrawScreenQuad(0, 0,
                (gX_F32)gpu->previewDisplayWidth,
                (gX_F32)gpu->previewDisplayHeight);
            gX_Drawing_EndShader();
        gX_Drawing_EndTexture();
    } else {
        gX_Drawing_BeginTexture(gpu->previewDisplayTarget);
            gX_Texture_Draw(previewTexture, (gX_Texture_Draw_Params){
                .dst = {0, 0, (gX_F32)gpu->previewDisplayWidth,
                    (gX_F32)gpu->previewDisplayHeight},
                .tint = (gX_Color){255, 255, 255, 255},
                .scale = {1, 1}
            });
        gX_Drawing_EndTexture();
    }

    targetWidth = target->texture.width;
    targetHeight = target->texture.height;
    if (gpu->previewMode == GX_GFX_RAY_PREVIEW_RAY) {
        gX_Drawing_BeginTexture(*target);
            gX_Texture_Draw(gpu->previewDisplayTarget.texture,
                (gX_Texture_Draw_Params){
                    .dst = {0, 0, (gX_F32)targetWidth, (gX_F32)targetHeight},
                    .tint = (gX_Color){255, 255, 255, 255},
                    .scale = {1, 1}
                });
        gX_Drawing_EndTexture();
    } else if (gpu->previewMode == GX_GFX_RAY_PREVIEW_SIDE_BY_SIDE
        && gpu->comparisonRasterTarget.id > 0) {
        gX_Drawing_BeginTexture(gpu->comparisonRasterTarget);
            gX_Texture_Draw(target->texture, (gX_Texture_Draw_Params){
                .dst = {0, 0, (gX_F32)gpu->comparisonRasterWidth,
                    (gX_F32)gpu->comparisonRasterHeight},
                .tint = (gX_Color){255, 255, 255, 255},
                .scale = {1, 1}
            });
        gX_Drawing_EndTexture();
        gX_Drawing_BeginTexture(*target);
            gX_Texture_Draw(gpu->comparisonRasterTarget.texture,
                (gX_Texture_Draw_Params){
                    .src = {(gX_F32)(targetWidth / 4u), 0,
                        (gX_F32)(targetWidth / 2u), (gX_F32)targetHeight},
                    .dst = {0, 0, (gX_F32)(targetWidth / 2u),
                        (gX_F32)targetHeight},
                    .tint = (gX_Color){255, 255, 255, 255},
                    .scale = {1, 1}
                });
            gX_Texture_Draw(gpu->previewDisplayTarget.texture,
                (gX_Texture_Draw_Params){
                    .dst = {(gX_F32)(targetWidth / 2u), 0,
                        (gX_F32)(targetWidth - targetWidth / 2u),
                        (gX_F32)targetHeight},
                    .tint = (gX_Color){255, 255, 255, 255},
                    .scale = {1, 1}
                });
        gX_Drawing_EndTexture();
    }
}

gX_I0 gX_RendererGFX_OracleGPU_Unload(gX_RendererGFX *gfx)
{
    gX_RendererGFX_OracleGPU *gpu = &gfx->oracleGpu;
    if (gpu->triangleSsbo > 0) gX_RHI_Buffer_Delete(gpu->triangleSsbo);
    if (gpu->bvhSsbo > 0) gX_RHI_Buffer_Delete(gpu->bvhSsbo);
    if (gpu->bvhIndexSsbo > 0) gX_RHI_Buffer_Delete(gpu->bvhIndexSsbo);
    if (gpu->albedoTextureArray > 0)
        gX_RHI_Tex_Delete(gpu->albedoTextureArray);
    if (gpu->program > 0) gX_RHI_Shader_Delete(gpu->program);
    if (gpu->previewTexture > 0) {
        gX_Texture_RT_Untrack(gpu->previewTexture);
        gX_RHI_Tex_Delete(gpu->previewTexture);
    }
    if (gpu->previewAccumTexture > 0)
        gX_RHI_Tex_Delete(gpu->previewAccumTexture);
    if (gpu->previewDisplayTarget.id > 0)
        gX_RenderTexture_Unload(gpu->previewDisplayTarget);
    if (gpu->comparisonRasterTarget.id > 0)
        gX_RenderTexture_Unload(gpu->comparisonRasterTarget);
    memset(gpu, 0, sizeof(*gpu));
}
