/**********************************************************************************************
*
*   [gX: src/renderer/gfx/reflection.h]
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



#ifndef RENDERER_GFX_REFLECTION_H
#define RENDERER_GFX_REFLECTION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "../../gxmath/matrix.h"
#include "texture.h"

//opencode: probe grid MVP-1 — static baked cubemap probes for
//   parallax-correct direct reflections. The single mirror-camera capture
//   is tuned for the floor; off-plane surfaces eat parallax error. Probes
//   reuse the capture path/units: per-model nearest-probe rebind needs
//   zero new texture units (same MAP_CUBEMAP slot), and the existing box
//   projection consumes the probe position via cubePos unmodified.
#define GFX_PROBE_MAX 4
typedef struct gX_RendererGFX_Probe {
    gX_F32v3    pos;
    gX_Texture  cube;
    gX_U32      fboID;
    gX_U32      depthRbo;
    gX_I1       baked;
    //ai: capture-level classification; mixed captures still need per-sample coverage.
    gX_I1       skyOnly;
} gX_RendererGFX_Probe;

typedef struct gX_RendererGFX_Reflection {
    gX_RendererGFX_RenderTexture planar;
    gX_Matrix  mvp;
    gX_Texture  dynamicCubemap;
    //ai: world-space origin owned by the dynamic capture, shared by every consumer.
    gX_F32v3    dynamicCapturePosition;
    //ai: no finite model submissions in the last dynamic capture.
    gX_I1       dynamicSkyOnly;
    gX_U32      cubeFboID;
    gX_U32      cubeDepthRbo;
    gX_RendererGFX_Probe probes[GFX_PROBE_MAX];
    gX_U32      probeCount;
    gX_U32      probeSize;
    gX_U32      nextProbe;
    gX_I1       probesReady;
    gX_Matrix   cubeViews[6];
    gX_U32      cubeSize;
    gX_I1       cubeReady;
    //opencode: v0.1.25 — true only for frames where the 6-face render actually
    //   ran. cubeReady alone stays true after one-time GL init even when no
    //   reflective model exists, so the SSR env fallback then samples stale
    //   (never-written) faces instead of the moon-bearing equirect sky LUT —
    //   celestial features vanished from reflections.
    //   Audit: now WRITTEN (reset at Draw_Reflections top, set after
    //   RenderEx) and read by the SSR env gate, the IBL fallback bind, and
    //   the Reflect lab status line. filledThisFrame got the same reset
    //   treatment (was gated inside ENABLE_REFLECTIONS).
    gX_I1       cubeUpdatedThisFrame;
    gX_F32      bias;
    gX_I1       filledThisFrame;    //opencode: v0.3.4 auxview - planar RT contains fresh mirror content
    gX_F32      planeDist;          //opencode: v0.3.6 - mirror plane (dot(n,x)=d)
    gX_F32v3    planeNormal;        //opencode: v0.3.6
    gX_U32      planarFbo;          //opencode: planar terrain - dedicated
                                    //   FBO id (draw.c static mirrored here
                                    //   for stats readback after EndTexture)
} gX_RendererGFX_Reflection;

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_REFLECTION_H
