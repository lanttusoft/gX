/**********************************************************************************************
*
*   [gX: src/renderer/gfx/ibl.h]
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



#ifndef RENDERER_GFX_IBL_H
#define RENDERER_GFX_IBL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "shader.h"

typedef struct gX_RendererGFX_IBL {
    gX_Texture  irradianceMap;
    gX_Texture  prefilterMap;
    gX_Texture  brdfLut;
    gX_Texture  dummyCubemap;
    gX_RendererGFX_Shader brdfShader;
    gX_RendererGFX_Shader irradianceShader;
    gX_RendererGFX_Shader prefilterShader;
    gX_I1       ready;
    gX_I1       failed; //opencode: set on FBO failure — stops per-frame retry storm
} gX_RendererGFX_IBL;

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_IBL_H
