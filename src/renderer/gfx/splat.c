/**********************************************************************************************
*
*   [gX: src/renderer/gfx/splat.c]
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


//opencode: 3D gaussian splat fixture — seeded procedural cloud, projected
//   2D covariance rasterizer. Sim (cloud build) and render (project, conic,
//   sort, upload) are separate stages. Heap buffers only — no BSS growth.
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../gfx.h"
#include "splat.h"
#include "boilerplate.h"
#include "../renderer.h"

#include "../../main.h"
#include "../../camera.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_state.h"
#include "../../rhi/rhi_buf.h"
#include "../../rhi/gl_state.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/profiler.h"
#include "../../common/path.h"

#include "../../gxmath/matrix.h"
#include "../../gxmath/gxmath.h"

//opencode: deterministic LCG — cloud randomness without rand() pollution.
static gX_F32 Splat_NextRandom(gX_RendererGFX_Splat *splat)
{
    return gX_Random32_F32(&splat->rng);
}

//opencode: read shader source from file into heap-allocated buffer.
static gX_I8 *Splat_ReadFile(const gX_I8 *path, gX_I64 *outLen)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { gX_TraceLog_GFX(gX_LOG_ERROR, "Shader not found: %s", path); return NULL; }
    fseek(fp, 0, SEEK_END);
    gX_I64 len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    gX_I8 *src = (gX_I8*)malloc((size_t)(len + 1));
    if (!src) { fclose(fp); return NULL; }
    size_t nread = fread(src, 1, (size_t)len, fp);
    src[nread] = '\0';
    fclose(fp);
    if (outLen) *outLen = len;
    return src;
}

//opencode: fibonacci-sphere shell with seeded jitter — normal-mapped colors.
static gX_I0 Splat_BuildCloud(gX_RendererGFX_Splat *splat)
{
    gX_F32 golden = 2.3999632f;
    for (gX_U32 i = 0; i < gX_SPLAT_MAX; i++)
    {
        gX_F32 t = (gX_F32)i / (gX_F32)gX_SPLAT_MAX;
        gX_F32 z = 1.0f - 2.0f * t;
        gX_F32 radius = sqrtf(1.0f - z * z);
        gX_F32 theta = golden * (gX_F32)i + Splat_NextRandom(splat) * 0.50f;
        gX_F32 nx = radius * cosf(theta);
        gX_F32 ny = radius * sinf(theta);
        gX_F32 nz = z;
        splat->posX[i] = splat->centerX + nx * splat->radius;
        splat->posY[i] = splat->centerY + ny * splat->radius;
        splat->posZ[i] = splat->centerZ + nz * splat->radius;
        splat->colR[i] = 0.50f + 0.50f * nx;
        splat->colG[i] = 0.50f + 0.50f * ny;
        splat->colB[i] = 0.50f + 0.50f * nz;
    }
    return;
}

//opencode: read the deterministic splat fixture hooks.
static gX_I0 Splat_ApplyEnv(gX_RendererGFX_Splat *splat)
{
    const gX_I8 *seedHook = getenv("GX_SPLAT_SEED");
    const gX_I8 *countHook = getenv("GX_SPLAT_COUNT");
    gX_U32 seed = seedHook ? (gX_U32)atoi((const char *)seedHook) : 777u;
    gX_I32 count = countHook ? (gX_I32)atoi((const char *)countHook) : 256;
    if (count < 0) count = 0;
    if (count > (gX_I32)gX_SPLAT_MAX) count = (gX_I32)gX_SPLAT_MAX;
    if (seed != splat->seed)
    {
        splat->seed = seed;
        gX_Random32_Seed(&splat->rng, (gX_U64)seed, 0u);
        Splat_BuildCloud(splat);
    }
    splat->targetCount = count;
    return;
}

gX_I0 gX_RendererGFX_Splat_Init(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Splat *splat = &gfx->splat;
    splat->seed = 777u;
    gX_Random32_Seed(&splat->rng, 777u, 0u);
    splat->targetCount = 256;
    splat->activeCount = 0;
    splat->sigma = 0.06f;
    splat->centerX = 0.0f;
    splat->centerY = 2.2f;
    splat->centerZ = -4.5f;
    splat->radius = 1.0f;
    splat->posX = (gX_F32*)malloc(gX_SPLAT_MAX * sizeof(gX_F32));
    splat->posY = (gX_F32*)malloc(gX_SPLAT_MAX * sizeof(gX_F32));
    splat->posZ = (gX_F32*)malloc(gX_SPLAT_MAX * sizeof(gX_F32));
    splat->colR = (gX_F32*)malloc(gX_SPLAT_MAX * sizeof(gX_F32));
    splat->colG = (gX_F32*)malloc(gX_SPLAT_MAX * sizeof(gX_F32));
    splat->colB = (gX_F32*)malloc(gX_SPLAT_MAX * sizeof(gX_F32));
    splat->depth = (gX_F32*)malloc(gX_SPLAT_MAX * sizeof(gX_F32));
    splat->conic = (gX_F32*)malloc(gX_SPLAT_MAX * 3 * sizeof(gX_F32));
    splat->eigenU = (gX_F32*)malloc(gX_SPLAT_MAX * 2 * sizeof(gX_F32));
    splat->eigenV = (gX_F32*)malloc(gX_SPLAT_MAX * 2 * sizeof(gX_F32));
    splat->order = (gX_U32*)malloc(gX_SPLAT_MAX * sizeof(gX_U32));
    splat->upload = (gX_F32*)malloc(gX_SPLAT_MAX * 6 * 16 * sizeof(gX_F32));
    splat->vbo = splat->vao = splat->renderProg = 0;
    splat->loaded = 0;
    splat->locsCached = 0;
    splat->uVP = splat->uHalfView = -1;
    if (!splat->posX || !splat->posY || !splat->posZ
        || !splat->colR || !splat->colG || !splat->colB
        || !splat->depth || !splat->conic || !splat->eigenU
        || !splat->eigenV || !splat->order || !splat->upload)
    {
        gX_TraceLog_GFX(gX_LOG_ERROR, "Splat: buffer allocation failed");
        return;
    }
    Splat_BuildCloud(splat);
    return;
}

gX_I0 gX_RendererGFX_Splat_Unload(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Splat *splat = &gfx->splat;
    if (splat->vbo > 0) { gX_RHI_Buffer_Delete(splat->vbo); splat->vbo = 0; }
    if (splat->vao > 0) { gX_RHI_VAO_Delete(splat->vao); splat->vao = 0; }
    if (splat->renderProg > 0) { gX_RHI_Shader_Delete(splat->renderProg); splat->renderProg = 0; }
    free(splat->posX); splat->posX = NULL;
    free(splat->posY); splat->posY = NULL;
    free(splat->posZ); splat->posZ = NULL;
    free(splat->colR); splat->colR = NULL;
    free(splat->colG); splat->colG = NULL;
    free(splat->colB); splat->colB = NULL;
    free(splat->depth); splat->depth = NULL;
    free(splat->conic); splat->conic = NULL;
    free(splat->eigenU); splat->eigenU = NULL;
    free(splat->eigenV); splat->eigenV = NULL;
    free(splat->order); splat->order = NULL;
    free(splat->upload); splat->upload = NULL;
    splat->loaded = 0;
    splat->locsCached = 0;
    return;
}

gX_I0 gX_RendererGFX_Splat_Update(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Splat *splat = &gfx->splat;
    if (!gfx->state.ENABLE_SPLAT) { splat->activeCount = 0; return; }
    if (!splat->loaded && maindata.backendLoaded)
    {
        splat->vao = gX_RHI_VAO_Create("splat_vao");
        splat->vbo = gX_RHI_Buffer_Create("splat_vbo");
        gX_RHI_VAO_Bind(splat->vao);
        gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, splat->vbo);
        gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, (gX_U32)(gX_SPLAT_MAX * 6 * 16 * sizeof(gX_F32)), NULL, gX_RHI_DYNAMIC_DRAW);
        gX_RHI_Attrib_Pointer(0, 3, gX_RHI_FLOAT, 0, (gX_I32)(16 * sizeof(gX_F32)), (void*)0);
        gX_RHI_Attrib_Enable(0);
        gX_RHI_Attrib_Pointer(1, 2, gX_RHI_FLOAT, 0, (gX_I32)(16 * sizeof(gX_F32)), (void*)(3 * sizeof(gX_F32)));
        gX_RHI_Attrib_Enable(1);
        gX_RHI_Attrib_Pointer(2, 3, gX_RHI_FLOAT, 0, (gX_I32)(16 * sizeof(gX_F32)), (void*)(5 * sizeof(gX_F32)));
        gX_RHI_Attrib_Enable(2);
        gX_RHI_Attrib_Pointer(3, 3, gX_RHI_FLOAT, 0, (gX_I32)(16 * sizeof(gX_F32)), (void*)(8 * sizeof(gX_F32)));
        gX_RHI_Attrib_Enable(3);
        gX_RHI_Attrib_Pointer(4, 1, gX_RHI_FLOAT, 0, (gX_I32)(16 * sizeof(gX_F32)), (void*)(11 * sizeof(gX_F32)));
        gX_RHI_Attrib_Enable(4);
        gX_RHI_Attrib_Pointer(5, 2, gX_RHI_FLOAT, 0, (gX_I32)(16 * sizeof(gX_F32)), (void*)(12 * sizeof(gX_F32)));
        gX_RHI_Attrib_Enable(5);
        gX_RHI_Attrib_Pointer(6, 2, gX_RHI_FLOAT, 0, (gX_I32)(16 * sizeof(gX_F32)), (void*)(14 * sizeof(gX_F32)));
        gX_RHI_Attrib_Enable(6);
        gX_RHI_VAO_Bind(0);
        gX_I8 vertPath[gX_GFX_PATH_BUFFER];
        gX_I8 fragPath[gX_GFX_PATH_BUFFER];
        snprintf(vertPath, sizeof(vertPath), "%s", GX_PATH("shader", "gfx_splat.vert"));
        snprintf(fragPath, sizeof(fragPath), "%s", GX_PATH("shader", "gfx_splat.frag"));
        gX_I8 *vertSrc = Splat_ReadFile(vertPath, NULL);
        gX_I8 *fragSrc = Splat_ReadFile(fragPath, NULL);
        if (vertSrc && fragSrc)
        {
            gX_I8 errorLog[1024] = {0};
            splat->renderProg = gX_RHI_Shader_Compile(vertSrc, NULL, fragSrc, "splat_render", errorLog, sizeof(errorLog));
            if (splat->renderProg == 0) gX_TraceLog_GFX(gX_LOG_ERROR, "Splat render shader: %s", errorLog);
        }
        if (vertSrc) free(vertSrc);
        if (fragSrc) free(fragSrc);
        if (splat->renderProg > 0 && splat->vao > 0 && splat->vbo > 0) splat->loaded = 1;
        gX_TraceLog_GFX(gX_LOG_NOTICE, "Splat: render=%u vao=%u vbo=%u",
            splat->renderProg, splat->vao, splat->vbo);
    }
    Splat_ApplyEnv(splat);
    if (splat->targetCount <= 0 || !splat->posX) { splat->activeCount = 0; return; }
    gX_F32 dx = camera.camera3D.position.x - camera.camera3D.target.x;
    gX_F32 dy = camera.camera3D.position.y - camera.camera3D.target.y;
    gX_F32 dz = camera.camera3D.position.z - camera.camera3D.target.z;
    gX_F32 dist = sqrtf(dx * dx + dy * dy + dz * dz);
    gX_I32 active = splat->targetCount;
    if (dist > 22.0f) active = active / 4;
    else if (dist > 12.0f) active = active / 2;
    if (active < 0) active = 0;
    splat->activeCount = active;
    return;
}

gX_I0 gX_RendererGFX_Splat_Draw(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Splat *splat = &gfx->splat;
    if (!gfx->state.ENABLE_SPLAT || !splat->loaded) return;
    if (splat->renderProg == 0 || splat->vao == 0 || splat->vbo == 0) return;
    if (splat->activeCount <= 0 || !splat->upload || !splat->posX || !splat->order) return;
    if (!maindata.backendLoaded) return;
    if (!splat->locsCached)
    {
        splat->uVP = gX_RHI_GetUniformLocation(splat->renderProg, "vp");
        splat->uHalfView = gX_RHI_GetUniformLocation(splat->renderProg, "halfView");
        splat->locsCached = 1;
    }
    gX_Matrix view = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
    gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix proj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect, camera.frustum.near, camera.frustum.far);
    gX_Matrix vp = gX_Matrix_Multiply(view, proj);
    gX_F32 halfTan = tanf(camera.camera3D.fovy * gX_DEG2RAD * 0.5f);
    gX_F32 focalY = ((gX_F32)maindata.renderHeight * 0.5f) / halfTan;
    gX_F32 focalX = focalY;
    gX_F32v3 forward = gX_F32v3_Normalize(gX_F32v3_Sub(camera.camera3D.target, camera.camera3D.position));
    gX_F32v3 right = gX_F32v3_Normalize(gX_F32v3_Cross(forward, camera.camera3D.up));
    gX_F32v3 upv = gX_F32v3_Cross(right, forward);
    gX_I32 active = splat->activeCount;
    if (active > (gX_I32)gX_SPLAT_MAX) active = (gX_I32)gX_SPLAT_MAX;
    gX_I32 kept = 0;
    gX_F32 sigma2 = splat->sigma * splat->sigma;
    for (gX_I32 i = 0; i < active; i++)
    {
        gX_U32 index = (gX_U32)i;
        gX_F32 relX = splat->posX[index] - camera.camera3D.position.x;
        gX_F32 relY = splat->posY[index] - camera.camera3D.position.y;
        gX_F32 relZ = splat->posZ[index] - camera.camera3D.position.z;
        gX_F32 viewX = relX * right.x + relY * right.y + relZ * right.z;
        gX_F32 viewY = relX * upv.x + relY * upv.y + relZ * upv.z;
        gX_F32 viewZ = relX * forward.x + relY * forward.y + relZ * forward.z;
        if (viewZ < camera.frustum.near + 0.10f) continue;
        gX_F32 inv = 1.0f / viewZ;
        gX_F32 jx = focalX * inv;
        gX_F32 jz = focalX * viewX * inv * inv;
        gX_F32 jy = focalY * inv;
        gX_F32 jw = focalY * viewY * inv * inv;
        gX_F32 m00 = jx * right.x - jz * forward.x;
        gX_F32 m01 = jx * right.y - jz * forward.y;
        gX_F32 m02 = jx * right.z - jz * forward.z;
        gX_F32 m10 = jy * upv.x - jw * forward.x;
        gX_F32 m11 = jy * upv.y - jw * forward.y;
        gX_F32 m12 = jy * upv.z - jw * forward.z;
        gX_F32 cov00 = sigma2 * (m00 * m00 + m01 * m01 + m02 * m02) + 0.09f;
        gX_F32 cov01 = sigma2 * (m00 * m10 + m01 * m11 + m02 * m12);
        gX_F32 cov11 = sigma2 * (m10 * m10 + m11 * m11 + m12 * m12) + 0.09f;
        gX_F32 det = cov00 * cov11 - cov01 * cov01;
        if (det < 0.0001f) det = 0.0001f;
        splat->conic[index * 3 + 0] = cov11 / det;
        splat->conic[index * 3 + 1] = -cov01 / det;
        splat->conic[index * 3 + 2] = cov00 / det;
        gX_F32 trace = cov00 + cov11;
        gX_F32 disc = sqrtf(trace * trace * 0.25f - det);
        if (!(disc >= 0.0f)) disc = 0.0f;
        gX_F32 len0 = trace * 0.50f + disc;
        gX_F32 len1 = trace * 0.50f - disc;
        if (len0 < 0.09f) len0 = 0.09f;
        if (len1 < 0.09f) len1 = 0.09f;
        gX_F32 ext0 = 3.0f * sqrtf(len0);
        gX_F32 ext1 = 3.0f * sqrtf(len1);
        if (ext0 > 48.0f) ext0 = 48.0f;
        if (ext1 > 48.0f) ext1 = 48.0f;
        gX_F32 axisX = cov01;
        gX_F32 axisY = len0 - cov00;
        gX_F32 axisLen = sqrtf(axisX * axisX + axisY * axisY);
        if (axisLen < 0.0001f) { axisX = 1.0f; axisY = 0.0f; axisLen = 1.0f; }
        splat->eigenU[index * 2 + 0] = axisX / axisLen * ext0;
        splat->eigenU[index * 2 + 1] = axisY / axisLen * ext0;
        splat->eigenV[index * 2 + 0] = -axisY / axisLen * ext1;
        splat->eigenV[index * 2 + 1] = axisX / axisLen * ext1;
        splat->depth[index] = viewZ;
        kept++;
    }
    gX_U32 *order = splat->order;
    for (gX_I32 i = 0; i < kept; i++) order[i] = (gX_U32)i;
    for (gX_I32 i = 1; i < kept; i++)
    {
        gX_U32 key = order[i];
        gX_F32 keyDepth = splat->depth[key];
        gX_I32 j = i - 1;
        while (j >= 0 && splat->depth[order[j]] < keyDepth)
        {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
    gX_F32 corners[6][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    gX_I32 vert = 0;
    for (gX_I32 i = 0; i < kept; i++)
    {
        gX_U32 index = order[i];
        if (index >= (gX_U32)active) continue;
        for (gX_I32 v = 0; v < 6; v++)
        {
            gX_F32 *dst = &splat->upload[vert * 16];
            dst[0] = splat->posX[index];
            dst[1] = splat->posY[index];
            dst[2] = splat->posZ[index];
            dst[3] = corners[v][0];
            dst[4] = corners[v][1];
            dst[5] = splat->conic[index * 3 + 0];
            dst[6] = splat->conic[index * 3 + 1];
            dst[7] = splat->conic[index * 3 + 2];
            dst[8] = splat->colR[index];
            dst[9] = splat->colG[index];
            dst[10] = splat->colB[index];
            dst[11] = 0.90f;
            dst[12] = splat->eigenU[index * 2 + 0];
            dst[13] = splat->eigenU[index * 2 + 1];
            dst[14] = splat->eigenV[index * 2 + 0];
            dst[15] = splat->eigenV[index * 2 + 1];
            vert++;
        }
    }
    if (vert <= 0) return;
    GX_PROFILE_ZONE_BEGIN("gfx:splat");
    gX_GL_State_Push();
    gX_RHI_Enable(gX_RHI_BLEND);
    gX_RHI_Blend_Func_Separate(gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA, gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA);
    gX_RHI_Depth_Write(0);
    gX_RHI_Enable(gX_RHI_DEPTH_TEST);
    gX_RHI_Depth_Func(gX_RHI_LEQUAL);
    gX_RHI_VAO_Bind(splat->vao);
    gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, splat->vbo);
    gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, (gX_U32)(vert * 16 * sizeof(gX_F32)), splat->upload, gX_RHI_DYNAMIC_DRAW);
    gX_RHI_Shader_Use(splat->renderProg);
    gX_RHI_UniformMatrix(splat->uVP, 1, 1, &vp.m0);
    gX_RHI_Uniform_Vec2(splat->uHalfView, (gX_F32)maindata.renderWidth * 0.5f, (gX_F32)maindata.renderHeight * 0.5f);
    gX_RHI_DrawArrays(gX_RHI_TRIANGLES, 0, (gX_U32)vert);
    gX_RHI_VAO_Bind(0);
    gX_RHI_Shader_Use(0);
    gX_GL_State_Pop();
    GX_PROFILE_ZONE_END("gfx:splat");
    return;
}
