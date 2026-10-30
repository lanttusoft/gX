/**********************************************************************************************
*
*   [gX: src/rhi/rhi_matrix.c]
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


#include "rhi_matrix.h"
#include "../gxmath/matrix.h"
#include "../gxmath/constants.h"
#include <string.h>

static gX_Matrix gx_stack[3][GX_RHI_MATRIX_STACK_DEPTH];
static gX_I32   gx_top[3] = {0, 0, 0};
static gX_U32   gx_mode = GX_RHI_MATRIX_MODELVIEW;
static gX_I1    gx_inited = 0;

static void ensure(void) {
    if (!gx_inited) {
        gX_Matrix id = gX_Matrix_Identity();
        for (gX_I32 m = 0; m < 3; m++) gx_stack[m][0] = id;
        gx_inited = 1;
    }
}

gX_I0 gX_RHI_Matrix_Mode(gX_U32 mode) {
    if (mode <= GX_RHI_MATRIX_TEXTURE) gx_mode = mode;
}

gX_I0 gX_RHI_Matrix_Push(gX_I0) {
    ensure();
    gX_I32 t = gx_top[gx_mode];
    if (t < GX_RHI_MATRIX_STACK_DEPTH - 1) {
        gx_stack[gx_mode][t + 1] = gx_stack[gx_mode][t];
        gx_top[gx_mode]++;
    }
}

gX_I0 gX_RHI_Matrix_Pop(gX_I0) {
    if (gx_top[gx_mode] > 0) gx_top[gx_mode]--;
}

gX_I0 gX_RHI_Matrix_LoadIdentity(gX_I0) {
    ensure();
    gx_stack[gx_mode][gx_top[gx_mode]] = gX_Matrix_Identity();
}

gX_I0 gX_RHI_Matrix_Translate(gX_F32 x, gX_F32 y, gX_F32 z) {
    ensure();
    gX_Matrix t = gX_Matrix_Translate(x, y, z);
    gx_stack[gx_mode][gx_top[gx_mode]] = gX_Matrix_Multiply(gx_stack[gx_mode][gx_top[gx_mode]], t);
}

gX_I0 gX_RHI_Matrix_Rotate(gX_F32 angle, gX_F32 x, gX_F32 y, gX_F32 z) {
    ensure();
    gX_Matrix r = gX_Matrix_Rotate((gX_F32v3){x, y, z}, angle * gX_DEG2RAD);
    gx_stack[gx_mode][gx_top[gx_mode]] = gX_Matrix_Multiply(gx_stack[gx_mode][gx_top[gx_mode]], r);
}

gX_I0 gX_RHI_Matrix_Scale(gX_F32 x, gX_F32 y, gX_F32 z) {
    ensure();
    gX_Matrix s = gX_Matrix_Scale(x, y, z);
    gx_stack[gx_mode][gx_top[gx_mode]] = gX_Matrix_Multiply(gx_stack[gx_mode][gx_top[gx_mode]], s);
}

gX_I0 gX_RHI_Matrix_Mult(const gX_F32 *mat) {
    ensure();
    gX_Matrix m;
    memcpy(&m, mat, sizeof(m));
    gx_stack[gx_mode][gx_top[gx_mode]] = gX_Matrix_Multiply(gx_stack[gx_mode][gx_top[gx_mode]], m);
}

gX_I0 gX_RHI_Matrix_Frustum(gX_F64 left, gX_F64 right, gX_F64 bottom, gX_F64 top, gX_F64 near, gX_F64 far) {
    ensure();
    gx_stack[gx_mode][gx_top[gx_mode]] = gX_Matrix_Frustum(
        (gX_F32)left, (gX_F32)right, (gX_F32)bottom, (gX_F32)top, (gX_F32)near, (gX_F32)far);
}

gX_I0 gX_RHI_Matrix_Ortho(gX_F64 left, gX_F64 right, gX_F64 bottom, gX_F64 top, gX_F64 near, gX_F64 far) {
    ensure();
    gx_stack[gx_mode][gx_top[gx_mode]] = gX_Matrix_Ortho(
        (gX_F32)left, (gX_F32)right, (gX_F32)bottom, (gX_F32)top, (gX_F32)near, (gX_F32)far);
}

gX_Matrix gX_RHI_Matrix_GetModelview(gX_I0) {
    ensure();
    return gx_stack[0][gx_top[0]];
}

gX_Matrix gX_RHI_Matrix_GetProjection(gX_I0) {
    ensure();
    return gx_stack[1][gx_top[1]];
}

gX_Matrix gX_RHI_Matrix_GetTransform(gX_I0) {
    ensure();
    return gx_stack[2][gx_top[2]];
}

//opencode: expose matrix-stack bookkeeping to the frame audit without
//   exposing the backing arrays.
gX_U32 gX_RHI_Matrix_GetMode(gX_I0) {
    return gx_mode;
}

gX_I32 gX_RHI_Matrix_GetStackDepth(gX_U32 mode) {
    ensure();
    if (mode > GX_RHI_MATRIX_TEXTURE) return -1;
    return gx_top[mode];
}

gX_I0 gX_RHI_Matrix_SetModelview(gX_Matrix matrix) {
    ensure();
    gx_stack[0][gx_top[0]] = matrix;
}

gX_I0 gX_RHI_Matrix_SetProjection(gX_Matrix matrix) {
    ensure();
    gx_stack[1][gx_top[1]] = matrix;
}
