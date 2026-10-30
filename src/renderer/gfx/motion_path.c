/**********************************************************************************************
*
*   [gX: src/renderer/gfx/motion_path.c]
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
#include <math.h>

#include "motion_path.h"

static gX_F32v3 gX_MotionPath_Waypoint_Get(const gX_F32 *waypoints,
    gX_I32 index) {
    return (gX_F32v3){waypoints[index * 3], waypoints[index * 3 + 1],
        waypoints[index * 3 + 2]};
}

static gX_F32 gX_MotionPath_SegmentLength(const gX_F32 *waypoints,
    gX_I32 index) {
    gX_F32v3 a = gX_MotionPath_Waypoint_Get(waypoints, index);
    gX_F32v3 b = gX_MotionPath_Waypoint_Get(waypoints, index + 1);
    gX_F32 dx = b.x - a.x;
    gX_F32 dy = b.y - a.y;
    gX_F32 dz = b.z - a.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static gX_F32 gX_MotionPath_TotalLength(const gX_F32 *waypoints,
    gX_I32 waypointCount) {
    gX_F32 total = 0.0f;
    for (gX_I32 i = 0; i + 1 < waypointCount; i++)
        total += gX_MotionPath_SegmentLength(waypoints, i);
    return total;
}

//opencode: uniform Catmull-Rom through p1/p2 with p0/p3 as tangent anchors.
static gX_F32v3 gX_MotionPath_CatmullRom(gX_F32v3 p0, gX_F32v3 p1,
    gX_F32v3 p2, gX_F32v3 p3, gX_F32 t) {
    gX_F32 t2 = t * t;
    gX_F32 t3 = t2 * t;
    gX_F32v3 result;
    result.x = 0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t
        + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2
        + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
    result.y = 0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t
        + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2
        + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
    result.z = 0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * t
        + (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2
        + (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3);
    return result;
}

//opencode: cubic Bezier with per-point handles: segment i uses P_i + T_out_i
//   and P_{i+1} - T_in_{i+1} as control points; zero handles give the straight
//   chord. Shared with the viewport preview.
gX_F32v3 gX_MotionPath_Bezier_Point(gX_F32v3 p0, gX_F32v3 p1,
    gX_F32v3 handle0, gX_F32v3 handle1, gX_F32 t) {
    gX_F32 mt = 1.0f - t;
    gX_F32 a = mt * mt * mt;
    gX_F32 b = 3.0f * mt * mt * t;
    gX_F32 c = 3.0f * mt * t * t;
    gX_F32 d = t * t * t;
    gX_F32v3 result;
    result.x = a * p0.x + b * (p0.x + handle0.x) + c * (p1.x - handle1.x) + d * p1.x;
    result.y = a * p0.y + b * (p0.y + handle0.y) + c * (p1.y - handle1.y) + d * p1.y;
    result.z = a * p0.z + b * (p0.z + handle0.z) + c * (p1.z - handle1.z) + d * p1.z;
    return result;
}

gX_I0 gX_MotionPath_Waypoint_Update(gX_F32 *waypoints, const gX_F32 *tangents,
    const gX_F32 *tangentsIn, gX_I1 independentHandles,
    gX_I32 *waypointCount, gX_I32 maxWaypoints, gX_I1 loop, gX_I1 pingPong,
    gX_I1 smooth, gX_I1 bezier, gX_F32 speed, gX_F64 deltaTime,
    gX_F64 *elapsed, gX_I1 *paused, gX_F32v3 *position) {
    if (!waypoints || !waypointCount || !elapsed || !paused || !position)
        return;
    if (*waypointCount > maxWaypoints) *waypointCount = maxWaypoints;
    if (*waypointCount < 2) return;
    if (!isfinite(*elapsed) || *elapsed < 0.0) *elapsed = 0.0;
    if (!*paused && isfinite(deltaTime) && deltaTime > 0.0) {
        if (deltaTime > 0.25) deltaTime = 0.25;
        *elapsed += deltaTime;
    }
    gX_F32 rate = isfinite(speed) ? fabsf(speed) : 0.0f;
    gX_F32 total = gX_MotionPath_TotalLength(waypoints, *waypointCount);
    if (total <= 0.0001f) {
        *position = gX_MotionPath_Waypoint_Get(waypoints, 0);
        return;
    }
    gX_F32 distance = (gX_F32)(*elapsed * rate);
    if (loop) {
        if (pingPong) {
            gX_F32 period = 2.0f * total;
            distance = fmodf(distance, period);
            if (distance < 0.0f) distance += period;
            if (distance > total) distance = period - distance;
        } else {
            distance = fmodf(distance, total);
            if (distance < 0.0f) distance += total;
        }
    } else if (distance >= total) {
        distance = total;
        *paused = 1;
    }
    gX_F32 remaining = distance;
    gX_I32 segment = 0;
    while (segment + 2 < *waypointCount) {
        gX_F32 length = gX_MotionPath_SegmentLength(waypoints, segment);
        if (remaining <= length) break;
        remaining -= length;
        segment++;
    }
    gX_F32 length = gX_MotionPath_SegmentLength(waypoints, segment);
    gX_F32 t = length > 0.0001f ? remaining / length : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    gX_F32v3 a = gX_MotionPath_Waypoint_Get(waypoints, segment);
    gX_F32v3 b = gX_MotionPath_Waypoint_Get(waypoints, segment + 1);
    if (bezier && tangents) {
        gX_F32v3 handle0 = (gX_F32v3){tangents[segment * 3],
            tangents[segment * 3 + 1], tangents[segment * 3 + 2]};
        //opencode: the in-handle follows the out side unless the handles are
        //   authored independently.
        const gX_F32 *inHandle = (independentHandles && tangentsIn)
            ? &tangentsIn[(segment + 1) * 3]
            : &tangents[(segment + 1) * 3];
        gX_F32v3 handle1 = (gX_F32v3){inHandle[0], inHandle[1], inHandle[2]};
        *position = gX_MotionPath_Bezier_Point(a, b, handle0, handle1, t);
        return;
    }
    if (smooth) {
        gX_I32 i0 = segment > 0 ? segment - 1 : 0;
        gX_I32 i3 = segment + 2 < *waypointCount ? segment + 2 : segment + 1;
        *position = gX_MotionPath_CatmullRom(
            gX_MotionPath_Waypoint_Get(waypoints, i0), a, b,
            gX_MotionPath_Waypoint_Get(waypoints, i3), t);
        return;
    }
    *position = (gX_F32v3){a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}
