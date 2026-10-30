/**********************************************************************************************
*
*   [gX: src/renderer/lissajous.h]
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


//opencode: Lissajous renderer — stereo XY phase plot from synth audio
#ifndef RENDERER_LISSAJOUS_H
#define RENDERER_LISSAJOUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/types/state.h"
#include "../wrappers/wrappers.h"

typedef struct gX_RendererLissajous {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_LoadingState loadingstate;

    gX_RenderTexture rt;          // render target
    gX_F32 scale;                 // overall zoom
    gX_F32 trail;                 // 0=no fade, 1=infinite persistence
    gX_Color color;
    gX_U32 pointCount;            // number of points to draw
    gX_F32 pointSize;             // size of each dot
} gX_RendererLissajous;

gX_I32 gX_RendererLissajous_Load(gX_RendererLissajous *lis);
gX_I32 gX_RendererLissajous_Unload(gX_RendererLissajous *lis);
gX_I0  gX_RendererLissajous_Update(gX_RendererLissajous *lis);
//opencode: DrawTo with explicit output RT (NULL = module's own persistent RT)
gX_I0  gX_RendererLissajous_DrawTo(gX_RendererLissajous *lis, gX_RenderTexture *outputRT);
#define gX_RendererLissajous_Draw(lis) gX_RendererLissajous_DrawTo((lis), NULL)

#ifdef __cplusplus
}
#endif

#endif //RENDERER_LISSAJOUS_H
