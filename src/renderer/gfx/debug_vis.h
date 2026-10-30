/**********************************************************************************************
*
*   [gX: src/renderer/gfx/debug_vis.h]
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



#ifndef RENDERER_GFX_DEBUG_VIS_H
#define RENDERER_GFX_DEBUG_VIS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "../../wrappers/wrappers.h"
#include "texture.h"

typedef struct gX_RendererGFX_DebugVis {
    gX_Shader   debugNormalsShader;
    gX_I32      debugNormalsMode;
    gX_I1       debugNormalsLoaded;
    gX_RendererGFX_RenderTexture refractionBg;
    //ai: transient, valid only after the current pre-glass copy and during its draw phase.
    gX_I1 refractionReady;
} gX_RendererGFX_DebugVis;

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_DEBUG_VIS_H
