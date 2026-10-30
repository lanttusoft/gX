/**********************************************************************************************
*
*   [gX: src/renderer/gfx/layout.c]
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


//opencode: auto-seat pass — world AABB from the same SRT composition the draw
//   path uses (S*R*T in gxmath multiply order = T@R@S standard), support
//   height from the mirror slab (or water plane / 0.075 default), then
//   drop-to-support + XZ push-out. Verify uses strict positive-volume overlap
//   so resting contact (zero Y depth) passes.
#include <string.h>
#include <stdlib.h>
//opencode: hunt - implicit-decl sweep
#include <math.h>
#include <stdio.h>

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../gxmath/matrix.h"
#include "../../gxmath/constants.h"
#include "../../gxmath/vector3.h"
#include "../gfx.h"
#include "../../main.h"
#include "layout.h"
#include "model.h"

//opencode: layout epsilon — contact within this is resting, not penetration
#define GX_LAYOUT_EPS 0.001f
#define GX_LAYOUT_MAX_SWEEPS 16

static gX_I1 Layout_IsSupport(gX_RendererGFX_Model *m)
{
    return (m->params.reflectionSource || m->params.planarReflection) ? 1 : 0;
}

gX_I1 gX_RendererGFX_Layout_WorldAABB(gX_RendererGFX *gfx, gX_U32 idx, gX_F32 outMin[3], gX_F32 outMax[3])
{
    gX_RendererGFX_Model *modelPtrs = (gX_RendererGFX_Model*)gfx->models.data;
    gX_RendererGFX_Model *model = &modelPtrs[idx];
    if (!model->aabbValid)
    {
        outMin[0] = outMin[1] = outMin[2] = 0.0f;
        outMax[0] = outMax[1] = outMax[2] = 0.0f;
        outMin[0] = model->params.pos.x;
        outMin[1] = model->params.pos.y;
        outMin[2] = model->params.pos.z;
        outMax[0] = model->params.pos.x;
        outMax[1] = model->params.pos.y;
        outMax[2] = model->params.pos.z;
        return 1;
    }
    gX_Matrix s_mat = gX_Matrix_Scale(model->params.scale.x, model->params.scale.y, model->params.scale.z);
    gX_Matrix r_mat = gX_Matrix_RotateXYZ((gX_F32v3){
        model->params.rotation.x * gX_DEG2RAD,
        model->params.rotation.y * gX_DEG2RAD,
        model->params.rotation.z * gX_DEG2RAD
    });
    gX_Matrix t_mat = gX_Matrix_Translate(model->params.pos.x, model->params.pos.y, model->params.pos.z);
    gX_Matrix trs = gX_Matrix_Multiply(s_mat, r_mat);
    trs = gX_Matrix_Multiply(trs, t_mat);
    gX_I1 first = 1;
    for (gX_I32 cx = 0; cx < 2; cx++)
    {
        for (gX_I32 cy = 0; cy < 2; cy++)
        {
            for (gX_I32 cz = 0; cz < 2; cz++)
            {
                gX_F32v3 corner = {
                    cx ? model->aabbMax[0] : model->aabbMin[0],
                    cy ? model->aabbMax[1] : model->aabbMin[1],
                    cz ? model->aabbMax[2] : model->aabbMin[2]
                };
                gX_F32v3 w = gX_F32v3_Transform(corner, trs);
                if (first)
                {
                    outMin[0] = outMax[0] = w.x;
                    outMin[1] = outMax[1] = w.y;
                    outMin[2] = outMax[2] = w.z;
                    first = 0;
                }
                else
                {
                    if (w.x < outMin[0]) outMin[0] = w.x;
                    if (w.y < outMin[1]) outMin[1] = w.y;
                    if (w.z < outMin[2]) outMin[2] = w.z;
                    if (w.x > outMax[0]) outMax[0] = w.x;
                    if (w.y > outMax[1]) outMax[1] = w.y;
                    if (w.z > outMax[2]) outMax[2] = w.z;
                }
            }
        }
    }
    return 0;
}

gX_F32 gX_RendererGFX_Layout_SupportY(gX_RendererGFX *gfx)
{
    extern gX_MainArguments arguments;
    if (arguments.showcaseWater) return 0.0f;
    gX_RendererGFX_Model *modelPtrs = (gX_RendererGFX_Model*)gfx->models.data;
    for (gX_U32 mi = 0; mi < gfx->models.amount; mi++)
    {
        if (!gfx->models.meta[mi].USED) continue;
        if (!modelPtrs[mi].loaded) continue;
        if (!Layout_IsSupport(&modelPtrs[mi])) continue;
        if (!modelPtrs[mi].aabbValid) continue;
        {
            gX_F32 mn[3], mx[3];
            gX_RendererGFX_Layout_WorldAABB(gfx, mi, mn, mx);
            return mx[1];
        }
    }
    return 0.075f;
}

gX_I32 gX_RendererGFX_Layout_Reseat(gX_RendererGFX *gfx, gX_F32 supportY)
{
    gX_RendererGFX_Model *modelPtrs = (gX_RendererGFX_Model*)gfx->models.data;
    gX_I32 moves = 0;
    if (getenv("GX_LAYOUT_DUMP")) {
        for (gX_U32 mi = 0; mi < gfx->models.amount; mi++) {
            if (!gfx->models.meta[mi].USED) continue;
            if (!modelPtrs[mi].loaded || !modelPtrs[mi].aabbValid) continue;
            {
                gX_F32 mn[3], mx[3];
                gX_RendererGFX_Layout_WorldAABB(gfx, mi, mn, mx);
                gX_TraceLog_GFX(gX_LOG_NOTICE,
                    "LAYOUT: model %u '%s' pos(%.2f,%.2f,%.2f) scale(%.2f,%.2f,%.2f) rot(%.0f,%.0f,%.0f) wmin(%.2f,%.2f,%.2f) wmax(%.2f,%.2f,%.2f)%s",
                    mi, modelPtrs[mi].path,
                    modelPtrs[mi].params.pos.x, modelPtrs[mi].params.pos.y, modelPtrs[mi].params.pos.z,
                    modelPtrs[mi].params.scale.x, modelPtrs[mi].params.scale.y, modelPtrs[mi].params.scale.z,
                    modelPtrs[mi].params.rotation.x, modelPtrs[mi].params.rotation.y, modelPtrs[mi].params.rotation.z,
                    mn[0], mn[1], mn[2], mx[0], mx[1], mx[2],
                    Layout_IsSupport(&modelPtrs[mi]) ? " SUPPORT" : "");
            }
        }
    }
    for (gX_U32 mi = 0; mi < gfx->models.amount; mi++)
    {
        if (!gfx->models.meta[mi].USED) continue;
        if (!modelPtrs[mi].loaded) continue;
        if (!modelPtrs[mi].aabbValid) continue;
        if (Layout_IsSupport(&modelPtrs[mi])) continue;
        {
            gX_F32 mn[3], mx[3];
            gX_RendererGFX_Layout_WorldAABB(gfx, mi, mn, mx);
            if (mn[1] < supportY - GX_LAYOUT_EPS)
            {
                gX_F32 oldY = modelPtrs[mi].params.pos.y;
                modelPtrs[mi].params.pos.y += (supportY - mn[1]);
                moves++;
                gX_TraceLog_GFX(gX_LOG_NOTICE, "LAYOUT: lift model %u y %.3f -> %.3f (rest on %.3f)",
                    mi, oldY, modelPtrs[mi].params.pos.y, supportY);
            }
        }
    }
    //opencode: XZ push-out — each model clears all EARLIER models at once
    //   along one (axis,dir) that every overlapping earlier partner agrees
    //   on. Pairwise sequential push ping-pongs when trapped between two
    //   neighbours (+eps never escapes); the agreed-side rule resolves each
    //   model in one move, so a single sweep converges and later models
    //   never disturb earlier pairs (their movers only move away).
    for (gX_I32 sweep = 0; sweep < GX_LAYOUT_MAX_SWEEPS; sweep++)
    {
        gX_I1 moved = 0;
        for (gX_U32 bi = 0; bi < gfx->models.amount; bi++)
        {
            if (!gfx->models.meta[bi].USED) continue;
            if (!modelPtrs[bi].loaded || !modelPtrs[bi].aabbValid) continue;
            if (Layout_IsSupport(&modelPtrs[bi])) continue;
            {
                gX_F32 bmn[3], bmx[3];
                gX_RendererGFX_Layout_WorldAABB(gfx, bi, bmn, bmx);
                gX_F32 bcx = (bmn[0] + bmx[0]) * 0.5f;
                gX_F32 bcz = (bmn[2] + bmx[2]) * 0.5f;
                gX_I32 bestAxis = -1;
                gX_F32 bestDir = 0.0f;
                gX_F32 bestShift = 0.0f;
                for (gX_I32 axis = 0; axis < 2; axis++)
                {
                    for (gX_I32 di = 0; di < 2; di++)
                    {
                        gX_F32 dir = di ? 1.0f : -1.0f;
                        gX_F32 need = 0.0f;
                        gX_I1 feasible = 1;
                        gX_I1 hasOverlap = 0;
                        for (gX_U32 ai = 0; ai < bi; ai++)
                        {
                            if (!gfx->models.meta[ai].USED) continue;
                            if (!modelPtrs[ai].loaded || !modelPtrs[ai].aabbValid) continue;
                            if (Layout_IsSupport(&modelPtrs[ai])) continue;
                            {
                                gX_F32 amn[3], amx[3];
                                gX_RendererGFX_Layout_WorldAABB(gfx, ai, amn, amx);
                                gX_F32 dx = (amx[0] < bmx[0] ? amx[0] : bmx[0]) - (amn[0] > bmn[0] ? amn[0] : bmn[0]);
                                gX_F32 dy = (amx[1] < bmx[1] ? amx[1] : bmx[1]) - (amn[1] > bmn[1] ? amn[1] : bmn[1]);
                                gX_F32 dz = (amx[2] < bmx[2] ? amx[2] : bmx[2]) - (amn[2] > bmn[2] ? amn[2] : bmn[2]);
                                gX_F32 pen = axis ? dz : dx;
                                gX_F32 other1 = axis ? dx : dz;
                                if (dy > GX_LAYOUT_EPS && other1 > GX_LAYOUT_EPS && pen > GX_LAYOUT_EPS)
                                {
                                    gX_F32 ac = axis ? (amn[2] + amx[2]) * 0.5f : (amn[0] + amx[0]) * 0.5f;
                                    gX_F32 bc = axis ? bcz : bcx;
                                    gX_F32 d = bc - ac;
                                    if (d != 0.0f && (d >= 0.0f ? 1.0f : -1.0f) != dir)
                                    {
                                        feasible = 0;
                                        break;
                                    }
                                    hasOverlap = 1;
                                    {
                                        gX_F32 req = pen + GX_LAYOUT_EPS;
                                        if (req > need) need = req;
                                    }
                                }
                            }
                        }
                        if (feasible && hasOverlap)
                        {
                            for (gX_U32 ai = 0; ai < bi; ai++)
                            {
                                if (!gfx->models.meta[ai].USED) continue;
                                if (!modelPtrs[ai].loaded || !modelPtrs[ai].aabbValid) continue;
                                if (Layout_IsSupport(&modelPtrs[ai])) continue;
                                {
                                    gX_F32 amn[3], amx[3];
                                    gX_RendererGFX_Layout_WorldAABB(gfx, ai, amn, amx);
                                    gX_F32 dx = (amx[0] < bmx[0] ? amx[0] : bmx[0]) - (amn[0] > bmn[0] ? amn[0] : bmn[0]);
                                    gX_F32 dy = (amx[1] < bmx[1] ? amx[1] : bmx[1]) - (amn[1] > bmn[1] ? amn[1] : bmn[1]);
                                    gX_F32 dz = (amx[2] < bmx[2] ? amx[2] : bmx[2]) - (amn[2] > bmn[2] ? amn[2] : bmn[2]);
                                    gX_F32 pen = axis ? dz : dx;
                                    gX_F32 other1 = axis ? dx : dz;
                                    if (dy > GX_LAYOUT_EPS && other1 > GX_LAYOUT_EPS && pen <= GX_LAYOUT_EPS)
                                    {
                                        gX_F32 ac = axis ? (amn[2] + amx[2]) * 0.5f : (amn[0] + amx[0]) * 0.5f;
                                        gX_F32 bc = axis ? bcz : bcx;
                                        gX_F32 d = bc - ac;
                                        if (d == 0.0f || (d >= 0.0f ? 1.0f : -1.0f) == dir)
                                        {
                                            if (need > -pen - GX_LAYOUT_EPS)
                                            {
                                                feasible = 0;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        if (feasible && hasOverlap
                            && (bestAxis < 0 || need < bestShift))
                        {
                            bestAxis = axis;
                            bestDir = dir;
                            bestShift = need;
                        }
                    }
                }
                if (bestAxis >= 0)
                {
                    if (bestAxis == 0) modelPtrs[bi].params.pos.x += bestDir * bestShift;
                    else modelPtrs[bi].params.pos.z += bestDir * bestShift;
                    moves++;
                    moved = 1;
                    gX_TraceLog_GFX(gX_LOG_NOTICE, "LAYOUT: push model %u %c %+.3f",
                        bi, bestAxis ? 'z' : 'x', bestDir * bestShift);
                }
            }
        }
        if (!moved) break;
        if (sweep == GX_LAYOUT_MAX_SWEEPS - 1)
        {
            gX_TraceLog_GFX(gX_LOG_WARNING, "LAYOUT: push-out did not converge in %d sweeps", GX_LAYOUT_MAX_SWEEPS);
        }
    }
    if (moves == 0)
    {
        gX_TraceLog_GFX(gX_LOG_NOTICE, "LAYOUT: reseat no-op (support %.3f)", supportY);
    }
    return moves;
}

gX_I32 gX_RendererGFX_Layout_Verify(gX_RendererGFX *gfx, gX_F32 supportY)
{
    gX_RendererGFX_Model *modelPtrs = (gX_RendererGFX_Model*)gfx->models.data;
    gX_I32 violations = 0;
    for (gX_U32 mi = 0; mi < gfx->models.amount; mi++)
    {
        if (!gfx->models.meta[mi].USED) continue;
        if (!modelPtrs[mi].loaded || !modelPtrs[mi].aabbValid) continue;
        if (Layout_IsSupport(&modelPtrs[mi])) continue;
        {
            gX_F32 mn[3], mx[3];
            gX_RendererGFX_Layout_WorldAABB(gfx, mi, mn, mx);
            if (mn[1] < supportY - GX_LAYOUT_EPS)
            {
                violations++;
                gX_TraceLog_GFX(gX_LOG_WARNING, "LAYOUT: model %u sunk (min-y %.3f < support %.3f)",
                    mi, mn[1], supportY);
            }
        }
    }
    for (gX_U32 ai = 0; ai < gfx->models.amount; ai++)
    {
        if (!gfx->models.meta[ai].USED) continue;
        if (!modelPtrs[ai].loaded || !modelPtrs[ai].aabbValid) continue;
        if (Layout_IsSupport(&modelPtrs[ai])) continue;
        for (gX_U32 bi = ai + 1; bi < gfx->models.amount; bi++)
        {
            if (!gfx->models.meta[bi].USED) continue;
            if (!modelPtrs[bi].loaded || !modelPtrs[bi].aabbValid) continue;
            if (Layout_IsSupport(&modelPtrs[bi])) continue;
            {
                gX_F32 amn[3], amx[3], bmn[3], bmx[3];
                gX_RendererGFX_Layout_WorldAABB(gfx, ai, amn, amx);
                gX_RendererGFX_Layout_WorldAABB(gfx, bi, bmn, bmx);
                gX_F32 dx = (amx[0] < bmx[0] ? amx[0] : bmx[0]) - (amn[0] > bmn[0] ? amn[0] : bmn[0]);
                gX_F32 dy = (amx[1] < bmx[1] ? amx[1] : bmx[1]) - (amn[1] > bmn[1] ? amn[1] : bmn[1]);
                gX_F32 dz = (amx[2] < bmx[2] ? amx[2] : bmx[2]) - (amn[2] > bmn[2] ? amn[2] : bmn[2]);
                if (dx > GX_LAYOUT_EPS && dy > GX_LAYOUT_EPS && dz > GX_LAYOUT_EPS)
                {
                    violations++;
                    gX_TraceLog_GFX(gX_LOG_WARNING, "LAYOUT: models %u+%u overlap (%.3f,%.3f,%.3f)",
                        ai, bi, dx, dy, dz);
                }
            }
        }
    }
    gX_TraceLog_GFX(violations ? gX_LOG_WARNING : gX_LOG_NOTICE,
        "LAYOUT: verify %d violation(s) (support %.3f)", violations, supportY);
    return violations;
}
