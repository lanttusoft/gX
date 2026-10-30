/**********************************************************************************************
*
*   [gX: src/renderer/gfx/oracle_gpu.h]
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


#ifndef RENDERER_GFX_ORACLE_GPU_H
#define RENDERER_GFX_ORACLE_GPU_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: editor preview modes; raster remains the default and GL33 path.
#define GX_GFX_RAY_PREVIEW_RASTER 0
#define GX_GFX_RAY_PREVIEW_RAY 1
#define GX_GFX_RAY_PREVIEW_SIDE_BY_SIDE 2

//opencode: Paper 1 M5-M8 — opt-in GL4.3 compute transport oracle resources.
typedef struct gX_RendererGFX_OracleGPU {
    gX_I1   initialized;
    gX_I1   unavailableLogged;
    gX_I1   sceneReady;
    gX_U32  program;
    gX_U32  triangleSsbo;
    gX_U32  bvhSsbo;
    gX_U32  bvhIndexSsbo;
    gX_U32  previewAccumTexture;
    gX_U32  previewTexture;
    gX_U32  previewWidth;
    gX_U32  previewHeight;
    gX_U32  previewSamples;
    gX_I32  previewMode;
    //opencode: cached GL4.3 array for per-mesh base-color texture sampling.
    gX_U32  albedoTextureArray;
    gX_U64  sceneSignature;
    gX_U64  sampleSignature;
    gX_RenderTexture previewDisplayTarget;
    gX_U32  previewDisplayWidth;
    gX_U32  previewDisplayHeight;
    gX_RenderTexture comparisonRasterTarget;
    gX_U32  comparisonRasterWidth;
    gX_U32  comparisonRasterHeight;
    gX_U32  triangleCount;
    gX_U32  triangleCapacity;
    gX_U32  bvhNodeCount;
    gX_U32  bvhNodeCapacity;
    gX_U32  bvhIndexCount;
    gX_U32  bvhIndexCapacity;
    gX_I32  resolutionLoc;
    gX_I32  cameraPositionLoc;
    gX_I32  cameraForwardLoc;
    gX_I32  cameraRightLoc;
    gX_I32  cameraUpLoc;
    gX_I32  tanHalfFovLoc;
    gX_I32  cameraProjectionLoc;
    gX_I32  orthoHalfHeightLoc;
    gX_I32  aspectLoc;
    gX_I32  pointCountLoc;
    gX_I32  pointPositionsLoc;
    gX_I32  pointRadianceLoc;
    gX_I32  pointAttenLoc;
    gX_I32  sunDirectionLoc;
    gX_I32  sunRadianceLoc;
    gX_I32  sunCastShadowsLoc;
    gX_I32  environmentColorLoc;
    gX_I32  exposureLoc;
    gX_I32  bounceCountLoc;
    gX_I32  bvhNodeCountLoc;
    gX_I32  reflectionModeLoc;
    gX_I32  sampleIndexLoc;
    gX_I32  previewModeLoc;
    //opencode: compute sampler for the cached base-color texture array.
    gX_I32  albedoTextureArrayLoc;
    gX_I32  displaySceneLoc;
    gX_I32  displayBloomLoc;
    gX_I32  displayIntensityLoc;
    gX_I32  displayExposureLoc;
    gX_I32  displayVignetteLoc;
    gX_I32  displaySaturationLoc;
    gX_I32  displayContrastLoc;
    gX_I32  displayACESLoc;
    gX_I32  displayAgXLoc;
    gX_I32  displayGammaLoc;
    gX_I32  displayDitherModeLoc;
    gX_I32  displayDitherSeedLoc;
    gX_I32  displayDitherStrengthLoc;
} gX_RendererGFX_OracleGPU;

gX_I0 gX_RendererGFX_OracleGPU_Init(gX_RendererGFX *gfx);
gX_I1 gX_RendererGFX_OracleGPU_Available(gX_I0);
gX_I1 gX_RendererGFX_OracleGPU_Enabled(gX_I0);
gX_I0 gX_RendererGFX_OracleGPU_Draw(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_OracleGPU_Present(gX_RendererGFX *gfx,
    gX_RenderTexture *outputRT);
gX_I0 gX_RendererGFX_OracleGPU_Unload(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif // RENDERER_GFX_ORACLE_GPU_H
