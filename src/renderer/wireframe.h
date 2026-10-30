/**********************************************************************************************
*
*   [gX: src/renderer/wireframe.h]
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


#ifndef RENDERER_WIREFRAME_H
#define RENDERER_WIREFRAME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/types/state.h"

//opencode: max models in wireframe cycle list
#define GX_WIREFRAME_MODEL_MAX  4

typedef struct gX_RendererWireframe {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_LoadingState loadingstate;

    gX_Model models[GX_WIREFRAME_MODEL_MAX];   // loaded models to cycle through
    gX_I32  modelCount;                         // number of loaded models
    gX_I32  currentModel;                       // index into models[]
    gX_F32  orbitAngle;                         // accumulated camera orbit angle (radians)
    gX_F32  orbitSpeed;                         // radians per second
    gX_Color wireColor;                         // current wireframe tint
    gX_Color bgColor;                           // current background color

    //opencode: custom VAOs (bypasses raylib mesh upload which produces invalid VAOs)
    gX_U32  customVao[GX_WIREFRAME_MODEL_MAX];
    gX_U32  customVbo[GX_WIREFRAME_MODEL_MAX];
    gX_U32  customEbo[GX_WIREFRAME_MODEL_MAX];
    gX_I32  customTriCount[GX_WIREFRAME_MODEL_MAX];

} gX_RendererWireframe;

gX_I32 gX_RendererWireframe_Load(gX_RendererWireframe *wf);
gX_I32 gX_RendererWireframe_Unload(gX_RendererWireframe *wf);

gX_I0 gX_RendererWireframe_Update(gX_RendererWireframe *wf);
//opencode: DrawTo with explicit output RT (NULL = renderer.canvas)
gX_I0 gX_RendererWireframe_DrawTo(gX_RendererWireframe *wf, gX_RenderTexture *outputRT);
#define gX_RendererWireframe_Draw(wf) gX_RendererWireframe_DrawTo((wf), NULL)

#ifdef __cplusplus
}
#endif

#endif //RENDERER_WIREFRAME_H
