/**********************************************************************************************
*
*   [gX: src/renderer/oscillo.h]
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


//opencode: oscilloscope renderer — waveform display from synth scope buffer
#ifndef RENDERER_OSCILLO_H
#define RENDERER_OSCILLO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/types/state.h"
#include "../wrappers/wrappers.h"

typedef struct gX_RendererOscillo {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_LoadingState loadingstate;

    gX_RenderTexture rt;          // render target for waveform
    gX_Color lineColor;
    gX_F32 lineWidth;
    gX_U32 pointCount;            // samples to display (max 2048)
    gX_F32 ampScale;              // vertical zoom (1.0 = full range)
    gX_I1  trigger;               // trigger mode
    gX_F32 triggerLevel;
} gX_RendererOscillo;

gX_I32 gX_RendererOscillo_Load(gX_RendererOscillo *osc);
gX_I32 gX_RendererOscillo_Unload(gX_RendererOscillo *osc);
gX_I0  gX_RendererOscillo_Update(gX_RendererOscillo *osc);
//opencode: DrawTo with explicit output RT (NULL = module's own persistent RT)
gX_I0  gX_RendererOscillo_DrawTo(gX_RendererOscillo *osc, gX_RenderTexture *outputRT);
#define gX_RendererOscillo_Draw(osc) gX_RendererOscillo_DrawTo((osc), NULL)

#ifdef __cplusplus
}
#endif

#endif //RENDERER_OSCILLO_H
