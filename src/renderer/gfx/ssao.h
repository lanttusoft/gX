/**********************************************************************************************
*
*   [gX: src/renderer/gfx/ssao.h]
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


#ifndef RENDERER_GFX_SSAO_H
#define RENDERER_GFX_SSAO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "shader.h"
#include "texture.h"
#include "temporal_history.h"

typedef struct gX_RendererGFX_SSAO {
    //opencode: render targets
    gX_RendererGFX_RenderTexture map;           // raw SSAO output
    gX_RendererGFX_RenderTexture blurMap;       // edge-aware blurred result

    //opencode: shaders (Step 3 — legacy kernel SSAO shader deleted, GTAO
    //   is the sole generator; neutral-white clear is the fallback)
    gX_RendererGFX_Shader blurShader;           // edge-aware blur

    //opencode: SSAO blur shader uniform locations
    gX_I32  blurMapLoc;
    gX_I32  blurDepthMapLoc;
    gX_I32  blurResolutionLoc;
    gX_I32  blurEdgeThresholdLoc;

    //opencode: state
    //opencode: v0.3-M2 — GTAO mode (horizon-scan, replaces kernel SSAO when on)
    gX_I1   useGTAO;
    gX_F32  thickness;
    gX_RendererGFX_Shader gtaoShader;
    gX_I32  gtaoDepthMapLoc;
    gX_I32  gtaoInvProjLoc;
    gX_I32  gtaoProjLoc;
    gX_I32  gtaoResolutionLoc;
    gX_I32  gtaoRadiusLoc;
    //ai: the existing authored SSAO bias now participates in GTAO sampling.
    gX_I32  gtaoBiasLoc;
    gX_I32  gtaoThicknessLoc;
    gX_I32  gtaoStrengthLoc;
    //opencode: oracle (Paper 1) - per-frame tap rotation (temporal rays)
    gX_I32  gtaoSeedLoc;
    gX_I32  gtaoSeedActiveLoc;
    gX_I1   gtaoLocsCached;
    //opencode: GTAO temporal accumulation (SSGI-Phase2 mirror): ping-pong
    //   history pair so the oracle rotation can run every frame (default
    //   ON) and converge instead of flickering. Consumed via gtaoFinalTex
    //   (falls back to blurMap when the pair is unavailable).
    gX_RendererGFX_Shader gtaoAccumShader;
    gX_RendererGFX_RenderTexture gtaoAccumA;
    gX_RendererGFX_RenderTexture gtaoAccumB;
    gX_RendererGFX_TemporalHistory gtaoTemporal;
    gX_I32  gtaoAccCurLoc;
    gX_I32  gtaoAccHistLoc;
    //ai: scene-depth input also seeds the depth channel of accumulated history.
    gX_I32  gtaoAccDepthMapLoc;
    //ai: lifecycle guard prevents sampling the cleared ping-pong pair before its first completed frame.
    gX_I32  gtaoAccHistoryValidLoc;
    gX_I32  gtaoAccVelLoc;
    gX_I32  gtaoAccHasVelLoc;
    gX_I32  gtaoAccCameraMovedLoc;
    gX_I32  gtaoAccResLoc;
    gX_Texture gtaoFinalTex;

    gX_I1   locsCached;

    //opencode: tuning
    gX_F32  radius;
    gX_F32  bias;
    gX_F32  strength;
    gX_I32  kernelSize;

    //opencode: v0.3-M4 — SSGI gather (experimental)
    gX_I1   useSSGI;
    gX_F32  ssgiIntensity;
    gX_RendererGFX_Shader ssgiShader;
    gX_RendererGFX_RenderTexture ssgiMap;
    gX_I32  ssgiSceneColorLoc;
    gX_I32  ssgiDepthLoc;
    gX_I32  ssgiInvProjLoc;
    gX_I32  ssgiProjLoc;
    gX_I32  ssgiResLoc;
    gX_I32  ssgiRadiusLoc;
    //ai: share the existing authored normal-separation bias with the SSGI gather.
    gX_I32  ssgiBiasLoc;
    gX_I32  ssgiThicknessLoc;
    gX_I32  ssgiIntensityLoc;
    gX_I32  ssgiPrevLoc;
    gX_I32  ssgiBounceLoc;
    //opencode: oracle (Paper 1) - per-frame tap rotation (temporal rays)
    gX_I32  ssgiSeedLoc;
    gX_I32  ssgiSeedActiveLoc;
    gX_F32  ssgiBounceGain;
    //opencode: v0.3-M4 Phase2 — denoise chain
    gX_RendererGFX_Shader ssgiBlurShader;
    gX_RendererGFX_Shader ssgiAccumShader;
    gX_RendererGFX_RenderTexture ssgiBlurMap;
    gX_RendererGFX_RenderTexture ssgiAccumA;
    gX_RendererGFX_RenderTexture ssgiAccumB;
    gX_RendererGFX_TemporalHistory ssgiTemporal;
    gX_I32  ssgiBlurMapLoc;
    gX_I32  ssgiBlurDepthLoc;
    gX_I32  ssgiBlurResLoc;
    gX_I32  ssgiBlurNearLoc;
    gX_I32  ssgiBlurFarLoc;
    gX_I32  ssgiAccCurLoc;
    gX_I32  ssgiAccHistLoc;
    //ai: lifecycle guard prevents sampling the cleared ping-pong pair before its first completed frame.
    gX_I32  ssgiAccHistoryValidLoc;
    gX_I32  ssgiAccVelLoc;
    gX_I32  ssgiAccHasVelLoc;
    gX_I32  ssgiAccResLoc;
    gX_Texture ssgiFinalTex; //opencode: latest accumulated output (parity-swapped)

    //ai: per-instance effect modes detect enable/algorithm transitions without global state.
    gX_I1   temporalModesInitialized;
    gX_I1   gtaoWasActive;
    gX_I1   ssgiWasActive;

} gX_RendererGFX_SSAO;

typedef struct gX_RendererGFX gX_RendererGFX;

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_SSAO_H
