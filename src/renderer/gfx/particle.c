/**********************************************************************************************
*
*   [gX: src/renderer/gfx/particle.c]
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


//opencode: GPU compute particles — SSBO-backed, compute-shader-updated,
//   point-sprite rendered via raw GL. Supports multiple emitters, each
//   owning a fixed range of particles in the SSBO.
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../gfx.h"
#include "particle.h"
#include "boilerplate.h"
#include "../renderer.h"

#include "../../main.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_state.h"
//opencode: hunt - implicit-decl sweep
#include "../../rhi/rhi_buf.h"
#include "../../rhi/gl_state.h"
#include "../../camera.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/profiler.h"
#include "../../common/path.h"


#include "../../gxmath/matrix.h"
#include "../../gxmath/gxmath.h"
#include "gl_level.h"

#define PARTICLE_FLOATS 16
#define PARTICLE_BYTES  (PARTICLE_FLOATS * sizeof(gX_F32))

//opencode: read shader source from file into heap-allocated buffer
static gX_I8 *gX_Particle_ReadFile(const gX_I8 *path, gX_I64 *outLen)
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

//opencode: emitter API — heap management
gX_I32 gX_RendererGFX_Emitter_Add(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Emitter *e = gX_Heap_Add_FirstEmpty_NoPtr(&gfx->particle.emitters);
    if (!e) return -1;
    e->active  = 1;
    e->emitRate = 10.0f;
    e->pos     = (gX_F32v3){0, 0, 0};
    e->baseVel = (gX_F32v3){0, 5, 0};
    e->spread  = 30.0f;
    e->color   = (gX_F32v4){1, 1, 1, 1};
    e->life    = 2.0f;
    e->speed   = 2.0f;
    return (gX_I32)((gX_U8*)e - (gX_U8*)gfx->particle.emitters.data)
           / (gX_I32)sizeof(gX_RendererGFX_Emitter);
}

gX_I0 gX_RendererGFX_Emitter_Remove(gX_RendererGFX *gfx, gX_U32 index)
{
    if (index >= gfx->particle.emitters.amount) return;
    gX_Heap_Remove_Index_NoPtr(&gfx->particle.emitters, index);
}

gX_I0 gX_RendererGFX_Emitter_RemoveAll(gX_RendererGFX *gfx)
{
    GX_HEAP_REMOVE_ALL(gfx->particle.emitters);
}

//opencode: particle system
gX_I0 gX_RendererGFX_Particle_Init(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Particle *p = &gfx->particle;
    p->maxCount  = 8192;
    p->emitAccum = 0.0f;
    p->ssbo = p->vao = p->computeProg = p->renderProg = p->velocityProg = 0;
    p->loaded = false;
    p->locsCached = false;

    //opencode: emitter heap — block-size = gX_RendererGFX_Emitter
    gX_Heap_Zero(&p->emitters);
    gX_Heap_SetBlockSize(&p->emitters, sizeof(gX_RendererGFX_Emitter));
    gX_Heap_SetMax(&p->emitters, gX_GFX_MAX_EMITTERS);
    gX_Heap_SetAmount(&p->emitters, 0);
    gX_Heap_Init(&p->emitters);
}

gX_I0 gX_RendererGFX_Particle_Unload(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Particle *p = &gfx->particle;
    if (p->ssbo > 0)        { gX_RHI_Buffer_Delete(p->ssbo); p->ssbo = 0; }
    if (p->vao > 0)         { gX_RHI_VAO_Delete(p->vao); p->vao = 0; }
    if (p->computeProg > 0) { gX_RHI_Shader_Delete(p->computeProg); p->computeProg = 0; }
    if (p->renderProg > 0)  { gX_RHI_Shader_Delete(p->renderProg); p->renderProg = 0; }
    if (p->velocityProg > 0) { gX_RHI_Shader_Delete(p->velocityProg); p->velocityProg = 0; }
    gX_Heap_DeInit(&p->emitters);
    p->loaded = false;
}

gX_I0 gX_RendererGFX_Particle_Update(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Particle *p = &gfx->particle;
    if (!gfx->state.ENABLE_PARTICLES) return;

    //opencode: deferred GL init
    if (!p->loaded && maindata.backendLoaded)
    {
        gX_U64 sz = (gX_U64)p->maxCount * PARTICLE_BYTES;
        p->ssbo = gX_RHI_Buffer_Create("particle_ssbo");
        gX_RHI_Buffer_Bind(gX_RHI_SHADER_STORAGE_BUFFER, p->ssbo);
        gX_RHI_Buffer_Data(gX_RHI_SHADER_STORAGE_BUFFER, (gX_U32)sz, NULL, gX_RHI_DYNAMIC_DRAW);
        gX_F32 *zero = (gX_F32*)calloc(1, (size_t)sz);
        gX_RHI_Buffer_SubData(gX_RHI_SHADER_STORAGE_BUFFER, 0, (gX_U32)sz, zero);
        free(zero);
        gX_RHI_Buffer_Bind(gX_RHI_SHADER_STORAGE_BUFFER, 0);

        p->vao = gX_RHI_VAO_Create("particle_vao");

        gX_I8 cpath[gX_GFX_PATH_BUFFER];
        p->computeProg = 0;
        if (gX_GL.hasCompute) {
            snprintf(cpath, sizeof(cpath), "%s", GX_PATH("shader", "gfx_particle_update.glsl"));
            gX_I8 *csrc = gX_Particle_ReadFile(cpath, NULL);
            if (csrc) {
                gX_I8 errorLog[1024] = {0};
                p->computeProg = gX_RHI_Shader_Compile_Compute(csrc, "particle_compute", errorLog, sizeof(errorLog));
                if (p->computeProg == 0)
                    gX_TraceLog_GFX(gX_LOG_ERROR, "Particle compute shader: %s", errorLog);
                free(csrc);
            }
        }

        gX_I8 vpath[gX_GFX_PATH_BUFFER], fpath[gX_GFX_PATH_BUFFER];
        gX_I8 velocityVpath[gX_GFX_PATH_BUFFER], velocityFpath[gX_GFX_PATH_BUFFER];
        snprintf(vpath, sizeof(vpath), "%s", GX_PATH("shader", "gfx_particle.vert"));
        snprintf(fpath, sizeof(fpath), "%s", GX_PATH("shader", "gfx_particle.frag"));
        snprintf(velocityVpath, sizeof(velocityVpath), "%s", GX_PATH("shader", "gfx_particle_velocity.vert"));
        snprintf(velocityFpath, sizeof(velocityFpath), "%s", GX_PATH("shader", "gfx_particle_velocity.frag"));
        //opencode: particle render/velocity shaders read SSBO binding 0 and
        //   use gl_VertexID/uint arithmetic — GL 4.3+ (or ES 3.1+) only by
        //   design. Compiling them below SSBO level can only produce version
        //   errors (430 has no legacy spelling), and the draw path already
        //   no-ops without programs, so skip silently like the compute load.
        if (gX_GL.hasSSBO) {
            gX_I8 *vsrc = gX_Particle_ReadFile(vpath, NULL);
            gX_I8 *fsrc = gX_Particle_ReadFile(fpath, NULL);
            gX_I8 *velocityVsrc = gX_Particle_ReadFile(velocityVpath, NULL);
            gX_I8 *velocityFsrc = gX_Particle_ReadFile(velocityFpath, NULL);
            if (vsrc && fsrc) {
                gX_I8 errorLog[1024] = {0};
                p->renderProg = gX_RHI_Shader_Compile(vsrc, NULL, fsrc, "particle_render", errorLog, sizeof(errorLog));
                if (p->renderProg == 0)
                    gX_TraceLog_GFX(gX_LOG_ERROR, "Particle render shader: %s", errorLog);
            }
            if (velocityVsrc && velocityFsrc) {
                gX_I8 errorLog[1024] = {0};
                p->velocityProg = gX_RHI_Shader_Compile(velocityVsrc, NULL, velocityFsrc,
                    "particle_velocity", errorLog, sizeof(errorLog));
                if (p->velocityProg == 0)
                    gX_TraceLog_GFX(gX_LOG_ERROR, "Particle velocity shader: %s", errorLog);
            }
            if (vsrc) free(vsrc);
            if (fsrc) free(fsrc);
            if (velocityVsrc) free(velocityVsrc);
            if (velocityFsrc) free(velocityFsrc);
        }

        if (p->computeProg > 0 && p->renderProg > 0) p->loaded = true;
        //opencode: v0.1.21 — GL 3.3 contexts have no compute/SSBO support:
        //   computeProg stays 0 and `loaded` never became true, so this whole
        //   deferred-init block re-ran EVERY FRAME — recreating the SSBO + VAO
        //   and recompiling both particle shaders per frame (program/buffer id
        //   explosion + per-frame compile cost). On non-compute contexts the
        //   GPU particle path is simply unavailable: mark loaded once (Draw
        //   then no-ops on the zero SSBO) and stop retrying.
        else if (!gX_GL.hasCompute) {
            p->loaded = true;
            gX_TraceLog_GFX(gX_LOG_WARNING, "Particles: GPU path unavailable (no compute) — disabled");
        }
        gX_TraceLog_GFX(gX_LOG_NOTICE, "Particles: compute=%u render=%u velocity=%u max=%d emitters=%u",
            p->computeProg, p->renderProg, p->velocityProg, p->maxCount, p->emitters.amount);
    }

    if (!p->loaded) return;

    //opencode: cache uniform locations once
    if (!p->locsCached) {
        if (p->computeProg > 0) {
            p->uDt         = gX_RHI_GetUniformLocation(p->computeProg, "dt");
            p->uEmitPos    = gX_RHI_GetUniformLocation(p->computeProg, "emitPos");
            p->uEmitBaseVel = gX_RHI_GetUniformLocation(p->computeProg, "emitBaseVel");
            p->uEmitSpread = gX_RHI_GetUniformLocation(p->computeProg, "emitSpread");
            p->uEmitColor  = gX_RHI_GetUniformLocation(p->computeProg, "emitColor");
            p->uEmitLife   = gX_RHI_GetUniformLocation(p->computeProg, "emitLife");
            p->uEmitSpeed  = gX_RHI_GetUniformLocation(p->computeProg, "emitSpeed");
            p->uMaxCount   = gX_RHI_GetUniformLocation(p->computeProg, "maxCount");
            p->uEmitOffset = gX_RHI_GetUniformLocation(p->computeProg, "emitOffset");
            p->uEmitCount  = gX_RHI_GetUniformLocation(p->computeProg, "emitCount");
            p->uEmitRate   = gX_RHI_GetUniformLocation(p->computeProg, "emitRate");
            p->uTime       = gX_RHI_GetUniformLocation(p->computeProg, "time");
        }
        if (p->renderProg > 0) {
            p->uVP        = gX_RHI_GetUniformLocation(p->renderProg, "vp");
            p->uCamPos    = gX_RHI_GetUniformLocation(p->renderProg, "cameraPos");
            p->uPointSize = gX_RHI_GetUniformLocation(p->renderProg, "pointSize");
        }
        if (p->velocityProg > 0) {
            p->uVelocityCurrVP = gX_RHI_GetUniformLocation(p->velocityProg, "matCurrVP");
            p->uVelocityPrevVP = gX_RHI_GetUniformLocation(p->velocityProg, "matPrevVP");
            p->uVelocityGlass = gX_RHI_GetUniformLocation(p->velocityProg, "uGlass");
            p->uVelocityPointSize = gX_RHI_GetUniformLocation(p->velocityProg, "pointSize");
        }
        p->locsCached = true;
    }

    //opencode: dispatch compute for each active emitter
    if (p->computeProg > 0 && p->emitters.amount > 0)
    {
        gX_RHI_Shader_Use(p->computeProg);
        gX_RHI_SSBO_Bind(p->ssbo, 0);

        gX_F32 ft = (gX_F32)gX_FrameTime_Get();
        //opencode: clamp dt — a stall would teleport particles (was unclamped)
        if (ft > 0.1f) ft = 0.1f;
        gX_F32 time = (gX_F32)maindata.timeElapsed;

        GX_HEAP_FOREACH(p->emitters, gX_RendererGFX_Emitter, e) {
            //opencode: inactive emitters still dispatch the update so alive
            //   particles age/fade out (emitRate=0 -> no respawn). Previously
            //   they were skipped entirely, freezing particles mid-air.
            gX_F32 emitRate = e->active ? e->emitRate : 0.0f;

            //opencode: compute particle range for this emitter
            gX_U32 ei = (gX_U32)(e - (gX_RendererGFX_Emitter*)p->emitters.data);
            gX_I32 offset = (gX_I32)ei * gX_GFX_PARTICLES_PER_EMITTER;
            gX_I32 count  = gX_GFX_PARTICLES_PER_EMITTER;
            if (offset + count > p->maxCount) count = p->maxCount - offset;
            if (count <= 0) continue;

            gX_RHI_Uniform_Float(p->uDt, ft);
            gX_RHI_Uniform_Vec3(p->uEmitPos, e->pos.x, e->pos.y, e->pos.z);
            gX_RHI_Uniform_Vec3(p->uEmitBaseVel, e->baseVel.x, e->baseVel.y, e->baseVel.z);
            gX_RHI_Uniform_Float(p->uEmitSpread, e->spread * gX_DEG2RAD);
            gX_RHI_UniformVec4(p->uEmitColor, 1, &e->color.x);
            gX_RHI_Uniform_Float(p->uEmitLife, e->life);
            gX_RHI_Uniform_Float(p->uEmitSpeed, e->speed);
            gX_RHI_UniformInt(p->uMaxCount, p->maxCount);
            gX_RHI_UniformInt(p->uEmitOffset, offset);
            gX_RHI_UniformInt(p->uEmitCount, count);
            gX_RHI_Uniform_Float(p->uEmitRate, emitRate);
            gX_RHI_Uniform_Float(p->uTime, time);

            if (gX_GL.hasCompute && p->computeProg) {
                //ai: the update shader indexes gl_GlobalInvocationID.x over
                //   [emitOffset, emitOffset+count); glDispatchCompute has no
                //   base offset, so the dispatch must cover the whole range.
                //   emitter 0 worked and every later emitter saw only
                //   idx < emitOffset invocations — dormant forever.
                gX_U32 groups = ((gX_U32)(offset + count) + 63) / 64;
                gX_RHI_DispatchCompute(groups, 1, 1);
            }
        }

        if (gX_GL.hasCompute && p->computeProg) {
            gX_RHI_MemoryBarrier(gX_RHI_SHADER_STORAGE_BARRIER_BIT | gX_RHI_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
        }
        gX_RHI_Shader_Use(0);
    }
}

gX_I0 gX_RendererGFX_Particle_Draw(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Particle *p = &gfx->particle;
    if (!gfx->state.ENABLE_PARTICLES || !p->loaded) return;
    if (p->renderProg == 0 || p->ssbo == 0 || p->vao == 0) return;
    //opencode: no emitters → no particles to draw. The SSBO is zero-initialised
    //   (all particles dead) so this was visually a no-op before, but the
    //   pass also flips blend/depth state; gate it to keep the pass honest.
    if (p->emitters.amount == 0) return;

    GX_PROFILE_ZONE_BEGIN("gfx:particles");

    gX_Matrix view = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
    gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix proj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, aspect,
        camera.frustum.near, camera.frustum.far);
    gX_Matrix vp = gX_Matrix_Multiply(view, proj);

    gX_GL_State_Push();

    gX_RHI_Enable(gX_RHI_BLEND);
    gX_RHI_Blend_Func_Separate(gX_RHI_SRC_ALPHA, gX_RHI_ONE, gX_RHI_SRC_ALPHA, gX_RHI_ONE);
    gX_RHI_Depth_Write(0);
    gX_RHI_Enable(gX_RHI_DEPTH_TEST);
    gX_RHI_Depth_Func(gX_RHI_LEQUAL);

    gX_RHI_Shader_Use(p->renderProg);
    gX_RHI_SSBO_Bind(p->ssbo, 0);
    gX_RHI_UniformMatrix(p->uVP, 1, 1, &vp.m0);
    gX_RHI_Uniform_Vec3(p->uCamPos, camera.camera3D.position.x,
                camera.camera3D.position.y, camera.camera3D.position.z);
    gX_F32 ps = 4.0f + (gX_F32)maindata.renderHeight * 0.008f;
    gX_RHI_Uniform_Float(p->uPointSize, ps);
    gX_RHI_VAO_Bind(p->vao);
    gX_RHI_DrawArrays(gX_RHI_POINTS, 0, p->maxCount);
    gX_RHI_VAO_Bind(0);
    gX_RHI_Shader_Use(0);

    gX_GL_State_Pop();

    GX_PROFILE_ZONE_END("gfx:particles");
}

//opencode: particle velocity pass — paint current/previous SSBO positions
//   into the shared velocity target with the same point footprint as beauty.
gX_I0 gX_RendererGFX_Particle_DrawVelocity(gX_RendererGFX *gfx,
    const gX_Matrix *currVP, const gX_Matrix *prevVP, gX_I1 hasPrevVP)
{
    gX_RendererGFX_Particle *p = &gfx->particle;
    if (!gfx->state.ENABLE_PARTICLES || !p->loaded || p->velocityProg == 0
        || p->ssbo == 0 || p->vao == 0 || p->emitters.amount == 0) return;
    if (!currVP || !prevVP) return;

    gX_GL_State_Push();
    gX_RHI_Disable(gX_RHI_BLEND);
    gX_RHI_Disable(gX_RHI_DEPTH_TEST);
    gX_RHI_Shader_Use(p->velocityProg);
    gX_RHI_SSBO_Bind(p->ssbo, 0);
    if (p->uVelocityCurrVP >= 0)
        gX_RHI_UniformMatrix(p->uVelocityCurrVP, 1, 1, &currVP->m0);
    if (p->uVelocityPrevVP >= 0)
        gX_RHI_UniformMatrix(p->uVelocityPrevVP, 1, 1,
            hasPrevVP ? &prevVP->m0 : &currVP->m0);
    if (p->uVelocityGlass >= 0)
        gX_RHI_Uniform_Float(p->uVelocityGlass, 1.0f);
    if (p->uVelocityPointSize >= 0)
        gX_RHI_Uniform_Float(p->uVelocityPointSize,
            4.0f + (gX_F32)maindata.renderHeight * 0.008f);
    gX_RHI_VAO_Bind(p->vao);
    gX_RHI_DrawArrays(gX_RHI_POINTS, 0, p->maxCount);
    gX_RHI_VAO_Bind(0);
    gX_RHI_Shader_Use(0);
    gX_GL_State_Pop();
    return;
}
