/**********************************************************************************************
*
*   [gX: src/rhi/rhi_event_log.c]
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


//opencode: RHI Event Log — ring buffer implementation.
//   Timestamp is wall-clock seconds via clock_gettime (no maindata dependency —
//   RHI must not depend on main.h).
#include "rhi_event_log.h"
#include <string.h>
#include <time.h>

static gX_RHI_Event g_events[GX_RHI_EVENT_LOG_SIZE];
static gX_U32 g_eventHead = 0;
static gX_U32 g_eventCount = 0;
static gX_U32 g_eventFrame = 0;

static gX_F64 now_seconds(gX_I0) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (gX_F64)ts.tv_sec + (gX_F64)ts.tv_nsec / 1e9;
}

gX_I0 gX_RHI_EventLog_Push(gX_RHI_EventType type,
    gX_U32 id0, gX_U32 id1, gX_U32 id2, const gX_I8 *label)
{
    gX_U32 idx = g_eventHead;
    g_events[idx].timestamp = now_seconds();
    g_events[idx].frameNumber = g_eventFrame;
    g_events[idx].type = type;
    g_events[idx].id0 = id0;
    g_events[idx].id1 = id1;
    g_events[idx].id2 = id2;
    if (label) {
        strncpy(g_events[idx].label, label, GX_RHI_EVENT_LABEL_LEN - 1);
        g_events[idx].label[GX_RHI_EVENT_LABEL_LEN - 1] = '\0';
    } else {
        g_events[idx].label[0] = '\0';
    }
    g_eventHead = (g_eventHead + 1) % GX_RHI_EVENT_LOG_SIZE;
    if (g_eventCount < GX_RHI_EVENT_LOG_SIZE) g_eventCount++;
}

gX_I0 gX_RHI_EventLog_NextFrame(gX_I0) { g_eventFrame++; }

gX_I0 gX_RHI_EventLog_Clear(gX_I0)
{
    g_eventHead = 0;
    g_eventCount = 0;
}

const gX_RHI_Event *gX_RHI_EventLog_Get(gX_U32 *outCount)
{
    if (outCount) *outCount = g_eventCount;
    return g_events;
}
