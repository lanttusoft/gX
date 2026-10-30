/**********************************************************************************************
*
*   [gX: src/renderer/gfx/lensflare.h]
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


#ifndef RENDERER_GFX_LENSFLARE_H
#define RENDERER_GFX_LENSFLARE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "shader.h"
#include "texture.h"

//ai: lens flares — bounded screen-space ghosts, halo and horizontal streak.
//   Single-pass additive shader: source brightness drives fixed-axis ghosts;
//   source scene depth occludes all elements, distances account for target aspect.
typedef struct gX_RendererGFX_LensFlare {
    gX_I1       enable;       //opencode: master toggle
    gX_F32      intensity;    //opencode: overall brightness (0.0-2.0, default 1.0)
    gX_F32      dispersal;    //opencode: element spread (0.0-2.0, default 1.0)

    gX_RendererGFX_Shader shader;

    gX_I32  sceneColorLoc;
    gX_I32  sceneDepthLoc;
    gX_I32  sunScreenPosLoc;
    gX_I32  intensityLoc;
    gX_I32  dispersalLoc;
    gX_I32  resolutionLoc;

    gX_I1   locsCached;
} gX_RendererGFX_LensFlare;

typedef struct gX_RendererGFX gX_RendererGFX;

gX_I0 gX_RendererGFX_LensFlare_Draw(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_LensFlare_CacheLocs(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_LENSFLARE_H
