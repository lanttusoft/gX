/**********************************************************************************************
*
*   [gX: src/renderer/gfx/clusters.c]
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


//opencode: v0.3.2 — Forward+ via CPU-culled tile clusters (GL330-safe). Each
//   16px tile gets ONE RG32UI 64-bit mask of covering light slots (bit i = upload
//   slot i) — ascending iteration order matches the legacy uniform-array loop
//   exactly, so shading math is bit-parity with the non-clustered path. The
//   32-slot ceiling equals the existing uniform-array cap; lifting it comes
//   with the compute upgrade (light data moves to SSBO alongside).

#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>

#include "../gfx.h"
#include "boilerplate.h"
#include "clusters.h"
#include "../renderer.h"

#include "../../wrappers/wrappers.h"

#include "../../main.h"
#include "../../camera.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/profiler.h"
#include "../../common/path.h"
#include "../../common/shader_compiler.h"

#include "../../gxmath/matrix.h"
#include "../../gxmath/constants.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_buf.h"
#include "../../rhi/rhi_gl.h"
#include "../../rhi/rhi_tex.h"

#define GX_CLUSTER_TILE       16

//opencode: effective radius where quadratic attenuation falls below eps
static float Cluster_EffectiveRadius(const gX_RendererGFX_Light *l)
{
    const float eps = 0.004f;
    float kq = l->attenQuad > 0.0f ? l->attenQuad : 0.0001f;
    float target = l->brightness / eps - l->attenConst;
    if (target <= 0.0f) return 0.0f;
    float disc = l->attenLinear * l->attenLinear + 4.0f * kq * target;
    float r = (-l->attenLinear + sqrtf(disc)) / (2.0f * kq);
    if (r < 0.0f) r = 0.0f;
    return r;
}

//opencode: CPU reference for the compute kernel — keep projection and tile
//   bound math in one place so the parity hook compares the actual fallback.
static gX_U32 Cluster_BuildCpuGrid(gX_RendererGFX *gfx, gX_U32 *grid,
    gX_I32 tilesX, gX_I32 tilesY)
{
    gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix view = gX_Matrix_LookAt(camera.camera3D.position,
        camera.camera3D.target, camera.camera3D.up);
    gX_Matrix proj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD,
        aspect, camera.frustum.near, camera.frustum.far);
    const gX_F32 projScaleY = (gX_F32)maindata.renderHeight
        / (2.0f * tanf(camera.camera3D.fovy * gX_DEG2RAD * 0.5f));
    gX_U32 usedCount = 0;
    memset(grid, 0, sizeof(gX_U32) * GX_CLUSTER_GRID_MAX * 2);
    GX_HEAP_FOREACH(gfx->lights, gX_RendererGFX_Light, light) {
        unsigned slot = (unsigned)(light - (gX_RendererGFX_Light*)gfx->lights.data);
        usedCount++;
        if (!light->sun && slot <
#ifdef GX_COMPUTE
            gX_GFX_MAX_LIGHTS_SSBO
#else
            32u
#endif
        ) {
            gX_F32 radius = Cluster_EffectiveRadius(light);
            if (light->castShadows && light->shadowRange > 0.0f)
                radius = fminf(radius, light->shadowRange);
            if (radius >= 0.05f) {
                gX_F32 x = light->pos.x, y = light->pos.y, z = light->pos.z;
                gX_F32 vx = view.m0*x + view.m4*y + view.m8*z + view.m12;
                gX_F32 vy = view.m1*x + view.m5*y + view.m9*z + view.m13;
                gX_F32 vz = view.m2*x + view.m6*y + view.m10*z + view.m14;
                gX_F32 vw = view.m3*x + view.m7*y + view.m11*z + view.m15;
                gX_F32 cw = proj.m3*vx + proj.m7*vy + proj.m11*vz + proj.m15*vw;
                if (cw > 0.001f && vz < -0.001f) {
                    gX_F32 ccx = proj.m0*vx + proj.m4*vy + proj.m8*vz + proj.m12*vw;
                    gX_F32 ccy = proj.m1*vx + proj.m5*vy + proj.m9*vz + proj.m13*vw;
                    gX_F32 cx = (ccx / cw * 0.5f + 0.5f) * (gX_F32)maindata.renderWidth;
                    gX_F32 cy = (ccy / cw * 0.5f + 0.5f) * (gX_F32)maindata.renderHeight;
                    gX_F32 depth = -vz;
                    gX_F32 sr = radius * projScaleY / (depth > 0.05f ? depth : 0.05f);
                    gX_I32 x0 = (gX_I32)floorf((cx - sr) / GX_CLUSTER_TILE);
                    gX_I32 x1 = (gX_I32)floorf((cx + sr) / GX_CLUSTER_TILE);
                    gX_I32 y0 = (gX_I32)floorf((cy - sr) / GX_CLUSTER_TILE);
                    gX_I32 y1 = (gX_I32)floorf((cy + sr) / GX_CLUSTER_TILE);
                    x0 = x0 < 0 ? 0 : x0; y0 = y0 < 0 ? 0 : y0;
                    x1 = x1 >= tilesX ? tilesX - 1 : x1;
                    y1 = y1 >= tilesY ? tilesY - 1 : y1;
                    const gX_U32 lo = (slot < 32u) ? (1u << slot) : 0u;
                    const gX_U32 hi = (slot >= 32u) ? (1u << (slot - 32u)) : 0u;
                    for (gX_I32 ty = y0; ty <= y1; ty++)
                        for (gX_I32 tx = x0; tx <= x1; tx++) {
                            grid[2 * (ty * tilesX + tx)] |= lo;
                            grid[2 * (ty * tilesX + tx) + 1] |= hi;
                        }
                }
            }
        }
    }
    return usedCount;
}

#ifdef GX_COMPUTE
//opencode: opt-in compute-vs-CPU grid proof. The readback is intentionally
//   absent from production frames; GX_T_CLUSTER_PARITY=1 enables one check.
static gX_I0 Cluster_CheckParity(gX_RendererGFX *gfx, gX_I32 tilesX,
    gX_I32 tilesY)
{
    static gX_I1 checked = 0;
    const gX_I8 *hook = getenv("GX_T_CLUSTER_PARITY");
    gX_U32 *gpuGrid;
    gX_U32 *cpuGrid;
    gX_U32 mismatchTiles = 0;
    gX_U32 mismatchWords = 0;
    if (checked || !hook || hook[0] == '0' || maindata.frameCount < 30) return;
    checked = 1;
    gpuGrid = (gX_U32*)calloc((size_t)GX_CLUSTER_GRID_MAX * 2, sizeof(gX_U32));
    cpuGrid = (gX_U32*)calloc((size_t)GX_CLUSTER_GRID_MAX * 2, sizeof(gX_U32));
    if (!gpuGrid || !cpuGrid) {
        free(gpuGrid);
        free(cpuGrid);
        gX_TraceLog_GFX(gX_LOG_ERROR, "CLUSTER_PARITY: allocation failed");
        return;
    }
    gX_RHI_Tex_Bind(gfx->clusters.gridTex, gX_RHI_TEXTURE_2D);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RG_INTEGER, GL_UNSIGNED_INT, gpuGrid);
    gX_RHI_Tex_Bind(0, gX_RHI_TEXTURE_2D);
    Cluster_BuildCpuGrid(gfx, cpuGrid, tilesX, tilesY);
    for (gX_I32 ty = 0; ty < tilesY; ty++) {
        for (gX_I32 tx = 0; tx < tilesX; tx++) {
            gX_U32 gpuIndex = 2u * ((gX_U32)ty * GX_CLUSTER_GRID_SIDE + (gX_U32)tx);
            gX_U32 cpuIndex = 2u * ((gX_U32)ty * (gX_U32)tilesX + (gX_U32)tx);
            gX_I1 tileMismatch = 0;
            for (gX_U32 word = 0; word < 2; word++) {
                if (gpuGrid[gpuIndex + word] != cpuGrid[cpuIndex + word]) {
                    mismatchWords++;
                    tileMismatch = 1;
                }
            }
            if (tileMismatch) mismatchTiles++;
        }
    }
    gX_TraceLog_GFX(gX_LOG_NOTICE,
        "CLUSTER_PARITY: tiles=%dx%d mismatchedTiles=%u mismatchedWords=%u",
        tilesX, tilesY, mismatchTiles, mismatchWords);
    free(gpuGrid);
    free(cpuGrid);
}
#endif

gX_I0 gX_RendererGFX_LightClusters_Build(gX_RendererGFX *gfx)
{
    if (!gfx->state.ENABLE_CLUSTERED) return;
    if (!gfx->clusters.inited) gX_RendererGFX_LightClusters_Init(gfx);

    GX_PROFILE_ZONE_BEGIN("gfx:light_clusters");

    const gX_I32 rw = maindata.renderWidth;
    const gX_I32 rh = maindata.renderHeight;
    const gX_I32 tilesX = (rw + GX_CLUSTER_TILE - 1) / GX_CLUSTER_TILE;
    const gX_I32 tilesY = (rh + GX_CLUSTER_TILE - 1) / GX_CLUSTER_TILE;

    memset(gfx->clusters.grid, 0, sizeof(gfx->clusters.grid));

    //opencode: GX_BENCH=1 — measure the grid-build segment (benchmark hook)
    static gX_F64 benchAccum = 0.0;
    static gX_U32 benchFrames = 0;
    gX_I1 bench = 0;
    {
        const gX_I8 *be = getenv("GX_BENCH");
        bench = (be && be[0] == '1');
    }
    struct timespec bt0, bt1;
    if (bench) clock_gettime(CLOCK_MONOTONIC, &bt0);

#ifdef GX_COMPUTE
    //opencode: v0.5.0 — compute culling path (GX_COMPUTE build + kernel loaded).
    //   Projection stays CPU-side (EXACT same math as the CPU grid path below —
    //   world->view->clip, screen disc radius from the attenuation quadratic
    //   root), heap-slot indexed into projLights[] (binding 1); the kernel
    //   assembles the per-tile bitmask grid in parallel (one thread per tile,
    //   image unit 1).
    //   Matrices are deliberately NOT passed to the kernel: the Cg-based GLSL
    //   translator on the test driver silently drops compute dispatches that
    //   multiply a mat4 uniform by an SSBO-derived vector (grid readback
    //   stayed uninitialized; constant-vector probes worked). GX_NO_COMPUTE=1
    //   forces the CPU path for A/B parity checks.
    const gX_I8 *noComputeEnv = getenv("GX_NO_COMPUTE");
    gX_I1 noCompute = (noComputeEnv && noComputeEnv[0] == '1');
    if (gfx->clusters.cullInited && !noCompute) {
        static gX_F32 projData[gX_GFX_MAX_LIGHTS_SSBO * 4];
        gX_U32 lightSpan = 0;
        memset(projData, 0, sizeof(projData));
        {
            gX_F32 aspect = (gX_F32)rw / (gX_F32)rh;
            gX_Matrix view = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
            gX_Matrix proj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect,
                camera.frustum.near, camera.frustum.far);
            const float projScaleY = (gX_F32)rh / (2.0f * tanf(camera.camera3D.fovy * gX_DEG2RAD * 0.5f));
            GX_HEAP_FOREACH(gfx->lights, gX_RendererGFX_Light, light) {
                gX_U32 slot = (gX_U32)(light - (gX_RendererGFX_Light*)gfx->lights.data);
                if (slot >= gX_GFX_MAX_LIGHTS_SSBO) continue;
                if (slot + 1 > lightSpan) lightSpan = slot + 1;
                if (!light->sun) {
                    float radius = Cluster_EffectiveRadius(light);
                    if (light->castShadows && light->shadowRange > 0.0f)
                        radius = fminf(radius, light->shadowRange);
                    if (radius >= 0.05f) {
                        float x = light->pos.x, y = light->pos.y, z = light->pos.z;
                        float vx = view.m0*x + view.m4*y + view.m8*z  + view.m12;
                        float vy = view.m1*x + view.m5*y + view.m9*z  + view.m13;
                        float vz = view.m2*x + view.m6*y + view.m10*z + view.m14;
                        float vw = view.m3*x + view.m7*y + view.m11*z + view.m15;
                        float cw = proj.m3*vx + proj.m7*vy + proj.m11*vz + proj.m15*vw;
                        if (cw > 0.001f && vz < -0.001f) {
                            float ccx = proj.m0*vx + proj.m4*vy + proj.m8*vz  + proj.m12*vw;
                            float ccy = proj.m1*vx + proj.m5*vy + proj.m9*vz  + proj.m13*vw;
                            projData[slot*4 + 0] = (ccx / cw * 0.5f + 0.5f) * (gX_F32)rw;
                            projData[slot*4 + 1] = (ccy / cw * 0.5f + 0.5f) * (gX_F32)rh;
                            float depth = -vz;
                            projData[slot*4 + 2] = radius * projScaleY / (depth > 0.05f ? depth : 0.05f);
                            projData[slot*4 + 3] = 0.0f;
                        }
                    }
                }
            }
        }
        if (!gfx->clusters.projSsbo) {
            gfx->clusters.projSsbo = rlLoadShaderBuffer(
                gX_GFX_MAX_LIGHTS_SSBO * 16, NULL, RL_DYNAMIC_COPY);
        }
        rlUpdateShaderBuffer(gfx->clusters.projSsbo, projData, lightSpan * 16, 0);

        gX_RHI_Shader_Use(gfx->clusters.cullProgram);
        gX_RHI_SSBO_Bind(gfx->clusters.projSsbo, 1);
        glBindImageTexture(1, gfx->clusters.gridTex, 0, GL_FALSE, 0,
            GL_READ_WRITE, GL_RG32UI);
        {
            gX_F32 tv[2] = { (gX_F32)tilesX, (gX_F32)tilesY };
            glUniform2fv(gX_RHI_GetUniformLocation(gfx->clusters.cullProgram, "tiles"), 1, tv);
        }
        gX_RHI_UniformInt(gX_RHI_GetUniformLocation(gfx->clusters.cullProgram, "lightCount"),
            (gX_I32)lightSpan);
        gX_RHI_DispatchCompute((gX_U32)tilesX, (gX_U32)tilesY, 1);
        gX_RHI_MemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT |
            GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
            GL_TEXTURE_UPDATE_BARRIER_BIT);

        gfx->clusters.tilesX = tilesX;
        gfx->clusters.tilesY = tilesY;
        GX_PROFILE_ZONE_END("gfx:light_clusters");
        if (bench) {
            clock_gettime(CLOCK_MONOTONIC, &bt1);
            benchAccum += (bt1.tv_sec - bt0.tv_sec) * 1e6 + (bt1.tv_nsec - bt0.tv_nsec) / 1e3;
            benchFrames++;
            if (benchFrames % 120 == 0)
                gX_TraceLog(gX_LOG_INFO, "BENCH compute grid build: %.1f us/frame (%u lights)",
                    benchAccum / (gX_F64)benchFrames, lightSpan);
        }
        Cluster_CheckParity(gfx, tilesX, tilesY);
        return;
    }
#endif

    //opencode: CPU-culled path (default 3.3 build / kernel unavailable).
    //   Per-tile RG32UI bitmask of light slots, ascending order == the
    //   uniform-array loop order (bit-parity with the non-clustered path).
    //opencode: the CPU fallback and parity reference share the exact same
    //   projection, heap-slot, and GLSL-floor semantics.
    gX_U32 usedCount = Cluster_BuildCpuGrid(gfx, gfx->clusters.grid, tilesX, tilesY);

    //opencode: upload grid (RG32UI pairs; UNPACK_ROW_LENGTH = texel rows)
    //opencode: route the cluster-grid upload binding through the target-aware RHI.
    gX_RHI_Tex_Bind(gfx->clusters.gridTex, gX_RHI_TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, tilesX);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tilesX, tilesY,
                    GL_RG_INTEGER, GL_UNSIGNED_INT, gfx->clusters.grid);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gX_RHI_Tex_Bind(0, gX_RHI_TEXTURE_2D);

    gfx->clusters.tilesX = tilesX;
    gfx->clusters.tilesY = tilesY;

    GX_PROFILE_ZONE_END("gfx:light_clusters");
    if (bench) {
        clock_gettime(CLOCK_MONOTONIC, &bt1);
        benchAccum += (bt1.tv_sec - bt0.tv_sec) * 1e6 + (bt1.tv_nsec - bt0.tv_nsec) / 1e3;
        benchFrames++;
        if (benchFrames % 120 == 0)
            gX_TraceLog(gX_LOG_INFO, "BENCH cpu grid build: %.1f us/frame (%u lights)",
                benchAccum / (gX_F64)benchFrames, usedCount);
    }
}

gX_I0 gX_RendererGFX_LightClusters_Init(gX_RendererGFX *gfx)
{
    if (gfx->clusters.inited) return;
    //opencode: register the cluster grid with the RHI so later sampler binds
    //   retain its 2D target instead of relying on an untracked GL name.
    gfx->clusters.gridTex = gX_RHI_Tex_Gen(gX_RHI_TEXTURE_2D, "gfx_cluster_grid");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32UI, GX_CLUSTER_GRID_SIDE, GX_CLUSTER_GRID_SIDE, 0,
                 GL_RG_INTEGER, GL_UNSIGNED_INT, NULL); //opencode: max grid 4096x16px tiles
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gX_RHI_Tex_Bind(0, gX_RHI_TEXTURE_2D);
    memset(gfx->clusters.grid, 0, sizeof(gfx->clusters.grid));
    gfx->clusters.tilesX = 0;
    gfx->clusters.tilesY = 0;
    gfx->clusters.inited = true;
    gX_TraceLog(gX_LOG_NOTICE, "CLUSTERS: Init ran (GX_COMPUTE=%d)",
#ifdef GX_COMPUTE
        1);
#else
        0);
#endif

#ifdef GX_COMPUTE
    //opencode: v0.5.0 — load the compute culling kernel (light_cull.comp)
    //   through the RHI compute compiler (raw glCreateShader(GL_COMPUTE_SHADER)).
    {
        gX_U64 srcLen = 0;
        gX_I8 *src = gX_Shader_Compiler_Preprocess(
            GX_RESOURCE_DIR "/shader/light_cull.comp", NULL, &srcLen);
        if (src) {
            gX_I8 err[1024] = {0};
            gX_U32 prog = gX_RHI_Shader_Compile_Compute(src, "light_cull", err, sizeof(err));
            if (prog > 0) {
                gfx->clusters.cullProgram = prog;
                gfx->clusters.cullInited = true;
                gX_TraceLog_GFX(gX_LOG_NOTICE,
                    "CLUSTERS: compute cull kernel loaded (id=%u)", prog);
            } else {
                gX_TraceLog_GFX(gX_LOG_WARNING,
                    "CLUSTERS: compute cull compile failed: %s", err);
            }
            free(src);
        } else {
            gX_TraceLog_GFX(gX_LOG_WARNING,
                "CLUSTERS: compute cull preprocess returned NULL (len=%llu)",
                (unsigned long long)srcLen);
        }
    }
#endif
}

//opencode: cluster resources are created independently of render textures, so
//   their teardown is explicit and idempotent. This permits a renderer reload
//   in one live GL context without retaining the grid, SSBOs, or cull program.
gX_I0 gX_RendererGFX_LightClusters_Unload(gX_RendererGFX *gfx)
{
#ifdef GX_COMPUTE
    if (gfx->clusters.lightSsbo > 0) {
        gX_RHI_SSBO_Bind(0, 0);
        gX_RHI_Buffer_Delete(gfx->clusters.lightSsbo);
    }
    if (gfx->clusters.projSsbo > 0) {
        gX_RHI_SSBO_Bind(0, 1);
        gX_RHI_Buffer_Delete(gfx->clusters.projSsbo);
    }
    if (gfx->clusters.cullProgram > 0) {
        gX_RHI_Shader_Use(0);
        gX_RHI_Shader_Delete(gfx->clusters.cullProgram);
    }
#endif
    if (gfx->clusters.gridTex > 0) {
        gX_RHI_Tex_Delete(gfx->clusters.gridTex);
    }
    memset(&gfx->clusters, 0, sizeof(gfx->clusters));
}
