/**********************************************************************************************
*
*   [gX: src/renderer/gfx/glb_writer.c]
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


//opencode: scene export v2 — minimal glTF 2.0 binary writer (one GLB per
//   model). Every drawable mesh becomes a primitive with the world transform
//   baked; the materials mirror the render-side resolution
//   (gX_RendererGFX_Model_MeshMaterialValues) and override albedo/emissive
//   maps are embedded as PNG/JPEG images. File-material textures from the
//   source GLB are not re-embedded (the loader does not keep their bytes).

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "glb_writer.h"
#include "../gfx.h"
#include "model.h"
#include "../../common/log.h"
#include "../../wrappers/file.h"

#define GX_GLBW_MAX_IMAGES 8

//opencode: growable byte builder for the JSON chunk.
typedef struct {
    gX_U8 *data;
    gX_U32 len;
    gX_U32 cap;
    gX_I1 ok;
} gX_GLBW_SB;

typedef struct {
    gX_I32 source;              // index into md->meshes
    gX_U32 vertexCount;
    gX_U32 indexCount;
    gX_U32 idxOff, posOff, nrmOff, uvOff;
    gX_I32 albedoImage;
    gX_I32 emissiveImage;
    gX_U32 accBase;
    gX_U32 viewBase;
} gX_GLBW_Mesh;

typedef struct {
    gX_I8 path[512];
    gX_I8 mime[32];
    gX_U8 *bytes;
    gX_I32 size;
    gX_U32 off;
    gX_I32 texture;
} gX_GLBW_Image;

static gX_U32 glbw_align4(gX_U32 value) {
    return (value + 3u) & ~3u;
}

static gX_I0 glbw_put_u32(gX_U8 *dst, gX_U32 value) {
    dst[0] = (gX_U8)(value & 0xFF);
    dst[1] = (gX_U8)((value >> 8) & 0xFF);
    dst[2] = (gX_U8)((value >> 16) & 0xFF);
    dst[3] = (gX_U8)((value >> 24) & 0xFF);
}

static gX_I0 glbw_sb_reserve(gX_GLBW_SB *sb, gX_U32 extra) {
    if (!sb->ok) return;
    if (sb->len + extra <= sb->cap) return;
    gX_U32 cap = sb->cap ? sb->cap : 65536u;
    while (cap < sb->len + extra) cap *= 2u;
    gX_U8 *grown = (gX_U8 *)realloc(sb->data, cap);
    if (!grown) {
        sb->ok = 0;
        return;
    }
    sb->data = grown;
    sb->cap = cap;
}

static gX_I0 glbw_sb_puts(gX_GLBW_SB *sb, const gX_I8 *text) {
    size_t length = strlen(text);
    glbw_sb_reserve(sb, (gX_U32)length);
    if (!sb->ok) return;
    memcpy(sb->data + sb->len, text, length);
    sb->len += (gX_U32)length;
}

static gX_I0 glbw_sb_putf(gX_GLBW_SB *sb, const gX_I8 *format, ...) {
    gX_I8 tmp[1024];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(tmp, sizeof(tmp), format, args);
    va_end(args);
    if (written < 0) {
        sb->ok = 0;
        return;
    }
    if (written >= (int)sizeof(tmp)) written = (int)sizeof(tmp) - 1;
    glbw_sb_reserve(sb, (gX_U32)written);
    if (!sb->ok) return;
    memcpy(sb->data + sb->len, tmp, (size_t)written);
    sb->len += (gX_U32)written;
}

static gX_I0 glbw_sb_put_json_string(gX_GLBW_SB *sb, const gX_I8 *text) {
    glbw_sb_puts(sb, "\"");
    for (const gX_I8 *cursor = text; *cursor; cursor++) {
        if (*cursor == '"' || *cursor == '\\') {
            gX_I8 pair[3] = {'\\', *cursor, 0};
            glbw_sb_puts(sb, pair);
        } else if ((gX_U8)*cursor < 0x20) {
            glbw_sb_puts(sb, "?");
        } else {
            gX_I8 one[2] = {*cursor, 0};
            glbw_sb_puts(sb, one);
        }
    }
    glbw_sb_puts(sb, "\"");
}

static gX_F32 glbw_clamp01(gX_F32 value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static const gX_I8 *glbw_basename(const gX_I8 *path) {
    const gX_I8 *slash = strrchr((const char *)path, '/');
    return slash ? slash + 1 : path;
}

//opencode: find-or-load an override map (resource subpath) as an embedded
//   image; dedupes by path and caps the per-model image count.
static gX_I32 glbw_image_find_or_add(gX_GLBW_Image *images, gX_I32 *count,
    const gX_I8 *subpath, gX_U32 *binOffset)
{
    if (!subpath || !subpath[0]) return -1;
    gX_I8 source[512];
    snprintf((char *)source, sizeof(source), "resources/texture/%s",
        (const char *)subpath);
    for (gX_I32 i = 0; i < *count; i++) {
        if (strcmp((const char *)images[i].path, (const char *)source) == 0)
            return i;
    }
    if (*count >= GX_GLBW_MAX_IMAGES) return -1;
    gX_I32 size = 0;
    gX_U8 *bytes = gX_FileData_Load(source, &size);
    if (!bytes || size <= 0) {
        gX_FileData_Unload(bytes);
        return -1;
    }
    gX_GLBW_Image *image = &images[*count];
    memset(image, 0, sizeof(*image));
    snprintf((char *)image->path, sizeof(image->path), "%s",
        (const char *)source);
    const gX_I8 *dot = strrchr((const char *)glbw_basename(source), '.');
    gX_I1 jpeg = dot && (strcasecmp((const char *)dot, ".jpg") == 0
        || strcasecmp((const char *)dot, ".jpeg") == 0);
    snprintf((char *)image->mime, sizeof(image->mime), "%s",
        jpeg ? "image/jpeg" : "image/png");
    image->bytes = bytes;
    image->size = size;
    image->off = glbw_align4(*binOffset);
    *binOffset = image->off + (gX_U32)size;
    image->texture = *count;
    (*count)++;
    return *count - 1;
}

//opencode: bake one vertex through the model's world matrix (same convention
//   as the OBJ export: row = m0,m4,m8,m12).
static gX_I0 glbw_bake_position(const gX_Mesh *mesh, gX_Matrix world,
    gX_I32 index, gX_F32 *out)
{
    gX_F32 x = mesh->vertices[index * 3 + 0];
    gX_F32 y = mesh->vertices[index * 3 + 1];
    gX_F32 z = mesh->vertices[index * 3 + 2];
    out[0] = world.m0 * x + world.m4 * y + world.m8 * z + world.m12;
    out[1] = world.m1 * x + world.m5 * y + world.m9 * z + world.m13;
    out[2] = world.m2 * x + world.m6 * y + world.m10 * z + world.m14;
}

static gX_I1 glbw_write_file(const gX_I8 *path, gX_GLBW_SB *json,
    const gX_U8 *bin, gX_U32 binSize)
{
    gX_U32 jsonPad = glbw_align4(json->len);
    gX_U32 binPad = glbw_align4(binSize);
    gX_U32 total = 12u + 8u + jsonPad + 8u + binPad;
    gX_U8 *file = (gX_U8 *)calloc(total, 1);
    if (!file) return 0;
    glbw_put_u32(file + 0, 0x46546C67u);
    glbw_put_u32(file + 4, 2u);
    glbw_put_u32(file + 8, total);
    glbw_put_u32(file + 12, jsonPad);
    glbw_put_u32(file + 16, 0x4E4F534Au);
    memcpy(file + 20, json->data, json->len);
    memset(file + 20 + json->len, ' ', jsonPad - json->len);
    gX_U32 binChunk = 20u + jsonPad;
    glbw_put_u32(file + binChunk, binPad);
    glbw_put_u32(file + binChunk + 4, 0x004E4942u);
    memcpy(file + binChunk + 8, bin, binSize);
    gX_I1 ok = gX_FileData_Save(path, (gX_I0 *)file, (gX_I32)total);
    free(file);
    return ok;
}

gX_I1 gX_RendererGFX_GLB_WriteModel(const gX_RendererGFX_Model *model,
    const gX_I8 *path)
{
    if (!model || !model->loaded || !path || !path[0]) return 0;
    const gX_Model *md = &model->model;
    if (md->meshCount <= 0 || !md->meshes) return 0;

    //opencode: keep only drawable meshes (node/mesh/material indices follow
    //   this compact order).
    gX_GLBW_Mesh *meshes = (gX_GLBW_Mesh *)calloc((size_t)md->meshCount,
        sizeof(gX_GLBW_Mesh));
    if (!meshes) return 0;
    gX_I32 count = 0;
    for (gX_I32 mi = 0; mi < md->meshCount; mi++) {
        const gX_Mesh *mesh = &md->meshes[mi];
        if (!mesh->vertices || mesh->vertexCount <= 0) continue;
        gX_GLBW_Mesh *record = &meshes[count++];
        record->source = mi;
        record->vertexCount = (gX_U32)mesh->vertexCount;
        record->indexCount = mesh->indices
            ? (gX_U32)mesh->triangleCount * 3u : 0u;
        record->albedoImage = -1;
        record->emissiveImage = -1;
    }
    if (count == 0) {
        free(meshes);
        return 0;
    }
    gX_GLBW_Image images[GX_GLBW_MAX_IMAGES];
    memset(images, 0, sizeof(images));
    gX_I32 imageCount = 0;
    gX_Matrix world = gX_RendererGFX_Model_WorldMatrix(model);

    //opencode: bin layout — mesh attributes first, then embedded images.
    gX_U32 binOffset = 0;
    gX_U32 totalViews = 0;
    gX_U32 totalAccessors = 0;
    for (gX_I32 i = 0; i < count; i++) {
        gX_GLBW_Mesh *record = &meshes[i];
        const gX_Mesh *mesh = &md->meshes[record->source];
        record->viewBase = totalViews;
        record->accBase = totalAccessors;
        if (record->indexCount > 0) {
            record->idxOff = glbw_align4(binOffset);
            binOffset = record->idxOff + record->indexCount * 2u;
            totalViews++;
            totalAccessors++;
        }
        record->posOff = glbw_align4(binOffset);
        binOffset = record->posOff + record->vertexCount * 12u;
        totalViews++;
        totalAccessors++;
        if (mesh->normals) {
            record->nrmOff = glbw_align4(binOffset);
            binOffset = record->nrmOff + record->vertexCount * 12u;
            totalViews++;
            totalAccessors++;
        }
        if (mesh->texcoords) {
            record->uvOff = glbw_align4(binOffset);
            binOffset = record->uvOff + record->vertexCount * 8u;
            totalViews++;
            totalAccessors++;
        }
    }
    gX_U32 meshViewCount = totalViews;
    for (gX_I32 i = 0; i < count; i++) {
        gX_RendererGFX_MeshMaterialValues values;
        gX_RendererGFX_Model_MeshMaterialValues(model, meshes[i].source,
            &values);
        meshes[i].albedoImage = glbw_image_find_or_add(images, &imageCount,
            values.albedoMap, &binOffset);
        meshes[i].emissiveImage = glbw_image_find_or_add(images, &imageCount,
            values.emissiveMap, &binOffset);
    }
    gX_U32 binSize = glbw_align4(binOffset);
    gX_U8 *bin = (gX_U8 *)calloc(binSize > 0 ? binSize : 4u, 1);
    if (!bin) {
        for (gX_I32 i = 0; i < imageCount; i++)
            gX_FileData_Unload(images[i].bytes);
        free(meshes);
        return 0;
    }

    //opencode: fill the binary chunk (baked positions/normals).
    for (gX_I32 i = 0; i < count; i++) {
        gX_GLBW_Mesh *record = &meshes[i];
        const gX_Mesh *mesh = &md->meshes[record->source];
        if (record->indexCount > 0)
            memcpy(bin + record->idxOff, mesh->indices,
                (size_t)record->indexCount * 2u);
        gX_F32 *positions = (gX_F32 *)(bin + record->posOff);
        for (gX_U32 vi = 0; vi < record->vertexCount; vi++)
            glbw_bake_position(mesh, world, (gX_I32)vi, &positions[vi * 3]);
        if (mesh->normals) {
            gX_F32 *normals = (gX_F32 *)(bin + record->nrmOff);
            for (gX_U32 vi = 0; vi < record->vertexCount; vi++) {
                gX_F32 nx = mesh->normals[vi * 3 + 0];
                gX_F32 ny = mesh->normals[vi * 3 + 1];
                gX_F32 nz = mesh->normals[vi * 3 + 2];
                gX_F32 tx = world.m0 * nx + world.m4 * ny + world.m8 * nz;
                gX_F32 ty = world.m1 * nx + world.m5 * ny + world.m9 * nz;
                gX_F32 tz = world.m2 * nx + world.m6 * ny + world.m10 * nz;
                gX_F32 length = sqrtf(tx * tx + ty * ty + tz * tz);
                gX_F32 inv = (length > 1e-8f) ? 1.0f / length : 0.0f;
                normals[vi * 3 + 0] = tx * inv;
                normals[vi * 3 + 1] = ty * inv;
                normals[vi * 3 + 2] = tz * inv;
            }
        }
        if (mesh->texcoords)
            memcpy(bin + record->uvOff, mesh->texcoords,
                (size_t)record->vertexCount * 8u);
    }
    for (gX_I32 i = 0; i < imageCount; i++)
        memcpy(bin + images[i].off, images[i].bytes, (size_t)images[i].size);

    gX_GLBW_SB json = {0};
    json.ok = 1;
    glbw_sb_puts(&json,
        "{\"asset\":{\"version\":\"2.0\",\"generator\":\"gX scene export\"},");
    glbw_sb_puts(&json, "\"scene\":0,\"scenes\":[{\"nodes\":[");
    for (gX_I32 i = 0; i < count; i++)
        glbw_sb_putf(&json, "%s%d", i ? "," : "", i);
    glbw_sb_puts(&json, "]}],\"nodes\":[");
    for (gX_I32 i = 0; i < count; i++) {
        glbw_sb_putf(&json, "%s{\"mesh\":%d,\"name\":", i ? "," : "", i);
        glbw_sb_put_json_string(&json,
            glbw_basename((const gX_I8 *)model->path));
        glbw_sb_puts(&json, "}");
    }
    glbw_sb_puts(&json, "],\"meshes\":[");
    for (gX_I32 i = 0; i < count; i++) {
        gX_GLBW_Mesh *record = &meshes[i];
        const gX_Mesh *mesh = &md->meshes[record->source];
        glbw_sb_puts(&json, i ? ",{\"primitives\":[{\"attributes\":{"
            : "{\"primitives\":[{\"attributes\":{");
        gX_U32 positionAcc = record->accBase + (record->indexCount > 0 ? 1u : 0u);
        glbw_sb_putf(&json, "\"POSITION\":%u", positionAcc);
        gX_U32 nextAcc = positionAcc + 1u;
        if (mesh->normals) {
            glbw_sb_putf(&json, ",\"NORMAL\":%u", nextAcc++);
        }
        if (mesh->texcoords)
            glbw_sb_putf(&json, ",\"TEXCOORD_0\":%u", nextAcc);
        glbw_sb_putf(&json, "},\"material\":%d,\"mode\":4", i);
        if (record->indexCount > 0)
            glbw_sb_putf(&json, ",\"indices\":%u", record->accBase);
        glbw_sb_puts(&json, "}]}");
    }
    glbw_sb_puts(&json, "],\"materials\":[");
    for (gX_I32 i = 0; i < count; i++) {
        gX_RendererGFX_MeshMaterialValues values;
        gX_RendererGFX_Model_MeshMaterialValues(model, meshes[i].source,
            &values);
        gX_F32 emissive[3];
        for (gX_I32 channel = 0; channel < 3; channel++) {
            gX_F32 raw = (channel == 0 ? values.emissive.r
                : (channel == 1 ? values.emissive.g : values.emissive.b))
                / 255.0f;
            emissive[channel] = values.emissiveGlow > 0.0f
                ? glbw_clamp01(raw * (values.emissiveGlow > 1.0f
                    ? 1.0f : values.emissiveGlow))
                : 0.0f;
        }
        gX_F32 alpha = model->params.enableBlending
            ? glbw_clamp01(model->params.blendOpacity) : 1.0f;
        glbw_sb_putf(&json, "%s{\"name\":", i ? "," : "");
        glbw_sb_put_json_string(&json,
            glbw_basename((const gX_I8 *)model->path));
        glbw_sb_putf(&json,
            ",\"pbrMetallicRoughness\":{\"baseColorFactor\":[%.6f,%.6f,"
            "%.6f,%.6f],\"metallicFactor\":%.6f,\"roughnessFactor\":%.6f",
            (double)values.albedo.r / 255.0, (double)values.albedo.g / 255.0,
            (double)values.albedo.b / 255.0, (double)alpha,
            (double)glbw_clamp01(values.metalness),
            (double)glbw_clamp01(values.roughness));
        if (meshes[i].albedoImage >= 0)
            glbw_sb_putf(&json, ",\"baseColorTexture\":{\"index\":%d}",
                images[meshes[i].albedoImage].texture);
        glbw_sb_puts(&json, "}");
        glbw_sb_putf(&json, ",\"emissiveFactor\":[%.6f,%.6f,%.6f]",
            (double)emissive[0], (double)emissive[1], (double)emissive[2]);
        if (meshes[i].emissiveImage >= 0)
            glbw_sb_putf(&json, ",\"emissiveTexture\":{\"index\":%d}",
                images[meshes[i].emissiveImage].texture);
        if (values.emissiveGlow > 1.0f)
            glbw_sb_putf(&json,
                ",\"extensions\":{\"KHR_materials_emissive_strength\":"
                "{\"emissiveStrength\":%.6f}}", (double)values.emissiveGlow);
        if (model->params.enableBlending)
            glbw_sb_puts(&json, ",\"alphaMode\":\"BLEND\"");
        glbw_sb_puts(&json, "}");
    }
    glbw_sb_puts(&json, "],\"accessors\":[");
    gX_I1 first = 1;
    for (gX_I32 i = 0; i < count; i++) {
        gX_GLBW_Mesh *record = &meshes[i];
        const gX_Mesh *mesh = &md->meshes[record->source];
        if (record->indexCount > 0) {
            glbw_sb_putf(&json, "%s{\"bufferView\":%u,\"componentType\":5123,"
                "\"count\":%u,\"type\":\"SCALAR\"}", first ? "" : ",",
                record->viewBase, record->indexCount);
            first = 0;
        }
        gX_F32 min[3] = {0}, max[3] = {0};
        const gX_F32 *positions = (const gX_F32 *)(bin + record->posOff);
        for (gX_U32 vi = 0; vi < record->vertexCount; vi++) {
            for (gX_I32 axis = 0; axis < 3; axis++) {
                gX_F32 value = positions[vi * 3 + axis];
                if (vi == 0 || value < min[axis]) min[axis] = value;
                if (vi == 0 || value > max[axis]) max[axis] = value;
            }
        }
        glbw_sb_putf(&json, "%s{\"bufferView\":%u,\"componentType\":5126,"
            "\"count\":%u,\"type\":\"VEC3\",\"min\":[%.6g,%.6g,%.6g],"
            "\"max\":[%.6g,%.6g,%.6g]}", first ? "" : ",",
            record->viewBase + (record->indexCount > 0 ? 1u : 0u),
            record->vertexCount, (double)min[0], (double)min[1],
            (double)min[2], (double)max[0], (double)max[1], (double)max[2]);
        first = 0;
        gX_U32 view = record->viewBase
            + (record->indexCount > 0 ? 1u : 0u) + 1u;
        if (mesh->normals) {
            glbw_sb_putf(&json, ",{\"bufferView\":%u,\"componentType\":5126,"
                "\"count\":%u,\"type\":\"VEC3\"}", view++,
                record->vertexCount);
        }
        if (mesh->texcoords)
            glbw_sb_putf(&json, ",{\"bufferView\":%u,\"componentType\":5126,"
                "\"count\":%u,\"type\":\"VEC2\"}", view,
                record->vertexCount);
    }
    glbw_sb_puts(&json, "],\"bufferViews\":[");
    first = 1;
    for (gX_I32 i = 0; i < count; i++) {
        gX_GLBW_Mesh *record = &meshes[i];
        const gX_Mesh *mesh = &md->meshes[record->source];
        if (record->indexCount > 0) {
            glbw_sb_putf(&json, "%s{\"buffer\":0,\"byteOffset\":%u,"
                "\"byteLength\":%u,\"target\":34963}", first ? "" : ",",
                record->idxOff, record->indexCount * 2u);
            first = 0;
        }
        glbw_sb_putf(&json, "%s{\"buffer\":0,\"byteOffset\":%u,"
            "\"byteLength\":%u,\"target\":34962}", first ? "" : ",",
            record->posOff, record->vertexCount * 12u);
        first = 0;
        if (mesh->normals)
            glbw_sb_putf(&json, ",{\"buffer\":0,\"byteOffset\":%u,"
                "\"byteLength\":%u,\"target\":34962}", record->nrmOff,
                record->vertexCount * 12u);
        if (mesh->texcoords)
            glbw_sb_putf(&json, ",{\"buffer\":0,\"byteOffset\":%u,"
                "\"byteLength\":%u,\"target\":34962}", record->uvOff,
                record->vertexCount * 8u);
    }
    for (gX_I32 i = 0; i < imageCount; i++) {
        glbw_sb_putf(&json, "%s{\"buffer\":0,\"byteOffset\":%u,"
            "\"byteLength\":%d}", first ? "" : ",", images[i].off,
            images[i].size);
        first = 0;
    }
    glbw_sb_puts(&json, "],");
    if (imageCount > 0) {
        glbw_sb_puts(&json, "\"samplers\":[{\"magFilter\":9729,"
            "\"minFilter\":9987,\"wrapS\":10497,\"wrapT\":10497}],");
        glbw_sb_puts(&json, "\"images\":[");
        for (gX_I32 i = 0; i < imageCount; i++) {
            glbw_sb_putf(&json, "%s{\"bufferView\":%u,\"mimeType\":",
                i ? "," : "", meshViewCount + (gX_U32)i);
            glbw_sb_put_json_string(&json, images[i].mime);
            glbw_sb_puts(&json, "}");
        }
        glbw_sb_puts(&json, "],\"textures\":[");
        for (gX_I32 i = 0; i < imageCount; i++)
            glbw_sb_putf(&json, "%s{\"sampler\":0,\"source\":%d}",
                i ? "," : "", i);
        glbw_sb_puts(&json, "],");
    }
    glbw_sb_putf(&json, "\"buffers\":[{\"byteLength\":%u}]}", binSize);

    gX_I1 ok = json.ok;
    if (ok) ok = glbw_write_file(path, &json, bin, binSize);
    if (ok)
        gX_TraceLog(gX_LOG_INFO,
            "GLB: wrote %d meshes, %d images, %u bytes -> %s",
            count, imageCount, binSize, (const char *)path);
    for (gX_I32 i = 0; i < imageCount; i++)
        gX_FileData_Unload(images[i].bytes);
    free(json.data);
    free(bin);
    free(meshes);
    return ok;
}
