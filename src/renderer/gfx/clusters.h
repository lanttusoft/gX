/**********************************************************************************************
*
*   [gX: src/renderer/gfx/clusters.h]
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
#ifndef GX_GFX_CLUSTERS_H
#define GX_GFX_CLUSTERS_H

//opencode: v0.3.2 — CPU-culled tile light clusters (Forward+, GL330-safe)

#include "../gfx.h"

//opencode: grid bitmask storage — 256x256 tiles max (4096px wide at 16px tiles)
#define GX_CLUSTER_GRID_SIDE 256
#define GX_CLUSTER_GRID_MAX  (GX_CLUSTER_GRID_SIDE * GX_CLUSTER_GRID_SIDE)

typedef struct gX_RendererGFX_LightClusters {
    gX_I1   inited;
    gX_U32  gridTex;
    gX_I32  tilesX;
    gX_I32  tilesY;
    //opencode: v0.5.0 — 64-bit tile mask (RG32UI lo/hi words): bit i = light
    //   slot i covers this tile; slots 0-31 in grid[2*idx], 32-63 in
    //   grid[2*idx+1]. The 32-bit R32UI cap made the >32-light SSBO lift a
    //   no-op (frag could only enumerate 32 slots/tile).
    unsigned grid[GX_CLUSTER_GRID_MAX * 2];
#ifdef GX_COMPUTE
    //opencode: v0.5.0 — SSBO holding the per-light data block (7x vec4 per
    //   light, std430). Bound at SSBO binding 0; the frag shader reads
    //   lightsData[i*7 + k] instead of the uniform arrays.
    gX_U32  lightSsbo;
    gX_U32  lightSsboCapacity;
    //opencode: v0.5.0 — compute culling kernel (light_cull.comp). One thread
    //   per tile replicates the CPU math; the RG32UI grid texture is bound as
    //   image unit 1 for imageStore and stays sampler-bound at unit 29.
    gX_U32  cullProgram;
    gX_U32  projSsbo;        // projected lights (vec4 cx,cy,sr) for the kernel
    gX_I1   cullInited;
#endif
} gX_RendererGFX_LightClusters;

gX_I0 gX_RendererGFX_LightClusters_Init(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_LightClusters_Build(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_LightClusters_Unload(gX_RendererGFX *gfx);

#endif //GX_GFX_CLUSTERS_H
