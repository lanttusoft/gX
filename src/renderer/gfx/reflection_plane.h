/**********************************************************************************************
*
*   [gX: src/renderer/gfx/reflection_plane.h]
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
//opencode: shared model-local to world-space plane conversion for planar capture,
//   receiver shading, and the editor's fitted-plane authoring controls.
#ifndef RENDERER_GFX_REFLECTION_PLANE_H
#define RENDERER_GFX_REFLECTION_PLANE_H

#include "model.h"
#include "../../gxmath/constants.h"
#include "../../gxmath/matrix.h"

#include <math.h>

static gX_Matrix gfx_reflection_plane_rotation(const gX_RendererGFX_Model *model)
{
    return gX_Matrix_RotateXYZ((gX_F32v3){
        model->params.rotation.x * gX_DEG2RAD,
        model->params.rotation.y * gX_DEG2RAD,
        model->params.rotation.z * gX_DEG2RAD});
}

static gX_F32v3 gfx_reflection_plane_rotate(gX_Matrix rotation, gX_F32v3 vector)
{
    return (gX_F32v3){
        rotation.m0 * vector.x + rotation.m4 * vector.y + rotation.m8 * vector.z,
        rotation.m1 * vector.x + rotation.m5 * vector.y + rotation.m9 * vector.z,
        rotation.m2 * vector.x + rotation.m6 * vector.y + rotation.m10 * vector.z};
}

static gX_F32v3 gfx_reflection_plane_unrotate(gX_Matrix rotation, gX_F32v3 vector)
{
    return (gX_F32v3){
        rotation.m0 * vector.x + rotation.m1 * vector.y + rotation.m2 * vector.z,
        rotation.m4 * vector.x + rotation.m5 * vector.y + rotation.m6 * vector.z,
        rotation.m8 * vector.x + rotation.m9 * vector.y + rotation.m10 * vector.z};
}

static gX_F32 gfx_reflection_plane_dot(gX_F32v3 a, gX_F32v3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static gX_I1 gfx_reflection_plane_world(const gX_RendererGFX_Model *model,
    gX_F32v3 *normal, gX_F32 *distance)
{
    if (!model || !normal || !distance) return 0;
    gX_F32v3 worldNormal = model->params.reflectionNormal;
    gX_F32 worldDistance = model->params.reflectionPlaneHeight;
    if (model->params.reflectionPlaneFollowsModel) {
        worldNormal = gfx_reflection_plane_rotate(
            gfx_reflection_plane_rotation(model), worldNormal);
        worldDistance += gfx_reflection_plane_dot(worldNormal, model->params.pos);
    }
    gX_F32 lengthSquared = gfx_reflection_plane_dot(worldNormal, worldNormal);
    if (lengthSquared < 0.000000000001f) return 0;
    gX_F32 inverseLength = 1.0f / sqrtf(lengthSquared);
    *normal = (gX_F32v3){
        worldNormal.x * inverseLength,
        worldNormal.y * inverseLength,
        worldNormal.z * inverseLength};
    *distance = worldDistance * inverseLength;
    return 1;
}

static gX_I0 gfx_reflection_plane_store_world(gX_RendererGFX_Model *model,
    gX_F32v3 worldNormal, gX_F32 worldDistance)
{
    if (!model) return;
    gX_F32 lengthSquared = gfx_reflection_plane_dot(worldNormal, worldNormal);
    if (lengthSquared < 0.000000000001f) {
        worldNormal = (gX_F32v3){0.0f, 1.0f, 0.0f};
        lengthSquared = 1.0f;
        worldDistance = 0.0f;
    }
    gX_F32 inverseLength = 1.0f / sqrtf(lengthSquared);
    worldNormal = (gX_F32v3){
        worldNormal.x * inverseLength,
        worldNormal.y * inverseLength,
        worldNormal.z * inverseLength};
    worldDistance *= inverseLength;
    if (model->params.reflectionPlaneFollowsModel) {
        gX_Matrix rotation = gfx_reflection_plane_rotation(model);
        gX_F32v3 localNormal = gfx_reflection_plane_unrotate(rotation, worldNormal);
        gX_F32 localDistance = worldDistance
            - gfx_reflection_plane_dot(worldNormal, model->params.pos);
        model->params.reflectionNormal = localNormal;
        model->params.reflectionPlaneHeight = localDistance;
    } else {
        model->params.reflectionNormal = worldNormal;
        model->params.reflectionPlaneHeight = worldDistance;
    }
}

#endif
