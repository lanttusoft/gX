/**********************************************************************************************
*
*   [gX: src/rhi/batch.h]
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


#ifndef RHI_BATCH_H
#define RHI_BATCH_H

#include "../common/types.h"

typedef enum {
    GX_BATCH_QUADS,
    GX_BATCH_TRIANGLES,
    GX_BATCH_LINES,
} gX_BatchMode;

gX_I0 gX_Batch_Init(gX_I0);
gX_I0 gX_Batch_Shutdown(gX_I0);
gX_I0 gX_Batch_Begin(gX_BatchMode mode);
gX_I0 gX_Batch_End(gX_I0);
gX_I0 gX_Batch_Flush(gX_I0);
gX_U32 gX_Batch_GetShader(gX_I0);       //opencode: v0.3.8 — internal program for raw draws
gX_U32 gX_Batch_GetDefaultTex(gX_I0);  //opencode: v0.3.8 — 1x1 white texture
gX_I0 gX_Batch_Vertex3f(gX_F32 x, gX_F32 y, gX_F32 z);
gX_I0 gX_Batch_Vertex2f(gX_F32 x, gX_F32 y);
gX_I0 gX_Batch_TexCoord2f(gX_F32 u, gX_F32 v);
gX_I0 gX_Batch_Color4ub(gX_U8 r, gX_U8 g, gX_U8 b, gX_U8 a);
gX_I0 gX_Batch_Normal3f(gX_F32 x, gX_F32 y, gX_F32 z);
gX_I0 gX_Batch_SetTexture(gX_U32 texId);

#endif
