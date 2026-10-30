/**********************************************************************************************
*
*   [gX: src/renderer/gfx/dof.h]
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


#ifndef RENDERER_GFX_DOF_H
#define RENDERER_GFX_DOF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "shader.h"
#include "texture.h"

//opencode: depth of field — circle of confusion + separable blur
typedef struct gX_RendererGFX_DOF {
    gX_I1   enable;
    gX_F32  focalDistance;
    gX_F32  focalRange;
    gX_F32  maxRadius;

    gX_RendererGFX_Shader cocShader;
    gX_RendererGFX_Shader blurShader;
    gX_RendererGFX_Shader compositeShader;

    gX_RendererGFX_RenderTexture cocMap;
    gX_RendererGFX_RenderTexture blurMap;
    gX_RendererGFX_RenderTexture blurPing;
    gX_RendererGFX_RenderTexture compositeMap; //opencode: full-res reduced-DOF output

    gX_I32  cocDepthMapLoc;
    gX_I32  cocResLoc;
    gX_I32  cocFocalDistLoc;
    gX_I32  cocFocalRangeLoc;
    gX_I32  cocNearLoc;
    gX_I32  cocFarLoc;

    gX_I32  blurColorMapLoc;
    gX_I32  blurCocMapLoc;
    gX_I32  blurDirLoc;
    gX_I32  blurResLoc;
    gX_I32  blurMaxRadiusLoc;

    gX_I32  compColorMapLoc;
    gX_I32  compBlurMapLoc;
    gX_I32  compCocMapLoc;
    gX_I32  compDepthMapLoc;

    gX_I1   locsCached;
} gX_RendererGFX_DOF;

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: DOF draw and loc caching
gX_I0 gX_RendererGFX_DOF_Draw(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_DOF_CacheLocs(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_DOF_H
