/**********************************************************************************************
*
*   [gX: src/renderer/gfx/motion_path.h]
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


//opencode: shared waypoint-polyline evaluator for authored motion paths.
#ifndef RENDERER_GFX_MOTION_PATH_H
#define RENDERER_GFX_MOTION_PATH_H

#include "../../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

//opencode: evaluate a waypoint polyline at the configured speed. Waypoints are
//   flat xyz triples; loop wraps, ping-pong reverses, and one-shot playback
//   clamps to the final point and sets *paused. With smooth set, each segment
//   is sampled as a Catmull-Rom spline through its neighbours instead of a
//   straight line. With bezier set, tangents (flat xyz out-handles per
//   waypoint) drive cubic Bezier segments; tangentsIn holds the in-handles and
//   is used only when independentHandles is set (otherwise the in side mirrors
//   the out side). Zero handles give the straight chord. Callers own the
//   authored state; this only advances the sample.
gX_I0 gX_MotionPath_Waypoint_Update(gX_F32 *waypoints, const gX_F32 *tangents,
    const gX_F32 *tangentsIn, gX_I1 independentHandles,
    gX_I32 *waypointCount, gX_I32 maxWaypoints, gX_I1 loop, gX_I1 pingPong,
    gX_I1 smooth, gX_I1 bezier, gX_F32 speed, gX_F64 deltaTime, gX_F64 *elapsed,
    gX_I1 *paused, gX_F32v3 *position);

//opencode: cubic Bezier point with per-point handles: segment control points
//   are p0 + handle0 (out-handle) and p1 - handle1 (in-handle). Shared with
//   the viewport preview.
gX_F32v3 gX_MotionPath_Bezier_Point(gX_F32v3 p0, gX_F32v3 p1,
    gX_F32v3 handle0, gX_F32v3 handle1, gX_F32 t);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_MOTION_PATH_H
