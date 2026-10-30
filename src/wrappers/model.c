/**********************************************************************************************
* 
*   [wrappers/model.c]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025-2026 lanttu, opencode
* 
*   [raylib]
*   LICENSE: zlib/libpng
*
*   raylib is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software:
*
*   Copyright (c) 2013-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/
//lanttu: original wrapper for raylib model/material/mesh functions
#include "wrappers_header.h"
#include "../common/path.h"
#include "model.h"
#include <stdlib.h>
#include <string.h>
#include "../rhi/rhi.h"
#include "../rhi/rhi_matrix.h"
#include "../rhi/rhi_state.h"
#include "../rhi/rhi_tex.h"
//opencode: hunt - implicit-decl sweep
#include "../rhi/rhi_mesh.h"
#include "../gxmath/matrix.h"
#include "../gxmath/constants.h"
#include "../gxmath/vector3.h"
#include "../main.h"
#include "../backend/raylib/raylib_wrappers_model.h"
#include "../external/tinyobj_loader_c.h"

//opencode: PS2 stub — model rendering deferred

//opencode: threaded OBJ loader — moves CPU-bound tinyobj parse off the main thread.
//   Worker thread reads + parses the OBJ, stores vertex data in a buffer.
//   Main thread creates raylib Meshes from the buffer and uploads to GPU.
//   Only active for .obj files over GX_GFX_MODEL_SIZE_WARN (5MB).
#include "../common/threading.h"
#include "../rhi/rhi_buf.h"

#define GX_MODEL_ASYNC_MAX 4

typedef struct gX_Model_AsyncSlot {
    gX_I8  path[512];
    gX_I1  busy;
    gX_I1  done;
    gX_Thread thread;
    // Parsed data (filled by worker thread, consumed by main thread)
    gX_F32 *positions;
    gX_F32 *normals;
    gX_F32 *texcoords;
    gX_U32  vertexCount;
    gX_U32  faceCount;
    gX_I8   errorMsg[256];
} gX_Model_AsyncSlot;

static gX_Model_AsyncSlot gX_modelAsyncQueue[GX_MODEL_ASYNC_MAX];

static gX_I0 *gX_Model_AsyncThreadFunc(gX_I0 *arg)
{
    gX_Model_AsyncSlot *slot = (gX_Model_AsyncSlot*)arg;
    // Read the file into memory
    FILE *f = fopen(slot->path, "rb");
    if (!f) { snprintf(slot->errorMsg, sizeof(slot->errorMsg), "Cannot open %.200s", slot->path); slot->done = true; return NULL; }
    fseek(f, 0, SEEK_END);
    long fileLen = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *fileText = (char*)malloc((gX_U64)fileLen + 1);
    if (!fileText) { fclose(f); snprintf(slot->errorMsg, sizeof(slot->errorMsg), "Out of memory"); slot->done = true; return NULL; }
    fread(fileText, 1, (gX_U64)fileLen, f);
    fclose(f);
    fileText[fileLen] = '\0';

    // Parse with tinyobj (raylib's bundled copy)
    tinyobj_attrib_t attrib;
    tinyobj_shape_t *shapes = NULL;
    unsigned int shapeCount = 0;
    tinyobj_material_t *materials = NULL;
    unsigned int materialCount = 0;
    int ret = tinyobj_parse_obj(&attrib, &shapes, &shapeCount, &materials, &materialCount, fileText, (unsigned int)fileLen, 0);
    free(fileText);
    if (ret != 0) {
        snprintf(slot->errorMsg, sizeof(slot->errorMsg), "tinyobj parse error");
        free(shapes); free(materials);
        slot->done = true; return NULL;
    }
    free(shapes);
    if (materials) { for (unsigned int mi = 0; mi < materialCount; mi++) free(materials[mi].name); free(materials); }

    // Allocate and fill vertex arrays (positions, normals, texcoords)
    slot->vertexCount = attrib.num_vertices;
    slot->faceCount = attrib.num_faces;
    if (attrib.num_vertices > 0) {
        slot->positions = (gX_F32*)malloc((gX_U64)attrib.num_vertices * 3 * sizeof(gX_F32));
        slot->normals   = (gX_F32*)malloc((gX_U64)attrib.num_vertices * 3 * sizeof(gX_F32));
        slot->texcoords = (gX_F32*)malloc((gX_U64)attrib.num_vertices * 2 * sizeof(gX_F32));
        if (slot->positions && slot->normals && slot->texcoords)
        {
            memcpy(slot->positions, attrib.vertices, (gX_U64)attrib.num_vertices * 3 * sizeof(gX_F32));
            memcpy(slot->normals,   attrib.normals,   (gX_U64)attrib.num_vertices * 3 * sizeof(gX_F32));
            memcpy(slot->texcoords, attrib.texcoords, (gX_U64)attrib.num_vertices * 2 * sizeof(gX_F32));
        }
    }
    tinyobj_attrib_free(&attrib);
    slot->done = true;
    return NULL;
}

//opencode: start async OBJ load — spawns worker thread for CPU-bound parsing
gX_I0 gX_Model_AsyncLoadStart(const gX_I8 *path)
{
    if (!path || !path[0]) return;
    // Find a free slot
    gX_Model_AsyncSlot *slot = NULL;
    for (gX_I32 i = 0; i < GX_MODEL_ASYNC_MAX; i++) {
        if (!gX_modelAsyncQueue[i].busy) { slot = &gX_modelAsyncQueue[i]; break; }
    }
    if (!slot) { gX_TraceLog(gX_LOG_WARNING, "MODEL: async queue full, loading synchronously"); return; }

    memset(slot, 0, sizeof(*slot));
    strncpy(slot->path, path, sizeof(slot->path) - 1);
    slot->busy = true;
    slot->done = false;
    gX_Thread_Create(&slot->thread, gX_Model_AsyncThreadFunc, slot);
    gX_TraceLog(gX_LOG_INFO, "MODEL: async parse started for '%s'", path);
}

//opencode: check if an async OBJ load has completed
gX_I1 gX_Model_AsyncLoadIsDone(const gX_I8 *path)
{
    for (gX_I32 i = 0; i < GX_MODEL_ASYNC_MAX; i++) {
        gX_Model_AsyncSlot *slot = &gX_modelAsyncQueue[i];
        if (slot->busy && strcmp(slot->path, path) == 0) return slot->done;
    }
    return false;
}

//opencode: retrieve parsed OBJ data (main thread) and create a raylib Model from it.
//   Must be called after AsyncLoadIsDone returns true. Frees the async slot.
gX_Model gX_Model_AsyncLoadFinish(const gX_I8 *path)
{
    gX_Model result = {0};
    Mesh rlMesh = {0};
    Model rlModel = {0};
    gX_Model_AsyncSlot *slot = NULL;
    for (gX_I32 i = 0; i < GX_MODEL_ASYNC_MAX; i++) {
        if (gX_modelAsyncQueue[i].busy && strcmp(gX_modelAsyncQueue[i].path, path) == 0) {
            slot = &gX_modelAsyncQueue[i]; break;
        }
    }
    if (!slot) return result;
    gX_Thread_Join(slot->thread);

    if (slot->errorMsg[0]) {
        gX_TraceLog(gX_LOG_ERROR, "MODEL: async load failed for '%s': %s", path, slot->errorMsg);
        goto cleanup;
    }
    if (!slot->positions || slot->vertexCount == 0) {
        gX_TraceLog(gX_LOG_ERROR, "MODEL: async load returned no vertices for '%s'", path);
        goto cleanup;
    }

    // Create raylib Mesh from the parsed vertex data
    rlMesh.vertexCount = (int)slot->vertexCount;
    rlMesh.triangleCount = (int)(slot->faceCount);
    rlMesh.vertices = slot->positions;     // transfer ownership
    rlMesh.normals   = slot->normals;
    rlMesh.texcoords = slot->texcoords;
    gX_RHI_Mesh_Upload((gX_Mesh *)&rlMesh, false);

    // Build raylib Model from the mesh
    //opencode: D12 — backend loader
    rlModel = gX_Model_ConvertToBackend(raylib_Model_LoadFromMesh(gX_Mesh_ConvertFromBackend(rlMesh)));
    if (rlModel.meshCount > 0 && rlModel.meshes) {
        rlModel.meshes[0] = rlMesh; // replace with our uploaded mesh
    }
    result = gX_Model_ConvertFromBackend(rlModel);

cleanup:
    free(slot->positions);
    free(slot->normals);
    free(slot->texcoords);
    memset(slot, 0, sizeof(*slot));
    return result;
}

//opencode: N64 stub — no threads/tinyobj on N64; callers treat as done.

//opencode: diagnostic flags — see forensic thought 008 (disabled by default,
//   re-enable by setting the function body to return 1 for debugging)
gX_I1 gX_DIAG_GFX_PIPELINE_GET(void);
gX_I1 gX_DIAG_GFX_PIPELINE_SET(gX_I1 v);
gX_I1 gX_DIAG_GFX_SHADER_STATE_GET(void);
gX_I1 gX_DIAG_GFX_SHADER_STATE_SET(gX_I1 v);
static gX_I1 s_gX_DIAG = 0;
gX_I1 gX_DIAG_GFX_PIPELINE_GET(void) { return s_gX_DIAG; }
gX_I1 gX_DIAG_GFX_PIPELINE_SET(gX_I1 v) { s_gX_DIAG = v; return v; }
gX_I1 gX_DIAG_GFX_SHADER_STATE_GET(void) { return s_gX_DIAG; }
gX_I1 gX_DIAG_GFX_SHADER_STATE_SET(gX_I1 v) { s_gX_DIAG = v; return v; }

gX_Material gX_Material_LoadDefault(gX_I0) {
    //Material material = LoadMaterialDefault();
    //return gX_Material_ConvertFromBackend(material);
    return raylib_Material_LoadDefault();
}

gX_Mesh gX_Mesh_GenCube(gX_F32 width, gX_F32 height, gX_F32 length) {
    gX_TraceLog_WRAPPERS(gX_LOG_INFO,"Generating cube mesh");
    Mesh mes = GenMeshCube(width,height,length);
    gX_Mesh mesh = gX_Mesh_ConvertFromBackend(mes);
    return mesh;
}

//opencode: added GenPlane for water renderer
gX_Mesh gX_Mesh_GenPlane(gX_F32 width, gX_F32 length, gX_I32 resX, gX_I32 resZ) {
    Mesh mes = GenMeshPlane(width, length, resX, resZ);
    return gX_Mesh_ConvertFromBackend(mes);
}

//opencode: added GenCylinder implementation for clock renderer
gX_Mesh gX_Mesh_GenCylinder(gX_F32 radius, gX_F32 height, gX_I32 slices) {
    Mesh mes = GenMeshCylinder(radius, height, slices);
    return gX_Mesh_ConvertFromBackend(mes);
}

//ai: Z-aligned cylinder mesh — raylib generates Y-aligned cylinders while
//   ODE's cylinder geometry (and so the gX cylinder body) runs along Z;
//   rotating the generated vertices about X keeps physics and pixels
//   aligned without a render-side base rotation.
gX_Mesh gX_Mesh_GenCylinderZ(gX_F32 radius, gX_F32 height, gX_I32 slices) {
    Mesh mes = GenMeshCylinder(radius, height, slices);
    for (int v = 0; v < mes.vertexCount; v++) {
        gX_F32 x = mes.vertices[v * 3 + 0];
        gX_F32 y = mes.vertices[v * 3 + 1];
        gX_F32 z = mes.vertices[v * 3 + 2];
        mes.vertices[v * 3 + 0] = x;
        mes.vertices[v * 3 + 1] = z;
        mes.vertices[v * 3 + 2] = -y;
        if (mes.normals) {
            gX_F32 nx = mes.normals[v * 3 + 0];
            gX_F32 ny = mes.normals[v * 3 + 1];
            gX_F32 nz = mes.normals[v * 3 + 2];
            mes.normals[v * 3 + 0] = nx;
            mes.normals[v * 3 + 1] = nz;
            mes.normals[v * 3 + 2] = -ny;
        }
    }
    return gX_Mesh_ConvertFromBackend(mes);
}

//ai: height-grid mesh — the visual twin of a wrapper heightfield: the
//   heights are row-major (x fastest, same as gX_PhysicsWorld_AddHeightfield)
//   and `size` scales (width, height, depth); the field is centered like the
//   collider. Normals come from the triangle faces.
gX_Mesh gX_Mesh_GenHeightGrid(const gX_F32 *heights, gX_I32 widthSamples,
    gX_I32 depthSamples, gX_F32v3 size) {
    Mesh mes = {0};
    if (!heights || widthSamples < 2 || depthSamples < 2) return gX_Mesh_ConvertFromBackend(mes);
    gX_I32 vertexCount = widthSamples * depthSamples;
    gX_I32 indexCount = (widthSamples - 1) * (depthSamples - 1) * 6;
    mes.vertexCount = vertexCount;
    mes.triangleCount = indexCount / 3;
    mes.vertices = (float *)MemAlloc((unsigned int)(vertexCount * 3)
        * sizeof(float));
    mes.normals = (float *)MemAlloc((unsigned int)(vertexCount * 3)
        * sizeof(float));
    mes.texcoords = (float *)MemAlloc((unsigned int)(vertexCount * 2)
        * sizeof(float));
    mes.indices = (unsigned short *)MemAlloc((unsigned int)indexCount
        * sizeof(unsigned short));
    if (!mes.vertices || !mes.normals || !mes.texcoords || !mes.indices) {
        if (mes.vertices) MemFree(mes.vertices);
        if (mes.normals) MemFree(mes.normals);
        if (mes.texcoords) MemFree(mes.texcoords);
        if (mes.indices) MemFree(mes.indices);
        Mesh empty = {0};
        return gX_Mesh_ConvertFromBackend(empty);
    }
    for (gX_I32 i = 0; i < widthSamples; i++) {
        for (gX_I32 j = 0; j < depthSamples; j++) {
            gX_I32 v = i * depthSamples + j;
            gX_F32 u = (gX_F32)i / (gX_F32)(widthSamples - 1);
            gX_F32 w = (gX_F32)j / (gX_F32)(depthSamples - 1);
            mes.vertices[v * 3 + 0] = (u - 0.5f) * size.x;
            mes.vertices[v * 3 + 1] = heights[v] * size.y;
            mes.vertices[v * 3 + 2] = (w - 0.5f) * size.z;
            mes.texcoords[v * 2 + 0] = u;
            mes.texcoords[v * 2 + 1] = w;
            mes.normals[v * 3 + 0] = 0.0f;
            mes.normals[v * 3 + 1] = 1.0f;
            mes.normals[v * 3 + 2] = 0.0f;
        }
    }
    gX_I32 index = 0;
    for (gX_I32 i = 0; i < widthSamples - 1; i++) {
        for (gX_I32 j = 0; j < depthSamples - 1; j++) {
            unsigned short a = (unsigned short)(i * depthSamples + j);
            unsigned short b = (unsigned short)((i + 1) * depthSamples + j);
            unsigned short c = (unsigned short)((i + 1) * depthSamples + j + 1);
            unsigned short d = (unsigned short)(i * depthSamples + j + 1);
            mes.indices[index++] = a;
            mes.indices[index++] = b;
            mes.indices[index++] = c;
            mes.indices[index++] = a;
            mes.indices[index++] = c;
            mes.indices[index++] = d;
        }
    }
    //ai: face normals (the flat grid needs no smoothing for terrain).
    for (gX_I32 t = 0; t < mes.triangleCount; t++) {
        unsigned short ia = mes.indices[t * 3 + 0];
        unsigned short ib = mes.indices[t * 3 + 1];
        unsigned short ic = mes.indices[t * 3 + 2];
        gX_F32 ax = mes.vertices[ia * 3], ay = mes.vertices[ia * 3 + 1],
            az = mes.vertices[ia * 3 + 2];
        gX_F32 bx = mes.vertices[ib * 3], by = mes.vertices[ib * 3 + 1],
            bz = mes.vertices[ib * 3 + 2];
        gX_F32 cx = mes.vertices[ic * 3], cy = mes.vertices[ic * 3 + 1],
            cz = mes.vertices[ic * 3 + 2];
        gX_F32 e1x = bx - ax, e1y = by - ay, e1z = bz - az;
        gX_F32 e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
        gX_F32 nx = e1y * e2z - e1z * e2y;
        gX_F32 ny = e1z * e2x - e1x * e2z;
        gX_F32 nz = e1x * e2y - e1y * e2x;
        gX_F32 len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 1e-6f) {
            nx /= len;
            ny /= len;
            nz /= len;
        } else {
            nx = 0.0f;
            ny = 1.0f;
            nz = 0.0f;
        }
        mes.normals[ia * 3 + 0] = nx;
        mes.normals[ia * 3 + 1] = ny;
        mes.normals[ia * 3 + 2] = nz;
        mes.normals[ib * 3 + 0] = nx;
        mes.normals[ib * 3 + 1] = ny;
        mes.normals[ib * 3 + 2] = nz;
        mes.normals[ic * 3 + 0] = nx;
        mes.normals[ic * 3 + 1] = ny;
        mes.normals[ic * 3 + 2] = nz;
    }
    return gX_Mesh_ConvertFromBackend(mes);
}

gX_Mesh gX_Mesh_GenSphere(gX_F32 radius, gX_I32 rings, gX_I32 slices) {
    gX_TraceLog_WRAPPERS(gX_LOG_INFO,"Generating sphere mesh");
    //Mesh mes = GenMeshSphere(radius,rings,slices);
    gX_Mesh mesh = raylib_Mesh_GenSphere(radius,rings,slices);
    //gX_TraceLog(gX_LOG_DEBUG,"WRAPPERS: mes.vboId %i",mes.vboId);
    gX_TraceLog_WRAPPERS(gX_LOG_DEBUG,"done1");
    //gX_Mesh mesh = gX_MeshPtr_ConvertFromBackend(&mes);
    gX_TraceLog_WRAPPERS(gX_LOG_DEBUG,"done2");
    return mesh;
}

//opencode: generic torus primitive
gX_Mesh gX_Mesh_GenTorus(gX_F32 radius, gX_F32 size, gX_I32 radSeq, gX_I32 sides) {
    Mesh mes = GenMeshTorus(radius, size, radSeq, sides);
    return gX_Mesh_ConvertFromBackend(mes);
}

//opencode: generic torus-knot primitive
gX_Mesh gX_Mesh_GenKnot(gX_F32 radius, gX_F32 size, gX_I32 radSeq, gX_I32 sides) {
    Mesh mes = GenMeshKnot(radius, size, radSeq, sides);
    return gX_Mesh_ConvertFromBackend(mes);
}

gX_I0 gX_Mesh_Unload(gX_Mesh mesh) {
    //opencode: D12 — backend loader
    raylib_Mesh_Unload(mesh);
}

gX_I0 gX_Mesh_Draw(gX_Mesh mesh, gX_Material material, gX_Matrix transform) {
    //opencode: routing — check if the material's shader program is registered for raw draw.
    if (gX_GL_IsRawProgram((gX_U32)material.shader.id)) {
        gX_Raw_Mesh_Draw(mesh);
    } else {
        raylib_Mesh_Draw(mesh, material, transform);
    }
}

gX_I0 gX_Mesh_GenTangents(gX_Mesh *mesh) {
    //GenMeshTangents((Mesh*)mesh);
    raylib_Mesh_GenTangents(mesh);
}

gX_Model gX_Model_Load(const gX_I8 *path) {
    //opencode: D12 — backend loader
    Model modell = gX_Model_ConvertToBackend(raylib_Model_Load(path));
    //opencode: v0.3.8 - guest fallback: registered guest roots may carry the
    //   model when gX's own resources lack it (loaded from GX_ASSETS dirs)
    if (modell.meshCount == 0) {
        const gX_I8 *guest = gX_Path_GuestFallback("model", path);
        if (guest) {
            gX_TraceLog(gX_LOG_INFO, "MODEL: guest fallback '%s' -> '%s'", path ? path : "?", guest);
            modell = gX_Model_ConvertToBackend(raylib_Model_Load(guest));
        }
    }
    if (modell.meshCount == 0) {
        gX_TraceLog(gX_LOG_WARNING, "MODEL: Failed to load '%s' — check file exists and format", path ? path : "null");
        return (gX_Model){0};
    }
    //GenMeshTangents(&modell.meshes[0]);
    gX_Model model = gX_Model_ConvertFromBackend(modell);
    //opencode: mipmap + trilinear for material maps (forest speckle hunt):
    //   loaders upload the base level only, so minification aliases (2K
    //   bark on 2cm twigs sparkled black at distance). 11 = map slots in
    //   gX_MATERIALMAPINDEX (common/enum.h X-list); NPOT mips are core GL.
    for (gX_I32 mai = 0; mai < model.materialCount; mai++) {
        if (model.materials[mai].maps == NULL) continue;
        for (gX_I32 mpi = 0; mpi < 11; mpi++) {
            gX_U32 tid = model.materials[mai].maps[mpi].texture.id;
            if (tid == 0) continue;
            gX_RHI_Tex_GenMipmap(tid, gX_RHI_TEXTURE_2D);
            gX_RHI_Tex_Param(tid, gX_RHI_TEXTURE_2D,
                gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_LINEAR_MIPMAP_LINEAR);
        }
    }
    return model;
}

gX_Model gX_Model_LoadMesh(gX_Mesh mesh) {
    //opencode: D12 — backend loader
    return raylib_Model_LoadFromMesh(mesh);
}

gX_I0 gX_Model_Unload(gX_Model model) {
    //opencode: D12 — backend loader
    raylib_Model_Unload(model);
}

gX_I0 gX_Model_Draw(gX_Model model) {
    if (model.meshCount == 0) return;
    //opencode: D10 — delegate to DrawEx (RHI path via raylib_Mesh_Draw);
    //   replaces raylib DrawModel (rlgl batch path).
    gX_Model_DrawEx(model, (gX_F32v3){0.0f, 0.0f, 0.0f}, (gX_F32v3){0.0f, 1.0f, 0.0f},
        0.0f, (gX_F32v3){1.0f, 1.0f, 1.0f}, (gX_Color){255, 255, 255, 255});
}

//opencode: wireframe draw wrapper for wireframe renderer
gX_I0 gX_Model_Draw_Wires(gX_Model model) {
    //opencode: D10 — unified RHI wireframe draw for both backends (was:
    //   raylib DrawModelWires for raylib, direct-VAO path for GLFW). The
    //   rlgl DrawMesh path is bypassed entirely: set default program + MVP
    //   + white diffuse + texture0, bind the VAO, draw with LINE mode.
    //   The model data is valid (VAO created by UploadMesh); only the rlgl
    //   draw path was broken on GLFW Core Profile.
    {
        gX_U32 program = gX_RHI_Shader_GetDefault();
        if (program > 0 && model.meshCount > 0 && model.meshes != NULL) {
            for (gX_I32 mi = 0; mi < model.meshCount; mi++) {
                gX_U32 vaoId = model.meshes[mi].vaoId;
                gX_I32 triCount = model.meshes[mi].triangleCount;
                if (vaoId > 0 && triCount > 0) {
                    gX_I32 locMVP = gX_RHI_GetUniformLocation(program, "mvp");
                    gX_I32 locDiff = gX_RHI_GetUniformLocation(program, "colDiffuse");
                    gX_I32 locTex0 = gX_RHI_GetUniformLocation(program, "texture0");

                    gX_RHI_Shader_Use((gX_U32)program);

                    gX_Matrix view = gX_RHI_Matrix_GetModelview();
                    gX_Matrix proj = gX_RHI_Matrix_GetProjection();
                    gX_Matrix mvp = gX_Matrix_Multiply(proj, gX_Matrix_Multiply(view, model.transform));
                    if (locMVP >= 0) gX_RHI_UniformMatrix(locMVP, 1, 1, &mvp.m0);

                    float cd[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                    if (locDiff >= 0) gX_RHI_UniformVec4(locDiff, 1, cd);

                    gX_RHI_Tex_BindOnUnit(gX_RHI_Tex_GetDefault(), 0);
                    if (locTex0 >= 0) gX_RHI_UniformInt(locTex0, 0);

                    gX_RHI_PolygonMode(gX_RHI_FRONT_AND_BACK, gX_RHI_LINE);
                    gX_RHI_VAO_Bind(vaoId);
                    //opencode: index type is ALWAYS short (U16 struct
                    //   contract - see gl_draw.c).
                    gX_U32 idxType = gX_RHI_UNSIGNED_SHORT;
                    if (model.meshes[mi].indices) {
                        gX_RHI_DrawElements(gX_RHI_TRIANGLES, (gX_U32)(triCount*3), idxType, NULL);
                    } else {
                        gX_RHI_DrawArrays(gX_RHI_TRIANGLES, 0, (gX_U32)model.meshes[mi].vertexCount);
                    }
                    gX_RHI_VAO_Bind(0);
                    gX_RHI_PolygonMode(gX_RHI_FRONT_AND_BACK, gX_RHI_FILL);
                }
            }
        }
    }
}

//opencode: GLFW bypass — raylib's DrawModel/DrawMesh has texture sampler
//   binding issues in the GLFW backend (texture0 sampler location may be -1,
//   material textures may not bind). We do a MINIMAL bypass: just bind the
//   VAO and call glDrawElements. We do NOT touch the shader, uniforms, or
//   textures — those were set up by the caller (e.g., gX_RendererGFX_Model_Draw
//   for the GFX scene, which sets 90+ PBR uniforms before calling us).
//   We also do NOT call rlEnableShader or change matrices, since the caller
//   has already set up the render state. This way the GFX PBR shader's
//   carefully-tuned uniforms (enableLighting, hasAlbedoMap, metalness, etc.)
//   remain in effect, and the bypass only does what raylib's DrawMesh does
//   but correctly: issue the glDrawElements call.
//
//opencode: v2 — found that the GFX PBR shader's vertex shader needs `mvp`,
//   `matModel`, `matNormal` uniforms which the GFX code does NOT set before
//   calling the bypass. With these at default (zero) values, `gl_Position`
//   becomes (0,0,0,0) and all triangles are degenerate → no fragments. We now
//   compute and upload these matrices in the bypass. Other uniforms
//   (viewPosition, lightPosition, etc. in the gX_Std UBO) are still set by
//   the GFX code via gX_Shader_SetValue_* — we don't touch those.
gX_I0 gX_Model_Draw_Parameters(gX_Model model, gX_Model_Parameters params) {
    //opencode: delegate to gX_Model_DrawEx which uses raylib_Mesh_Draw (works with
    //   custom shaders). DrawMesh (used previously) silently fails with custom
    //   shaders (water renderer) — the root cause was that params.scale defaults
    //   to 0 when not set by the caller, causing the model to render at zero scale
    //   (invisible). Default to 1.0 if scale is 0.
    gX_F32 s = (params.scale == 0.0f) ? 1.0f : params.scale;
    //opencode: honor params.color as tint — was hardcoded white, so element
    //   color had zero effect on model rendering. Zero color = not set = white.
    gX_Color tint = params.color;
    if (tint.r == 0 && tint.g == 0 && tint.b == 0 && tint.a == 0) {
        tint = (gX_Color){255, 255, 255, 255};
    }
    gX_Model_DrawEx(model,
        (gX_F32v3){params.position.x, params.position.y, params.position.z},
        (gX_F32v3){0, 1, 0}, 0,
        (gX_F32v3){s, s, s},
        tint);
}
//opencode: draw model with an externally-bound shader program (the caller has
//   already enabled it and uploaded element uniforms). Unlike raylib_Mesh_Draw
//   (which re-enables the material shader and restores the default at the end,
//   so an external BeginShader bracket can't survive), this path uses the
//   explicit program + locs for matrices and binds only the diffuse map to
//   unit 0 (sampler via locs[5], e.g. "texture0").
//   locs = {mvp, matModel, matNormal, matView, matProjection, texture0}
//   (-1 entries are skipped). The material diffuse COLOR is not applied —
//   the element shader's albedoColor uniform handles color.
gX_I0 gX_Model_Draw_Parameters_WithShader(gX_Model model, gX_Model_Parameters params,
    gX_U32 programId, const gX_I32 *locs) {
    if (!locs || programId == 0) return;
    gX_F32 s = (params.scale == 0.0f) ? 1.0f : params.scale;
    if (s == 0.0f) s = 1.0f;

    gX_Matrix matScale = gX_Matrix_Scale(s, s, s);
    gX_Matrix matTranslation = gX_Matrix_Translate(params.position.x, params.position.y, params.position.z);
    gX_Matrix matTransform = gX_Matrix_Multiply(matScale, matTranslation);
    gX_Matrix modelMat = gX_Matrix_Multiply(model.transform, matTransform);

    gX_Matrix view = gX_RHI_Matrix_GetModelview();
    gX_Matrix proj = gX_RHI_Matrix_GetProjection();
    gX_Matrix normalMat = gX_Matrix_Transpose(gX_Matrix_Invert(modelMat));
    gX_Matrix mvp = gX_Matrix_Multiply(proj, gX_Matrix_Multiply(view, modelMat));

    gX_RHI_Shader_Use(programId);
    if (locs[0] >= 0) gX_RHI_UniformMatrix(locs[0], 1, 1, &mvp.m0);
    if (locs[1] >= 0) gX_RHI_UniformMatrix(locs[1], 1, 1, &modelMat.m0);
    if (locs[2] >= 0) gX_RHI_UniformMatrix(locs[2], 1, 1, &normalMat.m0);
    if (locs[3] >= 0) gX_RHI_UniformMatrix(locs[3], 1, 1, &view.m0);
    if (locs[4] >= 0) gX_RHI_UniformMatrix(locs[4], 1, 1, &proj.m0);

    for (gX_I32 i = 0; i < model.meshCount; i++) {
        gX_I32 matIdx = (model.meshMaterial != NULL) ? model.meshMaterial[i] : 0;
        if (matIdx >= model.materialCount) matIdx = 0;
        gX_Material *mat = (model.materialCount > 0) ? &model.materials[matIdx] : NULL;

        gX_Mesh mesh = model.meshes[i];
        if (mat && mat->maps[MATERIAL_MAP_DIFFUSE].texture.id > 0) {
            gX_RHI_ActiveTexture(gX_RHI_TEXTURE0);
            gX_RHI_Tex_Bind(mat->maps[MATERIAL_MAP_DIFFUSE].texture.id, gX_RHI_TEXTURE_2D);
            if (locs[5] >= 0) gX_RHI_UniformInt(locs[5], 0);
        }
        if (mesh.vaoId == 0) continue;
        gX_RHI_VAO_Bind(mesh.vaoId);
        if (mesh.indices) {
            //opencode: index type is ALWAYS short (U16 struct contract).
            gX_U32 idxType = gX_RHI_UNSIGNED_SHORT;
            gX_RHI_DrawElements(gX_RHI_TRIANGLES, (gX_U32)(mesh.triangleCount * 3), idxType, NULL);
        } else {
            gX_RHI_DrawArrays(gX_RHI_TRIANGLES, 0, (gX_U32)mesh.vertexCount);
        }
        gX_RHI_VAO_Bind(0);
    }
}

//opencode: draw model with extended transform (uses raylib DrawModelEx)
gX_I0 gX_Model_DrawEx(gX_Model model, gX_F32v3 position,
                       gX_F32v3 rotationAxis, gX_F32 rotationAngle,
                       gX_F32v3 scale, gX_Color tint) {
    raylib_Model_DrawEx(model, position, rotationAxis, rotationAngle, scale, tint);
}

gX_RayCollision gX_RayCollision_Sphere(gX_Ray ray, gX_Sphere sphere) {
    Ray r = gX_Ray_ConvertToBackend(ray);
    RayCollision col = GetRayCollisionSphere(r,(Vector3){sphere.pos.x,sphere.pos.y,sphere.pos.z},sphere.radius);
    return gX_RayCollision_ConvertFromBackend(col);
}

gX_RayCollision gX_RayCollision_Mesh(gX_Ray ray, gX_Mesh mesh, gX_Matrix matrix) {
    Ray r = gX_Ray_ConvertToBackend(ray);
    Mesh m = gX_Mesh_ConvertToBackend(mesh);
    Matrix mat = gX_Matrix_ConvertToBackend(matrix);
    RayCollision col = GetRayCollisionMesh(r,m,mat);
    return gX_RayCollision_ConvertFromBackend(col);
}

//opencode: Model_Wrapper implementation (moved from common/utils.c)

gX_I0 gX_Model_Wrapper_Load(gX_Model_Wrapper *mw) {
    mw->model = gX_Model_Load(mw->path);
    mw->loadingState.loaded = true;
}

gX_I0 gX_Model_Wrapper_Unload(gX_Model_Wrapper *mw) {
    gX_Model_Unload(mw->model);
    mw->loadingState.loaded = false;
}

gX_I0 gX_Model_Wrapper_Update(gX_Model_Wrapper *mw) {
    gX_I1 loadasapstate = (maindata.backendLoaded && mw->loadingState.loadAsap);
    gX_I1 loadbeforestate = false;
    gX_I1 unloadafterstate = false;
    if ((mw->loadingState.load || loadasapstate || loadbeforestate) && !(mw->loadingState.loaded)) {
        gX_Model_Wrapper_Load(mw);
    }
    if ((mw->loadingState.unload || unloadafterstate) && mw->loadingState.loaded) {
        gX_Model_Wrapper_Unload(mw);
    }
    if (mw->loadingState.reload && mw->loadingState.loaded) {
        gX_Model_Wrapper_Unload(mw);
        gX_Model_Wrapper_Load(mw);
    }
}
