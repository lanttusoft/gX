/**********************************************************************************************
*
*   [gX: src/renderer/gfx/bloom.h]
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


#ifndef RENDERER_GFX_BLOOM_H
#define RENDERER_GFX_BLOOM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "shader.h"
#include "texture.h"

//opencode: bloom / hdr post-process
typedef struct gX_RendererGFX_Bloom {
    gX_I1       enable;
    gX_F32      threshold;
    gX_F32      knee;
    gX_F32      intensity;
    gX_F32      blurAmount;
    gX_F32      exposure;
    gX_F32      vignette;
    gX_F32      saturation;
    gX_F32      contrast;

    //opencode: v0.3-M1 — auto exposure (Krawczyk key, CPU-adapted)
    gX_I1       autoExposure;
    gX_F32      exposureCompensation;
    gX_F32      exposureMin;
    gX_F32      exposureMax;
    gX_F32      adaptedExposure;
    gX_F32      luminanceAvg;
    gX_I32      luminanceFrame;         //opencode: readback cadence counter
    gX_RendererGFX_Shader luminanceShader;
    gX_RendererGFX_RenderTexture luminanceRT;
    gX_I32      luminanceInputLoc;

    gX_RendererGFX_Shader extractShader;
    gX_RendererGFX_Shader blurHShader;
    gX_RendererGFX_Shader blurVShader;
    gX_RendererGFX_Shader compositeShader;

    gX_RendererGFX_RenderTexture hdrScene;
    //opencode: Phase 1 — MSAA resolve target. When msaaLevel >= 2 the scene
    //   renders into hdrMSAA (multisampled) and MSAA_Resolve copies it to
    //   hdrScene for post-process. hdrMSAA owns its GL objects directly in
    //   wrapper.rt (id/texture.id/depth.id); loadingState.loaded doubles as
    //   the MSAA-live predicate (MSAA_Create-owned, never generic-loaded).
    //   No raw-handle fields, no handle swapping into hdrScene. Multisample
    //   textures take no sampler/filter params — keep hdrMSAA out of SYNC_RT.
    //   When msaaLevel < 2, hdrMSAA stays unloaded and the scene renders
    //   directly to hdrScene (non-MSAA, no resolve pass needed).
    gX_RendererGFX_RenderTexture hdrMSAA;
    gX_RendererGFX_RenderTexture extract;
    gX_RendererGFX_RenderTexture ping;
    gX_RendererGFX_RenderTexture pong;
    gX_RendererGFX_RenderTexture scene;

    gX_I32      extractInputLoc;
    gX_I32      extractThresholdLoc;
    gX_I32      extractKneeLoc;

    gX_I32      blurInputLoc;
    gX_I32      blurResLoc;
    gX_I32      blurAmountLoc;

    gX_I32      compositeSceneLoc;
    gX_I32  compositeSsgiTexLoc;    //opencode: v0.3.4-fix GI sampler unit
    gX_I32  compositeSsgiGainLoc;   //opencode: v0.3.4-fix GI gain uniform
    gX_I32      compositeBloomLoc;
    gX_I32      compositeIntensityLoc;
    gX_I32      compositeExposureLoc;
    gX_I32      compositeVignetteLoc;
    gX_I32      compositeSaturationLoc;
    gX_I32      compositeContrastLoc;
    gX_I32      compositeACESLoc;   //opencode: ACES tone mapping toggle
    gX_I32      compositeAgXLoc;    //opencode: AgX tone mapping toggle (v0.3-M1)
    gX_I32      compositeGammaLoc;  //opencode: gamma correction toggle
    gX_I32      compositeDitherModeLoc; //opencode: opt-in dither mode
    gX_I32      compositeDitherSeedLoc; //opencode: deterministic dither seed
    gX_I32      compositeDitherStrengthLoc; //opencode: dither LSB amplitude
    gX_I32      extractResLoc;      //opencode: E-4 — was a shadowed local (-1); queried in CacheLocs

    gX_I1       locsCached;
} gX_RendererGFX_Bloom;

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: bloom draw
gX_I0 gX_RendererGFX_Bloom_Draw(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_BLOOM_H
