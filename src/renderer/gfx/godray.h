/**********************************************************************************************
*
*   [gX: src/renderer/gfx/godray.h]
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


#ifndef RENDERER_GFX_GODRAY_H
#define RENDERER_GFX_GODRAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "shader.h"
#include "texture.h"

//opencode: god rays (crepuscular rays) — screen-space volumetric light scattering.
//   Single-pass: generate via gen shader, composite via gX_Texture_Draw with GL_ONE,GL_ONE.
typedef struct gX_RendererGFX_Godray {
    gX_I1       enable;
    gX_I32      density;
    gX_F32      decay;
    gX_F32      weight;
    gX_F32      exposure;

    gX_RendererGFX_Shader genShader;

    gX_RendererGFX_RenderTexture genMap;

    gX_I32  genSceneColorLoc;
    gX_I32  genSceneDepthLoc;
    gX_I32  genSunScreenPosLoc;
    gX_I32  genDensityLoc;
    gX_I32  genDecayLoc;
    gX_I32  genWeightLoc;
    gX_I32  genExposureLoc;

    gX_I1   locsCached;
} gX_RendererGFX_Godray;

typedef struct gX_RendererGFX gX_RendererGFX;

gX_I0 gX_RendererGFX_Godray_Draw(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Godray_CacheLocs(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_GODRAY_H
