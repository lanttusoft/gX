/**********************************************************************************************
*
*   [gX: src/renderer/gfx/mesh_visibility.h]
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
//ai: conservative homogeneous clip tests for immutable, unskinned file meshes.
#ifndef GX_GFX_MESH_VISIBILITY_H
#define GX_GFX_MESH_VISIBILITY_H

#include "../../common/types.h"
#include <math.h>

static gX_I1 gfx_mesh_bounds_build(const gX_Mesh *Mesh, gX_F32 Bounds[6])
{
    for (gX_I32 Axis = 0; Axis < 3; Axis++)
    {
        Bounds[Axis] = INFINITY;
        Bounds[Axis + 3] = -INFINITY;
    }
    if (!Mesh || !Mesh->vertices || Mesh->vertexCount <= 0
        || Mesh->boneIndices || Mesh->boneWeights || Mesh->animVertices)
        return 0;
    for (gX_I32 Vertex = 0; Vertex < Mesh->vertexCount; Vertex++)
    {
        for (gX_I32 Axis = 0; Axis < 3; Axis++)
        {
            gX_F32 Value = Mesh->vertices[Vertex * 3 + Axis];
            if (!isfinite(Value))
            {
                Bounds[0] = INFINITY;
                return 0;
            }
            if (Value < Bounds[Axis]) Bounds[Axis] = Value;
            if (Value > Bounds[Axis + 3]) Bounds[Axis + 3] = Value;
        }
    }
    return 1;
}

static gX_I1 gfx_mesh_bounds_outside(const gX_F32 Bounds[6], gX_Matrix MVP)
{
    const gX_F32 *Matrix = &MVP.m0;
    for (gX_I32 Axis = 0; Axis < 3; Axis++)
    {
        if (!isfinite(Bounds[Axis]) || !isfinite(Bounds[Axis + 3])
            || Bounds[Axis] > Bounds[Axis + 3]) return 0;
    }
    for (gX_I32 Element = 0; Element < 16; Element++)
        if (!isfinite(Matrix[Element])) return 0;
    for (gX_I32 Plane = 0; Plane < 6; Plane++)
    {
        gX_I32 Row = Plane / 2;
        gX_F32 Sign = (Plane & 1) ? -1.0f : 1.0f;
        gX_F32 Maximum = Matrix[15] + Sign * Matrix[Row * 4 + 3];
        gX_F32 Scale = 1.0f + fabsf(Matrix[15]) + fabsf(Matrix[Row * 4 + 3]);
        for (gX_I32 Axis = 0; Axis < 3; Axis++)
        {
            gX_F32 Coefficient = Matrix[12 + Axis] + Sign * Matrix[Row * 4 + Axis];
            Maximum += Coefficient * Bounds[Axis + (Coefficient >= 0.0f ? 3 : 0)];
            Scale += (fabsf(Matrix[12 + Axis]) + fabsf(Matrix[Row * 4 + Axis]))
                * fmaxf(fabsf(Bounds[Axis]), fabsf(Bounds[Axis + 3]));
        }
        if (isfinite(Maximum) && isfinite(Scale) && Maximum < -0.00001f * Scale)
            return 1;
    }
    return 0;
}

#endif
