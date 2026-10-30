/**********************************************************************************************
*
*   [gX: src/renderer/generate.h]
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


//opencode: procedural pattern generator renderer — GPU-based fullscreen shader patterns
#ifndef RENDERER_GENERATE_H
#define RENDERER_GENERATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/types/state.h"

//opencode: pattern mode constants
#define GX_GENERATE_MODE_PLASMA     0
#define GX_GENERATE_MODE_GRADIENT   1
#define GX_GENERATE_MODE_CHECKER    2
#define GX_GENERATE_MODE_NOISE      3
#define GX_GENERATE_MODE_COUNT      4

typedef struct gX_RendererGenerate_State {

    gX_I32 mode;          // GX_GENERATE_MODE_*
    gX_F32 speed;         // animation speed multiplier
    gX_F32 scale;         // pattern scale
    gX_F32v3 color1;      // primary color
    gX_F32v3 color2;      // secondary color
    gX_I1  autoCycle;     // auto-cycle through modes

} gX_RendererGenerate_State;

typedef struct gX_RendererGenerate_Shader {

    gX_Shader shader;
    gX_I32 timeLoc;
    gX_I32 resLoc;
    gX_I32 modeLoc;
    gX_I32 speedLoc;
    gX_I32 scaleLoc;
    gX_I32 color1Loc;
    gX_I32 color2Loc;

} gX_RendererGenerate_Shader;

typedef struct gX_RendererGenerate {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_LoadingState loadingstate;
    gX_RendererGenerate_State state;
    gX_RendererGenerate_Shader shader;

    gX_F64 startTime;

} gX_RendererGenerate;

gX_I32 gX_RendererGenerate_Load(gX_RendererGenerate *gene);
gX_I32 gX_RendererGenerate_Unload(gX_RendererGenerate *gene);

gX_I0 gX_RendererGenerate_Update(gX_RendererGenerate *gene);
//opencode: DrawTo with explicit output RT (NULL = renderer.canvas)
gX_I0 gX_RendererGenerate_DrawTo(gX_RendererGenerate *gene, gX_RenderTexture *outputRT);
#define gX_RendererGenerate_Draw(gene) gX_RendererGenerate_DrawTo((gene), NULL)

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GENERATE_H
