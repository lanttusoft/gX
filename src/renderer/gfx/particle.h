/**********************************************************************************************
*
*   [gX: src/renderer/gfx/particle.h]
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


#ifndef RENDERER_GFX_PARTICLE_H
#define RENDERER_GFX_PARTICLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "../../common/heap.h"
#include "../../gxmath/matrix.h"

//opencode: particle emitter — per-emitter configuration.
//   Each emitter owns a fixed range of particles (PARTICLES_PER_EMITTER).
//   The compute shader updates one range per dispatch with the emitter's
//   parameters. Multiple emitters can be active simultaneously.
#define gX_GFX_PARTICLES_PER_EMITTER 512
#define gX_GFX_MAX_EMITTERS          16

typedef struct gX_RendererGFX_Emitter {
    gX_I1       active;       //opencode: 1 = emitting, 0 = dormant (particles fade out)
    gX_F32      emitRate;     //opencode: particles spawned per second
    gX_F32v3    pos;          //opencode: spawn position (world space)
    gX_F32v3    baseVel;      //opencode: base velocity direction
    gX_F32      spread;       //opencode: velocity spread angle (degrees, 0-180)
    gX_F32v4    color;        //opencode: spawn color (RGBA)
    gX_F32      life;         //opencode: particle lifetime (seconds)
    gX_F32      speed;        //opencode: particle speed multiplier
} gX_RendererGFX_Emitter;

//opencode: GPU particle system — raw GL compute + render.
//   16 floats per particle. Compute shader updates physics, render shader
//   draws particles as point sprites. Both managed via raw GL.
typedef struct gX_RendererGFX_Particle {
    gX_I32      maxCount;     //opencode: total particles (default 8192)
    gX_F32      emitAccum;    //opencode: fractional accumulator

    //opencode: emitter heap — dynamic array of gX_RendererGFX_Emitter
    gX_Heap     emitters;

    //opencode: raw GL resources
    gX_U32      ssbo;         //opencode: shader storage buffer
    gX_U32      vao;          //opencode: vertex array (for drawing)
    gX_U32      computeProg;  //opencode: compute shader program
    gX_U32      renderProg;   //opencode: render shader program (vert+frag)
    gX_U32      velocityProg; //opencode: velocity shader program (vert+frag)
    gX_I1       loaded;
    gX_I1       locsCached;

    //opencode: uniform locations (compute)
    gX_I32 uDt, uEmitPos, uEmitBaseVel, uEmitSpread, uEmitColor, uEmitLife, uEmitSpeed, uTime;
    gX_I32 uMaxCount, uEmitOffset, uEmitCount, uEmitRate;
    //opencode: uniform locations (render)
    gX_I32 uVP, uCamPos, uPointSize;
    //opencode: uniform locations (velocity)
    gX_I32 uVelocityCurrVP, uVelocityPrevVP, uVelocityGlass, uVelocityPointSize;
} gX_RendererGFX_Particle;

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: emitter API
gX_I32 gX_RendererGFX_Emitter_Add(   gX_RendererGFX *gfx);
gX_I0  gX_RendererGFX_Emitter_Remove(gX_RendererGFX *gfx, gX_U32 index);
gX_I0  gX_RendererGFX_Emitter_RemoveAll(gX_RendererGFX *gfx);

//opencode: particle system lifecycle
gX_I0 gX_RendererGFX_Particle_Init(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Particle_Unload(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Particle_Update(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Particle_Draw(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Particle_DrawVelocity(gX_RendererGFX *gfx,
    const gX_Matrix *currVP, const gX_Matrix *prevVP, gX_I1 hasPrevVP);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_PARTICLE_H
