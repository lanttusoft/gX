/**********************************************************************************************
*
*   [gX: src/renderer/gfx/csm.h]
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



#ifndef RENDERER_GFX_CSM_H
#define RENDERER_GFX_CSM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "texture.h"

typedef struct gX_RendererGFX_CSM {
    gX_RendererGFX_RenderTexture map; //opencode: single 2D array depth texture (4 layers)
    gX_U32     arrayTexId;   //opencode: GL_TEXTURE_2D_ARRAY texture name
    gX_U32     fboId;        //opencode: shared FBO (layer set via glFramebufferTextureLayer)
    gX_Matrix  vps[4];
    gX_F32     splits[4];
    gX_F32     frustumSizes[4];
    gX_I1      inited;
    gX_I32     cascadeCount;
    gX_F32     cascadeDistances[4];
    gX_F32     blendScale;
} gX_RendererGFX_CSM;

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_CSM_H
