/**********************************************************************************************
*
*   [gX: src/renderer/gfx/layout.h]
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


#ifndef RENDERER_GFX_LAYOUT_H
#define RENDERER_GFX_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: auto-seat pass — guarantees no interpenetration in the demo scene.
//   Drop-to-support lifts sunk models onto the support plane; XZ push-out
//   separates overlapping footprints. Deterministic (heap order, bounded
//   sweeps), moves logged. Verifies with strict positive-volume overlap.
gX_I1 gX_RendererGFX_Layout_WorldAABB(gX_RendererGFX *gfx, gX_U32 idx, gX_F32 outMin[3], gX_F32 outMax[3]);
gX_F32 gX_RendererGFX_Layout_SupportY(gX_RendererGFX *gfx);
gX_I32 gX_RendererGFX_Layout_Reseat(gX_RendererGFX *gfx, gX_F32 supportY);
gX_I32 gX_RendererGFX_Layout_Verify(gX_RendererGFX *gfx, gX_F32 supportY);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_LAYOUT_H
