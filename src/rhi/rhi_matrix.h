/**********************************************************************************************
*
*   [gX: src/rhi/rhi_matrix.h]
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


#ifndef RHI_MATRIX_H
#define RHI_MATRIX_H

#include "../common/types.h"

#define GX_RHI_MATRIX_MODELVIEW  0
#define GX_RHI_MATRIX_PROJECTION 1
#define GX_RHI_MATRIX_TEXTURE    2

#define GX_RHI_MATRIX_STACK_DEPTH 32

gX_I0     gX_RHI_Matrix_Mode(gX_U32 mode);
gX_I0     gX_RHI_Matrix_Push(gX_I0);
gX_I0     gX_RHI_Matrix_Pop(gX_I0);
gX_I0     gX_RHI_Matrix_LoadIdentity(gX_I0);
gX_I0     gX_RHI_Matrix_Translate(gX_F32 x, gX_F32 y, gX_F32 z);
gX_I0     gX_RHI_Matrix_Rotate(gX_F32 angle, gX_F32 x, gX_F32 y, gX_F32 z);
gX_I0     gX_RHI_Matrix_Scale(gX_F32 x, gX_F32 y, gX_F32 z);
gX_I0     gX_RHI_Matrix_Mult(const gX_F32 *mat);
gX_I0     gX_RHI_Matrix_Frustum(gX_F64 left, gX_F64 right, gX_F64 bottom, gX_F64 top, gX_F64 near, gX_F64 far);
gX_I0     gX_RHI_Matrix_Ortho(gX_F64 left, gX_F64 right, gX_F64 bottom, gX_F64 top, gX_F64 near, gX_F64 far);
gX_Matrix gX_RHI_Matrix_GetModelview(gX_I0);
gX_Matrix gX_RHI_Matrix_GetProjection(gX_I0);
gX_Matrix gX_RHI_Matrix_GetTransform(gX_I0);
gX_U32   gX_RHI_Matrix_GetMode(gX_I0);
gX_I32   gX_RHI_Matrix_GetStackDepth(gX_U32 mode);
gX_I0     gX_RHI_Matrix_SetModelview(gX_Matrix matrix);
gX_I0     gX_RHI_Matrix_SetProjection(gX_Matrix matrix);

#endif
